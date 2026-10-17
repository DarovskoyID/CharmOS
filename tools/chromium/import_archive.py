#!/usr/bin/env python3
"""Import the locked official release archive without running upstream code."""
import hashlib
import json
import lzma
import os
from pathlib import Path, PurePosixPath
import sys
import tarfile
import time

repo = Path(__file__).resolve().parents[2]
lock = json.loads((repo / 'tools/chromium/upstream.json').read_text())
version = lock['chromium']['version']
top = 'chromium-' + version
archive = Path(sys.argv[1]) if len(sys.argv) > 1 else repo / 'build/chromium' / (top + '.tar.xz')
cache = repo / 'build/chromium'
linux_cache = cache / 'linux-cache'
if not os.path.ismount(linux_cache):
    raise SystemExit('Use tools/chromium/with-cache.sh: source cache is not mounted')
destination = linux_cache / 'release'
report_path = cache / 'archive-import.json'
if sys.version_info < (3, 12):
    raise SystemExit('Python 3.12+ is required for tar data filters')
if archive.stat().st_size != lock['chromium']['archive']['bytes']:
    raise SystemExit('Archive size differs from the verified publisher metadata')
if destination.exists():
    raise SystemExit('Release destination exists; refusing to overwrite source files')

digest = hashlib.sha256()
with archive.open('rb') as stream:
    while chunk := stream.read(16 * 1024 * 1024):
        digest.update(chunk)
print('Archive SHA-256: ' + digest.hexdigest(), flush=True)
expected_digest = lock['chromium']['archive'].get('observed_sha256')
if expected_digest and digest.hexdigest() != expected_digest:
    raise SystemExit('Archive differs from the previously received release archive')
report = dict(archive=str(archive), version=version, bytes=archive.stat().st_size,
              sha256=digest.hexdigest(), status='extracting', files=0)
report_path.write_text(json.dumps(report, indent=2) + '\n')
destination.mkdir()
started = time.monotonic()
count = 0
try:
    # Drain the decompressor after tar EOF so the final XZ integrity check runs.
    with lzma.open(archive, 'rb') as decompressed:
        with tarfile.open(fileobj=decompressed, mode='r|') as source:
            for member in source:
                parts = PurePosixPath(member.name).parts
                if not parts or parts[0] != top or '..' in parts or member.name.startswith('/'):
                    raise ValueError('Unexpected archive path: ' + member.name)
                source.extract(member, path=destination, filter='data')
                count += 1
                if count % 5000 == 0:
                    print(f'Extracted {count} entries ({time.monotonic()-started:.0f}s)', flush=True)
                    report.update(files=count)
                    report_path.write_text(json.dumps(report, indent=2) + '\n')
        while decompressed.read(1024 * 1024):
            pass
    fields = {}
    for line in (destination / top / 'chrome/VERSION').read_text().splitlines():
        key, value = line.split('=', 1)
        fields[key] = value
    actual = '.'.join(fields[key] for key in ('MAJOR','MINOR','BUILD','PATCH'))
    if actual != version:
        raise ValueError('Archive version differs from lock: ' + actual)
    if not (destination / top / 'LICENSE').is_file():
        raise ValueError('Missing upstream license')
    report.update(status='complete', files=count, source=str(destination / top))
    print(f'Chromium {actual}: {count} entries imported; XZ integrity passed', flush=True)
except BaseException as error:
    report.update(status='incomplete', files=count, error=str(error))
    raise
finally:
    report_path.write_text(json.dumps(report, indent=2) + '\n')
