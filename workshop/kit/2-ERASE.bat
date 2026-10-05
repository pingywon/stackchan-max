@echo off
REM ============================================================================
REM  StackChan recovery kit - STEP 2: FULL ERASE
REM
REM  Wipes the entire 16MB flash to a known-blank state. This clears any
REM  half-written image from a failed flash, which is the most likely cause
REM  of a device that will not boot.
REM
REM  This CANNOT brick the device: ESP32-S3 download mode lives in mask ROM
REM  and is not stored in flash, so it survives any erase.
REM ============================================================================
setlocal
cd /d "%~dp0"

set "ESPTOOL=%~dp0tools\esptool.exe"

echo.
echo  ============================================================
echo   FULL FLASH ERASE
echo  ============================================================
echo.
echo  This erases everything on the device, including WiFi settings
echo  and servo calibration. After this you MUST run 3-FLASH.bat
echo  (or restore stock firmware with M5Burner) or the device will
echo  have nothing to boot.
echo.
echo  Put the device in download mode first:
echo    hold RESET ~2 seconds until the GREEN LED lights, then release.
echo.
set /p CONFIRM="Type ERASE and press Enter to continue: "
if /I not "%CONFIRM%"=="ERASE" (
  echo Cancelled. Nothing was changed.
  pause
  exit /b 0
)

echo.
echo  Erasing... this takes 30-60 seconds. Do not unplug.
echo.
"%ESPTOOL%" --chip esp32s3 -b 460800 --before default_reset --after no_reset erase_flash

if errorlevel 1 (
  echo.
  echo  ERASE FAILED.
  echo    - Is the GREEN LED lit? Hold RESET ~2s and try again.
  echo    - Is another program holding the COM port? Close M5Burner,
  echo      Arduino IDE, and any serial monitor.
  echo    - Is it a DATA cable, not charge-only?
  echo.
  pause
  exit /b 1
)

echo.
echo  ============================================================
echo   Erase complete. The device is now blank.
echo   Next: run 3-FLASH.bat
echo  ============================================================
echo.
pause
endlocal
