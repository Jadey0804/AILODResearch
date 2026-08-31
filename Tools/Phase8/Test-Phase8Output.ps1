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

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedGitCommit
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Phase8 {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

. (Join-Path $PSScriptRoot 'Get-Phase8RunDefinition.ps1')
$definition = Get-Phase8RunDefinition -RunSet $RunSet
$outputRoot = [IO.Path]::GetFullPath((Join-Path $OutputBase $definition.RelativeOutputPath))
$schedulePath = Join-Path $outputRoot 'run_schedule.csv'
$summaryPath = Join-Path $outputRoot 'metrics_summary.csv'
$runsRoot = Join-Path $outputRoot 'Runs'
$failurePath = Join-Path $outputRoot 'run_failures.csv'

Assert-Phase8 (Test-Path -LiteralPath $schedulePath -PathType Leaf) "Missing run_schedule.csv: $schedulePath"
Assert-Phase8 (Test-Path -LiteralPath $summaryPath -PathType Leaf) "Missing metrics_summary.csv: $summaryPath"
Assert-Phase8 (Test-Path -LiteralPath $runsRoot -PathType Container) "Missing Runs directory: $runsRoot"
Assert-Phase8 (-not (Test-Path -LiteralPath $failurePath -PathType Leaf)) "run_failures.csv exists: $failurePath"

$expectedRunIDs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($repeatIndex in 1..$definition.RepeatCount) {
    foreach ($seed in $definition.Seeds) {
        foreach ($method in $definition.Methods) {
            foreach ($scenario in $definition.Scenarios) {
                $baseID = "${method}-${scenario}-${seed}"
                $runID = if ($definition.RepeatCount -gt 1) { "${baseID}-R$($repeatIndex.ToString('00'))" } else { $baseID }
                Assert-Phase8 ($expectedRunIDs.Add($runID)) "Duplicate expected RunID: $runID"
            }
        }
    }
}
$expectedCount = $expectedRunIDs.Count

$schedule = @(Import-Csv -LiteralPath $schedulePath)
Assert-Phase8 ($schedule.Count -eq $expectedCount) "Schedule count $($schedule.Count) does not equal expected $expectedCount."
$scheduleByRunID = @{}
foreach ($row in $schedule) {
    Assert-Phase8 ($row.experiment_id -eq $definition.ExperimentID) "Schedule experiment ID mismatch for $($row.run_id)."
    Assert-Phase8 ($expectedRunIDs.Contains($row.run_id)) "Unexpected schedule RunID: $($row.run_id)"
    Assert-Phase8 (-not $scheduleByRunID.ContainsKey($row.run_id)) "Duplicate schedule RunID: $($row.run_id)"
    Assert-Phase8 ([int]$row.order_seed -eq $definition.OrderSeed) "Schedule order seed mismatch for $($row.run_id)."
    Assert-Phase8 ($row.randomized -eq 'true') "Schedule is not randomized for $($row.run_id)."
    $scheduleByRunID[$row.run_id] = $row
}

$runDirectories = @(Get-ChildItem -LiteralPath $runsRoot -Directory)
Assert-Phase8 ($runDirectories.Count -eq $expectedCount) "Run directory count $($runDirectories.Count) does not equal expected $expectedCount."
$hardware = $null
$inputHashesBySeed = @{}
$manifestDigests = @{}
$accuracyFiles = @(
    'kingdom_timeseries.csv',
    'cohort_timeseries.csv',
    'npc_snapshots.csv',
    'simulation_events.jsonl',
    'lod_transitions.jsonl',
    'ledger_transactions.jsonl')

foreach ($directory in $runDirectories) {
    $runID = $directory.Name
    Assert-Phase8 ($expectedRunIDs.Contains($runID)) "Unexpected run directory: $runID"
    $manifestPath = Join-Path $directory.FullName 'run_manifest.json'
    Assert-Phase8 (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Missing manifest: $manifestPath"
    $manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
    $scheduleRow = $scheduleByRunID[$runID]

    Assert-Phase8 ($manifest.experiment_id -eq $definition.ExperimentID) "Manifest experiment ID mismatch: $runID"
    Assert-Phase8 ($manifest.run_id -eq $runID) "Manifest RunID mismatch: $runID"
    Assert-Phase8 ($manifest.method -eq $scheduleRow.method) "Manifest method mismatch: $runID"
    Assert-Phase8 ($manifest.scenario -eq $scheduleRow.scenario) "Manifest scenario mismatch: $runID"
    Assert-Phase8 ([int]$manifest.seed -eq [int]$scheduleRow.seed) "Manifest seed mismatch: $runID"
    Assert-Phase8 ($manifest.git_commit -eq $ExpectedGitCommit) "Git commit mismatch: $runID"
    Assert-Phase8 ($manifest.build_type -eq 'Shipping') "Build type is not Shipping: $runID"
    Assert-Phase8 ($manifest.log_mode -eq $definition.LogMode) "Log mode mismatch: $runID"
    Assert-Phase8 ([bool]$manifest.valid) "Manifest valid=false: $runID"
    Assert-Phase8 ([bool]$manifest.formal_model_eligible) "Model is not formally eligible: $runID"
    Assert-Phase8 ([bool]$manifest.formal_run_requested) "Formal run was not requested: $runID"
    Assert-Phase8 ([bool]$manifest.formal_environment_eligible) "Formal environment is not eligible: $runID"
    Assert-Phase8 ([bool]$manifest.valid_for_formal_experiment) "Run is not valid for formal experiment: $runID"
    Assert-Phase8 ($manifest.formal_eligibility_reason -eq 'eligible') "Formal eligibility reason is not eligible: $runID"
    Assert-Phase8 ([int]$manifest.schedule_index -eq [int]$scheduleRow.schedule_index) "Schedule index mismatch: $runID"
    Assert-Phase8 ([int]$manifest.repeat_index -eq [int]$scheduleRow.repeat_index) "Repeat index mismatch: $runID"
    Assert-Phase8 ([int]$manifest.order_seed -eq $definition.OrderSeed) "Manifest order seed mismatch: $runID"
    Assert-Phase8 ([bool]$manifest.run_order_randomized) "Manifest says order was not randomized: $runID"
    Assert-Phase8 ([int]$manifest.parameters.population_per_kingdom -eq $definition.PopulationPerKingdom) "Population mismatch: $runID"
    Assert-Phase8 ($manifest.parameters.run_mode -eq $definition.Mode) "Run mode mismatch: $runID"
    Assert-Phase8 (-not [string]::IsNullOrWhiteSpace([string]$manifest.deterministic_digest)) "Missing deterministic digest: $runID"

    if ($manifest.method -eq 'Proposed') {
        Assert-Phase8 ($manifest.spec_version -eq '1.9') "Proposed spec version is not 1.9: $runID"
        Assert-Phase8 ($manifest.deterministic_digest_version -eq '1.9-domain-v1') "Proposed digest version mismatch: $runID"
    }
    foreach ($property in $manifest.hard_errors.PSObject.Properties) {
        Assert-Phase8 ([double]$property.Value -eq 0.0) "Hard error $($property.Name)=$($property.Value) in $runID"
    }

    if ($null -eq $hardware) {
        $hardware = [string]$manifest.hardware
    }
    Assert-Phase8 (-not [string]::IsNullOrWhiteSpace([string]$manifest.hardware)) "Hardware metadata is empty: $runID"
    Assert-Phase8 ([string]$manifest.hardware -eq $hardware) "Hardware metadata changed within the run set: $runID"

    $hashKey = [string]$manifest.seed
    Assert-Phase8 (-not [string]::IsNullOrWhiteSpace([string]$manifest.population_manifest_sha256)) "Population hash is empty: $runID"
    Assert-Phase8 (-not [string]::IsNullOrWhiteSpace([string]$manifest.damage_list_sha256)) "Damage hash is empty: $runID"
    Assert-Phase8 (-not [string]::IsNullOrWhiteSpace([string]$manifest.persistent_pool_sha256)) "Persistent-pool hash is empty: $runID"
    $hashValue = "$($manifest.population_manifest_sha256)|$($manifest.damage_list_sha256)|$($manifest.persistent_pool_sha256)"
    if ($inputHashesBySeed.ContainsKey($hashKey)) {
        Assert-Phase8 ($inputHashesBySeed[$hashKey] -eq $hashValue) "Input hashes changed for seed $hashKey."
    }
    else {
        $inputHashesBySeed[$hashKey] = $hashValue
    }
    $manifestDigests[$runID] = [string]$manifest.deterministic_digest

    if ($definition.Mode -eq 'Performance') {
        $performancePath = Join-Path $directory.FullName 'performance_1s.csv'
        Assert-Phase8 (Test-Path -LiteralPath $performancePath -PathType Leaf) "Missing performance_1s.csv: $runID"
        Assert-Phase8 (@(Import-Csv -LiteralPath $performancePath).Count -gt 0) "No performance samples: $runID"
    }
    else {
        foreach ($file in $accuracyFiles) {
            Assert-Phase8 (Test-Path -LiteralPath (Join-Path $directory.FullName $file) -PathType Leaf) "Missing $file in $runID"
        }
    }
}

$summary = @(Import-Csv -LiteralPath $summaryPath)
$summaryRunIDs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($row in $summary) {
    [void]$summaryRunIDs.Add($row.run_id)
}
foreach ($runID in $expectedRunIDs) {
    Assert-Phase8 ($summaryRunIDs.Contains($runID)) "Summary has no rows for $runID"
}

if ($definition.Mode -eq 'Performance') {
    $speedRows = @($summary | Where-Object { $_.metric_name -eq 'Performance.SpeedupVsPerAgent.TotalAI' })
    Assert-Phase8 ($speedRows.Count -eq $expectedCount) "Total-AI speed row count $($speedRows.Count) does not equal expected $expectedCount."
    foreach ($row in $speedRows) {
        $scheduleRow = $scheduleByRunID[$row.run_id]
        $expectedBaseline = "PerAgent-$($scheduleRow.scenario)-$($scheduleRow.seed)-R$(([int]$scheduleRow.repeat_index).ToString('00'))"
        Assert-Phase8 ($row.scale -eq "baseline=$expectedBaseline;full_67_game_day_production_cpu_ms") "Repeat-aware PerAgent pairing mismatch for $($row.run_id)."
    }
}
else {
    $policyRuns = @($schedule | Where-Object { $_.scenario -ne 'None' })
    foreach ($policyRun in $policyRuns) {
        Assert-Phase8 (@($summary | Where-Object { $_.run_id -eq $policyRun.run_id -and $_.metric_name -like 'PolicyEffect.*' }).Count -gt 0) "Missing policy metrics for $($policyRun.run_id)."
    }
}

$auditPath = Join-Path $outputRoot 'phase8_audit.json'
$audit = [ordered]@{
    protocol_version = '1.0'
    timestamp_utc = [DateTime]::UtcNow.ToString('o')
    success = $true
    excluded = [bool]$definition.Excluded
    run_set = $RunSet
    experiment_id = $definition.ExperimentID
    expected_run_count = $expectedCount
    actual_run_count = $runDirectories.Count
    git_commit = $ExpectedGitCommit
    build_type = 'Shipping'
    hardware = $hardware
    order_seed = $definition.OrderSeed
    schedule_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $schedulePath).Hash
    metrics_summary_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $summaryPath).Hash
    manifest_digests = $manifestDigests
}
$audit | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $auditPath -Encoding utf8
Write-Host "Phase 8 audit passed: $RunSet ($expectedCount/$expectedCount)"
Write-Host "Audit: $auditPath"
