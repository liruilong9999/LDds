param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$srcDir = Join-Path $repoRoot "scripts\stage2_tcp_reconnect_smoke"
$outDir = Join-Path $repoRoot "build\stage2_tcp_reconnect_smoke\out"

cmake -S $srcDir -B $outDir
cmake --build $outDir --config $Config

$env:PATH = "$repoRoot\bin;$env:PATH"
$exe = Join-Path $outDir "$Config\stage2_tcp_reconnect_smoke.exe"
if (!(Test-Path $exe)) {
    Write-Error "stage2_tcp_reconnect_smoke.exe not found: $exe"
}

$nativeExit = -1
try {
    & $exe
    if ($null -ne $LASTEXITCODE) {
        $nativeExit = [int]$LASTEXITCODE
    } else {
        $nativeExit = 0
    }
} catch {
    Write-Host "[stage2_tcp] runner error: $($_.Exception.Message)"
    if ($null -ne $LASTEXITCODE -and [int]$LASTEXITCODE -ne 0) {
        $nativeExit = [int]$LASTEXITCODE
    }
}

exit $nativeExit
