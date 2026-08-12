@echo off
cd /d "%~dp0"
python auto_runner.py --mode offline --seed 20260812 --count 14 --negative-count 3
pause
