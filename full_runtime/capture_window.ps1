# Run hangtime.exe, screenshot its window (genuine on-screen output) at given times, then stop it.
# Usage: capture_window.ps1 -Shots 6,10 -OutPrefix shot [-GameArgs '--press 4:START'] [-Log prefix] [-Topmost] [-Foreground]
# -Topmost keeps the (unfocused) test window above other windows without activating it: Windows
# throttles presentation of an occluded window, which performance runs must not measure.
# -Foreground makes the game the foreground window (as when a player clicks it; GPU drivers may cap
# background applications' frame rate) and reports at each shot whether it still is.
param([string]$Shots = '10', [string]$OutPrefix = 'hangtime_window', [string]$GameArgs = '', [string]$Log = 'hangtime_run',
      [int]$Seconds = 0, [string]$Out = '', [switch]$Topmost, [switch]$Foreground)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W { [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra); }
"@
[void][W]::SetProcessDPIAware()   # real pixels: the game window is per-monitor DPI aware
if ($Seconds -gt 0) { $Shots = "$Seconds" }                     # backward-compatible single shot
$ShotTimes = @($Shots -split '[,; ]+' | Where-Object { $_ } | ForEach-Object { [double]$_ })
$exe = Join-Path $PSScriptRoot 'build\Release\hangtime.exe'
if (-not (Test-Path $exe)) { "hangtime.exe not built"; exit 1 }
# The ROM as hangtime_paths.py finds it: $HANGTIME_ROM, else the repository's rom/ folder, else ..\rom.
$rom = $env:HANGTIME_ROM
if (-not $rom) {
    $local = Get-ChildItem (Join-Path (Split-Path $PSScriptRoot) 'rom') -File -ErrorAction SilentlyContinue |
             Where-Object { $_.Extension -in '.z64', '.n64', '.v64' } | Sort-Object Name | Select-Object -First 1
    $rom = if ($local) { $local.FullName } else { '..\rom\NBA Hangtime (USA).z64' }
}
$argList = @("`"$rom`""); if ($GameArgs) { $argList += $GameArgs }
$p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory (Split-Path $PSScriptRoot) -PassThru -RedirectStandardError "$Log.err.txt" -RedirectStandardOutput "$Log.out.txt"
$start = Get-Date
if ($Topmost -or $Foreground) {
    for ($i = 0; $i -lt 100 -and $p.MainWindowHandle -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100; $p.Refresh() }
}
if ($Topmost) { [void][W]::SetWindowPos($p.MainWindowHandle, [IntPtr](-1), 0, 0, 0, 0, 0x53) }   # HWND_TOPMOST, SWP_NOMOVE|NOSIZE|NOACTIVATE|SHOWWINDOW
if ($Foreground) {   # an Alt tap lifts the foreground lock for this process; the game ignores the keyboard in tests (--no-keyboard)
    [W]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [W]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [void][W]::SetForegroundWindow($p.MainWindowHandle)
    Start-Sleep -Milliseconds 300
    "foreground at start: $([W]::GetForegroundWindow() -eq $p.MainWindowHandle)"
}
foreach ($t in ($ShotTimes | Sort-Object)) {
    $wait = $t - ((Get-Date) - $start).TotalSeconds; if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
    $p.Refresh(); if ($p.HasExited) { "exited early at ${t}s"; exit 1 }
    $h = $p.MainWindowHandle; $r = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$r)
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap $w, $hh; $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc(); [void][W]::PrintWindow($h, $hdc, 2); $g.ReleaseHdc($hdc)
    $file = if ($Out -and $ShotTimes.Count -eq 1) { $Out } else { "${OutPrefix}_$([int]$t)s.png" }
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
    "shot ${t}s -> $file" + $(if ($Foreground) { " foreground=$([W]::GetForegroundWindow() -eq $h)" } else { '' })
}
$p.Kill()
