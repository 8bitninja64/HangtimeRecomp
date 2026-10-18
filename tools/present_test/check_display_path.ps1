# One-command check of this PC's display path for NBA Hangtime (PERFORMANCE.md "20 Hz present").
# Builds nothing; run after `cmake --build tools/present_test/build --config Release`.
#   powershell -ExecutionPolicy Bypass -File tools\present_test\check_display_path.ps1
# Prints: which GPU drives a display, GPU scheduling mode, refresh rate, and for every GPU the
# D3D12 present rate and the on-screen update rate of a 60 Hz test window. Healthy: the GPU that
# renders the game (most VRAM) shows ~60 presents/s and ~60 window updates/s.
$ErrorActionPreference = 'SilentlyContinue'
$b = Join-Path $PSScriptRoot 'build\Release'
if (-not (Test-Path "$b\present_test12.exe")) { "build first: cmake -S tools\present_test -B tools\present_test\build; cmake --build tools\present_test\build --config Release"; exit 1 }
"== system"
"date: $(Get-Date -Format s)"
$hags = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers' -Name HwSchMode).HwSchMode
"hardware-accelerated GPU scheduling: $(if ($hags -eq 2) { 'on' } elseif ($hags -eq 1) { 'off' } else { 'default' })"
Get-CimInstance Win32_VideoController | ForEach-Object { "gpu: $($_.Name) driver $($_.DriverVersion) $(if ($_.CurrentRefreshRate) { "drives the desktop at $($_.CurrentRefreshRate) Hz" })" }
$smi = (Get-Command nvidia-smi).Source
if ($smi) { "nvidia display attached: " + ((& $smi --query-gpu=display_attached,display_active --format=csv,noheader) -join ' ') }
"== D3D12 present rate and on-screen updates per GPU (60 Hz test window, 5 s each)"
$i = 0
while ($true) {
    $out = & "$b\present_test12.exe" $i 0 1 2>&1 | Select-String '^adapter'
    if (-not $out -or "$out" -match 'Basic Render') { break }
    $p = Start-Process "$b\present_test12.exe" -ArgumentList "$i 0 8 rate=60" -PassThru -RedirectStandardOutput "$env:TEMP\pt12_check.txt"
    Start-Sleep 2
    $s = & "$b\screen_rate.exe" "present_test12" 5 | Select-String 'window_updates'
    $p.WaitForExit()
    $r = Get-Content "$env:TEMP\pt12_check.txt" | Select-String '^d3d12'
    $name = ("$out" -replace '^adapter \d+: ', '')
    $pps = if ("$r" -match 'presents/s=([\d.]+)') { $Matches[1] } else { '?' }
    $ups = if ("$s" -match 'window_updates/s=([\d.]+)') { $Matches[1] } else { '?' }
    "adapter ${i}: $name  presents/s=$pps  on-screen updates/s=$ups"
    $i++
}
"Report these lines. The RTX line should read ~60 / ~60; 20 / 20 is the slow cross-adapter path."
