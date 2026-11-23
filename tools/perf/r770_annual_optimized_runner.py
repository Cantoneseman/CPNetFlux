#!/usr/bin/env python3
"""Run serial R770 5 GiB admissions and interleaved comparisons."""
import json
import os
from pathlib import Path
import secrets
import shlex
import subprocess
import sys
import time

import r770_annual_optimized_node as node
import run_r770_receive_cost as rx

OUT = node.OUT
HOSTS = (1, 2)
GRID = ("grid", "private", "anon")
SECURITY_BLOCKS = (
    ("grid", "tls", "anon", "clear", "private"),
    ("clear", "private", "grid", "anon", "tls"),
    ("anon", "grid", "private", "tls", "clear"),
)
TOOL = str(Path(node.__file__).resolve())


def argv(host, *args):
    command = ["python3", "-B", TOOL, *map(str, args)]
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


def bounds(before, after, observations):
    errors, deltas, metrics = {}, {}, {}
    for host in HOSTS:
        a,b = before[host],after[host]
        readings = [a,*observations.get(host, []),b]
        network, delta = rx.comparable(a,b)
        extra = [reason for sample in readings for reason in rx.violations(a,sample)]
        errors[host] = sorted(set(network + extra))
        deltas[host] = delta
        metrics[host] = {
            "mem_available_min_kib":min(s["available_kib"] for s in readings),
            "mem_free_min_kib":min(s["free_kib"] for s in readings),
            "cpu_seconds":b["host_busy_cpu_s"]-a["host_busy_cpu_s"],
            "tcp_retrans":b["tcp_retrans"]-a["tcp_retrans"],
            "swapout_delta":b["pswpout"]-a["pswpout"],
            "memory_full_stall_us":b["psi_full_us"]-a["psi_full_us"],
            "inflight_observations":len(observations.get(host, [])),
        }
    return errors, deltas, metrics


def idle():
    start=both("snapshot")
    time.sleep(2)
    end=both("snapshot")
    failures, _, _=bounds(start,end,{})
    if any(failures.values()):
        raise RuntimeError("idle gates: " + str(failures))
    return {"before":start,"after":end}


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


def validate_cp(row, receipt, mode, sender, receiver, size):
    s=row["summary"]
    tls=mode=="tls"
    fields = {"result":"pass","bytes_transferred":size,"completed_files":1,"failed_files":0,
              "actual_transport":"v3","data_transport":"tcp","data_connect_count":4,
              "compression_policy":"off","control_tls_verified":tls,"data_tls_verified":tls}
    if any(s.get(k)!=v for k,v in fields.items()):
        raise ValueError("CPNetFlux summary policy or terminal mismatch: " + str({k:s.get(k) for k in fields}))
    local, peer = (receiver,sender) if mode == "mmap" else (sender,receiver)
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


def transfer(label, mode, sender, receiver):
    paths={host:OUT/f"{label}.r{host}.watch.jsonl" for host in HOSTS}
    watchers={}
    try:
        for host,path in paths.items():
            with path.open("x") as stream:
                watchers[host]=subprocess.Popen(argv(host,"watch"), stdout=stream, stderr=subprocess.DEVNULL)
        until=time.monotonic()+5
        while not all(path.stat().st_size for path in paths.values()):
            if time.monotonic()>until or any(p.poll() is not None for p in watchers.values()):
                raise RuntimeError("resource watcher startup")
            time.sleep(.025)
        if mode in GRID:
            return call(1,"grid-client",mode,label,sender,receiver)
        client, server = (receiver,sender) if mode=="mmap" else (sender,receiver)
        return call(client,"cp-client",mode,label,server)
    finally:
        for proc in watchers.values():
            proc.terminate()
        for proc in watchers.values():
            proc.wait(timeout=5)


def sample(group, index, mode, sender, expected):
    receiver=3-sender
    label=f"{group}-{index:02}-{mode}"
    row = dict(group=group,index=index,label=label,mode=mode,sender=sender,receiver=receiver,
               payload_bytes=expected["bytes"],decision="rejected")
    server = sender if mode=="mmap" else receiver
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
        readings={}
        for host in HOSTS:
            path=OUT/f"{label}.r{host}.watch.jsonl"
            readings[host]=[json.loads(s) for s in path.read_text().splitlines() if s.endswith("}")]
        errors,deltas,metrics=bounds(before,after,readings)
        row.update(immediate=immediate,after=after,gate_errors=errors,interface_deltas=deltas,metrics=metrics)
        if mode in GRID:
            receipts={sender:call(sender,"grid-receipt",mode,label,"RETR"),
                      receiver:call(receiver,"grid-receipt",mode,label,"STOR")}
        else:
            receipts=call(server,"cp-receipt",label)
        row["receipts"]=receipts
        if result["returncode"]!=0:
            raise ValueError("client returned " + str(result["returncode"]))
        if mode in GRID:
            validate_grid(result,receipts,mode,sender,receiver,expected["bytes"])
        else:
            validate_cp(result,receipts,mode,sender,receiver,expected["bytes"])
        actual=call(receiver,"sha",mode,label,expected["bytes"])
        row["target"]=actual
        if (actual["sha256"],actual["bytes"])!=(expected["sha256"],expected["bytes"]):
            raise ValueError("target SHA or size is wrong")
        if any(errors.values()) or any(not x for x in readings.values()):
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
        for index, mode in enumerate(samples,1):
            if not sample(group,index,mode,sender,expected):
                return False
        return True
    finally:
        if not call(sender,"remove","source"):
            raise RuntimeError("source still exists")


def main():
    commit=sys.argv[1]
    builds=both("receipt",commit)
    if builds[1]!=builds[2]:
        raise RuntimeError("dual build mismatch")
    record("matching-builds",builds)
    initial=both("identity")
    record("preflight",{"identity":initial,"idle":idle()})
    prepared = []
    outcome = {}
    try:
        for host in HOSTS:
            call(host,"prepare")
            prepared.append(host)
        credentials()
        for host in HOSTS:
            for mode in ("grid","anon"):
                call(host,"service",mode,mode+"-listener")
        for sender in (2,1):
            if not sequence(f"gsi-smoke-r{sender}",sender,("grid",),node.SMOKE):
                raise RuntimeError("bidirectional authenticated smoke failed")
        first_phase="867c60a763149bef989bd4f23ee180ee83022256ed1518841f6ee2e41b45f8c0"
        main_ok=True
        for sender in (2,1):
            admission=("grid",) if sender==2 else ("grid","mmap")
            main_ok=sequence(f"main-admit-r{sender}",sender,admission,node.SIZE,first_phase)
            if not main_ok:
                break
        if main_ok:
            for sender in (2,1):
                ordered=tuple(arm for i in range(5) for arm in
                              (("mmap","grid") if (i+sender)%2==0 else ("grid","mmap")))
                main_ok=sequence(f"main-formal-r{sender}",sender,ordered,node.SIZE,first_phase)
                if not main_ok:
                    break
        outcome["main"]="accepted" if main_ok else "stopped"
        modes=SECURITY_BLOCKS[0]
        security_ok=sequence("security-admit",2,modes,node.SIZE,first_phase)
        if security_ok:
            security_ok=sequence("security-formal",2,sum(SECURITY_BLOCKS,()),node.SIZE,first_phase)
        outcome["security"]="accepted" if security_ok else "stopped"
    except Exception as error:
        outcome["error"]=str(error)
        raise
    finally:
        clean={}
        for host in prepared:
            try:
                clean[host]=call(host,"clean")
            except Exception as error:
                clean[host]={"error":str(error)}
        record("postflight",{"outcome":outcome,"cleanup":clean,"snapshot":both("snapshot"),
                             "identity_matches": {h:call(h,"identity")==initial[h] for h in HOSTS}})
    return 0 if outcome=={"main":"accepted","security":"accepted"} else 2


if __name__=="__main__":
    os.umask(0o077)
    sys.exit(main())
