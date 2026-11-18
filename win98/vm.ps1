# Test-harness wrapper around vmrun for the Windows 98 SE guest.
#   vm.ps1 put   HOSTFILE GUESTFILE      copy a file into the guest
#   vm.ps1 get   GUESTFILE HOSTFILE      copy a file out of the guest
#   vm.ps1 run   GUESTEXE [ARGS...]      run and wait for exit
#   vm.ps1 start GUESTEXE [ARGS...]      run without waiting (interactive window)
#   vm.ps1 shot  HOSTFILE.png            capture the guest screen
#   vm.ps1 mkdir GUESTDIR | ls GUESTDIR | ps | kill PID | rm GUESTFILE
# vmrun goes through cmd.exe because Windows PowerShell 5.1 drops the empty
# password argument when it calls a native program directly.
param([Parameter(Mandatory = $true)][string]$Action,
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)

$vmrun = 'C:\Program Files\VMware\VMware Workstation\vmrun.exe'
# The guest's .vmx: $env:MKSC_VMX, else "Windows 98 VM\Windows 98.vmx" in the
# folder that holds this repository.
$repoParent = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$vmx = if ($env:MKSC_VMX) { $env:MKSC_VMX } else { Join-Path $repoParent 'Windows 98 VM\Windows 98.vmx' }
if (-not (Test-Path $vmx)) { Write-Output "no VM at $vmx; set MKSC_VMX to the guest's .vmx file"; exit 2 }
$user = if ($env:MKSC_GUEST_USER) { $env:MKSC_GUEST_USER } else { 'Administrator' }
$password = "$env:MKSC_GUEST_PASSWORD"

function Quote([string]$text) { '"' + $text + '"' }
function Invoke-Vmrun([string]$command, [string[]]$arguments, [switch]$Guest, [string]$Flags = "") {
    $line = (Quote $vmrun) + ' -T ws'
    if ($Guest) { $line += ' -gu ' + (Quote $user) + ' -gp ' + (Quote $password) }
    $line += ' ' + $command + ' ' + (Quote $vmx)
    if ($Flags) { $line += ' ' + $Flags }
    foreach ($argument in $arguments) { $line += ' ' + (Quote $argument) }
    cmd /c $line
    if ($LASTEXITCODE -ne 0) { Write-Output "vmrun $command failed (exit $LASTEXITCODE)"; exit 1 }
}

switch ($Action) {
    'put'   { Invoke-Vmrun 'copyFileFromHostToGuest' @((Resolve-Path $Rest[0]).Path, $Rest[1]) -Guest }
    'get'   { Invoke-Vmrun 'copyFileFromGuestToHost' @($Rest[0], $Rest[1]) -Guest }
    'run'   { Invoke-Vmrun 'runProgramInGuest' $Rest -Guest -Flags '-activeWindow' }
    'start' { Invoke-Vmrun 'runProgramInGuest' $Rest -Guest -Flags '-noWait -activeWindow' }
    'shot'  { Invoke-Vmrun 'captureScreen' @($Rest[0]) -Guest }
    'mkdir' { Invoke-Vmrun 'createDirectoryInGuest' @($Rest[0]) -Guest }
    'ls'    { Invoke-Vmrun 'listDirectoryInGuest' @($Rest[0]) -Guest }
    'ps'    { Invoke-Vmrun 'listProcessesInGuest' @() -Guest }
    'kill'  { Invoke-Vmrun 'killProcessInGuest' @($Rest[0]) -Guest }
    'rm'    { Invoke-Vmrun 'deleteFileInGuest' @($Rest[0]) -Guest }
    default { Write-Output "unknown action $Action"; exit 2 }
}
