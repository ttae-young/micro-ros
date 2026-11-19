param(
    [string]$OffLog=(Join-Path $PSScriptRoot 'logs/mode1_latest.log'),
    [string]$MpuLog=(Join-Path $PSScriptRoot 'logs/mode2_latest.log')
)
$ErrorActionPreference='Stop'
$python=Join-Path $env:USERPROFILE 'pyocd-win/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { throw "Python not found: $python" }
$outputFile=Join-Path $PSScriptRoot 'logs/overhead.csv'
& $python (Join-Path $PSScriptRoot 'tools/compare_overhead.py') $OffLog $MpuLog $outputFile
if ($LASTEXITCODE -ne 0) { throw 'Comparison failed. Run MODE1 and MODE2 to valid=YES first.' }
Write-Host "Saved: $outputFile"
Import-Csv -LiteralPath $outputFile | Format-Table metric,count,total_overhead_us,mean_overhead_us,protected_over_off -AutoSize
