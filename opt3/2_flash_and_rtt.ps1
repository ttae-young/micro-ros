param(
    [string]$Firmware = (Join-Path $PSScriptRoot 'Debug/freertos_app_sfi.elf'),
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
$firmwareTime=(Get-Item -LiteralPath $Firmware).LastWriteTimeUtc
$changed=Get-ChildItem (Join-Path $PSScriptRoot 'Core'),(Join-Path $PSScriptRoot 'Middlewares') -Recurse -File |
    Where-Object { $_.Extension -in @('.c','.h','.s','.S') -and $_.LastWriteTimeUtc -gt $firmwareTime } |
    Select-Object -First 1
if ($changed) { throw "Source changed after this ELF: $($changed.Name). Save, Clean and Build successfully before flashing." }
& (Join-Path $PSScriptRoot 'tools/verify_worker.ps1') -Elf $Firmware
Write-Host "Flashing $Firmware"
# Release a previous RTT session cleanly before CubeProgrammer connects.
$probeId = @'
from pyocd.core.helpers import ConnectHelper
probes = ConnectHelper.get_all_connected_probes(blocking=False)
if len(probes) != 1:
    raise RuntimeError("Expected one debug probe. Close debuggers and connect only the target board.")
uid = probes[0].unique_id
with ConnectHelper.session_with_chosen_probe(unique_id=uid, target_override="cortex_m", connect_mode="attach", resume_on_disconnect=False) as session:
    session.target.reset_and_halt()
print(uid)
'@ | & $Python -
if ($LASTEXITCODE -ne 0) { throw 'ST-LINK recovery failed. Close other RTT/debug sessions and reconnect the ST-LINK USB cable.' }
$probeId = ($probeId | Select-Object -Last 1).Trim()
& $Programmer -c port=SWD "sn=$probeId" mode=UR reset=HWrst freq=1000 -w $Firmware -v -rst
if ($LASTEXITCODE -ne 0) { throw 'Flashing failed.' }
Write-Host 'Reading RTT and saving logs. Reader closes automatically after [BENCH DONE].'
& $Python (Join-Path $PSScriptRoot 'tools/capture_rtt.py') $Firmware
if ($LASTEXITCODE -ne 0) { throw 'RTT capture incomplete. See the saved log.' }


