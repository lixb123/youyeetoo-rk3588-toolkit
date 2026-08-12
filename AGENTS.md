# Youyeetoo RK3588 Project Instructions

Use the personal skill `$youyeetoo-handover` for work involving `youyeetoo_app`, CameraSDK, RK3588 builds, rootfs integration, image packaging, or board validation.

Select one primary role before substantial work:

- Agent A: application code and app-owned configuration.
- Agent B: rootfs, overlay, packages, runtime environment, and systemd.
- Agent B2: U-Boot, kernel, DTS, PCIe/NVMe, boot slots, and board bring-up.
- Agent C: `update.img` packaging, integrity checks, and artifact handoff.
- Agent D: board validation, evidence collection, and issue closure.

Treat role names as task boundaries, not background processes. Use sequential handoffs for dependent work. Preserve existing user changes, verify paths against current scripts, keep secrets out of files and logs, and do not flash hardware or overwrite the only known-good image without explicit user authorization.

The app working copy in this tree is primarily Agent A scope. Escalate into Agent B/B2/C/D only when the requested outcome crosses those ownership boundaries.

For app-only camera development on a healthy board, default to the recoverable UART development bundle workflow:

```text
Agent A-CAM -> UART development bundle -> Agent D-VAL
```

Back up the active binary and SDK library before installation, verify hashes, restart the service, run a health check, and roll back automatically on failure. Do not rebuild rootfs, package `update.img`, or flash for routine camera application iterations. Enter Agent B/B2/C only when changing rootfs/overlay/systemd integration, SDK runtime integration, kernel/DTS/driver/boot behavior, or performing formal release acceptance. Never use the UART application workflow for boot-chain, kernel, rootfs, or partition changes.

For CameraSDK validation, prove the targeted Insta360 USB identity (vendor `2e1a` by default), actual link speed, and the latest service `connected=yes` state before sending queries. Apply a product-ID filter only when known. Send `CAMERA_INIT`, status, battery, storage, and list-devices commands sequentially and wait for each worker task to complete. Route same-model and mixed-model devices by unique serial, never model name. Treat `dispatch_ok` only as accepted routing. If the target disappears from `lsusb`, report a hardware/USB blocker rather than restarting or repeatedly querying the SDK.

Use the narrow specialist Skill when applicable: `$youyeetoo-camera-sdk` for camera work, `$youyeetoo-network-gateway` for Ethernet/NB/X links, `$youyeetoo-protocol-audit` for requirement evidence, `$youyeetoo-release-image` for packaging, and `$youyeetoo-board-validation` for board tests.
