# capture.ps1 — take a screenshot of the dev shell while it holds its windows
# open, so the overlay and the data window can be inspected (and diffed against
# a run with them off).
#
#   .\tools\devshell\capture.ps1 -Scenario tests\scenarios\visual-demo.txt -Out shot.png
#
# Why a helper: cw_devshell `gui --script` keeps its windows up and waits for a
# `<script>.done` marker, so the caller controls exactly when the picture is
# taken. That removes the race between "the windows are up" and "the capture
# happens".
param(
    [Parameter(Mandatory = $true)][string]$Scenario,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$SettleMs = 3000,
    [string]$Exe = "build\cw_devshell.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exePath = if ([System.IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $root $Exe }
if (-not (Test-Path $exePath)) { throw "not built: $exePath" }

$scenarioPath = if ([System.IO.Path]::IsPathRooted($Scenario)) { $Scenario } else { Join-Path $root $Scenario }
$donePath = "$scenarioPath.done"
Remove-Item $donePath -Force -ErrorAction SilentlyContinue

$log = [System.IO.Path]::GetTempFileName()
$proc = Start-Process -FilePath $exePath `
    -ArgumentList @("gui", "--script", $scenarioPath) `
    -PassThru -RedirectStandardOutput $log
try {
    Start-Sleep -Milliseconds $SettleMs

    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $screen = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bmp = New-Object System.Drawing.Bitmap($screen.Width, $screen.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.CopyFromScreen($screen.Left, $screen.Top, 0, 0, $bmp.Size)
    $outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bmp.Dispose()
    Write-Host "captured $outPath"
} finally {
    # Tell the shell it may exit, then make sure it did.
    New-Item -ItemType File -Path $donePath -Force | Out-Null
    if (-not $proc.WaitForExit(5000)) {
        $proc | ForEach-Object { Stop-Process -Id $_.Id -Force }
    }
    Remove-Item $donePath -Force -ErrorAction SilentlyContinue
    Write-Host "--- devshell output:"
    Get-Content $log | Select-Object -Last 20
    Remove-Item $log -Force -ErrorAction SilentlyContinue
}
