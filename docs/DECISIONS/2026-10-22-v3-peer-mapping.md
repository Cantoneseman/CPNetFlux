# V3 per-lane peer endpoints

2026-10-22: implement the approved remaining desktop backend scope. This decision
supersedes only the unsupported per-lane mapping boundary in the 2026-10-21 desktop
agent decision and API 3 handoff. Other unsupported capabilities remain explicit.

Use --peer-addresses for one shared or exactly link-set-links numeric destination
IPs. Select by logical lane for control, data and reconnect. Preserve logical TLS
identity independently of the destination IP. Observe connected socket addresses,
not just requested configuration. Multiple IPs must belong to one server/root;
this is not cross-server sharding or evidence of independent physical routes.
See docs/tasks/2026-10-22-v3-desktop-peer-mapping-01.md for acceptance.
