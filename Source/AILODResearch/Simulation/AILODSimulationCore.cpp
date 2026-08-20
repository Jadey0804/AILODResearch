// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODSimulationCore.h"

namespace AILOD
{
	FSimulationTime FSimulationTime::FromMinutes(const int64 Value)
	{
		return { Value };
	}

	FSimulationTime FSimulationTime::FromHours(const int64 Value)
	{
		return FromMinutes(Value * MinutesPerHour);
	}

	FSimulationTime FSimulationTime::FromDays(const int64 Value)
	{
		return FromMinutes(Value * MinutesPerDay);
	}

	FString FSimulationTime::ToString() const
	{
		const bool bNegative = Minutes < 0;
		const int64 AbsoluteMinutes = FMath::Abs(Minutes);
		const int64 Day = AbsoluteMinutes / MinutesPerDay;
		const int64 MinuteWithinDay = AbsoluteMinutes % MinutesPerDay;
		const int64 Hour = MinuteWithinDay / MinutesPerHour;
		const int64 Minute = MinuteWithinDay % MinutesPerHour;
		return FString::Printf(TEXT("D%s%02lldT%02lld:%02lld"), bNegative ? TEXT("-") : TEXT(""), Day, Hour, Minute);
	}

	FSimulationClock::FSimulationClock(const FSimulationTime StartTime)
		: CurrentTime(StartTime)
	{
	}

	bool FSimulationClock::AdvanceTo(const FSimulationTime TargetTime, FString& OutError)
	{
		if (TargetTime < CurrentTime)
		{
			OutError = TEXT("Authoritative simulation time cannot move backwards.");
			return false;
		}

		CurrentTime = TargetTime;
		OutError.Reset();
		return true;
	}

	FArriveID FSimulationScheduler::IssueArriveID()
	{
		return NextArriveID++;
	}

	bool FSimulationScheduler::Schedule(const FScheduledEvent& Event, const FSimulationTime CurrentTime, FString& OutError)
	{
		if (Event.EventID <= 0 || Event.ArriveID <= 0)
		{
			OutError = TEXT("Scheduled events require non-zero EventID and ArriveID.");
			return false;
		}
		if (Event.ExecuteAt < CurrentTime)
		{
			OutError = TEXT("A scheduled event cannot be placed before the current simulation time.");
			return false;
		}
		if (PendingEvents.ContainsByPredicate([&Event](const FScheduledEvent& Existing)
		{
			return Existing.EventID == Event.EventID;
		}))
		{
			OutError = TEXT("EventID is already pending in the scheduler.");
			return false;
		}

		PendingEvents.Add(Event);
		OutError.Reset();
		return true;
	}

	bool FSimulationScheduler::RemovePending(
		const FEventID EventID,
		FScheduledEvent& OutRemoved,
		FString& OutError)
	{
		const int32 Index = PendingEvents.IndexOfByPredicate([EventID](const FScheduledEvent& Event)
		{
			return Event.EventID == EventID;
		});
		if (Index == INDEX_NONE)
		{
			OutError = TEXT("Pending scheduler event does not exist.");
			return false;
		}
		OutRemoved = PendingEvents[Index];
		PendingEvents.RemoveAt(Index, 1, EAllowShrinking::No);
		OutError.Reset();
		return true;
	}

	void FSimulationScheduler::PopDueThrough(const FSimulationTime Time, TArray<FScheduledEvent>& OutEvents)
	{
		PendingEvents.Sort([](const FScheduledEvent& Left, const FScheduledEvent& Right)
		{
			if (!(Left.ExecuteAt == Right.ExecuteAt))
			{
				return Left.ExecuteAt < Right.ExecuteAt;
			}
			if (Left.ArriveID != Right.ArriveID)
			{
				return Left.ArriveID < Right.ArriveID;
			}
			return Left.EventID < Right.EventID;
		});

		int32 DueCount = 0;
		while (DueCount < PendingEvents.Num() && PendingEvents[DueCount].ExecuteAt <= Time)
		{
			++DueCount;
		}
		OutEvents.Append(PendingEvents.GetData(), DueCount);
		PendingEvents.RemoveAt(0, DueCount, EAllowShrinking::No);
	}

	int32 FResourceLedger::ResourceIndex(const ESimulationResource Resource)
	{
		return Resource == ESimulationResource::Wood ? 0 : 1;
	}

	bool FResourceLedger::InitializeAccount(
		const ESimulationResource Resource,
		const FString& Account,
		const double Quantity,
		FString& OutError)
	{
		if (bInitialStateSealed)
		{
			OutError = TEXT("Initial ledger state is already sealed.");
			return false;
		}
		if (Account.IsEmpty() || Account == ExternalBoundaryAccount || !FMath::IsFinite(Quantity) || Quantity < 0.0)
		{
			OutError = TEXT("Initial ledger account and quantity are invalid.");
			return false;
		}
		if (Resource == ESimulationResource::Coin
			&& !FMath::IsNearlyEqual(Quantity, static_cast<double>(FMath::RoundToInt64(Quantity))))
		{
			OutError = TEXT("Coin balances must be integers.");
			return false;
		}

		const FResourceAccountKey Key{ Resource, Account };
		if (Balances.Contains(Key))
		{
			OutError = TEXT("Initial ledger account is already defined.");
			return false;
		}

		Balances.Add(Key, Quantity);
		OutError.Reset();
		return true;
	}

	void FResourceLedger::SealInitialState()
	{
		if (bInitialStateSealed)
		{
			return;
		}
		InitialTotals[0] = GetCurrentTotal(ESimulationResource::Wood);
		InitialTotals[1] = GetCurrentTotal(ESimulationResource::Coin);
		bInitialStateSealed = true;
	}

	bool FResourceLedger::SubmitTransfer(
		const FLedgerTransferRequest& Request,
		FTransactionID& OutTransactionID,
		FString& OutError)
	{
		OutTransactionID = 0;
		if (!bInitialStateSealed)
		{
			OutError = TEXT("Initial ledger state must be sealed before submitting transactions.");
			return false;
		}
		if (Request.IdempotencyKey.IsEmpty() || Request.Source.IsEmpty() || Request.Destination.IsEmpty()
			|| Request.Source == Request.Destination || !FMath::IsFinite(Request.Quantity) || Request.Quantity <= 0.0)
		{
			OutError = TEXT("Ledger transfer request is invalid.");
			return false;
		}
		if (Request.Resource == ESimulationResource::Coin
			&& !FMath::IsNearlyEqual(Request.Quantity, static_cast<double>(FMath::RoundToInt64(Request.Quantity))))
		{
			OutError = TEXT("Coin transfer quantities must be integers.");
			return false;
		}
		if (CommittedIdempotencyKeys.Contains(Request.IdempotencyKey))
		{
			++DuplicateTransactionCount;
			OutError = TEXT("IdempotencyKey has already committed a transaction.");
			return false;
		}

		const bool bSourceIsBoundary = Request.Source == ExternalBoundaryAccount;
		const bool bDestinationIsBoundary = Request.Destination == ExternalBoundaryAccount;
		if (Request.bBoundaryFlow != (bSourceIsBoundary != bDestinationIsBoundary))
		{
			OutError = TEXT("BoundaryFlow must have exactly one ExternalBoundary endpoint.");
			return false;
		}

		if (!bSourceIsBoundary)
		{
			const double SourceBalance = GetBalance(Request.Resource, Request.Source);
			if (SourceBalance + UE_DOUBLE_SMALL_NUMBER < Request.Quantity)
			{
				OutError = TEXT("Ledger transfer would make the source stock negative.");
				return false;
			}
			Balances.FindOrAdd({ Request.Resource, Request.Source }) = FMath::Max(0.0, SourceBalance - Request.Quantity);
		}

		if (!bDestinationIsBoundary)
		{
			Balances.FindOrAdd({ Request.Resource, Request.Destination }) += Request.Quantity;
		}

		const int32 Index = ResourceIndex(Request.Resource);
		if (bSourceIsBoundary)
		{
			BoundaryInTotals[Index] += Request.Quantity;
		}
		if (bDestinationIsBoundary)
		{
			BoundaryOutTotals[Index] += Request.Quantity;
		}

		OutTransactionID = NextTransactionID++;
		CommittedIdempotencyKeys.Add(Request.IdempotencyKey);
		Transactions.Add({ OutTransactionID, Request });
		OutError.Reset();
		return true;
	}

	bool FResourceLedger::RemoveZeroBalanceAccount(
		const ESimulationResource Resource,
		const FString& Account,
		FString& OutError)
	{
		const FResourceAccountKey Key{ Resource, Account };
		const double* Balance = Balances.Find(Key);
		if (Balance == nullptr)
		{
			OutError.Reset();
			return true;
		}
		if (!FMath::IsNearlyZero(*Balance, UE_DOUBLE_SMALL_NUMBER))
		{
			OutError = TEXT("Only an empty runtime Ledger account can be removed.");
			return false;
		}
		Balances.Remove(Key);
		OutError.Reset();
		return true;
	}

	double FResourceLedger::GetBalance(const ESimulationResource Resource, const FString& Account) const
	{
		if (const double* Balance = Balances.Find({ Resource, Account }))
		{
			return *Balance;
		}
		return 0.0;
	}

	double FResourceLedger::GetCurrentTotal(const ESimulationResource Resource) const
	{
		double Total = 0.0;
		for (const TPair<FResourceAccountKey, double>& Pair : Balances)
		{
			if (Pair.Key.Resource == Resource)
			{
				Total += Pair.Value;
			}
		}
		return Total;
	}

	double FResourceLedger::ComputeResidual(const ESimulationResource Resource) const
	{
		const int32 Index = ResourceIndex(Resource);
		return InitialTotals[Index] + BoundaryInTotals[Index] - BoundaryOutTotals[Index] - GetCurrentTotal(Resource);
	}

	int32 FResourceLedger::CountNegativeStocks() const
	{
		int32 Count = 0;
		for (const TPair<FResourceAccountKey, double>& Pair : Balances)
		{
			if (Pair.Value < -UE_DOUBLE_SMALL_NUMBER)
			{
				++Count;
			}
		}
		return Count;
	}

	bool FReservationStore::CreateReservation(
		const FReservationRequest& Request,
		FResourceLedger& Ledger,
		FReservationID& OutReservationID,
		FString& OutError)
	{
		OutReservationID = 0;
		FLedgerTransferRequest Transfer;
		Transfer.IdempotencyKey = Request.IdempotencyKey;
		Transfer.GameTime = Request.GameTime;
		Transfer.Resource = Request.Resource;
		Transfer.Source = Request.SourceAccount;
		Transfer.Destination = Request.ReservedAccount;
		Transfer.Quantity = Request.Quantity;
		Transfer.EventID = Request.EventID;
		Transfer.ArriveID = Request.ArriveID;
		Transfer.PolicyID = Request.PolicyID;

		FTransactionID TransactionID = 0;
		if (!Ledger.SubmitTransfer(Transfer, TransactionID, OutError))
		{
			return false;
		}

		OutReservationID = NextReservationID++;
		Reservations.Add(OutReservationID, { OutReservationID, Request, EReservationState::Active });
		return true;
	}

	bool FReservationStore::CommitReservation(
		const FReservationID ReservationID,
		const FString& DestinationAccount,
		const FIdempotencyKey& IdempotencyKey,
		const FSimulationTime GameTime,
		FResourceLedger& Ledger,
		FString& OutError)
	{
		FReservationRecord* Reservation = Reservations.Find(ReservationID);
		if (Reservation == nullptr || Reservation->State != EReservationState::Active)
		{
			OutError = TEXT("Reservation is not active.");
			return false;
		}

		FLedgerTransferRequest Transfer;
		Transfer.IdempotencyKey = IdempotencyKey;
		Transfer.GameTime = GameTime;
		Transfer.Resource = Reservation->Request.Resource;
		Transfer.Source = Reservation->Request.ReservedAccount;
		Transfer.Destination = DestinationAccount;
		Transfer.Quantity = Reservation->Request.Quantity;
		Transfer.EventID = Reservation->Request.EventID;
		Transfer.ArriveID = Reservation->Request.ArriveID;
		Transfer.PolicyID = Reservation->Request.PolicyID;
		Transfer.bBoundaryFlow = DestinationAccount == ExternalBoundaryAccount;

		FTransactionID TransactionID = 0;
		if (!Ledger.SubmitTransfer(Transfer, TransactionID, OutError))
		{
			return false;
		}
		Reservation->State = EReservationState::Committed;
		return true;
	}

	bool FReservationStore::ReleaseReservation(
		const FReservationID ReservationID,
		const FIdempotencyKey& IdempotencyKey,
		const FSimulationTime GameTime,
		FResourceLedger& Ledger,
		FString& OutError)
	{
		FReservationRecord* Reservation = Reservations.Find(ReservationID);
		if (Reservation == nullptr || Reservation->State != EReservationState::Active)
		{
			OutError = TEXT("Reservation is not active.");
			return false;
		}

		FLedgerTransferRequest Transfer;
		Transfer.IdempotencyKey = IdempotencyKey;
		Transfer.GameTime = GameTime;
		Transfer.Resource = Reservation->Request.Resource;
		Transfer.Source = Reservation->Request.ReservedAccount;
		Transfer.Destination = Reservation->Request.SourceAccount;
		Transfer.Quantity = Reservation->Request.Quantity;
		Transfer.EventID = Reservation->Request.EventID;
		Transfer.ArriveID = Reservation->Request.ArriveID;
		Transfer.PolicyID = Reservation->Request.PolicyID;

		FTransactionID TransactionID = 0;
		if (!Ledger.SubmitTransfer(Transfer, TransactionID, OutError))
		{
			return false;
		}
		Reservation->State = EReservationState::Released;
		return true;
	}

	bool FReservationStore::SplitReservation(
		const FReservationID ParentReservationID,
		const FEventID ChildEventID,
		const FArriveID ChildArriveID,
		const double Quantity,
		FReservationID& OutChildReservationID,
		FString& OutError)
	{
		OutChildReservationID = 0;
		FReservationRecord* Parent = Reservations.Find(ParentReservationID);
		if (Parent == nullptr
			|| Parent->State != EReservationState::Active
			|| ChildEventID <= 0
			|| ChildArriveID <= 0
			|| !FMath::IsFinite(Quantity)
			|| Quantity <= 0.0
			|| Parent->Request.Quantity <= Quantity)
		{
			OutError = TEXT("An active reservation can only split a positive proper subset into a valid child event.");
			return false;
		}

		FReservationRequest ChildRequest = Parent->Request;
		ChildRequest.IdempotencyKey = FString::Printf(
			TEXT("%s-SPLIT-%lld"),
			*Parent->Request.IdempotencyKey,
			ChildEventID);
		ChildRequest.Quantity = Quantity;
		ChildRequest.EventID = ChildEventID;
		ChildRequest.ArriveID = ChildArriveID;
		Parent->Request.Quantity -= Quantity;
		OutChildReservationID = NextReservationID++;
		Reservations.Add(
			OutChildReservationID,
			{ OutChildReservationID, MoveTemp(ChildRequest), EReservationState::Active });
		OutError.Reset();
		return true;
	}

	bool FReservationStore::MergeReservations(
		const FReservationID TargetReservationID,
		const FReservationID SourceReservationID,
		FString& OutError)
	{
		FReservationRecord* Target = Reservations.Find(TargetReservationID);
		FReservationRecord* Source = Reservations.Find(SourceReservationID);
		if (Target == nullptr
			|| Source == nullptr
			|| TargetReservationID == SourceReservationID
			|| Target->State != EReservationState::Active
			|| Source->State != EReservationState::Active
			|| Target->Request.Resource != Source->Request.Resource
			|| Target->Request.SourceAccount != Source->Request.SourceAccount
			|| Target->Request.ReservedAccount != Source->Request.ReservedAccount
			|| Target->Request.PolicyID != Source->Request.PolicyID)
		{
			OutError = TEXT("Only compatible active reservations can merge.");
			return false;
		}

		Target->Request.Quantity += Source->Request.Quantity;
		Source->State = EReservationState::Merged;
		OutError.Reset();
		return true;
	}

	const FReservationRecord* FReservationStore::Find(const FReservationID ReservationID) const
	{
		return Reservations.Find(ReservationID);
	}

	bool FSimulationEventStore::CreateEvent(
		const FSimulationEventRequest& Request,
		FEventID& OutEventID,
		FString& OutError)
	{
		OutEventID = 0;
		if (Request.Type.IsEmpty() || Request.Owner.IsEmpty() || Request.EndTime < Request.StartTime)
		{
			OutError = TEXT("Simulation event request is invalid.");
			return false;
		}

		OutEventID = NextEventID++;
		Events.Add(OutEventID, { OutEventID, Request, ESimulationEventState::Pending });
		OutError.Reset();
		return true;
	}

	bool FSimulationEventStore::TransferOwner(
		const FEventID EventID,
		const FString& ExpectedOwner,
		const FString& NewOwner,
		FString& OutError)
	{
		FSimulationEventRecord* Record = Events.Find(EventID);
		if (Record == nullptr || NewOwner.IsEmpty())
		{
			OutError = TEXT("Event or new owner is invalid.");
			return false;
		}
		if (Record->Event.Owner != ExpectedOwner)
		{
			++OwnerConflictCount;
			OutError = TEXT("Event owner does not match the expected owner.");
			return false;
		}

		Record->Event.Owner = NewOwner;
		OutError.Reset();
		return true;
	}

	bool FSimulationEventStore::ConvertPendingEventToAggregate(
		const FEventID EventID,
		const FString& ExpectedOwner,
		const FString& NewOwner,
		FString& OutError)
	{
		FSimulationEventRecord* Record = Events.Find(EventID);
		if (Record == nullptr || Record->State != ESimulationEventState::Pending || NewOwner.IsEmpty())
		{
			OutError = TEXT("Pending event or aggregate owner is invalid.");
			return false;
		}
		if (Record->Event.Owner != ExpectedOwner)
		{
			++OwnerConflictCount;
			OutError = TEXT("Pending event owner does not match the expected resident owner.");
			return false;
		}

		Record->Event.Owner = NewOwner;
		Record->Event.ResidentID = 0;
		OutError.Reset();
		return true;
	}

	bool FSimulationEventStore::ConvertPendingEventToIndividual(
		const FEventID EventID,
		const FString& ExpectedOwner,
		const FString& NewOwner,
		const FResidentID ResidentID,
		FString& OutError)
	{
		FSimulationEventRecord* Record = Events.Find(EventID);
		if (Record == nullptr
			|| Record->State != ESimulationEventState::Pending
			|| NewOwner.IsEmpty()
			|| ResidentID <= 0)
		{
			OutError = TEXT("Pending aggregate event or individual owner is invalid.");
			return false;
		}
		if (Record->Event.Owner != ExpectedOwner)
		{
			++OwnerConflictCount;
			OutError = TEXT("Pending event owner does not match the expected aggregate owner.");
			return false;
		}

		Record->Event.Owner = NewOwner;
		Record->Event.ResidentID = ResidentID;
		OutError.Reset();
		return true;
	}

	bool FSimulationEventStore::SetPendingParticipantCount(
		const FEventID EventID,
		const int32 ParticipantCount,
		FString& OutError)
	{
		FSimulationEventRecord* Record = Events.Find(EventID);
		if (Record == nullptr
			|| Record->State != ESimulationEventState::Pending
			|| ParticipantCount <= 0)
		{
			OutError = TEXT("Only a pending event can receive a positive participant count.");
			return false;
		}
		Record->Event.ParticipantCount = ParticipantCount;
		OutError.Reset();
		return true;
	}

	bool FSimulationEventStore::CompleteEvent(const FEventID EventID, FString& OutError)
	{
		FSimulationEventRecord* Record = Events.Find(EventID);
		if (Record == nullptr)
		{
			OutError = TEXT("Event does not exist.");
			return false;
		}
		if (Record->State == ESimulationEventState::Completed)
		{
			++DuplicateCompletionCount;
			OutError = TEXT("Event has already completed.");
			return false;
		}

		Record->State = ESimulationEventState::Completed;
		OutError.Reset();
		return true;
	}

	bool FSimulationEventStore::SetReservationID(
		const FEventID EventID,
		const FReservationID ReservationID,
		FString& OutError)
	{
		FSimulationEventRecord* Record = Events.Find(EventID);
		if (Record == nullptr || ReservationID <= 0)
		{
			OutError = TEXT("Event or ReservationID is invalid.");
			return false;
		}
		Record->Event.ReservationID = ReservationID;
		OutError.Reset();
		return true;
	}

	bool FSimulationEventStore::RemoveCompletedEvent(const FEventID EventID, FString& OutError)
	{
		const FSimulationEventRecord* Record = Events.Find(EventID);
		if (Record == nullptr || Record->State != ESimulationEventState::Completed)
		{
			OutError = TEXT("Only a completed event can be removed from retained event state.");
			return false;
		}
		Events.Remove(EventID);
		OutError.Reset();
		return true;
	}

	bool FSimulationEventStore::RemovePendingEvent(const FEventID EventID, FString& OutError)
	{
		const FSimulationEventRecord* Record = Events.Find(EventID);
		if (Record == nullptr || Record->State != ESimulationEventState::Pending)
		{
			OutError = TEXT("Only a pending event can be removed from retained event state.");
			return false;
		}
		Events.Remove(EventID);
		OutError.Reset();
		return true;
	}

	const FSimulationEventRecord* FSimulationEventStore::Find(const FEventID EventID) const
	{
		return Events.Find(EventID);
	}

	bool FConservationAudit::IsHardErrorFree() const
	{
		return PopulationResidual == 0
			&& FMath::IsNearlyZero(WoodResidual, UE_DOUBLE_SMALL_NUMBER)
			&& NegativeStockCount == 0
			&& DuplicateTransactionCount == 0
			&& EventOwnerConflictCount == 0
			&& DuplicateCompletionCount == 0;
	}

	FConservationAudit AuditConservation(
		const FPopulationState& Population,
		const FResourceLedger& Ledger,
		const FSimulationEventStore& EventStore)
	{
		FConservationAudit Result;
		Result.PopulationResidual = Population.Total
			- Population.Anonymous
			- Population.PersistentMacro
			- Population.ActiveMicro;
		Result.WoodResidual = Ledger.ComputeResidual(ESimulationResource::Wood);
		if (FMath::IsNearlyZero(Result.WoodResidual, 1.e-6))
		{
			Result.WoodResidual = 0.0;
		}
		Result.NegativeStockCount = Ledger.CountNegativeStocks();
		Result.DuplicateTransactionCount = Ledger.GetDuplicateTransactionCount();
		Result.EventOwnerConflictCount = EventStore.GetOwnerConflictCount();
		Result.DuplicateCompletionCount = EventStore.GetDuplicateCompletionCount();
		return Result;
	}

	bool FEmptyScenarioRunner::Run60Days(
		const int32 PopulationPerKingdom,
		FEmptyScenarioResult& OutResult,
		FString& OutError)
	{
		if (PopulationPerKingdom <= 0)
		{
			OutError = TEXT("Population per kingdom must be positive.");
			return false;
		}

		FResourceLedger Ledger;
		const double InitialForestWood = 16.0 * PopulationPerKingdom;
		const double InitialMarketWood = 2.0 * PopulationPerKingdom;
		const double InitialTreasury = 5.0 * PopulationPerKingdom;
		const struct
		{
			ESimulationResource Resource;
			const TCHAR* Account;
			double Quantity;
		} InitialAccounts[] =
		{
			{ ESimulationResource::Wood, TEXT("A.ForestWood"), InitialForestWood },
			{ ESimulationResource::Wood, TEXT("A.MarketWoodAvailable"), InitialMarketWood },
			{ ESimulationResource::Wood, TEXT("B.ForestWood"), InitialForestWood },
			{ ESimulationResource::Wood, TEXT("B.MarketWoodAvailable"), InitialMarketWood },
			{ ESimulationResource::Coin, TEXT("A.TreasuryAvailable"), InitialTreasury },
			{ ESimulationResource::Coin, TEXT("B.TreasuryAvailable"), InitialTreasury }
		};

		for (const auto& InitialAccount : InitialAccounts)
		{
			if (!Ledger.InitializeAccount(InitialAccount.Resource, InitialAccount.Account, InitialAccount.Quantity, OutError))
			{
				return false;
			}
		}
		Ledger.SealInitialState();

		FSimulationClock Clock;
		FSimulationScheduler Scheduler;
		for (int32 Hour = 1; Hour <= 60 * HoursPerDay; ++Hour)
		{
			if (!Clock.AdvanceTo(FSimulationTime::FromHours(Hour), OutError))
			{
				return false;
			}
			TArray<FScheduledEvent> DueEvents;
			Scheduler.PopDueThrough(Clock.Now(), DueEvents);
		}

		FSimulationEventStore EventStore;
		OutResult.FinalTime = Clock.Now();
		OutResult.HourSteps = 60 * HoursPerDay;
		OutResult.Population.Total = PopulationPerKingdom * 2;
		OutResult.Population.Anonymous = OutResult.Population.Total;
		OutResult.Audit = AuditConservation(OutResult.Population, Ledger, EventStore);
		OutError.Reset();
		return true;
	}
}
