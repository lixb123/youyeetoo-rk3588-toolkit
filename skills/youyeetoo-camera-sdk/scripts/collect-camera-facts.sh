#!/usr/bin/env bash

set -u

service_name="${1:-youyeetoo-app.service}"

section() {
    printf '\n=== %s ===\n' "$1"
}

section "TIME"
date --iso-8601=seconds 2>&1 || date 2>&1

section "SYSTEM"
uname -a 2>&1
cat /etc/os-release 2>&1

section "USB"
lsusb 2>&1
lsusb -t 2>&1

section "CAMERA USB SYSFS"
for device_dir in /sys/bus/usb/devices/*; do
    [[ -f "${device_dir}/idVendor" ]] || continue
    [[ "$(<"${device_dir}/idVendor")" == "2e1a" ]] || continue
    printf 'path=%s product=%s serial=%s vendor=%s product_id=%s\n' \
        "$(basename "${device_dir}")" \
        "$(cat "${device_dir}/product" 2>/dev/null || true)" \
        "$(cat "${device_dir}/serial" 2>/dev/null || true)" \
        "$(cat "${device_dir}/idVendor" 2>/dev/null || true)" \
        "$(cat "${device_dir}/idProduct" 2>/dev/null || true)"
done

section "PROCESS OWNERSHIP"
pgrep -af 'youyeetoo_app|CameraSDKTest' 2>&1 || true

section "SERVICE"
systemctl status "${service_name}" --no-pager 2>&1 || true

section "RECENT JOURNAL"
journalctl -u "${service_name}" -n 200 --no-pager 2>&1 || true
