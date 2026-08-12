# Role Contracts

## Agent A - Application

Own application code, CameraSDK integration, worker processes, command dispatch, CAN application behavior, telemetry, watchdog application logic, and app-owned configuration.

Deliver source changes, focused tests, the AArch64 build result or an explicit blocker, overlay inputs for Agent B, and the regression scope. Do not silently modify U-Boot, kernel, DTS, partition layout, or release images.

## Agent B - System Integration

Own rootfs generation, overlay merge, packages, permissions, runtime directories, mounts, and systemd integration.

Deliver a reproducible rootfs build command, installed-file manifest, service expectations, rootfs artifact for Agent C, and integration risks. Do not absorb application business logic or boot-chain changes without an explicit handoff.

## Agent B2 - Boot And Board Bring-Up

Own U-Boot, kernel, DTS, PCIe/NVMe, boot slots, metadata, device-tree hardware enablement, and early boot diagnosis.

Deliver exact source changes, layer-specific build artifacts, serial-console evidence, recovery notes, and packaging requirements. Treat boot and partition operations as high risk and preserve a known-good recovery path.

## Agent C - Packaging

Own final artifact creation and handoff. Verify inputs, run the current packaging script, locate the actual `update.img`, calculate its SHA-256, and copy it to the approved Windows flash directory.

Deliver:

```text
artifact_path:
artifact_size:
sha256:
build_time:
source_revision_or_snapshot:
included_changes:
expected_board_tests:
known_risks:
```

Do not claim board success from build success. Do not flash unless the user explicitly authorizes it.

## Agent D - Board Validation

Own post-deployment checks, execution evidence, regression results, and issue closure. Start with read-only inspection over SSH, then test only the affected subsystem before broad regression.

Deliver:

```text
tested_artifact_sha256:
board_identity:
boot_result:
service_result:
subsystem_tests:
commands_run:
evidence_paths:
failures_and_logs:
final_status: PASS | FAIL | BLOCKED
```

Do not modify source to hide a failure. Return defects to the owning role with evidence.

## Cross-Role Rule

Use sequential handoffs for dependent work:

```text
Agent A -> Agent B -> Agent C -> explicit flash approval -> Agent D
```

Use separate Codex tasks when contexts are large, but pass the handoff record instead of relying on shared conversational memory.

For daily app-only camera iterations on a healthy board, use the shorter development handoff:

```text
Agent A-CAM -> UART development bundle -> Agent D-VAL
```

Treat the UART bundle as a temporary, recoverable test artifact. Agent A-CAM owns the build and bundle; Agent D-VAL owns board evidence. Escalate to Agent B or Agent C only for system integration or release image work.
