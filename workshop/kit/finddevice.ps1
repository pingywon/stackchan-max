# finddevice.ps1 — locate the StackChan on the local network by MAC.
#
# DHCP will hand the device a different IP whenever the lease changes or it moves
# networks. Its WiFi MAC does not change, so that is what we search on.
#
# Strategy: sweep the local /24 with fast pings to populate the ARP cache, then read
# the cache back and match on MAC. Nothing is installed and nothing is modified.
#
# Usage:
#   finddevice.ps1                      every Espressif device found; the one that
#                                       answers like the StackChan portal is picked
#   finddevice.ps1 aa-bb-cc-dd-ee-ff    also single out one robot by its WiFi MAC
#
# The MAC is printed in the robot's boot log (4-BOOTLOG.bat). Dashes, colons or no
# separators are all accepted.

param(
    [string]$Mac = ""
)

$ErrorActionPreference = "SilentlyContinue"

# Optional MAC of one specific robot, normalised to the aa-bb-cc-dd-ee-ff form that
# `arp -a` prints. Empty means: recognise the robot by vendor prefix and by its portal.
$KnownMac = ""
$MacHex = ($Mac -replace '[^0-9a-fA-F]', '').ToLower()
if ($MacHex.Length -eq 12) {
    $KnownMac = ($MacHex -replace '(..)(?!$)', '$1-')
} elseif ($Mac -ne "") {
    Write-Host ("Ignoring '{0}': a MAC address has 12 hex digits." -f $Mac) -ForegroundColor Yellow
}

# Espressif OUI prefixes: how the robot is recognised when no MAC is given.
$EspPrefixes = @("7c-4f-ad", "24-6f-28", "30-ae-a4", "3c-71-bf", "48-3f-da",
                 "84-cc-a8", "8c-aa-b5", "a4-cf-12", "b4-e6-2d", "bc-dd-c2",
                 "c4-4f-33", "cc-50-e3", "d8-a0-1d", "dc-4f-22", "e8-db-84",
                 "f0-08-d1", "fc-f5-c4", "34-85-18", "40-91-51", "54-43-b2",
                 "68-b6-b3", "8c-4b-14", "e4-65-b8", "f4-12-fa")

function Get-LocalV4Subnets {
    Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
        ForEach-Object {
            [PSCustomObject]@{
                IP     = $_.IPAddress
                Prefix = ($_.IPAddress -replace '\.\d+$', '')
            }
        } | Sort-Object Prefix -Unique
}

$subnets = Get-LocalV4Subnets
if (-not $subnets) {
    Write-Host "No usable network interface found." -ForegroundColor Red
    exit 1
}

Write-Host "This PC is on:" -ForegroundColor Cyan
$subnets | ForEach-Object { Write-Host ("  {0}  (scanning {1}.1-254)" -f $_.IP, $_.Prefix) }
Write-Host ""

foreach ($net in $subnets) {
    Write-Host ("Sweeping {0}.0/24 ..." -f $net.Prefix) -NoNewline

    # Fire off short-timeout pings in parallel to populate ARP. We do not care about
    # replies; many devices ignore ICMP but still answer ARP.
    $jobs = 1..254 | ForEach-Object {
        $addr = "$($net.Prefix).$_"
        Start-Job -ScriptBlock {
            param($a)
            Test-Connection -ComputerName $a -Count 1 -TimeoutSeconds 1 -Quiet
        } -ArgumentList $addr
    }
    $null = $jobs | Wait-Job -Timeout 25
    $jobs | Remove-Job -Force
    Write-Host " done"
}

Write-Host ""
Write-Host "ARP table matches:" -ForegroundColor Cyan
Write-Host ""

$found = @()
$arp = arp -a

foreach ($line in $arp) {
    if ($line -match '\s*(\d+\.\d+\.\d+\.\d+)\s+([0-9a-fA-F-]{17})\s') {
        $ip  = $matches[1]
        $mac = $matches[2].ToLower()

        $isKnown = ($KnownMac -ne "" -and $mac -eq $KnownMac)
        $isEsp   = $EspPrefixes | Where-Object { $mac.StartsWith($_) }

        if ($isKnown -or $isEsp) {
            $label = if ($isKnown) { "  <-- THIS IS YOUR STACKCHAN" } else { "  (Espressif device)" }
            $colour = if ($isKnown) { "Green" } else { "Yellow" }
            Write-Host ("  {0,-16} {1}{2}" -f $ip, $mac, $label) -ForegroundColor $colour
            $found += [PSCustomObject]@{ IP = $ip; MAC = $mac; Known = $isKnown }
        }
    }
}

if ($found.Count -eq 0) {
    Write-Host "  nothing found." -ForegroundColor Red
    Write-Host ""
    Write-Host "Things to check:" -ForegroundColor Yellow
    Write-Host "  * Is the StackChan powered on and past the setup screen?"
    Write-Host "  * Is it on the SAME network as this PC? The boot log names the WiFi"
    Write-Host "    network it joined. If this PC is on a different subnet, the"
    Write-Host "    portal will not be reachable even once you know the address."
    Write-Host "  * Your router's admin page will list it under DHCP clients; look for"
    Write-Host "    the MAC address shown in the boot log."
    Write-Host "  * 4-BOOTLOG.bat prints the IP directly ('sta ip: ...') over USB."
    exit 1
}

Write-Host ""
$best = $found | Where-Object { $_.Known } | Select-Object -First 1
if (-not $best) {
    # No MAC given, or it was not seen: the robot is whichever candidate answers like
    # the StackChan portal.
    foreach ($cand in $found) {
        try {
            $probe = Invoke-WebRequest -Uri "http://$($cand.IP)/api/info" -TimeoutSec 3 -UseBasicParsing
            if (($probe.Content | ConvertFrom-Json).version) { $best = $cand; break }
        } catch { }
    }
}
if (-not $best) { $best = $found[0] }

$url = "http://$($best.IP)/"
Write-Host "Portal should be at: $url" -ForegroundColor Green
Write-Host ""

# Confirm the portal is actually answering before sending the user there.
try {
    $resp = Invoke-WebRequest -Uri "http://$($best.IP)/api/info" -TimeoutSec 4 -UseBasicParsing
    $info = $resp.Content | ConvertFrom-Json
    Write-Host "Portal is UP." -ForegroundColor Green
    Write-Host ("  firmware : {0}" -f $info.version)
    Write-Host ("  slot     : {0}" -f $info.partition)
    Write-Host ("  skin     : {0}" -f $info.skin)
    Write-Host ""
    Write-Host "Opening in your browser..."
    Start-Process $url
} catch {
    Write-Host "Found the device, but the portal did not answer on port 80." -ForegroundColor Yellow
    Write-Host "That usually means it is not the StackChan, or it is running firmware"
    Write-Host "without the portal (stock has none). Flash this firmware with 3-FLASH.bat,"
    Write-Host "or pass the robot's MAC to this script to single it out."
}
