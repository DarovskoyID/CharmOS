#!/usr/bin/env bash
# Keep mounting and the workload in one WSL invocation, then drop root privileges.
set -euo pipefail
[[ $# -ge 3 ]] || { echo 'Usage: with-cache.sh UID GID command [arguments...]' >&2; exit 2; }
uid="$1"; gid="$2"; shift 2
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
if ! bash "$root/tools/chromium/mount-cache.sh" "$uid" "$gid"; then
  bash "$root/tools/chromium/mount-cache.sh" "$uid" "$gid"
fi
account="$(getent passwd "$uid" | cut -d: -f1)"
[[ -n "$account" ]] || { echo 'Unknown cache owner' >&2; exit 2; }
cd "$root"
exec runuser -u "$account" -- "$@"
