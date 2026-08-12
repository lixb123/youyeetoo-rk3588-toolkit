@echo off
setlocal
cd /d "%~dp0"
set "CODEX_PY=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%CODEX_PY%" (
  "%CODEX_PY%" rk_eth_uart.py
  exit /b %errorlevel%
)
where py >nul 2>nul && (
  py -3 rk_eth_uart.py
  exit /b %errorlevel%
)
where python >nul 2>nul && (
  python rk_eth_uart.py
  exit /b %errorlevel%
)
echo Python 3 not found.
pause
exit /b 1
