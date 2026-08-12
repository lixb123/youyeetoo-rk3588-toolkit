---
name: youyeetoo-board-validation
description: Validate Youyeetoo RK3588 board images and subsystems over SSH or serial with evidence. Use for post-flash checks, youyeetoo-app.service, camera SDK and USB, Ethernet links, CAN, watchdog, storage, worker processes, regression testing, PASS/FAIL/BLOCKED reports, and issue closure.
---

# Youyeetoo Board Validation

Declare `Agent D-VAL`. Start read-only and test the smallest affected subsystem before broad regression.

## Validation Order

1. Confirm `tested_artifact_type: uart-dev-bundle | update.img`, artifact path, and SHA-256. For a UART bundle, also record the installed app SHA-256 and SDK SHA-256 when included.
2. Confirm board identity, OS, kernel, architecture, uptime, and mounts.
3. Confirm SSH or serial access and the management interface.
4. Confirm services, worker processes, deployed files, and recent logs.
5. Run the targeted camera, network, CAN, storage, or watchdog test.
6. Capture raw evidence and compare with the expected result.
7. Expand to regression only after the target test passes.

Use `scripts/collect-board-snapshot.sh` for a repeatable read-only baseline. Use the relevant specialist Skill for the targeted subsystem.

For camera queries, distinguish three gates: USB identity and speed, CameraSDK session `connected=yes`, and completed worker task results. Do not pass a query from `dispatch_ok` alone. Stop and report `BLOCKED` when the camera disappears from `lsusb`, even if it was connected earlier in the same run.

## Safety

- Do not alter configuration, flash, partition, delete media, or restart a production service without explicit scope.
- If a test requires stopping a service or changing a link, state the impact and restoration command before doing it.
- Keep board timestamps, host timestamps, and RTC mismatch visible.
- Separate a test not run from a test failed.

## Completion Output

Return a validation record with artifact type and hash, installed app/library hashes when applicable, board identity, commands, results, logs, generated files, timing, failures, and final status `PASS`, `FAIL`, or `BLOCKED`. For UART camera validation, distinguish a successful deploy/service recovery from a camera test blocked by missing USB identity; do not label the deployment failed when the artifact installed correctly.
