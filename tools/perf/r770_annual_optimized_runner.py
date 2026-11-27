#!/usr/bin/env python3
"""Run serial R770 5 GiB admissions and interleaved comparisons."""
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import subprocess
import sys
import time

import r770_annual_optimized_node as node
import run_r770_receive_cost as rx
import r770_tcp_diag as diag

OUT = node.OUT
HOSTS = (1, 2)
GRID = ("grid", "private", "anon")
SECURITY_BLOCKS = (
    ("grid", "tls", "anon", "clear", "private"),
    ("clear", "private", "grid", "anon", "tls"),
    ("anon", "grid", "private", "tls", "clear"),
)
FORWARD_ORDER = ("grid", "mmap", "mmap", "grid", "grid", "mmap", "mmap", "grid", "grid", "mmap")
REVERSE_ORDER = ("mmap", "grid", "grid", "mmap", "mmap", "grid", "grid", "mmap", "mmap", "grid")
TOOL = str(Path(node.__file__).resolve())


def argv(host, *args):
    command = ["env", "CPNETFLUX_ANNUAL_RUN=" + node.RUN, "python3", "-B", TOOL, *map(str, args)]
    if host == 1:
        return command
    return ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "r770-2", shlex.join(command)]


def call(host, *args, data=None):
    proc = subprocess.run(argv(host, *args), input=None if data is None else json.dumps(data),
                          text=True, capture_output=True, timeout=95)
    if proc.returncode:
        raise RuntimeError(f"R{host} {args[0]} exit {proc.returncode}: {proc.stderr[-1800:]}")
    return json.loads(proc.stdout)


def both(*args):
    return {host: call(host, *args) for host in HOSTS}


def record(name, value):
    with (OUT/(name + ".json")).open("x") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def credentials():
    work = node.CRED
    requests = {host: call(host, "request-cert") for host in HOSTS}
    ca, cakey = work/"ca.pem", work/"ca.key"
    common = "/O=CPNetFlux Annual Test/CN=R770 Test CA"
    node.cmd(["openssl", "req", "-new", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
              "-subj", common, "-addext", "basicConstraints=critical,CA:TRUE",
              "-addext", "keyUsage=critical,keyCertSign,cRLSign",
              "-keyout", str(cakey), "-out", str(ca)])
    token = secrets.token_urlsafe(32)
    user = "/O=CPNetFlux Annual Test/CN=R770 Test Transfer"
    usercsr, userkey, usercert = (work/name for name in ("user.csr","user.key","user.pem"))
    node.cmd(["openssl","req","-new","-newkey","rsa:2048","-nodes","-subj",user,
              "-keyout",str(userkey),"-out",str(usercsr)])
    def sign(csr, cert, extensions):
        extfile = work/"extensions"
        extfile.write_text(extensions)
        node.cmd(["openssl","x509","-req","-in",str(csr),"-CA",str(ca),"-CAkey",str(cakey),
                  "-set_serial",str(secrets.randbits(127) + 1),"-days","1","-sha256",
                  "-extfile",str(extfile),"-out",str(cert)])
    sign(usercsr,usercert,"basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=clientAuth\n")
    policy = (f"access_id_CA X509 '{common}'\npos_rights globus CA:sign\n"
              "cond_subjects globus '\"/O=CPNetFlux Annual Test/CN=*\"'\n")
    registered = {}
    for host, request in requests.items():
        file = work/f"host-{host}.csr"
        file.write_text(request)
        crt = work/f"host-{host}-signed.pem"
        sign(file,crt,"basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
             f"extendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=IP:192.168.100.{host},DNS:r770-{host}.annual-test.cpnetflux.invalid\n")
        registered[host] = call(host,"install-cert",data={"ca.pem":ca.read_text(),"host.pem":crt.read_text(),
            "policy":policy,"gridmap":f'"{user}" Su\n',"token":token})
    proxy = work/"proxy.pem"
    node.cmd(["grid-proxy-init","-q","-rfc","-verify","-valid","6:00","-cert",str(usercert),
              "-key",str(userkey),"-certdir",str(work/"trust"),"-out",str(proxy)])
    env = node.grid_env()
    env["X509_USER_PROXY"] = str(proxy)
    life = int(node.cmd(["grid-proxy-info","-f",str(proxy),"-timeleft"],env=env))
    if life < 3600:
        raise RuntimeError("short GSI identity lifetime")
    record("authentication", {"server_certificates":registered,"proxy_timeleft_seconds":life,
        "proxy_type":node.cmd(["grid-proxy-info","-f",str(proxy),"-type"],env=env),
        "ca_fingerprint":node.cmd(["openssl","x509","-in",str(ca),"-noout","-fingerprint","-sha256"]),
        "gridmap_account":"Su"})


def bounds(before, after, observations, require_resource_observations=False):
    errors, deltas, metrics = {}, {}, {}
    for host in HOSTS:
        start = before[host]
        end = after[host]
        observed = observations.get(host, [])
        readings = [start, *(item.get("resource", item) for item in observed), end]
        network, delta = rx.comparable(start, end)
        extra = [reason for sample in readings for reason in rx.violations(start, sample)]
        errors[host] = sorted(set(network + extra))
        deltas[host] = delta
        metrics[host] = {
            "mem_available_min_kib": min(item["available_kib"] for item in readings),
            "mem_free_min_kib": min(item["free_kib"] for item in readings),
            "cpu_seconds": end["host_busy_cpu_s"] - start["host_busy_cpu_s"],
            "tcp_retrans_diagnostic_only": end["tcp_retrans"] - start["tcp_retrans"],
            "swapout_delta": end["pswpout"] - start["pswpout"],
            "memory_full_stall_us": end["psi_full_us"] - start["psi_full_us"],
            "resource_observations": sum("resource" in item for item in observed),
        }
        if require_resource_observations and not metrics[host]["resource_observations"]:
            errors[host].append("resource watcher produced no samples")
    return errors, deltas, metrics


def _watch_socket_rows(records):
    rows = []
    for record in records:
        if record.get("kind") == "sample":
            rows.extend(record.get("sockets", []))
        elif record.get("kind") == "destroy" and record.get("socket"):
            rows.append(record["socket"])
    return rows


def socket_gate(records, host, label, expected_data_streams=4):
    errors = []
    ready = [item for item in records if item.get("kind") == "ready"]
    stopped = [item for item in records if item.get("kind") == "stopped"]
    observer_errors = [item for item in records if item.get("kind") == "observer_error"]

    if len(ready) != 1:
        errors.append("socket observer start handshake missing or duplicated")
    elif (ready[0].get("handshake") != "start" or ready[0].get("observer") != "NETLINK_SOCK_DIAG"
          or ready[0].get("host") != host or ready[0].get("run_id") != node.RUN
          or ready[0].get("label") != label or not ready[0].get("observer_id")):
        errors.append("socket observer start handshake does not match this sample")

    if len(stopped) != 1:
        errors.append("socket observer terminal handshake missing or duplicated")
    elif (stopped[0].get("handshake") != "stop" or stopped[0].get("observer") != "NETLINK_SOCK_DIAG"
          or stopped[0].get("host") != host or stopped[0].get("run_id") != node.RUN
          or stopped[0].get("label") != label or not stopped[0].get("observer_id")):
        errors.append("socket observer terminal handshake does not match this sample")
    if len(ready) == 1 and len(stopped) == 1:
        if ready[0].get("observer_id") != stopped[0].get("observer_id"):
            errors.append("socket observer start/stop identities differ")
        if stopped[0].get("event_loss_count") != 0:
            errors.append("socket observer reported kernel event loss")
        if stopped[0].get("drain_grace_seconds", 0) < 0.5:
            errors.append("socket observer terminal drain grace was incomplete")
    if observer_errors:
        errors.append("socket observer reported an error")
    if not any(item.get("kind") == "sample" for item in records):
        errors.append("socket observer produced no snapshots")

    socket_rows = _watch_socket_rows(records)
    history = {}
    terminal_rows = {}
    for record in records:
        if record.get("kind") == "destroy" and record.get("socket"):
            socket = record["socket"]
            if diag.is_tcp_flow_socket(socket) and diag.is_experiment_socket(socket):
                terminal_rows[diag.socket_key(socket)] = socket

    socket_rows = [row for row in socket_rows if diag.is_tcp_flow_socket(row)]
    for row in sorted(socket_rows, key=lambda item: item.get("observed_at_ns", 0)):
        if not diag.is_experiment_socket(row):
            errors.append("observer included a socket outside the experiment filter")
            continue
        key = diag.socket_key(row)
        if key[0] in (None, "", "0000000000000000", "ffffffffffffffff"):
            errors.append("experimental socket has no usable socket cookie")
        if (not row.get("tcp_info_present") or row.get("retrans_total") is None
                or row.get("bytes_retrans") is None):
            errors.append("experimental socket lacks TCP_INFO retransmission counters")
            continue
        prior = history.get(key, (0, 0))
        retrans_delta = row["retrans_total"] - prior[0]
        bytes_delta = row["bytes_retrans"] - prior[1]
        if retrans_delta < 0 or bytes_delta < 0:
            errors.append("socket TCP_INFO retransmission counter regressed")
        elif retrans_delta or bytes_delta:
            errors.append(
                "socket retransmission {}->{} retrans_total +{}, bytes_retrans +{}".format(
                    row["local_tuple"], row["peer_tuple"], retrans_delta, bytes_delta))
        history[key] = (max(prior[0], row["retrans_total"]),
                        max(prior[1], row["bytes_retrans"]))

    if not history:
        errors.append("no experiment TCP sockets observed")

    data_rows = {}
    for row in socket_rows:
        if diag.is_experiment_socket(row) and diag.is_data_socket(row):
            key = diag.socket_key(row)
            previous = data_rows.get(key)
            if previous is None or row.get("observed_at_ns", 0) >= previous.get("observed_at_ns", 0):
                data_rows[key] = row

    data_keys = set(data_rows)
    if len(data_keys) != expected_data_streams:
        errors.append("expected {} distinct data sockets, observed {}".format(
            expected_data_streams, len(data_keys)))
    missing_terminal = data_keys - set(terminal_rows)
    if missing_terminal:
        errors.append("{} data socket destroy terminal(s) missing".format(len(missing_terminal)))

    data_flows = []
    for key, row in sorted(data_rows.items(), key=lambda item: (
            item[1]["local_ip"], item[1]["local_port"],
            item[1]["peer_ip"], item[1]["peer_port"])):
        terminal = terminal_rows.get(key)
        data_flows.append({
            "socket_cookie": row["socket_cookie"],
            "local_ip": row["local_ip"],
            "local_port": row["local_port"],
            "peer_ip": row["peer_ip"],
            "peer_port": row["peer_port"],
            "local_tuple": row["local_tuple"],
            "peer_tuple": row["peer_tuple"],
            "terminal_event": terminal,
        })

    return {
        "host": host,
        "errors": sorted(set(errors)),
        "socket_count": len(history),
        "destroy_event_count": len(terminal_rows),
        "data_flows": data_flows,
        "observer_ready": len(ready) == 1,
        "observer_stopped": len(stopped) == 1,
        "event_loss_count": stopped[0].get("event_loss_count") if len(stopped) == 1 else None,
    }


def socket_pair_errors(gates):
    local = {
        (row["local_ip"], row["local_port"], row["peer_ip"], row["peer_port"])
        for row in gates[1]["data_flows"]
    }
    peer_reversed = {
        (row["peer_ip"], row["peer_port"], row["local_ip"], row["local_port"])
        for row in gates[2]["data_flows"]
    }
    if local != peer_reversed:
        return ["R1/R2 data socket 4-tuple coverage does not match"]
    return []

def idle(seconds=6):
    start=both("snapshot")
    time.sleep(seconds)
    end=both("snapshot")
    failures, _, _ = bounds(start, end, {})
    if any(failures.values()):
        raise RuntimeError("idle gates: " + str(failures))
    for host in HOSTS:
        rx.require_capacity(end[host], node.SIZE)
    return {"minimum_idle_seconds":seconds,"before":start,"after":end}


def prepare_preflight(commit):
    if OUT.exists():
        raise FileExistsError("annual evidence directory already exists: " + str(OUT))
    availability = both("run-available")
    if any(not availability[host]["available"] for host in HOSTS):
        raise FileExistsError("annual run path, payload root or credentials already exists")
    initial = both("identity")
    builds = both("receipt", commit)
    if builds[1] != builds[2]:
        raise RuntimeError("dual build mismatch")
    idle_state = idle()
    OUT.mkdir(mode=0o700, parents=True)
    record("matching-builds", builds)
    record("preflight", {"identity": initial, "idle": idle_state})
    return initial


def validate_grid(result, receipts, mode, sender, target, size):
    who = "anonymous" if mode == "anon" else "Su"
    markers = result["markers"]
    for host, peer, direction in ((sender,target,"RETR"),(target,sender,"STOR")):
        rows=receipts[host]
        if len(rows)!=1:
            raise ValueError(f"R{host} missing {direction} transfer receipt")
        actual=rows[0]
        fields = {"TYPE":direction,"USER":who,"CODE":"226","NBYTES":str(size),"STREAMS":"4",
                  "BLOCK":"262144","DEST":f"[192.168.100.{peer}]"}
        if any(actual.get(k)!=v for k,v in fields.items()):
            raise ValueError(f"R{host} incorrect transfer receipt")
    if markers.count(f"230 User {who} logged in")!=2 or markers.count("226 Transfer Complete")!=2:
        raise ValueError("missing dual login or completion")
    if markers.count("200 Mode set to E")!=2:
        raise ValueError("missing MODE E on either side")
    if mode == "private":
        if markers.count("200 Protection level set to P")!=2 or "PROT C" in markers:
            raise ValueError("missing PRIVATE data acknowledgement")
    elif "PROT P" in markers or "200 Protection level set to P" in markers or (
            mode == "grid" and markers.count("200 DCAU N")!=2):
        raise ValueError("unexpected protection or missing clear mode acknowledgement")


def validate_cp(row, receipt, mode, sender, receiver, size, service_receipt=None):
    s=row["summary"]
    tls=mode=="tls"
    fields = {"result":"pass","bytes_transferred":size,"completed_files":1,"failed_files":0,
              "actual_transport":"v3","data_transport":"tcp","data_connect_count":4,
              "compression_policy":"off","control_tls_verified":tls,"data_tls_verified":tls}
    if any(s.get(k)!=v for k,v in fields.items()):
        raise ValueError("CPNetFlux summary policy or terminal mismatch: " + str({k:s.get(k) for k in fields}))
    local, peer = (receiver,sender) if node.cp_download_mode(mode) else (sender,receiver)
    lanes=s.get("lane_endpoints",[])
    if len(lanes)!=4 or not all(x["data_local_address"]==f"192.168.100.{local}" and
            x["data_peer_address"]==f"192.168.100.{peer}" for x in lanes):
        raise ValueError("CPNetFlux endpoint mismatch")
    if receipt["batch_error"] or sorted(receipt["channels"])!=list(range(4)) or receipt["events"].count("auth_success")!=4:
        raise ValueError("missing CPNetFlux server channel/auth terminal")
    if receipt["events"].count("tls_handshake_success")!=(4 if tls else 0):
        raise ValueError("unexpected server TLS mode")
    if mode == "mmap" and (any(x["tcp_receive_buffer_bytes"]!=2097152 for x in lanes) or
            s["receiver_policy"]["range_checkpoint_batch_chunks_effective"]!=16 or
            s["receiver_policy"]["final_verify_policy_effective"]!="verified_chunks"):
        raise ValueError("mmap receiver parameters differ")
    if node.IS_COMFORT and mode in node.CP_MODES:
        profile = node.comfort_profile(mode)
        if s.get("data_frame_logical_bytes") != profile["data_frame_bytes"]:
            raise ValueError("CPNetFlux comfort DATA frame mismatch")
        if any(x["tcp_receive_buffer_bytes"] < profile["tcp_receive_buffer_requested_bytes"] or
               x["tcp_receive_buffer_bytes"] > 2 * profile["tcp_receive_buffer_requested_bytes"]
               for x in lanes):
            raise ValueError("CPNetFlux receive buffer readback mismatch")
        if node.IS_DIAG_VALIDATION:
            ranges = [item for item in s.get("file_timings", []) if item.get("range_id")]
            if (sum(item.get("length", 0) for item in ranges) != size or
                    not all(item.get("receiver_payload_execution") == "inline" and
                            item.get("receiver_write_backend") == "mmap_experimental" and
                            item.get("mapped_write", {}).get("direct_receive_calls") == 0 and
                            item.get("mapped_write", {}).get("direct_received_bytes") == 0
                            for item in ranges)):
                raise ValueError("CPNetFlux buffered inline mmap diagnostic receipt mismatch")
        elif s.get("phase_timing_schema") is not None:
            raise ValueError("CPNetFlux formal sample unexpectedly enabled phase timing")
        if (s.get("receiver_policy", {}).get("range_checkpoint_batch_chunks_effective") != 16 or
                s.get("receiver_policy", {}).get("final_verify_policy_effective") != "verified_chunks"):
            raise ValueError("CPNetFlux chunk checkpoint or verification mismatch")
        pacing = receipt.get("pacing", [])
        if len(pacing) != 4 or {item.get("lane") for item in pacing} != set(range(4)) or any(
                item.get("rate_bytes_per_second") != 1500000000 or
                item.get("spacing_violations") != 0 for item in pacing):
            raise ValueError("CPNetFlux per-lane application pacing receipt mismatch")
    if node.IS_SECURITY_5G and mode in ("clear", "tls"):
        profile = node.security_5g_profile(mode)
        if s.get("data_frame_logical_bytes") != profile["data_frame_bytes"]:
            raise ValueError("CPNetFlux security DATA frame mismatch")
        applied = (service_receipt or {}).get("runtime_profile", {})
        if applied.get("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND") != str(
                profile["application_pacing_bytes_per_second_per_lane"]):
            raise ValueError("CPNetFlux security sender pacing configuration mismatch")
        lanes = s.get("lane_endpoints", [])
        if any(lane.get("tcp_receive_buffer_bytes", 0) < profile["tcp_receive_buffer_requested_bytes"] or
               lane.get("tcp_receive_buffer_bytes", 0) >
               2 * profile["tcp_receive_buffer_requested_bytes"] for lane in lanes):
            raise ValueError("CPNetFlux security receive buffer readback mismatch")
        client_profile = row.get("client_runtime_profile", {})
        if (client_profile.get("CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE") != "mmap" or
                client_profile.get("CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION") != "inline" or
                client_profile.get("CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE") != "off" or
                client_profile.get("CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES") != "1048576" or
                client_profile.get("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND") != "off"):
            raise ValueError("CPNetFlux security receiver configuration receipt mismatch")
        if (s.get("receiver_policy", {}).get("range_checkpoint_batch_chunks_effective") != 16 or
                s.get("receiver_policy", {}).get("final_verify_policy_effective") != "verified_chunks"):
            raise ValueError("CPNetFlux security checkpoint or verification policy mismatch")
        pacing = receipt.get("sender_pacing", [])
        if len(pacing) != 4 or {item.get("lane") for item in pacing} != set(range(4)) or any(
                item.get("rate_bytes_per_second") !=
                profile["application_pacing_bytes_per_second_per_lane"] or
                item.get("spacing_violations") != 0 or item.get("frames", 0) <= 0
                for item in pacing):
            raise ValueError("CPNetFlux security sender pacing receipt mismatch")


def read_watch_records(path, allow_partial=False):
    records = []
    content = path.read_text()
    lines = content.splitlines(keepends=True)
    for line_number, line in enumerate(lines, 1):
        if not line.endswith("\n"):
            if allow_partial and line_number == len(lines):
                break
            raise RuntimeError("unterminated socket watcher record at {}:{}".format(
                path, line_number))
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise RuntimeError("invalid socket watcher JSON at {}:{}".format(
                path, line_number)) from error
    return records


def wait_for_watcher_ready(host, path, process, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        records = read_watch_records(path, allow_partial=True) if path.exists() else []
        if any(item.get("kind") == "observer_error" for item in records):
            raise RuntimeError("R{} socket watcher failed before ready".format(host))
        ready = [item for item in records if item.get("kind") == "ready"]
        if ready:
            item = ready[0]
            if (item.get("host") != host or item.get("run_id") != node.RUN
                    or item.get("label") != path.name.split(".r{}".format(host))[0]):
                raise RuntimeError("R{} socket watcher start handshake mismatch".format(host))
            return item
        if process.poll() is not None:
            raise RuntimeError("R{} socket watcher exited before ready: {}".format(
                host, process.returncode))
        time.sleep(0.025)
    raise RuntimeError("R{} socket watcher ready timeout".format(host))


def _append_handshake(path, row):
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(row) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def transfer(label, mode, sender, receiver):
    paths = {host: OUT / "{}.r{}.watch.jsonl".format(label, host) for host in HOSTS}
    stderr_paths = {host: OUT / "{}.r{}.watch.stderr.log".format(label, host) for host in HOSTS}
    handshake_path = OUT / (label + ".watch-handshake.jsonl")
    watchers = {}
    streams = {}
    stderr_streams = {}
    transfer_result = None
    transfer_error = None
    started = {}
    stopped = {}
    stop_errors = []
    with handshake_path.open("x", encoding="utf-8") as handshake:
        handshake.write(json.dumps({
            "phase": "launch", "label": label, "run_id": node.RUN,
            "requested_at_ns": time.time_ns(),
        }) + "\n")
        handshake.flush()
        os.fsync(handshake.fileno())
    try:
        for host in HOSTS:
            streams[host] = paths[host].open("xb", buffering=0)
            stderr_streams[host] = stderr_paths[host].open("xb", buffering=0)
            watchers[host] = subprocess.Popen(
                argv(host, "watch", label), stdin=subprocess.DEVNULL,
                stdout=streams[host], stderr=stderr_streams[host])
        for host in HOSTS:
            started[host] = wait_for_watcher_ready(host, paths[host], watchers[host])
        _append_handshake(handshake_path, {
            "phase": "start", "label": label, "run_id": node.RUN,
            "ready_at_ns": time.time_ns(), "observers": started,
        })
        if mode in GRID:
            transfer_result = call(1, "grid-client", mode, label, sender, receiver)
        else:
            client, server = (receiver, sender) if node.cp_download_mode(mode) else (sender, receiver)
            transfer_result = call(client, "cp-client", mode, label, server)
    except Exception as error:
        transfer_error = error
    finally:
        for host in watchers:
            try:
                call(host, "watch-stop", label)
            except Exception as error:
                stop_errors.append("R{} stop request: {}".format(host, error))
        for host, process in watchers.items():
            try:
                process.wait(timeout=12)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                stop_errors.append("R{} socket observer did not stop cleanly".format(host))
            if process.returncode != 0:
                stop_errors.append("R{} socket observer exit {}".format(host, process.returncode))
        for stream in streams.values():
            stream.close()
        for stream in stderr_streams.values():
            stream.close()
        for host in watchers:
            if paths[host].exists():
                try:
                    records = read_watch_records(paths[host])
                    rows = [item for item in records if item.get("kind") == "stopped"]
                    if len(rows) == 1:
                        stopped[host] = rows[0]
                    if not any(item.get("kind") == "ready" for item in records):
                        stop_errors.append("R{} socket observer never completed start handshake".format(host))
                except Exception as error:
                    stop_errors.append("R{} socket evidence parse: {}".format(host, error))
        _append_handshake(handshake_path, {
            "phase": "stop", "label": label, "run_id": node.RUN,
            "stopped_at_ns": time.time_ns(), "observers": stopped,
            "stop_errors": stop_errors,
        })
        if stop_errors and transfer_error is None:
            transfer_error = RuntimeError("; ".join(stop_errors))
    if transfer_error is not None:
        raise transfer_error
    if transfer_result is None:
        raise RuntimeError("transfer client produced no result")
    transfer_result["socket_watch_paths"] = {host: str(path) for host, path in paths.items()}
    transfer_result["socket_watch_handshake_path"] = str(handshake_path)
    transfer_result["socket_observer_start"] = started
    transfer_result["socket_observer_stop"] = stopped
    return transfer_result

def sample(group, index, mode, sender, expected):
    receiver=3-sender
    label=f"{group}-{index:02}-{mode}"
    row = dict(group=group,index=index,label=label,mode=mode,sender=sender,receiver=receiver,
               payload_bytes=expected["bytes"],decision="rejected")
    server = sender if node.cp_download_mode(mode) else receiver
    if node.IS_SECURITY_5G and mode in ("clear", "tls"):
        row["profile"] = node.security_5g_profile(mode)
    row["socket_evidence_paths"] = {
        host: str(OUT / "{}.r{}.watch.jsonl".format(label, host)) for host in HOSTS
    }
    row["socket_handshake_path"] = str(OUT / (label + ".watch-handshake.jsonl"))
    if node.IS_COMFORT and mode in node.CP_MODES:
        row["profile"] = node.comfort_profile(mode)
    folder_ready = False
    try:
        rx.require_capacity(call(receiver,"snapshot"),expected["bytes"])
        call(receiver,"mkdir",label)
        folder_ready=True
        if mode not in GRID:
            row["service"] = call(server,"service",mode,label)
        before=both("snapshot")
        row["before"] = before
        result = transfer(label,mode,sender,receiver)
        row["result"] = result
        immediate=both("snapshot")
        time.sleep(.35)
        after=both("snapshot")
        watch_records = {}
        resource_readings = {}
        for host in HOSTS:
            path = OUT / f"{label}.r{host}.watch.jsonl"
            watch_records[host] = read_watch_records(path)
            resource_readings[host] = [item for item in watch_records[host]
                                       if item.get("kind") == "sample"]
        errors, deltas, metrics = bounds(
            before, after, resource_readings, require_resource_observations=True)
        socket_gates = {host: socket_gate(watch_records[host], host, label) for host in HOSTS}
        pair_errors = socket_pair_errors(socket_gates)
        for host in HOSTS:
            errors[host].extend(socket_gates[host]["errors"])
            if pair_errors:
                errors[host].extend(pair_errors)
        row.update(immediate=immediate, after=after, gate_errors=errors,
                   interface_deltas=deltas, metrics=metrics, socket_gate=socket_gates,
                   socket_pair_errors=pair_errors, socket_evidence_paths={
                       host: str(OUT / f"{label}.r{host}.watch.jsonl") for host in HOSTS})
        if mode in GRID:
            receipts={sender:call(sender,"grid-receipt",mode,label,"RETR"),
                      receiver:call(receiver,"grid-receipt",mode,label,"STOR")}
        else:
            receipts=call(server,"cp-receipt",label)
            if node.IS_SECURITY_5G and mode in ("clear", "tls"):
                receipts["sender_pacing"] = (receipts.get("pacing", [])
                                              if node.cp_download_mode(mode) else
                                              call(sender,"cp-client-receipt",label)["pacing"])
        row["receipts"]=receipts
        if result["returncode"]!=0:
            raise ValueError("client returned " + str(result["returncode"]))
        if mode in GRID:
            validate_grid(result,receipts,mode,sender,receiver,expected["bytes"])
        else:
            validate_cp(result,receipts,mode,sender,receiver,expected["bytes"],row.get("service"))
        actual=call(receiver,"sha",mode,label,expected["bytes"])
        row["target"]=actual
        if (actual["sha256"],actual["bytes"])!=(expected["sha256"],expected["bytes"]):
            raise ValueError("target SHA or size is wrong")
        if any(errors.values()):
            raise ValueError("memory/network gate: " + str(errors))
        if min(deltas[sender]["tx_bytes"],deltas[receiver]["rx_bytes"]) < expected["bytes"]:
            raise ValueError("interface did not carry logical payload")
        row["decision"]="accepted"
    except (RuntimeError,ValueError,subprocess.SubprocessError,OSError,KeyError) as error:
        row["error"]=str(error)
    finally:
        try:
            if mode not in GRID:
                call(server,"stop",label)
            if (not node.IS_COMFORT and mode in ("clear", "tls") and
                    not node.cp_download_mode(mode) and not call(sender, "remove-upload-manifest")):
                raise RuntimeError("sender upload manifest still exists")
            if folder_ready and not call(receiver,"remove",label):
                raise RuntimeError("target still exists")
        except Exception as error:
            row.update(decision="rejected",cleanup_error=str(error))
        if row["decision"]=="accepted":
            row["task_gbps"]=row["payload_bytes"]*8e-9/row["result"]["seconds"]
        record(label,row)
        event={k:row[k] for k in ("label","decision","payload_bytes","task_gbps","error","cleanup_error") if k in row}
        print(json.dumps(event),flush=True)
    return row["decision"]=="accepted"


def sequence(group, sender, samples, size, expected_sha=None):
    safety=idle()
    rx.require_capacity(safety["after"][sender],size)
    expected = call(sender,"seed",size)
    try:
        record(group+"-source",dict(sender=sender,payload_bytes=size,source=expected,idle=safety))
        if expected["violations"] or expected["bytes"]!=size or (expected_sha and expected["sha256"]!=expected_sha):
            raise RuntimeError("source integrity/resource check")
        all_accepted = True
        for index, mode in enumerate(samples,1):
            if not sample(group,index,mode,sender,expected):
                all_accepted = False
        return all_accepted
    finally:
        if not call(sender,"remove","source"):
            raise RuntimeError("source still exists")


def stage_run_names():
    if node.IS_DIAG_VALIDATION:
        return {"validate": node.COMFORT_DIAG_RUN}
    if node.RUN in node.COMFORT_RUNS:
        return {"security": node.COMFORT_SECURITY_RUN, "forward": node.COMFORT_FORWARD_RUN,
                "reverse": node.COMFORT_REVERSE_RUN}
    if node.RUN in node.ANNUAL_5G_RUNS:
        return {"security": node.ANNUAL_5G_SECURITY_RUN, "forward": node.ANNUAL_5G_FORWARD_RUN,
                "reverse": node.ANNUAL_5G_REVERSE_RUN}
    return {"security": node.SECURITY_RUN, "reverse": node.REVERSE_RUN}


def security_matrix_complete(rows, size):
    counts = {mode: sum(row.get("mode") == mode and row.get("payload_bytes") == size
                        for row in rows)
              for mode in SECURITY_BLOCKS[0]}
    valid_decisions = all(row.get("decision") in ("accepted", "rejected") for row in rows)
    return len(rows) == 15 and set(counts.values()) == {3} and valid_decisions


def require_security_complete(commit):
    security_run = stage_run_names()["security"]
    security = OUT.parent / security_run
    post = json.loads((security / "postflight.json").read_text())
    builds = json.loads((security / "matching-builds.json").read_text())
    rows = [json.loads(path.read_text()) for path in security.glob("security-formal-*.json")
            if re.fullmatch(r"security-formal-\d{2}-(grid|private|anon|clear|tls)\.json", path.name)]
    if (post["stage"] != "security" or post["status"] not in ("pass", "complete_with_rejections") or
            not security_matrix_complete(rows, node.SIZE) or builds["1"]["commit"] != commit):
        raise RuntimeError("complete independent security matrix is missing at the requested commit")


def main():
    stage, commit = sys.argv[1:3]
    expected_sha = sys.argv[3] if len(sys.argv) > 3 else None
    run_names = stage_run_names()
    if node.RUN != run_names[stage] or node.HOST != 1:
        raise ValueError("stage, controller node or run directory mismatch")
    if stage == "reverse":
        require_security_complete(commit)
    initial = prepare_preflight(commit)
    prepared = []
    outcome = {"stage": stage, "status": "stopped"}
    try:
        for host in HOSTS:
            call(host, "prepare")
            prepared.append(host)
        if stage == "validate":
            loopback = {host: call(host, "diag-selftest") for host in HOSTS}
            record("socket-diag-loopback", loopback)
        credentials()
        for host in HOSTS:
            for mode in (("grid", "anon") if stage == "security" else ("grid",)):
                call(host, "service", mode, mode + "-listener")
        smoke_r2 = ("grid", "private", "anon", "clear", "tls") if stage == "security" else ("grid", "mmap")
        smoke_r1 = ("grid", "mmap")
        if not sequence("smoke-r2", 2, smoke_r2, node.SMOKE) or not sequence("smoke-r1", 1, smoke_r1, node.SMOKE):
            raise RuntimeError("8 MiB authentication/data mode smoke stopped")
        if expected_sha is None and not node.IS_COMFORT:
            expected_sha = "867c60a763149bef989bd4f23ee180ee83022256ed1518841f6ee2e41b45f8c0"
        if stage == "validate":
            done = True
        elif stage == "security":
            done = sequence("security-formal", 1, sum(SECURITY_BLOCKS, ()), node.SIZE, expected_sha)
        elif stage == "forward":
            done = sequence("main-forward-formal", 2, FORWARD_ORDER, node.SIZE, expected_sha)
        else:
            done = sequence("main-reverse-formal", 1, REVERSE_ORDER, node.SIZE, expected_sha)
        outcome["status"] = "pass" if done else "complete_with_rejections"
    except Exception as error:
        outcome["error"] = str(error)
        print(str(error), file=sys.stderr, flush=True)
    finally:
        cleanup = {}
        for host in prepared:
            try:
                cleanup[host] = call(host, "clean")
            except Exception as error:
                cleanup[host] = {"error":str(error)}
        outcome["cleanup"] = cleanup
        try:
            outcome["identity_matches"] = {host: call(host, "identity") == initial[host] for host in HOSTS}
            outcome["idle_after_cleanup"] = idle()
            if (not all(outcome["identity_matches"].values()) or len(cleanup) != len(HOSTS) or
                    not all(all(receipt.get(key) for key in ("payload_removed", "credential_removed", "listeners_empty"))
                            for receipt in cleanup.values())):
                raise RuntimeError("cleanup or original interface/service identity check")
        except Exception as error:
            outcome["postflight_error"] = str(error)
            outcome["status"] = "stopped"
        record("postflight", outcome)
    return 0 if outcome["status"] in ("pass", "complete_with_rejections") else 2


if __name__ == "__main__":
    os.umask(0o077)
    sys.exit(main())
