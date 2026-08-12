# Hidden Risks

- The full Linux SDK tree may not be present in the configured WSL workspace. Locate it before building.
- `camera_sdk_repro` contains an AArch64 runtime library and test binary but not complete CameraSDK headers.
- Rootfs generation may require chroot, QEMU user emulation, package mirrors, and an earlier base build.
- `FAST=1` is only valid after a successful full rootfs build.
- For RK3588 U-Boot, use the vendor SDK-supported build route and verify image format.
- A successful package build does not prove the board boots or boot-slot metadata is correct.
- A bad image can remove SSH access. Maintain serial-console and known-good recovery capability.
- Do not overwrite the only known-good `update.img` without preserving a versioned backup and its hash.
- Full reflashing is unnecessary for many app-only defects; use direct deployment when appropriate.
- CameraSDK access is generally exclusive. Stop the production camera service before running the vendor test program.
- Some old camera media return HTTP `400/1004` through both SDK and direct HTTP; do not misclassify this as an extension-wide SDK defect.
- `BUG-A-010` remains relevant: some 8K files without LRV may be invisible to the current media-list path.
- CAN, long-duration watchdog behavior, NVMe boot, and multi-camera topology still require hardware evidence.
- WSL build stalls can be environmental. Check WSL health before diagnosing project code.
