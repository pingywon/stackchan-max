@echo off
REM ============================================================================
REM  StackChan recovery kit - STEP 1: DIAGNOSE
REM
REM  Reads the chip without changing anything. Safe to run any time.
REM  Writes diagnostic-log.txt - keep it; it is the first thing to look at.
REM ============================================================================
setlocal
cd /d "%~dp0"

set "ESPTOOL=%~dp0tools\esptool.exe"
set "LOG=%~dp0diagnostic-log.txt"

echo.
echo  ============================================================
echo   StackChan diagnostic
echo  ============================================================
echo.
echo  Before continuing:
echo    1. Use a DATA USB-C cable (a charge-only cable looks identical
echo       and will silently do nothing).
echo    2. Hold RESET about 2 seconds until the internal GREEN LED
echo       lights, then release. A black screen is NORMAL and expected.
echo.
pause

if not exist "%ESPTOOL%" (
  echo ERROR: tools\esptool.exe is missing. Re-extract the whole kit folder.
  pause
  exit /b 1
)

echo StackChan diagnostic > "%LOG%"
echo run at %DATE% %TIME% >> "%LOG%"
echo. >> "%LOG%"

echo.
echo  --- Serial ports Windows can see -------------------------------
echo [serial ports] >> "%LOG%"
powershell -NoProfile -Command "Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } | Select-Object -ExpandProperty Name" >> "%LOG%" 2>&1
powershell -NoProfile -Command "Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } | Select-Object -ExpandProperty Name"

echo. >> "%LOG%"
echo  --- Chip identification ----------------------------------------
echo [chip_id] >> "%LOG%"
"%ESPTOOL%" --chip esp32s3 chip_id >> "%LOG%" 2>&1
type "%LOG%" | findstr /C:"Chip is" /C:"MAC:" /C:"Detecting" /C:"error" /C:"Error" /C:"failed"

echo. >> "%LOG%"
echo  --- Flash chip -------------------------------------------------
echo [flash_id] >> "%LOG%"
"%ESPTOOL%" --chip esp32s3 flash_id >> "%LOG%" 2>&1

echo. >> "%LOG%"
echo  --- First 64 bytes of flash (is a bootloader present at 0x0?) ---
echo [read_flash 0x0 64] >> "%LOG%"
"%ESPTOOL%" --chip esp32s3 read_flash 0x0 64 "%~dp0flash-head.bin" >> "%LOG%" 2>&1
if exist "%~dp0flash-head.bin" (
  echo [first bytes] >> "%LOG%"
  powershell -NoProfile -Command "$b=[System.IO.File]::ReadAllBytes('%~dp0flash-head.bin'); ($b[0..15] | ForEach-Object { $_.ToString('x2') }) -join ' '" >> "%LOG%" 2>&1
  powershell -NoProfile -Command "$b=[System.IO.File]::ReadAllBytes('%~dp0flash-head.bin'); if($b[0] -eq 0xE9){Write-Host '  Bootloader present at 0x0 (magic E9) - GOOD' -ForegroundColor Green}else{Write-Host ('  NO bootloader at 0x0 (first byte is 0x{0:x2}) - THIS IS THE PROBLEM' -f $b[0]) -ForegroundColor Red}"
  powershell -NoProfile -Command "$b=[System.IO.File]::ReadAllBytes('%~dp0flash-head.bin'); if($b[0] -eq 0xE9){'RESULT: bootloader present at 0x0'}else{'RESULT: NO bootloader at 0x0 - first byte 0x{0:x2}' -f $b[0]}" >> "%LOG%" 2>&1
)

echo.
echo  ============================================================
echo   Done. Log written to:
echo     %LOG%
echo   Keep that file. It is the first thing to look at, or to attach
echo   to a bug report.
echo  ============================================================
echo.
echo  If everything above said "Chip is ESP32-S3", the device is FINE
echo  and just needs reflashing - run 2-ERASE.bat then 3-FLASH.bat.
echo.
pause
endlocal
