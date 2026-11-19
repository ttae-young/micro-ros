param(
    [string]$Project = $PSScriptRoot,
    [string]$BridgeHost = ""
)
$ErrorActionPreference = 'Stop'
$python = "$env:USERPROFILE\pyocd-win\Scripts\python.exe"
$bridgeFile = Join-Path $Project 'serial_tcp_bridge.py'
$helper = Join-Path $PSScriptRoot 'start_agent_wsl.sh'
foreach ($file in @($python,$bridgeFile,$helper)) {
    if (!(Test-Path -LiteralPath $file)) { throw "Missing file: $file" }
}
# Start WSL BEFORE binding the Windows bridge. Its virtual adapter may not
# exist until the distribution has started. Resolve paths before opening COM7.
$helperForWsl = $helper.Replace('\', '/')
$linuxHelper = & wsl.exe -d Ubuntu-22.04 --exec wslpath -a -u $helperForWsl
if ($LASTEXITCODE -ne 0) { throw 'Cannot start WSL or resolve the WSL helper path.' }
if (!$BridgeHost) {
    $route = & wsl.exe -d Ubuntu-22.04 --exec ip -4 route show default
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read WSL default route.' }
    $match = [regex]::Match(($route -join ' '), 'default via ([0-9.]+)')
    if (!$match.Success) { throw 'Cannot detect Windows gateway. Set -BridgeHost explicitly.' }
    $BridgeHost = $match.Groups[1].Value
}
$bridgeAddress = $null
if (![Net.IPAddress]::TryParse($BridgeHost,[ref]$bridgeAddress) -or
    $bridgeAddress.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'BridgeHost must be an IPv4 address.'
}
# Allow the WSL virtual interface a short initialization interval.
$bound = $false
for ($i=0; $i -lt 20; $i++) {
    $probe = New-Object Net.Sockets.TcpListener($bridgeAddress,0)
    try { $probe.Start(); $bound=$true } catch { Start-Sleep -Milliseconds 250 } finally { $probe.Stop() }
    if ($bound) { break }
}
if (!$bound) { throw "WSL gateway $BridgeHost is not assigned to Windows. Set -BridgeHost to the Windows address reachable from WSL." }
Write-Host "WSL ready. Windows bridge address: $BridgeHost"
$logBase = Join-Path $env:TEMP ('profile-agent-' + [guid]::NewGuid().ToString('N'))
$bridge = $null
try {
    Write-Host 'Starting COM7 bridge in background...'
    $bridge = Start-Process -FilePath $python -ArgumentList @('-u',('"' + $bridgeFile + '"'),'--bind-host',$BridgeHost) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logBase.out.log" -RedirectStandardError "$logBase.err.log"
    $ready = $false
    for ($i=0; $i -lt 100; $i++) {
        $bridge.Refresh()
        if ($bridge.HasExited) {
            Get-Content "$logBase.err.log" -ErrorAction SilentlyContinue | Write-Host
            throw "Bridge startup failed. See the Python error above (COM7 or TCP ${BridgeHost}:8888)."
        }
        if ((Get-Content "$logBase.out.log" -Raw -ErrorAction SilentlyContinue) -match 'listening on TCP') { $ready=$true; break }
        Start-Sleep -Milliseconds 100
    }
    if (!$ready) { throw "Bridge startup timeout. Logs: $logBase" }
    Write-Host 'Starting link + Agent. Keep this window open. Ctrl+C stops this session.'
    & wsl.exe -d Ubuntu-22.04 --exec bash ($linuxHelper.Trim()) $BridgeHost
    if ($LASTEXITCODE -ne 0) { throw "Agent/link exited. Bridge logs: $logBase" }
} finally {
    if ($null -ne $bridge) {
        $bridge.Refresh()
        if (!$bridge.HasExited) { Stop-Process -Id $bridge.Id -ErrorAction SilentlyContinue }
    }
}
