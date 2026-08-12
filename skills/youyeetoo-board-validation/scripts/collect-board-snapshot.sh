#!/usr/bin/env bash

set -u

section() {
    printf '\n=== %s ===\n' "$1"
}

section "TIME"
date --iso-8601=seconds 2>&1 || date 2>&1

section "IDENTITY"
hostname 2>&1
uname -a 2>&1
cat /etc/os-release 2>&1
uptime 2>&1

section "STORAGE"
lsblk 2>&1
df -hT 2>&1
mount 2>&1

section "NETWORK"
ip -details link show 2>&1
ip -details address show 2>&1
ip route show table all 2>&1

section "USB"
lsusb 2>&1 || true

section "SERVICE"
systemctl status youyeetoo-app.service --no-pager 2>&1 || true
ps -ef | grep '[c]hizhou01_app' 2>&1 || true

section "JOURNAL"
journalctl -u youyeetoo-app.service -n 200 --no-pager 2>&1 || true
