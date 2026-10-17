set -e
root="$(cd "$(dirname "$0")/../.." && pwd)"
bash "$root/tools/qemu/libc.sh" "$1"
bash "$root/tools/qemu/swtpm.sh" "$1"
