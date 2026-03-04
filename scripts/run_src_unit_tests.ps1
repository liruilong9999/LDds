param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$reportDir = Join-Path $repoRoot "reports"
$reportPath = Join-Path $reportDir "src_unit_test_report.md"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null

& cmake -S $repoRoot -B (Join-Path $repoRoot "build") | Out-Null
& cmake --build (Join-Path $repoRoot "build") --config $Config --target LDdsCoreUnitTests | Out-Null

$env:PATH = "$repoRoot\bin;$env:PATH"
$testExe = Join-Path $repoRoot "bin\LDdsCoreUnitTests.exe"
if (!(Test-Path $testExe)) {
    throw "test executable not found: $testExe"
}

$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    $testOutput = & $testExe 2>&1 | Out-String
    $testExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousErrorAction
}

$normalizedOutput = $testOutput -replace "`r", ""

$caseMatches = [regex]::Matches(
    $normalizedOutput,
    "(?m)^\[ut\]\[case\]\s+(PASS|FAIL)\s+(\S+)\s+duration_ms=([0-9\.]+)(?:\s+detail=(.*))?$")

$summaryMatch = [regex]::Match(
    $normalizedOutput,
    "(?m)^\[ut\]\[summary\]\s+total=(\d+)\s+passed=(\d+)\s+failed=(\d+)\s+duration_ms=([0-9\.]+)$")

$tableRows = @()
foreach ($m in $caseMatches) {
    $status = $m.Groups[1].Value
    $name = $m.Groups[2].Value
    $duration = $m.Groups[3].Value
    $detail = $m.Groups[4].Value
    if ([string]::IsNullOrWhiteSpace($detail)) {
        $detail = "-"
    }
    $tableRows += "| $name | $status | $duration | $detail |"
}

$summaryLine = "N/A"
if ($summaryMatch.Success) {
    $summaryLine = "total=$($summaryMatch.Groups[1].Value), passed=$($summaryMatch.Groups[2].Value), failed=$($summaryMatch.Groups[3].Value), duration_ms=$($summaryMatch.Groups[4].Value)"
}

$generatedAt = Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"

$reportLines = @(
    "# Src Unit Test Report",
    "",
    "- Generated At: $generatedAt",
    "- Config: $Config",
    "- Command: scripts/run_src_unit_tests.ps1 -Config $Config",
    "- Exit Code: $testExitCode",
    "",
    "## 1. Summary",
    "",
    "- $summaryLine",
    "",
    "## 2. Case Results",
    "",
    "| Case | Status | Duration(ms) | Detail |",
    "|---|---|---:|---|"
)

if ($tableRows.Count -gt 0) {
    $reportLines += $tableRows
} else {
    $reportLines += "| - | - | - | no cases parsed |"
}

$reportLines += @(
    "",
    "## 3. Raw Output",
    "",
    '```text',
    $testOutput.TrimEnd(),
    '```',
    ""
)

$report = $reportLines -join [Environment]::NewLine

Set-Content -Path $reportPath -Value $report -Encoding UTF8
Write-Output "[src-unit-test] report=$reportPath"
Write-Output "[src-unit-test] exit=$testExitCode"

if ($testExitCode -ne 0) {
    exit $testExitCode
}
