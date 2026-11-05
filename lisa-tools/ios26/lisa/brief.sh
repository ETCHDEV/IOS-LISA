#!/bin/bash
# The useful part of a boot log: what launchd started and how the boot ended. brief.sh NAME [lines]
L="$(cd "$(dirname "$0")" && pwd)/logs/$1.log"
N="${2:-40}"
K="$(cd "$(dirname "$0")/../.." && pwd)/research-UniXNU-ios26/build/xnu-12377.121.6/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf"
[ -f "$L" ] || { echo "no log"; exit 1; }
echo "bytes $(wc -c < "$L"), execs $(grep -ac 'proc-trace: exec' "$L"), exits $(grep -ac 'proc-trace: exit' "$L"), panics $(grep -ac '^panic(' "$L")"
grep -ao 'proc-trace: exec pid [0-9]* ppid [0-9]* [^ ]*' "$L" | awk '{print $NF}' | sort | uniq -c | sort -nr | head -12 | awk '{printf "%s×%s ", $2, $1} END {print ""}' | cut -c1-500
grep -aE 'proc-trace: exit|exited --|^panic\(|initproc|Sandbox|mount.*fail|failed' "$L" | grep -avE 'swap|HardeningExceptions|fileSize|root snapshot|root_hash' | cut -c1-170 | tail -"$N"
a=$(grep -an '^lisa panic chain' "$L" | head -1 | cut -d: -f1)
if [ -n "$a" ]; then
	sed -n "$((a+1)),$((a+8))p" "$L" | awk '/fffffff/{print "0x"$2}' | xargs atos -o "$K" -arch arm64 2>/dev/null | sed -E 's/ \(in [^)]*\)//' | cut -c1-60
fi
tail -2 "$L" | cut -c1-150
