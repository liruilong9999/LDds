param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$codegenSrc = Join-Path $repoRoot "scripts\stage13_phase7_codegen"
$validatorSrc = Join-Path $repoRoot "scripts\stage13_phase7_validator"

$codegenOut = Join-Path $repoRoot "build\stage13_phase7_codegen\out"
$validatorOut = Join-Path $repoRoot "build\stage13_phase7_validator\out"
$generatedDir = Join-Path $repoRoot "build\stage13_phase7_smoke\generated"

New-Item -ItemType Directory -Force -Path $generatedDir | Out-Null

cmake -S $codegenSrc -B $codegenOut
cmake --build $codegenOut --config $Config

$env:PATH = "$repoRoot\bin;$env:PATH"
$codegenExe = Join-Path $codegenOut "$Config\stage13_phase7_codegen.exe"
if (!(Test-Path $codegenExe)) {
    Write-Error "stage13_phase7_codegen.exe not found: $codegenExe"
}

& $codegenExe $generatedDir
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (Test-Path $validatorOut) {
    Remove-Item -Recurse -Force $validatorOut
}
cmake -S $validatorSrc -B $validatorOut "-DPHASE7_GEN_DIR=$generatedDir"
cmake --build $validatorOut --config $Config

$validatorExe = Join-Path $validatorOut "$Config\stage13_phase7_validator.exe"
if (!(Test-Path $validatorExe)) {
    Write-Error "stage13_phase7_validator.exe not found: $validatorExe"
}

& $validatorExe
exit $LASTEXITCODE
