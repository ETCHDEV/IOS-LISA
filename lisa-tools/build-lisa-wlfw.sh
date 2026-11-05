#!/bin/bash
# Generate the upstream ath11k QMI descriptions into Lisa's actual Darwin build tree.
set -euo pipefail
T="$(cd "$(dirname "$0")" && pwd)"
R="$T/research-hackios-kernel"
export PATH="/opt/homebrew/opt/coreutils/libexec/gnubin:$PATH"
bash "$R/tools/mk-ath11k.sh" fetch
python3 - "$R" "$T/darwin-xnu-build" <<'PY'
from pathlib import Path
import subprocess, sys
research, build = map(Path, sys.argv[1:])
ath = research/'xnu-deps/build/ath11k/src/ath11k'
script = (research/'tools/mk-wlfw.sh').read_text()
program = script.split("<<'PY'\n", 1)[1].split('\nPY\n', 1)[0]
out = build/'build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT/redwood_wlfw_ath11k.h'
assert out.parent.is_dir(), 'Build the Lisa kernel once first'
release = (ath.parent/'.release').read_text().split()[0]
result = subprocess.run([sys.executable, '-', str(ath), release], input=program, text=True, capture_output=True)
if result.returncode:
    sys.stderr.write(result.stderr)
    raise SystemExit(result.returncode)
assert 'WLFW_PIPES' in result.stdout and 'qmi_wlanfw' in result.stdout
out.write_text(result.stdout)
# __has_include alone does not give make a dependency when the old build used the stub.
(build/'xnu/bsd/dev/lisa_wlfw.c').touch()
print(out, out.stat().st_size)
PY
