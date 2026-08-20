// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODUnifiedSimulation.h"

namespace AILOD
{
	struct FExperimentMatrixRequest
	{
		FString OutputRoot;
		FString ExperimentID;
		TArray<EUnifiedSimulationMethod> Methods;
		TArray<EStage2Scenario> Scenarios;
		TArray<int32> Seeds;
		int32 PopulationPerKingdom = 100;
		EUnifiedRunMode Mode = EUnifiedRunMode::Accuracy;
		bool bEnableMacroProfiling = false;
		bool bEnableV17ShadowCohort = false;
		EProposedModelVersion ProposedModelVersion = EProposedModelVersion::V16ExactCommit;
		FString GitCommit;
		FString UEVersion;
		FString BuildType;
		FString Hardware;
		FString LogMode = TEXT("EngineeringAccuracy");
		FString StartTime;
		FString EndTime;
	};

	struct FExperimentRunRecord
	{
		FString RunID;
		FString RunDirectory;
		FString DeterministicDigest;
		EUnifiedRunMode Mode = EUnifiedRunMode::Accuracy;
		int32 PopulationPerKingdom = 0;
		bool bHardErrorFree = false;
		int32 PerformanceSampleCount = 0;
		FUnifiedRunDiagnostics Diagnostics;
		FUnifiedCostBreakdown CostBreakdown;
		FUnifiedMacroProfile MacroProfile;
		FUnifiedV17ShadowProfile V17ShadowProfile;
	};

	class FExperimentRunner
	{
	public:
		static bool RunMatrix(
			const FExperimentMatrixRequest& Request,
			TArray<FExperimentRunRecord>& OutRuns,
			FString& OutError);

		static bool ReplayFromManifest(
			const FString& ManifestPath,
			const FString& OutputDirectory,
			FExperimentRunRecord& OutRun,
			FString& OutError);
	};
}
