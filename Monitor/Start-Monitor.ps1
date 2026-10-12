param([switch]$NoGame, [switch]$NoBrowser)
$ErrorActionPreference = 'Stop'
$url = 'http://127.0.0.1:8765'
$connected = $false
try {
    $state = Invoke-RestMethod "$url/api/state" -TimeoutSec 2
    if ($state.application -ne 'gse-monitor') { throw 'Port 8765 belongs to another application.' }
    $connected = $true
} catch {
    if ($_.Exception.Message -like '*another application*') { throw }
}
if (!$connected) {
    $python = (Get-Command python -ErrorAction Stop).Source
    $runtime = New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot '.runtime')
    $process = Start-Process -FilePath $python -ArgumentList 'server.py' -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runtime 'stdout.log') -RedirectStandardError (Join-Path $runtime 'stderr.log')
    for ($attempt = 0; $attempt -lt 25; $attempt++) {
        if ($process.HasExited) { throw "Monitor server exited. Read $runtime/stderr.log" }
        try {
            $state = Invoke-RestMethod "$url/api/state" -TimeoutSec 1
            if ($state.application -eq 'gse-monitor') { $connected = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 200
    }
    if (!$connected) {
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
        throw 'Monitor did not start on port 8765.'
    }
    Set-Content (Join-Path $runtime 'server.pid') $process.Id
}
if (!$NoBrowser) { Start-Process $url }
if (!$NoGame) {
    $exe = Join-Path $PSScriptRoot '../Tutorial/build/AshenShore.exe'
    if (!(Test-Path $exe)) { throw 'Build the game with Tutorial/build.cmd first. The monitor is ready.' }
    Start-Process -FilePath $exe -ArgumentList '--quiet-profile' -WorkingDirectory (Split-Path $exe)
}
Write-Output "Monitor ready: $url"
