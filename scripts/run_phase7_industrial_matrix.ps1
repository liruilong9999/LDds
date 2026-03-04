param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

function Invoke-CapturedStageScript {
    param(
        [Parameter(Mandatory = $true)][string]$ScriptPath,
        [string]$ConfigValue
    )

    $args = @("-ExecutionPolicy", "Bypass", "-File", $ScriptPath)
    if ($ConfigValue) {
        $args += @("-Config", $ConfigValue)
    }

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $capturedOutput = & powershell @args 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    return [pscustomobject]@{
        Output = $capturedOutput
        ExitCode = $exitCode
    }
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$reportDir = Join-Path $repoRoot "reports"
$reportPath = Join-Path $reportDir "phase7_industrial_baseline.md"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null

$runAllResult = Invoke-CapturedStageScript -ScriptPath (Join-Path $repoRoot "scripts\run_all_stage_tests.ps1")
$stage12Result = Invoke-CapturedStageScript -ScriptPath (Join-Path $repoRoot "scripts\run_stage12_phase6_smoke.ps1") -ConfigValue $Config
$stage13Result = Invoke-CapturedStageScript -ScriptPath (Join-Path $repoRoot "scripts\run_stage13_phase7_smoke.ps1") -ConfigValue $Config

if ($runAllResult.ExitCode -ne 0) {
    throw "run_all_stage_tests failed with exit code $($runAllResult.ExitCode)"
}
if ($stage12Result.ExitCode -ne 0) {
    throw "run_stage12_phase6_smoke failed with exit code $($stage12Result.ExitCode)"
}
if ($stage13Result.ExitCode -ne 0) {
    throw "run_stage13_phase7_smoke failed with exit code $($stage13Result.ExitCode)"
}

$runAllOutput = $runAllResult.Output
$stage12Output = $stage12Result.Output
$stage13Output = $stage13Result.Output

$stage8Summary = "N/A"
$stage8Match = [regex]::Match($runAllOutput, "\[stage8\]\[RECEIVER\].*done callbacks=(\d+) deadlineMissed=(\d+)")
if ($stage8Match.Success) {
    $stage8Summary = "callbacks=$($stage8Match.Groups[1].Value), deadlineMissed=$($stage8Match.Groups[2].Value)"
}

$perfSummary = "N/A"
$perfMatch = [regex]::Match($stage12Output, "perf_plain_ms=([0-9\.]+)\s+perf_secure_ms=([0-9\.]+)\s+delta_pct=([-0-9\.]+)")
if ($perfMatch.Success) {
    $perfSummary = "plain_ms=$($perfMatch.Groups[1].Value), secure_ms=$($perfMatch.Groups[2].Value), delta_pct=$($perfMatch.Groups[3].Value)"
}

$stage13Summary = "N/A"
if ($stage13Output -match "\[stage13_codegen\] result=ok" -and $stage13Output -match "\[stage13_validator\] result=ok") {
    $stage13Summary = "codegen=ok, validator=ok"
}

$report = @"
# Phase 7 Industrial Baseline Report

- Generated At: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz")
- Host OS: $([System.Environment]::OSVersion.VersionString)
- Config: $Config

## 1. Verification Matrix

| Item | Status | Notes |
|---|---|---|
| Windows Smoke/Stress | DONE | run_all_stage_tests PASS; stage8: $stage8Summary |
| Phase7 IDL Round-trip | DONE | stage13: $stage13Summary |
| Linux Smoke/Stress | TODO | Add Linux CI runner and mirror stage scripts |
| Network Emulation (loss/reorder/jitter) | TODO | Recommend ``tc netem`` profiles: loss 5%, reorder 20%, delay 50ms+20ms |
| OpenDDS Interop Pre-study | TODO | Separate spike project, map message header compatibility and discovery path |

## 2. Baseline Numbers

- Stage8 stress summary: $stage8Summary
- Phase6 security perf summary: $perfSummary
- Phase7 codegen summary: $stage13Summary

## 3. Next Actions

1. Add Linux job to CI and run stage3/4/7/56/8 + stage13.
2. Add ``tc netem`` scripted profiles and collect latency/throughput/recovery curves.
3. Create OpenDDS interop spike repo with protocol adapter experiments.
"@

Set-Content -Path $reportPath -Value $report -Encoding UTF8
Write-Output "[phase7-matrix] report=$reportPath"
Write-Output "[phase7-matrix] done"

