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
# Start the shell through .NET rather than Start-Process: Start-Process with
# -RedirectStandardOutput hands the child a pipe whose contents PowerShell only
# flushes on exit, which made the captured output unreadable when the point of
# the run is to READ the scenario report.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exePath
$psi.Arguments = @("gui", "--script", "`"$scenarioPath`"") -join " "
$psi.WorkingDirectory = $root
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $false
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
$stdoutTask = $proc.StandardOutput.ReadToEndAsync()
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
    $captured = ""
    if ($stdoutTask.Wait(2000)) {
        $captured = $stdoutTask.Result
    }
    if ($captured) {
        $captured.TrimEnd().Split("`n") | Select-Object -Last 30 |
            ForEach-Object { Write-Host "    $($_.TrimEnd())" }
    } else {
        Write-Host "    (no output)"
    }
    Remove-Item $log -Force -ErrorAction SilentlyContinue
}
