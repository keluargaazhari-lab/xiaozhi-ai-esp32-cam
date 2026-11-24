@echo off
call C:\Espressif\v5.5.1\esp-idf\export.bat
set PYTHONIOENCODING=utf-8
set "PATH=C:\Users\frank\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%PATH%"
python scripts\build.py esp32s3-cam-st7789 --name esp32s3-cam-st7789-240x280 --wake-word wn9_hijason_tts2 %*
