#!/usr/bin/env bash
# Ставит тулчейн CharmOS в WSL2 Ubuntu. Запускать один раз: bash tools/setup-wsl.sh
set -euo pipefail
sudo apt-get update
sudo apt-get install -y clang lld llvm cmake ninja-build qemu-system-x86 ovmf gdb
