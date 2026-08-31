[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('StaticNormal', 'FastTraversal', 'DenseProxies', 'ActorCap50', 'WorldPartitionTravel', 'TelescopeLift')]
    [string]$Scenario,

    [ValidateSet(2000, 20000, 100000)]
    [int]$Population = 2000,

    [ValidateSet(1, 4)]
    [int]$TimeScale = 1,

    [ValidateRange(0, 300)]
    [double]$WarmupSeconds = 15,

    [ValidateRange(1, 600)]
    [double]$CaptureSeconds = 30,

    [bool]$LabelsOn = $true,

    [ValidateRange(1, 44)]
    [int]$NormalActiveActorBudget = 35,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$RunSet = 'adhoc',

    [switch]$RenderOffscreen,

    [string]$UnrealRoot = 'D:\ruanjian\Unreal Engine\UE_5.4'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$projectFile = Join-Path $projectRoot 'AILODResearch.uproject'
$editorFile = Join-Path $UnrealRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path -LiteralPath $editorFile)) {
    throw "UnrealEditor.exe was not found at: $editorFile"
}

$populationPerKingdom = [int]($Population / 2)
$labelsValue = if ($LabelsOn) { 'True' } else { 'False' }
$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$captureStem = "Phase7F_E_${RunSet}_${Population}_${Scenario}_${TimeScale}x_${timestamp}"
$captureFile = "$captureStem.csv"
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$manifestDirectory = Join-Path $projectRoot 'Saved\Profiling\Phase7F\Manifests'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $manifestDirectory | Out-Null
$logFile = Join-Path $logDirectory "$captureStem.log"
$manifestFile = Join-Path $manifestDirectory "$captureStem.json"

$gitHead = (& git -C $projectRoot rev-parse HEAD).Trim()
& git -C $projectRoot diff --quiet
$trackedWorktreeDirty = $LASTEXITCODE -ne 0
& git -C $projectRoot diff --cached --quiet
$trackedIndexDirty = $LASTEXITCODE -ne 0

$consoleCommands = @(
    'r.VSync 0'
    't.MaxFPS 0'
    'r.ScreenPercentage 100'
    'sg.ResolutionQuality 100'
    'sg.ViewDistanceQuality 3'
    'sg.AntiAliasingQuality 3'
    'sg.ShadowQuality 3'
    'sg.GlobalIlluminationQuality 3'
    'sg.ReflectionQuality 3'
    'sg.PostProcessQuality 3'
    'sg.TextureQuality 3'
    'sg.EffectsQuality 3'
    'sg.FoliageQuality 3'
    'sg.ShadingQuality 3'
) -join ','

$arguments = @(
    $projectFile
    '/Game/Phase7/Maps/L_Phase7_VisualDemo'
    '-game'
    '-d3d12'
    '-windowed'
    '-ResX=1920'
    '-ResY=1080'
    '-ForceRes'
    '-NoVSync'
    '-NoSplash'
    "-ExecCmds=$consoleCommands"
    "-ini:Game:[/Script/AILODResearch.AILODVisualDemoSettings]:PopulationPerKingdom=$populationPerKingdom"
    "-ini:Game:[/Script/AILODResearch.AILODVisualDemoSettings]:bShowResidentDebugLabels=$labelsValue"
    "-ini:Game:[/Script/AILODResearch.AILODVisualDemoSettings]:NormalActiveActorBudget=$NormalActiveActorBudget"
    "-AILODPerfScenario=$Scenario"
    "-AILODPerfTimeScale=$TimeScale"
    "-AILODPerfWarmupSeconds=$WarmupSeconds"
    "-AILODPerfCaptureSeconds=$CaptureSeconds"
    "-AILODPerfCaptureName=$captureFile"
    '-csvGpuStats'
    '-csvStatCounts'
    '-csvCategories=AILODVisual'
    "-csvMetadata=gitcommit=$gitHead,trackedworktreedirty=$([int]$trackedWorktreeDirty),trackedindexdirty=$([int]$trackedIndexDirty),resolution=1920x1080,rhi=DX12,quality=Epic"
    '-ExitAfterCsvProfiling'
    "-abslog=$logFile"
)
if ($RenderOffscreen) {
    $arguments += '-RenderOffscreen'
    $arguments += '-unattended'
}

$manifest = [ordered]@{
    phase = '7F-E'
    run_set = $RunSet
    timestamp_local = (Get-Date).ToString('o')
    git_commit = $gitHead
    tracked_worktree_dirty = $trackedWorktreeDirty
    tracked_index_dirty = $trackedIndexDirty
    population = $Population
    population_per_kingdom = $populationPerKingdom
    scenario = $Scenario
    time_scale = $TimeScale
    warmup_seconds = $WarmupSeconds
    capture_seconds = $CaptureSeconds
    labels_on = $LabelsOn
    normal_active_actor_budget = $NormalActiveActorBudget
    resolution = '1920x1080'
    window_mode = 'windowed'
    rhi = 'DX12'
    quality = 'Epic'
    screen_percentage = 100
    vsync = $false
    frame_cap = 0
    render_offscreen = [bool]$RenderOffscreen
    csv_file = $captureFile
    log_file = $logFile
    arguments = $arguments
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestFile -Encoding utf8

Write-Host "Starting Phase 7F-E capture: $Population / $Scenario / ${TimeScale}x"
Write-Host "Warmup ${WarmupSeconds}s, then record ${CaptureSeconds}s. The game exits after the CSV is written."
Write-Host "Manifest: $manifestFile"

$launchArguments = $arguments | ForEach-Object {
    if ($_ -match '\s') {
        '"' + $_.Replace('"', '\"') + '"'
    }
    else {
        $_
    }
}
$process = Start-Process -FilePath $editorFile -ArgumentList $launchArguments -PassThru -Wait
if ($process.ExitCode -ne 0) {
    throw "UnrealEditor exited with code $($process.ExitCode). See: $logFile"
}

$expectedCsv = Join-Path $projectRoot "Saved\Profiling\CSV\$captureFile"
if (-not (Test-Path -LiteralPath $expectedCsv)) {
    throw "The game exited without producing the expected CSV: $expectedCsv"
}
Write-Host "CSV: $expectedCsv"
Write-Host "Log: $logFile"
