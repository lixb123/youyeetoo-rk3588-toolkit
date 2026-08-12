# Environment And Access

## Known Local Resources

- WSL distribution: `RK3588-Ubuntu22`
- Recommended WSL workspace: `/home/rk3588/workspace`
- Windows flash area: `<flash-dir>`
- App working copy: `<workspace>\youyeetoo_app`
- Project documents: `<project-docs>`
- Board address documented by the project: `192.168.137.2`
- Board user documented by the project: `cat`

Treat all paths and network values as last-known information. Verify them before mutation.

## External Dependencies

- Complete AArch64 CameraSDK package with `include/` and `lib/libCameraSDK.so`
- LubanCat RK3588 SDK and cross-toolchain
- Board and USB camera hardware
- SSH private key or an interactive credential supplied outside the repository
- Windows network sharing for the documented board subnet
- Serial adapter for early-boot recovery
- CAN analyzer or injector for full CAN validation

Never place private-key contents or passwords in prompts, skills, `AGENTS.md`, project documentation, or logs. Refer to a credential path only when needed, and verify filesystem permissions.

## Board And Recovery

- Prefer SSH for normal post-boot validation.
- Use serial console for loader, U-Boot, kernel handoff, or rootfs boot failure.
- Last-known serial parameters are `1500000`, `8N1`, and no flow control.
- Treat `<flash-dir>` as containing operational recovery assets; inspect before overwriting anything.

## Camera Routing

Logical names such as `cam0` and `cam1` resolve through `/opt/youyeetoo_app/configs/camera_port_map.txt`. The mapping depends on physical USB topology, so changing ports or hubs requires recalibration.
