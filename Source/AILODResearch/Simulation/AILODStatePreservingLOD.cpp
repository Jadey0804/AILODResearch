// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODStatePreservingLOD.h"

#include "AILODDomainRules.h"
#include "AILODPhase0Manifest.h"
#include "Containers/StringConv.h"
#include "Misc/SecureHash.h"

namespace AILOD
{
	namespace
	{
		const TCHAR* RepresentationName(const EResidentRepresentation Representation)
		{
			return Representation == EResidentRepresentation::ActiveMicro
				? TEXT("ActiveMicro")
				: TEXT("CohortManaged");
		}

		const TCHAR* TransitionResultName(const ELODTransitionResult Result)
		{
			switch (Result)
			{
			case ELODTransitionResult::Committed: return TEXT("Committed");
			case ELODTransitionResult::AlreadyInState: return TEXT("AlreadyInState");
			case ELODTransitionResult::ResidentNotFound: return TEXT("ResidentNotFound");
			case ELODTransitionResult::ActiveCapReached: return TEXT("ActiveCapReached");
			case ELODTransitionResult::EventOwnerConflict: return TEXT("EventOwnerConflict");
			case ELODTransitionResult::CohortCacheMismatch: return TEXT("CohortCacheMismatch");
			default: return TEXT("Unknown");
			}
		}

		int32 ResourceInt(
			const FResourceLedger& Ledger,
			const ESimulationResource Resource,
			const FString& Account)
		{
			return static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(Resource, Account)));
		}

		FString ResidentAccountName(const FResidentID ResidentID, const TCHAR* StockName)
		{
			return FString::Printf(TEXT("Resident.%lld.%s"), ResidentID, StockName);
		}

		void AddResidentToBucket(
			const FResidentCoreState& Resident,
			const FResourceLedger& Ledger,
			const FSimulationEventStore& EventStore,
			const FSimulationTime Time,
			FCohortBucket& Bucket)
		{
			const FString CashAccount = ResidentAccountName(Resident.ResidentID, TEXT("Cash"));
			const FString CreditAccount = ResidentAccountName(Resident.ResidentID, TEXT("RepairCredit"));
			const FString WoodAccount = ResidentAccountName(Resident.ResidentID, TEXT("Wood"));
			const int32 Cash = ResourceInt(Ledger, ESimulationResource::Coin, CashAccount);
			const int32 RepairCredit = ResourceInt(Ledger, ESimulationResource::Coin, CreditAccount);
			const int32 Wood = ResourceInt(Ledger, ESimulationResource::Wood, WoodAccount);

			++Bucket.PopulationCount;
			Bucket.CashSum += Cash;
			Bucket.CashSquaredSum += static_cast<int64>(Cash) * Cash;
			Bucket.RepairCreditSum += RepairCredit;
			++Bucket.WoodCounts[FMath::Clamp(Wood, 0, 4)];
			Bucket.AidReceivedCount += Resident.bAidReceived ? 1 : 0;
			Bucket.LastUpdateTime = Time;

			if (Resident.ActiveEventID != 0)
			{
				Bucket.EventBatchRefs.AddUnique(Resident.ActiveEventID);
				if (Resident.HomeState == EHomeState::UnderRepair)
				{
					const FSimulationEventRecord* Event = EventStore.Find(Resident.ActiveEventID);
					if (Event != nullptr)
					{
						const int64 Duration = FMath::Max<int64>(1, Event->Event.EndTime.Minutes - Event->Event.StartTime.Minutes);
						const int64 Elapsed = FMath::Clamp<int64>(Time.Minutes - Event->Event.StartTime.Minutes, 0, Duration);
						const int32 ProgressBin = FMath::Clamp(static_cast<int32>((Elapsed * 4) / Duration), 0, 3);
						++Bucket.RepairProgressBins[ProgressBin];
					}
				}
			}
		}

		bool BucketsEqual(const FCohortBucket& Left, const FCohortBucket& Right)
		{
			if (Left.PopulationCount != Right.PopulationCount
				|| Left.CashSum != Right.CashSum
				|| Left.CashSquaredSum != Right.CashSquaredSum
				|| Left.RepairCreditSum != Right.RepairCreditSum
				|| Left.AidEligibleCount != Right.AidEligibleCount
				|| Left.AidReceivedCount != Right.AidReceivedCount
				|| !(Left.LastUpdateTime == Right.LastUpdateTime)
				|| Left.RNGStreamKey != Right.RNGStreamKey)
			{
				return false;
			}

			for (int32 Index = 0; Index < 5; ++Index)
			{
				if (Left.WoodCounts[Index] != Right.WoodCounts[Index])
				{
					return false;
				}
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (Left.RepairProgressBins[Index] != Right.RepairProgressBins[Index])
				{
					return false;
				}
			}
			for (int32 Index = 0; Index < 6; ++Index)
			{
				if (!FMath::IsNearlyEqual(Left.ResidualFlows[Index], Right.ResidualFlows[Index]))
				{
					return false;
				}
			}

			TArray<FEventID> LeftEvents = Left.EventBatchRefs;
			TArray<FEventID> RightEvents = Right.EventBatchRefs;
			LeftEvents.Sort();
			RightEvents.Sort();
			return LeftEvents == RightEvents;
		}
	}

	bool FPhase4Audit::IsHardErrorFree() const
	{
		return Conservation.IsHardErrorFree()
			&& FMath::IsNearlyZero(CoinResidual, 1.e-6)
			&& CoreLedgerMismatchCount == 0
			&& IdentityMismatchCount == 0
			&& EventReferenceMismatchCount == 0
			&& CohortMismatchCount == 0
			&& ActiveCapViolationCount == 0;
	}

	bool FStatePreservingLODSystem::Initialize(const FPhase0Config& InConfig, FString& OutError)
	{
		Config = InConfig;
		PopulationManifest = {};
		DamageList = {};
		ContinuitySample = {};
		Residents.Reset();
		ResidentIndices.Reset();
		Cohorts.Reset();
		Scheduler = {};
		Ledger = {};
		Reservations = {};
		EventStore = {};
		Transitions.Reset();
		CompetitionHistory.Reset();
		IssuedActionRequests.Reset();
		ActiveActionRequestIDs.Reset();
		RepairCapacityDay = TNumericLimits<int32>::Min();
		RepairStartsRemaining = 0;
		CohortTime = FSimulationTime::FromDays(-7);
		Clock = FSimulationClock(CohortTime);

		if (!FPhase0ManifestGenerator::Generate(
			Config,
			PopulationManifest,
			DamageList,
			ContinuitySample,
			OutError))
		{
			return false;
		}

		Residents.Reserve(PopulationManifest.Residents.Num());
		for (const FInitialResidentRecord& Initial : PopulationManifest.Residents)
		{
			FResidentCoreState& Resident = Residents.AddDefaulted_GetRef();
			Resident.ResidentID = Initial.ResidentID;
			Resident.HomeID = Initial.HomeID;
			Resident.PersistentID = Initial.PersistentID;
			Resident.Name = Initial.Name;
			Resident.Kingdom = Initial.Kingdom;
			Resident.Profession = Initial.Profession;
			Resident.IncomeBand = Initial.IncomeBand;
			Resident.Cash = Initial.Cash;
			Resident.RepairCredit = Initial.RepairCredit;
			Resident.InventoryWood = Initial.InventoryWood;
			Resident.HomeState = Initial.HomeState;
			Resident.CurrentGoal = FIndividualDomain::SelectGoal(Resident);
			Resident.LastUpdateTime = CohortTime;
			Resident.RNGStreamKey = static_cast<uint32>(DomainRules::Mix64(
				DomainRules::Mix64(static_cast<uint64>(static_cast<uint32>(Config.Seed)))
				^ DomainRules::Mix64(static_cast<uint64>(Resident.ResidentID))));
			Resident.Representation = EResidentRepresentation::CohortManaged;
			ResidentIndices.Add(Resident.ResidentID, Residents.Num() - 1);
		}

		if (!InitializeLedger(OutError))
		{
			return false;
		}
		for (const FResidentCoreState& Resident : Residents)
		{
			AddToCohort(Resident, CohortTime);
		}

		const FPhase4Audit InitialAudit = Audit();
		if (!InitialAudit.IsHardErrorFree())
		{
			OutError = TEXT("Initial state-preserving LOD audit failed.");
			return false;
		}
		OutError.Reset();
		return true;
	}

	bool FStatePreservingLODSystem::InitializeLedger(FString& OutError)
	{
		for (const FResidentCoreState& Resident : Residents)
		{
			if (!Ledger.InitializeAccount(
				ESimulationResource::Coin,
				ResidentAccount(Resident.ResidentID, TEXT("Cash")),
				Resident.Cash,
				OutError)
				|| !Ledger.InitializeAccount(
					ESimulationResource::Coin,
					ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")),
					Resident.RepairCredit,
					OutError)
				|| !Ledger.InitializeAccount(
					ESimulationResource::Wood,
					ResidentAccount(Resident.ResidentID, TEXT("Wood")),
					Resident.InventoryWood,
					OutError))
			{
				return false;
			}
		}

		for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
		{
			if (!Ledger.InitializeAccount(
				ESimulationResource::Wood,
				MakeKingdomAccount(Kingdom, TEXT("WoodEmbeddedInRepairs")),
				0.0,
				OutError)
				|| !Ledger.InitializeAccount(
					ESimulationResource::Wood,
					MakeKingdomAccount(Kingdom, TEXT("WoodInRepairedHomes")),
					0.0,
					OutError))
			{
				return false;
			}
		}

		Ledger.SealInitialState();
		return true;
	}

	bool FStatePreservingLODSystem::ApplyEarthquakeDamage(const FSimulationTime Time, FString& OutError)
	{
		if (!AdvanceTo(Time, OutError))
		{
			return false;
		}
		for (const FEarthquakeDamageRecord& Damage : DamageList.DamagedResidents)
		{
			FResidentCoreState* Resident = FindMutableResident(Damage.ResidentID);
			if (Resident == nullptr)
			{
				OutError = TEXT("Earthquake damage references an unknown resident.");
				return false;
			}
			if (Resident->HomeState != EHomeState::Healthy)
			{
				continue;
			}

			const bool bMacroManaged = Resident->Representation == EResidentRepresentation::CohortManaged;
			if (bMacroManaged && !RemoveFromCohort(*Resident, Time, OutError))
			{
				return false;
			}
			Resident->HomeState = EHomeState::DamagedWaiting;
			Resident->CurrentGoal = EIndividualGoal::RestoreHome;
			Resident->LastUpdateTime = Time;
			++Resident->Version;
			if (bMacroManaged)
			{
				AddToCohort(*Resident, Time);
			}
		}
		OutError.Reset();
		return true;
	}

	bool FStatePreservingLODSystem::Activate(
		const FResidentID ResidentID,
		const FSimulationTime Time,
		FString& OutError)
	{
		return Transition(ResidentID, EResidentRepresentation::ActiveMicro, Time, OutError);
	}

	bool FStatePreservingLODSystem::Deactivate(
		const FResidentID ResidentID,
		const FSimulationTime Time,
		FString& OutError)
	{
		return Transition(ResidentID, EResidentRepresentation::CohortManaged, Time, OutError);
	}

	bool FStatePreservingLODSystem::Transition(
		const FResidentID ResidentID,
		const EResidentRepresentation Target,
		const FSimulationTime Time,
		FString& OutError)
	{
		if (!AdvanceTo(Time, OutError))
		{
			return false;
		}

		FLODTransitionRecord Record;
		Record.RequestedTime = Time;
		Record.CommittedTime = Time;
		Record.To = Target;
		Record.ArriveID = Scheduler.IssueArriveID();

		FResidentCoreState* Resident = FindMutableResident(ResidentID);
		if (Resident == nullptr)
		{
			Record.Result = ELODTransitionResult::ResidentNotFound;
			Transitions.Add(Record);
			OutError = TEXT("LOD transition references an unknown resident.");
			return false;
		}

		Record.PersistentID = Resident->PersistentID;
		Record.From = Resident->Representation;
		Record.Bucket = MakeCohortLabel(MakeCohortKey(*Resident));
		if (Resident->Representation == Target)
		{
			Record.Result = ELODTransitionResult::AlreadyInState;
			Transitions.Add(Record);
			OutError.Reset();
			return true;
		}
		if (Target == EResidentRepresentation::ActiveMicro
			&& GetActiveMicroCount() >= ActiveMicroCap)
		{
			Record.Result = ELODTransitionResult::ActiveCapReached;
			Transitions.Add(Record);
			OutError = TEXT("Active Micro cap has been reached.");
			return false;
		}

		const FString ExpectedOwner = RepresentationOwner(Resident->Representation, ResidentID);
		const FString NewOwner = RepresentationOwner(Target, ResidentID);
		if (Resident->ActiveEventID != 0)
		{
			const FSimulationEventRecord* Event = EventStore.Find(Resident->ActiveEventID);
			if (Event == nullptr || Event->Event.Owner != ExpectedOwner)
			{
				Record.Result = ELODTransitionResult::EventOwnerConflict;
				Transitions.Add(Record);
				OutError = TEXT("LOD transition event owner does not match the current representation.");
				return false;
			}
		}
		if (Resident->Representation == EResidentRepresentation::CohortManaged)
		{
			const FCohortBucket* Bucket = Cohorts.Find(MakeCohortKey(*Resident));
			if (Bucket == nullptr || Bucket->PopulationCount <= 0)
			{
				Record.Result = ELODTransitionResult::CohortCacheMismatch;
				Transitions.Add(Record);
				OutError = TEXT("LOD transition cannot remove the resident from its Cohort cache.");
				return false;
			}
		}

		if (Resident->ActiveEventID != 0
			&& !EventStore.TransferOwner(Resident->ActiveEventID, ExpectedOwner, NewOwner, OutError))
		{
			Record.Result = ELODTransitionResult::EventOwnerConflict;
			Transitions.Add(Record);
			return false;
		}
		if (Resident->Representation == EResidentRepresentation::CohortManaged
			&& !RemoveFromCohort(*Resident, Time, OutError))
		{
			if (Resident->ActiveEventID != 0)
			{
				FString RollbackError;
				EventStore.TransferOwner(Resident->ActiveEventID, NewOwner, ExpectedOwner, RollbackError);
			}
			Record.Result = ELODTransitionResult::CohortCacheMismatch;
			Transitions.Add(Record);
			return false;
		}

		Resident->Representation = Target;
		if (Target == EResidentRepresentation::CohortManaged)
		{
			AddToCohort(*Resident, Time);
		}
		Record.Result = ELODTransitionResult::Committed;
		Transitions.Add(Record);
		OutError.Reset();
		return true;
	}

	bool FStatePreservingLODSystem::StartRepair(
		const FResidentID ResidentID,
		const FSimulationTime Time,
		FString& OutError,
		const FArriveID ExistingArriveID)
	{
		using namespace DomainRules;
		FResidentCoreState* Resident = FindMutableResident(ResidentID);
		if (Resident == nullptr
			|| Resident->HomeState != EHomeState::DamagedWaiting
			|| Resident->ActiveEventID != 0)
		{
			OutError = TEXT("Resident cannot start Repair from the current state.");
			return false;
		}

		const FString WoodAccount = ResidentAccount(ResidentID, TEXT("Wood"));
		if (Ledger.GetBalance(ESimulationResource::Wood, WoodAccount) + UE_DOUBLE_SMALL_NUMBER < RepairWoodPerHome)
		{
			OutError = TEXT("Resident does not hold enough Wood to start Repair.");
			return false;
		}
		if (!AdvanceTo(Time, OutError))
		{
			return false;
		}
		EnsureRepairCapacityDay(Time);
		if (RepairStartsRemaining <= 0)
		{
			OutError = TEXT("No Repair start capacity remains for this game day.");
			return false;
		}
		if (ExistingArriveID <= 0)
		{
			OutError = TEXT("Repair start requires a winning ArriveID from the unified competition queue.");
			return false;
		}
		const FActionRequestKey RequestKey{ ResidentID, EIndividualAction::StartRepair };
		const FArriveID ArriveID = ExistingArriveID;
		const FIssuedActionRequest* IssuedRequest = IssuedActionRequests.Find(ArriveID);
		const FArriveID* ActiveArriveID = ActiveActionRequestIDs.Find(RequestKey);
		if (IssuedRequest == nullptr
			|| !(IssuedRequest->Key == RequestKey)
			|| IssuedRequest->State != EActionRequestState::Won
			|| !(IssuedRequest->LastResolvedTime == Time)
			|| ActiveArriveID == nullptr
			|| *ActiveArriveID != ArriveID)
		{
			OutError = TEXT("Repair start ArriveID is not the current winning request for this resident and action.");
			return false;
		}

		const bool bMacroManaged = Resident->Representation == EResidentRepresentation::CohortManaged;
		if (bMacroManaged && !RemoveFromCohort(*Resident, Time, OutError))
		{
			return false;
		}

		FSimulationEventRequest EventRequest;
		EventRequest.Type = TEXT("Repair");
		EventRequest.Owner = RepresentationOwner(Resident->Representation, ResidentID);
		EventRequest.ResidentID = ResidentID;
		EventRequest.ActionCode = static_cast<int32>(EIndividualAction::ContinueRepair);
		EventRequest.WoodQuantity = static_cast<int32>(RepairWoodPerHome);
		EventRequest.StartTime = Time;
		EventRequest.EndTime = FSimulationTime::FromMinutes(Time.Minutes + 2 * MinutesPerDay);
		EventRequest.ArriveID = ArriveID;
		EventRequest.ParticipantCount = 1;
		EventRequest.Cause = ToString(EIndividualGoal::RestoreHome);
		FEventID EventID = 0;
		if (!EventStore.CreateEvent(EventRequest, EventID, OutError))
		{
			if (bMacroManaged)
			{
				AddToCohort(*Resident, Time);
			}
			return false;
		}

		FLedgerTransferRequest Transfer;
		Transfer.IdempotencyKey = FString::Printf(TEXT("REPAIR-START-%lld"), EventID);
		Transfer.GameTime = Time;
		Transfer.Resource = ESimulationResource::Wood;
		Transfer.Source = WoodAccount;
		Transfer.Destination = MakeKingdomAccount(Resident->Kingdom, TEXT("WoodEmbeddedInRepairs"));
		Transfer.Quantity = RepairWoodPerHome;
		Transfer.EventID = EventID;
		Transfer.ArriveID = ArriveID;
		FTransactionID TransactionID = 0;
		if (!Ledger.SubmitTransfer(Transfer, TransactionID, OutError))
		{
			if (bMacroManaged)
			{
				AddToCohort(*Resident, Time);
			}
			return false;
		}

		SyncResidentResourceView(*Resident);
		--RepairStartsRemaining;
		Resident->HomeState = EHomeState::UnderRepair;
		Resident->CurrentGoal = EIndividualGoal::RestoreHome;
		Resident->CurrentAction = EIndividualAction::ContinueRepair;
		Resident->MacroIntent = EMacroIntent::Repair;
		Resident->ActiveEventID = EventID;
		Resident->ActiveArriveID = ArriveID;
		Resident->ActionStartTime = Time;
		Resident->ActionEndTime = EventRequest.EndTime;
		Resident->LastUpdateTime = Time;
		++Resident->Version;
		if (bMacroManaged)
		{
			AddToCohort(*Resident, Time);
		}
		if (!Scheduler.Schedule({ EventID, ArriveID, EventRequest.EndTime }, Time, OutError))
		{
			return false;
		}
		IssuedActionRequests.FindChecked(ArriveID).State = EActionRequestState::Committed;
		ActiveActionRequestIDs.Remove(RequestKey);
		return true;
	}

#if WITH_DEV_AUTOMATION_TESTS
	bool FStatePreservingLODSystem::SeedResidentWoodForTest(
		const FResidentID ResidentID,
		const int32 Quantity,
		const FSimulationTime Time,
		FString& OutError)
	{
		FResidentCoreState* Resident = FindMutableResident(ResidentID);
		if (Resident == nullptr || Quantity <= 0)
		{
			OutError = TEXT("Test Wood seed requires a valid resident and positive quantity.");
			return false;
		}
		if (!AdvanceTo(Time, OutError))
		{
			return false;
		}

		const bool bMacroManaged = Resident->Representation == EResidentRepresentation::CohortManaged;
		if (bMacroManaged && !RemoveFromCohort(*Resident, Time, OutError))
		{
			return false;
		}

		FLedgerTransferRequest Transfer;
		Transfer.IdempotencyKey = FString::Printf(
			TEXT("TEST-WOOD-SEED-%lld-V%u"),
			ResidentID,
			Resident->Version);
		Transfer.GameTime = Time;
		Transfer.Resource = ESimulationResource::Wood;
		Transfer.Source = ExternalBoundaryAccount;
		Transfer.Destination = ResidentAccount(ResidentID, TEXT("Wood"));
		Transfer.Quantity = Quantity;
		Transfer.bBoundaryFlow = true;
		Transfer.ArriveID = Scheduler.IssueArriveID();
		FTransactionID TransactionID = 0;
		if (!Ledger.SubmitTransfer(Transfer, TransactionID, OutError))
		{
			if (bMacroManaged)
			{
				AddToCohort(*Resident, Time);
			}
			return false;
		}

		SyncResidentResourceView(*Resident);
		Resident->LastUpdateTime = Time;
		++Resident->Version;
		if (bMacroManaged)
		{
			AddToCohort(*Resident, Time);
		}
		OutError.Reset();
		return true;
	}
#endif

	bool FStatePreservingLODSystem::AdvanceTo(const FSimulationTime Time, FString& OutError)
	{
		using namespace DomainRules;
		if (!SynchronizeTime(Time, OutError))
		{
			return false;
		}
		TArray<FScheduledEvent> DueEvents;
		Scheduler.PopDueThrough(Time, DueEvents);
		for (const FScheduledEvent& Due : DueEvents)
		{
			const FSimulationTime CompletionTime = Due.ExecuteAt;
			if (!(CohortTime == CompletionTime))
			{
				CohortTime = CompletionTime;
				Cohorts = RebuildCohorts();
			}
			const FSimulationEventRecord* EventRecord = EventStore.Find(Due.EventID);
			if (EventRecord == nullptr
				|| EventRecord->Event.ActionCode != static_cast<int32>(EIndividualAction::ContinueRepair))
			{
				OutError = TEXT("Phase 4 encountered an unsupported scheduled event.");
				return false;
			}
			const FSimulationEventRequest Event = EventRecord->Event;
			FResidentCoreState* Resident = FindMutableResident(Event.ResidentID);
			if (Resident == nullptr || Resident->ActiveEventID != Due.EventID)
			{
				OutError = TEXT("Repair completion references an invalid resident event.");
				return false;
			}

			const bool bMacroManaged = Resident->Representation == EResidentRepresentation::CohortManaged;
			if (bMacroManaged && !RemoveFromCohort(*Resident, CompletionTime, OutError))
			{
				return false;
			}

			FLedgerTransferRequest Transfer;
			Transfer.IdempotencyKey = FString::Printf(TEXT("REPAIR-COMPLETE-%lld"), Due.EventID);
			Transfer.GameTime = CompletionTime;
			Transfer.Resource = ESimulationResource::Wood;
			Transfer.Source = MakeKingdomAccount(Resident->Kingdom, TEXT("WoodEmbeddedInRepairs"));
			Transfer.Destination = MakeKingdomAccount(Resident->Kingdom, TEXT("WoodInRepairedHomes"));
			Transfer.Quantity = RepairWoodPerHome;
			Transfer.EventID = Due.EventID;
			Transfer.ArriveID = Due.ArriveID;
			FTransactionID TransactionID = 0;
			if (!Ledger.SubmitTransfer(Transfer, TransactionID, OutError)
				|| !EventStore.CompleteEvent(Due.EventID, OutError))
			{
				return false;
			}

			Resident->HomeState = EHomeState::Repaired;
			Resident->CurrentGoal = FIndividualDomain::SelectGoal(*Resident);
			Resident->LastCompletedAction = EIndividualAction::ContinueRepair;
			Resident->CurrentAction = EIndividualAction::None;
			Resident->MacroIntent = EMacroIntent::Routine;
			Resident->ActiveEventID = 0;
			Resident->ActiveArriveID = 0;
			Resident->ActiveReservationID = 0;
			Resident->ActionEndTime = CompletionTime;
			Resident->LastUpdateTime = CompletionTime;
			++Resident->Version;
			if (bMacroManaged)
			{
				AddToCohort(*Resident, CompletionTime);
			}
		}
		if (!(CohortTime == Time))
		{
			CohortTime = Time;
			Cohorts = RebuildCohorts();
		}
		OutError.Reset();
		return true;
	}

	bool FStatePreservingLODSystem::BuildMacroDecisionBatch(
		const FIndividualWorldFacts& KingdomAWorld,
		const FIndividualWorldFacts& KingdomBWorld,
		FMacroDecisionBatch& OutBatch,
		FString& OutError) const
	{
		OutBatch = {};
		TMap<FString, TArray<int32>> GroupsByState;
		for (int32 ResidentIndex = 0; ResidentIndex < Residents.Num(); ++ResidentIndex)
		{
			const FResidentCoreState& Resident = Residents[ResidentIndex];
			if (Resident.Representation != EResidentRepresentation::CohortManaged
				|| Resident.ActiveEventID != 0)
			{
				continue;
			}

			const int32 Cash = ResourceInt(Ledger, ESimulationResource::Coin, ResidentAccount(Resident.ResidentID, TEXT("Cash")));
			const int32 Credit = ResourceInt(Ledger, ESimulationResource::Coin, ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")));
			const int32 Wood = ResourceInt(Ledger, ESimulationResource::Wood, ResidentAccount(Resident.ResidentID, TEXT("Wood")));
			const FString Key = FString::Printf(
				TEXT("%d|%d|%d|%d|%d|%d|%d"),
				static_cast<int32>(Resident.Kingdom),
				static_cast<int32>(Resident.Profession),
				static_cast<int32>(Resident.IncomeBand),
				Cash,
				Credit,
				Wood,
				static_cast<int32>(Resident.HomeState));
			GroupsByState.FindOrAdd(Key).Add(ResidentIndex);
			++OutBatch.ResidentCount;
		}

		TArray<FString> Keys;
		GroupsByState.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			const TArray<int32>& ResidentGroup = GroupsByState[Key];
			if (ResidentGroup.Num() == 0)
			{
				continue;
			}

			FResidentCoreState Representative = Residents[ResidentGroup[0]];
			Representative.Cash = ResourceInt(Ledger, ESimulationResource::Coin, ResidentAccount(Representative.ResidentID, TEXT("Cash")));
			Representative.RepairCredit = ResourceInt(Ledger, ESimulationResource::Coin, ResidentAccount(Representative.ResidentID, TEXT("RepairCredit")));
			Representative.InventoryWood = ResourceInt(Ledger, ESimulationResource::Wood, ResidentAccount(Representative.ResidentID, TEXT("Wood")));
			const FIndividualWorldFacts& World = Representative.Kingdom == EKingdom::A ? KingdomAWorld : KingdomBWorld;
			const FIndividualPlan Plan = FIndividualDomain::BuildPlan(Representative, World);

			FMacroDecisionGroup& Decision = OutBatch.Groups.AddDefaulted_GetRef();
			Decision.Goal = Plan.Goal;
			Decision.Action = Plan.Actions.Num() > 0 ? Plan.Actions[0] : EIndividualAction::Wait;
			for (const int32 ResidentIndex : ResidentGroup)
			{
				Decision.ResidentIDs.Add(Residents[ResidentIndex].ResidentID);
			}
			Decision.ResidentIDs.Sort();
			++OutBatch.PlanningEvaluationCount;
		}

		OutError.Reset();
		return true;
	}

	bool FStatePreservingLODSystem::ResolveCompetition(
		const FSimulationTime Time,
		const int32 AvailableQuantity,
		TArray<FUnifiedActionRequest>& InOutRequests,
		FString& OutError)
	{
		if (AvailableQuantity < 0)
		{
			OutError = TEXT("Competition capacity cannot be negative.");
			return false;
		}
		if (!AdvanceTo(Time, OutError))
		{
			return false;
		}
		TSet<FString> CandidateKeys;
		TSet<FArriveID> ExistingArriveIDs;
		for (FUnifiedActionRequest& Request : InOutRequests)
		{
			const FResidentCoreState* Resident = FindResident(Request.ResidentID);
			if (Resident == nullptr
				|| Request.Action == EIndividualAction::None
				|| Request.Quantity <= 0)
			{
				OutError = TEXT("Competition contains an invalid resident request.");
				return false;
			}
			const FString CandidateKey = FString::Printf(
				TEXT("%lld|%d"),
				Request.ResidentID,
				static_cast<int32>(Request.Action));
			const FActionRequestKey RequestKey{ Request.ResidentID, Request.Action };
			if (CandidateKeys.Contains(CandidateKey)
				|| Request.ArriveID < 0
				|| (Request.ArriveID > 0 && ExistingArriveIDs.Contains(Request.ArriveID)))
			{
				OutError = TEXT("Competition contains a duplicate candidate or ArriveID.");
				return false;
			}
			if (Request.ArriveID > 0)
			{
				const FIssuedActionRequest* IssuedRequest = IssuedActionRequests.Find(Request.ArriveID);
				const FArriveID* ActiveArriveID = ActiveActionRequestIDs.Find(RequestKey);
				if (IssuedRequest == nullptr
					|| !(IssuedRequest->Key == RequestKey)
					|| IssuedRequest->State == EActionRequestState::Committed
					|| ActiveArriveID == nullptr
					|| *ActiveArriveID != Request.ArriveID)
				{
					OutError = TEXT("Competition ArriveID is not bound to this unfinished resident action request.");
					return false;
				}
			}
			else if (const FArriveID* ActiveArriveID = ActiveActionRequestIDs.Find(RequestKey))
			{
				const FIssuedActionRequest* IssuedRequest = IssuedActionRequests.Find(*ActiveArriveID);
				if (IssuedRequest == nullptr || IssuedRequest->State == EActionRequestState::Committed)
				{
					OutError = TEXT("Competition action request registry is inconsistent.");
					return false;
				}
				Request.ArriveID = *ActiveArriveID;
			}
			CandidateKeys.Add(CandidateKey);
			if (Request.ArriveID > 0)
			{
				ExistingArriveIDs.Add(Request.ArriveID);
			}
			Request.Representation = Resident->Representation;
			Request.OrderKey = DomainRules::CompetitionOrderKey(
				Config.Seed,
				Time.Minutes,
				Request.ResidentID,
				static_cast<uint64>(Request.Action));
			Request.bWon = false;
		}

		InOutRequests.Sort([](const FUnifiedActionRequest& Left, const FUnifiedActionRequest& Right)
		{
			return Left.OrderKey == Right.OrderKey
				? Left.ResidentID < Right.ResidentID
				: Left.OrderKey < Right.OrderKey;
		});
		for (FUnifiedActionRequest& Request : InOutRequests)
		{
			if (Request.ArriveID == 0)
			{
				Request.ArriveID = Scheduler.IssueArriveID();
				const FActionRequestKey RequestKey{ Request.ResidentID, Request.Action };
				IssuedActionRequests.Add(Request.ArriveID, { RequestKey, EActionRequestState::Pending, Time });
				ActiveActionRequestIDs.Add(RequestKey, Request.ArriveID);
			}
		}

		int32 RemainingQuantity = AvailableQuantity;
		for (FUnifiedActionRequest& Request : InOutRequests)
		{
			if (Request.Quantity <= RemainingQuantity)
			{
				Request.bWon = true;
				RemainingQuantity -= Request.Quantity;
			}
			FIssuedActionRequest& IssuedRequest = IssuedActionRequests.FindChecked(Request.ArriveID);
			IssuedRequest.State = Request.bWon
				? EActionRequestState::Won
				: EActionRequestState::Pending;
			IssuedRequest.LastResolvedTime = Time;
		}
		CompetitionHistory.Add({ Time, AvailableQuantity, InOutRequests });
		OutError.Reset();
		return true;
	}

	const FResidentCoreState* FStatePreservingLODSystem::FindResident(const FResidentID ResidentID) const
	{
		const int32* ResidentIndex = ResidentIndices.Find(ResidentID);
		return ResidentIndex != nullptr && Residents.IsValidIndex(*ResidentIndex)
			? &Residents[*ResidentIndex]
			: nullptr;
	}

	FResidentCoreState* FStatePreservingLODSystem::FindMutableResident(const FResidentID ResidentID)
	{
		const int32* ResidentIndex = ResidentIndices.Find(ResidentID);
		return ResidentIndex != nullptr && Residents.IsValidIndex(*ResidentIndex)
			? &Residents[*ResidentIndex]
			: nullptr;
	}

	bool FStatePreservingLODSystem::SynchronizeTime(
		const FSimulationTime Time,
		FString& OutError)
	{
		if (!Clock.AdvanceTo(Time, OutError))
		{
			return false;
		}
		if (!(CohortTime == Time))
		{
			CohortTime = Time;
			Cohorts = RebuildCohorts();
		}
		return true;
	}

	void FStatePreservingLODSystem::SyncResidentResourceView(FResidentCoreState& Resident)
	{
		Resident.Cash = ResourceInt(
			Ledger,
			ESimulationResource::Coin,
			ResidentAccount(Resident.ResidentID, TEXT("Cash")));
		Resident.RepairCredit = ResourceInt(
			Ledger,
			ESimulationResource::Coin,
			ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")));
		Resident.InventoryWood = ResourceInt(
			Ledger,
			ESimulationResource::Wood,
			ResidentAccount(Resident.ResidentID, TEXT("Wood")));
	}

	void FStatePreservingLODSystem::EnsureRepairCapacityDay(const FSimulationTime Time)
	{
		const int32 Day = static_cast<int32>(Time.Minutes / MinutesPerDay);
		if (RepairCapacityDay != Day)
		{
			RepairCapacityDay = Day;
			RepairStartsRemaining = FMath::FloorToInt(
				DomainRules::RepairStartCapacityPerPersonPerDay * Config.PopulationPerKingdom);
		}
	}

	FPopulationState FStatePreservingLODSystem::BuildPopulationState() const
	{
		FPopulationState Population;
		Population.Total = Residents.Num();
		for (const FResidentCoreState& Resident : Residents)
		{
			if (Resident.Representation == EResidentRepresentation::ActiveMicro)
			{
				++Population.ActiveMicro;
			}
			else
			{
				++Population.PersistentMacro;
			}
		}
		return Population;
	}

	int32 FStatePreservingLODSystem::GetActiveMicroCount() const
	{
		return BuildPopulationState().ActiveMicro;
	}

	int64 FStatePreservingLODSystem::GetRemainingWorkMinutes(
		const FResidentID ResidentID,
		const FSimulationTime Time) const
	{
		const FResidentCoreState* Resident = FindResident(ResidentID);
		if (Resident == nullptr || Resident->ActiveEventID == 0)
		{
			return 0;
		}
		const FSimulationEventRecord* Event = EventStore.Find(Resident->ActiveEventID);
		return Event != nullptr && Event->State == ESimulationEventState::Pending
			? FMath::Max<int64>(0, Event->Event.EndTime.Minutes - Time.Minutes)
			: 0;
	}

	FPhase4Audit FStatePreservingLODSystem::Audit() const
	{
		FPhase4Audit Result;
		Result.Conservation = AuditConservation(BuildPopulationState(), Ledger, EventStore);
		Result.CoinResidual = Ledger.ComputeResidual(ESimulationResource::Coin);
		Result.ActiveCapViolationCount = GetActiveMicroCount() > ActiveMicroCap ? 1 : 0;

		TSet<FEventID> ReferencedPendingEvents;
		for (const FResidentCoreState& Resident : Residents)
		{
			if (Resident.PersistentID != Resident.ResidentID
				|| Resident.Name != MakeStableResidentName(Resident.ResidentID)
				|| Resident.HomeID != Resident.ResidentID)
			{
				++Result.IdentityMismatchCount;
			}

			const int32 Cash = ResourceInt(Ledger, ESimulationResource::Coin, ResidentAccount(Resident.ResidentID, TEXT("Cash")));
			const int32 Credit = ResourceInt(Ledger, ESimulationResource::Coin, ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")));
			const int32 Wood = ResourceInt(Ledger, ESimulationResource::Wood, ResidentAccount(Resident.ResidentID, TEXT("Wood")));
			if (Cash != Resident.Cash || Credit != Resident.RepairCredit || Wood != Resident.InventoryWood)
			{
				++Result.CoreLedgerMismatchCount;
			}

			if (Resident.ActiveEventID != 0)
			{
				const FSimulationEventRecord* Event = EventStore.Find(Resident.ActiveEventID);
				if (Event == nullptr
					|| Event->State != ESimulationEventState::Pending
					|| Event->Event.ResidentID != Resident.ResidentID
					|| Event->Event.Owner != RepresentationOwner(Resident.Representation, Resident.ResidentID)
					|| Event->Event.ArriveID != Resident.ActiveArriveID
					|| Event->Event.ReservationID != Resident.ActiveReservationID
					|| !(Event->Event.StartTime == Resident.ActionStartTime)
					|| !(Event->Event.EndTime == Resident.ActionEndTime))
				{
					++Result.EventReferenceMismatchCount;
				}
				else
				{
					ReferencedPendingEvents.Add(Resident.ActiveEventID);
				}
			}
		}
		for (const TPair<FEventID, FSimulationEventRecord>& Pair : EventStore.GetEvents())
		{
			if (Pair.Value.State == ESimulationEventState::Pending
				&& Pair.Value.Event.ResidentID != 0
				&& !ReferencedPendingEvents.Contains(Pair.Key))
			{
				++Result.EventReferenceMismatchCount;
			}
		}

		const TMap<FCohortKey, FCohortBucket> Expected = RebuildCohorts();
		if (Expected.Num() != Cohorts.Num())
		{
			++Result.CohortMismatchCount;
		}
		for (const TPair<FCohortKey, FCohortBucket>& Pair : Expected)
		{
			const FCohortBucket* Actual = Cohorts.Find(Pair.Key);
			if (Actual == nullptr || !BucketsEqual(Pair.Value, *Actual))
			{
				++Result.CohortMismatchCount;
			}
		}
		return Result;
	}

	void FStatePreservingLODSystem::AddToCohort(
		const FResidentCoreState& Resident,
		const FSimulationTime Time)
	{
		CohortTime = Time;
		FCohortBucket& Bucket = Cohorts.FindOrAdd(MakeCohortKey(Resident));
		AddResidentToBucket(Resident, Ledger, EventStore, Time, Bucket);
		Bucket.RNGStreamKey = HashCombine(::GetTypeHash(Config.Seed), GetTypeHash(MakeCohortKey(Resident)));
	}

	bool FStatePreservingLODSystem::RemoveFromCohort(
		const FResidentCoreState& Resident,
		const FSimulationTime Time,
		FString& OutError)
	{
		const FCohortKey Key = MakeCohortKey(Resident);
		FCohortBucket* Bucket = Cohorts.Find(Key);
		if (Bucket == nullptr || Bucket->PopulationCount <= 0)
		{
			OutError = TEXT("Resident is missing from the derived Cohort cache.");
			return false;
		}

		const int32 Cash = ResourceInt(Ledger, ESimulationResource::Coin, ResidentAccount(Resident.ResidentID, TEXT("Cash")));
		const int32 Credit = ResourceInt(Ledger, ESimulationResource::Coin, ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")));
		const int32 Wood = ResourceInt(Ledger, ESimulationResource::Wood, ResidentAccount(Resident.ResidentID, TEXT("Wood")));
		--Bucket->PopulationCount;
		Bucket->CashSum -= Cash;
		Bucket->CashSquaredSum -= static_cast<int64>(Cash) * Cash;
		Bucket->RepairCreditSum -= Credit;
		--Bucket->WoodCounts[FMath::Clamp(Wood, 0, 4)];
		Bucket->AidReceivedCount -= Resident.bAidReceived ? 1 : 0;
		Bucket->LastUpdateTime = Time;
		if (Resident.ActiveEventID != 0)
		{
			Bucket->EventBatchRefs.RemoveSingle(Resident.ActiveEventID);
			if (Resident.HomeState == EHomeState::UnderRepair)
			{
				const FSimulationEventRecord* Event = EventStore.Find(Resident.ActiveEventID);
				if (Event != nullptr)
				{
					const int64 Duration = FMath::Max<int64>(1, Event->Event.EndTime.Minutes - Event->Event.StartTime.Minutes);
					const int64 Elapsed = FMath::Clamp<int64>(Time.Minutes - Event->Event.StartTime.Minutes, 0, Duration);
					const int32 ProgressBin = FMath::Clamp(static_cast<int32>((Elapsed * 4) / Duration), 0, 3);
					--Bucket->RepairProgressBins[ProgressBin];
				}
			}
		}

		if (Bucket->PopulationCount == 0)
		{
			Cohorts.Remove(Key);
		}
		CohortTime = Time;
		OutError.Reset();
		return true;
	}

	TMap<FCohortKey, FCohortBucket> FStatePreservingLODSystem::RebuildCohorts() const
	{
		TMap<FCohortKey, FCohortBucket> Rebuilt;
		for (const FResidentCoreState& Resident : Residents)
		{
			if (Resident.Representation != EResidentRepresentation::CohortManaged)
			{
				continue;
			}
			FCohortBucket& Bucket = Rebuilt.FindOrAdd(MakeCohortKey(Resident));
			AddResidentToBucket(Resident, Ledger, EventStore, CohortTime, Bucket);
			Bucket.RNGStreamKey = HashCombine(::GetTypeHash(Config.Seed), GetTypeHash(MakeCohortKey(Resident)));
		}
		return Rebuilt;
	}

	FString FStatePreservingLODSystem::ResidentAccount(
		const FResidentID ResidentID,
		const TCHAR* StockName)
	{
		return ResidentAccountName(ResidentID, StockName);
	}

	FString FStatePreservingLODSystem::RepresentationOwner(
		const EResidentRepresentation Representation,
		const FResidentID ResidentID)
	{
		return FString::Printf(
			TEXT("%s:%lld"),
			Representation == EResidentRepresentation::ActiveMicro ? TEXT("Micro") : TEXT("Macro"),
			ResidentID);
	}

	FCohortKey FStatePreservingLODSystem::MakeCohortKey(const FResidentCoreState& Resident)
	{
		return {
			Resident.Kingdom,
			Resident.Profession,
			Resident.IncomeBand,
			Resident.HomeState,
			Resident.MacroIntent
		};
	}

	FString FStatePreservingLODSystem::MakeCohortLabel(const FCohortKey& Key)
	{
		return FString::Printf(
			TEXT("%d|%d|%d|%d|%d"),
			static_cast<int32>(Key.Kingdom),
			static_cast<int32>(Key.Profession),
			static_cast<int32>(Key.IncomeBand),
			static_cast<int32>(Key.HomeState),
			static_cast<int32>(Key.MacroIntent));
	}

	FString FStatePreservingLODSystem::BuildDeterministicDigest() const
	{
		FString State = FString::Printf(
			TEXT("Config=%s|Clock=%lld|CohortTime=%lld|NextArrive=%lld|RepairDay=%d|RepairRemaining=%d"),
			*PopulationManifest.ConfigHash,
			Clock.Now().Minutes,
			CohortTime.Minutes,
			Scheduler.GetNextArriveID(),
			RepairCapacityDay,
			RepairStartsRemaining);
		for (const FResidentCoreState& Resident : Residents)
		{
			State += FString::Printf(
				TEXT("\nR|%lld|%lld|%lld|%s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%s|%u|%u|%d|%d"),
				Resident.ResidentID,
				Resident.HomeID,
				Resident.PersistentID,
				*Resident.Name,
				static_cast<int32>(Resident.Kingdom),
				static_cast<int32>(Resident.Profession),
				static_cast<int32>(Resident.IncomeBand),
				Resident.Cash,
				Resident.RepairCredit,
				Resident.InventoryWood,
				static_cast<int32>(Resident.HomeState),
				static_cast<int32>(Resident.CurrentGoal),
				static_cast<int32>(Resident.CurrentAction),
				static_cast<int32>(Resident.LastCompletedAction),
				static_cast<int32>(Resident.MacroIntent),
				Resident.ActiveEventID,
				Resident.ParentEventID,
				Resident.ActiveArriveID,
				Resident.ActiveReservationID,
				Resident.CausalPolicyID,
				Resident.ActionStartTime.Minutes,
				Resident.ActionEndTime.Minutes,
				Resident.LastUpdateTime.Minutes,
				*Resident.LocationAnchor,
				Resident.RNGStreamKey,
				Resident.Version,
				static_cast<int32>(Resident.Representation),
				Resident.bAidReceived ? 1 : 0);
		}

		TArray<FCohortKey> CohortKeys;
		Cohorts.GetKeys(CohortKeys);
		CohortKeys.Sort([](const FCohortKey& Left, const FCohortKey& Right)
		{
			return MakeCohortLabel(Left) < MakeCohortLabel(Right);
		});
		for (const FCohortKey& Key : CohortKeys)
		{
			const FCohortBucket& Bucket = Cohorts.FindChecked(Key);
			State += FString::Printf(
				TEXT("\nC|%s|%d|%lld|%lld|%lld|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%lld|%u"),
				*MakeCohortLabel(Key),
				Bucket.PopulationCount,
				Bucket.CashSum,
				Bucket.CashSquaredSum,
				Bucket.RepairCreditSum,
				Bucket.WoodCounts[0],
				Bucket.WoodCounts[1],
				Bucket.WoodCounts[2],
				Bucket.WoodCounts[3],
				Bucket.WoodCounts[4],
				Bucket.AidEligibleCount,
				Bucket.AidReceivedCount,
				Bucket.RepairProgressBins[0],
				Bucket.RepairProgressBins[1],
				Bucket.RepairProgressBins[2],
				Bucket.RepairProgressBins[3],
				Bucket.LastUpdateTime.Minutes,
				Bucket.RNGStreamKey);
			for (const double Residual : Bucket.ResidualFlows)
			{
				State += FString::Printf(TEXT("|%.9f"), Residual);
			}
			TArray<FEventID> EventRefs = Bucket.EventBatchRefs;
			EventRefs.Sort();
			for (const FEventID EventID : EventRefs)
			{
				State += FString::Printf(TEXT("|E%lld"), EventID);
			}
		}

		for (const FLODTransitionRecord& Transition : Transitions)
		{
			State += FString::Printf(
				TEXT("\nT|%lld|%s|%s|%lld|%lld|%lld|%s|%s"),
				Transition.PersistentID,
				RepresentationName(Transition.From),
				RepresentationName(Transition.To),
				Transition.RequestedTime.Minutes,
				Transition.CommittedTime.Minutes,
				Transition.ArriveID,
				*Transition.Bucket,
				TransitionResultName(Transition.Result));
		}

		TArray<FScheduledEvent> PendingEvents = Scheduler.GetPendingEvents();
		PendingEvents.Sort([](const FScheduledEvent& Left, const FScheduledEvent& Right)
		{
			if (!(Left.ExecuteAt == Right.ExecuteAt))
			{
				return Left.ExecuteAt < Right.ExecuteAt;
			}
			return Left.ArriveID == Right.ArriveID
				? Left.EventID < Right.EventID
				: Left.ArriveID < Right.ArriveID;
		});
		for (const FScheduledEvent& Pending : PendingEvents)
		{
			State += FString::Printf(
				TEXT("\nS|%lld|%lld|%lld"),
				Pending.EventID,
				Pending.ArriveID,
				Pending.ExecuteAt.Minutes);
		}

		TArray<FResourceAccountKey> BalanceKeys;
		Ledger.GetBalances().GetKeys(BalanceKeys);
		BalanceKeys.Sort([](const FResourceAccountKey& Left, const FResourceAccountKey& Right)
		{
			return Left.Resource == Right.Resource
				? Left.Account < Right.Account
				: static_cast<uint8>(Left.Resource) < static_cast<uint8>(Right.Resource);
		});
		for (const FResourceAccountKey& Key : BalanceKeys)
		{
			State += FString::Printf(
				TEXT("\nB|%d|%s|%.9f"),
				static_cast<int32>(Key.Resource),
				*Key.Account,
				Ledger.GetBalances().FindChecked(Key));
		}
		for (const FLedgerTransaction& Transaction : Ledger.GetTransactions())
		{
			State += FString::Printf(
				TEXT("\nL|%lld|%s|%lld|%d|%s|%s|%.9f|%d|%lld|%lld|%lld"),
				Transaction.TransactionID,
				*Transaction.Transfer.IdempotencyKey,
				Transaction.Transfer.GameTime.Minutes,
				static_cast<int32>(Transaction.Transfer.Resource),
				*Transaction.Transfer.Source,
				*Transaction.Transfer.Destination,
				Transaction.Transfer.Quantity,
				Transaction.Transfer.bBoundaryFlow ? 1 : 0,
				Transaction.Transfer.EventID,
				Transaction.Transfer.ArriveID,
				Transaction.Transfer.PolicyID);
		}

		TArray<FEventID> EventIDs;
		EventStore.GetEvents().GetKeys(EventIDs);
		EventIDs.Sort();
		for (const FEventID EventID : EventIDs)
		{
			const FSimulationEventRecord& Event = EventStore.GetEvents()[EventID];
			State += FString::Printf(
				TEXT("\nE|%lld|%s|%s|%lld|%d|%d|%lld|%lld|%lld|%lld|%lld|%d|%s|%lld|%d"),
				EventID,
				*Event.Event.Type,
				*Event.Event.Owner,
				Event.Event.ResidentID,
				Event.Event.ActionCode,
				Event.Event.WoodQuantity,
				Event.Event.StartTime.Minutes,
				Event.Event.EndTime.Minutes,
				Event.Event.ReservationID,
				Event.Event.ArriveID,
				Event.Event.ParentEventID,
				Event.Event.ParticipantCount,
				*Event.Event.Cause,
				Event.Event.PolicyID,
				static_cast<int32>(Event.State));
		}

		TArray<FReservationID> ReservationIDs;
		Reservations.GetReservations().GetKeys(ReservationIDs);
		ReservationIDs.Sort();
		for (const FReservationID ReservationID : ReservationIDs)
		{
			const FReservationRecord& Reservation = Reservations.GetReservations().FindChecked(ReservationID);
			State += FString::Printf(
				TEXT("\nV|%lld|%s|%lld|%d|%s|%s|%.9f|%lld|%lld|%lld|%d"),
				ReservationID,
				*Reservation.Request.IdempotencyKey,
				Reservation.Request.GameTime.Minutes,
				static_cast<int32>(Reservation.Request.Resource),
				*Reservation.Request.SourceAccount,
				*Reservation.Request.ReservedAccount,
				Reservation.Request.Quantity,
				Reservation.Request.EventID,
				Reservation.Request.ArriveID,
				Reservation.Request.PolicyID,
				static_cast<int32>(Reservation.State));
		}

		TArray<FArriveID> ActionRequestIDs;
		IssuedActionRequests.GetKeys(ActionRequestIDs);
		ActionRequestIDs.Sort();
		for (const FArriveID ArriveID : ActionRequestIDs)
		{
			const FIssuedActionRequest& Request = IssuedActionRequests.FindChecked(ArriveID);
			State += FString::Printf(
				TEXT("\nA|%lld|%lld|%d|%d|%lld"),
				ArriveID,
				Request.Key.ResidentID,
				static_cast<int32>(Request.Key.Action),
				static_cast<int32>(Request.State),
				Request.LastResolvedTime.Minutes);
		}

		for (const FCompetitionRecord& Competition : CompetitionHistory)
		{
			State += FString::Printf(
				TEXT("\nQ|%lld|%d|%d"),
				Competition.Time.Minutes,
				Competition.AvailableQuantity,
				Competition.Requests.Num());
			for (const FUnifiedActionRequest& Request : Competition.Requests)
			{
				State += FString::Printf(
					TEXT("|%lld,%d,%d,%d,%llu,%lld,%d"),
					Request.ResidentID,
					static_cast<int32>(Request.Action),
					Request.Quantity,
					static_cast<int32>(Request.Representation),
					Request.OrderKey,
					Request.ArriveID,
					Request.bWon ? 1 : 0);
			}
		}

		FTCHARToUTF8 Utf8(*State);
		return FSHA1::HashBuffer(Utf8.Get(), Utf8.Length()).ToString();
	}
}
