#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

APP_BIN="${APP_DIR}/build/youyeetoo_app"
CAMERA_SDK_DIR="${CAMERA_SDK_DIR:-${APP_DIR}/../CameraSDK}"
CAMERA_SDK_LIB="${CAMERA_SDK_DIR}/lib/libCameraSDK.so"
LUBANCAT_UBUNTU22_DIR="${LUBANCAT_UBUNTU22_DIR:-${APP_DIR}/../LubanCat_Linux_Generic_SDK_20260424/ubuntu22.04}"
OVERLAY_DIR="${OVERLAY_DIR:-${LUBANCAT_UBUNTU22_DIR}/overlay}"
TARGET_DIR="${TARGET_DIR:-${OVERLAY_DIR}/opt/youyeetoo_app}"

TARGET_BIN_DIR="${TARGET_DIR}/bin"
TARGET_LIB_DIR="${TARGET_DIR}/lib"
TARGET_CONFIG_DIR="${TARGET_DIR}/configs"
TARGET_EXAMPLE_DIR="${TARGET_DIR}/examples/systemd"
APP_CONFIG_DIR="${APP_DIR}/configs"
APP_SYSTEMD_DIR="${APP_DIR}/systemd"

if [[ ! -f "${APP_BIN}" ]]; then
  echo "App binary not found: ${APP_BIN}" >&2
  echo "Run 'make' first." >&2
  exit 1
fi

if [[ ! -f "${CAMERA_SDK_LIB}" ]]; then
  echo "Camera SDK library not found: ${CAMERA_SDK_LIB}" >&2
  exit 1
fi

if [[ ! -d "${OVERLAY_DIR}" ]]; then
  echo "Overlay directory not found: ${OVERLAY_DIR}" >&2
  exit 1
fi

echo "Installing app to ${TARGET_BIN_DIR}"
install -d "${TARGET_BIN_DIR}" "${TARGET_LIB_DIR}" "${TARGET_CONFIG_DIR}" "${TARGET_EXAMPLE_DIR}"
install -m 0755 "${APP_BIN}" "${TARGET_BIN_DIR}/youyeetoo_app"

echo "Installing CameraSDK library to ${TARGET_LIB_DIR}"
install -m 0644 "${CAMERA_SDK_LIB}" "${TARGET_LIB_DIR}/libCameraSDK.so"

if [[ -d "${APP_CONFIG_DIR}" ]]; then
  echo "Installing app configs to ${TARGET_CONFIG_DIR}"
  install -m 0644 "${APP_CONFIG_DIR}"/* "${TARGET_CONFIG_DIR}/"
fi

if [[ -d "${APP_SYSTEMD_DIR}" ]]; then
  echo "Installing systemd service example to ${TARGET_EXAMPLE_DIR}"
  install -m 0644 "${APP_SYSTEMD_DIR}"/*.service "${TARGET_EXAMPLE_DIR}/"
fi

echo
echo "Done."
echo "Overlay updated:"
echo "  ${TARGET_DIR}"
echo
echo "Next step:"
echo "  cd ${LUBANCAT_UBUNTU22_DIR} && ./mk-ubuntu-rootfs.sh"
