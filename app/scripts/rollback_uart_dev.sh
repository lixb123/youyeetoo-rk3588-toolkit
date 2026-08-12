#!/usr/bin/env bash

set -euo pipefail

APP_ROOT="${APP_ROOT:-/opt/youyeetoo_app}"
BACKUP_ROOT="${BACKUP_ROOT:-${APP_ROOT}/dev/backups}"
SERVICE_NAME="${SERVICE_NAME:-youyeetoo-app.service}"
SYSTEMCTL="${SYSTEMCTL:-systemctl}"
BACKUP="${1:-}"

if [[ "${EUID}" -ne 0 && "${ALLOW_NON_ROOT:-0}" != "1" ]]; then
  echo "Run rollback as root." >&2
  exit 1
fi

if [[ -z "${BACKUP}" ]]; then
  BACKUP="$(find "${BACKUP_ROOT}" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort | tail -n 1)"
fi
[[ -n "${BACKUP}" && -d "${BACKUP}" ]] || { echo "Backup not found: ${BACKUP:-<latest>}" >&2; exit 1; }
[[ -f "${BACKUP}/backup.info" ]] || { echo "Invalid backup: missing backup.info" >&2; exit 1; }

# shellcheck disable=SC1090
source "${BACKUP}/backup.info"
"${SYSTEMCTL}" stop "${SERVICE_NAME}" || true
install -d "${APP_ROOT}/bin" "${APP_ROOT}/lib"

if [[ "${app_existed}" == "true" ]]; then
  install -m 0755 "${BACKUP}/youyeetoo_app" "${APP_ROOT}/bin/youyeetoo_app"
else
  rm -f "${APP_ROOT}/bin/youyeetoo_app"
fi
if [[ "${sdk_existed}" == "true" ]]; then
  install -m 0644 "${BACKUP}/libCameraSDK.so" "${APP_ROOT}/lib/libCameraSDK.so"
else
  rm -f "${APP_ROOT}/lib/libCameraSDK.so"
fi

"${SYSTEMCTL}" start "${SERVICE_NAME}"
"${SYSTEMCTL}" is-active --quiet "${SERVICE_NAME}"
echo "UART_DEV_ROLLBACK_OK backup=${BACKUP}"
