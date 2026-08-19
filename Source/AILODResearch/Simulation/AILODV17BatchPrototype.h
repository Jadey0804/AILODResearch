// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODIndividualSimulation.h"

namespace AILOD
{
	using FV17BatchClaimID = uint64;
	using FV17JointCellID = uint64;

	struct FV17BatchPrototypeCell
	{
		FV17JointCellID CellID = 0;
		int32 ReadyCount = 0;
		EKingdom Kingdom = EKingdom::A;
		EIncomeBand IncomeBand = EIncomeBand::Low;
		int64 InitialCash = 0;
	};

	struct FV17BatchPrototypeTreasury
	{
		EKingdom Kingdom = EKingdom::A;
		int64 AvailableCoin = 0;
	};

	enum class EV17BatchPrototypeFailurePoint : uint8
	{
		None,
		AfterFirstWorkLedgerTransfer
	};

	struct FV17BatchPrototypeClaim
	{
		FV17BatchClaimID BatchClaimID = 0;
		FSimulationTime GameTime;
		FString ResourceScope;
		EIndividualAction Action = EIndividualAction::None;
		FV17JointCellID SourceCellID = 0;
		int32 RequestedCount = 0;
		int32 PerParticipantDemand = 0;
		uint64 CausalPolicyID = 0;
		uint64 StableOrderKey = 0;
	};

	struct FV17BatchPrototypeEvent
	{
		FEventID BatchEventID = 0;
		FEventID ParentBatchEventID = 0;
		FV17BatchClaimID BatchClaimID = 0;
		FV17JointCellID SourceCellID = 0;
		FV17JointCellID TargetCellID = 0;
		EIndividualAction Action = EIndividualAction::None;
		int32 ParticipantCount = 0;
		int32 RequestedCount = 0;
		int32 GrantedCount = 0;
		int32 RejectedCount = 0;
		FSimulationTime StartTime;
		FSimulationTime EndTime;
		int64 RemainingWorkMinutes = 0;
		uint64 BatchReservationID = 0;
		uint64 CausalPolicyID = 0;
		uint64 InheritedOrderKey = 0;
		ESimulationEventState Status = ESimulationEventState::Pending;
	};

	struct FV17BatchPrototypeAudit
	{
		int64 PopulationResidual = 0;
		int32 NegativeReadyCellCount = 0;
		int32 RequestResultResidualCount = 0;
		int32 EventParticipantResidualCount = 0;
		int32 PendingEventResidualCount = 0;
		int32 DuplicateCompletionCount = 0;
		int32 CommitResidueCount = 0;
		double CoinResidual = 0.0;
		int32 NegativeCoinStockCount = 0;
		int32 DuplicateTransactionCount = 0;
		int32 WorkLedgerResidualCount = 0;
		int32 TreasuryResidualCount = 0;

		bool IsHardErrorFree() const;
	};

	class FV17BatchSlicePrototype
	{
	public:
		explicit FV17BatchSlicePrototype(int32 InSeed);

		bool Initialize(
			const TArray<FV17BatchPrototypeCell>& Cells,
			FSimulationTime StartTime,
			FString& OutError);
		bool InitializeWithWorkLedger(
			const TArray<FV17BatchPrototypeCell>& Cells,
			const TArray<FV17BatchPrototypeTreasury>& Treasuries,
			FSimulationTime StartTime,
			FString& OutError);

		bool SubmitBatch(
			FV17JointCellID SourceCellID,
			EIndividualAction Action,
			int32 ParticipantCount,
			FEventID& OutEventID,
			FString& OutError);

		bool AdvanceTo(
			FSimulationTime TargetTime,
			FString& OutError,
			EV17BatchPrototypeFailurePoint FailurePoint = EV17BatchPrototypeFailurePoint::None);

		int32 GetReadyCount(FV17JointCellID CellID) const;
		int64 GetAggregateCash(FV17JointCellID CellID) const;
		int64 GetTreasuryAvailable(EKingdom Kingdom) const;
		int32 GetPendingParticipantCount() const;
		int32 GetPendingEventCount() const;
		int32 GetCompletedEventCount() const;
		int64 GetParticipantWeightedActionCount(EIndividualAction Action) const;
		const TMap<FV17BatchClaimID, FV17BatchPrototypeClaim>& GetBatchClaims() const { return BatchClaims; }
		const TMap<FEventID, FV17BatchPrototypeEvent>& GetBatchEvents() const { return BatchEvents; }
		const FSimulationEventStore& GetEventStore() const { return EventStore; }
		const FSimulationScheduler& GetScheduler() const { return Scheduler; }
		const FResourceLedger& GetLedger() const { return Ledger; }
		FV17BatchPrototypeAudit BuildAudit() const;
		FString BuildDeterministicDigest() const;

	private:
		FV17BatchClaimID BuildClaimID(
			FV17JointCellID SourceCellID,
			EIndividualAction Action) const;
		bool InitializeInternal(
			const TArray<FV17BatchPrototypeCell>& Cells,
			const TArray<FV17BatchPrototypeTreasury>& Treasuries,
			FSimulationTime StartTime,
			bool bEnableWorkLedger,
			FString& OutError);
		static FString CellCashAccount(FV17JointCellID CellID);
		static FString TreasuryAccount(EKingdom Kingdom);

		int32 Seed = 0;
		bool bInitialized = false;
		bool bWorkLedgerEnabled = false;
		int32 InitialPopulation = 0;
		int32 CommitResidueCount = 0;
		FSimulationClock Clock;
		FSimulationScheduler Scheduler;
		FSimulationEventStore EventStore;
		FResourceLedger Ledger;
		TMap<FV17JointCellID, FV17BatchPrototypeCell> CellDefinitions;
		TMap<EKingdom, int64> InitialTreasuries;
		TMap<FV17JointCellID, int32> ReadyCounts;
		TSet<FV17BatchClaimID> SubmittedClaimIDs;
		TMap<FV17BatchClaimID, FV17BatchPrototypeClaim> BatchClaims;
		TMap<FEventID, FV17BatchPrototypeEvent> BatchEvents;
	};
}
