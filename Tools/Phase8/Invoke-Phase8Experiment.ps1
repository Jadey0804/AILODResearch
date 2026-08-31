[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'PreflightAccuracy',
        'PreflightPerformance',
        'FormalAccuracy',
        'FormalPerformance2k',
        'FormalPerformance10k',
        'FormalPerformance20k')]
    [string]$RunSet,

    [string]$OutputBase = 'D:\AILODFormal\Phase8',

    [string]$ShippingExecutable,

    [switch]$Resume,

    [switch]$RebuildSummary
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Get-Phase8RunDefinition.ps1')
$definition = Get-Phase8RunDefinition -RunSet $RunSet
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($ShippingExecutable)) {
    $ShippingExecutable = Join-Path $projectRoot 'Binaries\Win64\AILODResearch-Win64-Shipping.exe'
}
if (-not (Test-Path -LiteralPath $ShippingExecutable -PathType Leaf)) {
    throw "Shipping executable was not found: $ShippingExecutable"
}

$targetFile = Join-Path $projectRoot 'Binaries\Win64\AILODResearch-Win64-Shipping.target'
if (-not (Test-Path -LiteralPath $targetFile -PathType Leaf)) {
    throw "Game target metadata was not found: $targetFile"
}
$target = Get-Content -Raw -Encoding UTF8 -LiteralPath $targetFile | ConvertFrom-Json
if ($target.Configuration -ne 'Shipping' -or $target.TargetType -ne 'Game') {
    throw "Phase 8 requires a Shipping Game build. Current target is $($target.Configuration) $($target.TargetType)."
}

$gitHead = (& git -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $gitHead -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Could not resolve the current full Git commit.'
}
& git -C $projectRoot diff --quiet
if ($LASTEXITCODE -ne 0) {
    throw 'Tracked working-tree changes exist. Commit or revert them before a Phase 8 run.'
}
& git -C $projectRoot diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    throw 'Staged changes exist. Commit or unstage them before a Phase 8 run.'
}

$outputRoot = [IO.Path]::GetFullPath((Join-Path $OutputBase $definition.RelativeOutputPath))
$outputBaseFull = [IO.Path]::GetFullPath($OutputBase).TrimEnd('\')
if (-not $outputRoot.StartsWith($outputBaseFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved output root escaped OutputBase: $outputRoot"
}

if (-not $definition.Excluded) {
    foreach ($requiredAudit in @(
        (Join-Path $OutputBase 'PreflightExcluded\Accuracy\phase8_audit.json'),
        (Join-Path $OutputBase 'PreflightExcluded\Performance-2k\phase8_audit.json'))) {
        if (-not (Test-Path -LiteralPath $requiredAudit -PathType Leaf)) {
            throw "Formal runs require both successful excluded preflights. Missing: $requiredAudit"
        }
        $audit = Get-Content -Raw -Encoding UTF8 -LiteralPath $requiredAudit | ConvertFrom-Json
        if (-not $audit.success -or -not $audit.excluded) {
            throw "Preflight audit is not a successful excluded run: $requiredAudit"
        }
        if ($audit.git_commit -ne $gitHead) {
            throw "Preflight audit came from a different Git commit. Rerun the excluded preflights: $requiredAudit"
        }
    }
}

if (-not $Resume -and (Test-Path -LiteralPath $outputRoot)) {
    $existing = Get-ChildItem -LiteralPath $outputRoot -Force -ErrorAction Stop | Select-Object -First 1
    if ($null -ne $existing) {
        throw "Output root already contains data. Use -Resume or choose a new approved root: $outputRoot"
    }
}
if ($RebuildSummary -and -not $Resume) {
    throw '-RebuildSummary requires -Resume.'
}
if ($RebuildSummary) {
    $summaryPath = Join-Path $outputRoot 'metrics_summary.csv'
    if (Test-Path -LiteralPath $summaryPath -PathType Leaf) {
        Remove-Item -LiteralPath $summaryPath -Force
    }
}

$logDirectory = Join-Path $OutputBase 'Logs'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$logPath = Join-Path $logDirectory "${RunSet}_${timestamp}.log"
$methods = $definition.Methods -join '+'
$scenarios = $definition.Scenarios -join '+'
$seeds = $definition.Seeds -join '+'
$arguments = @(
    '-run=AILODExperiment'
    "-OutputRoot=$outputRoot"
    "-ExperimentID=$($definition.ExperimentID)"
    "-Methods=$methods"
    "-Scenarios=$scenarios"
    "-Seeds=$seeds"
    "-GitCommit=$gitHead"
    "-Mode=$($definition.Mode)"
    "-PopulationPerKingdom=$($definition.PopulationPerKingdom)"
    "-RepeatCount=$($definition.RepeatCount)"
    "-OrderSeed=$($definition.OrderSeed)"
    "-LogMode=$($definition.LogMode)"
    '-Randomize'
    '-Formal'
    '-BuildSummary'
    '-NullRHI'
    '-Unattended'
    '-NoSound'
    '-NoSplash'
    '-NoP4'
    '-UTF8Output'
    '-stdout'
    '-FullStdOutLogOutput'
    "-abslog=$logPath"
)
if ($Resume) {
    $arguments += '-Resume'
}

$launchArguments = $arguments | ForEach-Object {
    if ($_ -match '\s') {
        '"' + $_.Replace('"', '\"') + '"'
    }
    else {
        $_
    }
}

Write-Host "Phase 8 run set: $RunSet"
Write-Host "Output: $outputRoot"
Write-Host "Git: $gitHead"
Write-Host "Expected runs: $($definition.Methods.Count * $definition.Scenarios.Count * $definition.Seeds.Count * $definition.RepeatCount)"
$process = Start-Process -FilePath $ShippingExecutable -ArgumentList $launchArguments -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    throw "Shipping experiment exited with code $($process.ExitCode). See: $logPath"
}

& (Join-Path $PSScriptRoot 'Test-Phase8Output.ps1') `
    -RunSet $RunSet `
    -OutputBase $OutputBase `
    -ExpectedGitCommit $gitHead

Write-Host "Phase 8 run and audit passed: $RunSet"
Write-Host "Log: $logPath"
