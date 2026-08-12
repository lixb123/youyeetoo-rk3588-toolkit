# Validation Matrix

## Baseline

```bash
uname -a
cat /etc/os-release
lsblk
mount
systemctl status youyeetoo-app.service --no-pager
journalctl -u youyeetoo-app.service -n 200 --no-pager
ps -ef | grep youyeetoo_app
```

## Targeted Checks

| Subsystem | Minimum evidence |
| --- | --- |
| Camera | USB ID/link, service connection, serial routing, requested operation, output file and metadata |
| Ethernet | interface/carrier/speed, exact addresses, peer, bidirectional packets, loss/errors |
| CAN | interface state, bitrate/config, RX/TX counters, known frame or analyzer evidence |
| Storage | mount, capacity, write path, output file, free-space behavior |
| Watchdog | service health, feed behavior, timeout or reset evidence when explicitly tested |
| Boot | image hash, boot stage, kernel, rootfs, service readiness, serial evidence if needed |

## Status Rules

- `PASS`: expected behavior observed with sufficient evidence.
- `FAIL`: expected behavior not observed or a regression is proven.
- `BLOCKED`: test could not run because hardware, access, artifact, or ICD evidence was missing.
