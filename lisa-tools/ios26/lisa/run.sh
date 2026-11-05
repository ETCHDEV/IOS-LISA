#!/bin/bash
# One iOS 26 test on the phone, RAM boot only, no photos: run.sh NAME
# Phone in recovery or fastboot -> fastboot boot out/NAME.img -> kernel resets at panic or at the root wait ->
# recovery -> logs/NAME.log from pstore, with the panic chain turned into function names.
set -uo pipefail
L="$(cd "$(dirname "$0")" && pwd)"
T="$(cd "$L/../.." && pwd)"
S="${LISA_SERIAL:?set LISA_SERIAL to the fastboot serial of your phone}"
NAME="${1:?usage: run.sh NAME}"
IMG="$L/out/$NAME.img"
LOG="$L/logs/$NAME.log"
K="$T/research-UniXNU-ios26/build/xnu-12377.121.6/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf"
WAIT_SECONDS="${WAIT_SECONDS:-300}"
mkdir -p "$L/logs"
[ -f "$IMG" ] || { echo "no $IMG"; exit 1; }

fastboot_ready() { timeout 6 fastboot -s "$S" getvar product 2>&1 | grep -q 'product: lisa'; }

if adb devices | grep -q "$S.*recovery"; then
	adb -s "$S" reboot bootloader
	sleep 12
fi
sent=0
for try in 1 2 3; do
	for i in $(seq 1 30); do fastboot_ready && break; sleep 3; done
	sleep 5
	if timeout 45 fastboot -s "$S" boot "$IMG" 2>&1 | tr '\r' '\n' | grep -q '^Finished'; then sent=1; break; fi
	echo "send attempt $try failed"
	sleep 5
done
[ "$sent" = 1 ] || { echo 'NEEDS-USER: fastboot is stuck. Hold Power + Volume Down, then run this again.'; exit 2; }
echo "booted $NAME at $(date +%H:%M:%S), waiting for recovery"
sleep 20
for i in $(seq 1 $((WAIT_SECONDS / 3))); do
	adb devices | grep -q "$S.*recovery" && break
	sleep 3
done
if ! adb devices | grep -q "$S.*recovery"; then
	echo "NEEDS-USER: no recovery after ${WAIT_SECONDS}s: the kernel hung without resetting, or recovery has no ADB."
	exit 3
fi
sleep 4
bash "$T/pull-lisa-log.sh" "$S" "$LOG" || echo "NO-STORAGE-LOG: the kernel never flushed it"
echo "log: $LOG ($(wc -c < "$LOG" | tr -d ' ') bytes)"
tail -40 "$LOG" | cut -c1-180
chain="$(grep -aE '^ +[0-9]+ fffffff0[0-9a-f]+$' "$LOG" | awk '{print "0x"$2}' | tail -16)"
if [ -n "$chain" ]; then
	echo '--- panic chain ---'
	# shellcheck disable=SC2086
	atos -o "$K" -arch arm64 $chain 2>/dev/null | sed -E 's/ \(in [^)]*\)//' | cut -c1-90
fi
