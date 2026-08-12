---
name: youyeetoo-release-image
description: Build and hand off Youyeetoo RK3588 application, rootfs, and update.img release artifacts. Use for Makefile builds, CameraSDK library deployment, overlay sync, Ubuntu rootfs rebuild, pack_update.sh, SDK build.sh, image size and SHA-256 checks, release manifests, and Agent C packaging handoff.
---

# Youyeetoo Release Image

Declare `Agent C-REL` for final packaging. Agent B may use this Skill only for rootfs inputs; Agent C owns the release artifact and handoff.

## Pipeline

1. Inspect the current source tree, scripts, dirty changes, and input artifacts.
2. Build the app with the complete AArch64 CameraSDK package.
3. Sync app-owned files into overlay.
4. Rebuild rootfs when the change requires image integration.
5. Inspect and run the current packaging entry point.
6. Verify the final `update.img` path, size, timestamp, and SHA-256.
7. Preserve the previous known-good artifact before replacing the Windows handoff file.
8. Produce the handoff manifest for Agent D.

## Gates

- Do not invent missing SDK, LubanCat, or WSL paths; locate them first.
- Do not call a successful compile a board validation result.
- Do not overwrite the only known-good image without a versioned backup and hash.
- Do not flash the board from this Skill. Require a separate explicit user authorization and use `$youyeetoo-rk3588-flash`.
- Use direct app deployment instead of a full image only when the board is healthy and the change is clearly app-only.

## Completion Output

Report source snapshot, commands, inputs, artifact path, size, SHA-256, backup path, included changes, expected tests, blockers, and the exact handoff message for Agent D.
