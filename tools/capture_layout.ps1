#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Captures the current monitor layout as an anonymized JSON fixture for tests.

.DESCRIPTION
    Enumerates all connected monitors (position, size, DPI, primary flag) and
    writes tests/layouts/<name>.json. By default, potentially identifying
    information (device names) is anonymized to DISPLAY1, DISPLAY2, ...

.PARAMETER Name
    Fixture name (without extension). Default: captured-layout.

.PARAMETER OutDir
    Output directory. Default: tests/layouts.

.EXAMPLE
    .\tools\capture_layout.ps1 -Name dual-horizontal
#>
param(
    [string]$Name = 'captured-layout',
    [string]$OutDir = (Join-Path $PSScriptRoot '..\tests\layouts')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

$monitors = @()
$index = 0
foreach ($screen in [System.Windows.Forms.Screen]::AllScreens) {
    $index++
    $bounds = $screen.Bounds
    $dpi = 96
    try {
        $source = Add-Type -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("shcore.dll")]
public static extern int GetDpiForMonitor(IntPtr hMonitor, int type, out uint dpiX, out uint dpiY);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern IntPtr MonitorFromPoint(System.Drawing.Point pt, uint flags);
'@ -Name DpiProbe$index -PassThru -ErrorAction SilentlyContinue
        if ($source) {
            $hMon = $source::MonitorFromPoint($bounds.Location, 1) # MONITOR_DEFAULTTOPRIMARY
            $dx = [uint32]0; $dy = [uint32]0
            if ($source::GetDpiForMonitor($hMon, 0, [ref]$dx, [ref]$dy) -eq 0) { $dpi = [int]$dx }
        }
    } catch { }
    $monitors += [ordered]@{
        device = "DISPLAY$index"
        primary = $screen.Primary
        left = $bounds.X
        top = $bounds.Y
        right = $bounds.X + $bounds.Width
        bottom = $bounds.Y + $bounds.Height
        dpi = $dpi
    }
}

$fixture = [ordered]@{
    '$comment' = 'Captured monitor layout, device names anonymized. Safe to commit.'
    name = $Name
    capturedAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    monitors = $monitors
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$out = Join-Path $OutDir "$Name.json"
$fixture | ConvertTo-Json -Depth 4 | Set-Content -Path $out -Encoding UTF8
Write-Host "Wrote $out ($($monitors.Count) monitor(s))"
