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
