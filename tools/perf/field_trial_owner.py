"""Identify the stage-owned persistent source without matching nearby paths."""
from pathlib import PurePosixPath


def is_private_source_command(cmdline, private_source_root):
    args = cmdline.rstrip(b"\0").split(b"\0")
    if not args or PurePosixPath(args[0].decode(errors="replace")).name != "cpnetflux-gridftp-server":
        return False
    positions = [i for i, value in enumerate(args) if value == b"--root"]
    if len(positions) != 1 or positions[0] + 1 >= len(args):
        return False
    return args[positions[0] + 1] == str(private_source_root).encode()
