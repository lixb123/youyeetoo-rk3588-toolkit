# Post-Flash Checks

After a successful boot, verify in this order:

1. Board identity, kernel, architecture, uptime, and storage mounts.
2. SSH reachability and management interface.
3. `youyeetoo-app.service` state and worker processes.
4. Expected deployed binary, SDK library, configs, and systemd unit.
5. The subsystem affected by the image change.
6. Recovery and rollback evidence.

Do not claim success from a flashing tool's completion message alone.
