#!/usr/bin/env bash

set -u

section() {
    printf '\n=== %s ===\n' "$1"
}

section "TIME AND SYSTEM"
date --iso-8601=seconds 2>&1 || date 2>&1
uname -a 2>&1

section "LINKS"
ip -details link show 2>&1

section "ADDRESSES"
ip -details address show 2>&1

section "ROUTES AND RULES"
ip route show table all 2>&1
ip rule show 2>&1

section "NEIGHBORS"
ip neighbor show 2>&1

section "SOCKETS"
ss -lntup 2>&1 || ss -lntu 2>&1

section "INTERFACE DETAILS"
for iface_path in /sys/class/net/*; do
    iface="$(basename "${iface_path}")"
    [[ "${iface}" == "lo" ]] && continue
    printf '\n--- %s ---\n' "${iface}"
    ethtool "${iface}" 2>&1 || true
    ethtool -S "${iface}" 2>&1 || true
done

section "QDISC"
tc qdisc show 2>&1 || true

section "FORWARDING"
sysctl net.ipv4.ip_forward 2>&1 || true

section "FIREWALL SUMMARY"
nft list ruleset 2>&1 || iptables-save 2>&1 || true
