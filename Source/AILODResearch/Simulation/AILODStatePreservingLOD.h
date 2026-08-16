// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODIndividualSimulation.h"

namespace AILOD
{
	inline constexpr int32 ActiveMicroCap = 50;

	enum class ELODTransitionResult : uint8
	{
		Committed,
		AlreadyInState,
		ResidentNotFound,
		ActiveCapReached,
		EventOwnerConflict,
		CohortCacheMismatch
	};

	struct FLODTransitionRecord
	{
		FPersistentID PersistentID = 0;
		EResidentRepresentation From = EResidentRepresentation::CohortManaged;
		EResidentRepresentation To = EResidentRepresentation::CohortManaged;
		FSimulationTime RequestedTime;
		FSimulationTime CommittedTime;
		FArriveID ArriveID = 0;
		FString Bucket;
		ELODTransitionResult Result = ELODTransitionResult::ResidentNotFound;
	};

	struct FUnifiedActionRequest
	{
		FResidentID ResidentID = 0;
		EIndividualAction Action = EIndividualAction::None;
		int32 Quantity = 1;
		EResidentRepresentation Representation = EResidentRepresentation::CohortManaged;
		uint64 OrderKey = 0;
		FArriveID ArriveID = 0;
		bool bWon = false;
	};

	struct FMacroDecisionGroup
	{
		EIndividualGoal Goal = EIndividualGoal::RoutineLife;
		EIndividualAction Action = EIndividualAction::None;
		TArray<FResidentID> ResidentIDs;
	};

	struct FMacroDecisionBatch
	{
		int32 ResidentCount = 0;
		int32 PlanningEvaluationCount = 0;
		TArray<FMacroDecisionGroup> Groups;
	};

	struct FCompetitionRecord
	{
		FSimulationTime Time;
		int32 AvailableQuantity = 0;
		TArray<FUnifiedActionRequest> Requests;
	};

	enum class EActionRequestState : uint8
	{
		Pending,
		Won,
		Committed
	};

	struct FActionRequestKey
	{
		FResidentID ResidentID = 0;
		EIndividualAction Action = EIndividualAction::None;

		bool operator==(const FActionRequestKey& Other) const
		{
			return ResidentID == Other.ResidentID && Action == Other.Action;
		}

		friend uint32 GetTypeHash(const FActionRequestKey& Key)
		{
			return HashCombine(
				::GetTypeHash(Key.ResidentID),
				::GetTypeHash(static_cast<uint8>(Key.Action)));
		}
	};

	struct FIssuedActionRequest
	{
		FActionRequestKey Key;
		EActionRequestState State = EActionRequestState::Pending;
		FSimulationTime LastResolvedTime;
	};

	struct FPhase4Audit
	{
		FConservationAudit Conservation;
		double CoinResidual = 0.0;
		int32 CoreLedgerMismatchCount = 0;
		int32 IdentityMismatchCount = 0;
		int32 EventReferenceMismatchCount = 0;
		int32 CohortMismatchCount = 0;
		int32 ActiveCapViolationCount = 0;

		bool IsHardErrorFree() const;
	};

	class FStatePreservingLODSystem
	{
	public:
		bool Initialize(const FPhase0Config& Config, FString& OutError);
		bool ApplyEarthquakeDamage(FSimulationTime Time, FString& OutError);
		bool Activate(FResidentID ResidentID, FSimulationTime Time, FString& OutError);
		bool Deactivate(FResidentID ResidentID, FSimulationTime Time, FString& OutError);
		bool StartRepair(
			FResidentID ResidentID,
			FSimulationTime Time,
			FString& OutError,
			FArriveID ExistingArriveID);
		bool AdvanceTo(FSimulationTime Time, FString& OutError);

#if WITH_DEV_AUTOMATION_TESTS
		bool SeedResidentWoodForTest(
			FResidentID ResidentID,
			int32 Quantity,
			FSimulationTime Time,
			FString& OutError);
#endif

		bool BuildMacroDecisionBatch(
			const FIndividualWorldFacts& KingdomAWorld,
			const FIndividualWorldFacts& KingdomBWorld,
			FMacroDecisionBatch& OutBatch,
			FString& OutError) const;

		bool ResolveCompetition(
			FSimulationTime Time,
			int32 AvailableQuantity,
			TArray<FUnifiedActionRequest>& InOutRequests,
			FString& OutError);

		const FResidentCoreState* FindResident(FResidentID ResidentID) const;
		const FPersistentTestPool& GetContinuitySample() const { return ContinuitySample; }
		const TMap<FCohortKey, FCohortBucket>& GetCohorts() const { return Cohorts; }
		const FResourceLedger& GetLedger() const { return Ledger; }
		const FSimulationEventStore& GetEventStore() const { return EventStore; }
		const TArray<FLODTransitionRecord>& GetTransitions() const { return Transitions; }
		const TArray<FCompetitionRecord>& GetCompetitionHistory() const { return CompetitionHistory; }
		FPopulationState BuildPopulationState() const;
		FPhase4Audit Audit() const;
		int32 GetActiveMicroCount() const;
		int64 GetRemainingWorkMinutes(FResidentID ResidentID, FSimulationTime Time) const;
		FString BuildDeterministicDigest() const;

	private:
		static FString ResidentAccount(FResidentID ResidentID, const TCHAR* StockName);
		static FString RepresentationOwner(EResidentRepresentation Representation, FResidentID ResidentID);
		static FCohortKey MakeCohortKey(const FResidentCoreState& Resident);
		static FString MakeCohortLabel(const FCohortKey& Key);

		FResidentCoreState* FindMutableResident(FResidentID ResidentID);
		bool InitializeLedger(FString& OutError);
		bool SynchronizeTime(FSimulationTime Time, FString& OutError);
		void SyncResidentResourceView(FResidentCoreState& Resident);
		void EnsureRepairCapacityDay(FSimulationTime Time);
		void AddToCohort(const FResidentCoreState& Resident, FSimulationTime Time);
		bool RemoveFromCohort(const FResidentCoreState& Resident, FSimulationTime Time, FString& OutError);
		TMap<FCohortKey, FCohortBucket> RebuildCohorts() const;
		bool Transition(
			FResidentID ResidentID,
			EResidentRepresentation Target,
			FSimulationTime Time,
			FString& OutError);

		FPhase0Config Config;
		FInitialPopulationManifest PopulationManifest;
		FEarthquakeDamageList DamageList;
		FPersistentTestPool ContinuitySample;
		TArray<FResidentCoreState> Residents;
		TMap<FResidentID, int32> ResidentIndices;
		TMap<FCohortKey, FCohortBucket> Cohorts;
		FSimulationTime CohortTime;
		FSimulationClock Clock;
		FSimulationScheduler Scheduler;
		FResourceLedger Ledger;
		FReservationStore Reservations;
		FSimulationEventStore EventStore;
		TArray<FLODTransitionRecord> Transitions;
		TArray<FCompetitionRecord> CompetitionHistory;
		TMap<FArriveID, FIssuedActionRequest> IssuedActionRequests;
		TMap<FActionRequestKey, FArriveID> ActiveActionRequestIDs;
		int32 RepairCapacityDay = TNumericLimits<int32>::Min();
		int32 RepairStartsRemaining = 0;
	};
}
