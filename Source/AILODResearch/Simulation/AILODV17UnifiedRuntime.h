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
		bool SubmitDemoObservationRequest(
			const FUnifiedDemoObservationRequest& Request,
			FString& OutError);
		bool ReplayDemoObservationRecord(
			const FUnifiedDemoObservationRecord& Record,
			FString& OutError);
		bool BuildDemoSnapshot(FUnifiedDemoSnapshot& OutSnapshot, FString& OutError) const;
		void CopyDemoObservationLog(TArray<FUnifiedDemoObservationRecord>& OutRecords) const
		{
			OutRecords = DemoObservationRecords;
		}
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
		bool ApplyEarthquake(FString& OutError);
		bool ApplyPolicies(FSimulationTime Time, FString& OutError);
		bool PlaceStateImportOrder(FString& OutError);
		bool FreezeRepairAidEligibility(FString& OutError);
		bool PayRepairAid(FString& OutError);
		bool ApplyEnvironment(FSimulationTime Time, FString& OutError);
		bool QueuePlannedFlows(FString& OutError);
		bool ApplyActivationTrace(FSimulationTime Time, FString& OutError);
		void ResolvePendingFirstActions();
		void RecordFirstAction(FResidentID ResidentID, EIndividualAction Action, bool bContinuedEvent);
		FIndividualWorldFacts BuildWorldFacts(EKingdom Kingdom) const;
		FKingdomStocks BuildKingdomStocks(EKingdom Kingdom) const;
		FKingdomSnapshot BuildKingdomSnapshot(FSimulationTime Time, EKingdom Kingdom) const;
		void BuildCohortObservations(FSimulationTime Time, TArray<FUnifiedCohortObservation>& OutObservations) const;
		void PublishObservations(FSimulationTime GameTime, FSimulationTime ProcessedTime);
		FString PolicyStateAt(FSimulationTime ProcessedTime) const;
		bool IsHarvestCapActive(EKingdom Kingdom, FSimulationTime Time) const;
		void EnsureHarvestDay(EKingdom Kingdom, FSimulationTime Time);
		void FillDiagnostics(FUnifiedRunResult& OutResult) const;
		void RecordDemoObservation(
			const FUnifiedDemoObservationRequest& Request,
			bool bCommitted,
			const FString& Message);

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
		FUnifiedRunDiagnostics Diagnostics;
		TArray<FUnifiedActivationObservation> ActivationObservations;
		TMap<FResidentID, int32> PendingFirstActions;
		TArray<FUnifiedDemoObservationRecord> DemoObservationRecords;
		FResidentID DemoTrackedResidentID = 0;
		int64 NextDemoObservationSequence = 1;
		bool bEarthquakeApplied = false;
		int64 ImportBudgetRemaining = 0;
		double WoodPrices[2] = { 1.0, 1.0 };
		int32 HarvestDays[2] = { TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min() };
		double HarvestAllowances[2] = { 0.0, 0.0 };
		int64 ResidentTouches = 0;
		int32 MaxActiveCount = 0;
	};
}
