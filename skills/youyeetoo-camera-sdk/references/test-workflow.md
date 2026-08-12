# Camera Test Workflow

## Read-Only Snapshot

Copy `scripts/collect-camera-facts.sh` to the board or pipe it through SSH. Save its output with the test timestamp.

Minimum manual checks:

```bash
uname -m
lsusb | grep -i '2e1a:'
lsusb -t
systemctl status youyeetoo-app.service --no-pager
journalctl -u youyeetoo-app.service -n 200 --no-pager
```

## Vendor SDK Smoke Test

Stop the service first because both paths may compete for the same camera:

```bash
sudo systemctl stop youyeetoo-app.service
cd /mnt/userdata/youyeetoo/repro
sudo env LD_LIBRARY_PATH="$PWD/lib" ./bin/CameraSDKTest
```

Use non-destructive menu items first: list files, capture status, battery, storage, connectivity, then photo or short video if authorized. Exit cleanly and restore the service:

```bash
sudo systemctl start youyeetoo-app.service
```

## Production Service Commands

The application consumes commands from `/var/opt/youyeetoo/runtime/telemetry_command_request.txt`.

Examples:

```bash
printf '%s\n' 'CAMERA_LIST_DEVICES' | sudo tee -a /var/opt/youyeetoo/runtime/telemetry_command_request.txt
printf '%s\n' 'CAMERA_TAKE_PHOTO camera_serial=<SERIAL>' | sudo tee -a /var/opt/youyeetoo/runtime/telemetry_command_request.txt
printf '%s\n' 'CAMERA_LIST_MEDIA camera_serial=<SERIAL>' | sudo tee -a /var/opt/youyeetoo/runtime/telemetry_command_request.txt
```

Follow the service journal and inspect generated media plus JSON metadata. Use the exact command syntax documented in `CAM016_SDK指令封装清单.md`.

For same-model or mixed-model testing, list devices once, record every serial and model key, then repeat read-only queries with `camera_serial=<SERIAL>`. A complete matrix includes one X5, two X5 devices, and X5 plus another SDK-supported model. Mark missing hardware as `BLOCKED`; do not claim mixed-model validation from discovery code alone.

## Download A/B Test

1. Select a newly recorded or otherwise confirmed-readable remote file.
2. Record its exact remote path.
3. Download through unmodified `CameraSDKTest`.
4. Download the same path through the camera HTTP endpoint when available.
5. Compare HTTP status, file size, and SHA-256.
6. Do not infer an extension-wide defect from one unreadable media record.
