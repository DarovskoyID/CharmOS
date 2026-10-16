from pathlib import Path
import sys

data = (Path(sys.argv[1]) / 'ui-desktop-clean.ppm').read_bytes()
magic, dimensions, maximum, pixels = data.split(b'\n', 3)
assert magic == b'P6' and maximum == b'255'
width, height = map(int, dimensions.split())
assert (width, height) == (1280, 800) and len(pixels) == width * height * 3
def color(x, y):
    offset = (y * width + x) * 3
    return pixels[offset:offset + 3]

# Reference endpoints from Main.dc.html's SVG, translated to the desktop.
assert color(706, 320).hex() == 'e0352b', 'red hand endpoint'
assert color(706, 396).hex() == 'f5c518', 'rounded lower arc endpoint'
# The old horizontal cut left an amber ledge beyond the round endpoint.
assert max(color(715, 397)) < 50, 'logo has a ledge outside the SVG arc cap'
print('logo: SVG endpoints and clean arc opening verified')
