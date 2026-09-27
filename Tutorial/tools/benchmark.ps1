param(
    [string]$Executable = "",
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$tutorialDirectory = Split-Path -Parent $PSScriptRoot
if (!$Executable) { $Executable = Join-Path $tutorialDirectory 'build/AshenShore.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (!$OutputDirectory) { $OutputDirectory = Join-Path $tutorialDirectory ("profiles/benchmark-" + [guid]::NewGuid().ToString('N')) }
$runDirectory = (New-Item -ItemType Directory -Force -Path $OutputDirectory).FullName
foreach ($scenario in @('level1','tutorial')) {
    $scenarioDirectory = (New-Item -ItemType Directory -Force -Path (Join-Path $runDirectory $scenario)).FullName
    $arguments = '--benchmark --quiet-profile --validate-render'
    if ($scenario -eq 'tutorial') { $arguments += ' --tutorial' }
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $scenarioDirectory -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "$scenario exited with $($process.ExitCode)" }
    $session = Get-ChildItem -LiteralPath (Join-Path $scenarioDirectory 'profiles') -Directory | Sort-Object LastWriteTime | Select-Object -Last 1
    $summaryPath = Join-Path $scenarioDirectory 'summary.json'
    & python (Join-Path $PSScriptRoot 'analyze_profile.py') $session.FullName --output $summaryPath --assert-clean
    if ($LASTEXITCODE -ne 0) { throw "Profile validation failed: $scenario" }
    Write-Output $summaryPath
}
