#!/usr/bin/env bash
# Interactive content shell with the native CharmOS window manager.
set -eu
build="$1"
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
work="$build/browser-work"
out="$build/chromium/linux-cache/release/chromium-155.0.8059.84/out/charmos-native"
mkdir -p "$work"
cat >"$work/index.html" <<'EOF'
<!doctype html>
<html><head><meta charset="utf-8"><title>CharmOS</title>
<style>
  body { margin: 0; padding: 48px; background: #101820; color: #e8eef2;
         font-family: sans-serif; }
  h1 { color: #4fd1c5; font-size: 42px; margin: 0 0 16px; }
  p { font-size: 20px; line-height: 1.5; }
  a { color: #63b3ed; }
</style></head>
<body>
  <h1>CharmOS browser</h1>
  <p>Chromium content shell is running with the native CharmOS window
     manager, Ozone platform and software compositor.</p>
  <p>Address bar, Back, Forward, Refresh and Stop are live.
     Try <a href="http://example.com">http://example.com</a>.</p>
  <p><input id="probe" size="40" autofocus
            placeholder="type here"></p>
  <script>document.getElementById('probe').focus();</script>
</body></html>
EOF
cat >"$work/init.conf" <<'EOF'
service logd /bin/logd
service wm /bin/wm
service browser /bin/content_shell --single-process --no-sandbox --disable-gpu --disable-in-process-stack-traces --js-flags=--jitless --blink-settings=imagesEnabled=false --user-data-dir=/data/content_shell http://example.com
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf"
      --put "2:/data/index.html=$work/index.html")
for p in init logd sh wm files calc edit wdemo cat ls echo mkdir rm sleep; do
  puts+=(--put "2:/bin/$p=$build/user/user/$p.elf")
done
binary="$out/content_shell.elf"
strip_tool="$out/../../third_party/llvm-build/Release+Asserts/bin/llvm-strip"
if [[ -x "$strip_tool" ]]; then
  "$strip_tool" --strip-all -o "$build/content_shell.stripped" "$binary"
  binary="$build/content_shell.stripped"
fi
puts+=(--put "2:/bin/content_shell=$binary")
for f in content_shell.pak icudtl.dat; do
  [[ -f "$out/$f" ]] && puts+=(--put "2:/data/content_shell/$f=$out/$f")
done
puts+=(--put "2:/fonts/ui.ttf=$root/assets/fonts/space-grotesk/SpaceGrotesk-Medium.ttf"
      --put "2:/fonts/ui-OFL.txt=$root/assets/fonts/space-grotesk/OFL.txt"
      --put "2:/fonts/mono.ttf=$root/assets/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf"
      --put "2:/fonts/mono-OFL.txt=$root/assets/fonts/jetbrains-mono/OFL.txt")
# Build the image on the native (ext4) temporary filesystem: mkdisk issues
# one write per 1 KiB block, which is prohibitively slow over the Windows
# drive mount. 512 MiB is enough for the stripped browser and assets.
image="$build/disk-browser.img"
tmp_image="${TMPDIR:-/tmp}/charmos-disk-browser.img"
rm -f "$tmp_image"
"$build/host/tools/mkdisk/mkdisk" "$tmp_image" 512 --esp 40 --root ext2 "${puts[@]}"
cp --sparse=always "$tmp_image" "$image"
rm -f "$tmp_image"
