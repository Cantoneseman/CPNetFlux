# V3 Receiver Verification Semantics

Date: 2026-11-05. Input: 330b959d4d30b5e8b63cf049f3a8d61b5fc1bc31.

The receiver-policy regression exposed two gaps in the prior planning assumptions. TransferSession::verifyTempChunks and DownloadSession::verifyTempChunks immediately return for checksum=None, even when FileTransaction::commit selects its full branch. Thus "checksum none forces full readback" in earlier notes/reports does not describe actual IO or a content integrity guarantee. A deterministic regression waits for a persisted complete chunk, truncates the temporary inode, then sends FILE_END; the input revision's no-checksum receiver publishes it successfully. Source d323f1fa adds the physical-size guard and passes this regression.

Decision: preserve existing policy defaults and CRC behavior, and add an unconditional physical file-size check after flushing and before publication. Complete logical coverage and actual size must both match. This guard applies to full and verified_chunks, upload and download, checksum and no-checksum. It does not detect same-size external content changes or replace CRC verification.

JSON identifies the local download receiver versus an unobserved remote upload receiver. With CRC, effective policies remain full or verified_chunks; full performs post-write checksum reread and verified_chunks uses complete previously verified chunks. With checksum=None, effective verification is coverage_only and post_write_readback=false, regardless of the requested final policy. No checksum-free reread is introduced: reading bytes with no reference digest would not prove their content. No-checksum is unsuitable for integrity-qualified performance claims and is excluded from the controlled comparison.

The comparison retains CRC32C, equal commit-sync and checkpoint policy, external SHA-256 outside the task timer, and corruption/recovery/publication regressions. This supersedes the incorrect no-checksum readback assumption in the prior result-lifetime report and the initial receiver-policy plan; the prior CRC32C performance samples remain valid.

The receiver_policy JSON describes the configured normal commit path. Existing resume/skip and transport-failure integrity audits can still read and verify prior output data under verified_chunks. It does not promise that every recovery path performs no disk reads. External SHA in this experiment does not become a built-in safeguard or part of either task-rate timer.
