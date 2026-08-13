# UART Camera Development

## Choose The Route

Use this route only for `youyeetoo_app` and optional `libCameraSDK.so` changes on a board that already boots and provides a root serial console. Use the full rootfs/image path for systemd or overlay integration, packages, kernel/DTS/drivers, boot behavior, partitions, or formal release acceptance.

```text
Agent A-CAM -> UART development bundle -> Agent D-VAL
```

## Build And Transfer

Build in WSL with the complete CameraSDK headers and AArch64 library:

```bash
cd /home/rk3588/youyeetoo-yy3588/youyeetoo_app
make CAMERA_SDK_DIR=/home/rk3588/youyeetoo-yy3588/CameraSDK
./scripts/build_uart_dev_bundle.sh
```

Keep the default app-only bundle for normal changes. Add `--include-sdk` only when the SDK library itself changes. On Windows, verify the COM port before sending:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
powershell -ExecutionPolicy Bypass -File .\scripts\uart_deploy_camera.ps1 `
  -Port COM3 -Bundle .\dist\uart-dev\youyeetoo-uart-dev.tar.gz
```

The sender uses 1500000 baud, 8N1, no flow control, 32 KiB frames, CRC32 per frame, ACK validation with retry, an explicit abort frame, and whole-file SHA-256. It bootstraps a Python receiver through the existing root console. Use `-Install` only after confirming that the console does not echo binary data and passwordless or already-authenticated `sudo` is available. If the UART enters the RK3588 `debug>` FIQ prompt, send `console` to return to the Linux console.

If repeated UART transfers enter the RK3588 FIQ debugger or time out at different frames, stop retrying the bulk transfer. Keep UART open as the recovery console and use an already linked Ethernet management interface only for the bundle transfer:

1. Record PC and board addresses, routes, carrier, speed, and the active management path before changes.
2. Prefer an existing address. If one is required, add a temporary secondary address instead of clearing the interface.
3. Serve or copy only the bundle, then verify its board-side SHA-256 before installation.
4. Run the same bundle installer, health check, and rollback workflow shown below.
5. Stop the temporary transfer service and remove the staged bundle after validation.

This fallback remains an app-only workflow. It does not authorize rootfs, kernel, boot-chain, partition, or firmware changes.

## Install And Recover

The bundle contains its board tools. Extract it and run:

```bash
tmpdir="$(mktemp -d)"
tar -xzf /tmp/youyeetoo-uart-dev.tar.gz -C "$tmpdir"
sudo "$tmpdir/tools/install_uart_dev_bundle.sh" /tmp/youyeetoo-uart-dev.tar.gz
```

The installer verifies the manifest, saves the active app and optional SDK under `/opt/youyeetoo_app/dev/backups/<timestamp>`, stops and restarts `youyeetoo-app.service`, waits up to 60 seconds for a required camera to reconnect, and automatically rolls back when the health check fails. Override the wait with `CAMERA_CONNECT_WAIT_SECONDS` only when the hardware needs a known longer startup interval.

Manual recovery:

```bash
sudo /opt/youyeetoo_app/dev/tools/rollback_uart_dev.sh
```

## Validate

Start read-only:

```bash
sudo /opt/youyeetoo_app/dev/tools/camera_uart_smoke_test.sh
```

Verify queries in this order and wait for each worker task to finish:

```text
CAMERA_INIT
CAMERA_GET_STATUS
CAMERA_GET_CAPTURE_STATUS
CAMERA_GET_BATTERY
CAMERA_GET_STORAGE
CAMERA_LIST_DEVICES
```

Use `completed_tasks`, `task_active=no`, and `last_task_summary` as completion evidence. `dispatch_ok mode=module_task` proves only that the request reached the task manager. Require battery/storage values in the completed summary or runtime snapshot.

After a physical reconnect, observe whether the existing service changes from `FATAL`/`ERROR_RECOVER` to `connected=yes` without a service restart. The controller retries ten times, enters a 30-second cooldown, then starts a new retry cycle. `CAMERA_INIT` must not close an already healthy session; it triggers immediate discovery only while disconnected.

Confirm the target with `lsusb` using vendor `2e1a` by default. Do not hard-code product `0002` unless testing that known X5 identity. `CAMERA_USB_PRODUCT_ID` may contain comma-separated IDs for mixed models. If the targeted USB identity disappears, stop CameraSDK queries and record a hardware/session blocker.

For two X5 devices or mixed Insta360 models, run `CAMERA_LIST_DEVICES` first and use each unique `camera_serial=` for status, battery, storage, photo, video, and media commands. Confirm the JSON `camera_type`, `model_key`, and `capabilities`; never route by model name.

Only add `--take-photo` or `--short-video-seconds N` when capture is explicitly in scope. Save evidence under `/var/opt/youyeetoo/runtime/dev-tests/<timestamp>` and report bundle SHA-256 plus installed app SHA-256.

Treat CameraSDK ownership as exclusive. Do not run `CameraSDKTest` while the production service is active. Stop the service for vendor-tool reproduction and restore it afterward.
