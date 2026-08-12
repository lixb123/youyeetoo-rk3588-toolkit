#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${APP_DIR}/dist/uart-dev"
INCLUDE_SDK=0

usage() {
  echo "Usage: $0 [--include-sdk] [--output-dir DIR]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --include-sdk) INCLUDE_SDK=1; shift ;;
    --output-dir) OUT_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

APP_BIN="${APP_DIR}/build/youyeetoo_app"
CAMERA_SDK_DIR="${CAMERA_SDK_DIR:-${APP_DIR}/../CameraSDK}"
SDK_LIB="${CAMERA_SDK_DIR}/lib/libCameraSDK.so"
STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

if [[ ! -x "${APP_BIN}" ]]; then
  echo "App binary not found or not executable: ${APP_BIN}" >&2
  echo "Run make first." >&2
  exit 1
fi

mkdir -p "${STAGE}/bin" "${STAGE}/lib" "${STAGE}/tools" "${OUT_DIR}"
install -m 0755 "${APP_BIN}" "${STAGE}/bin/youyeetoo_app"
for tool in install_uart_dev_bundle.sh rollback_uart_dev.sh camera_dev_healthcheck.sh camera_uart_smoke_test.sh; do
  install -m 0755 "${SCRIPT_DIR}/${tool}" "${STAGE}/tools/${tool}"
done
if [[ "${INCLUDE_SDK}" -eq 1 ]]; then
  [[ -f "${SDK_LIB}" ]] || { echo "CameraSDK library not found: ${SDK_LIB}" >&2; exit 1; }
  install -m 0644 "${SDK_LIB}" "${STAGE}/lib/libCameraSDK.so"
fi

{
  echo "format=youyeetoo-uart-dev-v1"
  echo "created_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "app_sha256=$(sha256sum "${STAGE}/bin/youyeetoo_app" | awk '{print $1}')"
  if [[ -f "${STAGE}/lib/libCameraSDK.so" ]]; then
    echo "sdk_sha256=$(sha256sum "${STAGE}/lib/libCameraSDK.so" | awk '{print $1}')"
  else
    echo "sdk_included=false"
  fi
} > "${STAGE}/bundle.info"

(
  cd "${STAGE}"
  sha256sum bin/youyeetoo_app
  if [[ -f lib/libCameraSDK.so ]]; then
    sha256sum lib/libCameraSDK.so
  fi
) > "${STAGE}/manifest.sha256"

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BUNDLE="${OUT_DIR}/youyeetoo-uart-dev-${STAMP}.tar.gz"
tar -czf "${BUNDLE}" -C "${STAGE}" bin lib tools bundle.info manifest.sha256
cp -f "${BUNDLE}" "${OUT_DIR}/youyeetoo-uart-dev.tar.gz"
sha256sum "${BUNDLE}" > "${BUNDLE}.sha256"
sha256sum "${OUT_DIR}/youyeetoo-uart-dev.tar.gz" > "${OUT_DIR}/youyeetoo-uart-dev.tar.gz.sha256"

echo "UART_DEV_BUNDLE=${BUNDLE}"
echo "UART_DEV_BUNDLE_LATEST=${OUT_DIR}/youyeetoo-uart-dev.tar.gz"
echo "UART_DEV_BUNDLE_SHA256=$(sha256sum "${BUNDLE}" | awk '{print $1}')"
