# Системный диск M8: GPT, ESP и корень ext2 с программами (/bin), init.conf,
# /etc/hello.txt и сайтом для httpd (/srv/www).
build="$1"
work="$build/system-work"
mkdir -p "$work"
echo "hello from mkdisk" >"$work/hello"
cat >"$work/init.conf" <<'EOF'
# Службы init (M8): service — перезапуск при выходе; console — сеанс на консоли.
service logd /bin/logd
service httpd /bin/httpd 8080 /srv/www
console sh /bin/sh
EOF
cat >"$work/index.html" <<'EOF'
<!doctype html>
<html><head><title>CharmOS</title></head>
<body><h1>Hello from CharmOS httpd</h1><p>Served from an ext2 disk by a user-space HTTP server.</p></body></html>
EOF
puts=(--put "2:/etc/hello.txt=$work/hello" --put "2:/etc/init.conf=$work/init.conf"
      --put "2:/srv/www/index.html=$work/index.html")
for p in init logd sh echo cat ls mkdir rm sleep httpd; do
  puts+=(--put "2:/bin/$p=$build/user/user/$p.elf")
done
"$build/host/tools/mkdisk/mkdisk" "$build/disk-system.img" 96 --esp 40 --root ext2 "${puts[@]}"
