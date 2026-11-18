# Builds win98/exports/<dll>.exports: the export names of the guest's own
# system DLLs, used by check_exe.sh to prove every import of the final .exe
# exists on a stock Windows 98 SE install. The DLLs themselves are copied to
# a temporary directory and are not kept.
param([string]$Temp = (Join-Path $env:TEMP 'mksc-win98-dlls'))
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path $here 'exports'
New-Item -ItemType Directory -Force $Temp, $out | Out-Null
$dlls = 'KERNEL32', 'USER32', 'GDI32', 'WINMM', 'MSVCRT', 'DDRAW', 'DSOUND', 'ADVAPI32', 'SHELL32', 'OLE32'
foreach ($dll in $dlls) {
    $local = Join-Path $Temp "$dll.DLL"
    & (Join-Path $here 'vm.ps1') get "C:\WINDOWS\SYSTEM\$dll.DLL" $local
    if (-not (Test-Path $local)) { Write-Output "could not fetch $dll.DLL"; continue }
    $wsl = '/mnt/' + $local.Substring(0, 1).ToLower() + ($local.Substring(2) -replace '\\', '/')
    $names = wsl -d Ubuntu -- bash -c "i686-w64-mingw32-objdump -p '$wsl' | awk '/\[Ordinal\/Name Pointer\] Table/{t=1;next} t&&/^\t\[ *[0-9]+\]/{print \`$NF}'"
    $target = Join-Path $out ($dll.ToLower() + '.dll.exports')
    [IO.File]::WriteAllLines($target, [string[]]$names)
    $info = (Get-Item $local).VersionInfo
    Write-Output ("{0,-10} {1,5} exports  version {2}" -f $dll, @($names).Count, $info.FileVersion)
}
