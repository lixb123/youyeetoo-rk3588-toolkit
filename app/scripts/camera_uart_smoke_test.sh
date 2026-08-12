#!/usr/bin/env bash

set -euo pipefail

INBOX="${INBOX:-/var/opt/youyeetoo/runtime/telemetry_command_request.txt}"
OUT_DIR="${OUT_DIR:-/var/opt/youyeetoo/runtime/dev-tests}"
SERVICE_NAME="${SERVICE_NAME:-youyeetoo-app.service}"
JOURNALCTL="${JOURNALCTL:-journalctl}"
CAMERA_USB_VENDOR_ID="${CAMERA_USB_VENDOR_ID:-2e1a}"
CAMERA_USB_PRODUCT_ID="${CAMERA_USB_PRODUCT_ID:-}"
TAKE_PHOTO=0
VIDEO_SECONDS=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --take-photo) TAKE_PHOTO=1; shift ;;
    --short-video-seconds) VIDEO_SECONDS="$2"; shift 2 ;;
    -h|--help) echo "Usage: $0 [--take-photo] [--short-video-seconds N]"; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
JOURNAL_SINCE="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
EVIDENCE_DIR="${OUT_DIR}/${STAMP}"
install -d "${EVIDENCE_DIR}"
{
  date -u +%Y-%m-%dT%H:%M:%SZ
  uname -a
  cat /etc/os-release
  if command -v lsusb >/dev/null; then
    lsusb | awk -v vendor="${CAMERA_USB_VENDOR_ID}" -v products="${CAMERA_USB_PRODUCT_ID}" '
      $1 == "Bus" && $6 ~ /^[[:xdigit:]]+:[[:xdigit:]]+$/ {
        split($6, ids, ":")
        allowed = (products == "")
        count = split(products, configured, ",")
        for (i = 1; i <= count; i++) {
          gsub(/[[:space:]]/, "", configured[i])
          if (tolower(ids[2]) == tolower(configured[i])) allowed = 1
        }
        if (tolower(ids[1]) == tolower(vendor) && allowed) print
      }'
  fi
  systemctl is-active "${SERVICE_NAME}" || true
} > "${EVIDENCE_DIR}/baseline.txt"

send_command() {
  local command="$1"
  local before_count=0
  local after_count=0
  local latest_status=""
  latest_status="$("${JOURNALCTL}" -u "${SERVICE_NAME}" -n 50 --no-pager 2>/dev/null |
    grep 'profile=BOARD_CONTROLLER' | tail -n 1 || true)"
  before_count="$(sed -n 's/.*completed_tasks=\([0-9][0-9]*\).*/\1/p' <<<"${latest_status}")"
  before_count="${before_count:-0}"
  printf '%s\n' "${command}" >> "${INBOX}"
  for _ in $(seq 1 "${COMMAND_WAIT_STEPS:-60}"); do
    latest_status="$("${JOURNALCTL}" -u "${SERVICE_NAME}" --since "${JOURNAL_SINCE}" --no-pager 2>/dev/null |
      grep "last_command=${command}" | tail -n 1 || true)"
    after_count="$(sed -n 's/.*completed_tasks=\([0-9][0-9]*\).*/\1/p' <<<"${latest_status}")"
    after_count="${after_count:-0}"
    if [[ "${latest_status}" == *"task_active=no"* && "${after_count}" -gt "${before_count}" ]]; then
      printf '%s\n' "${latest_status}" >> "${EVIDENCE_DIR}/command-results.log"
      return 0
    fi
    sleep 0.5
  done
  echo "Camera command did not complete: ${command}" >&2
  return 1
}

send_command CAMERA_LIST_DEVICES
send_command CAMERA_GET_STATUS
send_command CAMERA_GET_BATTERY
send_command CAMERA_GET_STORAGE

if [[ "${TAKE_PHOTO}" -eq 1 ]]; then
  send_command CAMERA_TAKE_PHOTO
fi
if [[ "${VIDEO_SECONDS}" -gt 0 ]]; then
  send_command CAMERA_VIDEO_START
  sleep "${VIDEO_SECONDS}"
  send_command CAMERA_VIDEO_STOP
fi

sleep "${SETTLE_SECONDS:-5}"
"${JOURNALCTL}" -u "${SERVICE_NAME}" --since "${JOURNAL_SINCE}" --no-pager > "${EVIDENCE_DIR}/service.log" || true
cp -f "${INBOX}" "${EVIDENCE_DIR}/command-inbox.snapshot" 2>/dev/null || true
printf 'CAMERA_UART_SMOKE_OK evidence=%s destructive_photo=%s video_seconds=%s\n' \
  "${EVIDENCE_DIR}" "${TAKE_PHOTO}" "${VIDEO_SECONDS}"
