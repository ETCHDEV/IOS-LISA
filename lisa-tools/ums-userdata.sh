#!/bin/bash
# Edit the phone's iOS root (userdata, sda33: a whole-partition HFSX volume) over USB mass storage
# from the recovery's configfs gadget, instead of re-flashing 25 GB of userdata.
#   MODE=check bash ums-userdata.sh            read-only: mount, compare our files, unmount
#   MODE=push  bash ums-userdata.sh f1 f2 ...  copy lisa-gl/out files into their places on the phone
# The phone must be in recovery with adb. Leaves it in recovery with adb, the gadget restored.
set -u
S="${LISA_SERIAL:?set LISA_SERIAL to the fastboot serial of your phone}"
MODE="${MODE:-check}"
T="$(cd "$(dirname "$0")" && pwd)"
O="$T/lisa-gl/out"
SIZE=113254576128   # the same through the loop device
RO=1; [ "$MODE" = push ] && RO=0; [ "$MODE" = fetch ] && RO=1
LUNRO=$RO; [ "${MEDIA_RW:-0}" = 1 ] && LUNRO=0
dest() {
  case "$1" in
    LisaMetal) echo System/Library/Extensions/LisaMetal.bundle/LisaMetal ;;
    lisa-metal-smoke) echo usr/local/bin/lisa-metal-smoke ;;
    lisa-shots) echo private/var/tmp/lisa-shots ;;
    lisa-mdiag) echo private/var/tmp/lisa-mdiag ;;
    lisa-drawsync) echo private/var/tmp/lisa-drawsync ;;
    lisa-softpipe) echo private/var/tmp/lisa-softpipe ;;
    lisa-widen16) echo private/var/tmp/lisa-widen16 ;;
    lisa-skiplub) echo private/var/tmp/lisa-skiplub ;;
    lisa-disasm) echo private/var/tmp/lisa-disasm ;;
    libvulkan_freedreno.dylib) echo usr/local/lib/lisa/libvulkan_freedreno.dylib ;;
    lisa-turnip-smoke) echo usr/local/bin/lisa-metal-smoke ;;
    lisa-m2v-smoke) echo usr/local/bin/lisa-metal-smoke ;;
    liblisavk.dylib) echo usr/local/lib/lisa/liblisavk.dylib ;;
    lisa-passlog) echo private/var/tmp/lisa-passlog ;;
    native-metal-processes) echo usr/local/lib/lisa/native-metal-processes ;;
    *) echo "usr/local/lib/lisa/$1" ;;
  esac
}
adb -s $S get-state | grep -q recovery || { echo "phone not in recovery"; exit 1; }
# 1. the gadget: adb + a LUN over sda33, done by a detached shell (the USB link drops while it rebinds)
# sda33 has 4096-byte blocks and the HFSX volume was made with 512-byte ones (macOS finds no file
# system on it as it is): a loop device over it, which has 512-byte blocks, is what the LUN serves.
# the recovery's SELinux policy denies the gadget's file-storage thread any block device
# (avc denied read, kernel -> loop_device): permissive for the session, enforcing again after
ENF="$(adb -s $S shell getenforce | tr -d '\r')"
adb -s $S shell setenforce 0; echo "SELinux $ENF -> $(adb -s $S shell getenforce | tr -d '\r')"
LOOP="$(adb -s $S shell 'losetup -f' | tr -d '\r')"
adb -s $S shell "losetup $( [ $LUNRO = 1 ] && echo -r ) $LOOP /dev/block/sda33 && blockdev --getss $LOOP && echo loop $LOOP" || { echo "no loop"; exit 1; }
adb -s $S shell "nohup sh -c 'G=/config/usb_gadget/g1; echo \"\" > \$G/UDC; mkdir -p \$G/functions/mass_storage.ums; echo $LUNRO > \$G/functions/mass_storage.ums/lun.0/ro; echo 0 > \$G/functions/mass_storage.ums/lun.0/removable; echo $LOOP > \$G/functions/mass_storage.ums/lun.0/file; ln -s \$G/functions/mass_storage.ums \$G/configs/b.1/f2; echo a600000.dwc3 > \$G/UDC' >/dev/null 2>&1 &"
# 2. find the disk
DISK=""
for i in $(seq 1 60); do
  for d in $(diskutil list external physical 2>/dev/null | awk '/^\/dev\/disk/{print $1}'); do
    sz=$(diskutil info "$d" 2>/dev/null | awk -F'[()]' '/Disk Size/{print $2}' | awk '{print $1}')
    [ "$sz" = "$SIZE" ] && DISK="$d"
  done
  [ -n "$DISK" ] && break
  sleep 2
done
echo "disk: ${DISK:-none}"
restore() {
  for i in $(seq 1 30); do adb -s $S get-state 2>/dev/null | grep -q recovery && break; sleep 2; done
  adb -s $S shell "nohup sh -c 'G=/config/usb_gadget/g1; echo \"\" > \$G/UDC; rm -f \$G/configs/b.1/f2; rmdir \$G/functions/mass_storage.ums; echo a600000.dwc3 > \$G/UDC' >/dev/null 2>&1 &"
  sleep 6
  for i in $(seq 1 30); do adb -s $S get-state 2>/dev/null | grep -q recovery && break; sleep 2; done
  adb -s $S shell "sync; losetup -d $LOOP" && echo "loop $LOOP detached"
  [ "$ENF" = Enforcing ] && adb -s $S shell setenforce 1; echo "SELinux back to $(adb -s $S shell getenforce | tr -d '\r')"
  for i in $(seq 1 30); do adb -s $S get-state 2>/dev/null | grep -q recovery && break; sleep 2; done
  adb -s $S shell 'ls /config/usb_gadget/g1/functions' 2>&1 | tr '\n' ' '; echo "<- gadget functions"
}
[ -n "$DISK" ] || { restore; exit 1; }
sleep 3
if [ "$MODE" = hold ]; then
  echo "experiments on $DISK"
  diskutil list "$DISK"
  ls -la "$DISK" "${DISK/disk/rdisk}"
  id
  diskutil mount readOnly "$DISK"; echo "rc $?"
  diskutil mountDisk "$DISK"; echo "rc $?"
  hdiutil attach -readonly -nomount -imagekey diskimage-class=CRawDiskImage "$DISK" 2>&1; echo "rc $?"
  /usr/sbin/diskutil info -plist "$DISK" | grep -A1 -i "filesystem\|content" | head -12
  log show --last 2m --predicate 'process == "diskarbitrationd" OR process == "fsck_hfs" OR process == "hfs.util"' 2>/dev/null | tail -20
  touch "$T/agent-queue/ums-release"
  while [ ! -e "$T/agent-queue/ums-release" ]; do sleep 2; done
  mv "$T/agent-queue/ums-release" "$T/agent-queue/ums-release.done"
  diskutil eject "$DISK" >/dev/null 2>&1 || true
  restore; exit 0
fi
diskutil unmountDisk "$DISK" >/dev/null 2>&1 || true
MNT="$(mktemp -d /tmp/lisa-ums.XXXX)"
if [ $RO = 1 ]; then OPTS="readOnly"; else OPTS=""; fi
diskutil info "$DISK" | grep -E "Type|File System|Read-Only|Mounted|Personality|Size" | head -12
MOUNTED=""
for try in "$OPTS -mountPoint $MNT" "$OPTS" "-mountOptions rdonly"; do
  [ $RO = 0 ] && [ "$try" = "-mountOptions rdonly" ] && continue
  if diskutil mount $try "$DISK"; then MOUNTED=1; break; fi
  echo "(tried: diskutil mount $try)"
done
if [ -n "$MOUNTED" ]; then
  MP="$(diskutil info "$DISK" | awk -F': *' '/Mount Point/{print $2}')"
  [ -n "$MP" ] && MNT="$MP"
fi
if [ -z "$MOUNTED" ]; then
  log show --last 1m --predicate 'process == "diskarbitrationd" OR process CONTAINS "hfs" OR sender CONTAINS "hfs"' 2>/dev/null | grep -iv "xpc" | tail -12
  echo "diskutil mount failed; trying mount_hfs"
  if [ $RO = 1 ]; then MO="-o rdonly,nobrowse"; else MO="-o nobrowse"; fi
  /sbin/mount_hfs $MO "$DISK" "$MNT" 2>&1 || { echo "mount_hfs failed"; dd if="$DISK" bs=512 skip=2 count=1 2>/dev/null | od -A d -c | head -4; restore; exit 1; }
fi
mount | grep "$MNT"
test -f "$MNT/System/Library/CoreServices/SystemVersion.plist" && echo "iOS root seen"
rc=0
if [ "$MODE" = push ]; then
  for f in "$@"; do
    d="$MNT/$(dest "$f")"
    [ -f "$d" ] || [ "$f" = lisa-shots ] || [ "$f" = lisa-mdiag ] || [ "$f" = lisa-drawsync ] || [ "$f" = lisa-softpipe ] || [ "$f" = lisa-widen16 ] || [ "$f" = lisa-skiplub ] || [ "$f" = lisa-disasm ] || [ "$f" = libvulkan_freedreno.dylib ] || [ "$f" = liblisavk.dylib ] || [ "$f" = lisa-passlog ] || { echo "no $d on the phone"; rc=1; continue; }
    cp "$O/$f" "$d" && cmp "$O/$f" "$d" && echo "pushed $f -> $(dest "$f")" || rc=1
  done
  sync
elif [ "$MODE" = fetch ]; then
  mkdir -p "$T/diagnostic/shots"
  for f in "$MNT"/private/var/tmp/lisa-shot-*.raw; do
    [ -f "$f" ] || continue
    cp "$f" "$T/diagnostic/shots/" && echo "fetched $(basename "$f") $(stat -f %z "$f")"
  done
  for f in "$MNT"/private/var/tmp/lisa-mesa-*.log; do
    [ -f "$f" ] || continue
    cp "$f" "$T/diagnostic/shots/" && echo "fetched $(basename "$f") $(stat -f %z "$f")"
  done
else
  for f in liblisair.dylib LisaMetal liblisagl.dylib; do
    cmp -s "$O/$f" "$MNT/$(dest "$f")" && echo "same: $f" || echo "DIFFERS: $f"
  done
fi
diskutil unmount "$MNT" || diskutil unmount force "$MNT"
diskutil eject "$DISK" >/dev/null 2>&1 || true
restore
exit $rc
