from __future__ import annotations

PASSIVE_DATA_PORT_WINDOW_SIZE = 512
MAX_PASSIVE_DATA_PORT_BASE = 65535 - PASSIVE_DATA_PORT_WINDOW_SIZE + 1


def clamp_passive_data_port_base(port: int) -> int:
    if port < 1:
        raise ValueError("passive data port base must be positive")
    return min(port, MAX_PASSIVE_DATA_PORT_BASE)


def passive_data_port_window_end(base: int) -> int:
    if base < 1 or base > MAX_PASSIVE_DATA_PORT_BASE:
        raise ValueError("passive data port base must fit a 512-port window")
    return base + PASSIVE_DATA_PORT_WINDOW_SIZE - 1


def assert_epsv_port_in_window(port: int, base: int) -> None:
    end = passive_data_port_window_end(base)
    if port < base or port > end:
        raise RuntimeError(f"EPSV port {port} is outside passive window {base}..{end}")
