#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE="${1:-}"
APP_ROOT="${APP_ROOT:-/opt/youyeetoo_app}"
BACKUP_ROOT="${BACKUP_ROOT:-${APP_ROOT}/dev/backups}"
SERVICE_NAME="${SERVICE_NAME:-youyeetoo-app.service}"
SYSTEMCTL="${SYSTEMCTL:-systemctl}"
REQUIRE_CAMERA="${REQUIRE_CAMERA:-0}"

[[ -n "${BUNDLE}" ]] || { echo "Usage: $0 BUNDLE.tar.gz" >&2; exit 2; }
[[ -f "${BUNDLE}" ]] || { echo "Bundle not found: ${BUNDLE}" >&2; exit 1; }
if [[ "${EUID}" -ne 0 && "${ALLOW_NON_ROOT:-0}" != "1" ]]; then
  echo "Run installation as root." >&2
  exit 1
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT
if tar -tzf "${BUNDLE}" | grep -Eq '(^/|(^|/)\.\.(/|$))'; then
  echo "Unsafe path in bundle" >&2
  exit 1
fi
tar -xzf "${BUNDLE}" -C "${STAGE}"
[[ -x "${STAGE}/bin/youyeetoo_app" ]] || { echo "Bundle app missing" >&2; exit 1; }
(cd "${STAGE}" && sha256sum -c manifest.sha256)

STAMP="$(date -u +%Y%m%dT%H%M%SZ)-$$"
BACKUP="${BACKUP_ROOT}/${STAMP}"
install -d "${BACKUP}" "${APP_ROOT}/bin" "${APP_ROOT}/lib" "${APP_ROOT}/dev/tools"
app_existed=false
sdk_existed=false
if [[ -f "${APP_ROOT}/bin/youyeetoo_app" ]]; then
  cp -a "${APP_ROOT}/bin/youyeetoo_app" "${BACKUP}/"
  app_existed=true
fi
if [[ -f "${APP_ROOT}/lib/libCameraSDK.so" ]]; then
  cp -a "${APP_ROOT}/lib/libCameraSDK.so" "${BACKUP}/"
  sdk_existed=true
fi
printf 'app_existed=%s\nsdk_existed=%s\n' "${app_existed}" "${sdk_existed}" > "${BACKUP}/backup.info"
sha256sum "${BUNDLE}" > "${BACKUP}/installed-bundle.sha256"

rollback() {
  echo "Installation health check failed; restoring ${BACKUP}" >&2
  APP_ROOT="${APP_ROOT}" BACKUP_ROOT="${BACKUP_ROOT}" SERVICE_NAME="${SERVICE_NAME}" SYSTEMCTL="${SYSTEMCTL}" ALLOW_NON_ROOT="${ALLOW_NON_ROOT:-0}" \
    "${STAGE}/tools/rollback_uart_dev.sh" "${BACKUP}"
  echo "UART_DEV_INSTALL_ROLLBACK_OK backup=${BACKUP}" >&2
}
trap 'status=$?; if [[ $status -ne 0 ]]; then rollback || true; fi; rm -rf "${STAGE}"; exit $status' EXIT

"${SYSTEMCTL}" stop "${SERVICE_NAME}" || true
install -m 0755 "${STAGE}/bin/youyeetoo_app" "${APP_ROOT}/bin/youyeetoo_app"
if [[ -f "${STAGE}/lib/libCameraSDK.so" ]]; then
  install -m 0644 "${STAGE}/lib/libCameraSDK.so" "${APP_ROOT}/lib/libCameraSDK.so"
fi
install -m 0755 "${STAGE}/tools/"*.sh "${APP_ROOT}/dev/tools/"
"${SYSTEMCTL}" start "${SERVICE_NAME}"

health_args=()
[[ "${REQUIRE_CAMERA}" == "1" ]] && health_args+=(--require-camera)
APP_ROOT="${APP_ROOT}" SERVICE_NAME="${SERVICE_NAME}" SYSTEMCTL="${SYSTEMCTL}" PROCESS_CHECK="${PROCESS_CHECK:-1}" \
  "${STAGE}/tools/camera_dev_healthcheck.sh" "${health_args[@]}"

trap 'rm -rf "${STAGE}"' EXIT
echo "UART_DEV_INSTALL_OK backup=${BACKUP} app_sha256=$(sha256sum "${APP_ROOT}/bin/youyeetoo_app" | awk '{print $1}')"
