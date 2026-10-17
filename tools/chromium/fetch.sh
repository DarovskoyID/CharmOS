#!/usr/bin/env bash
# Official upstream sources stay in the ignored build cache, never in CharmOS Git.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
browser="$root/build/chromium"
depot="$browser/depot_tools"
checkout="$browser/upstream"
manifest="$root/tools/chromium/upstream.json"
mapfile -t locked < <(python3 - "$manifest" <<'PY'
import json, sys
lock = json.load(open(sys.argv[1], encoding='utf-8'))
for group, key in [('chromium','url'), ('chromium','revision'), ('depot_tools','url'), ('depot_tools','revision')]:
    print(lock[group][key])
PY
)
[[ ${#locked[@]} == 4 ]] || { echo 'Invalid Chromium lock file' >&2; exit 2; }
mkdir -p "$browser" "$checkout" "$browser/cache/cipd" "$browser/cache/vpython"
if [[ ! -d "$depot/.git" ]]; then
  git clone --no-checkout --filter=blob:none "${locked[2]}" "$depot"
  git -C "$depot" fetch --depth=1 origin "${locked[3]}"
  git -C "$depot" checkout --detach "${locked[3]}"
fi
[[ $(git -C "$depot" rev-parse HEAD) == "${locked[3]}" ]] || { echo 'depot_tools differs from lock; refusing to replace it' >&2; exit 2; }
[[ -z $(git -C "$depot" status --porcelain --untracked-files=no) ]] || { echo 'Modified depot_tools; refusing to run' >&2; exit 2; }
python3 - "$checkout/.gclient" "${locked[0]}" <<'PY'
from pathlib import Path
import sys
config = Path(sys.argv[1])
text = 'solutions = ' + repr([dict(name='src', url=sys.argv[2], managed=False, custom_deps={}, custom_vars={})]) + '\n'
# Linux dependencies bootstrap the host tools; they do not provide a CharmOS port.
text += "target_os = ['linux']\n"
if config.exists() and config.read_text() != text:
    raise SystemExit('Existing .gclient differs; refusing to overwrite')
config.write_text(text)
PY
export PATH="$depot:$PATH"
export DEPOT_TOOLS_UPDATE=0
export DEPOT_TOOLS_COLLECT_METRICS=0
export CIPD_CACHE_DIR="$browser/cache/cipd"
export VPYTHON_VIRTUALENV_ROOT="$browser/cache/vpython"
cd "$checkout"
gclient sync --nohooks --noprehooks --no-history --revision "src@${locked[1]}" --jobs=8
[[ $(git -C src rev-parse HEAD) == "${locked[1]}" ]] || { echo 'Chromium revision mismatch' >&2; exit 2; }
echo 'Chromium sources synchronized; CharmOS platform support is still required.'
