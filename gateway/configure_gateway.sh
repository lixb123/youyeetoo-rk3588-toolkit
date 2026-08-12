#!/bin/sh
set -eu

PAYLOAD_IFACE=${1:-eth0}
X_IFACE=${2:-eth1}

ip link set "$PAYLOAD_IFACE" up
ip link set "$X_IFACE" up

# UART is the control path, so both selected Ethernet ports can be reset.
ip -4 addr flush dev "$PAYLOAD_IFACE"
ip -4 addr flush dev "$X_IFACE"

# These are the platform-side addresses from Appendix B. The payload and X
# machine are connected to separate physical ports.
ip addr replace 10.240.1.34/24 dev "$PAYLOAD_IFACE"
ip addr replace 10.240.1.1/30 dev "$X_IFACE"

sysctl -w net.ipv4.ip_forward=1 >/dev/null
sysctl -w net.ipv4.conf.all.rp_filter=0 >/dev/null || true
sysctl -w net.ipv4.conf."$PAYLOAD_IFACE".rp_filter=0 >/dev/null || true
sysctl -w net.ipv4.conf."$X_IFACE".rp_filter=0 >/dev/null || true

# The documented order is upload, business/S1, management and log. Some
# board MAC drivers reject software qdiscs; keep forwarding usable and report
# that limitation instead of aborting address setup.
TC_OK=1
for dev in "$PAYLOAD_IFACE" "$X_IFACE"; do
    if ! tc qdisc replace dev "$dev" root handle 1: prio bands 4 2>/dev/null; then
        TC_OK=0
        continue
    fi
    tc filter del dev "$dev" parent 1: 2>/dev/null || true
    tc filter del dev "$dev" parent 1: 3 2>/dev/null || true
    tc filter del dev "$dev" parent 1: 4 2>/dev/null || true
    tc filter add dev "$dev" parent 1: protocol ip prio 1 u32 \
        match ip src 10.240.1.36/32 flowid 1:1
    tc filter add dev "$dev" parent 1: protocol ip prio 2 u32 \
        match ip dst 10.240.1.36/32 flowid 1:1
    tc filter add dev "$dev" parent 1: protocol ip prio 3 u32 \
        match ip src 10.240.1.38/32 flowid 1:2
    tc filter add dev "$dev" parent 1: protocol ip prio 4 u32 \
        match ip dst 10.240.1.38/32 flowid 1:2
    tc filter add dev "$dev" parent 1: protocol ip prio 5 u32 \
        match ip src 10.2.0.0/16 flowid 1:2
    tc filter add dev "$dev" parent 1: protocol ip prio 6 u32 \
        match ip dst 10.2.0.0/16 flowid 1:2
    tc filter add dev "$dev" parent 1: protocol ip prio 7 u32 \
        match ip src 10.240.1.35/32 flowid 1:3
    tc filter add dev "$dev" parent 1: protocol ip prio 8 u32 \
        match ip dst 10.240.1.35/32 flowid 1:3
    tc filter add dev "$dev" parent 1: protocol ip prio 9 u32 \
        match ip src 10.240.1.39/32 flowid 1:3
    tc filter add dev "$dev" parent 1: protocol ip prio 10 u32 \
        match ip dst 10.240.1.39/32 flowid 1:3
done

if [ "$TC_OK" -eq 0 ]; then
    echo "warning: this driver does not support tc prio; monitor still labels priorities"
fi

ip route replace 10.240.1.0/24 dev "$PAYLOAD_IFACE" src 10.240.1.34
ip route replace 10.240.1.0/30 dev "$X_IFACE" src 10.240.1.1
ip route replace 10.240.1.50/32 via 10.240.1.2 dev "$X_IFACE"
ip route replace 10.240.1.51/32 via 10.240.1.2 dev "$X_IFACE"
ip route replace 10.240.1.52/32 via 10.240.1.2 dev "$X_IFACE"
ip route replace 10.2.0.0/16 via 10.240.1.38 dev "$PAYLOAD_IFACE"
echo "configured payload=$PAYLOAD_IFACE (10.240.1.34/24) x=$X_IFACE (10.240.1.1/30)"
