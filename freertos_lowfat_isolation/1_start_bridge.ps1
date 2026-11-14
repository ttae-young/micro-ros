$ErrorActionPreference = 'Stop'
Write-Host 'Keep this window open. Next open another PowerShell and run 3_start_link.ps1.'
& "$env:USERPROFILE/pyocd-win/Scripts/python.exe" (Join-Path $PSScriptRoot 'serial_tcp_bridge.py')
