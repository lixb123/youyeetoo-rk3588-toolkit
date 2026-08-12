---
name: youyeetoo-camera-sdk
description: Develop, integrate, diagnose, and validate the Insta360 CameraSDK path on the Youyeetoo RK3588 Ubuntu board. Use for CameraSDKTest, libCameraSDK.so, CameraSdkAdapter, camera_worker, USB discovery, X5 and other Insta360 models, same-model multi-camera or mixed-model routing, photo/video control, media listing or download, SDK logs, HTTP fallback, and camera regression tasks.
---

# Youyeetoo Camera SDK

## Route The Task

1. Read `references/project-facts.md` before making environment or version claims.
2. Read `references/test-workflow.md` for board commands, service commands, or A/B reproduction work.
3. Read `references/uart-development.md` when building, uploading, installing, rolling back, or validating an app-only camera change over UART.
4. Read `references/known-issues.md` before diagnosing media listing, `.insv`, `.lrv`, download, delete, or multi-camera behavior.
5. Use `scripts/collect-camera-facts.sh` for a read-only board snapshot when shell access is available.

Declare `Agent A-CAM` for implementation and `Agent D-VAL` for board-only validation.

## Work In Layers

Choose exactly one starting layer:

- USB/runtime: prove architecture, USB identity, link speed, process ownership, library loading, and basic SDK open.
- Vendor SDK: reproduce with the unmodified `CameraSDKTest` before blaming application code.
- Adapter: inspect `camera_sdk_adapter.*` and map the failing application operation to the exact SDK call.
- Controller/service: inspect timeout, task state, output path, worker isolation, command inbox, and result metadata.
- Multi-camera/model: verify SDK camera type, physical USB topology, unique serials, slot mapping, per-model capabilities, and session ownership.

Do not jump from an application symptom directly to an SDK defect claim.

## Safety Rules

- Treat CameraSDK access as exclusive. Stop the production service before launching `CameraSDKTest`, then restore it after testing.
- Start with listing, status, battery, storage, and non-destructive capture tests.
- Do not delete media, upgrade firmware, shut down the camera, or change persistent settings unless explicitly requested.
- Preserve raw SDK logs, application logs, exact remote paths, local file sizes, and SHA-256 values.
- Use a confirmed-readable media file for SDK-versus-HTTP A/B tests.
- Keep passwords, private keys, and device secrets out of artifacts.
- Treat `dispatch_ok` as command-routing evidence only. Require a completed worker task summary and updated runtime values before claiming a CameraSDK query passed.
- Before status, battery, or storage queries, confirm the targeted Insta360 USB device remains in `lsusb` and the current service snapshot says `connected=yes`. Default to vendor `2e1a`; apply a product filter only when the target product ID is known. Stop querying when the USB identity disappears.
- Route devices by unique SDK serial. Never use `camera_name`, `camera_type`, or model name as a multi-camera identity.
- Treat extended capture modes as capabilities. Reject unverified model/mode pairs with an explicit error instead of applying X5 parameters.

## Completion Output

Report the tested layer, hardware and SDK identity, exact operation, expected and observed results, logs and artifacts, likely fault boundary, confidence, and the next discriminating test.
