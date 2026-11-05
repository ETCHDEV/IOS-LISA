#!/system/bin/sh
# Run only in Lisa recovery. Expose only its unmounted userdata as USB storage,
# alongside ADB, for targeted HFS file updates. Reboot also clears this setup.
set -eu
g=/config/usb_gadget/g1
f=$g/functions/mass_storage.lisa
link=$g/configs/b.1/lisa-storage
dev=$(readlink -f /dev/block/by-name/userdata)
[ "$dev" = /dev/block/sda33 ] || { echo 'Unexpected userdata device'; exit 1; }
[ "$(blockdev --getsize64 "$dev")" = 113254576128 ] || { echo 'Unexpected userdata size'; exit 1; }
[ "$(getprop ro.product.device)" = lisa ] || { echo 'Unexpected device product'; exit 1; }
udc=$(cat "$g/UDC")
[ -n "$udc" ] || { echo 'USB controller is not bound'; exit 1; }
case "${1:-}" in
enable)
    major_minor=$(cat /sys/class/block/sda33/dev)
    while read -r mount_id parent_id mounted_device remainder; do
        [ "$mounted_device" != "$major_minor" ] || { echo 'Refusing to expose mounted userdata'; exit 1; }
    done < /proc/self/mountinfo
    [ ! -e "$link" ] || { echo 'Storage already enabled'; exit 0; }
    mkdir -p "$f"
    echo 1 > "$f/lun.0/removable"
    echo 0 > "$f/lun.0/ro"
    echo "$dev" > "$f/lun.0/file"
    echo '' > "$g/UDC"
    trap 'echo "$udc" > "$g/UDC"' EXIT
    ln -s "$f" "$link"
    echo "$udc" > "$g/UDC"
    trap - EXIT
    echo 'Enabled userdata USB storage alongside ADB'
    ;;
disable)
    # The Mac must unmount/eject the iOS volume BEFORE this operation.
    echo '' > "$g/UDC"
    trap 'echo "$udc" > "$g/UDC"' EXIT
    rm -f "$link"
    if [ -d "$f" ]; then
        echo '' > "$f/lun.0/file"
        rmdir "$f"
    fi
    if [ -f /tmp/lisa-userdata-usb.loop ]; then
        loop=$(cat /tmp/lisa-userdata-usb.loop)
        losetup -d "$loop"
        rm /tmp/lisa-userdata-usb.loop
    fi
    echo "$udc" > "$g/UDC"
    trap - EXIT
    echo 'Restored ADB-only recovery USB'
    ;;
*) echo 'usage: recovery-userdata-usb.sh enable|disable'; exit 1;;
esac
