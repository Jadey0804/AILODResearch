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
	};

	enum class EV17AuthoritativeFailurePoint : uint8
	{
		None,
		AfterFirstCommittedClaim,
		AfterFirstCompletionResourceWrite
	};

	struct FV17AuthoritativeAudit
	{
		int64 PopulationResidual = 0;
		double CoinResidual = 0.0;
		double WoodResidual = 0.0;
		int32 NegativeJointCellCount = 0;
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

		bool IsHardErrorFree() const;
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

		const TMap<FV17AuthoritativeClaimID, FV17AuthoritativeClaim>& GetClaims() const { return Claims; }
		const TMap<FEventID, FV17AuthoritativeBatchEvent>& GetBatchEvents() const { return BatchEvents; }
		const FResourceLedger& GetLedger() const { return Ledger; }
		const FReservationStore& GetReservations() const { return Reservations; }
		const FSimulationEventStore& GetEventStore() const { return EventStore; }
		const FSimulationScheduler& GetScheduler() const { return Scheduler; }
		FV17AuthoritativeAudit BuildAudit() const;
		FString BuildDeterministicDigest() const;

	private:
		struct FActiveState
		{
			FV17AuthoritativeActiveConfig Definition;
			bool bReady = true;
		};

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
		TMap<EKingdom, FV17AuthoritativeKingdomConfig> KingdomConfigs;
		TMap<EKingdom, int32> RepairCapacityRemaining;
		TMap<EKingdom, int64> HarvestRemaining;
		int64 CapacityDay = 0;
		TMap<FV17AuthoritativeClaimID, FV17AuthoritativeClaim> Claims;
		TArray<FV17AuthoritativeClaimID> QueuedClaimIDs;
		TMap<FEventID, FV17AuthoritativeBatchEvent> BatchEvents;
	};
}
