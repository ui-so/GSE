param([switch]$NoGame, [switch]$NoBrowser, [string]$ChromePath = '')
$ErrorActionPreference = 'Stop'
$url = 'http://127.0.0.1:8765'

function Resolve-MonitorChrome {
    param([string]$ExplicitPath)
    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "Chrome executable does not exist: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }
    $candidates = @()
    $command = Get-Command chrome.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LOCALAPPDATA)) {
        if ($root) { $candidates += Join-Path $root 'Google/Chrome/Application/chrome.exe' }
    }
    foreach ($key in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\chrome.exe',
                       'HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\chrome.exe')) {
        if (Test-Path -LiteralPath $key) { $candidates += (Get-Item -LiteralPath $key).GetValue('') }
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Google Chrome was not found. Install Chrome or provide -ChromePath. Use -NoBrowser for server-only mode.'
}

# Resolve the explicitly requested browser before starting any background process.
$chrome = if (!$NoBrowser) { Resolve-MonitorChrome $ChromePath }
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
if (!$NoBrowser) {
    Start-Process -FilePath $chrome -ArgumentList @('--new-window', $url) -WindowStyle Normal
    Write-Output "Chrome monitoring window: $url"
}
if (!$NoGame) {
    $exe = Join-Path $PSScriptRoot '../Tutorial/build/AshenShore.exe'
    if (!(Test-Path $exe)) { throw 'Build the game with Tutorial/build.cmd first. The monitor is ready.' }
    Start-Process -FilePath $exe -ArgumentList '--quiet-profile' -WorkingDirectory (Split-Path $exe)
}
Write-Output "Monitor ready: $url"
