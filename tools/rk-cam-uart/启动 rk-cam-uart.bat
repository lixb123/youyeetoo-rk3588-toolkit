@echo off
chcp 65001 >nul
where pwsh.exe >nul 2>&1
if %errorlevel% equ 0 (
  pwsh.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0rk-cam-uart.ps1"
) else (
  powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -Command "& ([scriptblock]::Create((Get-Content -Raw -Encoding UTF8 -LiteralPath '%~dp0rk-cam-uart.ps1')))"
)
