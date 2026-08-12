---
name: youyeetoo-handover
description: Classify and execute work for the Youyeetoo RK3588 board project across app development, rootfs integration, boot-chain changes, image packaging, flashing handoff, and board validation. Use when a request mentions Youyeetoo, RK3588, youyeetoo_app, CameraSDK, LubanCat SDK, update.img, rootfs, U-Boot, kernel/DTS, NVMe boot slots, board flashing, or board-side test closure.
---

# Youyeetoo Handover

## Start Here

1. Read `references/project-map.md` to locate the current source of truth.
2. Classify the request into exactly one primary role from `references/role-contracts.md`.
3. Read `references/workflow.md` when the task builds, packages, deploys, flashes, or validates an artifact.
4. Read `references/environment.md` only when local paths, WSL, SSH, hardware, or credentials matter.
5. Read `references/hidden-risks.md` before rootfs, boot-chain, packaging, flashing, or recovery work.

## Role Routing

- Agent A: application logic, camera control, task dispatch, CAN application behavior, telemetry, and app-owned configuration.
- Agent B: rootfs, overlay integration, packages, runtime directories, and systemd.
- Agent B2: U-Boot, kernel, DTS, PCIe, NVMe, boot slots, metadata, and board bring-up.
- Agent C: final image packaging, artifact integrity, checksums, and Windows-side flash handoff.
- Agent D: board validation, evidence collection, regression checks, and issue closure.

## Specialist Skill Routing

- Agent A-CAM: `$youyeetoo-camera-sdk`
- Agent A-NET: `$youyeetoo-network-gateway`
- Agent A-ICD: `$youyeetoo-protocol-audit`
- Agent C-REL: `$youyeetoo-release-image`
- Agent B2-REC: `$youyeetoo-rk3588-flash`
- Agent D-VAL: `$youyeetoo-board-validation`

Load one specialist Skill only after the task boundary is clear. Keep `$youyeetoo-handover` as the router and cross-role handoff contract.

Treat these names as task roles, not persistent background processes. Declare the selected role at the start of substantial work. If work crosses boundaries, finish and document one role's output before entering the next role.

## Required Workflow

Keep the handoff order explicit:

1. Compile the app.
2. Sync app-owned files into the overlay.
3. Rebuild the rootfs when required.
4. Package the final image.
5. Record and hand off the Windows-side artifact.
6. Flash only when the user explicitly authorizes flashing.
7. Validate the affected subsystem on the board and record evidence.

For app-only camera work on a healthy board, default to the recoverable UART development path:

```text
Agent A-CAM -> UART development bundle -> Agent D-VAL
```

Skip Agent B and Agent C for this path. Require backup, SHA-256 verification, service health checks, evidence capture, and automatic rollback on failure. Use the full image path when the change affects overlay/rootfs/systemd integration, the SDK runtime integration, kernel/DTS/drivers, boot behavior, packaging, or formal release acceptance:

```text
Agent A-CAM -> Agent B -> Agent C-REL -> explicit flash approval -> Agent D-VAL
```

## Safety And Evidence

- Inspect current scripts and files before trusting old documentation.
- Preserve existing user changes and known-good images.
- Never store passwords, private-key contents, tokens, or other secrets in the skill, project documentation, logs, or responses.
- Resolve the exact image and device before any flash or recovery command.
- Use read-only SSH checks first during Agent D validation.
- Record commands, artifact paths, hashes, versions, timestamps, observed results, and relevant logs.
- Distinguish verified facts from assumptions and stale documentation.

## Completion Output

Report:

- selected role and scope;
- files or artifacts changed;
- commands and verification performed;
- handoff package for the next role;
- remaining risks, blockers, and tests not run.
