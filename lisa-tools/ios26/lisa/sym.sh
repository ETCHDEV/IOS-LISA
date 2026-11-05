#!/bin/bash
# Names for shared cache addresses from a boot log: sym.sh SLIDE ADDR...
L="$(cd "$(dirname "$0")" && pwd)"
S="$1"; shift
cd "$L/../work/symdir" || exit 1
for a in "$@"; do
	printf '%s ' "$a"
	timeout 280 ../../../ipsw dyld a2s --cache ../a2s --slide "$S" -d dyld_shared_cache_arm64e "$a" 2>&1 | sed -E 's/\x1b\[[0-9;]*m//g' | grep -vE 'Parsing|Loading|^$' | tail -1 | cut -c1-165
done
