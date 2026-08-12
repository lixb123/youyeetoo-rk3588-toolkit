# Camera Project Facts

## Current Working Material

- App: `<workspace>\youyeetoo_app`
- Adapter: `youyeetoo_app/src/camera_sdk_adapter.cpp`
- Controller: `youyeetoo_app/src/camera_controller.cpp`
- Worker isolation: `youyeetoo_app/src/camera_worker.cpp`
- Command definitions: `youyeetoo_app/configs/telemetry_commands.txt`
- Port map: `youyeetoo_app/configs/camera_port_map.txt`
- SDK reproduction bundle: `<workspace>\camera_sdk_repro`
- Command-to-SDK map: `youyeetoo_app/docs/CAM016_SDK指令封装清单.md`

## Last Verified Board Baseline

- Board: Rockchip RK3588 EVB7 V11
- OS: Ubuntu 22.04.5 LTS, AArch64
- Kernel: Linux 6.1.75
- Last tested camera: Insta360 X5; two X5 devices have previously been routed by serial
- Last tested USB identity: `2e1a:0002`
- Expected USB link: 5000M
- CameraSDK: 2.1.1 AArch64
- Production service: `youyeetoo-app.service`

Treat these as a last-known baseline. Re-collect them for every new defect report.

CameraSDK 2.1.1 exposes `One X`, `One R`, `One RS`, `One X2`, `X3`, `X4`, `X5`, `X4 Air`, and `Unknown`. Discovery support does not prove every operation is supported. Keep the SDK `camera_type` as `model_key`, keep serial as device identity, and validate each model/mode pair before enabling model-specific parameters.

## Build Boundary

The reproduction bundle contains an AArch64 `CameraSDKTest` and `libCameraSDK.so`, but not the complete SDK headers. Rebuilding `youyeetoo_app` requires the vendor package with `include/` and `lib/libCameraSDK.so`.

The application Makefile links `-lCameraSDK -latomic -pthread -ldl` and expects the deployed library beside the application under `../lib` through its runtime path.
