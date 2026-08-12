# Build, Package, And Validation Workflow

## App Change

1. Work in `youyeetoo_app/`.
2. Load the RK3588 cross-build environment.
3. Build with the app `Makefile` and the complete AArch64 CameraSDK package.
4. Run focused checks where possible.
5. Use `scripts/sync_to_overlay.sh` when the change must enter an image.

For a healthy board and an app-only camera change, build `scripts/build_uart_dev_bundle.sh`, transfer it with `scripts/uart_deploy_camera.ps1`, install with backup and health checking, then hand the bundle and app hashes to Agent D-VAL. Do not rebuild rootfs or package an image for this daily loop.

Do not use this route for systemd/overlay/rootfs, kernel/DTS/driver, boot-chain, or partition changes. Those changes require their owning role and the full image workflow.

## Rootfs Integration

1. Identify whether each file is app-owned or overlay-owned.
2. Sync the application, SDK library, configuration, and systemd unit into the overlay.
3. Run the current `ubuntu22.04/mk-ubuntu-rootfs.sh`.
4. Do not use `FAST=1` for the first build; it requires an existing completed build tree.
5. Record the generated artifact and package-install failures.

## Boot Or Kernel Change

1. Read current board bring-up and recovery notes before editing.
2. Build U-Boot through the SDK-supported `build.sh uboot` route.
3. Build kernel and DTS through the matching vendor SDK flow.
4. Preserve the last known-good image and serial-console access.
5. Pass exact artifact names and partition expectations to Agent C.

## Package Image

1. Inspect the current `pack_update.sh` before running it.
2. Confirm its inputs and output directory.
3. Package the image.
4. Verify the final file exists, has a plausible nonzero size, and is newer than its inputs.
5. Calculate SHA-256.
6. Preserve or version the known-good artifact before replacing `<flash-dir>\update.img`.
7. Produce the Agent C handoff record.

## Flash Handoff

Flashing changes external hardware state. Require an explicit user instruction identifying the image and target board. Verify artifact path and SHA-256 again before flashing.

If normal boot becomes unavailable, switch from SSH to serial console. Use Maskrom recovery only when ordinary paths cannot recover the board and the user has authorized recovery.

## Board Validation

1. Confirm the tested artifact type and SHA-256: `uart-dev-bundle` for daily app tests or `update.img` for releases.
2. Confirm board identity, kernel, architecture, uptime, and storage mounts.
3. Check `youyeetoo-app.service` and worker processes.
4. Test the subsystem changed in this release.
5. Capture relevant `journalctl`, command output, files, and media artifacts.
6. Expand into regression only after the targeted test passes.
7. Write PASS, FAIL, or BLOCKED with evidence.

For CameraSDK queries, require current USB identity/speed, latest `connected=yes`, and a completed worker result. Treat `dispatch_ok` as routing evidence only. If the camera disappears from `lsusb`, stop the query campaign and return `BLOCKED` with the last known connected timestamp.

Typical read-only starting checks:

```bash
uname -a
cat /etc/os-release
lsblk
mount
systemctl status youyeetoo-app.service --no-pager
journalctl -u youyeetoo-app.service -n 200 --no-pager
ps -ef | grep youyeetoo_app
```
