// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODStatePreservingLOD.h"

namespace AILOD
{
	enum class EUnifiedSimulationMethod : uint8
	{
		Oracle,
		Proposed,
		PerAgent,
		Simple
	};

	const TCHAR* ToString(EUnifiedSimulationMethod Method);

	class IUnifiedSimulationObserver;
	class IUnifiedSimulationEventSink;

	enum class EUnifiedRunMode : uint8
	{
		Validation,
		Accuracy,
		Performance
	};

	enum class EUnifiedFaultInjectionPoint : uint8
	{
		None,
		BuyWoodPreflight,
		ChopWoodPreflight,
		StartRepairPreflight,
		StateImportPreflight
	};

	enum class EUnifiedCompetitionResource : uint8
	{
		None,
		MarketWood,
		ForestWood,
		RepairCapacity
	};

	struct FUnifiedCompetitionScope
	{
		EKingdom Kingdom = EKingdom::A;
		EUnifiedCompetitionResource Resource = EUnifiedCompetitionResource::None;
		int32 Window = 0;

		bool operator==(const FUnifiedCompetitionScope& Other) const
		{
			return Kingdom == Other.Kingdom
				&& Resource == Other.Resource
				&& Window == Other.Window;
		}
	};

	FORCEINLINE uint32 GetTypeHash(const FUnifiedCompetitionScope& Scope)
	{
		uint32 Hash = ::GetTypeHash(static_cast<uint8>(Scope.Kingdom));
		Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Scope.Resource)));
		return HashCombine(Hash, ::GetTypeHash(Scope.Window));
	}

	struct FUnifiedRunOptions
	{
		EUnifiedRunMode Mode = EUnifiedRunMode::Validation;
		bool bRetainCompletedEvents = true;
		bool bRecordSnapshots = true;
		bool bVerifyCohortApproximation = false;
		EUnifiedFaultInjectionPoint FaultInjection = EUnifiedFaultInjectionPoint::None;
		IUnifiedSimulationObserver* Observer = nullptr;
		IUnifiedSimulationEventSink* EventSink = nullptr;
	};

	struct FUnifiedRunDiagnostics
	{
		int64 PlanningEvaluationCount = 0;
		int64 ValidationPlanningEvaluationCount = 0;
		int64 CohortPlanningEvaluationCount = 0;
		int64 ActiveMicroPlanningEvaluationCount = 0;
		int64 FullPopulationScanCount = 0;
		int64 FullAuditCount = 0;
		int64 AuditResidentVisitCount = 0;
		int64 SnapshotResidentVisitCount = 0;
		int64 LedgerQueryCount = 0;
		int64 CandidateCount = 0;
		int64 CompetitionScopeCount = 0;
		int64 MixedRepresentationCompetitionCount = 0;
		int64 CohortDecisionDisagreementCount = 0;
		int64 CohortAllocationFallbackCount = 0;
		int64 RejectedActionResidueCount = 0;
		int64 FaultInjectionCount = 0;
		int64 EventCount = 0;
		int64 TransactionCount = 0;
		int32 MaxActiveMicro = 0;
		int32 ActivationRequestCount = 0;
		int32 Day14ActivationCount = 0;
		int32 Day14DeactivationCount = 0;
		int32 SimpleMicroReconstructionCount = 0;
		int32 SimpleMicroWritebackCount = 0;
		int32 FirstActionCount = 0;
	};

	struct FUnifiedActivationObservation
	{
		FResidentID ResidentID = 0;
		FSimulationTime ActivationTime;
		EIndividualAction FirstAction = EIndividualAction::None;
		bool bSimpleReconstructed = false;
		bool bContinuedCommittedEvent = false;
	};

	struct FUnifiedCohortObservation
	{
		FSimulationTime GameTime;
		FString CohortKey;
		int32 Count = 0;
		int64 CashSum = 0;
		int64 CashSquaredSum = 0;
		int64 RepairCreditSum = 0;
		int32 WoodCounts[5] = {};
		EMacroIntent MacroIntent = EMacroIntent::Routine;
	};

	struct FUnifiedNPCObservation
	{
		FSimulationTime GameTime;
		FResidentCoreState Resident;
		EIndividualAction FirstAction = EIndividualAction::None;
	};

	struct FUnifiedHourObservation
	{
		FSimulationTime GameTime;
		FKingdomSnapshot KingdomA;
		FKingdomSnapshot KingdomB;
		FString PolicyState;
		TArray<FUnifiedCohortObservation> Cohorts;
	};

	class IUnifiedSimulationObserver
	{
	public:
		virtual ~IUnifiedSimulationObserver() = default;
		virtual void OnHourCompleted(const FUnifiedHourObservation& Observation) = 0;
		virtual void OnNPCSnapshot(const FUnifiedNPCObservation& Observation) = 0;
	};

	class IUnifiedSimulationEventSink
	{
	public:
		virtual ~IUnifiedSimulationEventSink() = default;
		virtual void OnEventCommitted(const FSimulationEventRecord& Event) = 0;
		virtual void OnTransactionCommitted(const FLedgerTransaction& Transaction) = 0;
		virtual void OnLODTransitionCommitted(const FLODTransitionRecord& Transition) = 0;
		virtual void OnActivationObserved(const FUnifiedActivationObservation& Observation) = 0;
	};

	struct FUnifiedRunResult
	{
		EUnifiedSimulationMethod Method = EUnifiedSimulationMethod::Oracle;
		EStage2Scenario Scenario = EStage2Scenario::None;
		int32 Seed = 0;
		int32 PopulationPerKingdom = 0;
		EUnifiedRunMode Mode = EUnifiedRunMode::Validation;
		bool bRetainCompletedEvents = true;
		bool bRecordSnapshots = true;
		bool bVerifyCohortApproximation = false;
		EUnifiedFaultInjectionPoint FaultInjection = EUnifiedFaultInjectionPoint::None;
		FString ConfigHash;
		FSimulationTime FinalTime;
		int32 WarmupHourSteps = 0;
		int32 FormalHourSteps = 0;
		FKingdomStocks KingdomAStocks;
		FKingdomStocks KingdomBStocks;
		int32 KingdomAHomeStates[4] = {};
		int32 KingdomBHomeStates[4] = {};
		TArray<FResidentCoreState> Residents;
		FConservationAudit Audit;
		double CoinResidual = 0.0;
		int32 CoreLedgerMismatchCount = 0;
		int32 EventReferenceErrorCount = 0;
		int32 ActiveCapViolationCount = 0;
		int32 ReservationErrorCount = 0;
		int32 TaskResetCount = 0;
		int32 PendingEventsAtOrBeforeEnd = 0;
		int32 SimpleIndividualCoreStateCount = 0;
		FUnifiedRunDiagnostics Diagnostics;
		TArray<FLedgerTransaction> Transactions;
		TArray<FSimulationEventRecord> Events;
		TArray<FKingdomSnapshot> Snapshots;
		TArray<FLODTransitionRecord> LODTransitions;
		TArray<FUnifiedActivationObservation> ActivationObservations;

		bool IsHardErrorFree() const;
		int32 GetHomeStateCount(EKingdom Kingdom, EHomeState HomeState) const;
	};

	class FUnifiedSimulationSession
	{
	public:
		FUnifiedSimulationSession(
			const FPhase0Config& Config,
			EUnifiedSimulationMethod Method,
			EStage2Scenario Scenario,
			const FUnifiedRunOptions& Options);
		~FUnifiedSimulationSession();

		FUnifiedSimulationSession(const FUnifiedSimulationSession&) = delete;
		FUnifiedSimulationSession& operator=(const FUnifiedSimulationSession&) = delete;

		bool Initialize(FString& OutError);
		bool StepHour(FString& OutError);
		bool Finalize(FUnifiedRunResult& OutResult, FString& OutError);

		bool IsComplete() const;
		FSimulationTime GetCurrentTime() const;
		int32 GetCompletedHourSteps() const;

	private:
		class FImpl;
		TUniquePtr<FImpl> Impl;
	};

	class FUnifiedSimulationRunner
	{
	public:
		static bool Run(
			const FPhase0Config& Config,
			EUnifiedSimulationMethod Method,
			EStage2Scenario Scenario,
			const FUnifiedRunOptions& Options,
			FUnifiedRunResult& OutResult,
			FString& OutError);

		static FString BuildDeterministicDigest(const FUnifiedRunResult& Result);
	};
}
