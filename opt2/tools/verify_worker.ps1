param([Parameter(Mandatory=$true)][string]$Elf)
$ErrorActionPreference='Stop'
$python=Join-Path $env:USERPROFILE 'pyocd-win/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { throw "Python not found: $python" }
& $python (Join-Path $PSScriptRoot 'verify_worker.py') $Elf
if ($LASTEXITCODE -ne 0) { throw 'Worker validation failed. Do not flash this ELF.' }
