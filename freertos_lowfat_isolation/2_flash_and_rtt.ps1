param(
    [string]$Firmware = (Join-Path $PSScriptRoot 'Debug/freertos_lowfat_isolation.elf'),
    [string]$Python = "$env:USERPROFILE/pyocd-win/Scripts/python.exe",
    [string]$Programmer = ''
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Firmware)) { throw 'Build the Debug project in CubeIDE first.' }
if (!(Test-Path -LiteralPath $Python)) { throw 'Set -Python to your pyOCD environment python.exe.' }
if (!$Programmer) {
    $Programmer = Get-ChildItem 'C:/ST/STM32CubeIDE*/STM32CubeIDE/plugins/*cubeprogrammer*/tools/bin/STM32_Programmer_CLI.exe' |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (!$Programmer) { throw 'Set -Programmer to STM32_Programmer_CLI.exe.' }
Write-Host "Flashing $Firmware"
& $Programmer -c port=SWD -w $Firmware -v -rst
if ($LASTEXITCODE -ne 0) { throw 'Flashing failed.' }
Write-Host 'Reading RTT. Wait for [BENCH DONE]. Ctrl+C stops the reader.'
& $Python -m pyocd rtt -t cortex_m -M attach -a 0x20000000 -s 0x30000


