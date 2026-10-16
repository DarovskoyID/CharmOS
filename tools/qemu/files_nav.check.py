from pathlib import Path
import sys

logs = sorted(Path(sys.argv[1]).glob('test-*-files_nav.serial'), key=lambda p: p.stat().st_mtime)
text = logs[-1].read_text(errors='replace')
tail = text.split('files: directory /etc (', 1)[1]
assert 'files: directory ' not in tail, 'forward history was not discarded after a new navigation'
print('files: new navigation discards forward history')
