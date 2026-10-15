# Образ для m8/spawn: GPT, ESP и корень ext2 с /bin/spawn_child и /etc/hello.txt.
build="$1"
work="$build/spawn-work"
mkdir -p "$work"
echo "hello from mkdisk" >"$work/hello"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-spawn.img" 96 --esp 40 --root ext2 \
  --put "2:/etc/hello.txt=$work/hello" --put "2:/bin/spawn_child=$build/user/user/spawn_child.elf"
