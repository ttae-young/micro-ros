param(
    [string]$Project = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$python = "$env:USERPROFILE\pyocd-win\Scripts\python.exe"
$bridgeFile = Join-Path $Project 'serial_tcp_bridge.py'
$helper = Join-Path $PSScriptRoot 'start_agent_wsl.sh'
foreach ($file in @($python,$bridgeFile,$helper)) {
    if (!(Test-Path -LiteralPath $file)) { throw "Missing file: $file" }
}
$logBase = Join-Path $env:TEMP ('profile-agent-' + [guid]::NewGuid().ToString('N'))
$bridge = $null
try {
    Write-Host 'Starting COM7 bridge in background...'
    $bridge = Start-Process -FilePath $python -ArgumentList @('-u',('"' + $bridgeFile + '"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logBase.out.log" -RedirectStandardError "$logBase.err.log"
    $ready = $false
    for ($i=0; $i -lt 100; $i++) {
        $bridge.Refresh()
        if ($bridge.HasExited) {
            Get-Content "$logBase.err.log" -ErrorAction SilentlyContinue | Write-Host
            throw 'COM7 bridge failed. Check USB/COM7 and close any old bridge.'
        }
        if ((Get-Content "$logBase.out.log" -Raw -ErrorAction SilentlyContinue) -match 'listening on TCP') { $ready=$true; break }
        Start-Sleep -Milliseconds 100
    }
    if (!$ready) { throw "Bridge startup timeout. Logs: $logBase" }
    # --exec avoids the default shell interpreting Windows backslashes.
    # Forward slashes also survive Windows PowerShell 5.1 argument passing.
    $helperForWsl = $helper.Replace('\', '/')
    $linuxHelper = & wsl.exe -d Ubuntu-22.04 --exec wslpath -a -u $helperForWsl
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the WSL helper path.' }
    Write-Host 'Starting link + Agent. Keep this window open. Ctrl+C stops this session.'
    & wsl.exe -d Ubuntu-22.04 --exec bash ($linuxHelper.Trim())
    if ($LASTEXITCODE -ne 0) { throw "Agent/link exited. Bridge logs: $logBase" }
} finally {
    if ($null -ne $bridge) {
        $bridge.Refresh()
        if (!$bridge.HasExited) { Stop-Process -Id $bridge.Id -ErrorAction SilentlyContinue }
    }
}
