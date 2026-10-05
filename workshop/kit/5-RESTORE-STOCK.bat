@echo off
REM ============================================================================
REM  StackChan recovery kit - STEP 5: RESTORE OFFICIAL M5STACK FIRMWARE
REM
REM  This is the guaranteed way back. It writes M5Stack's own factory image
REM  (StackChan-UserDemo, downloaded from M5Stack's servers), exactly what
REM  M5Burner would install. Nothing from this project is involved.
REM
REM  Put the image at stock\StackChan-UserDemo.bin first. See README.md in
REM  workshop/kit for where to download it.
REM
REM  Use this if this firmware misbehaves, or any time you want stock back.
REM ============================================================================
setlocal
cd /d "%~dp0"

set "ESPTOOL=%~dp0tools\esptool.exe"
set "STOCK=%~dp0stock\StackChan-UserDemo.bin"

echo.
echo  ============================================================
echo   Restore official M5Stack firmware  (StackChan-UserDemo)
echo  ============================================================
echo.

if not exist "%ESPTOOL%" ( echo ERROR: tools\esptool.exe missing. & pause & exit /b 1 )
if not exist "%STOCK%"   ( echo ERROR: stock\StackChan-UserDemo.bin missing. & pause & exit /b 1 )

echo  This restores the device to how it shipped. Your WiFi settings and
echo  servo calibration are cleared - that is what a factory restore does,
echo  and it is exactly what M5Burner does too.
echo.
echo  Put the device in download mode:
echo    hold RESET ~2 seconds until the GREEN LED lights, then release.
echo.
pause

echo.
echo  --- Checking the device responds before writing anything ---
"%ESPTOOL%" --chip esp32s3 chip_id > "%~dp0precheck.txt" 2>&1
findstr /C:"Chip is ESP32-S3" "%~dp0precheck.txt" >nul
if errorlevel 1 (
  echo.
  echo  Could not talk to the chip. NOTHING was written.
  type "%~dp0precheck.txt"
  echo.
  echo    - GREEN LED not lit  -^> hold RESET ~2s
  echo    - charge-only cable  -^> use a DATA USB-C cable
  echo    - port in use        -^> close M5Burner / Arduino / serial monitors
  echo.
  pause
  exit /b 1
)
echo  OK - chip responded.

echo.
echo  --- Writing factory image (about 3 minutes). Do not unplug. ---
echo.
"%ESPTOOL%" --chip esp32s3 -b 460800 --before default_reset --after hard_reset ^
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m ^
  0x0 "%STOCK%"

if errorlevel 1 (
  echo.
  echo  RESTORE FAILED - the device may be half-written.
  echo  Run 2-ERASE.bat, then run this again.
  echo.
  pause
  exit /b 1
)

echo.
echo  ============================================================
echo   Factory firmware restored. The device should boot to the
echo   normal StackChan face and setup flow.
echo  ============================================================
echo.
pause
endlocal
