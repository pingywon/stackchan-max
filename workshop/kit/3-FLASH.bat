@echo off
REM ============================================================================
REM  StackyChan - STEP 3: FLASH
REM
REM  Writes five partitions at fixed offsets. You never type an address.
REM
REM  READ THIS ONE THING: this firmware uses a DIFFERENT PARTITION TABLE from
REM  stock. The assets partition is 5.69 MB instead of 4 MB, to fit eleven
REM  wake-word models. That means the first install CANNOT be done over the
REM  air from a web page - an OTA update rewrites an app slot and cannot move a
REM  partition boundary. It has to go on over USB, which is what this script
REM  does.
REM
REM  Your WiFi credentials and servo calibration live in NVS at 0x9000, which is
REM  NOT written here. They survive.
REM ============================================================================
setlocal
cd /d "%~dp0"

set "ESPTOOL=%~dp0tools\esptool.exe"
set "BIN=%~dp0bin"

echo.
echo  ============================================================
echo   Flash StackyChan
echo  ============================================================
echo.

if not exist "%ESPTOOL%"  ( echo ERROR: tools\esptool.exe missing.  & pause & exit /b 1 )
if not exist "%BIN%\bootloader.bin"       ( echo ERROR: bin\bootloader.bin missing.       & pause & exit /b 1 )
if not exist "%BIN%\partition-table.bin"  ( echo ERROR: bin\partition-table.bin missing.  & pause & exit /b 1 )
if not exist "%BIN%\ota_data_initial.bin" ( echo ERROR: bin\ota_data_initial.bin missing. & pause & exit /b 1 )
if not exist "%BIN%\stack-chan.bin"       ( echo ERROR: bin\stack-chan.bin missing.       & pause & exit /b 1 )
if not exist "%BIN%\generated_assets.bin" ( echo ERROR: bin\generated_assets.bin missing. & pause & exit /b 1 )

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
  echo.
  type "%~dp0precheck.txt"
  echo.
  echo  Fix one of these and try again:
  echo    - GREEN LED not lit    -^> hold RESET ~2s
  echo    - charge-only cable    -^> use a DATA USB-C cable
  echo    - COM port in use      -^> close M5Burner / Arduino / serial monitors
  echo.
  pause
  exit /b 1
)
echo  OK - chip responded.
echo.

echo  --- Writing 5 partitions (about 2 minutes - the assets grew). Do not unplug. ---
echo.
"%ESPTOOL%" --chip esp32s3 -b 460800 --before default_reset --after hard_reset ^
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m ^
  0x0      "%BIN%\bootloader.bin" ^
  0x8000   "%BIN%\partition-table.bin" ^
  0xd000   "%BIN%\ota_data_initial.bin" ^
  0x20000  "%BIN%\stack-chan.bin" ^
  0xa00000 "%BIN%\generated_assets.bin"

if errorlevel 1 (
  echo.
  echo  ============================================================
  echo   FLASH FAILED - the device may be half-written.
  echo   Run 2-ERASE.bat, then run this again.
  echo  ============================================================
  pause
  exit /b 1
)

echo.
echo  ============================================================
echo   Flash complete. The device reboots into StackyChan.
echo.
echo   Next: run 6-FIND-DEVICE.bat to get its IP address, then open
echo   that address in a browser - or try http://stackchan.local/
echo.
echo   Check it is really ours: the boot log should say
echo     App version: 9.9.3-stackychan
echo   A plain version such as 1.4.4 or 1.5.1 is M5Stack's stock image.
echo.
echo   If the screen stays black, run 4-BOOTLOG.bat and read the
echo   bootlog.txt it produces.
echo  ============================================================
echo.
pause
endlocal
