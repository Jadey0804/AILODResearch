// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODIndividualSimulation.h"

namespace AILOD
{
	using FV17AuthoritativeClaimID = uint64;
	using FV17AuthoritativeCellID = uint64;

	struct FV17AuthoritativeJointKey
	{
		EKingdom Kingdom = EKingdom::A;
		EProfession Profession = EProfession::Worker;
		EIncomeBand IncomeBand = EIncomeBand::Low;
		EHomeState HomeState = EHomeState::Healthy;
		EMacroIntent Intent = EMacroIntent::Routine;
		int32 PurchasingPowerBand = 0;
		int32 WoodBand = 0;
		bool bAidEligible = false;

		bool operator==(const FV17AuthoritativeJointKey& Other) const
		{
			return Kingdom == Other.Kingdom
				&& Profession == Other.Profession
				&& IncomeBand == Other.IncomeBand
				&& HomeState == Other.HomeState
				&& Intent == Other.Intent
				&& PurchasingPowerBand == Other.PurchasingPowerBand
				&& WoodBand == Other.WoodBand
				&& bAidEligible == Other.bAidEligible;
		}
	};

	FORCEINLINE uint32 GetTypeHash(const FV17AuthoritativeJointKey& Key)
	{
		uint32 Hash = ::GetTypeHash(static_cast<uint8>(Key.Kingdom));
		Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.Profession)));
		Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.IncomeBand)));
		Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.HomeState)));
		Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.Intent)));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.PurchasingPowerBand));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.WoodBand));
		return HashCombine(Hash, ::GetTypeHash(Key.bAidEligible));
	}

	struct FV17AuthoritativeCellConfig
	{
		FV17AuthoritativeCellID CellID = 0;
		FV17AuthoritativeJointKey Key;
		int32 Count = 0;
		int64 CashTotal = 0;
		int64 RepairCreditTotal = 0;
		int64 WoodTotal = 0;
	};

	struct FV17AuthoritativeActiveConfig
	{
		FResidentID ResidentID = 0;
		FV17AuthoritativeCellID SourceCellID = 0;
		FV17AuthoritativeJointKey Key;
		int32 Cash = 0;
		int32 RepairCredit = 0;
		int32 Wood = 0;
	};

	struct FV17IdentityRecord
	{
		FResidentID ResidentID = 0;
		FPersistentID PersistentID = 0;
		uint32 NameSeed = 0;
		uint32 AppearanceSeed = 0;
		FHomeID HomeID = 0;
		EKingdom InitialKingdom = EKingdom::A;
		EProfession Profession = EProfession::Worker;
		EIncomeBand IncomeBand = EIncomeBand::Low;
		uint32 HomeStateIndex = MAX_uint32;
		uint32 IdentityVersion = 1;
	};

	struct FV17ContinuityCapsule
	{
		FResidentID ResidentID = 0;
		FSimulationTime LastObservedTime;
		FIndividualActionState LastObservedState;
		FV17AuthoritativeCellID LastKnownCellID = 0;
		TArray<EIndividualAction> KnownCompletedActions;
		TArray<FEventID> CommittedEventLineage;
		FEventID BatchCursor = 0;
		uint32 CapsuleVersion = 1;
	};

	struct FV17ParticipantRef
	{
		uint64 ParticipantRefID = 0;
		FResidentID ResidentID = 0;
		FEventID BatchEventID = 0;
		FEventID ParentBatchEventID = 0;
		FReservationID ReservationID = 0;
		uint64 InheritedOrderKey = 0;
	};

	enum class EV17LODTransitionResult : uint8
	{
		Committed,
		ResidentNotFound,
		AlreadyActive,
		AlreadyRestricted,
		ActiveCapReached,
		NoEligibleJointCell,
		StateExtractionFailed,
		EventSplitFailed,
		EventMergeFailed
	};

	struct FV17LODTransitionRecord
	{
		FResidentID ResidentID = 0;
		FSimulationTime GameTime;
		bool bLift = true;
		FV17AuthoritativeCellID SelectedCellID = 0;
		FEventID BatchEventID = 0;
		FEventID ParentBatchEventID = 0;
		bool bUsedFallback = false;
		EV17LODTransitionResult Result = EV17LODTransitionResult::ResidentNotFound;
	};

	struct FV17AuthoritativeKingdomConfig
	{
		EKingdom Kingdom = EKingdom::A;
		int64 MarketWood = 0;
		int64 ForestWood = 0;
		int64 HarvestCapacity = 0;
		int64 MarketCoin = 0;
		int64 EmbeddedRepairWood = 0;
		int64 RepairedHomeWood = 0;
		int32 RepairCapacity = 0;
		double WoodPrice = 1.0;
		int64 TreasuryCoin = 0;
	};

	struct FV17AuthoritativeClaim
	{
		FV17AuthoritativeClaimID BatchClaimID = 0;
		FSimulationTime GameTime;
		FString ResourceScope;
		EIndividualAction Action = EIndividualAction::None;
		FV17AuthoritativeCellID SourceCellID = 0;
		int32 RequestedCount = 0;
		int32 PerParticipantDemand = 0;
		FPolicyID CausalPolicyID = 0;
		uint64 StableOrderKey = 0;
		int32 GrantedCount = 0;
		int32 RejectedCount = 0;
		FResidentID ActiveResidentID = 0;
		FIndividualActionState FrozenState;
	};

	struct FV17AuthoritativeBatchEvent
	{
		struct FTargetFlow
		{
			FV17AuthoritativeCellID TargetCellID = 0;
			FV17AuthoritativeJointKey TargetKey;
			int32 ParticipantCount = 0;
			int64 CashTotal = 0;
			int64 RepairCreditTotal = 0;
			int64 WoodTotal = 0;
		};

		FEventID BatchEventID = 0;
		FEventID ParentBatchEventID = 0;
		FV17AuthoritativeClaimID BatchClaimID = 0;
		FV17AuthoritativeCellID SourceCellID = 0;
		FV17AuthoritativeCellID TargetCellID = 0;
		EIndividualAction Action = EIndividualAction::None;
		int32 ParticipantCount = 0;
		FSimulationTime StartTime;
		FSimulationTime EndTime;
		int64 RemainingWorkMinutes = 0;
		FReservationID BatchReservationID = 0;
		FPolicyID CausalPolicyID = 0;
		uint64 InheritedOrderKey = 0;
		ESimulationEventState Status = ESimulationEventState::Pending;
		FResidentID ActiveResidentID = 0;
		FIndividualActionState FrozenState;
		TArray<FTargetFlow> TargetFlows;
		TArray<uint32> AssignedHomeStateIndices;
	};

	struct FV17SystemImportEvent
	{
		FEventID EventID = 0;
		FReservationID ReservationID = 0;
		EKingdom Kingdom = EKingdom::A;
		double WoodQuantity = 0.0;
		int64 CoinCost = 0;
		FSimulationTime StartTime;
		FSimulationTime EndTime;
		ESimulationEventState Status = ESimulationEventState::Pending;
	};

	enum class EV17AuthoritativeFailurePoint : uint8
	{
		None,
		AfterFirstCommittedClaim,
		AfterFirstCompletionResourceWrite
	};

	enum class EV17LODTransitionFailurePoint : uint8
	{
		None,
		LiftAfterCellCount,
		LiftAfterLedgerTransfer,
		LiftAfterEventSplit,
		RestrictAfterLedgerTransfer,
		RestrictAfterEventMerge
	};

	struct FV17AuthoritativeAudit
	{
		int64 PopulationResidual = 0;
		double CoinResidual = 0.0;
		double WoodResidual = 0.0;
		int32 NegativeJointCellCount = 0;
		int32 JointCellResourceBandMismatchCount = 0;
		int32 NegativeStockCount = 0;
		int32 BatchRequestedGrantResidualCount = 0;
		int32 BatchCapacityOverflowCount = 0;
		int32 DuplicateBatchCommitCount = 0;
		int32 PendingEventResidualCount = 0;
		int32 ReservationResidualCount = 0;
		int32 CommitResidueCount = 0;
		int32 OwnerConflictCount = 0;
		int32 ActiveCapViolationCount = 0;
		int32 DuplicateTransactionCount = 0;
		int32 DuplicateCompletionCount = 0;
		int32 IdentityMismatchCount = 0;
		int32 CapsuleIdentityMismatchCount = 0;
		int32 BatchSplitMergeResidualCount = 0;
		int32 LiftRestrictResidueCount = 0;
		int32 TaskResetCount = 0;
		int32 HomeContinuityResidualCount = 0;

		bool IsHardErrorFree() const;
	};

	struct FV17TrackedAuthorityMemory
	{
		uint64 AuthorityFixedBytes = 0;
		uint64 IdentityRegistryBytes = 0;
		uint64 HomeContinuityBytes = 0;
		uint64 JointStateBytes = 0;
		uint64 ActiveStateBytes = 0;
		uint64 CapsuleBytes = 0;
		uint64 ParticipantRefBytes = 0;
		uint64 BatchClaimBytes = 0;
		uint64 BatchEventBytes = 0;
		uint64 SystemEventBytes = 0;
		uint64 LedgerBytes = 0;
		uint64 ReservationBytes = 0;
		uint64 EventStoreBytes = 0;
		uint64 SchedulerBytes = 0;
		uint64 LODTransitionBytes = 0;
		uint64 TotalBytes = 0;

		uint64 SumComponents() const
		{
			return AuthorityFixedBytes
				+ IdentityRegistryBytes
				+ HomeContinuityBytes
				+ JointStateBytes
				+ ActiveStateBytes
				+ CapsuleBytes
				+ ParticipantRefBytes
				+ BatchClaimBytes
				+ BatchEventBytes
				+ SystemEventBytes
				+ LedgerBytes
				+ ReservationBytes
				+ EventStoreBytes
				+ SchedulerBytes
				+ LODTransitionBytes;
		}
	};

	class FV17AuthoritativeMacroSession
	{
	public:
		explicit FV17AuthoritativeMacroSession(int32 InSeed);

		bool Initialize(
			const TArray<FV17AuthoritativeCellConfig>& Cells,
			const TArray<FV17AuthoritativeActiveConfig>& ActiveResidents,
			const TArray<FV17AuthoritativeKingdomConfig>& Kingdoms,
			FSimulationTime StartTime,
			FString& OutError);
		bool InitializeWithIdentity(
			const TArray<FV17AuthoritativeCellConfig>& Cells,
			const TArray<FV17IdentityRecord>& Identities,
			const TArray<FV17AuthoritativeKingdomConfig>& Kingdoms,
			FSimulationTime StartTime,
			FString& OutError);

		bool LiftResident(
			FResidentID ResidentID,
			FSimulationTime Time,
			FString& OutError,
			EV17LODTransitionFailurePoint FailurePoint = EV17LODTransitionFailurePoint::None);
		bool RestrictResident(
			FResidentID ResidentID,
			FSimulationTime Time,
			FString& OutError,
			EV17LODTransitionFailurePoint FailurePoint = EV17LODTransitionFailurePoint::None);

		bool QueueMacroAction(
			FV17AuthoritativeCellID SourceCellID,
			EIndividualAction Action,
			int32 RequestedCount,
			FPolicyID CausalPolicyID,
			FV17AuthoritativeClaimID& OutClaimID,
			FString& OutError);

		bool QueueActiveAction(
			FResidentID ResidentID,
			EIndividualAction Action,
			FPolicyID CausalPolicyID,
			FV17AuthoritativeClaimID& OutClaimID,
			FString& OutError);

		bool ResolveAndCommitClaims(
			FString& OutError,
			EV17AuthoritativeFailurePoint FailurePoint = EV17AuthoritativeFailurePoint::None);

		bool AdvanceTo(
			FSimulationTime TargetTime,
			FString& OutError,
			EV17AuthoritativeFailurePoint FailurePoint = EV17AuthoritativeFailurePoint::None);

		bool ApplySystemTransfer(
			ESimulationResource Resource,
			const FString& Source,
			const FString& Destination,
			double Quantity,
			bool bBoundary,
			const FString& IdempotencyKey,
			FPolicyID PolicyID,
			FString& OutError);
		bool MoveJointCellParticipants(
			FV17AuthoritativeCellID SourceCellID,
			const FV17AuthoritativeJointKey& TargetKey,
			int32 ParticipantCount,
			int64 CashTotal,
			int64 RepairCreditTotal,
			int64 WoodTotal,
			const FString& IdempotencyPrefix,
			FPolicyID PolicyID,
			FV17AuthoritativeCellID& OutTargetCellID,
			FString& OutError);
		bool ApplyEarthquakeHomeDamage(
			const TArray<FResidentID>& DamagedResidentIDs,
			FString& OutError);
		bool SetJointCellAidEligibility(
			FV17AuthoritativeCellID SourceCellID,
			bool bEligible,
			FV17AuthoritativeCellID& OutTargetCellID,
			FString& OutError);
		bool GrantRepairAid(
			FV17AuthoritativeCellID SourceCellID,
			int32 ParticipantCount,
			int32 CoinPerParticipant,
			FPolicyID PolicyID,
			FV17AuthoritativeCellID& OutTargetCellID,
			FString& OutError);
		bool CreateInstantSystemEvent(
			const FString& Type,
			int32 ParticipantCount,
			FPolicyID PolicyID,
			FString& OutError);
		bool QueueStateImport(
			EKingdom Kingdom,
			double WoodQuantity,
			int64 CoinCost,
			FPolicyID PolicyID,
			FString& OutError);
		bool SetWoodPrice(EKingdom Kingdom, double WoodPrice, FString& OutError);
		bool SetHarvestRemaining(EKingdom Kingdom, int64 Quantity, FString& OutError);
		bool NormalizeReadyCellBands(
			FV17AuthoritativeCellID CellID,
			FV17AuthoritativeCellID& OutPrimaryCellID,
			FString& OutError);
		void EnableExactAggregateResourceSplits() { bExactAggregateResourceSplits = true; }

		int32 GetReadyCount(FV17AuthoritativeCellID CellID) const;
		int32 GetPendingParticipantCount() const;
		int32 GetActionParticipantCount(EIndividualAction Action) const;
		int32 GetClaimGrantedCount(FV17AuthoritativeClaimID ClaimID) const;
		int32 GetClaimRejectedCount(FV17AuthoritativeClaimID ClaimID) const;
		int32 GetRepairCapacityRemaining(EKingdom Kingdom) const;
		int64 GetHarvestRemaining(EKingdom Kingdom) const;
		bool IsActiveReady(FResidentID ResidentID) const;
		int64 GetCellCash(FV17AuthoritativeCellID CellID) const;
		int64 GetCellRepairCredit(FV17AuthoritativeCellID CellID) const;
		int64 GetCellWood(FV17AuthoritativeCellID CellID) const;
		int64 GetKingdomBalance(EKingdom Kingdom, ESimulationResource Resource, const TCHAR* Stock) const;
		double GetKingdomBalanceExact(EKingdom Kingdom, ESimulationResource Resource, const TCHAR* Stock) const;
		int32 GetActiveMicroCount() const { return ActiveStates.Num(); }
		int32 GetLiftReconstructionFallbackCount() const { return LiftReconstructionFallbackCount; }
		int64 GetRemainingWorkMinutes(FResidentID ResidentID) const;
		bool GetActiveSnapshot(
			FResidentID ResidentID,
			FIndividualActionState& OutState,
			EIndividualAction& OutAction,
			FEventID& OutEventID) const;
		const FV17IdentityRecord* FindIdentity(FResidentID ResidentID) const;
		const FV17ContinuityCapsule* FindCapsule(FResidentID ResidentID) const;
		const FV17ParticipantRef* FindParticipantRef(FResidentID ResidentID) const;
		bool GetResidentHomeState(FResidentID ResidentID, EHomeState& OutHomeState) const;
		int64 GetHomeStateUpdateCount() const { return HomeStateUpdateCount; }

		const TMap<FV17AuthoritativeClaimID, FV17AuthoritativeClaim>& GetClaims() const { return Claims; }
		const TMap<FEventID, FV17AuthoritativeBatchEvent>& GetBatchEvents() const { return BatchEvents; }
		const TMap<FEventID, FV17SystemImportEvent>& GetSystemImportEvents() const { return SystemImportEvents; }
		const TMap<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& GetCells() const { return Cells; }
		const FResourceLedger& GetLedger() const { return Ledger; }
		const FReservationStore& GetReservations() const { return Reservations; }
		const FSimulationEventStore& GetEventStore() const { return EventStore; }
		const FSimulationScheduler& GetScheduler() const { return Scheduler; }
		const TMap<FResidentID, FV17IdentityRecord>& GetIdentityRegistry() const { return IdentityRegistry; }
		const TMap<FResidentID, FV17ContinuityCapsule>& GetCapsules() const { return Capsules; }
		const TMap<FResidentID, FV17ParticipantRef>& GetParticipantRefs() const { return ParticipantRefs; }
		const TArray<FV17LODTransitionRecord>& GetLODTransitions() const { return LODTransitions; }
		FSimulationTime GetCurrentTime() const { return Clock.Now(); }
		void GetActiveResidentIDs(TArray<FResidentID>& OutResidentIDs) const
		{
			ActiveStates.GetKeys(OutResidentIDs);
			OutResidentIDs.Sort();
		}
		FV17AuthoritativeAudit BuildAudit() const;
		FV17TrackedAuthorityMemory BuildTrackedMemory() const;
		FString BuildDeterministicDigest() const;

	private:
		struct FHomeRepairQueue
		{
			TArray<uint32> CandidateHomeStateIndices;
			TArray<uint32> DeferredActiveHomeStateIndices;
			int32 NextCandidate = 0;
		};

		struct FActiveState
		{
			FV17AuthoritativeActiveConfig Definition;
			bool bReady = true;
			EIndividualAction CurrentAction = EIndividualAction::None;
			FEventID ActiveEventID = 0;
			FEventID ParentEventID = 0;
			FArriveID ActiveArriveID = 0;
			FReservationID ActiveReservationID = 0;
			uint64 InheritedOrderKey = 0;
			FSimulationTime ActionStartTime;
			FSimulationTime ActionEndTime;
		};

		bool SelectLiftCell(
			const FV17IdentityRecord& Identity,
			const FV17ContinuityCapsule* Capsule,
			FV17AuthoritativeCellID& OutCellID,
			bool& bOutUsedFallback,
			FString& OutError) const;
		bool ExtractStateFromCell(
			FV17AuthoritativeCellID CellID,
			FResidentID ResidentID,
			FIndividualActionState& OutState,
			FString& OutError) const;
		bool LiftFromJointCell(
			const FV17IdentityRecord& Identity,
			FV17AuthoritativeCellID CellID,
			FIndividualActionState State,
			EV17LODTransitionFailurePoint FailurePoint,
			FString& OutError);
		bool LiftFromPendingEvent(
			const FV17IdentityRecord& Identity,
			FV17ParticipantRef& ParticipantRef,
			EV17LODTransitionFailurePoint FailurePoint,
			FString& OutError);
		bool RestrictIdleResident(
			FResidentID ResidentID,
			FActiveState& Active,
			EV17LODTransitionFailurePoint FailurePoint,
			FV17AuthoritativeCellID& OutTargetCellID,
			FString& OutError);
		bool RestrictPendingResident(
			FResidentID ResidentID,
			FActiveState& Active,
			EV17LODTransitionFailurePoint FailurePoint,
			FEventID& OutBatchEventID,
			FEventID& OutParentEventID,
			FString& OutError);
		FEventID FindCompatibleMergeTarget(const FV17AuthoritativeBatchEvent& ActiveEvent) const;
		bool MoveEventAccountBalances(FEventID SourceEventID, FEventID TargetEventID, FPolicyID PolicyID, FString& OutError);
		FIndividualActionState ReadPendingEventState(const FV17AuthoritativeBatchEvent& Event) const;
		void UpdateCapsule(
			FResidentID ResidentID,
			FSimulationTime Time,
			const FIndividualActionState& State,
			FV17AuthoritativeCellID CellID,
			FEventID BatchCursor,
			FEventID LineageEventID,
			EIndividualAction CompletedAction = EIndividualAction::None);
		uint64 BuildParticipantRefID(FResidentID ResidentID, FEventID EventID) const;
		static bool SameFrozenState(const FIndividualActionState& Left, const FIndividualActionState& Right);

		bool QueueAction(
			FV17AuthoritativeCellID SourceCellID,
			FResidentID ActiveResidentID,
			EIndividualAction Action,
			int32 RequestedCount,
			FPolicyID CausalPolicyID,
			FV17AuthoritativeClaimID& OutClaimID,
			FString& OutError);
		bool PreflightClaims(FString& OutError) const;
		bool AllocateClaims(FString& OutError);
		bool CommitClaim(const FV17AuthoritativeClaim& Claim, FString& OutError);
		bool CommitEvent(
			const FV17AuthoritativeClaim& Claim,
			EIndividualAction EventAction,
			int32 ParticipantCount,
			bool bRejectedWait,
			FString& OutError);
		bool CompleteEvent(FV17AuthoritativeBatchEvent& Event, const FScheduledEvent& Due, bool& bWroteResource, FString& OutError);
		bool AssignRepairHomes(
			const FV17AuthoritativeClaim& Claim,
			FEventID EventID,
			int32 ParticipantCount,
			FV17AuthoritativeBatchEvent& Event,
			FString& OutError);
		bool CompleteRepairHomes(FV17AuthoritativeBatchEvent& Event, FString& OutError);
		void RebuildHomeRepairQueues();
		static uint32 BuildHomeOuterKey(EKingdom Kingdom, EProfession Profession, EIncomeBand IncomeBand);
		bool CompleteSystemImport(FV17SystemImportEvent& Event, const FScheduledEvent& Due, FString& OutError);
		void BuildNormalizedTargetFlows(
			const FV17AuthoritativeJointKey& BaseKey,
			int32 ParticipantCount,
			int64 CashTotal,
			int64 RepairCreditTotal,
			int64 WoodTotal,
			TArray<FV17AuthoritativeBatchEvent::FTargetFlow>& OutFlows);
		bool DistributeEventStateToTargets(
			FV17AuthoritativeBatchEvent& Event,
			const FV17AuthoritativeJointKey& BaseKey,
			FV17AuthoritativeCellID& OutPrimaryCellID,
			FString& OutError);

		FV17AuthoritativeClaimID BuildClaimID(
			FV17AuthoritativeCellID SourceCellID,
			FResidentID ActiveResidentID,
			EIndividualAction Action,
			const FString& ResourceScope,
			int32 PerParticipantDemand,
			FPolicyID CausalPolicyID) const;
		uint64 BuildStableOrderKey(FV17AuthoritativeClaimID ClaimID) const;
		uint64 BuildRemainderTieKey(const FV17AuthoritativeClaim& Claim) const;
		int64 GetScopeCapacity(const FV17AuthoritativeClaim& Claim) const;
		FString BuildScope(EKingdom Kingdom, EIndividualAction Action) const;
		FV17AuthoritativeCellID FindOrCreateTargetCell(
			const FV17AuthoritativeJointKey& SourceKey,
			const FIndividualActionState& FinalState);
		FV17AuthoritativeCellID FindOrCreateCellByKey(const FV17AuthoritativeJointKey& Key);
		bool TransferQuantity(
			FSimulationTime Time,
			ESimulationResource Resource,
			const FString& Source,
			const FString& Destination,
			double Quantity,
			bool bBoundary,
			const FString& IdempotencyKey,
			FEventID EventID,
			FArriveID ArriveID,
			FPolicyID PolicyID,
			FString& OutError);
		bool Transfer(
			FSimulationTime Time,
			ESimulationResource Resource,
			const FString& Source,
			const FString& Destination,
			int64 Quantity,
			bool bBoundary,
			const FString& IdempotencyKey,
			FEventID EventID,
			FArriveID ArriveID,
			FPolicyID PolicyID,
			FString& OutError);
		bool TransferParticipantStateToEvent(
			const FV17AuthoritativeClaim& Claim,
			FEventID EventID,
			FArriveID ArriveID,
			int32 ParticipantCount,
			FString& OutError);
		bool TransferEventStateToDestination(
			const FV17AuthoritativeBatchEvent& Event,
			FV17AuthoritativeCellID TargetCellID,
			FString& OutError);

		static int32 PurchasingPowerBand(int64 PurchasingPower);
		static int32 WoodBand(int64 Wood);
		static FString CellAccount(FV17AuthoritativeCellID CellID, const TCHAR* Stock);
		static FString ActiveAccount(FResidentID ResidentID, const TCHAR* Stock);
		static FString EventAccount(FEventID EventID, const TCHAR* Stock);
		static FString KingdomAccount(EKingdom Kingdom, const TCHAR* Stock);

		int32 Seed = 0;
		bool bInitialized = false;
		bool bDynamicLODEnabled = false;
		bool bClaimsCommittedAtCurrentTime = false;
		int32 InitialPopulation = 0;
		int32 BatchCapacityOverflowCount = 0;
		int32 DuplicateBatchCommitCount = 0;
		int32 CommitResidueCount = 0;
		FSimulationClock Clock;
		FSimulationScheduler Scheduler;
		FSimulationEventStore EventStore;
		FResourceLedger Ledger;
		FReservationStore Reservations;
		TMap<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig> Cells;
		TMap<FV17AuthoritativeJointKey, FV17AuthoritativeCellID> CellIDsByKey;
		TMap<FResidentID, FActiveState> ActiveStates;
		TMap<FResidentID, FV17IdentityRecord> IdentityRegistry;
		TArray<uint8> HomeStatesByIndex;
		TArray<FResidentID> ResidentIDsByHomeStateIndex;
		TMap<uint32, FHomeRepairQueue> HomeRepairQueues;
		TMap<uint32, FEventID> PendingRepairEventByHomeStateIndex;
		TMap<FResidentID, FV17ContinuityCapsule> Capsules;
		TMap<FResidentID, FV17ParticipantRef> ParticipantRefs;
		TArray<FV17LODTransitionRecord> LODTransitions;
		TMap<EKingdom, FV17AuthoritativeKingdomConfig> KingdomConfigs;
		TMap<EKingdom, int32> RepairCapacityRemaining;
		TMap<EKingdom, int64> HarvestRemaining;
		int64 CapacityDay = 0;
		TMap<FV17AuthoritativeClaimID, FV17AuthoritativeClaim> Claims;
		TArray<FV17AuthoritativeClaimID> QueuedClaimIDs;
		TMap<FEventID, FV17AuthoritativeBatchEvent> BatchEvents;
		TMap<FEventID, FV17SystemImportEvent> SystemImportEvents;
		bool bFractionalResourcesEnabled = false;
		bool bExactAggregateResourceSplits = false;
		int32 LiftReconstructionFallbackCount = 0;
		int32 LiftRestrictResidueCount = 0;
		int32 BatchSplitMergeResidualCount = 0;
		int32 TaskResetCount = 0;
		int64 HomeStateUpdateCount = 0;
	};
}
