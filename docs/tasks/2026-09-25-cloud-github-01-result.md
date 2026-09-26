# CLOUD-GITHUB-01 reconciliation stage

Status: source/destination per-file comparison passed; waiting for independent 04 review. Migration gate is not finally accepted yet.

Source HEAD: a076c532640ba06de016ed7ed20f7d2a6d48a0a7. Shenzhen input: 2523956be60217df377306f3493fa749397662b4.
Using git ls-files -co --exclude-standard -z, each in-scope file was hashed on both sides. The result was 548 versus 548 with zero differences. Shenzhen has three cloud-only artifacts: migration marker, summary manifest, and cross-domain smoke result. None is part of migration source scope. GridFlux-Beta, CPSS(DCC), and science-compressor were not entered or changed.

Per-file manifest: docs/coordination/CLOUD-GITHUB-01-FILES.json (548 entries); SHA-256: f7f962862a7676147f36518600c21ba7edf0d39aa2b4365d4b5680d3f2bc699d.
Limit: this report relies on the Windows transition directory as a read-only comparison source. The Windows repository remains dirty, and the Shenzhen root import remains dirty. New development must use a clean Shenzhen task worktree.
