"""Semantic oracle for a deliberately invalid-token field-trial attempt."""


def validate_auth_rejection(*, returncode, summary, published_files, deadline_expired=False):
    if not isinstance(returncode, int) or returncode <= 0 or deadline_expired:
        raise ValueError("expected normal client failure, not success, signal or deadline")
    if not isinstance(summary, dict) or summary.get("result") != "fail":
        raise ValueError("missing failed structured result")
    error = summary.get("error")
    if not isinstance(error, dict) or error.get("error_code") != "auth_failed":
        raise ValueError("failure must be authentication rejection, not TLS/transport failure")
    if any(summary.get(k) != 0 for k in ("bytes_total", "bytes_transferred", "file_count")):
        raise ValueError("authentication rejection must precede inventory and payload")
    if not isinstance(published_files, (list, tuple, dict)) or published_files:
        raise ValueError("external inventory must prove no file was published")
    return dict(passed=True, error_code="auth_failed", zero_payload=True, no_publication=True,
                diagnostic_message=error.get("message", ""), predicate="structured_auth_v1")
