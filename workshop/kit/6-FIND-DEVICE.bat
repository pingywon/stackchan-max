@echo off
REM ============================================================================
REM  StackChan recovery kit - FIND THE DEVICE ON YOUR NETWORK
REM
REM  Finds the StackChan's current IP. It looks for Espressif network chips
REM  and asks each one whether it is the StackChan portal.
REM
REM  Optional: give the robot's WiFi MAC (it is in the boot log) to pick out one
REM  robot for certain:   6-FIND-DEVICE.bat aa-bb-cc-dd-ee-ff
REM
REM  No USB cable needed. The device just has to be powered on and on the same
REM  network as this PC.
REM ============================================================================
setlocal
cd /d "%~dp0"

echo.
echo  ============================================================
echo   Looking for StackChan on your network
echo  ============================================================
echo.
echo  Looking for Espressif devices and asking each one whether it
echo  is the StackChan portal.
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\finddevice.ps1" %*

echo.
pause
endlocal
