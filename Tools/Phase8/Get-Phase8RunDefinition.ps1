function Get-Phase8RunDefinition {
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
        [string]$RunSet
    )

    $formalAccuracySeeds = [int[]](20260815..20260844)
    $commonPerformance = @{
        Mode = 'Performance'
        Methods = @('Proposed', 'Simple', 'PerAgent')
        Scenarios = @('StateImport')
        Seeds = @([int]20260815)
        RepeatCount = 10
        Excluded = $false
    }

    switch ($RunSet) {
        'PreflightAccuracy' {
            return [pscustomobject]@{
                RunSet = $RunSet
                Mode = 'Accuracy'
                TotalPopulation = 200
                PopulationPerKingdom = 100
                Methods = @('Oracle', 'Proposed', 'Simple', 'PerAgent')
                Scenarios = @('None', 'StateImport')
                Seeds = @([int]20260810)
                RepeatCount = 1
                OrderSeed = 830801
                ExperimentID = 'PHASE8-PREFLIGHT-EXCLUDED-ACCURACY'
                RelativeOutputPath = 'PreflightExcluded\Accuracy'
                LogMode = 'Phase8PreflightExcludedAccuracy'
                Excluded = $true
            }
        }
        'PreflightPerformance' {
            return [pscustomobject]@{
                RunSet = $RunSet
                Mode = 'Performance'
                TotalPopulation = 2000
                PopulationPerKingdom = 1000
                Methods = @('Proposed', 'Simple', 'PerAgent')
                Scenarios = @('StateImport')
                Seeds = @([int]20260810)
                RepeatCount = 2
                OrderSeed = 830802
                ExperimentID = 'PHASE8-PREFLIGHT-EXCLUDED-PERFORMANCE'
                RelativeOutputPath = 'PreflightExcluded\Performance-2k'
                LogMode = 'Phase8PreflightExcludedPerformance'
                Excluded = $true
            }
        }
        'FormalAccuracy' {
            return [pscustomobject]@{
                RunSet = $RunSet
                Mode = 'Accuracy'
                TotalPopulation = 200
                PopulationPerKingdom = 100
                Methods = @('Oracle', 'Proposed', 'Simple', 'PerAgent')
                Scenarios = @('None', 'HarvestCap', 'StateImport', 'RepairAid')
                Seeds = $formalAccuracySeeds
                RepeatCount = 1
                OrderSeed = 830480
                ExperimentID = 'PHASE8-FORMAL-ACCURACY-V1'
                RelativeOutputPath = 'FormalAccuracy-v1'
                LogMode = 'Phase8FormalAccuracy'
                Excluded = $false
            }
        }
        'FormalPerformance2k' {
            return [pscustomobject]@{
                RunSet = $RunSet
                Mode = $commonPerformance.Mode
                TotalPopulation = 2000
                PopulationPerKingdom = 1000
                Methods = $commonPerformance.Methods
                Scenarios = $commonPerformance.Scenarios
                Seeds = $commonPerformance.Seeds
                RepeatCount = $commonPerformance.RepeatCount
                OrderSeed = 830002
                ExperimentID = 'PHASE8-FORMAL-PERFORMANCE-2K-V1'
                RelativeOutputPath = 'FormalPerformance-v1\N2000'
                LogMode = 'Phase8FormalPerformance2k'
                Excluded = $commonPerformance.Excluded
            }
        }
        'FormalPerformance10k' {
            return [pscustomobject]@{
                RunSet = $RunSet
                Mode = $commonPerformance.Mode
                TotalPopulation = 10000
                PopulationPerKingdom = 5000
                Methods = $commonPerformance.Methods
                Scenarios = $commonPerformance.Scenarios
                Seeds = $commonPerformance.Seeds
                RepeatCount = $commonPerformance.RepeatCount
                OrderSeed = 830010
                ExperimentID = 'PHASE8-FORMAL-PERFORMANCE-10K-V1'
                RelativeOutputPath = 'FormalPerformance-v1\N10000'
                LogMode = 'Phase8FormalPerformance10k'
                Excluded = $commonPerformance.Excluded
            }
        }
        'FormalPerformance20k' {
            return [pscustomobject]@{
                RunSet = $RunSet
                Mode = $commonPerformance.Mode
                TotalPopulation = 20000
                PopulationPerKingdom = 10000
                Methods = $commonPerformance.Methods
                Scenarios = $commonPerformance.Scenarios
                Seeds = $commonPerformance.Seeds
                RepeatCount = $commonPerformance.RepeatCount
                OrderSeed = 830020
                ExperimentID = 'PHASE8-FORMAL-PERFORMANCE-20K-V1'
                RelativeOutputPath = 'FormalPerformance-v1\N20000'
                LogMode = 'Phase8FormalPerformance20k'
                Excluded = $commonPerformance.Excluded
            }
        }
    }
}
