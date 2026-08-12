@echo off
cd /d "%~dp0"
python auto_runner.py --mode live --com COM3 --baud 1500000 --seed 20260812 --count 14 --negative-count 3
pause
