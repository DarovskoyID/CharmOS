set -e
root="$(cd "$(dirname "$0")/../.." && pwd)"
bash "$root/tools/qemu/libc.sh" "$1"
