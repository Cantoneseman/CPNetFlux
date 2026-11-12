"""Offline pacing contract for the frozen plain/TLS field-trial profiles.

These are experiment settings, not production defaults. Validation has no I/O.
"""

APPLICATION = "CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND"
KERNEL = "CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND"


def validate_sender_profile(environment, *, tls):
    application = environment.get(APPLICATION, "off")
    kernel = environment.get(KERNEL, "off")
    if application not in ("off", "1500000000"):
        raise ValueError("unsupported application pacing profile")
    if kernel not in ("off", "2000000000"):
        raise ValueError("unsupported kernel pacing profile")
    if tls and (application != "off" or kernel != "off"):
        raise ValueError("TLS requires both experimental plain-TCP pacers off")
    if application != "off" and kernel != "off":
        raise ValueError("application and kernel pacing are mutually exclusive")


def sender_pacing_environment(*, tls):
    environment = {APPLICATION: "off" if tls else "1500000000", KERNEL: "off"}
    validate_sender_profile(environment, tls=tls)
    return environment
