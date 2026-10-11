#!/usr/bin/env python3
"""Gate: retired file wire/state engines cannot silently reappear."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
for folder in ('include','src'):
    for file in (root/folder).rglob('*'):
        if file.suffix not in ('.h','.cpp'): continue
        text=file.read_text()
        for forbidden in ('FrameType::SessionInit','FrameType::ChunkComplete',
                          'ChunkCompletePayload','encodeSessionInitPayload','decodeSessionInitPayload'):
            if forbidden in text: raise SystemExit(f'{file}: retired symbol {forbidden}')
for name in ('file_transfer_client','file_transfer_server','file_download_client','file_download_sender'):
    file=root/'src/core/io'/f'{name}.cpp'
    assert len(file.read_text().splitlines())<40,file
print('v1 retirement source gate passed')
