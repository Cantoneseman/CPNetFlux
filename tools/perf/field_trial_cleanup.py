"""Archive one explicitly owned root metadata file before experiment cleanup."""
import hashlib
import re
from pathlib import Path


def preserve_tree_manifest(path, *, payload_root, evidence_root):
    path, payload_root, evidence_root = map(Path, (path, payload_root, evidence_root))
    if payload_root.is_symlink() or path.parent != payload_root:
        raise ValueError("metadata must be an immediate child of owned payload root")
    if not re.fullmatch(r"dst-[A-Za-z0-9-]+\.cpnetflux\.tree\.download\.manifest", path.name):
        raise ValueError("unexpected root metadata filename")
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 64 * 1024**2:
        raise ValueError("metadata must be a bounded regular file")
    if evidence_root.is_symlink() or evidence_root.resolve().is_relative_to(payload_root.resolve()):
        raise ValueError("evidence directory must be outside payload")
    evidence_root.mkdir(parents=True, exist_ok=True)
    retained = evidence_root / path.name
    data = path.read_bytes()
    with retained.open("xb") as handle:
        handle.write(data)
    digest = hashlib.sha256(data).hexdigest()
    if hashlib.sha256(retained.read_bytes()).hexdigest() != digest:
        raise RuntimeError("metadata archive hash mismatch; source preserved")
    path.unlink()
    return dict(removed=str(path), retained_metadata=str(retained), bytes=len(data), sha256=digest)
