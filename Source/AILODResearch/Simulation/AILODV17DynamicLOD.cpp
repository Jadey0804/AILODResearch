// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODV17AuthoritativeMacro.h"

#include "AILODDomainRules.h"
#include "AILODStatePreservingLOD.h"

namespace AILOD
{
	using namespace DomainRules;

	namespace
	{
		FString OuterKey(const EKingdom Kingdom, const EProfession Profession, const EIncomeBand IncomeBand)
		{
			return FString::Printf(
				TEXT("%d,%d,%d"),
				static_cast<int32>(Kingdom),
				static_cast<int32>(Profession),
				static_cast<int32>(IncomeBand));
		}

		FString ActiveOwner(const FResidentID ResidentID)
		{
			return FString::Printf(TEXT("Active:%lld"), ResidentID);
		}

		FString BatchOwner(const FV17AuthoritativeCellID CellID)
		{
			return FString::Printf(TEXT("BatchCell:%llu"), CellID);
		}

		bool IsSameTime(const FSimulationTime Left, const FSimulationTime Right)
		{
			return Left.Minutes == Right.Minutes;
		}
	}

	uint32 FV17AuthoritativeMacroSession::BuildHomeOuterKey(
		const EKingdom Kingdom,
		const EProfession Profession,
		const EIncomeBand IncomeBand)
	{
		return static_cast<uint32>(static_cast<uint8>(Kingdom))
			| (static_cast<uint32>(static_cast<uint8>(Profession)) << 8)
			| (static_cast<uint32>(static_cast<uint8>(IncomeBand)) << 16);
	}

	bool FV17AuthoritativeMacroSession::InitializeWithIdentity(
		const TArray<FV17AuthoritativeCellConfig>& InCells,
		const TArray<FV17IdentityRecord>& Identities,
		const TArray<FV17AuthoritativeKingdomConfig>& InKingdoms,
		const FSimulationTime StartTime,
		FString& OutError)
	{
		const FV17AuthoritativeMacroSession Before = *this;
		if (!Initialize(InCells, {}, InKingdoms, StartTime, OutError))
		{
			return false;
		}
		auto Restore = [this, &Before]() { *this = Before; };
		if (Identities.Num() != InitialPopulation)
		{
			OutError = TEXT("Identity Registry size must exactly match the authoritative population.");
			Restore();
			return false;
		}

		TMap<FString, int32> CellOuterCounts;
		for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Cells)
		{
			const FV17AuthoritativeJointKey& Key = Pair.Value.Key;
			CellOuterCounts.FindOrAdd(OuterKey(Key.Kingdom, Key.Profession, Key.IncomeBand)) += Pair.Value.Count;
		}

		TMap<FString, int32> IdentityOuterCounts;
		TSet<FPersistentID> PersistentIDs;
		TSet<FHomeID> HomeIDs;
		uint32 HomeStateIndex = 0;
		for (const FV17IdentityRecord& Identity : Identities)
		{
			if (Identity.ResidentID <= 0
				|| Identity.PersistentID <= 0
				|| Identity.HomeID <= 0
				|| Identity.IdentityVersion == 0
				|| IdentityRegistry.Contains(Identity.ResidentID)
				|| PersistentIDs.Contains(Identity.PersistentID)
				|| HomeIDs.Contains(Identity.HomeID)
				|| !KingdomConfigs.Contains(Identity.InitialKingdom))
			{
				OutError = TEXT("Identity Registry entries require unique positive resident, persistent and home IDs.");
				Restore();
				return false;
			}
			FV17IdentityRecord StoredIdentity = Identity;
			StoredIdentity.HomeStateIndex = HomeStateIndex;
			IdentityRegistry.Add(Identity.ResidentID, StoredIdentity);
			HomeStatesByIndex.Add(static_cast<uint8>(EHomeState::Healthy));
			ResidentIDsByHomeStateIndex.Add(Identity.ResidentID);
			++HomeStateIndex;
			PersistentIDs.Add(Identity.PersistentID);
			HomeIDs.Add(Identity.HomeID);
			IdentityOuterCounts.FindOrAdd(OuterKey(
				Identity.InitialKingdom,
				Identity.Profession,
				Identity.IncomeBand)) += 1;
		}
		if (IdentityOuterCounts.OrderIndependentCompareEqual(CellOuterCounts) == false)
		{
			OutError = TEXT("Identity outer Cohort counts must match the initialized Joint Cell population.");
			Restore();
			return false;
		}

		TMap<uint32, TArray<FResidentID>> ResidentIDsByOuter;
		for (const TPair<FResidentID, FV17IdentityRecord>& Pair : IdentityRegistry)
		{
			const FV17IdentityRecord& Identity = Pair.Value;
			ResidentIDsByOuter.FindOrAdd(BuildHomeOuterKey(
				Identity.InitialKingdom,
				Identity.Profession,
				Identity.IncomeBand)).Add(Identity.ResidentID);
		}
		for (TPair<uint32, TArray<FResidentID>>& Pair : ResidentIDsByOuter)
		{
			Pair.Value.Sort();
		}
		TMap<uint32, int32> NextIdentityByOuter;
		TArray<FV17AuthoritativeCellID> CellIDs;
		Cells.GetKeys(CellIDs);
		CellIDs.Sort();
		for (const FV17AuthoritativeCellID CellID : CellIDs)
		{
			const FV17AuthoritativeCellConfig& Cell = Cells.FindChecked(CellID);
			const uint32 Outer = BuildHomeOuterKey(
				Cell.Key.Kingdom,
				Cell.Key.Profession,
				Cell.Key.IncomeBand);
			const TArray<FResidentID>* ResidentIDs = ResidentIDsByOuter.Find(Outer);
			int32& NextIdentity = NextIdentityByOuter.FindOrAdd(Outer);
			if (ResidentIDs == nullptr || NextIdentity + Cell.Count > ResidentIDs->Num())
			{
				OutError = TEXT("Initial Home Continuity assignment does not match the Joint Cell population.");
				Restore();
				return false;
			}
			for (int32 Offset = 0; Offset < Cell.Count; ++Offset)
			{
				const FV17IdentityRecord& AssignedIdentity = IdentityRegistry.FindChecked((*ResidentIDs)[NextIdentity + Offset]);
				HomeStatesByIndex[AssignedIdentity.HomeStateIndex] = static_cast<uint8>(Cell.Key.HomeState);
			}
			NextIdentity += Cell.Count;
		}
		RebuildHomeRepairQueues();

		bDynamicLODEnabled = true;
		OutError.Reset();
		return true;
	}

	const FV17IdentityRecord* FV17AuthoritativeMacroSession::FindIdentity(const FResidentID ResidentID) const
	{
		return IdentityRegistry.Find(ResidentID);
	}

	const FV17ContinuityCapsule* FV17AuthoritativeMacroSession::FindCapsule(const FResidentID ResidentID) const
	{
		return Capsules.Find(ResidentID);
	}

	const FV17ParticipantRef* FV17AuthoritativeMacroSession::FindParticipantRef(const FResidentID ResidentID) const
	{
		return ParticipantRefs.Find(ResidentID);
	}

	bool FV17AuthoritativeMacroSession::GetResidentHomeState(
		const FResidentID ResidentID,
		EHomeState& OutHomeState) const
	{
		const FV17IdentityRecord* Identity = IdentityRegistry.Find(ResidentID);
		if (Identity == nullptr || !HomeStatesByIndex.IsValidIndex(Identity->HomeStateIndex)) return false;
		OutHomeState = static_cast<EHomeState>(HomeStatesByIndex[Identity->HomeStateIndex]);
		return true;
	}

	void FV17AuthoritativeMacroSession::RebuildHomeRepairQueues()
	{
		HomeRepairQueues.Reset();
		TArray<FResidentID> ResidentIDs;
		IdentityRegistry.GetKeys(ResidentIDs);
		ResidentIDs.Sort();
		for (const FResidentID ResidentID : ResidentIDs)
		{
			const FV17IdentityRecord& Identity = IdentityRegistry.FindChecked(ResidentID);
			if (HomeStatesByIndex.IsValidIndex(Identity.HomeStateIndex)
				&& static_cast<EHomeState>(HomeStatesByIndex[Identity.HomeStateIndex]) == EHomeState::DamagedWaiting)
			{
				HomeRepairQueues.FindOrAdd(BuildHomeOuterKey(
					Identity.InitialKingdom,
					Identity.Profession,
					Identity.IncomeBand)).CandidateHomeStateIndices.Add(Identity.HomeStateIndex);
			}
		}
	}

	bool FV17AuthoritativeMacroSession::ApplyEarthquakeHomeDamage(
		const TArray<FResidentID>& DamagedResidentIDs,
		FString& OutError)
	{
		if (!bInitialized || !bDynamicLODEnabled)
		{
			OutError = TEXT("Earthquake Home Continuity updates require an initialized dynamic authority.");
			return false;
		}
		const FV17AuthoritativeMacroSession Before = *this;
		TSet<FResidentID> Seen;
		for (const FResidentID ResidentID : DamagedResidentIDs)
		{
			const FV17IdentityRecord* Identity = IdentityRegistry.Find(ResidentID);
			if (Identity == nullptr
				|| Seen.Contains(ResidentID)
				|| !HomeStatesByIndex.IsValidIndex(Identity->HomeStateIndex)
				|| static_cast<EHomeState>(HomeStatesByIndex[Identity->HomeStateIndex]) != EHomeState::Healthy)
			{
				OutError = TEXT("The earthquake Home Continuity list must contain unique Healthy residents.");
				*this = Before;
				return false;
			}
			Seen.Add(ResidentID);
			HomeStatesByIndex[Identity->HomeStateIndex] = static_cast<uint8>(EHomeState::DamagedWaiting);
			++HomeStateUpdateCount;
		}
		RebuildHomeRepairQueues();
		OutError.Reset();
		return true;
	}

	int64 FV17AuthoritativeMacroSession::GetRemainingWorkMinutes(const FResidentID ResidentID) const
	{
		const FActiveState* Active = ActiveStates.Find(ResidentID);
		return Active != nullptr && Active->ActiveEventID > 0
			? FMath::Max<int64>(0, Active->ActionEndTime.Minutes - Clock.Now().Minutes)
			: 0;
	}

	bool FV17AuthoritativeMacroSession::GetActiveSnapshot(
		const FResidentID ResidentID,
		FIndividualActionState& OutState,
		EIndividualAction& OutAction,
		FEventID& OutEventID) const
	{
		const FActiveState* Active = ActiveStates.Find(ResidentID);
		if (Active == nullptr) return false;
		OutState = {
			Active->Definition.Cash,
			Active->Definition.RepairCredit,
			Active->Definition.Wood,
			Active->Definition.Key.HomeState
		};
		OutAction = Active->CurrentAction;
		OutEventID = Active->ActiveEventID;
		return true;
	}

	bool FV17AuthoritativeMacroSession::SelectLiftCell(
		const FV17IdentityRecord& Identity,
		const FV17ContinuityCapsule* Capsule,
		FV17AuthoritativeCellID& OutCellID,
		bool& bOutUsedFallback,
		FString& OutError) const
	{
		OutCellID = 0;
		bOutUsedFallback = false;
		if (!HomeStatesByIndex.IsValidIndex(Identity.HomeStateIndex))
		{
			OutError = TEXT("The resident has no valid Home Continuity state.");
			return false;
		}
		const EHomeState ExactHomeState = static_cast<EHomeState>(HomeStatesByIndex[Identity.HomeStateIndex]);
		TArray<FV17AuthoritativeCellID> Candidates;
		for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Cells)
		{
			const FV17AuthoritativeJointKey& Key = Pair.Value.Key;
			if (Pair.Value.Count > 0
				&& Key.Kingdom == Identity.InitialKingdom
				&& Key.Profession == Identity.Profession
				&& Key.IncomeBand == Identity.IncomeBand
				&& Key.HomeState == ExactHomeState)
			{
				Candidates.Add(Pair.Key);
			}
		}
		Candidates.Sort();
		if (Candidates.IsEmpty())
		{
			OutError = TEXT("The resident's exact HomeState has no non-empty Joint Cell available for Lift.");
			return false;
		}

		if (Capsule != nullptr && Capsule->LastKnownCellID != 0)
		{
			const FV17AuthoritativeCellConfig* Exact = Cells.Find(Capsule->LastKnownCellID);
			if (Exact != nullptr && Exact->Count > 0 && Candidates.Contains(Capsule->LastKnownCellID))
			{
				OutCellID = Capsule->LastKnownCellID;
				OutError.Reset();
				return true;
			}

			bOutUsedFallback = true;
			int32 BestDistance = TNumericLimits<int32>::Max();
			for (const FV17AuthoritativeCellID CandidateID : Candidates)
			{
				const FV17AuthoritativeJointKey& Key = Cells.FindChecked(CandidateID).Key;
				const int32 Distance =
					(Key.HomeState == Capsule->LastObservedState.HomeState ? 0 : 16)
					+ FMath::Abs(Key.PurchasingPowerBand - PurchasingPowerBand(
						Capsule->LastObservedState.Cash + Capsule->LastObservedState.RepairCredit)) * 4
					+ FMath::Abs(Key.WoodBand - WoodBand(Capsule->LastObservedState.Wood)) * 2
					+ (Key.bAidEligible ? 1 : 0);
				if (Distance < BestDistance || (Distance == BestDistance && CandidateID < OutCellID))
				{
					BestDistance = Distance;
					OutCellID = CandidateID;
				}
			}
			OutError.Reset();
			return OutCellID != 0;
		}

		int64 TotalCount = 0;
		for (const FV17AuthoritativeCellID CandidateID : Candidates)
		{
			TotalCount += Cells.FindChecked(CandidateID).Count;
		}
		uint64 Value = Mix64(static_cast<uint64>(static_cast<uint32>(Seed)));
		Value ^= Mix64(static_cast<uint64>(Clock.Now().Minutes));
		Value ^= Mix64(static_cast<uint64>(Identity.ResidentID));
		Value ^= Mix64(0x4C49465443454C4Cull);
		int64 Pick = static_cast<int64>(Mix64(Value) % static_cast<uint64>(TotalCount));
		for (const FV17AuthoritativeCellID CandidateID : Candidates)
		{
			const int32 Count = Cells.FindChecked(CandidateID).Count;
			if (Pick < Count)
			{
				OutCellID = CandidateID;
				OutError.Reset();
				return true;
			}
			Pick -= Count;
		}
		OutError = TEXT("Deterministic weighted Joint Cell selection did not resolve a resident.");
		return false;
	}

	bool FV17AuthoritativeMacroSession::ExtractStateFromCell(
		const FV17AuthoritativeCellID CellID,
		const FResidentID ResidentID,
		FIndividualActionState& OutState,
		FString& OutError) const
	{
		const FV17AuthoritativeCellConfig* Cell = Cells.Find(CellID);
		if (Cell == nullptr || Cell->Count <= 0)
		{
			OutError = TEXT("Lift state extraction requires a non-empty Joint Cell.");
			return false;
		}

		auto ExtractBandValue = [](const int64 Total, const int32 Count, const int64 Minimum,
			const int64 Maximum, const uint64 Hash, int64& OutValue)
		{
			const bool bOpenMaximum = Maximum < 0;
			if (Total < static_cast<int64>(Count) * Minimum
				|| (!bOpenMaximum && Total > static_cast<int64>(Count) * Maximum))
			{
				return false;
			}
			const int64 Lower = bOpenMaximum
				? Minimum
				: FMath::Max<int64>(Minimum, Total - static_cast<int64>(Count - 1) * Maximum);
			const int64 Upper = FMath::Min<int64>(
				bOpenMaximum ? Total : Maximum,
				Total - static_cast<int64>(Count - 1) * Minimum);
			if (Lower > Upper) return false;
			OutValue = Lower + static_cast<int64>(Hash % static_cast<uint64>(Upper - Lower + 1));
			return true;
		};

		const int64 CashTotal = GetCellCash(CellID);
		const int64 CreditTotal = GetCellRepairCredit(CellID);
		const int64 WoodTotal = GetCellWood(CellID);
		const int64 PowerTotal = CashTotal + CreditTotal;
		const int64 PowerMinimum = Cell->Key.PurchasingPowerBand == 0 ? 0 : Cell->Key.PurchasingPowerBand == 1 ? 4 : 8;
		const int64 PowerMaximum = Cell->Key.PurchasingPowerBand == 0 ? 3 : Cell->Key.PurchasingPowerBand == 1 ? 7 : -1;
		const int64 WoodMinimum = Cell->Key.WoodBand == 0 ? 0 : Cell->Key.WoodBand == 1 ? 1 : 4;
		const int64 WoodMaximum = Cell->Key.WoodBand == 0 ? 0 : Cell->Key.WoodBand == 1 ? 3 : -1;
		uint64 Base = Mix64(static_cast<uint64>(static_cast<uint32>(Seed)));
		Base ^= Mix64(static_cast<uint64>(Clock.Now().Minutes));
		Base ^= Mix64(static_cast<uint64>(ResidentID));
		Base ^= Mix64(CellID);

		int64 Power = 0;
		int64 Wood = 0;
		if (!ExtractBandValue(PowerTotal, Cell->Count, PowerMinimum, PowerMaximum,
			Mix64(Base ^ 0x4C494654504F5745ull), Power)
			|| !ExtractBandValue(WoodTotal, Cell->Count, WoodMinimum, WoodMaximum,
				Mix64(Base ^ 0x4C494654574F4F44ull), Wood))
		{
			OutError = TEXT("Joint Cell totals cannot produce one resident while keeping the remaining Band feasible.");
			return false;
		}

		const int64 CreditMinimum = FMath::Max<int64>(0, Power - CashTotal);
		const int64 CreditMaximum = FMath::Min<int64>(Power, CreditTotal);
		if (CreditMinimum > CreditMaximum)
		{
			OutError = TEXT("Joint Cell Cash and RepairCredit totals cannot produce the selected purchasing power.");
			return false;
		}
		const int64 Credit = CreditMinimum + static_cast<int64>(
			Mix64(Base ^ 0x4C49465443524544ull) % static_cast<uint64>(CreditMaximum - CreditMinimum + 1));
		OutState.Cash = static_cast<int32>(Power - Credit);
		OutState.RepairCredit = static_cast<int32>(Credit);
		OutState.Wood = static_cast<int32>(Wood);
		OutState.HomeState = Cell->Key.HomeState;
		OutError.Reset();
		return true;
	}

	void FV17AuthoritativeMacroSession::UpdateCapsule(
		const FResidentID ResidentID,
		const FSimulationTime Time,
		const FIndividualActionState& State,
		const FV17AuthoritativeCellID CellID,
		const FEventID BatchCursor,
		const FEventID LineageEventID,
		const EIndividualAction CompletedAction)
	{
		FV17ContinuityCapsule* Existing = Capsules.Find(ResidentID);
		if (Existing == nullptr)
		{
			FV17ContinuityCapsule Capsule;
			Capsule.ResidentID = ResidentID;
			Existing = &Capsules.Add(ResidentID, MoveTemp(Capsule));
		}
		else
		{
			++Existing->CapsuleVersion;
		}
		Existing->LastObservedTime = Time;
		Existing->LastObservedState = State;
		if (CellID != 0) Existing->LastKnownCellID = CellID;
		Existing->BatchCursor = BatchCursor;
		if (LineageEventID > 0) Existing->CommittedEventLineage.AddUnique(LineageEventID);
		if (CompletedAction != EIndividualAction::None) Existing->KnownCompletedActions.AddUnique(CompletedAction);
	}

	uint64 FV17AuthoritativeMacroSession::BuildParticipantRefID(
		const FResidentID ResidentID,
		const FEventID EventID) const
	{
		uint64 Value = Mix64(static_cast<uint64>(static_cast<uint32>(Seed)));
		Value ^= Mix64(static_cast<uint64>(ResidentID));
		Value ^= Mix64(static_cast<uint64>(EventID));
		const uint64 Result = Mix64(Value ^ 0x53504C4954524546ull);
		return Result == 0 ? 1 : Result;
	}

	bool FV17AuthoritativeMacroSession::LiftFromJointCell(
		const FV17IdentityRecord& Identity,
		const FV17AuthoritativeCellID CellID,
		const FIndividualActionState State,
		const EV17LODTransitionFailurePoint FailurePoint,
		FString& OutError)
	{
		FV17AuthoritativeCellConfig& Cell = Cells.FindChecked(CellID);
		--Cell.Count;
		if (FailurePoint == EV17LODTransitionFailurePoint::LiftAfterCellCount)
		{
			OutError = TEXT("Injected B4 Lift failure after removing one Joint Cell participant.");
			return false;
		}

		const FString CashAccount = ActiveAccount(Identity.ResidentID, TEXT("Cash"));
		const FString CreditAccount = ActiveAccount(Identity.ResidentID, TEXT("RepairCredit"));
		const FString WoodAccount = ActiveAccount(Identity.ResidentID, TEXT("Wood"));
		if (!Transfer(
			Clock.Now(), ESimulationResource::Coin,
			CellAccount(CellID, TEXT("Cash")), CashAccount, State.Cash, false,
			FString::Printf(TEXT("V17-LIFT-%lld-%lld-CASH"), Identity.ResidentID, Clock.Now().Minutes),
			0, 0, 0, OutError)
			|| !Transfer(
				Clock.Now(), ESimulationResource::Coin,
				CellAccount(CellID, TEXT("RepairCredit")), CreditAccount, State.RepairCredit, false,
				FString::Printf(TEXT("V17-LIFT-%lld-%lld-CREDIT"), Identity.ResidentID, Clock.Now().Minutes),
				0, 0, 0, OutError)
			|| !Transfer(
				Clock.Now(), ESimulationResource::Wood,
				CellAccount(CellID, TEXT("Wood")), WoodAccount, State.Wood, false,
				FString::Printf(TEXT("V17-LIFT-%lld-%lld-WOOD"), Identity.ResidentID, Clock.Now().Minutes),
				0, 0, 0, OutError))
		{
			return false;
		}
		if (FailurePoint == EV17LODTransitionFailurePoint::LiftAfterLedgerTransfer)
		{
			OutError = TEXT("Injected B4 Lift failure after moving personal resources.");
			return false;
		}

		FV17AuthoritativeActiveConfig Definition;
		Definition.ResidentID = Identity.ResidentID;
		Definition.SourceCellID = CellID;
		Definition.Key = Cell.Key;
		Definition.Cash = State.Cash;
		Definition.RepairCredit = State.RepairCredit;
		Definition.Wood = State.Wood;
		ActiveStates.Add(Identity.ResidentID, { Definition, true });
		OutError.Reset();
		return true;
	}

	FIndividualActionState FV17AuthoritativeMacroSession::ReadPendingEventState(
		const FV17AuthoritativeBatchEvent& Event) const
	{
		FIndividualActionState State = Event.FrozenState;
		const int32 Count = FMath::Max(1, Event.ParticipantCount);
		State.Cash = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
			ESimulationResource::Coin, EventAccount(Event.BatchEventID, TEXT("Cash")))) / Count);
		State.RepairCredit = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
			ESimulationResource::Coin, EventAccount(Event.BatchEventID, TEXT("RepairCredit")))) / Count);
		State.Wood = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
			ESimulationResource::Wood, EventAccount(Event.BatchEventID, TEXT("Wood")))) / Count);
		if (Event.Action == EIndividualAction::ContinueRepair) State.HomeState = EHomeState::UnderRepair;
		return State;
	}

	bool FV17AuthoritativeMacroSession::LiftFromPendingEvent(
		const FV17IdentityRecord& Identity,
		FV17ParticipantRef& ParticipantRef,
		const EV17LODTransitionFailurePoint FailurePoint,
		FString& OutError)
	{
		FV17AuthoritativeBatchEvent* Parent = BatchEvents.Find(ParticipantRef.BatchEventID);
		if (Parent == nullptr || Parent->Status != ESimulationEventState::Pending || Parent->ActiveResidentID != 0)
		{
			OutError = TEXT("ParticipantRef does not point to a pending aggregate Batch Event.");
			return false;
		}
		const FSimulationEventRecord* StoredParent = EventStore.Find(Parent->BatchEventID);
		if (StoredParent == nullptr || StoredParent->State != ESimulationEventState::Pending)
		{
			OutError = TEXT("ParticipantRef Batch Event is missing from the event store.");
			return false;
		}
		const bool bRepairEvent = Parent->Action == EIndividualAction::ContinueRepair;
		if (bRepairEvent
			&& (!Parent->AssignedHomeStateIndices.Contains(Identity.HomeStateIndex)
				|| PendingRepairEventByHomeStateIndex.FindRef(Identity.HomeStateIndex) != Parent->BatchEventID))
		{
			OutError = TEXT("The resident's HomeID is not owned by the pending Repair Batch Event.");
			return false;
		}

		FEventID ActiveEventID = Parent->BatchEventID;
		if (Parent->ParticipantCount == 1)
		{
			if (!EventStore.ConvertPendingEventToIndividual(
				Parent->BatchEventID,
				StoredParent->Event.Owner,
				ActiveOwner(Identity.ResidentID),
				Identity.ResidentID,
				OutError))
			{
				return false;
			}
			Parent->ActiveResidentID = Identity.ResidentID;
		}
		else
		{
			const int32 ParentCountBefore = Parent->ParticipantCount;
			FSimulationEventRequest ChildRequest = StoredParent->Event;
			ChildRequest.Owner = ActiveOwner(Identity.ResidentID);
			ChildRequest.ResidentID = Identity.ResidentID;
			ChildRequest.ParticipantCount = 1;
			ChildRequest.ParentEventID = Parent->BatchEventID;
			ChildRequest.ArriveID = Scheduler.IssueArriveID();
			ChildRequest.ReservationID = 0;
			FEventID ChildEventID = 0;
			if (!EventStore.CreateEvent(ChildRequest, ChildEventID, OutError)) return false;

			FReservationID ChildReservationID = 0;
			if (Parent->BatchReservationID > 0)
			{
				const FV17AuthoritativeClaim* Claim = Claims.Find(Parent->BatchClaimID);
				if (Claim == nullptr
					|| !Reservations.SplitReservation(
						Parent->BatchReservationID,
						ChildEventID,
						ChildRequest.ArriveID,
						Claim->PerParticipantDemand,
						ChildReservationID,
						OutError)
					|| !EventStore.SetReservationID(ChildEventID, ChildReservationID, OutError))
				{
					return false;
				}
			}
			if (!Scheduler.Schedule({ ChildEventID, ChildRequest.ArriveID, ChildRequest.EndTime }, Clock.Now(), OutError))
			{
				return false;
			}

			const struct
			{
				ESimulationResource Resource;
				const TCHAR* Stock;
			} Stocks[] =
			{
				{ ESimulationResource::Coin, TEXT("Cash") },
				{ ESimulationResource::Coin, TEXT("RepairCredit") },
				{ ESimulationResource::Wood, TEXT("Wood") }
			};
			for (const auto& Stock : Stocks)
			{
				const int64 Total = FMath::RoundToInt64(Ledger.GetBalance(
					Stock.Resource, EventAccount(Parent->BatchEventID, Stock.Stock)));
				const int64 Share = Total / ParentCountBefore;
				if (!Transfer(
					Clock.Now(), Stock.Resource,
					EventAccount(Parent->BatchEventID, Stock.Stock),
					EventAccount(ChildEventID, Stock.Stock),
					Share, false,
					FString::Printf(TEXT("V17-SPLIT-%lld-%lld-%s"), Parent->BatchEventID, ChildEventID, Stock.Stock),
					ChildEventID, ChildRequest.ArriveID, Parent->CausalPolicyID, OutError))
				{
					return false;
				}
			}

			if (bRepairEvent && Parent->AssignedHomeStateIndices.RemoveSingle(Identity.HomeStateIndex) != 1)
			{
				OutError = TEXT("Repair Batch split could not move the resident's exact HomeID.");
				return false;
			}
			--Parent->ParticipantCount;
			if (!EventStore.SetPendingParticipantCount(
				Parent->BatchEventID, Parent->ParticipantCount, OutError))
			{
				return false;
			}
			FV17AuthoritativeBatchEvent Child = *Parent;
			Child.BatchEventID = ChildEventID;
			Child.ParentBatchEventID = Parent->BatchEventID;
			Child.ParticipantCount = 1;
			Child.BatchReservationID = ChildReservationID;
			Child.ActiveResidentID = Identity.ResidentID;
			Child.InheritedOrderKey = ParticipantRef.InheritedOrderKey;
			Child.RemainingWorkMinutes = FMath::Max<int64>(0, Child.EndTime.Minutes - Clock.Now().Minutes);
			if (bRepairEvent)
			{
				Child.AssignedHomeStateIndices.Reset(1);
				Child.AssignedHomeStateIndices.Add(Identity.HomeStateIndex);
				PendingRepairEventByHomeStateIndex.FindChecked(Identity.HomeStateIndex) = ChildEventID;
			}
			BatchEvents.Add(ChildEventID, Child);
			ActiveEventID = ChildEventID;
			ParticipantRef.BatchEventID = ChildEventID;
			ParticipantRef.ParentBatchEventID = Parent->BatchEventID;
			ParticipantRef.ReservationID = ChildReservationID;
		}

		FV17AuthoritativeBatchEvent& ActiveEvent = BatchEvents.FindChecked(ActiveEventID);
		const FIndividualActionState State = ReadPendingEventState(ActiveEvent);
		const FV17AuthoritativeCellConfig& SourceCell = Cells.FindChecked(ActiveEvent.SourceCellID);
		FV17AuthoritativeActiveConfig Definition;
		Definition.ResidentID = Identity.ResidentID;
		Definition.SourceCellID = ActiveEvent.SourceCellID;
		Definition.Key = SourceCell.Key;
		Definition.Key.HomeState = State.HomeState;
		Definition.Key.Intent = ToMacroIntent(ActiveEvent.Action);
		Definition.Key.PurchasingPowerBand = PurchasingPowerBand(State.Cash + State.RepairCredit);
		Definition.Key.WoodBand = WoodBand(State.Wood);
		Definition.Cash = State.Cash;
		Definition.RepairCredit = State.RepairCredit;
		Definition.Wood = State.Wood;
		FActiveState Active;
		Active.Definition = Definition;
		Active.bReady = false;
		Active.CurrentAction = ActiveEvent.Action;
		Active.ActiveEventID = ActiveEvent.BatchEventID;
		Active.ParentEventID = ActiveEvent.ParentBatchEventID;
		Active.ActiveArriveID = EventStore.Find(ActiveEvent.BatchEventID)->Event.ArriveID;
		Active.ActiveReservationID = ActiveEvent.BatchReservationID;
		Active.InheritedOrderKey = ActiveEvent.InheritedOrderKey;
		Active.ActionStartTime = ActiveEvent.StartTime;
		Active.ActionEndTime = ActiveEvent.EndTime;
		ActiveStates.Add(Identity.ResidentID, MoveTemp(Active));
		ParticipantRef.ParticipantRefID = BuildParticipantRefID(Identity.ResidentID, ActiveEventID);
		ParticipantRef.InheritedOrderKey = ActiveEvent.InheritedOrderKey;
		if (FailurePoint == EV17LODTransitionFailurePoint::LiftAfterEventSplit)
		{
			OutError = TEXT("Injected B4 Lift failure after splitting a pending Batch Event.");
			return false;
		}
		OutError.Reset();
		return true;
	}

	bool FV17AuthoritativeMacroSession::LiftResident(
		const FResidentID ResidentID,
		const FSimulationTime Time,
		FString& OutError,
		const EV17LODTransitionFailurePoint FailurePoint)
	{
		FV17LODTransitionRecord Transition;
		Transition.ResidentID = ResidentID;
		Transition.GameTime = Time;
		Transition.bLift = true;
		if (!bInitialized || !bDynamicLODEnabled || !IsSameTime(Time, Clock.Now()))
		{
			OutError = TEXT("B4 Lift requires an initialized dynamic session at the current authoritative time.");
			return false;
		}
		const FV17IdentityRecord* Identity = IdentityRegistry.Find(ResidentID);
		if (Identity == nullptr)
		{
			Transition.Result = EV17LODTransitionResult::ResidentNotFound;
			LODTransitions.Add(Transition);
			OutError = TEXT("ResidentID does not exist in the Identity Registry.");
			return false;
		}
		if (ActiveStates.Contains(ResidentID))
		{
			Transition.Result = EV17LODTransitionResult::AlreadyActive;
			LODTransitions.Add(Transition);
			OutError = TEXT("Resident is already Active.");
			return false;
		}
		if (ActiveStates.Num() >= ActiveMicroCap)
		{
			Transition.Result = EV17LODTransitionResult::ActiveCapReached;
			LODTransitions.Add(Transition);
			OutError = TEXT("The global Active Micro cap of 50 has been reached.");
			return false;
		}

		const FV17AuthoritativeMacroSession Before = *this;
		auto Restore = [this, &Before]() { *this = Before; };
		FIndividualActionState State;
		FV17AuthoritativeCellID SelectedCellID = 0;
		bool bUsedFallback = false;
		FV17ParticipantRef* ParticipantRef = ParticipantRefs.Find(ResidentID);
		if (ParticipantRef == nullptr)
		{
			if (const FEventID* RepairEventID = PendingRepairEventByHomeStateIndex.Find(Identity->HomeStateIndex))
			{
				const FV17AuthoritativeBatchEvent* RepairEvent = BatchEvents.Find(*RepairEventID);
				if (RepairEvent == nullptr
					|| RepairEvent->Status != ESimulationEventState::Pending
					|| RepairEvent->Action != EIndividualAction::ContinueRepair)
				{
					Transition.Result = EV17LODTransitionResult::EventSplitFailed;
					OutError = TEXT("The resident's HomeID points to a missing pending Repair Batch Event.");
					Restore();
					return false;
				}
				FV17ParticipantRef NewRef;
				NewRef.ParticipantRefID = BuildParticipantRefID(ResidentID, *RepairEventID);
				NewRef.ResidentID = ResidentID;
				NewRef.BatchEventID = *RepairEventID;
				NewRef.ParentBatchEventID = RepairEvent->ParentBatchEventID;
				NewRef.ReservationID = RepairEvent->BatchReservationID;
				NewRef.InheritedOrderKey = RepairEvent->InheritedOrderKey;
				ParticipantRef = &ParticipantRefs.Add(ResidentID, NewRef);
			}
		}
		if (ParticipantRef != nullptr)
		{
			SelectedCellID = BatchEvents.FindChecked(ParticipantRef->BatchEventID).SourceCellID;
			if (!LiftFromPendingEvent(*Identity, *ParticipantRef, FailurePoint, OutError))
			{
				Restore();
				return false;
			}
			const FActiveState& Active = ActiveStates.FindChecked(ResidentID);
			State = {
				Active.Definition.Cash,
				Active.Definition.RepairCredit,
				Active.Definition.Wood,
				Active.Definition.Key.HomeState
			};
			Transition.BatchEventID = Active.ActiveEventID;
			Transition.ParentBatchEventID = Active.ParentEventID;
		}
		else
		{
			if (SelectLiftCell(*Identity, Capsules.Find(ResidentID), SelectedCellID, bUsedFallback, OutError))
			{
				if (!ExtractStateFromCell(SelectedCellID, ResidentID, State, OutError)
					|| !LiftFromJointCell(*Identity, SelectedCellID, State, FailurePoint, OutError))
				{
					Restore();
					return false;
				}
			}
			else
			{
				const EHomeState ExactHomeState = static_cast<EHomeState>(
					HomeStatesByIndex[Identity->HomeStateIndex]);
				TArray<FEventID> PendingCandidates;
				for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : BatchEvents)
				{
					const FV17AuthoritativeBatchEvent& Event = Pair.Value;
					const FV17AuthoritativeCellConfig* SourceCell = Cells.Find(Event.SourceCellID);
					if (Event.Status == ESimulationEventState::Pending
						&& Event.ActiveResidentID == 0
						&& Event.ParticipantCount > 0
						&& SourceCell != nullptr
						&& SourceCell->Key.Kingdom == Identity->InitialKingdom
						&& SourceCell->Key.Profession == Identity->Profession
						&& SourceCell->Key.IncomeBand == Identity->IncomeBand
						&& (Event.Action == EIndividualAction::ContinueRepair
							? EHomeState::UnderRepair
							: SourceCell->Key.HomeState) == ExactHomeState)
					{
						PendingCandidates.Add(Event.BatchEventID);
					}
				}
				PendingCandidates.Sort();
				if (PendingCandidates.IsEmpty())
				{
					Transition.Result = EV17LODTransitionResult::NoEligibleJointCell;
					Restore();
					return false;
				}

				FEventID SelectedEventID = 0;
				if (const FV17ContinuityCapsule* Capsule = Capsules.Find(ResidentID))
				{
					if (PendingCandidates.Contains(Capsule->BatchCursor))
					{
						SelectedEventID = Capsule->BatchCursor;
					}
				}
				if (SelectedEventID == 0)
				{
					int64 TotalCount = 0;
					for (const FEventID EventID : PendingCandidates)
					{
						TotalCount += BatchEvents.FindChecked(EventID).ParticipantCount;
					}
					uint64 Value = Mix64(static_cast<uint64>(static_cast<uint32>(Seed)));
					Value ^= Mix64(static_cast<uint64>(Clock.Now().Minutes));
					Value ^= Mix64(static_cast<uint64>(ResidentID));
					int64 Pick = static_cast<int64>(Mix64(Value ^ 0x4C49465445564E54ull)
						% static_cast<uint64>(TotalCount));
					for (const FEventID EventID : PendingCandidates)
					{
						const int32 Count = BatchEvents.FindChecked(EventID).ParticipantCount;
						if (Pick < Count)
						{
							SelectedEventID = EventID;
							break;
						}
						Pick -= Count;
					}
				}

				FV17AuthoritativeBatchEvent& SelectedEvent = BatchEvents.FindChecked(SelectedEventID);
				SelectedCellID = SelectedEvent.SourceCellID;
				FV17ParticipantRef NewRef;
				NewRef.ParticipantRefID = BuildParticipantRefID(ResidentID, SelectedEventID);
				NewRef.ResidentID = ResidentID;
				NewRef.BatchEventID = SelectedEventID;
				NewRef.ParentBatchEventID = SelectedEvent.ParentBatchEventID;
				NewRef.ReservationID = SelectedEvent.BatchReservationID;
				NewRef.InheritedOrderKey = SelectedEvent.InheritedOrderKey;
				ParticipantRef = &ParticipantRefs.Add(ResidentID, NewRef);
				if (!LiftFromPendingEvent(*Identity, *ParticipantRef, FailurePoint, OutError))
				{
					Restore();
					return false;
				}
				const FActiveState& Active = ActiveStates.FindChecked(ResidentID);
				State = {
					Active.Definition.Cash,
					Active.Definition.RepairCredit,
					Active.Definition.Wood,
					Active.Definition.Key.HomeState
				};
				Transition.BatchEventID = Active.ActiveEventID;
				Transition.ParentBatchEventID = Active.ParentEventID;
				bUsedFallback = true;
			}
			if (bUsedFallback) ++LiftReconstructionFallbackCount;
		}

		UpdateCapsule(
			ResidentID,
			Time,
			State,
			SelectedCellID,
			Transition.BatchEventID,
			Transition.ParentBatchEventID);
		Transition.SelectedCellID = SelectedCellID;
		Transition.bUsedFallback = bUsedFallback;
		Transition.Result = EV17LODTransitionResult::Committed;
		LODTransitions.Add(Transition);
		OutError.Reset();
		return true;
	}

	bool FV17AuthoritativeMacroSession::RestrictIdleResident(
		const FResidentID ResidentID,
		FActiveState& Active,
		const EV17LODTransitionFailurePoint FailurePoint,
		FV17AuthoritativeCellID& OutTargetCellID,
		FString& OutError)
	{
		const FIndividualActionState State = {
			Active.Definition.Cash,
			Active.Definition.RepairCredit,
			Active.Definition.Wood,
			Active.Definition.Key.HomeState
		};
		OutTargetCellID = FindOrCreateTargetCell(Active.Definition.Key, State);
		const struct
		{
			ESimulationResource Resource;
			const TCHAR* Stock;
		} Stocks[] =
		{
			{ ESimulationResource::Coin, TEXT("Cash") },
			{ ESimulationResource::Coin, TEXT("RepairCredit") },
			{ ESimulationResource::Wood, TEXT("Wood") }
		};
		for (const auto& Stock : Stocks)
		{
			const int64 Quantity = FMath::RoundToInt64(Ledger.GetBalance(
				Stock.Resource, ActiveAccount(ResidentID, Stock.Stock)));
			if (!Transfer(
				Clock.Now(), Stock.Resource,
				ActiveAccount(ResidentID, Stock.Stock), CellAccount(OutTargetCellID, Stock.Stock),
				Quantity, false,
				FString::Printf(TEXT("V17-RESTRICT-%lld-%lld-%s"), ResidentID, Clock.Now().Minutes, Stock.Stock),
				0, 0, 0, OutError))
			{
				return false;
			}
		}
		if (FailurePoint == EV17LODTransitionFailurePoint::RestrictAfterLedgerTransfer)
		{
			OutError = TEXT("Injected B4 Restrict failure after returning personal resources.");
			return false;
		}
		if (!Ledger.RemoveZeroBalanceAccount(
			ESimulationResource::Coin, ActiveAccount(ResidentID, TEXT("Cash")), OutError)
			|| !Ledger.RemoveZeroBalanceAccount(
				ESimulationResource::Coin, ActiveAccount(ResidentID, TEXT("RepairCredit")), OutError)
			|| !Ledger.RemoveZeroBalanceAccount(
				ESimulationResource::Wood, ActiveAccount(ResidentID, TEXT("Wood")), OutError))
		{
			return false;
		}
		++Cells.FindChecked(OutTargetCellID).Count;
		ParticipantRefs.Remove(ResidentID);
		ActiveStates.Remove(ResidentID);
		OutError.Reset();
		return true;
	}

	bool FV17AuthoritativeMacroSession::SameFrozenState(
		const FIndividualActionState& Left,
		const FIndividualActionState& Right)
	{
		return Left.Cash == Right.Cash
			&& Left.RepairCredit == Right.RepairCredit
			&& Left.Wood == Right.Wood
			&& Left.HomeState == Right.HomeState;
	}

	FEventID FV17AuthoritativeMacroSession::FindCompatibleMergeTarget(
		const FV17AuthoritativeBatchEvent& ActiveEvent) const
	{
		TArray<FEventID> EventIDs;
		BatchEvents.GetKeys(EventIDs);
		EventIDs.Sort([&ActiveEvent](const FEventID Left, const FEventID Right)
		{
			if (Left != Right && Left == ActiveEvent.ParentBatchEventID) return true;
			if (Left != Right && Right == ActiveEvent.ParentBatchEventID) return false;
			return Left < Right;
		});
		for (const FEventID EventID : EventIDs)
		{
			const FV17AuthoritativeBatchEvent& Candidate = BatchEvents.FindChecked(EventID);
			if (EventID == ActiveEvent.BatchEventID
				|| Candidate.Status != ESimulationEventState::Pending
				|| Candidate.ActiveResidentID != 0
				|| Candidate.Action != ActiveEvent.Action
				|| Candidate.SourceCellID != ActiveEvent.SourceCellID
				|| Candidate.EndTime.Minutes != ActiveEvent.EndTime.Minutes
				|| Candidate.CausalPolicyID != ActiveEvent.CausalPolicyID
				|| !SameFrozenState(Candidate.FrozenState, ActiveEvent.FrozenState))
			{
				continue;
			}
			if (ActiveEvent.ParentBatchEventID > 0
				&& EventID != ActiveEvent.ParentBatchEventID
				&& Candidate.ParentBatchEventID != ActiveEvent.ParentBatchEventID)
			{
				continue;
			}
			if ((Candidate.BatchReservationID == 0) != (ActiveEvent.BatchReservationID == 0))
			{
				continue;
			}
			if (Candidate.BatchReservationID > 0)
			{
				const FReservationRecord* Left = Reservations.Find(Candidate.BatchReservationID);
				const FReservationRecord* Right = Reservations.Find(ActiveEvent.BatchReservationID);
				if (Left == nullptr || Right == nullptr
					|| Left->State != EReservationState::Active
					|| Right->State != EReservationState::Active
					|| Left->Request.Resource != Right->Request.Resource
					|| Left->Request.SourceAccount != Right->Request.SourceAccount
					|| Left->Request.ReservedAccount != Right->Request.ReservedAccount)
				{
					continue;
				}
			}
			return EventID;
		}
		return 0;
	}

	bool FV17AuthoritativeMacroSession::MoveEventAccountBalances(
		const FEventID SourceEventID,
		const FEventID TargetEventID,
		const FPolicyID PolicyID,
		FString& OutError)
	{
		const struct
		{
			ESimulationResource Resource;
			const TCHAR* Stock;
		} Stocks[] =
		{
			{ ESimulationResource::Coin, TEXT("Cash") },
			{ ESimulationResource::Coin, TEXT("RepairCredit") },
			{ ESimulationResource::Wood, TEXT("Wood") }
		};
		for (const auto& Stock : Stocks)
		{
			const int64 Quantity = FMath::RoundToInt64(Ledger.GetBalance(
				Stock.Resource, EventAccount(SourceEventID, Stock.Stock)));
			if (!Transfer(
				Clock.Now(), Stock.Resource,
				EventAccount(SourceEventID, Stock.Stock), EventAccount(TargetEventID, Stock.Stock),
				Quantity, false,
				FString::Printf(TEXT("V17-MERGE-%lld-%lld-%s"), SourceEventID, TargetEventID, Stock.Stock),
				TargetEventID, 0, PolicyID, OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool FV17AuthoritativeMacroSession::RestrictPendingResident(
		const FResidentID ResidentID,
		FActiveState& Active,
		const EV17LODTransitionFailurePoint FailurePoint,
		FEventID& OutBatchEventID,
		FEventID& OutParentEventID,
		FString& OutError)
	{
		FV17AuthoritativeBatchEvent* ActiveEvent = BatchEvents.Find(Active.ActiveEventID);
		const FSimulationEventRecord* Stored = EventStore.Find(Active.ActiveEventID);
		if (ActiveEvent == nullptr
			|| Stored == nullptr
			|| ActiveEvent->Status != ESimulationEventState::Pending
			|| ActiveEvent->ActiveResidentID != ResidentID)
		{
			OutError = TEXT("Active resident's pending event is missing or no longer owned by that resident.");
			return false;
		}

		const FEventID ActiveEventID = ActiveEvent->BatchEventID;
		const FEventID MergeTargetID = FindCompatibleMergeTarget(*ActiveEvent);
		FV17ParticipantRef Ref;
		Ref.ParticipantRefID = BuildParticipantRefID(ResidentID, ActiveEventID);
		Ref.ResidentID = ResidentID;
		Ref.BatchEventID = ActiveEventID;
		Ref.ParentBatchEventID = ActiveEvent->ParentBatchEventID;
		Ref.ReservationID = ActiveEvent->BatchReservationID;
		Ref.InheritedOrderKey = Active.InheritedOrderKey;

		if (MergeTargetID > 0)
		{
			FV17AuthoritativeBatchEvent& Target = BatchEvents.FindChecked(MergeTargetID);
			if (!MoveEventAccountBalances(ActiveEventID, MergeTargetID, ActiveEvent->CausalPolicyID, OutError)
				|| (ActiveEvent->BatchReservationID > 0
					&& !Reservations.MergeReservations(
						Target.BatchReservationID,
						ActiveEvent->BatchReservationID,
						OutError)))
			{
				return false;
			}
			if (ActiveEvent->Action == EIndividualAction::ContinueRepair)
			{
				const FV17IdentityRecord* Identity = IdentityRegistry.Find(ResidentID);
				if (Identity == nullptr
					|| ActiveEvent->AssignedHomeStateIndices.Num() != 1
					|| ActiveEvent->AssignedHomeStateIndices[0] != Identity->HomeStateIndex
					|| PendingRepairEventByHomeStateIndex.FindRef(Identity->HomeStateIndex) != ActiveEventID)
				{
					OutError = TEXT("Repair Batch merge could not find the resident's exact HomeID ownership.");
					return false;
				}
				Target.AssignedHomeStateIndices.Add(Identity->HomeStateIndex);
				Target.AssignedHomeStateIndices.Sort();
				PendingRepairEventByHomeStateIndex.FindChecked(Identity->HomeStateIndex) = MergeTargetID;
			}
			++Target.ParticipantCount;
			if (!EventStore.SetPendingParticipantCount(MergeTargetID, Target.ParticipantCount, OutError))
			{
				return false;
			}
			FScheduledEvent Removed;
			if (!Scheduler.RemovePending(ActiveEventID, Removed, OutError)
				|| !EventStore.RemovePendingEvent(ActiveEventID, OutError))
			{
				return false;
			}
			Ref.ParticipantRefID = BuildParticipantRefID(ResidentID, MergeTargetID);
			Ref.BatchEventID = MergeTargetID;
			Ref.ParentBatchEventID = Target.ParentBatchEventID;
			Ref.ReservationID = Target.BatchReservationID;
			OutBatchEventID = MergeTargetID;
			OutParentEventID = Target.ParentBatchEventID;
			BatchEvents.Remove(ActiveEventID);
		}
		else
		{
			if (!EventStore.ConvertPendingEventToAggregate(
				ActiveEventID,
				Stored->Event.Owner,
				BatchOwner(ActiveEvent->SourceCellID),
				OutError))
			{
				return false;
			}
			ActiveEvent->ActiveResidentID = 0;
			OutBatchEventID = ActiveEventID;
			OutParentEventID = ActiveEvent->ParentBatchEventID;
		}
		if (!Ledger.RemoveZeroBalanceAccount(
			ESimulationResource::Coin, ActiveAccount(ResidentID, TEXT("Cash")), OutError)
			|| !Ledger.RemoveZeroBalanceAccount(
				ESimulationResource::Coin, ActiveAccount(ResidentID, TEXT("RepairCredit")), OutError)
			|| !Ledger.RemoveZeroBalanceAccount(
				ESimulationResource::Wood, ActiveAccount(ResidentID, TEXT("Wood")), OutError))
		{
			return false;
		}
		ParticipantRefs.Add(ResidentID, Ref);
		ActiveStates.Remove(ResidentID);
		if (FailurePoint == EV17LODTransitionFailurePoint::RestrictAfterEventMerge)
		{
			OutError = TEXT("Injected B4 Restrict failure after merging a pending personal event.");
			return false;
		}
		OutError.Reset();
		return true;
	}

	bool FV17AuthoritativeMacroSession::RestrictResident(
		const FResidentID ResidentID,
		const FSimulationTime Time,
		FString& OutError,
		const EV17LODTransitionFailurePoint FailurePoint)
	{
		FV17LODTransitionRecord Transition;
		Transition.ResidentID = ResidentID;
		Transition.GameTime = Time;
		Transition.bLift = false;
		if (!bInitialized || !bDynamicLODEnabled || !IsSameTime(Time, Clock.Now()))
		{
			OutError = TEXT("B4 Restrict requires an initialized dynamic session at the current authoritative time.");
			return false;
		}
		FActiveState* Active = ActiveStates.Find(ResidentID);
		if (Active == nullptr)
		{
			Transition.Result = EV17LODTransitionResult::AlreadyRestricted;
			LODTransitions.Add(Transition);
			OutError = TEXT("Resident is not Active.");
			return false;
		}
		for (const FV17AuthoritativeClaimID ClaimID : QueuedClaimIDs)
		{
			if (Claims.FindChecked(ClaimID).ActiveResidentID == ResidentID)
			{
				OutError = TEXT("Restrict cannot run while the resident has an uncommitted Count=1 Claim.");
				return false;
			}
		}
		const FV17IdentityRecord* Identity = IdentityRegistry.Find(ResidentID);
		if (Identity == nullptr
			|| !HomeStatesByIndex.IsValidIndex(Identity->HomeStateIndex)
			|| static_cast<EHomeState>(HomeStatesByIndex[Identity->HomeStateIndex]) != Active->Definition.Key.HomeState)
		{
			OutError = TEXT("Restrict requires the Active HomeState to match the resident's HomeID state.");
			return false;
		}

		const FV17AuthoritativeMacroSession Before = *this;
		auto Restore = [this, &Before]() { *this = Before; };
		const FIndividualActionState ObservedState = {
			Active->Definition.Cash,
			Active->Definition.RepairCredit,
			Active->Definition.Wood,
			Active->Definition.Key.HomeState
		};
		FV17AuthoritativeCellID TargetCellID = Active->Definition.SourceCellID;
		FEventID BatchEventID = 0;
		FEventID ParentEventID = 0;
		if (Active->ActiveEventID > 0)
		{
			if (!RestrictPendingResident(
				ResidentID, *Active, FailurePoint, BatchEventID, ParentEventID, OutError))
			{
				Restore();
				return false;
			}
		}
		else if (!RestrictIdleResident(
			ResidentID, *Active, FailurePoint, TargetCellID, OutError))
		{
			Restore();
			return false;
		}

		UpdateCapsule(
			ResidentID,
			Time,
			ObservedState,
			TargetCellID,
			BatchEventID,
			ParentEventID);
		Transition.SelectedCellID = TargetCellID;
		Transition.BatchEventID = BatchEventID;
		Transition.ParentBatchEventID = ParentEventID;
		Transition.Result = EV17LODTransitionResult::Committed;
		LODTransitions.Add(Transition);
		OutError.Reset();
		return true;
	}
}
