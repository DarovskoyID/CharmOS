from pathlib import Path
import re
import sys

build = Path(sys.argv[1])
data = (build / 'minimize-hidden.ppm').read_bytes()
magic, dimensions, maximum, pixels = data.split(b'\n', 3)
assert magic == b'P6' and maximum == b'255'
width, height = map(int, dimensions.split())
assert len(pixels) == width * height * 3
for x, y, window_color in [(90, 150, bytes.fromhex('0a3ae7')), (400, 330, bytes.fromhex('ff8a00'))]:
    offset = (y * width + x) * 3
    color = pixels[offset:offset + 3]
    assert color != window_color, f'hidden window still visible at {x},{y}'
    assert max(color) < 50, f'expected dark wallpaper at {x},{y}, got {color.hex()}'
# The script also works when test.sh is run against a temporary phase.
logs = sorted(build.glob('test-*-minimize.serial'), key=lambda p: p.stat().st_mtime)
text = logs[-1].read_text(errors='replace')
hidden = text.split('wm: window 1 minimized', 1)[1].split('wm: launcher open', 1)[0]
assert 'wdemo amber: key' not in hidden and 'wdemo ember: key' not in hidden
for title in ('amber', 'ember'):
    assert re.search(rf'wdemo {title}: \d+ frames', hidden), f'hidden {title} stopped producing frames'
print('minimize: hidden pixels, input isolation and background frames verified')
