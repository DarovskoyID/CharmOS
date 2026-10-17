#!/usr/bin/env bash
# A workspace-local ext4 image avoids per-file DrvFS overhead and the full C: disk.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Run this helper as WSL root' >&2; exit 2; }
[[ ${1:-} =~ ^[0-9]+$ && ${2:-} =~ ^[0-9]+$ ]] || { echo 'Expected owner uid and gid' >&2; exit 2; }
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cache="$root/build/chromium"
mkdir -p "$cache"
image="$cache/linux-cache.ext4"
target="$cache/linux-cache"
[[ $(realpath -m "$image") == "$image" && $(realpath -m "$target") == "$target" ]] || { echo 'Unexpected cache symlink' >&2; exit 2; }
if [[ ! -e "$image" ]]; then
  # This is a new regular file, never a disk device or an existing filesystem.
  (set -o noclobber; : >"$image")
  # DrvFS otherwise allocates and zero-fills the full image on its first tail write.
  fsutil.exe sparse setflag "$(wslpath -w "$image")"
  truncate -s 160G -- "$image"
  mkfs.ext4 -q -F -m 0 -L CharmOSChromium -E nodiscard,lazy_itable_init=1,lazy_journal_init=1 "$image"
fi
[[ -f "$image" && $(blkid -s TYPE -o value "$image") == ext4 && $(blkid -s LABEL -o value "$image") == CharmOSChromium ]] || { echo 'Cache image identity mismatch' >&2; exit 2; }
mkdir -p "$target"
if ! mountpoint -q "$target"; then
  [[ -z $(find "$target" -mindepth 1 -maxdepth 1 -print -quit) ]] || { echo 'Mount directory is not empty' >&2; exit 2; }
  # WSL invocations can have separate mount namespaces. Reuse the same loop
  # device by backing path, rather than mounting a second device over this image.
  existing="$(losetup --json --list --output NAME,BACK-FILE | python3 -c '
import json,sys
devices=[d["name"] for d in json.load(sys.stdin)["loopdevices"] if d.get("back-file")==sys.argv[1]]
if len(devices)>1: raise SystemExit("Multiple cache loop devices; refusing to mount")
print(devices[0] if devices else "")
' "$image")"
  if [[ -n "$existing" ]]; then
    mount -t ext4 -o noatime,nodev,nosuid "$existing" "$target"
  else
    mount -o loop,noatime,nodev,nosuid "$image" "$target"
  fi
fi
device="$(findmnt -n -o SOURCE --mountpoint "$target")"
backing="$(losetup -n -O BACK-FILE "$device")"
[[ "$backing" == "$image" ]] || { echo 'Mounted cache backing file mismatch' >&2; exit 2; }
chown "$1:$2" "$target"
df -h "$target"
