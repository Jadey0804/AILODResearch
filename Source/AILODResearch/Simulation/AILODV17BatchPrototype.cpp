// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODV17BatchPrototype.h"

#include "AILODDomainRules.h"
#include "Misc/SecureHash.h"

namespace AILOD
{
	using namespace DomainRules;

	bool FV17BatchPrototypeAudit::IsHardErrorFree() const
	{
		return PopulationResidual == 0
			&& NegativeReadyCellCount == 0
			&& RequestResultResidualCount == 0
			&& EventParticipantResidualCount == 0
			&& PendingEventResidualCount == 0
			&& DuplicateCompletionCount == 0
			&& CommitResidueCount == 0
			&& FMath::IsNearlyZero(CoinResidual, UE_DOUBLE_SMALL_NUMBER)
			&& NegativeCoinStockCount == 0
			&& DuplicateTransactionCount == 0
			&& WorkLedgerResidualCount == 0
			&& TreasuryResidualCount == 0;
	}

	FV17BatchSlicePrototype::FV17BatchSlicePrototype(const int32 InSeed)
		: Seed(InSeed)
	{
	}

	bool FV17BatchSlicePrototype::Initialize(
		const TArray<FV17BatchPrototypeCell>& Cells,
		const FSimulationTime StartTime,
		FString& OutError)
	{
		return InitializeInternal(Cells, {}, StartTime, false, OutError);
	}

	bool FV17BatchSlicePrototype::InitializeWithWorkLedger(
		const TArray<FV17BatchPrototypeCell>& Cells,
		const TArray<FV17BatchPrototypeTreasury>& Treasuries,
		const FSimulationTime StartTime,
		FString& OutError)
	{
		return InitializeInternal(Cells, Treasuries, StartTime, true, OutError);
	}

	bool FV17BatchSlicePrototype::InitializeInternal(
		const TArray<FV17BatchPrototypeCell>& Cells,
		const TArray<FV17BatchPrototypeTreasury>& Treasuries,
		const FSimulationTime StartTime,
		const bool bEnableWorkLedger,
		FString& OutError)
	{
		if (bInitialized || Cells.IsEmpty())
		{
			OutError = TEXT("The no-resource batch prototype requires one initialization with at least one cell.");
			return false;
		}

		TMap<FV17JointCellID, int32> NewReadyCounts;
		TMap<FV17JointCellID, FV17BatchPrototypeCell> NewCellDefinitions;
		int32 NewPopulation = 0;
		for (const FV17BatchPrototypeCell& Cell : Cells)
		{
			if (Cell.CellID == 0 || Cell.ReadyCount < 0 || Cell.InitialCash < 0
				|| NewReadyCounts.Contains(Cell.CellID))
			{
				OutError = TEXT("Batch prototype cells require unique non-zero IDs and non-negative counts and cash.");
				return false;
			}
			NewReadyCounts.Add(Cell.CellID, Cell.ReadyCount);
			NewCellDefinitions.Add(Cell.CellID, Cell);
			NewPopulation += Cell.ReadyCount;
		}
		if (NewPopulation <= 0)
		{
			OutError = TEXT("The no-resource batch prototype requires a positive population.");
			return false;
		}

		TMap<EKingdom, int64> NewInitialTreasuries;
		for (const FV17BatchPrototypeTreasury& Treasury : Treasuries)
		{
			if (Treasury.AvailableCoin < 0 || NewInitialTreasuries.Contains(Treasury.Kingdom))
			{
				OutError = TEXT("Work batch treasury entries must be unique and non-negative.");
				return false;
			}
			NewInitialTreasuries.Add(Treasury.Kingdom, Treasury.AvailableCoin);
		}

		if (bEnableWorkLedger)
		{
			for (const TPair<FV17JointCellID, FV17BatchPrototypeCell>& Pair : NewCellDefinitions)
			{
				if (!Ledger.InitializeAccount(
					ESimulationResource::Coin,
					CellCashAccount(Pair.Key),
					static_cast<double>(Pair.Value.InitialCash),
					OutError))
				{
					Ledger = {};
					return false;
				}
			}
			for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
			{
				const int64* InitialTreasury = NewInitialTreasuries.Find(Kingdom);
				if (!Ledger.InitializeAccount(
					ESimulationResource::Coin,
					TreasuryAccount(Kingdom),
					static_cast<double>(InitialTreasury != nullptr ? *InitialTreasury : 0),
					OutError))
				{
					Ledger = {};
					return false;
				}
			}
			Ledger.SealInitialState();
		}

		ReadyCounts = MoveTemp(NewReadyCounts);
		CellDefinitions = MoveTemp(NewCellDefinitions);
		InitialTreasuries = MoveTemp(NewInitialTreasuries);
		InitialPopulation = NewPopulation;
		Clock = FSimulationClock(StartTime);
		bWorkLedgerEnabled = bEnableWorkLedger;
		bInitialized = true;
		OutError.Reset();
		return true;
	}

	FString FV17BatchSlicePrototype::CellCashAccount(const FV17JointCellID CellID)
	{
		return FString::Printf(TEXT("V17.Cell.%llu.Cash"), CellID);
	}

	FString FV17BatchSlicePrototype::TreasuryAccount(const EKingdom Kingdom)
	{
		return MakeKingdomAccount(Kingdom, TEXT("TreasuryAvailable"));
	}

	FV17BatchClaimID FV17BatchSlicePrototype::BuildClaimID(
		const FV17JointCellID SourceCellID,
		const EIndividualAction Action) const
	{
		uint64 Value = Mix64(static_cast<uint64>(static_cast<uint32>(Seed)));
		Value ^= Mix64(static_cast<uint64>(Clock.Now().Minutes));
		Value ^= Mix64(SourceCellID);
		Value ^= Mix64(static_cast<uint64>(Action));
		const uint64 ClaimID = Mix64(Value ^ 0xB2A00001ull);
		return ClaimID == 0 ? 1 : ClaimID;
	}

	bool FV17BatchSlicePrototype::SubmitBatch(
		const FV17JointCellID SourceCellID,
		const EIndividualAction Action,
		const int32 ParticipantCount,
		FEventID& OutEventID,
		FString& OutError)
	{
		OutEventID = 0;
		const int32* ReadyCount = ReadyCounts.Find(SourceCellID);
		if (!bInitialized || ReadyCount == nullptr)
		{
			OutError = TEXT("The source cell does not exist in the initialized batch prototype.");
			return false;
		}
		const bool bNoResourceAction = Action == EIndividualAction::Routine || Action == EIndividualAction::Wait;
		if (!bNoResourceAction && !(Action == EIndividualAction::Work && bWorkLedgerEnabled))
		{
			OutError = bWorkLedgerEnabled
				? TEXT("Phase 6G-B2 only accepts Routine, Wait and Work batches.")
				: TEXT("Phase 6G-B2A only accepts Routine and Wait batches.");
			return false;
		}
		if (ParticipantCount <= 0 || ParticipantCount > *ReadyCount)
		{
			OutError = TEXT("The batch participant count must be positive and available in the source cell.");
			return false;
		}
		const int64 Duration = FIndividualDomain::GetActionDuration(Action);
		if (Duration <= 0)
		{
			OutError = TEXT("The shared Individual Domain returned an invalid batch duration.");
			return false;
		}

		const FV17BatchClaimID ClaimID = BuildClaimID(SourceCellID, Action);
		if (SubmittedClaimIDs.Contains(ClaimID))
		{
			OutError = TEXT("The same no-resource batch claim has already been submitted.");
			return false;
		}

		const FSimulationScheduler SchedulerBefore = Scheduler;
		const FSimulationEventStore EventStoreBefore = EventStore;
		FSimulationEventRequest Request;
		switch (Action)
		{
		case EIndividualAction::Routine: Request.Type = TEXT("BatchRoutine"); break;
		case EIndividualAction::Wait: Request.Type = TEXT("BatchWait"); break;
		case EIndividualAction::Work: Request.Type = TEXT("BatchWork"); break;
		default: break;
		}
		Request.Owner = FString::Printf(TEXT("BatchCell:%llu"), SourceCellID);
		Request.ActionCode = static_cast<int32>(Action);
		Request.StartTime = Clock.Now();
		Request.EndTime = FSimulationTime::FromMinutes(Clock.Now().Minutes + Duration);
		Request.ArriveID = Scheduler.IssueArriveID();
		Request.ParticipantCount = ParticipantCount;
		Request.Cause = FString::Printf(TEXT("BatchClaim:%llu"), ClaimID);

		if (!EventStore.CreateEvent(Request, OutEventID, OutError)
			|| !Scheduler.Schedule({ OutEventID, Request.ArriveID, Request.EndTime }, Clock.Now(), OutError))
		{
			Scheduler = SchedulerBefore;
			EventStore = EventStoreBefore;
			OutEventID = 0;
			return false;
		}

		ReadyCounts.FindChecked(SourceCellID) -= ParticipantCount;
		SubmittedClaimIDs.Add(ClaimID);
		FV17BatchPrototypeClaim BatchClaim;
		BatchClaim.BatchClaimID = ClaimID;
		BatchClaim.GameTime = Clock.Now();
		BatchClaim.ResourceScope = Action == EIndividualAction::Work ? TEXT("BoundaryIncome") : TEXT("None");
		BatchClaim.Action = Action;
		BatchClaim.SourceCellID = SourceCellID;
		BatchClaim.RequestedCount = ParticipantCount;
		BatchClaim.PerParticipantDemand = Action == EIndividualAction::Work
			? FIndividualDomain::GetWorkIncome(CellDefinitions.FindChecked(SourceCellID).IncomeBand)
			: 0;
		BatchClaim.StableOrderKey = Mix64(ClaimID ^ 0xB2A00003ull);
		if (BatchClaim.StableOrderKey == 0) BatchClaim.StableOrderKey = 1;
		BatchClaims.Add(ClaimID, BatchClaim);
		FV17BatchPrototypeEvent BatchEvent;
		BatchEvent.BatchEventID = OutEventID;
		BatchEvent.BatchClaimID = ClaimID;
		BatchEvent.SourceCellID = SourceCellID;
		BatchEvent.TargetCellID = Mix64(SourceCellID ^ (static_cast<uint64>(Action) << 48) ^ 0xB2A00002ull);
		if (BatchEvent.TargetCellID == 0) BatchEvent.TargetCellID = 1;
		BatchEvent.Action = Action;
		BatchEvent.ParticipantCount = ParticipantCount;
		BatchEvent.RequestedCount = ParticipantCount;
		BatchEvent.GrantedCount = ParticipantCount;
		BatchEvent.StartTime = Request.StartTime;
		BatchEvent.EndTime = Request.EndTime;
		BatchEvent.RemainingWorkMinutes = Duration;
		BatchEvent.InheritedOrderKey = BatchClaim.StableOrderKey;
		BatchEvents.Add(OutEventID, BatchEvent);
		OutError.Reset();
		return true;
	}

	bool FV17BatchSlicePrototype::AdvanceTo(
		const FSimulationTime TargetTime,
		FString& OutError,
		const EV17BatchPrototypeFailurePoint FailurePoint)
	{
		if (!bInitialized || TargetTime < Clock.Now())
		{
			OutError = TEXT("The initialized batch prototype cannot move backwards in time.");
			return false;
		}

		const FSimulationClock ClockBefore = Clock;
		const FSimulationScheduler SchedulerBefore = Scheduler;
		const FSimulationEventStore EventStoreBefore = EventStore;
		const FResourceLedger LedgerBefore = Ledger;
		const TMap<FV17JointCellID, int32> ReadyCountsBefore = ReadyCounts;
		const TMap<FEventID, FV17BatchPrototypeEvent> BatchEventsBefore = BatchEvents;
		auto Restore = [&]()
		{
			Clock = ClockBefore;
			Scheduler = SchedulerBefore;
			EventStore = EventStoreBefore;
			Ledger = LedgerBefore;
			ReadyCounts = ReadyCountsBefore;
			BatchEvents = BatchEventsBefore;
		};
		TArray<FScheduledEvent> DueEvents;
		Scheduler.PopDueThrough(TargetTime, DueEvents);
		for (const FScheduledEvent& Due : DueEvents)
		{
			FV17BatchPrototypeEvent* BatchEvent = BatchEvents.Find(Due.EventID);
			const FSimulationEventRecord* StoredEvent = EventStore.Find(Due.EventID);
			if (BatchEvent == nullptr || BatchEvent->Status != ESimulationEventState::Pending
				|| StoredEvent == nullptr || StoredEvent->State != ESimulationEventState::Pending
				|| StoredEvent->Event.ParticipantCount != BatchEvent->GrantedCount)
			{
				OutError = TEXT("A due batch event does not match its stored event or participant count.");
				Restore();
				return false;
			}

			if (BatchEvent->Action == EIndividualAction::Work)
			{
				const FV17BatchPrototypeClaim* Claim = BatchClaims.Find(BatchEvent->BatchClaimID);
				if (!bWorkLedgerEnabled || Claim == nullptr
					|| Claim->Action != EIndividualAction::Work
					|| Claim->ResourceScope != TEXT("BoundaryIncome")
					|| Claim->PerParticipantDemand <= 0)
				{
					OutError = TEXT("A due Work batch is missing its frozen aggregate wage claim.");
					Restore();
					return false;
				}

				FLedgerTransferRequest WageTransfer;
				WageTransfer.IdempotencyKey = FString::Printf(
					TEXT("V17-WORK-INCOME-%llu-0"),
					BatchEvent->BatchClaimID);
				WageTransfer.GameTime = Due.ExecuteAt;
				WageTransfer.Resource = ESimulationResource::Coin;
				WageTransfer.Source = ExternalBoundaryAccount;
				WageTransfer.Destination = CellCashAccount(BatchEvent->SourceCellID);
				WageTransfer.Quantity = static_cast<double>(
					static_cast<int64>(BatchEvent->ParticipantCount) * Claim->PerParticipantDemand);
				WageTransfer.bBoundaryFlow = true;
				WageTransfer.EventID = BatchEvent->BatchEventID;
				WageTransfer.ArriveID = StoredEvent->Event.ArriveID;
				WageTransfer.PolicyID = BatchEvent->CausalPolicyID;
				FTransactionID TransactionID = 0;
				if (!Ledger.SubmitTransfer(WageTransfer, TransactionID, OutError))
				{
					Restore();
					return false;
				}
				if (FailurePoint == EV17BatchPrototypeFailurePoint::AfterFirstWorkLedgerTransfer)
				{
					OutError = TEXT("Injected B2B failure after the first aggregate Work wage transfer.");
					Restore();
					return false;
				}
			}

			if (!EventStore.CompleteEvent(Due.EventID, OutError))
			{
				Restore();
				return false;
			}
			ReadyCounts.FindChecked(BatchEvent->SourceCellID) += BatchEvent->GrantedCount;
			BatchEvent->RemainingWorkMinutes = 0;
			BatchEvent->Status = ESimulationEventState::Completed;
		}
		for (TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
		{
			FV17BatchPrototypeEvent& BatchEvent = Pair.Value;
			if (BatchEvent.Status == ESimulationEventState::Pending)
			{
				BatchEvent.RemainingWorkMinutes = FMath::Max<int64>(0, BatchEvent.EndTime.Minutes - TargetTime.Minutes);
			}
		}

		if (!Clock.AdvanceTo(TargetTime, OutError))
		{
			Restore();
			return false;
		}
		OutError.Reset();
		return true;
	}

	int32 FV17BatchSlicePrototype::GetReadyCount(const FV17JointCellID CellID) const
	{
		const int32* Count = ReadyCounts.Find(CellID);
		return Count != nullptr ? *Count : 0;
	}

	int64 FV17BatchSlicePrototype::GetAggregateCash(const FV17JointCellID CellID) const
	{
		return FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, CellCashAccount(CellID)));
	}

	int64 FV17BatchSlicePrototype::GetTreasuryAvailable(const EKingdom Kingdom) const
	{
		return FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, TreasuryAccount(Kingdom)));
	}

	int32 FV17BatchSlicePrototype::GetPendingParticipantCount() const
	{
		int32 Count = 0;
		for (const TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
		{
			Count += Pair.Value.Status == ESimulationEventState::Pending ? Pair.Value.GrantedCount : 0;
		}
		return Count;
	}

	int32 FV17BatchSlicePrototype::GetPendingEventCount() const
	{
		return Scheduler.NumPending();
	}

	int32 FV17BatchSlicePrototype::GetCompletedEventCount() const
	{
		int32 Count = 0;
		for (const TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
		{
			Count += Pair.Value.Status == ESimulationEventState::Completed ? 1 : 0;
		}
		return Count;
	}

	int64 FV17BatchSlicePrototype::GetParticipantWeightedActionCount(const EIndividualAction Action) const
	{
		int64 Count = 0;
		for (const TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
		{
			Count += Pair.Value.Action == Action ? Pair.Value.GrantedCount : 0;
		}
		return Count;
	}

	FV17BatchPrototypeAudit FV17BatchSlicePrototype::BuildAudit() const
	{
		FV17BatchPrototypeAudit Audit;
		int64 AccountedPopulation = GetPendingParticipantCount();
		for (const TPair<FV17JointCellID, int32>& Pair : ReadyCounts)
		{
			AccountedPopulation += Pair.Value;
			Audit.NegativeReadyCellCount += Pair.Value < 0 ? 1 : 0;
		}
		Audit.PopulationResidual = static_cast<int64>(InitialPopulation) - AccountedPopulation;

		int32 PendingBatchEvents = 0;
		for (const TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
		{
			const FV17BatchPrototypeEvent& BatchEvent = Pair.Value;
			Audit.RequestResultResidualCount += BatchEvent.RequestedCount
				== BatchEvent.GrantedCount + BatchEvent.RejectedCount ? 0 : 1;
			const FSimulationEventRecord* StoredEvent = EventStore.Find(BatchEvent.BatchEventID);
			Audit.EventParticipantResidualCount += StoredEvent != nullptr
				&& StoredEvent->Event.ParticipantCount == BatchEvent.ParticipantCount
				&& StoredEvent->State == BatchEvent.Status ? 0 : 1;
			PendingBatchEvents += BatchEvent.Status == ESimulationEventState::Pending ? 1 : 0;
		}
		Audit.PendingEventResidualCount = FMath::Abs(PendingBatchEvents - Scheduler.NumPending());
		Audit.DuplicateCompletionCount = EventStore.GetDuplicateCompletionCount();
		Audit.CommitResidueCount = CommitResidueCount;
		if (bWorkLedgerEnabled)
		{
			Audit.CoinResidual = Ledger.ComputeResidual(ESimulationResource::Coin);
			Audit.NegativeCoinStockCount = Ledger.CountNegativeStocks();
			Audit.DuplicateTransactionCount = Ledger.GetDuplicateTransactionCount();
			for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
			{
				const int64* InitialTreasury = InitialTreasuries.Find(Kingdom);
				Audit.TreasuryResidualCount += GetTreasuryAvailable(Kingdom)
					== (InitialTreasury != nullptr ? *InitialTreasury : 0) ? 0 : 1;
			}

			for (const TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
			{
				const FV17BatchPrototypeEvent& BatchEvent = Pair.Value;
				int32 MatchingTransactionCount = 0;
				const FLedgerTransaction* MatchingTransaction = nullptr;
				for (const FLedgerTransaction& Transaction : Ledger.GetTransactions())
				{
					if (Transaction.Transfer.EventID == BatchEvent.BatchEventID)
					{
						++MatchingTransactionCount;
						MatchingTransaction = &Transaction;
					}
				}

				if (BatchEvent.Action != EIndividualAction::Work)
				{
					Audit.WorkLedgerResidualCount += MatchingTransactionCount;
					continue;
				}

				const int32 ExpectedTransactionCount = BatchEvent.Status == ESimulationEventState::Completed ? 1 : 0;
				if (MatchingTransactionCount != ExpectedTransactionCount)
				{
					++Audit.WorkLedgerResidualCount;
					continue;
				}
				if (ExpectedTransactionCount == 1)
				{
					const FV17BatchPrototypeClaim* Claim = BatchClaims.Find(BatchEvent.BatchClaimID);
					const int64 ExpectedIncome = Claim != nullptr
						? static_cast<int64>(BatchEvent.ParticipantCount) * Claim->PerParticipantDemand
						: 0;
					const FLedgerTransferRequest& Transfer = MatchingTransaction->Transfer;
					if (Claim == nullptr
						|| Transfer.IdempotencyKey != FString::Printf(TEXT("V17-WORK-INCOME-%llu-0"), BatchEvent.BatchClaimID)
						|| Transfer.Source != ExternalBoundaryAccount
						|| Transfer.Destination != CellCashAccount(BatchEvent.SourceCellID)
						|| FMath::RoundToInt64(Transfer.Quantity) != ExpectedIncome
						|| !Transfer.bBoundaryFlow)
					{
						++Audit.WorkLedgerResidualCount;
					}
				}
			}
		}
		return Audit;
	}

	FString FV17BatchSlicePrototype::BuildDeterministicDigest() const
	{
		FString Canonical = FString::Printf(
			TEXT("Seed=%d|Time=%lld|Population=%d|NextArrive=%lld|CommitResidue=%d|"),
			Seed,
			Clock.Now().Minutes,
			InitialPopulation,
			Scheduler.GetNextArriveID(),
			CommitResidueCount);
		TArray<FV17JointCellID> CellIDs;
		ReadyCounts.GetKeys(CellIDs);
		CellIDs.Sort();
		for (const FV17JointCellID CellID : CellIDs)
		{
			Canonical += FString::Printf(TEXT("C=%llu,%d|"), CellID, ReadyCounts.FindChecked(CellID));
		}
		if (bWorkLedgerEnabled)
		{
			Canonical += TEXT("WorkLedger=1|");
			for (const FV17JointCellID CellID : CellIDs)
			{
				const FV17BatchPrototypeCell& Cell = CellDefinitions.FindChecked(CellID);
				Canonical += FString::Printf(
					TEXT("CM=%llu,%d,%d,%lld|"),
					CellID,
					static_cast<int32>(Cell.Kingdom),
					static_cast<int32>(Cell.IncomeBand),
					Cell.InitialCash);
			}

			TArray<FResourceAccountKey> BalanceKeys;
			Ledger.GetBalances().GetKeys(BalanceKeys);
			BalanceKeys.Sort([](const FResourceAccountKey& Left, const FResourceAccountKey& Right)
			{
				if (Left.Resource != Right.Resource)
				{
					return static_cast<uint8>(Left.Resource) < static_cast<uint8>(Right.Resource);
				}
				return Left.Account.Compare(Right.Account, ESearchCase::CaseSensitive) < 0;
			});
			for (const FResourceAccountKey& Key : BalanceKeys)
			{
				Canonical += FString::Printf(
					TEXT("L=%d,%s,%lld|"),
					static_cast<int32>(Key.Resource),
					*Key.Account,
					FMath::RoundToInt64(Ledger.GetBalances().FindChecked(Key)));
			}
			for (const FLedgerTransaction& Transaction : Ledger.GetTransactions())
			{
				const FLedgerTransferRequest& Transfer = Transaction.Transfer;
				Canonical += FString::Printf(
					TEXT("T=%lld,%s,%lld,%d,%s,%s,%lld,%d,%lld,%lld,%lld|"),
					Transaction.TransactionID,
					*Transfer.IdempotencyKey,
					Transfer.GameTime.Minutes,
					static_cast<int32>(Transfer.Resource),
					*Transfer.Source,
					*Transfer.Destination,
					FMath::RoundToInt64(Transfer.Quantity),
					Transfer.bBoundaryFlow ? 1 : 0,
					Transfer.EventID,
					Transfer.ArriveID,
					Transfer.PolicyID);
			}
			Canonical += FString::Printf(TEXT("LedgerDup=%d|"), Ledger.GetDuplicateTransactionCount());
		}

		TArray<FV17BatchClaimID> ClaimIDs;
		BatchClaims.GetKeys(ClaimIDs);
		ClaimIDs.Sort();
		for (const FV17BatchClaimID ClaimID : ClaimIDs)
		{
			const FV17BatchPrototypeClaim& Claim = BatchClaims.FindChecked(ClaimID);
			Canonical += FString::Printf(
				TEXT("Q=%llu,%lld,%s,%d,%llu,%d,%d,%llu,%llu|"),
				Claim.BatchClaimID,
				Claim.GameTime.Minutes,
				*Claim.ResourceScope,
				static_cast<int32>(Claim.Action),
				Claim.SourceCellID,
				Claim.RequestedCount,
				Claim.PerParticipantDemand,
				Claim.CausalPolicyID,
				Claim.StableOrderKey);
		}

		TArray<FEventID> EventIDs;
		BatchEvents.GetKeys(EventIDs);
		EventIDs.Sort();
		for (const FEventID EventID : EventIDs)
		{
			const FV17BatchPrototypeEvent& Event = BatchEvents.FindChecked(EventID);
			Canonical += FString::Printf(
				TEXT("E=%lld,%lld,%llu,%llu,%llu,%d,%d,%d,%d,%d,%lld,%lld,%lld,%llu,%llu,%llu,%d|"),
				Event.BatchEventID,
				Event.ParentBatchEventID,
				Event.BatchClaimID,
				Event.SourceCellID,
				Event.TargetCellID,
				static_cast<int32>(Event.Action),
				Event.ParticipantCount,
				Event.RequestedCount,
				Event.GrantedCount,
				Event.RejectedCount,
				Event.StartTime.Minutes,
				Event.EndTime.Minutes,
				Event.RemainingWorkMinutes,
				Event.BatchReservationID,
				Event.CausalPolicyID,
				Event.InheritedOrderKey,
				static_cast<int32>(Event.Status));
		}

		TArray<FEventID> StoredEventIDs;
		EventStore.GetEvents().GetKeys(StoredEventIDs);
		StoredEventIDs.Sort();
		for (const FEventID EventID : StoredEventIDs)
		{
			const FSimulationEventRecord& Stored = EventStore.GetEvents().FindChecked(EventID);
			Canonical += FString::Printf(
				TEXT("S=%lld,%s,%s,%d,%lld,%lld,%lld,%d,%d|"),
				Stored.EventID,
				*Stored.Event.Type,
				*Stored.Event.Owner,
				Stored.Event.ActionCode,
				Stored.Event.StartTime.Minutes,
				Stored.Event.EndTime.Minutes,
				Stored.Event.ArriveID,
				Stored.Event.ParticipantCount,
				static_cast<int32>(Stored.State));
		}

		TArray<FScheduledEvent> PendingEvents = Scheduler.GetPendingEvents();
		PendingEvents.Sort([](const FScheduledEvent& Left, const FScheduledEvent& Right)
		{
			if (!(Left.ExecuteAt == Right.ExecuteAt)) return Left.ExecuteAt < Right.ExecuteAt;
			if (Left.ArriveID != Right.ArriveID) return Left.ArriveID < Right.ArriveID;
			return Left.EventID < Right.EventID;
		});
		for (const FScheduledEvent& Pending : PendingEvents)
		{
			Canonical += FString::Printf(
				TEXT("P=%lld,%lld,%lld|"),
				Pending.EventID,
				Pending.ArriveID,
				Pending.ExecuteAt.Minutes);
		}
		const FTCHARToUTF8 Utf8(*Canonical);
		return FSHA1::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length())).ToString();
	}
}
