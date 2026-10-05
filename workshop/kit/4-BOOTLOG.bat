@echo off
REM ============================================================================
REM  StackChan recovery kit - STEP 4: CAPTURE BOOT LOG
REM
REM  Records what the device prints as it starts up. This is the single most
REM  useful thing to have if it still will not boot - it names the actual
REM  fault instead of leaving you to guess.
REM
REM  Normal boot, NOT download mode. Do not hold RESET beforehand.
REM ============================================================================
setlocal
cd /d "%~dp0"

echo.
echo  ============================================================
echo   Capture boot log
echo  ============================================================
echo.
echo  Leave the device in NORMAL mode (do NOT hold reset for the
echo  green LED this time). Just have it plugged in.
echo.
echo  When the capture starts, press and release RESET once so the
echo  startup messages are recorded.
echo.
pause

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\serialmon.ps1" -Seconds 30 -Out "%~dp0bootlog.txt"

echo.
echo  ============================================================
echo   Saved to: %~dp0bootlog.txt
echo   Read that file first, or attach it to a bug report.
echo  ============================================================
echo.
pause
endlocal
