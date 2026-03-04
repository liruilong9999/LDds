param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",
    [string]$BinDir = "bin",
    [switch]$SkipStage8,
    [switch]$SkipBuild,
    [switch]$StopOnCrash
)

$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

function Resolve-RepoRoot {
    $scriptPath = $PSCommandPath
    if ([string]::IsNullOrWhiteSpace($scriptPath)) {
        $scriptPath = $MyInvocation.MyCommand.Definition
    }
    $scriptDir = Split-Path -Parent $scriptPath
    return (Resolve-Path (Join-Path $scriptDir "..")).Path
}

function New-RunContext {
    param(
        [string]$RepoRootPath
    )

    $logsDir = Join-Path $RepoRootPath "build\stage_runs"
    New-Item -ItemType Directory -Path $logsDir -Force | Out-Null

    return [PSCustomObject]@{
        RepoRoot = $RepoRootPath
        LogsDir = $logsDir
        BinPath = Join-Path $RepoRootPath $BinDir
    }
}

function Invoke-StageExecutable {
    param(
        [string]$StageName,
        [string]$ExePath,
        [string[]]$Arguments,
        [string]$LogPath,
        [string]$ErrPath
    )

    if (!(Test-Path $ExePath)) {
        return [PSCustomObject]@{
            Stage = $StageName
            Success = $false
            ExitCode = -1
            DurationMs = 0
            IsCrash = $false
            Summary = "executable not found: $ExePath"
            Log = $LogPath
            Err = $ErrPath
        }
    }

    $start = Get-Date
    Remove-Item $LogPath, $ErrPath -ErrorAction SilentlyContinue

    Push-Location (Split-Path -Parent $ExePath)
    $nativeExit = -1
    try {
        if ($null -ne $Arguments -and $Arguments.Count -gt 0) {
            & $ExePath @Arguments 1> $LogPath 2> $ErrPath
        } else {
            & $ExePath 1> $LogPath 2> $ErrPath
        }
        $nativeExit = $LASTEXITCODE
    } catch {
        $errorText = $_ | Out-String
        Add-Content -Path $ErrPath -Value $errorText
        if ($null -ne $LASTEXITCODE) {
            $nativeExit = [int]$LASTEXITCODE
        }
    } finally {
        Pop-Location
    }

    $end = Get-Date
    $duration = [int][Math]::Round(($end - $start).TotalMilliseconds)
    $exitCode = if ($null -eq $nativeExit) { 0 } else { [int]$nativeExit }
    $ok = ($exitCode -eq 0)
    $isCrash = ($exitCode -lt 0)

    $tail = ""
    if (Test-Path $LogPath) {
        $tail = (Get-Content $LogPath -Tail 3 | ForEach-Object { $_.Trim() }) -join " | "
    }
    if ([string]::IsNullOrWhiteSpace($tail) -and (Test-Path $ErrPath)) {
        $tail = (Get-Content $ErrPath -Tail 3 | ForEach-Object { $_.Trim() }) -join " | "
    }
    if ([string]::IsNullOrWhiteSpace($tail)) {
        $tail = "(no output)"
    }

    return [PSCustomObject]@{
        Stage = $StageName
        Success = $ok
        ExitCode = $exitCode
        DurationMs = $duration
        IsCrash = $isCrash
        Summary = $tail
        Log = $LogPath
        Err = $ErrPath
    }
}

function Invoke-StageBuild {
    param(
        [string]$StageName,
        [string]$BuildDir,
        [string]$BuildLog,
        [string]$BuildErr
    )

    if (!(Test-Path $BuildDir)) {
        return [PSCustomObject]@{
            Stage = $StageName
            Success = $false
            ExitCode = -1
            DurationMs = 0
            IsCrash = $false
            Summary = "build dir not found: $BuildDir"
            Log = $BuildLog
            Err = $BuildErr
        }
    }

    $start = Get-Date
    Remove-Item $BuildLog, $BuildErr -ErrorAction SilentlyContinue
    Push-Location $BuildDir
    $nativeExit = -1
    try {
        & cmake --build . --config $Config 1> $BuildLog 2> $BuildErr
        $nativeExit = $LASTEXITCODE
    } catch {
        $errorText = $_ | Out-String
        Add-Content -Path $BuildErr -Value $errorText
        if ($null -ne $LASTEXITCODE) {
            $nativeExit = [int]$LASTEXITCODE
        }
    } finally {
        Pop-Location
    }

    $end = Get-Date
    $duration = [int][Math]::Round(($end - $start).TotalMilliseconds)
    $exitCode = if ($null -eq $nativeExit) { 0 } else { [int]$nativeExit }
    $ok = ($exitCode -eq 0)

    $tail = ""
    if (Test-Path $BuildLog) {
        $tail = (Get-Content $BuildLog -Tail 3 | ForEach-Object { $_.Trim() }) -join " | "
    }
    if ([string]::IsNullOrWhiteSpace($tail) -and (Test-Path $BuildErr)) {
        $tail = (Get-Content $BuildErr -Tail 3 | ForEach-Object { $_.Trim() }) -join " | "
    }
    if ([string]::IsNullOrWhiteSpace($tail)) {
        $tail = "(no output)"
    }

    return [PSCustomObject]@{
        Stage = "$StageName-build"
        Success = $ok
        ExitCode = $exitCode
        DurationMs = $duration
        IsCrash = $false
        Summary = $tail
        Log = $BuildLog
        Err = $BuildErr
    }
}

function Invoke-Stage8Script {
    param(
        [string]$RepoRootPath,
        [string]$LogPath,
        [string]$ErrPath
    )

    $scriptPath = Join-Path $RepoRootPath "build\stage8_stress\run_stage8.ps1"
    if (!(Test-Path $scriptPath)) {
        return [PSCustomObject]@{
            Stage = "stage8"
            Success = $false
            ExitCode = -1
            DurationMs = 0
            IsCrash = $false
            Summary = "script not found: $scriptPath"
            Log = $LogPath
            Err = $ErrPath
        }
    }

    $start = Get-Date
    Remove-Item $LogPath, $ErrPath -ErrorAction SilentlyContinue

    $args = @(
        "-ExecutionPolicy", "Bypass",
        "-File", $scriptPath,
        "-Protocol", "udp",
        "-DurationSec", "20",
        "-Topics", "8",
        "-PublisherThreads", "4",
        "-SubscribersPerTopic", "4",
        "-RatePerThread", "150"
    )

    Push-Location $RepoRootPath
    $nativeExit = -1
    try {
        & powershell @args 1> $LogPath 2> $ErrPath
        $nativeExit = $LASTEXITCODE
    } catch {
        $errorText = $_ | Out-String
        Add-Content -Path $ErrPath -Value $errorText
        if ($null -ne $LASTEXITCODE) {
            $nativeExit = [int]$LASTEXITCODE
        }
    } finally {
        Pop-Location
    }

    $end = Get-Date
    $duration = [int][Math]::Round(($end - $start).TotalMilliseconds)
    $exitCode = if ($null -eq $nativeExit) { 0 } else { [int]$nativeExit }
    $ok = ($exitCode -eq 0)
    $isCrash = ($exitCode -lt 0)

    $tail = ""
    if (Test-Path $LogPath) {
        $tail = (Get-Content $LogPath -Tail 5 | ForEach-Object { $_.Trim() }) -join " | "
    }
    if ([string]::IsNullOrWhiteSpace($tail) -and (Test-Path $ErrPath)) {
        $tail = (Get-Content $ErrPath -Tail 5 | ForEach-Object { $_.Trim() }) -join " | "
    }
    if (-not $ok -and (Test-Path $ErrPath)) {
        $errTail = (Get-Content $ErrPath -Tail 5 | ForEach-Object { $_.Trim() }) -join " | "
        if (-not [string]::IsNullOrWhiteSpace($errTail)) {
            if ([string]::IsNullOrWhiteSpace($tail) -or $tail -eq "(no output)") {
                $tail = $errTail
            } elseif (-not $tail.Contains($errTail)) {
                $tail = "$tail | err=$errTail"
            }
        }
    }
    if ([string]::IsNullOrWhiteSpace($tail)) {
        $tail = "(no output)"
    }

    return [PSCustomObject]@{
        Stage = "stage8"
        Success = $ok
        ExitCode = $exitCode
        DurationMs = $duration
        IsCrash = $isCrash
        Summary = $tail
        Log = $LogPath
        Err = $ErrPath
    }
}

function Format-StageLine {
    param([object]$Result)

    $status = if ($Result.Success) { "PASS" } else { "FAIL" }
    $crashFlag = if ($Result.IsCrash) { " crash=true" } else { "" }
    $seconds = "{0:N2}" -f ($Result.DurationMs / 1000.0)
    return "[stage-run] {0} {1} duration={2}s exit={3}{4} summary={5}" -f `
        $status, $Result.Stage, $seconds, $Result.ExitCode, $crashFlag, $Result.Summary
}

$repoRoot = Resolve-RepoRoot
$ctx = New-RunContext -RepoRootPath $repoRoot

$env:PATH = "$($ctx.BinPath);$env:PATH"

$tests = @(
    [PSCustomObject]@{
        Stage = "stage3"
        Exe = (Join-Path $repoRoot "build\stage3_smoke\out\$Config\stage3_smoke.exe")
        BuildDir = (Join-Path $repoRoot "build\stage3_smoke\out")
    },
    [PSCustomObject]@{
        Stage = "stage4"
        Exe = (Join-Path $repoRoot "build\stage4_smoke\out\$Config\stage4_smoke.exe")
        BuildDir = (Join-Path $repoRoot "build\stage4_smoke\out")
    },
    [PSCustomObject]@{
        Stage = "stage7"
        Exe = (Join-Path $repoRoot "build\stage7_smoke\out\$Config\stage7_smoke.exe")
        BuildDir = (Join-Path $repoRoot "build\stage7_smoke\out")
    },
    [PSCustomObject]@{
        Stage = "stage56"
        Exe = (Join-Path $repoRoot "build\stage56_smoke\out\$Config\stage56_smoke.exe")
        BuildDir = (Join-Path $repoRoot "build\stage56_smoke\out")
    }
)

$results = @()
$aborted = $false

foreach ($t in $tests) {
    if (-not $SkipBuild.IsPresent) {
        $buildLog = Join-Path $ctx.LogsDir "$($t.Stage).build.out.log"
        $buildErr = Join-Path $ctx.LogsDir "$($t.Stage).build.err.log"
        $buildResult = Invoke-StageBuild `
            -StageName $t.Stage `
            -BuildDir $t.BuildDir `
            -BuildLog $buildLog `
            -BuildErr $buildErr
        $results += $buildResult
        Write-Host (Format-StageLine -Result $buildResult)
        if (-not $buildResult.Success) {
            if ($StopOnCrash.IsPresent) {
                Write-Host "[stage-run] stop-on-crash active, aborting after build failure"
                $aborted = $true
                break
            }
            continue
        }
    }

    $logPath = Join-Path $ctx.LogsDir "$($t.Stage).out.log"
    $errPath = Join-Path $ctx.LogsDir "$($t.Stage).err.log"
    $result = Invoke-StageExecutable `
        -StageName $t.Stage `
        -ExePath $t.Exe `
        -Arguments @() `
        -LogPath $logPath `
        -ErrPath $errPath
    $results += $result
    Write-Host (Format-StageLine -Result $result)
    if ($result.IsCrash -and $StopOnCrash.IsPresent) {
        Write-Host "[stage-run] stop-on-crash active, aborting after crash in $($t.Stage)"
        $aborted = $true
        break
    }
}

if (-not $SkipStage8.IsPresent -and -not $aborted) {
    if (-not $SkipBuild.IsPresent) {
        $stage8BuildLog = Join-Path $ctx.LogsDir "stage8.build.out.log"
        $stage8BuildErr = Join-Path $ctx.LogsDir "stage8.build.err.log"
        $stage8BuildResult = Invoke-StageBuild `
            -StageName "stage8" `
            -BuildDir (Join-Path $repoRoot "build\stage8_stress\out") `
            -BuildLog $stage8BuildLog `
            -BuildErr $stage8BuildErr
        $results += $stage8BuildResult
        Write-Host (Format-StageLine -Result $stage8BuildResult)
        if (-not $stage8BuildResult.Success) {
            if ($StopOnCrash.IsPresent) {
                Write-Host "[stage-run] stop-on-crash active, aborting after stage8 build failure"
                $aborted = $true
            }
        } else {
            $stage8Out = Join-Path $ctx.LogsDir "stage8.out.log"
            $stage8Err = Join-Path $ctx.LogsDir "stage8.err.log"
            $stage8Result = Invoke-Stage8Script -RepoRootPath $repoRoot -LogPath $stage8Out -ErrPath $stage8Err
            $results += $stage8Result
            Write-Host (Format-StageLine -Result $stage8Result)
            if ($stage8Result.IsCrash -and $StopOnCrash.IsPresent) {
                $aborted = $true
            }
        }
    } else {
        $stage8Out = Join-Path $ctx.LogsDir "stage8.out.log"
        $stage8Err = Join-Path $ctx.LogsDir "stage8.err.log"
        $stage8Result = Invoke-Stage8Script -RepoRootPath $repoRoot -LogPath $stage8Out -ErrPath $stage8Err
        $results += $stage8Result
        Write-Host (Format-StageLine -Result $stage8Result)
        if ($stage8Result.IsCrash -and $StopOnCrash.IsPresent) {
            $aborted = $true
        }
    }
}

$failed = @($results | Where-Object { -not $_.Success })

Write-Host "[stage-run] -------- summary --------"
foreach ($r in $results) {
    Write-Host (Format-StageLine -Result $r)
}
Write-Host "[stage-run] logs: $($ctx.LogsDir)"

if ($failed.Count -gt 0) {
    Write-Host "[stage-run] FAIL total=$($failed.Count)"
    exit 1
}

Write-Host "[stage-run] PASS all stages"
exit 0
