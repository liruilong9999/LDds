param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$srcDir = Join-Path $repoRoot "scripts\stage1_domain_smoke"
$outDir = Join-Path $repoRoot "build\stage1_domain_smoke\out"

cmake -S $srcDir -B $outDir

cmake --build $outDir --config $Config

$env:PATH = "$repoRoot\bin;$env:PATH"
$exe = Join-Path $outDir "$Config\stage1_domain_smoke.exe"
if (!(Test-Path $exe)) {
    Write-Error "stage1_domain_smoke.exe not found: $exe"
}

& $exe
exit $LASTEXITCODE
