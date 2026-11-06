#!/usr/bin/env bash
set -euo pipefail
build_dir=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$build_dir/.." && pwd)
tool_root=${RAISE2_TOOLCHAIN_ROOT:-"$HOME/.local/opt/raise2-toolchain"}
export PATH="$tool_root/gcc-arm-none-eabi-10.3-2021.10/bin:$tool_root/venv/bin:$PATH"
python -c 'import unicorn, intelhex'
mkdir -p "$build_dir/vendor" "$build_dir/verification"
if [[ ! -f "$build_dir/vendor/keyscanner-v1.5.0.bin" ]]; then
  python - "$build_dir/vendor/keyscanner-v1.5.0.bin" <<'PY'
from pathlib import Path
import sys, urllib.request
url='https://github.com/Dygmalab/Firmware-release/releases/download/Raise2_v1.5.0/keyscanner.bin'
Path(sys.argv[1]).write_bytes(urllib.request.urlopen(url).read())
PY
fi
python "$root/tests/splash/check.py" | tee "$build_dir/verification/tests.log"
make -C "$build_dir" clean
make -C "$build_dir" -j4 release RAISE2_FW_VERSION=v1.5.0-splash8 > "$build_dir/verification/build.log" 2>&1
python "$build_dir/package-local.py"
