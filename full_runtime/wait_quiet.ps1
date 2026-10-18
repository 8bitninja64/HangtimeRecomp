# Wait until the machine is quiet enough for a performance run: no WSL VM work (vmmemWSL /
# wsl.exe running a test), no other hangtime.exe, and total CPU below -MaxCpu percent over a
# 3-second sample. Prints the measured load; exit 1 after -TimeoutMin minutes.
param([double]$MaxCpu = 15, [int]$TimeoutMin = 60)
$deadline = (Get-Date).AddMinutes($TimeoutMin)
while ((Get-Date) -lt $deadline) {
    $busy = @(Get-Process hangtime, wsl -ErrorAction SilentlyContinue)
    $cpu = (Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 3 -MaxSamples 1).CounterSamples[0].CookedValue
    if ($busy.Count -eq 0 -and $cpu -lt $MaxCpu) { "quiet: cpu={0:N1}%" -f $cpu; exit 0 }
    Start-Sleep -Seconds 10
}
"not quiet after $TimeoutMin min"; exit 1
