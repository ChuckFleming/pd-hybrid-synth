# Validates the built VST3 with Tracktion's pluginval.
# Downloads pluginval to build/tools if it isn't already present, then runs it
# at strictness level 8 (param fuzzing, state save/restore, threads, editor).
#
# Usage (from repo root, after a Release build):
#   powershell -ExecutionPolicy Bypass -File scripts/run_pluginval.ps1

$ErrorActionPreference = "Stop"

$root    = Split-Path -Parent $PSScriptRoot
$vst3    = Join-Path $root "build\PDHybridSynth_artefacts\Release\VST3\PD Hybrid Synth.vst3"
$toolDir = Join-Path $root "build\tools"
$exe     = Join-Path $toolDir "pluginval.exe"

if (-not (Test-Path $vst3)) {
    Write-Error "VST3 not found at $vst3 - build the Release config first."
}

if (-not (Test-Path $exe)) {
    New-Item -ItemType Directory -Force -Path $toolDir | Out-Null
    $zip = Join-Path $toolDir "pluginval.zip"
    $url = "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip"
    Write-Output "Downloading pluginval from $url ..."
    Invoke-WebRequest -Uri $url -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $toolDir -Force
    Remove-Item $zip
}

Write-Output "Running pluginval on: $vst3"
& $exe --strictness-level 8 --validate-in-process --skip-gui-tests "$vst3"
Write-Output "PLUGINVAL_EXIT=$LASTEXITCODE"
exit $LASTEXITCODE
