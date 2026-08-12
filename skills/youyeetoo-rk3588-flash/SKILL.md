---
name: youyeetoo-rk3588-flash
description: Plan and safely execute Youyeetoo RK3588 image flashing, RKDevTool handoff, loader or Maskrom recovery, rollback, serial-console diagnosis, and post-flash identity checks. Use when the task mentions RKDevTool, update.img flashing, loader mode, Maskrom, SPI loader, NVMe boot recovery, serial console, image rollback, or a board that no longer reaches SSH.
---

# Youyeetoo RK3588 Flash And Recovery

Declare `Agent B2-REC`. This Skill is safety-first and does not provide an unattended flash script.

## Before Any Mutation

1. Identify the exact board and its current boot state.
2. Identify the exact image path and calculate SHA-256.
3. Confirm the image is intended for this board, SoC, storage target, and boot mode.
4. Preserve the last known-good project image and vendor recovery image.
5. Confirm the operator has authorized flashing or recovery.
6. Choose the least destructive path: SSH copy, normal maintenance, loader, then Maskrom recovery only if necessary.

## Diagnosis Order

- If SSH works, use read-only checks first.
- If the board reaches login but the app is wrong, prefer app-level correction over full rollback.
- If normal boot fails before SSH, switch to UART serial at 1500000 8N1 with no flow control.
- Classify the stop point as loader, U-Boot, kernel handoff, rootfs, or service startup.
- Use RKDevTool only after confirming the USB mode and target image.
- Use Maskrom and vendor recovery only when ordinary paths cannot recover the board.

## Evidence And Stop Conditions

Stop and request clarification when the target board, image, storage device, partition, or authorization is ambiguous. Record RKDevTool version, USB state, image hash, serial output, tool result, and recovery outcome. Never guess partition maps or erase unrelated storage.
