Write-Host 'Keep this window open. After successfully connected, run 4_start_agent.ps1 in another window.'
& wsl.exe -d Ubuntu-22.04 -- bash -lc 'socat -d -d pty,link=/tmp/ttyMICROROS,raw,echo=0 tcp:192.168.64.1:8888'
