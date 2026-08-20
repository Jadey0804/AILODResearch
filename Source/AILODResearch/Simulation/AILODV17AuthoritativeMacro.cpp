// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODV17AuthoritativeMacro.h"

#include "AILODDomainRules.h"
#include "AILODStatePreservingLOD.h"
#include "Misc/SecureHash.h"

namespace AILOD
{
	using namespace DomainRules;

	namespace
	{
		const TCHAR* EventType(const EIndividualAction Action)
		{
			return Action == EIndividualAction::ContinueRepair ? TEXT("BatchRepair") : ToString(Action);
		}

		bool IsSupportedAction(const EIndividualAction Action)
		{
			return Action == EIndividualAction::Routine
				|| Action == EIndividualAction::Work
				|| Action == EIndividualAction::BuyWood
				|| Action == EIndividualAction::ChopWood
				|| Action == EIndividualAction::StartRepair
				|| Action == EIndividualAction::Wait;
		}
	}

	bool FV17AuthoritativeAudit::IsHardErrorFree() const
	{
		return PopulationResidual == 0
			&& FMath::IsNearlyZero(CoinResidual, UE_DOUBLE_SMALL_NUMBER)
			&& FMath::IsNearlyZero(WoodResidual, UE_DOUBLE_SMALL_NUMBER)
			&& NegativeJointCellCount == 0
			&& NegativeStockCount == 0
			&& BatchRequestedGrantResidualCount == 0
			&& BatchCapacityOverflowCount == 0
			&& DuplicateBatchCommitCount == 0
			&& PendingEventResidualCount == 0
			&& ReservationResidualCount == 0
			&& CommitResidueCount == 0
			&& OwnerConflictCount == 0
			&& ActiveCapViolationCount == 0
			&& DuplicateTransactionCount == 0
			&& DuplicateCompletionCount == 0
			&& IdentityMismatchCount == 0
			&& CapsuleIdentityMismatchCount == 0
			&& BatchSplitMergeResidualCount == 0
			&& LiftRestrictResidueCount == 0
			&& TaskResetCount == 0;
	}

	FV17AuthoritativeMacroSession::FV17AuthoritativeMacroSession(const int32 InSeed)
		: Seed(InSeed)
	{
	}

	int32 FV17AuthoritativeMacroSession::PurchasingPowerBand(const int64 PurchasingPower)
	{
		return PurchasingPower <= 3 ? 0 : PurchasingPower <= 7 ? 1 : 2;
	}

	int32 FV17AuthoritativeMacroSession::WoodBand(const int64 Wood)
	{
		return Wood <= 0 ? 0 : Wood < static_cast<int64>(RepairWoodPerHome) ? 1 : 2;
	}

	FString FV17AuthoritativeMacroSession::CellAccount(
		const FV17AuthoritativeCellID CellID,
		const TCHAR* Stock)
	{
		return FString::Printf(TEXT("V17.JointCell.%llu.%s"), CellID, Stock);
	}

	FString FV17AuthoritativeMacroSession::ActiveAccount(
		const FResidentID ResidentID,
		const TCHAR* Stock)
	{
		return FString::Printf(TEXT("V17.Active.%lld.%s"), ResidentID, Stock);
	}

	FString FV17AuthoritativeMacroSession::EventAccount(
		const FEventID EventID,
		const TCHAR* Stock)
	{
		return FString::Printf(TEXT("V17.BatchEvent.%lld.%s"), EventID, Stock);
	}

	FString FV17AuthoritativeMacroSession::KingdomAccount(
		const EKingdom Kingdom,
		const TCHAR* Stock)
	{
		return MakeKingdomAccount(Kingdom, Stock);
	}

	bool FV17AuthoritativeMacroSession::Initialize(
		const TArray<FV17AuthoritativeCellConfig>& InCells,
		const TArray<FV17AuthoritativeActiveConfig>& InActiveResidents,
		const TArray<FV17AuthoritativeKingdomConfig>& InKingdoms,
		const FSimulationTime StartTime,
		FString& OutError)
	{
		if (bInitialized || InKingdoms.IsEmpty())
		{
			OutError = TEXT("The v1.7 authoritative Macro session requires one initialization and at least one kingdom.");
			return false;
		}

		FResourceLedger NewLedger;
		TMap<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig> NewCells;
		TMap<FV17AuthoritativeJointKey, FV17AuthoritativeCellID> NewCellIDsByKey;
		TMap<FResidentID, FActiveState> NewActiveStates;
		TMap<EKingdom, FV17AuthoritativeKingdomConfig> NewKingdomConfigs;
		TMap<EKingdom, int32> NewRepairCapacity;
		TMap<EKingdom, int64> NewHarvestRemaining;
		int32 NewPopulation = 0;

		for (const FV17AuthoritativeKingdomConfig& Kingdom : InKingdoms)
		{
			if (NewKingdomConfigs.Contains(Kingdom.Kingdom)
				|| Kingdom.MarketWood < 0
				|| Kingdom.ForestWood < 0
				|| Kingdom.HarvestCapacity < 0
				|| Kingdom.MarketCoin < 0
				|| Kingdom.EmbeddedRepairWood < 0
				|| Kingdom.RepairedHomeWood < 0
				|| Kingdom.RepairCapacity < 0
				|| !FMath::IsFinite(Kingdom.WoodPrice)
				|| Kingdom.WoodPrice <= 0.0)
			{
				OutError = TEXT("Kingdom resource settings must be unique, finite and non-negative.");
				return false;
			}

			const struct
			{
				ESimulationResource Resource;
				const TCHAR* Stock;
				int64 Quantity;
			} Accounts[] =
			{
				{ ESimulationResource::Wood, TEXT("MarketWoodAvailable"), Kingdom.MarketWood },
				{ ESimulationResource::Wood, TEXT("MarketWoodReserved"), 0 },
				{ ESimulationResource::Wood, TEXT("ForestWood"), Kingdom.ForestWood },
				{ ESimulationResource::Wood, TEXT("ForestWoodReserved"), 0 },
				{ ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs"), Kingdom.EmbeddedRepairWood },
				{ ESimulationResource::Wood, TEXT("WoodInRepairedHomes"), Kingdom.RepairedHomeWood },
				{ ESimulationResource::Coin, TEXT("MarketCoin"), Kingdom.MarketCoin },
				{ ESimulationResource::Coin, TEXT("TreasuryAvailable"), 0 }
			};
			for (const auto& Account : Accounts)
			{
				if (!NewLedger.InitializeAccount(
					Account.Resource,
					KingdomAccount(Kingdom.Kingdom, Account.Stock),
					static_cast<double>(Account.Quantity),
					OutError))
				{
					return false;
				}
			}
			NewKingdomConfigs.Add(Kingdom.Kingdom, Kingdom);
			NewRepairCapacity.Add(Kingdom.Kingdom, Kingdom.RepairCapacity);
			NewHarvestRemaining.Add(Kingdom.Kingdom, Kingdom.HarvestCapacity);
		}

		for (const FV17AuthoritativeCellConfig& Cell : InCells)
		{
			if (Cell.CellID == 0
				|| Cell.Count <= 0
				|| Cell.CashTotal < 0
				|| Cell.RepairCreditTotal < 0
				|| Cell.WoodTotal < 0
				|| !NewKingdomConfigs.Contains(Cell.Key.Kingdom)
				|| NewCells.Contains(Cell.CellID)
				|| NewCellIDsByKey.Contains(Cell.Key))
			{
				OutError = TEXT("Joint cells require a unique ID/key, a positive count, valid resources and an initialized kingdom.");
				return false;
			}
			const int64 RepresentativePower = (Cell.CashTotal + Cell.RepairCreditTotal) / Cell.Count;
			const int64 RepresentativeWood = Cell.WoodTotal / Cell.Count;
			if (PurchasingPowerBand(RepresentativePower) != Cell.Key.PurchasingPowerBand
				|| WoodBand(RepresentativeWood) != Cell.Key.WoodBand)
			{
				OutError = TEXT("A Joint Cell key does not match its aggregate representative resource bands.");
				return false;
			}

			const struct
			{
				ESimulationResource Resource;
				const TCHAR* Stock;
				int64 Quantity;
			} Accounts[] =
			{
				{ ESimulationResource::Coin, TEXT("Cash"), Cell.CashTotal },
				{ ESimulationResource::Coin, TEXT("RepairCredit"), Cell.RepairCreditTotal },
				{ ESimulationResource::Wood, TEXT("Wood"), Cell.WoodTotal }
			};
			for (const auto& Account : Accounts)
			{
				if (!NewLedger.InitializeAccount(
					Account.Resource,
					CellAccount(Cell.CellID, Account.Stock),
					static_cast<double>(Account.Quantity),
					OutError))
				{
					return false;
				}
			}
			NewCells.Add(Cell.CellID, Cell);
			NewCellIDsByKey.Add(Cell.Key, Cell.CellID);
			NewPopulation += Cell.Count;
		}

		for (const FV17AuthoritativeActiveConfig& Active : InActiveResidents)
		{
			if (Active.ResidentID <= 0
				|| Active.SourceCellID == 0
				|| Active.Cash < 0
				|| Active.RepairCredit < 0
				|| Active.Wood < 0
				|| !NewKingdomConfigs.Contains(Active.Key.Kingdom)
				|| NewActiveStates.Contains(Active.ResidentID)
				|| PurchasingPowerBand(Active.Cash + Active.RepairCredit) != Active.Key.PurchasingPowerBand
				|| WoodBand(Active.Wood) != Active.Key.WoodBand)
			{
				OutError = TEXT("Pre-seeded Active residents require unique IDs and exact state matching their Joint Cell bands.");
				return false;
			}

			const struct
			{
				ESimulationResource Resource;
				const TCHAR* Stock;
				int64 Quantity;
			} Accounts[] =
			{
				{ ESimulationResource::Coin, TEXT("Cash"), Active.Cash },
				{ ESimulationResource::Coin, TEXT("RepairCredit"), Active.RepairCredit },
				{ ESimulationResource::Wood, TEXT("Wood"), Active.Wood }
			};
			for (const auto& Account : Accounts)
			{
				if (!NewLedger.InitializeAccount(
					Account.Resource,
					ActiveAccount(Active.ResidentID, Account.Stock),
					static_cast<double>(Account.Quantity),
					OutError))
				{
					return false;
				}
			}
			NewActiveStates.Add(Active.ResidentID, { Active, true });
			++NewPopulation;
		}

		if (NewPopulation <= 0 || NewActiveStates.Num() > ActiveMicroCap)
		{
			OutError = TEXT("The authoritative Macro session requires a positive population and at most 50 Active residents.");
			return false;
		}

		NewLedger.SealInitialState();
		Ledger = MoveTemp(NewLedger);
		Cells = MoveTemp(NewCells);
		CellIDsByKey = MoveTemp(NewCellIDsByKey);
		ActiveStates = MoveTemp(NewActiveStates);
		KingdomConfigs = MoveTemp(NewKingdomConfigs);
		RepairCapacityRemaining = MoveTemp(NewRepairCapacity);
		HarvestRemaining = MoveTemp(NewHarvestRemaining);
		InitialPopulation = NewPopulation;
		Clock = FSimulationClock(StartTime);
		CapacityDay = StartTime.Minutes / MinutesPerDay;
		bInitialized = true;
		OutError.Reset();
		return true;
	}

	FV17AuthoritativeClaimID FV17AuthoritativeMacroSession::BuildClaimID(
		const FV17AuthoritativeCellID SourceCellID,
		const FResidentID ActiveResidentID,
		const EIndividualAction Action,
		const FString& ResourceScope,
		const int32 PerParticipantDemand,
		const FPolicyID CausalPolicyID) const
	{
		uint64 Value = Mix64(static_cast<uint64>(static_cast<uint32>(Seed)));
		Value ^= Mix64(static_cast<uint64>(Clock.Now().Minutes));
		const uint64 StableSource = ActiveResidentID > 0
			? Mix64(static_cast<uint64>(ActiveResidentID) ^ 0xA6715EEDull)
			: Mix64(SourceCellID ^ 0xC011EC7ull);
		Value ^= StableSource;
		Value ^= Mix64(static_cast<uint64>(Action));
		Value ^= Mix64(static_cast<uint64>(FCrc::StrCrc32(*ResourceScope)));
		Value ^= Mix64(static_cast<uint64>(PerParticipantDemand));
		Value ^= Mix64(static_cast<uint64>(CausalPolicyID));
		const uint64 ClaimID = Mix64(Value ^ 0xB3000001ull);
		return ClaimID == 0 ? 1 : ClaimID;
	}

	uint64 FV17AuthoritativeMacroSession::BuildStableOrderKey(
		const FV17AuthoritativeClaimID ClaimID) const
	{
		const uint64 Key = Mix64(ClaimID ^ 0xB3000002ull);
		return Key == 0 ? 1 : Key;
	}

	uint64 FV17AuthoritativeMacroSession::BuildRemainderTieKey(
		const FV17AuthoritativeClaim& Claim) const
	{
		uint64 Value = Mix64(static_cast<uint64>(static_cast<uint32>(Seed)));
		Value ^= Mix64(static_cast<uint64>(Claim.GameTime.Minutes));
		Value ^= Mix64(static_cast<uint64>(FCrc::StrCrc32(*Claim.ResourceScope)));
		Value ^= Mix64(Claim.StableOrderKey);
		return Mix64(Value ^ 0xB3000003ull);
	}

	FString FV17AuthoritativeMacroSession::BuildScope(
		const EKingdom Kingdom,
		const EIndividualAction Action) const
	{
		switch (Action)
		{
		case EIndividualAction::BuyWood: return KingdomAccount(Kingdom, TEXT("MarketWood"));
		case EIndividualAction::ChopWood: return KingdomAccount(Kingdom, TEXT("ForestWood"));
		case EIndividualAction::StartRepair: return KingdomAccount(Kingdom, TEXT("RepairCapacity"));
		case EIndividualAction::Work: return TEXT("BoundaryIncome");
		case EIndividualAction::Routine:
		case EIndividualAction::Wait: return TEXT("None");
		default: return TEXT("Invalid");
		}
	}

	bool FV17AuthoritativeMacroSession::QueueMacroAction(
		const FV17AuthoritativeCellID SourceCellID,
		const EIndividualAction Action,
		const int32 RequestedCount,
		const FPolicyID CausalPolicyID,
		FV17AuthoritativeClaimID& OutClaimID,
		FString& OutError)
	{
		return QueueAction(SourceCellID, 0, Action, RequestedCount, CausalPolicyID, OutClaimID, OutError);
	}

	bool FV17AuthoritativeMacroSession::QueueActiveAction(
		const FResidentID ResidentID,
		const EIndividualAction Action,
		const FPolicyID CausalPolicyID,
		FV17AuthoritativeClaimID& OutClaimID,
		FString& OutError)
	{
		const FActiveState* Active = ActiveStates.Find(ResidentID);
		return QueueAction(
			Active != nullptr ? Active->Definition.SourceCellID : 0,
			ResidentID,
			Action,
			1,
			CausalPolicyID,
			OutClaimID,
			OutError);
	}

	bool FV17AuthoritativeMacroSession::QueueAction(
		const FV17AuthoritativeCellID SourceCellID,
		const FResidentID ActiveResidentID,
		const EIndividualAction Action,
		const int32 RequestedCount,
		const FPolicyID CausalPolicyID,
		FV17AuthoritativeClaimID& OutClaimID,
		FString& OutError)
	{
		OutClaimID = 0;
		if (!bInitialized || bClaimsCommittedAtCurrentTime || !IsSupportedAction(Action) || RequestedCount <= 0)
		{
			OutError = TEXT("A supported positive action Flow must be queued before the current hour is committed.");
			return false;
		}

		FV17AuthoritativeJointKey SourceKey;
		FIndividualActionState FrozenState;
		if (ActiveResidentID > 0)
		{
			const FActiveState* Active = ActiveStates.Find(ActiveResidentID);
			if (Active == nullptr || !Active->bReady || RequestedCount != 1)
			{
				OutError = TEXT("An Active request must reference one ready resident and always use Count=1.");
				return false;
			}
			SourceKey = Active->Definition.Key;
			FrozenState = {
				Active->Definition.Cash,
				Active->Definition.RepairCredit,
				Active->Definition.Wood,
				Active->Definition.Key.HomeState
			};
		}
		else
		{
			const FV17AuthoritativeCellConfig* Cell = Cells.Find(SourceCellID);
			if (Cell == nullptr || RequestedCount > Cell->Count)
			{
				OutError = TEXT("A Macro Flow must fit inside an existing non-empty source Joint Cell.");
				return false;
			}
			SourceKey = Cell->Key;
			FrozenState = {
				static_cast<int32>(GetCellCash(SourceCellID) / Cell->Count),
				static_cast<int32>(GetCellRepairCredit(SourceCellID) / Cell->Count),
				static_cast<int32>(GetCellWood(SourceCellID) / Cell->Count),
				Cell->Key.HomeState
			};
		}

		const FV17AuthoritativeKingdomConfig* Kingdom = KingdomConfigs.Find(SourceKey.Kingdom);
		if (Kingdom == nullptr)
		{
			OutError = TEXT("The action source does not have initialized kingdom resources.");
			return false;
		}

		FIndividualWorldFacts World;
		World.MarketWoodAvailable = TNumericLimits<double>::Max() / 4.0;
		World.ForestWood = TNumericLimits<double>::Max() / 4.0;
		World.HarvestAllowance = TNumericLimits<double>::Max() / 4.0;
		World.WoodPrice = Kingdom->WoodPrice;
		const FIndividualActionEvaluation Evaluation = FIndividualDomain::EvaluateAction(
			Action,
			FrozenState,
			SourceKey.Profession,
			SourceKey.IncomeBand,
			World);
		if (!Evaluation.bApplicable)
		{
			OutError = TEXT("The shared Individual Domain says this homogeneous Flow is not applicable.");
			return false;
		}

		const FString ResourceScope = BuildScope(SourceKey.Kingdom, Action);
		const int32 PerParticipantDemand = Action == EIndividualAction::BuyWood
			|| Action == EIndividualAction::ChopWood ? Evaluation.WoodQuantity
			: Action == EIndividualAction::StartRepair ? 1
			: Action == EIndividualAction::Work ? FIndividualDomain::GetWorkIncome(SourceKey.IncomeBand)
			: 0;
		const FV17AuthoritativeClaimID ClaimID = BuildClaimID(
			SourceCellID,
			ActiveResidentID,
			Action,
			ResourceScope,
			PerParticipantDemand,
			CausalPolicyID);
		if (Claims.Contains(ClaimID))
		{
			OutError = TEXT("The same stable Batch Claim has already been queued.");
			return false;
		}

		FV17AuthoritativeClaim Claim;
		Claim.BatchClaimID = ClaimID;
		Claim.GameTime = Clock.Now();
		Claim.ResourceScope = ResourceScope;
		Claim.Action = Action;
		Claim.SourceCellID = SourceCellID;
		Claim.RequestedCount = RequestedCount;
		Claim.PerParticipantDemand = PerParticipantDemand;
		Claim.CausalPolicyID = CausalPolicyID;
		Claim.StableOrderKey = BuildStableOrderKey(ClaimID);
		Claim.ActiveResidentID = ActiveResidentID;
		Claim.FrozenState = FrozenState;
		Claims.Add(ClaimID, Claim);
		QueuedClaimIDs.Add(ClaimID);
		OutClaimID = ClaimID;
		OutError.Reset();
		return true;
	}

	bool FV17AuthoritativeMacroSession::PreflightClaims(FString& OutError) const
	{
		TMap<FV17AuthoritativeCellID, int32> MacroRequests;
		TSet<FResidentID> ActiveRequests;
		for (const FV17AuthoritativeClaimID ClaimID : QueuedClaimIDs)
		{
			const FV17AuthoritativeClaim* Claim = Claims.Find(ClaimID);
			if (Claim == nullptr
				|| Claim->GameTime.Minutes != Clock.Now().Minutes
				|| Claim->GrantedCount != 0
				|| Claim->RejectedCount != 0
				|| Claim->RequestedCount <= 0)
			{
				OutError = TEXT("A queued Batch Claim is missing, stale or already allocated.");
				return false;
			}

			if (Claim->ActiveResidentID > 0)
			{
				const FActiveState* Active = ActiveStates.Find(Claim->ActiveResidentID);
				if (Claim->RequestedCount != 1
					|| Active == nullptr
					|| !Active->bReady
					|| ActiveRequests.Contains(Claim->ActiveResidentID))
				{
					OutError = TEXT("An Active resident cannot submit more than one Count=1 Claim in the same allocation.");
					return false;
				}
				ActiveRequests.Add(Claim->ActiveResidentID);
			}
			else
			{
				const FV17AuthoritativeCellConfig* Cell = Cells.Find(Claim->SourceCellID);
				if (Cell == nullptr)
				{
					OutError = TEXT("A Macro Claim references a missing Joint Cell.");
					return false;
				}
				MacroRequests.FindOrAdd(Claim->SourceCellID) += Claim->RequestedCount;
			}
			if ((Claim->Action == EIndividualAction::BuyWood
				|| Claim->Action == EIndividualAction::ChopWood
				|| Claim->Action == EIndividualAction::StartRepair)
				&& Claim->PerParticipantDemand <= 0)
			{
				OutError = TEXT("A scarce-resource Claim must have a positive integer unit demand.");
				return false;
			}
		}

		for (const TPair<FV17AuthoritativeCellID, int32>& Pair : MacroRequests)
		{
			if (Pair.Value > Cells.FindChecked(Pair.Key).Count)
			{
				OutError = TEXT("The total same-hour Flows exceed their source Joint Cell population.");
				return false;
			}
		}
		OutError.Reset();
		return true;
	}

	int64 FV17AuthoritativeMacroSession::GetScopeCapacity(const FV17AuthoritativeClaim& Claim) const
	{
		FV17AuthoritativeJointKey Key;
		if (Claim.ActiveResidentID > 0)
		{
			const FActiveState* Active = ActiveStates.Find(Claim.ActiveResidentID);
			if (Active == nullptr) return 0;
			Key = Active->Definition.Key;
		}
		else
		{
			const FV17AuthoritativeCellConfig* Cell = Cells.Find(Claim.SourceCellID);
			if (Cell == nullptr) return 0;
			Key = Cell->Key;
		}

		switch (Claim.Action)
		{
		case EIndividualAction::BuyWood:
			return FMath::FloorToInt64(Ledger.GetBalance(
				ESimulationResource::Wood,
				KingdomAccount(Key.Kingdom, TEXT("MarketWoodAvailable"))));
		case EIndividualAction::ChopWood:
			return FMath::Min<int64>(
				FMath::FloorToInt64(Ledger.GetBalance(
					ESimulationResource::Wood,
					KingdomAccount(Key.Kingdom, TEXT("ForestWood")))),
				HarvestRemaining.FindRef(Key.Kingdom));
		case EIndividualAction::StartRepair:
			return RepairCapacityRemaining.FindRef(Key.Kingdom);
		default:
			return TNumericLimits<int64>::Max();
		}
	}

	bool FV17AuthoritativeMacroSession::AllocateClaims(FString& OutError)
	{
		TMap<FString, TArray<FV17AuthoritativeClaimID>> Groups;
		for (const FV17AuthoritativeClaimID ClaimID : QueuedClaimIDs)
		{
			const FV17AuthoritativeClaim& Claim = Claims.FindChecked(ClaimID);
			Groups.FindOrAdd(Claim.ResourceScope).Add(ClaimID);
		}

		TArray<FString> Scopes;
		Groups.GetKeys(Scopes);
		Scopes.Sort();
		for (const FString& Scope : Scopes)
		{
			TArray<FV17AuthoritativeClaimID>& Group = Groups.FindChecked(Scope);
			Group.Sort([this](const FV17AuthoritativeClaimID LeftID, const FV17AuthoritativeClaimID RightID)
			{
				const FV17AuthoritativeClaim& Left = Claims.FindChecked(LeftID);
				const FV17AuthoritativeClaim& Right = Claims.FindChecked(RightID);
				if (Left.StableOrderKey != Right.StableOrderKey) return Left.StableOrderKey < Right.StableOrderKey;
				return LeftID < RightID;
			});

			const bool bScarce = Scope != TEXT("None") && Scope != TEXT("BoundaryIncome");
			if (!bScarce)
			{
				for (const FV17AuthoritativeClaimID ClaimID : Group)
				{
					FV17AuthoritativeClaim& Claim = Claims.FindChecked(ClaimID);
					Claim.GrantedCount = Claim.RequestedCount;
				}
				continue;
			}

			const int64 Capacity = GetScopeCapacity(Claims.FindChecked(Group[0]));
			int64 TotalRequestedUnits = 0;
			for (const FV17AuthoritativeClaimID ClaimID : Group)
			{
				const FV17AuthoritativeClaim& Claim = Claims.FindChecked(ClaimID);
				TotalRequestedUnits += static_cast<int64>(Claim.RequestedCount) * Claim.PerParticipantDemand;
			}
			if (TotalRequestedUnits <= Capacity)
			{
				for (const FV17AuthoritativeClaimID ClaimID : Group)
				{
					FV17AuthoritativeClaim& Claim = Claims.FindChecked(ClaimID);
					Claim.GrantedCount = Claim.RequestedCount;
				}
				continue;
			}

			struct FRemainderCandidate
			{
				FV17AuthoritativeClaimID ClaimID = 0;
				int64 Remainder = 0;
				uint64 TieKey = 0;
			};
			TArray<FRemainderCandidate> Remainders;
			int64 Used = 0;
			for (const FV17AuthoritativeClaimID ClaimID : Group)
			{
				FV17AuthoritativeClaim& Claim = Claims.FindChecked(ClaimID);
				const int64 Numerator = Capacity * static_cast<int64>(Claim.RequestedCount);
				Claim.GrantedCount = static_cast<int32>(Numerator / TotalRequestedUnits);
				Claim.GrantedCount = FMath::Clamp(Claim.GrantedCount, 0, Claim.RequestedCount);
				Used += static_cast<int64>(Claim.GrantedCount) * Claim.PerParticipantDemand;
				Remainders.Add({ ClaimID, Numerator % TotalRequestedUnits, BuildRemainderTieKey(Claim) });
			}
			Remainders.Sort([](const FRemainderCandidate& Left, const FRemainderCandidate& Right)
			{
				if (Left.Remainder != Right.Remainder) return Left.Remainder > Right.Remainder;
				if (Left.TieKey != Right.TieKey) return Left.TieKey < Right.TieKey;
				return Left.ClaimID < Right.ClaimID;
			});

			int64 Remaining = Capacity - Used;
			for (const FRemainderCandidate& Candidate : Remainders)
			{
				FV17AuthoritativeClaim& Claim = Claims.FindChecked(Candidate.ClaimID);
				if (Claim.GrantedCount < Claim.RequestedCount && Remaining >= Claim.PerParticipantDemand)
				{
					++Claim.GrantedCount;
					Remaining -= Claim.PerParticipantDemand;
				}
			}
			for (const FRemainderCandidate& Candidate : Remainders)
			{
				FV17AuthoritativeClaim& Claim = Claims.FindChecked(Candidate.ClaimID);
				const int32 Additional = FMath::Min(
					Claim.RequestedCount - Claim.GrantedCount,
					static_cast<int32>(Remaining / Claim.PerParticipantDemand));
				Claim.GrantedCount += Additional;
				Remaining -= static_cast<int64>(Additional) * Claim.PerParticipantDemand;
			}

			int64 GrantedUnits = 0;
			for (const FV17AuthoritativeClaimID ClaimID : Group)
			{
				const FV17AuthoritativeClaim& Claim = Claims.FindChecked(ClaimID);
				GrantedUnits += static_cast<int64>(Claim.GrantedCount) * Claim.PerParticipantDemand;
			}
			if (GrantedUnits > Capacity)
			{
				++BatchCapacityOverflowCount;
				OutError = TEXT("The deterministic integer allocator exceeded its resource capacity.");
				return false;
			}
		}

		for (const FV17AuthoritativeClaimID ClaimID : QueuedClaimIDs)
		{
			FV17AuthoritativeClaim& Claim = Claims.FindChecked(ClaimID);
			Claim.RejectedCount = Claim.RequestedCount - Claim.GrantedCount;
		}
		OutError.Reset();
		return true;
	}

	bool FV17AuthoritativeMacroSession::Transfer(
		const FSimulationTime Time,
		const ESimulationResource Resource,
		const FString& Source,
		const FString& Destination,
		const int64 Quantity,
		const bool bBoundary,
		const FString& IdempotencyKey,
		const FEventID EventID,
		const FArriveID ArriveID,
		const FPolicyID PolicyID,
		FString& OutError)
	{
		if (Quantity <= 0)
		{
			OutError.Reset();
			return true;
		}
		FLedgerTransferRequest Request;
		Request.IdempotencyKey = IdempotencyKey;
		Request.GameTime = Time;
		Request.Resource = Resource;
		Request.Source = Source;
		Request.Destination = Destination;
		Request.Quantity = static_cast<double>(Quantity);
		Request.bBoundaryFlow = bBoundary;
		Request.EventID = EventID;
		Request.ArriveID = ArriveID;
		Request.PolicyID = PolicyID;
		FTransactionID TransactionID = 0;
		return Ledger.SubmitTransfer(Request, TransactionID, OutError);
	}

	bool FV17AuthoritativeMacroSession::TransferParticipantStateToEvent(
		const FV17AuthoritativeClaim& Claim,
		const FEventID EventID,
		const FArriveID ArriveID,
		const int32 ParticipantCount,
		FString& OutError)
	{
		const bool bActive = Claim.ActiveResidentID > 0;
		const FString CashSource = bActive
			? ActiveAccount(Claim.ActiveResidentID, TEXT("Cash"))
			: CellAccount(Claim.SourceCellID, TEXT("Cash"));
		const FString CreditSource = bActive
			? ActiveAccount(Claim.ActiveResidentID, TEXT("RepairCredit"))
			: CellAccount(Claim.SourceCellID, TEXT("RepairCredit"));
		const FString WoodSource = bActive
			? ActiveAccount(Claim.ActiveResidentID, TEXT("Wood"))
			: CellAccount(Claim.SourceCellID, TEXT("Wood"));
		return Transfer(
			Clock.Now(), ESimulationResource::Coin, CashSource, EventAccount(EventID, TEXT("Cash")),
			static_cast<int64>(Claim.FrozenState.Cash) * ParticipantCount, false,
			FString::Printf(TEXT("V17-CLAIM-%llu-EVENT-%lld-CASH"), Claim.BatchClaimID, EventID),
			EventID, ArriveID, Claim.CausalPolicyID, OutError)
			&& Transfer(
				Clock.Now(), ESimulationResource::Coin, CreditSource, EventAccount(EventID, TEXT("RepairCredit")),
				static_cast<int64>(Claim.FrozenState.RepairCredit) * ParticipantCount, false,
				FString::Printf(TEXT("V17-CLAIM-%llu-EVENT-%lld-CREDIT"), Claim.BatchClaimID, EventID),
				EventID, ArriveID, Claim.CausalPolicyID, OutError)
			&& Transfer(
				Clock.Now(), ESimulationResource::Wood, WoodSource, EventAccount(EventID, TEXT("Wood")),
				static_cast<int64>(Claim.FrozenState.Wood) * ParticipantCount, false,
				FString::Printf(TEXT("V17-CLAIM-%llu-EVENT-%lld-WOOD"), Claim.BatchClaimID, EventID),
				EventID, ArriveID, Claim.CausalPolicyID, OutError);
	}

	bool FV17AuthoritativeMacroSession::CommitEvent(
		const FV17AuthoritativeClaim& Claim,
		const EIndividualAction EventAction,
		const int32 ParticipantCount,
		const bool bRejectedWait,
		FString& OutError)
	{
		if (ParticipantCount <= 0) return true;
		const EIndividualAction StoredAction = EventAction == EIndividualAction::StartRepair
			? EIndividualAction::ContinueRepair
			: EventAction;
		const int64 Duration = FIndividualDomain::GetActionDuration(StoredAction);
		if (Duration <= 0)
		{
			OutError = TEXT("A committed Batch Event requires a positive shared Domain duration.");
			return false;
		}

		FSimulationEventRequest Request;
		Request.Type = bRejectedWait ? TEXT("BatchRejectedWait") : EventType(StoredAction);
		Request.Owner = Claim.ActiveResidentID > 0
			? FString::Printf(TEXT("Active:%lld"), Claim.ActiveResidentID)
			: FString::Printf(TEXT("BatchCell:%llu"), Claim.SourceCellID);
		Request.ResidentID = Claim.ActiveResidentID;
		Request.ActionCode = static_cast<int32>(StoredAction);
		Request.WoodQuantity = EventAction == EIndividualAction::BuyWood
			|| EventAction == EIndividualAction::ChopWood ? Claim.PerParticipantDemand * ParticipantCount
			: EventAction == EIndividualAction::StartRepair
				? static_cast<int32>(RepairWoodPerHome) * ParticipantCount
				: 0;
		Request.StartTime = Clock.Now();
		Request.EndTime = FSimulationTime::FromMinutes(Clock.Now().Minutes + Duration);
		Request.ArriveID = Scheduler.IssueArriveID();
		Request.ParticipantCount = ParticipantCount;
		Request.Cause = FString::Printf(TEXT("BatchClaim:%llu"), Claim.BatchClaimID);
		Request.PolicyID = Claim.CausalPolicyID;
		FEventID EventID = 0;
		if (!EventStore.CreateEvent(Request, EventID, OutError)
			|| !TransferParticipantStateToEvent(Claim, EventID, Request.ArriveID, ParticipantCount, OutError))
		{
			return false;
		}

		if (Claim.ActiveResidentID > 0)
		{
			ActiveStates.FindChecked(Claim.ActiveResidentID).bReady = false;
		}
		else
		{
			Cells.FindChecked(Claim.SourceCellID).Count -= ParticipantCount;
		}

		FReservationID ReservationID = 0;
		if (!bRejectedWait && (EventAction == EIndividualAction::BuyWood || EventAction == EIndividualAction::ChopWood))
		{
			FV17AuthoritativeJointKey SourceKey = Claim.ActiveResidentID > 0
				? ActiveStates.FindChecked(Claim.ActiveResidentID).Definition.Key
				: Cells.FindChecked(Claim.SourceCellID).Key;
			FReservationRequest Reservation;
			Reservation.IdempotencyKey = FString::Printf(TEXT("V17-CLAIM-%llu-RESERVE"), Claim.BatchClaimID);
			Reservation.GameTime = Clock.Now();
			Reservation.Resource = ESimulationResource::Wood;
			Reservation.SourceAccount = KingdomAccount(
				SourceKey.Kingdom,
				EventAction == EIndividualAction::BuyWood ? TEXT("MarketWoodAvailable") : TEXT("ForestWood"));
			Reservation.ReservedAccount = KingdomAccount(
				SourceKey.Kingdom,
				EventAction == EIndividualAction::BuyWood ? TEXT("MarketWoodReserved") : TEXT("ForestWoodReserved"));
			Reservation.Quantity = static_cast<double>(Claim.PerParticipantDemand) * ParticipantCount;
			Reservation.EventID = EventID;
			Reservation.ArriveID = Request.ArriveID;
			Reservation.PolicyID = Claim.CausalPolicyID;
			if (!Reservations.CreateReservation(Reservation, Ledger, ReservationID, OutError)
				|| !EventStore.SetReservationID(EventID, ReservationID, OutError))
			{
				return false;
			}
			if (EventAction == EIndividualAction::ChopWood)
			{
				const int64 HarvestQuantity = static_cast<int64>(Claim.PerParticipantDemand) * ParticipantCount;
				if (HarvestRemaining.FindRef(SourceKey.Kingdom) < HarvestQuantity)
				{
					OutError = TEXT("A ChopWood Batch would exceed the frozen daily harvest allowance.");
					return false;
				}
				HarvestRemaining.FindChecked(SourceKey.Kingdom) -= HarvestQuantity;
			}
		}

		if (!bRejectedWait && EventAction == EIndividualAction::BuyWood)
		{
			const FV17AuthoritativeJointKey SourceKey = Claim.ActiveResidentID > 0
				? ActiveStates.FindChecked(Claim.ActiveResidentID).Definition.Key
				: Cells.FindChecked(Claim.SourceCellID).Key;
			const int64 CostPerParticipant = PaymentCoins(
				Claim.PerParticipantDemand,
				KingdomConfigs.FindChecked(SourceKey.Kingdom).WoodPrice);
			const int64 CreditPerParticipant = FMath::Min<int64>(Claim.FrozenState.RepairCredit, CostPerParticipant);
			const int64 CashPerParticipant = CostPerParticipant - CreditPerParticipant;
			if (!Transfer(
				Clock.Now(), ESimulationResource::Coin,
				EventAccount(EventID, TEXT("RepairCredit")), KingdomAccount(SourceKey.Kingdom, TEXT("MarketCoin")),
				CreditPerParticipant * ParticipantCount, false,
				FString::Printf(TEXT("V17-CLAIM-%llu-BUY-CREDIT"), Claim.BatchClaimID),
				EventID, Request.ArriveID, Claim.CausalPolicyID, OutError)
				|| !Transfer(
					Clock.Now(), ESimulationResource::Coin,
					EventAccount(EventID, TEXT("Cash")), KingdomAccount(SourceKey.Kingdom, TEXT("MarketCoin")),
					CashPerParticipant * ParticipantCount, false,
					FString::Printf(TEXT("V17-CLAIM-%llu-BUY-CASH"), Claim.BatchClaimID),
					EventID, Request.ArriveID, Claim.CausalPolicyID, OutError))
			{
				return false;
			}
		}

		if (!bRejectedWait && EventAction == EIndividualAction::StartRepair)
		{
			const FV17AuthoritativeJointKey SourceKey = Claim.ActiveResidentID > 0
				? ActiveStates.FindChecked(Claim.ActiveResidentID).Definition.Key
				: Cells.FindChecked(Claim.SourceCellID).Key;
			const int64 WoodQuantity = static_cast<int64>(RepairWoodPerHome) * ParticipantCount;
			if (RepairCapacityRemaining.FindRef(SourceKey.Kingdom) < ParticipantCount
				|| !Transfer(
					Clock.Now(), ESimulationResource::Wood,
					EventAccount(EventID, TEXT("Wood")), KingdomAccount(SourceKey.Kingdom, TEXT("WoodEmbeddedInRepairs")),
					WoodQuantity, false,
					FString::Printf(TEXT("V17-CLAIM-%llu-REPAIR-START"), Claim.BatchClaimID),
					EventID, Request.ArriveID, Claim.CausalPolicyID, OutError))
			{
				return false;
			}
			RepairCapacityRemaining.FindChecked(SourceKey.Kingdom) -= ParticipantCount;
		}

		if (!Scheduler.Schedule({ EventID, Request.ArriveID, Request.EndTime }, Clock.Now(), OutError))
		{
			return false;
		}

		FV17AuthoritativeBatchEvent BatchEvent;
		BatchEvent.BatchEventID = EventID;
		BatchEvent.BatchClaimID = Claim.BatchClaimID;
		BatchEvent.SourceCellID = Claim.SourceCellID;
		BatchEvent.Action = StoredAction;
		BatchEvent.ParticipantCount = ParticipantCount;
		BatchEvent.StartTime = Request.StartTime;
		BatchEvent.EndTime = Request.EndTime;
		BatchEvent.RemainingWorkMinutes = Duration;
		BatchEvent.BatchReservationID = ReservationID;
		BatchEvent.CausalPolicyID = Claim.CausalPolicyID;
		BatchEvent.InheritedOrderKey = Claim.StableOrderKey;
		BatchEvent.ActiveResidentID = Claim.ActiveResidentID;
		BatchEvent.FrozenState = Claim.FrozenState;
		BatchEvents.Add(EventID, BatchEvent);
		if (bDynamicLODEnabled && Claim.ActiveResidentID > 0)
		{
			FActiveState& Active = ActiveStates.FindChecked(Claim.ActiveResidentID);
			const FIndividualActionState CurrentState = ReadPendingEventState(BatchEvent);
			Active.Definition.Cash = CurrentState.Cash;
			Active.Definition.RepairCredit = CurrentState.RepairCredit;
			Active.Definition.Wood = CurrentState.Wood;
			Active.Definition.Key.HomeState = CurrentState.HomeState;
			Active.Definition.Key.Intent = ToMacroIntent(StoredAction);
			Active.Definition.Key.PurchasingPowerBand = PurchasingPowerBand(
				CurrentState.Cash + CurrentState.RepairCredit);
			Active.Definition.Key.WoodBand = WoodBand(CurrentState.Wood);
			Active.bReady = false;
			Active.CurrentAction = StoredAction;
			Active.ActiveEventID = EventID;
			Active.ParentEventID = 0;
			Active.ActiveArriveID = Request.ArriveID;
			Active.ActiveReservationID = ReservationID;
			Active.InheritedOrderKey = Claim.StableOrderKey;
			Active.ActionStartTime = Request.StartTime;
			Active.ActionEndTime = Request.EndTime;
		}
		return true;
	}

	bool FV17AuthoritativeMacroSession::CommitClaim(
		const FV17AuthoritativeClaim& Claim,
		FString& OutError)
	{
		return CommitEvent(Claim, Claim.Action, Claim.GrantedCount, false, OutError)
			&& CommitEvent(Claim, EIndividualAction::Wait, Claim.RejectedCount, true, OutError);
	}

	bool FV17AuthoritativeMacroSession::ResolveAndCommitClaims(
		FString& OutError,
		const EV17AuthoritativeFailurePoint FailurePoint)
	{
		if (!bInitialized || QueuedClaimIDs.IsEmpty() || bClaimsCommittedAtCurrentTime)
		{
			if (bClaimsCommittedAtCurrentTime) ++DuplicateBatchCommitCount;
			OutError = TEXT("The current hour has no uncommitted Batch Claims.");
			return false;
		}

		const FV17AuthoritativeMacroSession Before = *this;
		auto Restore = [this, &Before]() { *this = Before; };
		if (!PreflightClaims(OutError) || !AllocateClaims(OutError))
		{
			Restore();
			return false;
		}

		TArray<FV17AuthoritativeClaimID> CommitOrder = QueuedClaimIDs;
		CommitOrder.Sort([this](const FV17AuthoritativeClaimID LeftID, const FV17AuthoritativeClaimID RightID)
		{
			const FV17AuthoritativeClaim& Left = Claims.FindChecked(LeftID);
			const FV17AuthoritativeClaim& Right = Claims.FindChecked(RightID);
			const int32 ScopeOrder = Left.ResourceScope.Compare(Right.ResourceScope, ESearchCase::CaseSensitive);
			if (ScopeOrder != 0) return ScopeOrder < 0;
			if (Left.StableOrderKey != Right.StableOrderKey) return Left.StableOrderKey < Right.StableOrderKey;
			return LeftID < RightID;
		});

		int32 CommittedClaims = 0;
		for (const FV17AuthoritativeClaimID ClaimID : CommitOrder)
		{
			if (!CommitClaim(Claims.FindChecked(ClaimID), OutError))
			{
				Restore();
				return false;
			}
			++CommittedClaims;
			if (FailurePoint == EV17AuthoritativeFailurePoint::AfterFirstCommittedClaim && CommittedClaims == 1)
			{
				OutError = TEXT("Injected B3 failure after the first complete Batch Claim write.");
				Restore();
				return false;
			}
		}

		QueuedClaimIDs.Reset();
		bClaimsCommittedAtCurrentTime = true;
		OutError.Reset();
		return true;
	}

	FV17AuthoritativeCellID FV17AuthoritativeMacroSession::FindOrCreateTargetCell(
		const FV17AuthoritativeJointKey& SourceKey,
		const FIndividualActionState& FinalState)
	{
		FV17AuthoritativeJointKey TargetKey = SourceKey;
		TargetKey.HomeState = FinalState.HomeState;
		TargetKey.Intent = EMacroIntent::Routine;
		TargetKey.PurchasingPowerBand = PurchasingPowerBand(FinalState.Cash + FinalState.RepairCredit);
		TargetKey.WoodBand = WoodBand(FinalState.Wood);
		TargetKey.bAidEligible = SourceKey.bAidEligible && FinalState.HomeState == EHomeState::DamagedWaiting;
		if (const FV17AuthoritativeCellID* Existing = CellIDsByKey.Find(TargetKey))
		{
			return *Existing;
		}

		uint64 Value = Mix64(static_cast<uint64>(static_cast<uint8>(TargetKey.Kingdom)));
		Value ^= Mix64(static_cast<uint64>(static_cast<uint8>(TargetKey.Profession)));
		Value ^= Mix64(static_cast<uint64>(static_cast<uint8>(TargetKey.IncomeBand)));
		Value ^= Mix64(static_cast<uint64>(static_cast<uint8>(TargetKey.HomeState)));
		Value ^= Mix64(static_cast<uint64>(static_cast<uint8>(TargetKey.Intent)));
		Value ^= Mix64(static_cast<uint64>(TargetKey.PurchasingPowerBand));
		Value ^= Mix64(static_cast<uint64>(TargetKey.WoodBand));
		Value ^= Mix64(TargetKey.bAidEligible ? 1ull : 0ull);
		FV17AuthoritativeCellID CellID = Mix64(Value ^ 0xB3000004ull);
		if (CellID == 0) CellID = 1;
		while (Cells.Contains(CellID)) CellID = Mix64(CellID);
		FV17AuthoritativeCellConfig Cell;
		Cell.CellID = CellID;
		Cell.Key = TargetKey;
		Cells.Add(CellID, Cell);
		CellIDsByKey.Add(TargetKey, CellID);
		return CellID;
	}

	bool FV17AuthoritativeMacroSession::TransferEventStateToDestination(
		const FV17AuthoritativeBatchEvent& Event,
		const FV17AuthoritativeCellID TargetCellID,
		FString& OutError)
	{
		const bool bActive = Event.ActiveResidentID > 0;
		const FString CashDestination = bActive
			? ActiveAccount(Event.ActiveResidentID, TEXT("Cash"))
			: CellAccount(TargetCellID, TEXT("Cash"));
		const FString CreditDestination = bActive
			? ActiveAccount(Event.ActiveResidentID, TEXT("RepairCredit"))
			: CellAccount(TargetCellID, TEXT("RepairCredit"));
		const FString WoodDestination = bActive
			? ActiveAccount(Event.ActiveResidentID, TEXT("Wood"))
			: CellAccount(TargetCellID, TEXT("Wood"));
		const int64 Cash = FMath::RoundToInt64(Ledger.GetBalance(
			ESimulationResource::Coin, EventAccount(Event.BatchEventID, TEXT("Cash"))));
		const int64 Credit = FMath::RoundToInt64(Ledger.GetBalance(
			ESimulationResource::Coin, EventAccount(Event.BatchEventID, TEXT("RepairCredit"))));
		const int64 Wood = FMath::RoundToInt64(Ledger.GetBalance(
			ESimulationResource::Wood, EventAccount(Event.BatchEventID, TEXT("Wood"))));
		const FSimulationEventRecord* Stored = EventStore.Find(Event.BatchEventID);
		const FArriveID ArriveID = Stored != nullptr ? Stored->Event.ArriveID : 0;
		return Transfer(
			Event.EndTime, ESimulationResource::Coin,
			EventAccount(Event.BatchEventID, TEXT("Cash")), CashDestination, Cash, false,
			FString::Printf(TEXT("V17-EVENT-%lld-TARGET-CASH"), Event.BatchEventID),
			Event.BatchEventID, ArriveID, Event.CausalPolicyID, OutError)
			&& Transfer(
				Event.EndTime, ESimulationResource::Coin,
				EventAccount(Event.BatchEventID, TEXT("RepairCredit")), CreditDestination, Credit, false,
				FString::Printf(TEXT("V17-EVENT-%lld-TARGET-CREDIT"), Event.BatchEventID),
				Event.BatchEventID, ArriveID, Event.CausalPolicyID, OutError)
			&& Transfer(
				Event.EndTime, ESimulationResource::Wood,
				EventAccount(Event.BatchEventID, TEXT("Wood")), WoodDestination, Wood, false,
				FString::Printf(TEXT("V17-EVENT-%lld-TARGET-WOOD"), Event.BatchEventID),
				Event.BatchEventID, ArriveID, Event.CausalPolicyID, OutError);
	}

	bool FV17AuthoritativeMacroSession::CompleteEvent(
		FV17AuthoritativeBatchEvent& Event,
		const FScheduledEvent& Due,
		bool& bWroteResource,
		FString& OutError)
	{
		const FSimulationEventRecord* Stored = EventStore.Find(Event.BatchEventID);
		if (Stored == nullptr
			|| Stored->State != ESimulationEventState::Pending
			|| Event.Status != ESimulationEventState::Pending
			|| Stored->Event.ParticipantCount != Event.ParticipantCount)
		{
			OutError = TEXT("A due Batch Event does not match its stored participant count or state.");
			return false;
		}

		const FV17AuthoritativeClaim* Claim = Claims.Find(Event.BatchClaimID);
		if (Claim == nullptr)
		{
			OutError = TEXT("A due Batch Event has lost its source Claim.");
			return false;
		}
		const FV17AuthoritativeJointKey SourceKey = Event.ActiveResidentID > 0
			? ActiveStates.FindChecked(Event.ActiveResidentID).Definition.Key
			: Cells.FindChecked(Event.SourceCellID).Key;
		FIndividualActionState FinalState = Event.FrozenState;

		switch (Event.Action)
		{
		case EIndividualAction::Work:
		{
			const int64 Income = static_cast<int64>(FIndividualDomain::GetWorkIncome(SourceKey.IncomeBand))
				* Event.ParticipantCount;
			if (!Transfer(
				Due.ExecuteAt, ESimulationResource::Coin,
				ExternalBoundaryAccount, EventAccount(Event.BatchEventID, TEXT("Cash")), Income, true,
				FString::Printf(TEXT("V17-EVENT-%lld-WORK-INCOME"), Event.BatchEventID),
				Event.BatchEventID, Due.ArriveID, Event.CausalPolicyID, OutError))
			{
				return false;
			}
			FinalState.Cash += FIndividualDomain::GetWorkIncome(SourceKey.IncomeBand);
			bWroteResource = true;
			break;
		}
		case EIndividualAction::BuyWood:
		case EIndividualAction::ChopWood:
		{
			if (!Reservations.CommitReservation(
				Event.BatchReservationID,
				EventAccount(Event.BatchEventID, TEXT("Wood")),
				FString::Printf(TEXT("V17-EVENT-%lld-WOOD-DELIVERY"), Event.BatchEventID),
				Due.ExecuteAt,
				Ledger,
				OutError))
			{
				return false;
			}
			FinalState.Wood += Claim->PerParticipantDemand;
			if (Event.Action == EIndividualAction::BuyWood)
			{
				const int64 Cost = PaymentCoins(
					Claim->PerParticipantDemand,
					KingdomConfigs.FindChecked(SourceKey.Kingdom).WoodPrice);
				const int32 Credit = FMath::Min<int64>(FinalState.RepairCredit, Cost);
				FinalState.RepairCredit -= Credit;
				FinalState.Cash -= static_cast<int32>(Cost) - Credit;
			}
			bWroteResource = true;
			break;
		}
		case EIndividualAction::ContinueRepair:
			if (!Transfer(
				Due.ExecuteAt, ESimulationResource::Wood,
				KingdomAccount(SourceKey.Kingdom, TEXT("WoodEmbeddedInRepairs")),
				KingdomAccount(SourceKey.Kingdom, TEXT("WoodInRepairedHomes")),
				static_cast<int64>(RepairWoodPerHome) * Event.ParticipantCount, false,
				FString::Printf(TEXT("V17-EVENT-%lld-REPAIR-COMPLETE"), Event.BatchEventID),
				Event.BatchEventID, Due.ArriveID, Event.CausalPolicyID, OutError))
			{
				return false;
			}
			FinalState.Wood -= static_cast<int32>(RepairWoodPerHome);
			FinalState.HomeState = EHomeState::Repaired;
			bWroteResource = true;
			break;
		case EIndividualAction::Routine:
		case EIndividualAction::Wait:
			break;
		default:
			OutError = TEXT("The authoritative Macro session cannot complete this action.");
			return false;
		}

		FV17AuthoritativeCellID TargetCellID = Event.SourceCellID;
		if (Event.ActiveResidentID > 0)
		{
			FActiveState& Active = ActiveStates.FindChecked(Event.ActiveResidentID);
			Active.Definition.Cash = FinalState.Cash;
			Active.Definition.RepairCredit = FinalState.RepairCredit;
			Active.Definition.Wood = FinalState.Wood;
			Active.Definition.Key.HomeState = FinalState.HomeState;
			Active.Definition.Key.Intent = EMacroIntent::Routine;
			Active.Definition.Key.PurchasingPowerBand = PurchasingPowerBand(FinalState.Cash + FinalState.RepairCredit);
			Active.Definition.Key.WoodBand = WoodBand(FinalState.Wood);
			Active.bReady = true;
			if (bDynamicLODEnabled)
			{
				Active.CurrentAction = EIndividualAction::None;
				Active.ActiveEventID = 0;
				Active.ParentEventID = 0;
				Active.ActiveArriveID = 0;
				Active.ActiveReservationID = 0;
				Active.InheritedOrderKey = 0;
				Active.ActionStartTime = Due.ExecuteAt;
				Active.ActionEndTime = Due.ExecuteAt;
				UpdateCapsule(
					Event.ActiveResidentID,
					Due.ExecuteAt,
					FinalState,
					Event.SourceCellID,
					0,
					Event.BatchEventID,
					Event.Action);
				ParticipantRefs.Remove(Event.ActiveResidentID);
			}
		}
		else
		{
			TargetCellID = FindOrCreateTargetCell(SourceKey, FinalState);
			Cells.FindChecked(TargetCellID).Count += Event.ParticipantCount;
			if (bDynamicLODEnabled)
			{
				TArray<FResidentID> CompletedResidents;
				for (const TPair<FResidentID, FV17ParticipantRef>& Pair : ParticipantRefs)
				{
					if (Pair.Value.BatchEventID == Event.BatchEventID)
					{
						CompletedResidents.Add(Pair.Key);
					}
				}
				CompletedResidents.Sort();
				for (const FResidentID ResidentID : CompletedResidents)
				{
					UpdateCapsule(
						ResidentID,
						Due.ExecuteAt,
						FinalState,
						TargetCellID,
						0,
						Event.BatchEventID,
						Event.Action);
					ParticipantRefs.Remove(ResidentID);
				}
			}
		}
		if (!TransferEventStateToDestination(Event, TargetCellID, OutError)
			|| !EventStore.CompleteEvent(Event.BatchEventID, OutError))
		{
			return false;
		}
		Event.TargetCellID = TargetCellID;
		Event.RemainingWorkMinutes = 0;
		Event.Status = ESimulationEventState::Completed;
		return true;
	}

	bool FV17AuthoritativeMacroSession::AdvanceTo(
		const FSimulationTime TargetTime,
		FString& OutError,
		const EV17AuthoritativeFailurePoint FailurePoint)
	{
		if (!bInitialized || TargetTime < Clock.Now())
		{
			OutError = TEXT("The authoritative Macro session cannot move backwards in time.");
			return false;
		}

		const FV17AuthoritativeMacroSession Before = *this;
		auto Restore = [this, &Before]() { *this = Before; };
		TArray<FScheduledEvent> DueEvents;
		Scheduler.PopDueThrough(TargetTime, DueEvents);
		bool bInjectedResourceWriteSeen = false;
		for (const FScheduledEvent& Due : DueEvents)
		{
			FV17AuthoritativeBatchEvent* Event = BatchEvents.Find(Due.EventID);
			bool bWroteResource = false;
			if (Event == nullptr || !CompleteEvent(*Event, Due, bWroteResource, OutError))
			{
				Restore();
				return false;
			}
			if (bWroteResource && !bInjectedResourceWriteSeen)
			{
				bInjectedResourceWriteSeen = true;
				if (FailurePoint == EV17AuthoritativeFailurePoint::AfterFirstCompletionResourceWrite)
				{
					OutError = TEXT("Injected B3 failure after the first completion resource write.");
					Restore();
					return false;
				}
			}
		}

		for (TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : BatchEvents)
		{
			if (Pair.Value.Status == ESimulationEventState::Pending)
			{
				Pair.Value.RemainingWorkMinutes = FMath::Max<int64>(
					0,
					Pair.Value.EndTime.Minutes - TargetTime.Minutes);
			}
		}
		if (!Clock.AdvanceTo(TargetTime, OutError))
		{
			Restore();
			return false;
		}
		if (TargetTime.Minutes > Before.Clock.Now().Minutes)
		{
			bClaimsCommittedAtCurrentTime = false;
		}
		const int64 TargetDay = TargetTime.Minutes / MinutesPerDay;
		if (TargetDay != CapacityDay)
		{
			for (const TPair<EKingdom, FV17AuthoritativeKingdomConfig>& Pair : KingdomConfigs)
			{
				RepairCapacityRemaining.FindOrAdd(Pair.Key) = Pair.Value.RepairCapacity;
				HarvestRemaining.FindOrAdd(Pair.Key) = Pair.Value.HarvestCapacity;
			}
			CapacityDay = TargetDay;
		}
		OutError.Reset();
		return true;
	}

	int32 FV17AuthoritativeMacroSession::GetReadyCount(const FV17AuthoritativeCellID CellID) const
	{
		const FV17AuthoritativeCellConfig* Cell = Cells.Find(CellID);
		return Cell != nullptr ? Cell->Count : 0;
	}

	int32 FV17AuthoritativeMacroSession::GetPendingParticipantCount() const
	{
		int32 Count = 0;
		for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : BatchEvents)
		{
			Count += Pair.Value.Status == ESimulationEventState::Pending ? Pair.Value.ParticipantCount : 0;
		}
		return Count;
	}

	int32 FV17AuthoritativeMacroSession::GetActionParticipantCount(const EIndividualAction Action) const
	{
		int32 Count = 0;
		for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : BatchEvents)
		{
			Count += Pair.Value.Action == Action ? Pair.Value.ParticipantCount : 0;
		}
		return Count;
	}

	int32 FV17AuthoritativeMacroSession::GetClaimGrantedCount(const FV17AuthoritativeClaimID ClaimID) const
	{
		const FV17AuthoritativeClaim* Claim = Claims.Find(ClaimID);
		return Claim != nullptr ? Claim->GrantedCount : 0;
	}

	int32 FV17AuthoritativeMacroSession::GetClaimRejectedCount(const FV17AuthoritativeClaimID ClaimID) const
	{
		const FV17AuthoritativeClaim* Claim = Claims.Find(ClaimID);
		return Claim != nullptr ? Claim->RejectedCount : 0;
	}

	int32 FV17AuthoritativeMacroSession::GetRepairCapacityRemaining(const EKingdom Kingdom) const
	{
		return RepairCapacityRemaining.FindRef(Kingdom);
	}

	int64 FV17AuthoritativeMacroSession::GetHarvestRemaining(const EKingdom Kingdom) const
	{
		return HarvestRemaining.FindRef(Kingdom);
	}

	bool FV17AuthoritativeMacroSession::IsActiveReady(const FResidentID ResidentID) const
	{
		const FActiveState* Active = ActiveStates.Find(ResidentID);
		return Active != nullptr && Active->bReady;
	}

	int64 FV17AuthoritativeMacroSession::GetCellCash(const FV17AuthoritativeCellID CellID) const
	{
		return FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, CellAccount(CellID, TEXT("Cash"))));
	}

	int64 FV17AuthoritativeMacroSession::GetCellRepairCredit(const FV17AuthoritativeCellID CellID) const
	{
		return FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, CellAccount(CellID, TEXT("RepairCredit"))));
	}

	int64 FV17AuthoritativeMacroSession::GetCellWood(const FV17AuthoritativeCellID CellID) const
	{
		return FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Wood, CellAccount(CellID, TEXT("Wood"))));
	}

	int64 FV17AuthoritativeMacroSession::GetKingdomBalance(
		const EKingdom Kingdom,
		const ESimulationResource Resource,
		const TCHAR* Stock) const
	{
		return FMath::RoundToInt64(Ledger.GetBalance(Resource, KingdomAccount(Kingdom, Stock)));
	}

	FV17AuthoritativeAudit FV17AuthoritativeMacroSession::BuildAudit() const
	{
		FV17AuthoritativeAudit Audit;
		int64 AccountedPopulation = ActiveStates.Num();
		for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Cells)
		{
			AccountedPopulation += Pair.Value.Count;
			Audit.NegativeJointCellCount += Pair.Value.Count < 0 ? 1 : 0;
		}
		for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : BatchEvents)
		{
			if (Pair.Value.Status == ESimulationEventState::Pending && Pair.Value.ActiveResidentID == 0)
			{
				AccountedPopulation += Pair.Value.ParticipantCount;
			}
		}
		Audit.PopulationResidual = static_cast<int64>(InitialPopulation) - AccountedPopulation;
		Audit.CoinResidual = Ledger.ComputeResidual(ESimulationResource::Coin);
		Audit.WoodResidual = Ledger.ComputeResidual(ESimulationResource::Wood);
		if (FMath::IsNearlyZero(Audit.CoinResidual, 1.e-6)) Audit.CoinResidual = 0.0;
		if (FMath::IsNearlyZero(Audit.WoodResidual, 1.e-6)) Audit.WoodResidual = 0.0;
		Audit.NegativeStockCount = Ledger.CountNegativeStocks();
		for (const TPair<FV17AuthoritativeClaimID, FV17AuthoritativeClaim>& Pair : Claims)
		{
			if (!QueuedClaimIDs.Contains(Pair.Key))
			{
				Audit.BatchRequestedGrantResidualCount += Pair.Value.RequestedCount
					== Pair.Value.GrantedCount + Pair.Value.RejectedCount ? 0 : 1;
			}
		}

		int32 PendingBatchEventCount = 0;
		TSet<FEventID> ScheduledEventIDs;
		for (const FScheduledEvent& Pending : Scheduler.GetPendingEvents())
		{
			ScheduledEventIDs.Add(Pending.EventID);
		}
		for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : BatchEvents)
		{
			const FV17AuthoritativeBatchEvent& Event = Pair.Value;
			const FSimulationEventRecord* Stored = EventStore.Find(Event.BatchEventID);
			if (Event.Status == ESimulationEventState::Pending) ++PendingBatchEventCount;
			if (Stored == nullptr
				|| Stored->State != Event.Status
				|| Stored->Event.ParticipantCount != Event.ParticipantCount
				|| Stored->Event.ActionCode != static_cast<int32>(Event.Action)
				|| Stored->Event.ReservationID != Event.BatchReservationID
				|| ScheduledEventIDs.Contains(Event.BatchEventID) != (Event.Status == ESimulationEventState::Pending))
			{
				++Audit.PendingEventResidualCount;
			}
			if (Event.BatchReservationID > 0)
			{
				const FReservationRecord* Reservation = Reservations.Find(Event.BatchReservationID);
				const EReservationState Expected = Event.Status == ESimulationEventState::Pending
					? EReservationState::Active
					: EReservationState::Committed;
				Audit.ReservationResidualCount += Reservation != nullptr && Reservation->State == Expected ? 0 : 1;
			}
		}
		Audit.PendingEventResidualCount += PendingBatchEventCount == Scheduler.NumPending() ? 0 : 1;
		Audit.BatchCapacityOverflowCount = BatchCapacityOverflowCount;
		for (const TPair<EKingdom, int64>& Pair : HarvestRemaining)
		{
			Audit.BatchCapacityOverflowCount += Pair.Value < 0 ? 1 : 0;
		}
		for (const TPair<EKingdom, int32>& Pair : RepairCapacityRemaining)
		{
			Audit.BatchCapacityOverflowCount += Pair.Value < 0 ? 1 : 0;
		}
		Audit.DuplicateBatchCommitCount = DuplicateBatchCommitCount;
		Audit.CommitResidueCount = CommitResidueCount;
		Audit.OwnerConflictCount = EventStore.GetOwnerConflictCount();
		Audit.ActiveCapViolationCount = ActiveStates.Num() > ActiveMicroCap ? 1 : 0;
		Audit.DuplicateTransactionCount = Ledger.GetDuplicateTransactionCount();
		Audit.DuplicateCompletionCount = EventStore.GetDuplicateCompletionCount();
		if (bDynamicLODEnabled)
		{
			auto BuildOuterKey = [](const EKingdom Kingdom, const EProfession Profession, const EIncomeBand IncomeBand)
			{
				return FString::Printf(
					TEXT("%d,%d,%d"),
					static_cast<int32>(Kingdom),
					static_cast<int32>(Profession),
					static_cast<int32>(IncomeBand));
			};
			TMap<FString, int32> IdentityOuterCounts;
			TMap<FString, int32> StateOuterCounts;
			TSet<FString> LiveActiveAccounts;
			Audit.IdentityMismatchCount += IdentityRegistry.Num() == InitialPopulation ? 0 : 1;
			for (const TPair<FResidentID, FV17IdentityRecord>& Pair : IdentityRegistry)
			{
				const FV17IdentityRecord& Identity = Pair.Value;
				Audit.IdentityMismatchCount += Pair.Key == Identity.ResidentID ? 0 : 1;
				IdentityOuterCounts.FindOrAdd(BuildOuterKey(
					Identity.InitialKingdom, Identity.Profession, Identity.IncomeBand)) += 1;
			}
			for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Cells)
			{
				const FV17AuthoritativeJointKey& Key = Pair.Value.Key;
				StateOuterCounts.FindOrAdd(BuildOuterKey(Key.Kingdom, Key.Profession, Key.IncomeBand)) += Pair.Value.Count;
			}

			TMap<FEventID, int32> RefCounts;
			for (const TPair<FResidentID, FV17ParticipantRef>& Pair : ParticipantRefs)
			{
				const FV17ParticipantRef& Ref = Pair.Value;
				const FV17AuthoritativeBatchEvent* Event = BatchEvents.Find(Ref.BatchEventID);
				const FV17ContinuityCapsule* Capsule = Capsules.Find(Pair.Key);
				if (Pair.Key != Ref.ResidentID
					|| !IdentityRegistry.Contains(Pair.Key)
					|| Capsule == nullptr
					|| Event == nullptr
					|| Event->Status != ESimulationEventState::Pending)
				{
					++Audit.BatchSplitMergeResidualCount;
					continue;
				}
				++RefCounts.FindOrAdd(Ref.BatchEventID);
				const FActiveState* Active = ActiveStates.Find(Pair.Key);
				if ((Active != nullptr && (Active->ActiveEventID != Ref.BatchEventID
						|| Event->ActiveResidentID != Pair.Key))
					|| (Active == nullptr && Event->ActiveResidentID != 0)
					|| Capsule->BatchCursor != Ref.BatchEventID)
				{
					++Audit.BatchSplitMergeResidualCount;
				}
			}

			for (const TPair<FResidentID, FActiveState>& Pair : ActiveStates)
			{
				const FV17IdentityRecord* Identity = IdentityRegistry.Find(Pair.Key);
				const FActiveState& Active = Pair.Value;
				if (Identity == nullptr
					|| Pair.Key != Active.Definition.ResidentID
					|| Active.Definition.Key.Kingdom != Identity->InitialKingdom
					|| Active.Definition.Key.Profession != Identity->Profession
					|| Active.Definition.Key.IncomeBand != Identity->IncomeBand)
				{
					++Audit.IdentityMismatchCount;
				}
				StateOuterCounts.FindOrAdd(BuildOuterKey(
					Active.Definition.Key.Kingdom,
					Active.Definition.Key.Profession,
					Active.Definition.Key.IncomeBand)) += 1;
				LiveActiveAccounts.Add(ActiveAccount(Pair.Key, TEXT("Cash")));
				LiveActiveAccounts.Add(ActiveAccount(Pair.Key, TEXT("RepairCredit")));
				LiveActiveAccounts.Add(ActiveAccount(Pair.Key, TEXT("Wood")));

				const FIndividualActionState DefinitionState = {
					Active.Definition.Cash,
					Active.Definition.RepairCredit,
					Active.Definition.Wood,
					Active.Definition.Key.HomeState
				};
				if (Active.ActiveEventID == 0)
				{
					const bool bAccountMatches =
						FMath::RoundToInt64(Ledger.GetBalance(
							ESimulationResource::Coin, ActiveAccount(Pair.Key, TEXT("Cash")))) == DefinitionState.Cash
						&& FMath::RoundToInt64(Ledger.GetBalance(
							ESimulationResource::Coin, ActiveAccount(Pair.Key, TEXT("RepairCredit")))) == DefinitionState.RepairCredit
						&& FMath::RoundToInt64(Ledger.GetBalance(
							ESimulationResource::Wood, ActiveAccount(Pair.Key, TEXT("Wood")))) == DefinitionState.Wood;
					Audit.LiftRestrictResidueCount += Active.bReady
						&& Active.CurrentAction == EIndividualAction::None
						&& bAccountMatches ? 0 : 1;
				}
				else
				{
					const FV17AuthoritativeBatchEvent* Event = BatchEvents.Find(Active.ActiveEventID);
					const bool bEventMatches = Event != nullptr
						&& Event->Status == ESimulationEventState::Pending
						&& Event->ParticipantCount == 1
						&& Event->ActiveResidentID == Pair.Key
						&& Event->Action == Active.CurrentAction
						&& SameFrozenState(ReadPendingEventState(*Event), DefinitionState);
					Audit.LiftRestrictResidueCount += !Active.bReady && bEventMatches ? 0 : 1;
				}
			}

			for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : BatchEvents)
			{
				const FV17AuthoritativeBatchEvent& Event = Pair.Value;
				if (Event.Status != ESimulationEventState::Pending) continue;
				const FSimulationEventRecord* Stored = EventStore.Find(Event.BatchEventID);
				const int32 RefCount = RefCounts.FindRef(Event.BatchEventID);
				if (Event.ActiveResidentID == 0)
				{
					const FV17AuthoritativeCellConfig* Source = Cells.Find(Event.SourceCellID);
					if (Source != nullptr)
					{
						const FV17AuthoritativeJointKey& Key = Source->Key;
						StateOuterCounts.FindOrAdd(BuildOuterKey(
							Key.Kingdom, Key.Profession, Key.IncomeBand)) += Event.ParticipantCount;
					}
					if (Source == nullptr
						|| RefCount > Event.ParticipantCount
						|| Stored == nullptr
						|| Stored->Event.ResidentID != 0)
					{
						++Audit.BatchSplitMergeResidualCount;
					}
				}
				else if (Event.ParticipantCount != 1
					|| RefCount > 1
					|| Stored == nullptr
					|| Stored->Event.ResidentID != Event.ActiveResidentID)
				{
					++Audit.BatchSplitMergeResidualCount;
				}
			}

			for (const TPair<FResidentID, FV17ContinuityCapsule>& Pair : Capsules)
			{
				const FV17ContinuityCapsule& Capsule = Pair.Value;
				Audit.CapsuleIdentityMismatchCount += Pair.Key == Capsule.ResidentID
					&& IdentityRegistry.Contains(Pair.Key)
					&& Capsule.CapsuleVersion > 0
					&& Capsule.LastObservedTime <= Clock.Now() ? 0 : 1;
			}
			for (const TPair<FResourceAccountKey, double>& Pair : Ledger.GetBalances())
			{
				if (Pair.Key.Account.StartsWith(TEXT("V17.Active."))
					&& !LiveActiveAccounts.Contains(Pair.Key.Account))
				{
					++Audit.LiftRestrictResidueCount;
				}
			}
			Audit.IdentityMismatchCount += IdentityOuterCounts.OrderIndependentCompareEqual(StateOuterCounts) ? 0 : 1;
			Audit.BatchSplitMergeResidualCount += BatchSplitMergeResidualCount;
			Audit.LiftRestrictResidueCount += LiftRestrictResidueCount;
			Audit.TaskResetCount = TaskResetCount;
		}
		return Audit;
	}

	FString FV17AuthoritativeMacroSession::BuildDeterministicDigest() const
	{
		FString Canonical = FString::Printf(
			TEXT("Spec=1.7|Schema=1.2|Authority=v1.7_authoritative|Formal=0|Seed=%d|Time=%lld|Population=%d|NextArrive=%lld|Committed=%d|CapacityDay=%lld|"),
			Seed,
			Clock.Now().Minutes,
			InitialPopulation,
			Scheduler.GetNextArriveID(),
			bClaimsCommittedAtCurrentTime ? 1 : 0,
			CapacityDay);

		TArray<FV17AuthoritativeCellID> CellIDs;
		Cells.GetKeys(CellIDs);
		CellIDs.Sort();
		for (const FV17AuthoritativeCellID CellID : CellIDs)
		{
			const FV17AuthoritativeCellConfig& Cell = Cells.FindChecked(CellID);
			Canonical += FString::Printf(
				TEXT("C=%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lld,%lld,%lld|"),
				CellID,
				Cell.Count,
				static_cast<int32>(Cell.Key.Kingdom),
				static_cast<int32>(Cell.Key.Profession),
				static_cast<int32>(Cell.Key.IncomeBand),
				static_cast<int32>(Cell.Key.HomeState),
				static_cast<int32>(Cell.Key.Intent),
				Cell.Key.PurchasingPowerBand,
				Cell.Key.WoodBand,
				Cell.Key.bAidEligible ? 1 : 0,
				GetCellCash(CellID),
				GetCellRepairCredit(CellID),
				GetCellWood(CellID));
		}

		TArray<FResidentID> ResidentIDs;
		ActiveStates.GetKeys(ResidentIDs);
		ResidentIDs.Sort();
		for (const FResidentID ResidentID : ResidentIDs)
		{
			const FActiveState& Active = ActiveStates.FindChecked(ResidentID);
			Canonical += FString::Printf(
				TEXT("A=%lld,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lld,%lld,%lld|"),
				ResidentID,
				Active.Definition.SourceCellID,
				Active.bReady ? 1 : 0,
				static_cast<int32>(Active.Definition.Key.Kingdom),
				static_cast<int32>(Active.Definition.Key.Profession),
				static_cast<int32>(Active.Definition.Key.IncomeBand),
				static_cast<int32>(Active.Definition.Key.HomeState),
				static_cast<int32>(Active.Definition.Key.Intent),
				Active.Definition.Key.PurchasingPowerBand,
				Active.Definition.Key.WoodBand,
				Active.Definition.Key.bAidEligible ? 1 : 0,
				FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, ActiveAccount(ResidentID, TEXT("Cash")))),
				FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, ActiveAccount(ResidentID, TEXT("RepairCredit")))),
				FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Wood, ActiveAccount(ResidentID, TEXT("Wood")))));
		}

		TArray<FV17AuthoritativeClaimID> ClaimIDs;
		Claims.GetKeys(ClaimIDs);
		ClaimIDs.Sort();
		for (const FV17AuthoritativeClaimID ClaimID : ClaimIDs)
		{
			const FV17AuthoritativeClaim& Claim = Claims.FindChecked(ClaimID);
			Canonical += FString::Printf(
				TEXT("Q=%llu,%lld,%s,%d,%llu,%d,%d,%lld,%llu,%d,%d,%lld,%d,%d,%d,%d|"),
				ClaimID,
				Claim.GameTime.Minutes,
				*Claim.ResourceScope,
				static_cast<int32>(Claim.Action),
				Claim.SourceCellID,
				Claim.RequestedCount,
				Claim.PerParticipantDemand,
				Claim.CausalPolicyID,
				Claim.StableOrderKey,
				Claim.GrantedCount,
				Claim.RejectedCount,
				Claim.ActiveResidentID,
				Claim.FrozenState.Cash,
				Claim.FrozenState.RepairCredit,
				Claim.FrozenState.Wood,
				static_cast<int32>(Claim.FrozenState.HomeState));
		}

		TArray<FEventID> EventIDs;
		BatchEvents.GetKeys(EventIDs);
		EventIDs.Sort();
		for (const FEventID EventID : EventIDs)
		{
			const FV17AuthoritativeBatchEvent& Event = BatchEvents.FindChecked(EventID);
			Canonical += FString::Printf(
				TEXT("E=%lld,%lld,%llu,%llu,%llu,%d,%d,%lld,%lld,%lld,%lld,%lld,%llu,%d,%lld,%d,%d,%d,%d|"),
				EventID,
				Event.ParentBatchEventID,
				Event.BatchClaimID,
				Event.SourceCellID,
				Event.TargetCellID,
				static_cast<int32>(Event.Action),
				Event.ParticipantCount,
				Event.StartTime.Minutes,
				Event.EndTime.Minutes,
				Event.RemainingWorkMinutes,
				Event.BatchReservationID,
				Event.CausalPolicyID,
				Event.InheritedOrderKey,
				static_cast<int32>(Event.Status),
				Event.ActiveResidentID,
				Event.FrozenState.Cash,
				Event.FrozenState.RepairCredit,
				Event.FrozenState.Wood,
				static_cast<int32>(Event.FrozenState.HomeState));
		}

		TArray<FEventID> StoredEventIDs;
		EventStore.GetEvents().GetKeys(StoredEventIDs);
		StoredEventIDs.Sort();
		for (const FEventID EventID : StoredEventIDs)
		{
			const FSimulationEventRecord& Stored = EventStore.GetEvents().FindChecked(EventID);
			Canonical += FString::Printf(
				TEXT("S=%lld,%s,%s,%lld,%d,%d,%lld,%lld,%lld,%lld,%d,%s,%lld,%d|"),
				Stored.EventID,
				*Stored.Event.Type,
				*Stored.Event.Owner,
				Stored.Event.ResidentID,
				Stored.Event.ActionCode,
				Stored.Event.WoodQuantity,
				Stored.Event.StartTime.Minutes,
				Stored.Event.EndTime.Minutes,
				Stored.Event.ReservationID,
				Stored.Event.ArriveID,
				Stored.Event.ParticipantCount,
				*Stored.Event.Cause,
				Stored.Event.PolicyID,
				static_cast<int32>(Stored.State));
		}

		TArray<FResourceAccountKey> BalanceKeys;
		Ledger.GetBalances().GetKeys(BalanceKeys);
		BalanceKeys.Sort([](const FResourceAccountKey& Left, const FResourceAccountKey& Right)
		{
			if (Left.Resource != Right.Resource) return static_cast<uint8>(Left.Resource) < static_cast<uint8>(Right.Resource);
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
			const FLedgerTransferRequest& TransferRequest = Transaction.Transfer;
			Canonical += FString::Printf(
				TEXT("T=%lld,%s,%lld,%d,%s,%s,%lld,%d,%lld,%lld,%lld|"),
				Transaction.TransactionID,
				*TransferRequest.IdempotencyKey,
				TransferRequest.GameTime.Minutes,
				static_cast<int32>(TransferRequest.Resource),
				*TransferRequest.Source,
				*TransferRequest.Destination,
				FMath::RoundToInt64(TransferRequest.Quantity),
				TransferRequest.bBoundaryFlow ? 1 : 0,
				TransferRequest.EventID,
				TransferRequest.ArriveID,
				TransferRequest.PolicyID);
		}

		TArray<FReservationID> ReservationIDs;
		Reservations.GetReservations().GetKeys(ReservationIDs);
		ReservationIDs.Sort();
		for (const FReservationID ReservationID : ReservationIDs)
		{
			const FReservationRecord& Reservation = Reservations.GetReservations().FindChecked(ReservationID);
			Canonical += FString::Printf(
				TEXT("R=%lld,%s,%lld,%d,%s,%s,%lld,%lld,%lld,%lld,%d|"),
				ReservationID,
				*Reservation.Request.IdempotencyKey,
				Reservation.Request.GameTime.Minutes,
				static_cast<int32>(Reservation.Request.Resource),
				*Reservation.Request.SourceAccount,
				*Reservation.Request.ReservedAccount,
				FMath::RoundToInt64(Reservation.Request.Quantity),
				Reservation.Request.EventID,
				Reservation.Request.ArriveID,
				Reservation.Request.PolicyID,
				static_cast<int32>(Reservation.State));
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
			Canonical += FString::Printf(TEXT("P=%lld,%lld,%lld|"), Pending.EventID, Pending.ArriveID, Pending.ExecuteAt.Minutes);
		}
		TArray<EKingdom> Kingdoms;
		KingdomConfigs.GetKeys(Kingdoms);
		Kingdoms.Sort([](const EKingdom Left, const EKingdom Right)
		{
			return static_cast<uint8>(Left) < static_cast<uint8>(Right);
		});
		for (const EKingdom Kingdom : Kingdoms)
		{
			const FV17AuthoritativeKingdomConfig& Config = KingdomConfigs.FindChecked(Kingdom);
			Canonical += FString::Printf(
				TEXT("K=%d,%lld,%lld,%lld,%lld,%lld,%lld,%d,%.6f,%d,%lld|"),
				static_cast<int32>(Kingdom),
				Config.MarketWood,
				Config.ForestWood,
				Config.HarvestCapacity,
				Config.MarketCoin,
				Config.EmbeddedRepairWood,
				Config.RepairedHomeWood,
				Config.RepairCapacity,
				Config.WoodPrice,
				RepairCapacityRemaining.FindRef(Kingdom),
				HarvestRemaining.FindRef(Kingdom));
		}
		const FV17AuthoritativeAudit Audit = BuildAudit();
		Canonical += FString::Printf(
			TEXT("X=%lld,%.9g,%.9g,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d|"),
			Audit.PopulationResidual,
			Audit.CoinResidual,
			Audit.WoodResidual,
			Audit.NegativeJointCellCount,
			Audit.NegativeStockCount,
			Audit.BatchRequestedGrantResidualCount,
			Audit.BatchCapacityOverflowCount,
			Audit.DuplicateBatchCommitCount,
			Audit.PendingEventResidualCount,
			Audit.ReservationResidualCount,
			Audit.CommitResidueCount,
			Audit.OwnerConflictCount,
			Audit.ActiveCapViolationCount,
			Audit.DuplicateTransactionCount,
			Audit.DuplicateCompletionCount);
		if (bDynamicLODEnabled)
		{
			Canonical += TEXT("DynamicLOD=1|");
			TArray<FResidentID> IdentityIDs;
			IdentityRegistry.GetKeys(IdentityIDs);
			IdentityIDs.Sort();
			for (const FResidentID ResidentID : IdentityIDs)
			{
				const FV17IdentityRecord& Identity = IdentityRegistry.FindChecked(ResidentID);
				Canonical += FString::Printf(
					TEXT("I=%lld,%lld,%u,%u,%lld,%d,%d,%d,%u|"),
					Identity.ResidentID,
					Identity.PersistentID,
					Identity.NameSeed,
					Identity.AppearanceSeed,
					Identity.HomeID,
					static_cast<int32>(Identity.InitialKingdom),
					static_cast<int32>(Identity.Profession),
					static_cast<int32>(Identity.IncomeBand),
					Identity.IdentityVersion);
			}

			TArray<FResidentID> CapsuleIDs;
			Capsules.GetKeys(CapsuleIDs);
			CapsuleIDs.Sort();
			for (const FResidentID ResidentID : CapsuleIDs)
			{
				const FV17ContinuityCapsule& Capsule = Capsules.FindChecked(ResidentID);
				Canonical += FString::Printf(
					TEXT("U=%lld,%lld,%d,%d,%d,%d,%llu,%lld,%u|"),
					Capsule.ResidentID,
					Capsule.LastObservedTime.Minutes,
					Capsule.LastObservedState.Cash,
					Capsule.LastObservedState.RepairCredit,
					Capsule.LastObservedState.Wood,
					static_cast<int32>(Capsule.LastObservedState.HomeState),
					Capsule.LastKnownCellID,
					Capsule.BatchCursor,
					Capsule.CapsuleVersion);
				TArray<EIndividualAction> Completed = Capsule.KnownCompletedActions;
				Completed.Sort([](const EIndividualAction Left, const EIndividualAction Right)
				{
					return static_cast<uint8>(Left) < static_cast<uint8>(Right);
				});
				for (const EIndividualAction Action : Completed)
				{
					Canonical += FString::Printf(TEXT("UA=%lld,%d|"), ResidentID, static_cast<int32>(Action));
				}
				TArray<FEventID> Lineage = Capsule.CommittedEventLineage;
				Lineage.Sort();
				for (const FEventID EventID : Lineage)
				{
					Canonical += FString::Printf(TEXT("UE=%lld,%lld|"), ResidentID, EventID);
				}
			}

			TArray<FResidentID> RefResidentIDs;
			ParticipantRefs.GetKeys(RefResidentIDs);
			RefResidentIDs.Sort();
			for (const FResidentID ResidentID : RefResidentIDs)
			{
				const FV17ParticipantRef& Ref = ParticipantRefs.FindChecked(ResidentID);
				Canonical += FString::Printf(
					TEXT("F=%llu,%lld,%lld,%lld,%lld,%llu|"),
					Ref.ParticipantRefID,
					Ref.ResidentID,
					Ref.BatchEventID,
					Ref.ParentBatchEventID,
					Ref.ReservationID,
					Ref.InheritedOrderKey);
			}

			for (const FResidentID ResidentID : ResidentIDs)
			{
				const FActiveState& Active = ActiveStates.FindChecked(ResidentID);
				Canonical += FString::Printf(
					TEXT("DA=%lld,%d,%lld,%lld,%lld,%lld,%llu,%lld,%lld|"),
					ResidentID,
					static_cast<int32>(Active.CurrentAction),
					Active.ActiveEventID,
					Active.ParentEventID,
					Active.ActiveArriveID,
					Active.ActiveReservationID,
					Active.InheritedOrderKey,
					Active.ActionStartTime.Minutes,
					Active.ActionEndTime.Minutes);
			}

			for (const FV17LODTransitionRecord& Transition : LODTransitions)
			{
				Canonical += FString::Printf(
					TEXT("D=%lld,%lld,%d,%llu,%lld,%lld,%d,%d|"),
					Transition.ResidentID,
					Transition.GameTime.Minutes,
					Transition.bLift ? 1 : 0,
					Transition.SelectedCellID,
					Transition.BatchEventID,
					Transition.ParentBatchEventID,
					Transition.bUsedFallback ? 1 : 0,
					static_cast<int32>(Transition.Result));
			}
			Canonical += FString::Printf(
				TEXT("DX=%d,%d,%d,%d,%d,%d|"),
				LiftReconstructionFallbackCount,
				Audit.IdentityMismatchCount,
				Audit.CapsuleIdentityMismatchCount,
				Audit.BatchSplitMergeResidualCount,
				Audit.LiftRestrictResidueCount,
				Audit.TaskResetCount);
		}
		const FTCHARToUTF8 Utf8(*Canonical);
		return FSHA1::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length())).ToString();
	}
}
