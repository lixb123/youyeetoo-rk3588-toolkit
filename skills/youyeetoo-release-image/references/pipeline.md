# Release Pipeline Facts

## App Build

The app Makefile expects a sibling or explicitly supplied `CameraSDK` directory with `include/` and `lib/`. It links `libCameraSDK.so`, `libatomic`, pthread, and dl and uses an origin-relative runtime path.

## Overlay And Rootfs

The app-owned sync script installs the application binary, SDK library, configs, and systemd example into the Ubuntu overlay. The rootfs script may require an existing base tree, chroot/QEMU support, sudo, and network package mirrors. `FAST=1` is not a first-build replacement.

## Packaging

Inspect the current `pack_update.sh` and SDK `build.sh` before execution. The documented current Windows handoff path is:

```text
<flash-dir>\update.img
```

The path is an operational default, not permission to overwrite it blindly.
