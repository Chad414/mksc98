# Test-harness helper: build (optional), copy mksc98.exe into the guest and
# run it there.
#   deploy.ps1 [-Build] [-Exe mksc98.exe] [-Arguments "--hash --frames 600"] [-NoWait] [-ShotAfter SECONDS] [-Name tag]
# Without -NoWait the guest log is copied back to out\guest-<Name>.log and printed.
# With -ShotAfter a screenshot is saved to out\guest-<Name>.png while it runs.
param([switch]$Build, [string]$Exe = 'mksc98.exe', [string]$Arguments = '', [switch]$NoWait,
      [int]$ShotAfter = 0, [string]$Name = 'run', [int]$Tail = 40)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$vm = Join-Path $here 'vm.ps1'
if ($Build) {
    $wsl = '/mnt/' + $here.Substring(0, 1).ToLower() + ($here.Substring(2) -replace '\\', '/')
    wsl -d Ubuntu -- bash "$wsl/build.sh" $(if ($Exe -match 'profile') { 'profile' } else { 'win98' }) | Select-Object -Last 6
    if ($LASTEXITCODE -ne 0) { Write-Output 'build failed'; exit 1 }
}
& $vm put (Join-Path $here "out\$Exe") "C:\MKSC\$Exe"
$log = Join-Path $here "out\guest-$Name.log"
Remove-Item $log -ErrorAction SilentlyContinue
if ($NoWait -or $ShotAfter -gt 0) {
    if ($Arguments) { & $vm start "C:\MKSC\$Exe" $Arguments } else { & $vm start "C:\MKSC\$Exe" }
    if ($ShotAfter -gt 0) {
        Start-Sleep -Seconds $ShotAfter
        & $vm shot (Join-Path $here "out\guest-$Name.png")
    }
    if ($NoWait) { exit 0 }
    # Wait for the program to leave by itself (it was given --frames).
    for ($i = 0; $i -lt 120; $i++) {
        if (-not ((& $vm ps) -match [regex]::Escape($Exe.ToUpper()) -or (& $vm ps) -match [regex]::Escape($Exe))) { break }
        Start-Sleep -Seconds 2
    }
} else {
    if ($Arguments) { & $vm run "C:\MKSC\$Exe" $Arguments } else { & $vm run "C:\MKSC\$Exe" }
}
& $vm get 'C:\MKSC\mksc98.log' $log
Get-Content $log | Where-Object { $_ -notmatch '^GP rendered race' } | Select-Object -Last $Tail
