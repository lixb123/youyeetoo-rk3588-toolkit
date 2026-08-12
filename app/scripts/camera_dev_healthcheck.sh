#!/usr/bin/env bash

set -euo pipefail

APP_ROOT="${APP_ROOT:-/opt/youyeetoo_app}"
SERVICE_NAME="${SERVICE_NAME:-youyeetoo-app.service}"
SYSTEMCTL="${SYSTEMCTL:-systemctl}"
JOURNALCTL="${JOURNALCTL:-journalctl}"
PROCESS_CHECK="${PROCESS_CHECK:-1}"
CAMERA_USB_VENDOR_ID="${CAMERA_USB_VENDOR_ID:-2e1a}"
CAMERA_USB_PRODUCT_ID="${CAMERA_USB_PRODUCT_ID:-}"
REQUIRE_CAMERA=0

if [[ "${1:-}" == "--require-camera" ]]; then
  REQUIRE_CAMERA=1
elif [[ $# -gt 0 ]]; then
  echo "Usage: $0 [--require-camera]" >&2
  exit 2
fi

failures=()

product_allowed() {
  local actual="${1,,}"
  local configured="${CAMERA_USB_PRODUCT_ID,,}"
  [[ -z "${configured}" ]] && return 0
  local item
  IFS=',' read -ra product_ids <<<"${configured}"
  for item in "${product_ids[@]}"; do
    item="${item//[[:space:]]/}"
    [[ "${actual}" == "${item}" ]] && return 0
  done
  return 1
}
[[ -x "${APP_ROOT}/bin/youyeetoo_app" ]] || failures+=("app executable missing")
[[ -f "${APP_ROOT}/lib/libCameraSDK.so" ]] || failures+=("CameraSDK library missing")
"${SYSTEMCTL}" is-active --quiet "${SERVICE_NAME}" || failures+=("service inactive")

if [[ "${PROCESS_CHECK}" == "1" ]] && ! pgrep -x youyeetoo_app >/dev/null; then
  failures+=("app process missing")
fi

if [[ "${REQUIRE_CAMERA}" -eq 1 ]]; then
  command -v lsusb >/dev/null || failures+=("lsusb unavailable")
  if command -v lsusb >/dev/null; then
    camera_usb_present=0
    while read -r _ _ _ _ _ usb_id _; do
      [[ "${usb_id:-}" == *:* ]] || continue
      usb_vendor="${usb_id%%:*}"
      usb_product="${usb_id#*:}"
      if [[ "${usb_vendor,,}" == "${CAMERA_USB_VENDOR_ID,,}" ]] &&
         product_allowed "${usb_product}"; then
        camera_usb_present=1
        break
      fi
    done < <(lsusb)
    if [[ "${camera_usb_present}" -ne 1 ]]; then
      expected_usb="${CAMERA_USB_VENDOR_ID}:"
      [[ -n "${CAMERA_USB_PRODUCT_ID}" ]] && expected_usb+="{${CAMERA_USB_PRODUCT_ID}}"
      failures+=("camera USB ${expected_usb} missing")
    fi
  fi
  latest_status="$("${JOURNALCTL}" -u "${SERVICE_NAME}" -n 200 --no-pager 2>/dev/null |
    grep 'profile=BOARD_CONTROLLER' | tail -n 1 || true)"
  if [[ "${latest_status}" != *"connected=yes"* ]]; then
    failures+=("latest service status is not connected=yes")
  fi
fi

if [[ ${#failures[@]} -gt 0 ]]; then
  printf 'CAMERA_DEV_HEALTH_FAIL: %s\n' "$(IFS='; '; echo "${failures[*]}")" >&2
  exit 1
fi

echo "CAMERA_DEV_HEALTH_OK service=${SERVICE_NAME} app=${APP_ROOT}/bin/youyeetoo_app require_camera=${REQUIRE_CAMERA}"
