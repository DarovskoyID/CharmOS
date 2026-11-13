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
  <p lang="ru">Проверка русского текста: Привет, мир! Ёжик, ёлка, № 42.</p>
  <p>Greek: Ελληνικά. Symbols: € — «текст».</p>
  <p lang="ar" dir="rtl">مرحبا بالعالم</p>
  <p lang="zh">中文字体检查</p>
  <p><a href="https://www.youtube.com/watch?v=dQw4w9WgXcQ">YouTube video test</a></p>
  <script>document.getElementById('probe').focus();</script>
  <script>
    document.fonts.ready.then(() => requestAnimationFrame(() =>
      requestAnimationFrame(() => { document.title = 'CharmOS Unicode probe ready'; })));
  </script>
</body></html>
EOF
if [[ "${CHARMOS_FULL_CHROME:-0}" == 1 ]]; then
  browser_command='/bin/chrome --charmos-browser-window --single-process --no-sandbox --disable-gpu --disable-in-process-stack-traces --js-flags=--jitless --no-first-run --no-default-browser-check --user-data-dir=/data/chrome file:///data/index.html'
else
  browser_command='/bin/content_shell --charmos-browser-window --single-process --no-sandbox --disable-gpu --disable-in-process-stack-traces --js-flags=--jitless --user-data-dir=/data/content_shell file:///data/index.html'
fi
cat >"$work/init.conf" <<EOF
service logd /bin/logd
service wm /bin/wm
manual browser $browser_command
console sh /bin/sh
EOF
puts=(--put "2:/etc/init.conf=$work/init.conf"
      --put "2:/data/index.html=$work/index.html")
for p in init logd sh wm files calc edit wdemo cat ls echo mkdir rm sleep; do
  puts+=(--put "2:/bin/$p=$build/user/user/$p.elf")
done
if [[ "${CHARMOS_FULL_CHROME:-0}" == 1 ]]; then
  browser_name=chrome
  asset_dir=chrome
  assets=(resources.pak chrome_100_percent.pak chrome_200_percent.pak icudtl.dat)
else
  browser_name=content_shell
  asset_dir=content_shell
  assets=(content_shell.pak icudtl.dat)
fi
binary="$out/$browser_name.elf"
strip_tool="$out/../../third_party/llvm-build/Release+Asserts/bin/llvm-strip"
if [[ -x "$strip_tool" ]]; then
  "$strip_tool" --strip-all -o "$build/$browser_name.stripped" "$binary"
  binary="$build/$browser_name.stripped"
fi
puts+=(--put "2:/bin/$browser_name=$binary")
for f in "${assets[@]}"; do
  [[ -f "$out/$f" ]] || { echo "Missing browser resource: $out/$f" >&2; exit 1; }
  puts+=(--put "2:/data/$asset_dir/$f=$out/$f")
done
if [[ "$browser_name" == chrome ]]; then
  for locale in en-US ru; do
    [[ -f "$out/locales/$locale.pak" ]] || { echo "Missing Chrome locale: $locale" >&2; exit 1; }
    puts+=(--put "2:/data/chrome/locales/$locale.pak=$out/locales/$locale.pak")
  done
fi
puts+=(--put "2:/fonts/ui.ttf=$root/assets/fonts/space-grotesk/SpaceGrotesk-Medium.ttf"
      --put "2:/fonts/ui-OFL.txt=$root/assets/fonts/space-grotesk/OFL.txt"
      --put "2:/fonts/mono.ttf=$root/assets/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf"
      --put "2:/fonts/mono-OFL.txt=$root/assets/fonts/jetbrains-mono/OFL.txt"
      --put "2:/fonts/fallback.ttf=$root/assets/fonts/noto/NotoSans-Regular.ttf"
      --put "2:/fonts/fallback-OFL.txt=$root/assets/fonts/noto/OFL.txt"
      --put "2:/fonts/arabic.ttf=$root/assets/fonts/noto/NotoSansArabic-Regular.ttf"
      --put "2:/fonts/cjk.otf=$root/assets/fonts/noto-cjk/NotoSansCJKsc-Regular.otf"
      --put "2:/fonts/cjk-OFL.txt=$root/assets/fonts/noto-cjk/OFL.txt")
# Build the image on the native (ext4) temporary filesystem: mkdisk issues
# one write per 1 KiB block, which is prohibitively slow over the Windows
# drive mount. 512 MiB is enough for the stripped browser and assets.
image="$build/disk-browser.img"
# Build into a fresh file and swap the symlink: never rewrite an image a QEMU
# instance may still have open (a torn write makes the guest hang while
# loading the browser).
tmp_image="$(mktemp "${TMPDIR:-/tmp}/charmos-disk-browser-XXXXXXXX.img")"
link="$image.next-$$"
trap 'rm -f -- "$tmp_image" "$link"' EXIT
# 4 KiB ext2 blocks: with 1 KiB blocks one indirect block covers only 256 data
# blocks, so mkdisk interleaves metadata every 256 KiB and content_shell ends up
# in ~800 extents. Large blocks keep the 206 MiB binary in a handful of runs,
# which is what the loader's batched reads need.
image_size=512
[[ "$browser_name" == chrome ]] && image_size=1024
"$build/host/tools/mkdisk/mkdisk" "$tmp_image" "$image_size" --esp 40 --root ext2 --block 4096 "${puts[@]}"
old=""
[[ -L "$image" ]] && old="$(readlink -f "$image" 2>/dev/null || true)"
# The image stays on the native filesystem: QEMU reads it through the Windows
# mount otherwise, and loading the 200 MiB browser binary from there takes
# tens of seconds. A symlink keeps the documented path working.
ln -s -- "$tmp_image" "$link"
mv -Tf -- "$link" "$image"
trap - EXIT  # The published image must outlive this preparation process.
# Never delete an arbitrary file that the old symlink happened to reference.
case "$old" in
  "$(dirname "$tmp_image")"/charmos-disk-browser-*.img)
    if [[ "$old" != "$tmp_image" && -f "$old" ]]; then rm -f -- "$old" || true; fi ;;
esac
