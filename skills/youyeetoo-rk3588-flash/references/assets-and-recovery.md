# RK3588 Flash Assets And Recovery Facts

## Windows Handoff Directory

```text
<flash-dir>
```

Known material includes a project `update.img`, a vendor Ubuntu image, an SPI loader, RKDevTool, and text instructions for Ubuntu and NVMe flashing. Inspect current filenames and hashes before use; do not assume a file is current solely because it exists.

## Recovery References

- `<flash-dir>\22Ubuntu刷机方法.txt`
- `<flash-dir>\鲁班猫5镜像刷入nvme方法.txt`
- `<flash-dir>\rkspi_loader_lubancat_5.img`

## Serial

- Baud: `1500000`
- Format: `8N1`
- Flow control: none

## Safe Handoff Record

```text
target_board:
boot_state_before:
image_path:
image_sha256:
image_size:
storage_target:
tool_and_version:
authorization:
serial_evidence:
rollback_path:
```
