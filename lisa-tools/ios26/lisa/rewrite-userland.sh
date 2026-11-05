#!/bin/bash
# iOS 26 userland as plain arm64 for a CPU without pointer authentication, offline, with zeta's tools. Needs mount-roots.sh.
# Each step keeps its output, so a rerun continues. Results in ios26/work.
set -uo pipefail
L="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$L/.." && pwd)"
cd "$R" || exit 1
export PATH=/opt/homebrew/opt/llvm/bin:$PATH
Z=../research-none24smu-UniXNU-zeta/scripts
PY=lisa/.venv/bin/python
W=work
TC=(--apple-trust-cache raw/trust/094-54256-108.dmg.aea.trustcache --apple-trust-cache raw/trust/094-54345-109.dmg.aea.trustcache)
C=mnt/SystemOS/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e
mkdir -p "$W"
for v in OS SystemOS; do
	if [ ! -f "$W/tree-$v.json" ]; then
		rm -rf "$W/tree-$v"
		echo "$(date +%H:%M:%S) tree $v"
		"$PY" "$Z/rewrite-zeta-userland-tree.py" --root "mnt/$v" --output "$W/tree-$v" "${TC[@]}" --report "$W/tree-$v.json" --file-list "$W/tree-$v.files.json" --jobs 6 2>&1 | tail -4 | cut -c1-170
		[ -f "$W/tree-$v.json" ] || { echo "TREE-$v-FAILED"; exit 1; }
	fi
done
echo "$(date +%H:%M:%S) prove cache"
"$PY" "$Z/prove-zeta-dyld-cache.py" --cache "$C" --work "$W/cache-proof" --jobs 7 2>&1 | tail -6 | cut -c1-190
echo "prove rc=${PIPESTATUS[0]} $(date +%H:%M:%S)"
du -sh "$W"/* 2>/dev/null | cut -c1-50
df -h / | tail -1 | awk '{print $4" free"}'
