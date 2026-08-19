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
			&& CommitResidueCount == 0;
	}

	FV17NoResourceBatchPrototype::FV17NoResourceBatchPrototype(const int32 InSeed)
		: Seed(InSeed)
	{
	}

	bool FV17NoResourceBatchPrototype::Initialize(
		const TArray<FV17BatchPrototypeCell>& Cells,
		const FSimulationTime StartTime,
		FString& OutError)
	{
		if (bInitialized || Cells.IsEmpty())
		{
			OutError = TEXT("The no-resource batch prototype requires one initialization with at least one cell.");
			return false;
		}

		TMap<FV17JointCellID, int32> NewReadyCounts;
		int32 NewPopulation = 0;
		for (const FV17BatchPrototypeCell& Cell : Cells)
		{
			if (Cell.CellID == 0 || Cell.ReadyCount < 0 || NewReadyCounts.Contains(Cell.CellID))
			{
				OutError = TEXT("Batch prototype cells require unique non-zero IDs and non-negative counts.");
				return false;
			}
			NewReadyCounts.Add(Cell.CellID, Cell.ReadyCount);
			NewPopulation += Cell.ReadyCount;
		}
		if (NewPopulation <= 0)
		{
			OutError = TEXT("The no-resource batch prototype requires a positive population.");
			return false;
		}

		ReadyCounts = MoveTemp(NewReadyCounts);
		InitialPopulation = NewPopulation;
		Clock = FSimulationClock(StartTime);
		bInitialized = true;
		OutError.Reset();
		return true;
	}

	FV17BatchClaimID FV17NoResourceBatchPrototype::BuildClaimID(
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

	bool FV17NoResourceBatchPrototype::SubmitBatch(
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
		if (Action != EIndividualAction::Routine && Action != EIndividualAction::Wait)
		{
			OutError = TEXT("Phase 6G-B2A only accepts Routine and Wait batches.");
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
		Request.Type = Action == EIndividualAction::Routine ? TEXT("BatchRoutine") : TEXT("BatchWait");
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
		BatchClaim.ResourceScope = TEXT("None");
		BatchClaim.Action = Action;
		BatchClaim.SourceCellID = SourceCellID;
		BatchClaim.RequestedCount = ParticipantCount;
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

	bool FV17NoResourceBatchPrototype::AdvanceTo(const FSimulationTime TargetTime, FString& OutError)
	{
		if (!bInitialized || TargetTime < Clock.Now())
		{
			OutError = TEXT("The initialized batch prototype cannot move backwards in time.");
			return false;
		}

		const FSimulationClock ClockBefore = Clock;
		const FSimulationScheduler SchedulerBefore = Scheduler;
		const FSimulationEventStore EventStoreBefore = EventStore;
		const TMap<FV17JointCellID, int32> ReadyCountsBefore = ReadyCounts;
		const TMap<FEventID, FV17BatchPrototypeEvent> BatchEventsBefore = BatchEvents;
		TArray<FScheduledEvent> DueEvents;
		Scheduler.PopDueThrough(TargetTime, DueEvents);
		for (const FScheduledEvent& Due : DueEvents)
		{
			FV17BatchPrototypeEvent* BatchEvent = BatchEvents.Find(Due.EventID);
			const FSimulationEventRecord* StoredEvent = EventStore.Find(Due.EventID);
			if (BatchEvent == nullptr || BatchEvent->Status != ESimulationEventState::Pending
				|| StoredEvent == nullptr || StoredEvent->State != ESimulationEventState::Pending
				|| StoredEvent->Event.ParticipantCount != BatchEvent->GrantedCount
				|| !EventStore.CompleteEvent(Due.EventID, OutError))
			{
				Clock = ClockBefore;
				Scheduler = SchedulerBefore;
				EventStore = EventStoreBefore;
				ReadyCounts = ReadyCountsBefore;
				BatchEvents = BatchEventsBefore;
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
			Clock = ClockBefore;
			Scheduler = SchedulerBefore;
			EventStore = EventStoreBefore;
			ReadyCounts = ReadyCountsBefore;
			BatchEvents = BatchEventsBefore;
			return false;
		}
		OutError.Reset();
		return true;
	}

	int32 FV17NoResourceBatchPrototype::GetReadyCount(const FV17JointCellID CellID) const
	{
		const int32* Count = ReadyCounts.Find(CellID);
		return Count != nullptr ? *Count : 0;
	}

	int32 FV17NoResourceBatchPrototype::GetPendingParticipantCount() const
	{
		int32 Count = 0;
		for (const TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
		{
			Count += Pair.Value.Status == ESimulationEventState::Pending ? Pair.Value.GrantedCount : 0;
		}
		return Count;
	}

	int32 FV17NoResourceBatchPrototype::GetPendingEventCount() const
	{
		return Scheduler.NumPending();
	}

	int32 FV17NoResourceBatchPrototype::GetCompletedEventCount() const
	{
		int32 Count = 0;
		for (const TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
		{
			Count += Pair.Value.Status == ESimulationEventState::Completed ? 1 : 0;
		}
		return Count;
	}

	int64 FV17NoResourceBatchPrototype::GetParticipantWeightedActionCount(const EIndividualAction Action) const
	{
		int64 Count = 0;
		for (const TPair<FEventID, FV17BatchPrototypeEvent>& Pair : BatchEvents)
		{
			Count += Pair.Value.Action == Action ? Pair.Value.GrantedCount : 0;
		}
		return Count;
	}

	FV17BatchPrototypeAudit FV17NoResourceBatchPrototype::BuildAudit() const
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
		return Audit;
	}

	FString FV17NoResourceBatchPrototype::BuildDeterministicDigest() const
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
