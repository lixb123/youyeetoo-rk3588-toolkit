@echo off
cd /d "%~dp0"
where py >nul 2>nul && goto use_py
python --version >nul 2>nul && goto use_python
if exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" goto use_codex_python
echo 未找到 Python 3，请先安装 Python 3 并勾选 Add Python to PATH。
pause
exit /b 1

:use_py
py -3 payload_sim.py
exit /b %errorlevel%

:use_python
python payload_sim.py
exit /b %errorlevel%

:use_codex_python
"%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" payload_sim.py
exit /b %errorlevel%
