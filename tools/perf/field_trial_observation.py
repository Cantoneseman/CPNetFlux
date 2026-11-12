"""Keep observed image history without changing a process's latest resource snapshot."""


def merge_process_observation(previous, current):
    identity = (current["pid"], current["start_ticks"])
    if previous is not None and (previous["pid"], previous["start_ticks"]) != identity:
        raise ValueError("cannot merge observations across process identities")
    images = set()
    if previous is not None:
        images.update(previous.get("observed_executables", []))
        if previous.get("executable"):
            images.add(previous["executable"])
    if current.get("executable"):
        images.add(current["executable"])
    return dict(current, observed_executables=sorted(images))


def executable_was_observed(processes, executable):
    return any(executable == p.get("executable") or executable in p.get("observed_executables", [])
               for p in processes.values())
