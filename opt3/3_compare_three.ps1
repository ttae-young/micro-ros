param([ValidateSet(0,1)][int]$DirectRead=1)
$ErrorActionPreference='Stop'
$python=Join-Path $env:USERPROFILE 'pyocd-win/Scripts/python.exe'
$baseline=Join-Path $PSScriptRoot "logs/attack${DirectRead}_mode1_latest.log"
$mpu=Join-Path $PSScriptRoot "logs/attack${DirectRead}_mode2_latest.log"
$sfi=Join-Path $PSScriptRoot "logs/attack${DirectRead}_mode3_latest.log"
$outputFile=Join-Path $PSScriptRoot "logs/attack${DirectRead}_comparison.csv"
& $python (Join-Path $PSScriptRoot 'tools/compare_three.py') $baseline $mpu $sfi $outputFile
if ($LASTEXITCODE -ne 0) { throw 'Comparison failed. Run all three modes with the same BENCH_DIRECT_READ setting.' }
Write-Host "Saved: $outputFile"
Import-Csv -LiteralPath $outputFile | Where-Object { $_.metric -in @('pub_request_to_all_done','all_dispatch_excluding_middleware') } |
    Format-Table metric,baseline_mean_us,mpu_mean_us,sfi_mean_us,mpu_over_baseline,sfi_over_baseline -AutoSize
