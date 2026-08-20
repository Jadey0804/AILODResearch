// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODUnifiedSimulation.h"

namespace AILOD
{
	class FV17UnifiedRuntime
	{
	public:
		FV17UnifiedRuntime(
			const FPhase0Config& InConfig,
			EStage2Scenario InScenario,
			const FUnifiedRunOptions& InOptions);

		bool Initialize(FString& OutError);
		bool StepHour(FString& OutError);
		bool Finalize(FUnifiedRunResult& OutResult, FString& OutError);
		bool IsComplete() const;
		FSimulationTime GetCurrentTime() const;
		const FUnifiedStepMeasurement& GetLastStepMeasurement() const { return LastStepMeasurement; }

	private:
		bool BuildDay14ActivationSample(FString& OutError);
		bool BuildAuthorityInput(
			TArray<FV17AuthoritativeCellConfig>& OutCells,
			TArray<FV17IdentityRecord>& OutIdentities,
			TArray<FV17AuthoritativeKingdomConfig>& OutKingdoms,
			FString& OutError) const;
		bool QueueReadyFlows(FString& OutError);
		bool ApplyActivationTrace(FSimulationTime Time, FString& OutError);
		void FillDiagnostics(FUnifiedRunResult& OutResult) const;

		FPhase0Config Config;
		EStage2Scenario Scenario = EStage2Scenario::None;
		FUnifiedRunOptions Options;
		FInitialPopulationManifest PopulationManifest;
		FEarthquakeDamageList DamageList;
		FPersistentTestPool ContinuitySample;
		TArray<FResidentID> Day14ActivationResidents;
		TUniquePtr<FV17AuthoritativeMacroSession> Authority;
		FUnifiedStepMeasurement LastStepMeasurement;
		FUnifiedCostBreakdown CostBreakdown;
		int64 ResidentTouches = 0;
		int32 MaxActiveCount = 0;
	};
}
