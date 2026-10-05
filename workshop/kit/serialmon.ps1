# serialmon.ps1 — capture the StackChan boot log to a file.
#
# A device stuck in a boot loop re-enumerates its USB serial port on every reset, so the
# COM port keeps vanishing. This reopens the port continuously for the whole capture window
# and stitches together whatever it manages to read, which is exactly what you need to see
# a panic or a bootloader error.

param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$Seconds = 30,
    [string]$Out = "bootlog.txt"
)

function Find-EspPort {
    # Prefer a port whose PnP name looks like an Espressif / USB serial device.
    try {
        $candidates = Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
            Where-Object { $_.Name -match '\(COM(\d+)\)' }
        foreach ($c in $candidates) {
            if ($c.Name -match 'USB|Serial|JTAG|Espressif|CP210|CH34|Silicon') {
                if ($c.Name -match '\((COM\d+)\)') { return $matches[1] }
            }
        }
        foreach ($c in $candidates) {
            if ($c.Name -match '\((COM\d+)\)') { return $matches[1] }
        }
    } catch { }

    $names = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($names.Count -gt 0) { return $names[0] }
    return ""
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    $Port = Find-EspPort
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Host ""
    Write-Host "No COM port found." -ForegroundColor Red
    Write-Host "Plug the StackChan in with a DATA USB-C cable and try again."
    Write-Host "If it still does not appear, the cable is the usual culprit."
    exit 1
}

Write-Host ""
Write-Host "Capturing from $Port at $Baud baud for $Seconds seconds." -ForegroundColor Cyan
Write-Host "Press and release the RESET button now so the boot messages are captured." -ForegroundColor Yellow
Write-Host ""

$deadline = (Get-Date).AddSeconds($Seconds)
$writer = New-Object System.IO.StreamWriter($Out, $false)
$writer.WriteLine("=== StackChan boot log ===")
$writer.WriteLine("port=$Port baud=$Baud captured=$(Get-Date -Format o)")
$writer.WriteLine("")

$captured = 0

while ((Get-Date) -lt $deadline) {
    $sp = $null
    try {
        $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
        $sp.ReadTimeout = 500
        $sp.DtrEnable = $true
        $sp.RtsEnable = $false
        $sp.Open()

        while ((Get-Date) -lt $deadline -and $sp.IsOpen) {
            try {
                $line = $sp.ReadLine()
                if ($line) {
                    $clean = $line -replace "`r", ""
                    Write-Host $clean
                    $writer.WriteLine($clean)
                    $writer.Flush()
                    $captured++
                }
            } catch [TimeoutException] {
                # normal: nothing arrived in this window
            } catch {
                break   # port dropped (device reset) — fall out and reopen
            }
        }
    } catch {
        Start-Sleep -Milliseconds 300   # port not present yet; keep trying
    } finally {
        if ($sp -and $sp.IsOpen) { $sp.Close() }
        if ($sp) { $sp.Dispose() }
    }
}

$writer.WriteLine("")
$writer.WriteLine("=== captured $captured lines ===")
$writer.Close()

Write-Host ""
if ($captured -eq 0) {
    Write-Host "Captured nothing." -ForegroundColor Yellow
    Write-Host "That itself is a clue: it usually means the chip is not running any code at all,"
    Write-Host "or the port is a charge-only cable. Keep the file anyway."
} else {
    Write-Host "Captured $captured lines." -ForegroundColor Green
}
Write-Host "Saved to: $Out" -ForegroundColor Green
