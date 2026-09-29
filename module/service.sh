#!/system/bin/sh
MODDIR=${0%/*}
(
    while [ "$(getprop sys.boot_completed)" != 1 ]; do
        sleep 2
    done
    while [ ! -e "$MODDIR/disable" ]; do
        "$MODDIR/ttmax_bridge" >> "$MODDIR/bridge.log" 2>&1
        sleep 2
    done
) </dev/null >/dev/null 2>&1 &
