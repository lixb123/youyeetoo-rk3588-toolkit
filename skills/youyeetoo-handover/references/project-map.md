# Project Map

## Current Windows Locations

| Area | Current location | Purpose |
| --- | --- | --- |
| App working copy | `<workspace>\youyeetoo_app` | Application and CameraSDK adapter work |
| RK3588 WSL helper files | `<workspace>` | WSL launch and cross-build environment notes |
| Project archive and flash assets | `<project-archive>` | Archive, vendor material, SSH key copy, and flash handoff |
| Windows flash directory | `<flash-dir>` | `update.img` and recovery material |
| Project input documents | `<project-docs>` | Interface specifications and project-level Codex instructions |
| Installed personal skill | `%USERPROFILE%\.codex\skills\youyeetoo-handover` | Reusable Codex workflow |

## Expected Full Linux Project Tree

Historical project documentation refers to a full tree under `<linux-project-root>`. The configured WSL distribution is `RK3588-Ubuntu22`, whose recommended workspace is `/home/rk3588/workspace`.

Do not assume either Linux path currently contains the full source. Locate the actual tree before building:

```bash
find /home -maxdepth 4 -type d -name youyeetoo_app 2>/dev/null
find /home -maxdepth 4 -type d -name LubanCat_Linux_Generic_SDK_20260424 2>/dev/null
find /home -maxdepth 4 -type d -name CameraSDK 2>/dev/null
```

## Ownership Boundaries

| Layer | Typical source | Primary role |
| --- | --- | --- |
| App | `youyeetoo_app/` | Agent A |
| Rootfs and overlay | `ubuntu22.04/`, `overlay/`, systemd | Agent B |
| Boot and board integration | `u-boot/`, `kernel-6.1/`, `nvme_img/` | Agent B2 |
| Image packaging | `pack_update.sh`, SDK `build.sh`, `output/update/Image/` | Agent C |
| Test closure | `测试/`, board logs, execution records | Agent D |

When documentation and scripts disagree, prefer the current script after verifying its behavior. Report missing trees or external dependencies instead of inventing paths.
