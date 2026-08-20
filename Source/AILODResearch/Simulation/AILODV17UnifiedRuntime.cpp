// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODV17UnifiedRuntime.h"

#include "AILODDomainRules.h"
#include "AILODPhase0Manifest.h"
#include "HAL/PlatformTime.h"

namespace AILOD
{
	using namespace DomainRules;

	namespace
	{
		int32 KingdomIndex(const EKingdom Kingdom)
		{
			return Kingdom == EKingdom::A ? 0 : 1;
		}

		int32 PowerBand(const int64 PurchasingPower)
		{
			return PurchasingPower <= 3 ? 0 : PurchasingPower <= 7 ? 1 : 2;
		}

		int32 InventoryBand(const int64 Wood)
		{
			return Wood <= 0 ? 0 : Wood < static_cast<int64>(RepairWoodPerHome) ? 1 : 2;
		}

		bool JointKeyLess(const FV17AuthoritativeJointKey& Left, const FV17AuthoritativeJointKey& Right)
		{
			if (Left.Kingdom != Right.Kingdom) return Left.Kingdom < Right.Kingdom;
			if (Left.Profession != Right.Profession) return Left.Profession < Right.Profession;
			if (Left.IncomeBand != Right.IncomeBand) return Left.IncomeBand < Right.IncomeBand;
			if (Left.HomeState != Right.HomeState) return Left.HomeState < Right.HomeState;
			if (Left.Intent != Right.Intent) return Left.Intent < Right.Intent;
			if (Left.PurchasingPowerBand != Right.PurchasingPowerBand)
			{
				return Left.PurchasingPowerBand < Right.PurchasingPowerBand;
			}
			if (Left.WoodBand != Right.WoodBand) return Left.WoodBand < Right.WoodBand;
			return Left.bAidEligible < Right.bAidEligible;
		}

		FString OuterKeyString(const FV17AuthoritativeJointKey& Key)
		{
			return FString::Printf(
				TEXT("K=%d|P=%d|I=%d"),
				static_cast<int32>(Key.Kingdom),
				static_cast<int32>(Key.Profession),
				static_cast<int32>(Key.IncomeBand));
		}

		FString JointKeyString(const FV17AuthoritativeJointKey& Key)
		{
			return FString::Printf(
				TEXT("K=%d|P=%d|I=%d|H=%d|M=%d|Power=%d|Wood=%d|Aid=%d"),
				static_cast<int32>(Key.Kingdom),
				static_cast<int32>(Key.Profession),
				static_cast<int32>(Key.IncomeBand),
				static_cast<int32>(Key.HomeState),
				static_cast<int32>(Key.Intent),
				Key.PurchasingPowerBand,
				Key.WoodBand,
				Key.bAidEligible ? 1 : 0);
		}

		FString EventAccount(const FEventID EventID, const TCHAR* Stock)
		{
			return FString::Printf(TEXT("V17.BatchEvent.%lld.%s"), EventID, Stock);
		}

		FString ActiveAccount(const FResidentID ResidentID, const TCHAR* Stock)
		{
			return FString::Printf(TEXT("V17.Active.%lld.%s"), ResidentID, Stock);
		}
	}

	FV17UnifiedRuntime::FV17UnifiedRuntime(
		const FPhase0Config& InConfig,
		const EStage2Scenario InScenario,
		const FUnifiedRunOptions& InOptions)
		: Config(InConfig)
		, Scenario(InScenario)
		, Options(InOptions)
	{
	}

	bool FV17UnifiedRuntime::Initialize(FString& OutError)
	{
		const double Start = FPlatformTime::Seconds();
		if (Config.PopulationPerKingdom <= 0)
		{
			OutError = TEXT("The v1.7 unified runtime requires a positive per-kingdom population.");
			return false;
		}
		if (Options.bEnableV17ShadowCohort)
		{
			OutError = TEXT("The v1.7 authoritative runtime cannot also enable the v1.7 shadow diagnostic.");
			return false;
		}
		if (!FPhase0ManifestGenerator::Generate(
			Config, PopulationManifest, DamageList, ContinuitySample, OutError)
			|| !BuildDay14ActivationSample(OutError))
		{
			return false;
		}

		TArray<FV17AuthoritativeCellConfig> Cells;
		TArray<FV17IdentityRecord> Identities;
		TArray<FV17AuthoritativeKingdomConfig> Kingdoms;
		if (!BuildAuthorityInput(Cells, Identities, Kingdoms, OutError))
		{
			return false;
		}
		Authority = MakeUnique<FV17AuthoritativeMacroSession>(Config.Seed);
		if (!Authority->InitializeWithIdentity(
			Cells, Identities, Kingdoms, FSimulationTime::FromDays(-7), OutError))
		{
			Authority.Reset();
			return false;
		}
		Authority->EnableExactAggregateResourceSplits();
		ImportBudgetRemaining = Config.PopulationPerKingdom;
		const double AuditStart = FPlatformTime::Seconds();
		++Diagnostics.FullAuditCount;
		if (!Authority->BuildAudit().IsHardErrorFree())
		{
			OutError = TEXT("The initialized v1.7 authority failed its hard-error check.");
			Authority.Reset();
			return false;
		}
		CostBreakdown.AuditCpuMs = (FPlatformTime::Seconds() - AuditStart) * 1000.0;
		CostBreakdown.InitializeCpuMs = FMath::Max(
			0.0,
			(FPlatformTime::Seconds() - Start) * 1000.0 - CostBreakdown.AuditCpuMs);
		OutError.Reset();
		return true;
	}

	bool FV17UnifiedRuntime::BuildDay14ActivationSample(FString& OutError)
	{
		TSet<FResidentID> ContinuityIDs;
		for (const FPersistentTestRecord& Record : ContinuitySample.Residents)
		{
			ContinuityIDs.Add(Record.ResidentID);
		}
		struct FStratum
		{
			EKingdom Kingdom;
			EProfession Profession;
			EIncomeBand IncomeBand;
			int32 Count;
		};
		const FStratum Strata[] =
		{
			{ EKingdom::A, EProfession::Logger, EIncomeBand::Low, 1 },
			{ EKingdom::A, EProfession::Logger, EIncomeBand::NonLow, 1 },
			{ EKingdom::A, EProfession::Worker, EIncomeBand::Low, 6 },
			{ EKingdom::A, EProfession::Worker, EIncomeBand::NonLow, 2 },
			{ EKingdom::B, EProfession::Logger, EIncomeBand::Low, 1 },
			{ EKingdom::B, EProfession::Logger, EIncomeBand::NonLow, 1 },
			{ EKingdom::B, EProfession::Worker, EIncomeBand::Low, 6 },
			{ EKingdom::B, EProfession::Worker, EIncomeBand::NonLow, 2 }
		};
		for (const FStratum& Stratum : Strata)
		{
			TArray<const FInitialResidentRecord*> Candidates;
			for (const FInitialResidentRecord& Resident : PopulationManifest.Residents)
			{
				if (Resident.Kingdom == Stratum.Kingdom
					&& Resident.Profession == Stratum.Profession
					&& Resident.IncomeBand == Stratum.IncomeBand
					&& !ContinuityIDs.Contains(Resident.ResidentID))
				{
					Candidates.Add(&Resident);
				}
			}
			Candidates.Sort([this](const FInitialResidentRecord& Left, const FInitialResidentRecord& Right)
			{
				const uint64 LeftKey = CompetitionOrderKey(
					Config.Seed, FSimulationTime::FromDays(14).Minutes, Left.ResidentID, 0xD14ull);
				const uint64 RightKey = CompetitionOrderKey(
					Config.Seed, FSimulationTime::FromDays(14).Minutes, Right.ResidentID, 0xD14ull);
				return LeftKey != RightKey ? LeftKey < RightKey : Left.ResidentID < Right.ResidentID;
			});
			if (Candidates.Num() < Stratum.Count)
			{
				OutError = TEXT("The manifest cannot supply the frozen Day 14 v1.7 activation sample.");
				return false;
			}
			for (int32 Index = 0; Index < Stratum.Count; ++Index)
			{
				Day14ActivationResidents.Add(Candidates[Index]->ResidentID);
			}
		}
		Day14ActivationResidents.Sort();
		OutError.Reset();
		return Day14ActivationResidents.Num() == 20;
	}

	bool FV17UnifiedRuntime::BuildAuthorityInput(
		TArray<FV17AuthoritativeCellConfig>& OutCells,
		TArray<FV17IdentityRecord>& OutIdentities,
		TArray<FV17AuthoritativeKingdomConfig>& OutKingdoms,
		FString& OutError) const
	{
		TMap<FV17AuthoritativeJointKey, FV17AuthoritativeCellConfig> Grouped;
		OutIdentities.Reserve(PopulationManifest.Residents.Num());
		for (const FInitialResidentRecord& Initial : PopulationManifest.Residents)
		{
			FV17IdentityRecord& Identity = OutIdentities.AddDefaulted_GetRef();
			Identity.ResidentID = Initial.ResidentID;
			Identity.PersistentID = Initial.PersistentID;
			Identity.NameSeed = static_cast<uint32>(Mix64(static_cast<uint64>(Initial.ResidentID) ^ 0x4E414D45ull));
			Identity.AppearanceSeed = static_cast<uint32>(Mix64(static_cast<uint64>(Initial.ResidentID) ^ 0x41505052ull));
			Identity.HomeID = Initial.HomeID;
			Identity.InitialKingdom = Initial.Kingdom;
			Identity.Profession = Initial.Profession;
			Identity.IncomeBand = Initial.IncomeBand;

			FV17AuthoritativeJointKey Key;
			Key.Kingdom = Initial.Kingdom;
			Key.Profession = Initial.Profession;
			Key.IncomeBand = Initial.IncomeBand;
			Key.HomeState = Initial.HomeState;
			Key.Intent = EMacroIntent::Routine;
			Key.PurchasingPowerBand = PowerBand(Initial.Cash + Initial.RepairCredit);
			Key.WoodBand = InventoryBand(Initial.InventoryWood);
			FV17AuthoritativeCellConfig& Cell = Grouped.FindOrAdd(Key);
			Cell.Key = Key;
			++Cell.Count;
			Cell.CashTotal += Initial.Cash;
			Cell.RepairCreditTotal += Initial.RepairCredit;
			Cell.WoodTotal += Initial.InventoryWood;
		}

		Grouped.GenerateValueArray(OutCells);
		OutCells.Sort([](const FV17AuthoritativeCellConfig& Left, const FV17AuthoritativeCellConfig& Right)
		{
			return JointKeyLess(Left.Key, Right.Key);
		});
		for (int32 Index = 0; Index < OutCells.Num(); ++Index)
		{
			OutCells[Index].CellID = static_cast<FV17AuthoritativeCellID>(Index + 1);
		}

		const int64 N = Config.PopulationPerKingdom;
		for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
		{
			FV17AuthoritativeKingdomConfig& State = OutKingdoms.AddDefaulted_GetRef();
			State.Kingdom = Kingdom;
			State.MarketWood = 2 * N;
			State.ForestWood = 16 * N;
			State.HarvestCapacity = TNumericLimits<int64>::Max() / 4;
			State.RepairCapacity = FMath::FloorToInt(RepairStartCapacityPerPersonPerDay * N);
			State.WoodPrice = 1.0;
			State.TreasuryCoin = 5 * N;
		}
		if (OutCells.IsEmpty() || OutIdentities.Num() != 2 * Config.PopulationPerKingdom)
		{
			OutError = TEXT("The v1.7 authority input did not preserve the Phase 0 population.");
			return false;
		}
		OutError.Reset();
		return true;
	}

	bool FV17UnifiedRuntime::ApplyEarthquake(FString& OutError)
	{
		TMap<FString, int32> DamagedByOuterCohort;
		for (const FEarthquakeDamageRecord& Damage : DamageList.DamagedResidents)
		{
			const FV17IdentityRecord* Identity = Authority->FindIdentity(Damage.ResidentID);
			if (Identity == nullptr)
			{
				OutError = TEXT("The earthquake list references an identity that is not in the v1.7 registry.");
				return false;
			}
			FV17AuthoritativeJointKey Outer;
			Outer.Kingdom = Identity->InitialKingdom;
			Outer.Profession = Identity->Profession;
			Outer.IncomeBand = Identity->IncomeBand;
			++DamagedByOuterCohort.FindOrAdd(OuterKeyString(Outer));
		}

		TArray<FString> OuterKeys;
		DamagedByOuterCohort.GetKeys(OuterKeys);
		OuterKeys.Sort();
		for (const FString& OuterKey : OuterKeys)
		{
			int32 Remaining = DamagedByOuterCohort.FindChecked(OuterKey);
			TArray<FV17AuthoritativeCellID> CandidateIDs;
			for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Authority->GetCells())
			{
				if (Pair.Value.Count > 0
					&& Pair.Value.Key.HomeState == EHomeState::Healthy
					&& OuterKeyString(Pair.Value.Key) == OuterKey)
				{
					CandidateIDs.Add(Pair.Key);
				}
			}
			CandidateIDs.Sort();
			for (const FV17AuthoritativeCellID CellID : CandidateIDs)
			{
				if (Remaining <= 0) break;
				const FV17AuthoritativeCellConfig* Cell = Authority->GetCells().Find(CellID);
				if (Cell == nullptr || Cell->Count <= 0) continue;
				const int32 SourceCount = Cell->Count;
				const int32 MovedCount = FMath::Min(Remaining, SourceCount);
				auto Share = [MovedCount, SourceCount](const int64 Total)
				{
					return MovedCount == SourceCount ? Total : Total * MovedCount / SourceCount;
				};
				FV17AuthoritativeJointKey TargetKey = Cell->Key;
				TargetKey.HomeState = EHomeState::DamagedWaiting;
				TargetKey.Intent = EMacroIntent::Wait;
				FV17AuthoritativeCellID TargetCellID = 0;
				if (!Authority->MoveJointCellParticipants(
					CellID,
					TargetKey,
					MovedCount,
					Share(Authority->GetCellCash(CellID)),
					Share(Authority->GetCellRepairCredit(CellID)),
					Share(Authority->GetCellWood(CellID)),
					FString::Printf(TEXT("V17-EARTHQUAKE-C%llu"), CellID),
					0,
					TargetCellID,
					OutError))
				{
					return false;
				}
				FV17AuthoritativeCellID NormalizedCellID = 0;
				if (!Authority->NormalizeReadyCellBands(TargetCellID, NormalizedCellID, OutError)) return false;
				Remaining -= MovedCount;
			}
			if (Remaining != 0)
			{
				OutError = TEXT("The earthquake aggregate could not preserve the exact damaged population in one outer Cohort.");
				return false;
			}
		}
		bEarthquakeApplied = true;
		return Authority->CreateInstantSystemEvent(
			TEXT("EarthquakeDamage"), DamageList.DamagedResidents.Num(), 0, OutError);
	}

	bool FV17UnifiedRuntime::IsHarvestCapActive(
		const EKingdom Kingdom,
		const FSimulationTime Time) const
	{
		return Kingdom == EKingdom::A
			&& Scenario == EStage2Scenario::HarvestCap
			&& Time.Minutes >= FSimulationTime::FromDays(3).Minutes
			&& Time.Minutes < FSimulationTime::FromDays(30).Minutes;
	}

	void FV17UnifiedRuntime::EnsureHarvestDay(
		const EKingdom Kingdom,
		const FSimulationTime Time)
	{
		const int32 Index = KingdomIndex(Kingdom);
		const int32 Day = static_cast<int32>(FMath::FloorToDouble(
			static_cast<double>(Time.Minutes) / MinutesPerDay));
		if (HarvestDays[Index] == Day) return;
		HarvestDays[Index] = Day;
		HarvestAllowances[Index] = IsHarvestCapActive(Kingdom, Time)
			? HarvestCapPerPersonPerDay * Config.PopulationPerKingdom
			: static_cast<double>(TNumericLimits<int64>::Max() / 4);
	}

	FIndividualWorldFacts FV17UnifiedRuntime::BuildWorldFacts(const EKingdom Kingdom) const
	{
		FIndividualWorldFacts World;
		World.MarketWoodAvailable = Authority->GetKingdomBalanceExact(
			Kingdom, ESimulationResource::Wood, TEXT("MarketWoodAvailable"));
		World.ForestWood = Authority->GetKingdomBalanceExact(
			Kingdom, ESimulationResource::Wood, TEXT("ForestWood"));
		World.HarvestAllowance = Authority->GetHarvestRemaining(Kingdom);
		World.WoodPrice = WoodPrices[KingdomIndex(Kingdom)];
		return World;
	}

	bool FV17UnifiedRuntime::ApplyEnvironment(
		const FSimulationTime Time,
		FString& OutError)
	{
		for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
		{
			const int32 Index = KingdomIndex(Kingdom);
			EnsureHarvestDay(Kingdom, Time);
			const double Forest = Authority->GetKingdomBalanceExact(
				Kingdom, ESimulationResource::Wood, TEXT("ForestWood"));
			const double ForestCapacity = 20.0 * Config.PopulationPerKingdom;
			const double Growth = FMath::Max(
				0.0,
				ForestGrowthRatePerDay * Forest * (1.0 - Forest / ForestCapacity) / HoursPerGameDay);
			const double BaselineImport = BaselineImportPerPersonPerDay
				* Config.PopulationPerKingdom / HoursPerGameDay;
			const double DesiredHarvest = BaselineHarvestPerPersonPerDay
				* Config.PopulationPerKingdom / HoursPerGameDay;
			const double Harvest = FMath::Min(
				DesiredHarvest,
				FMath::Min(Forest, HarvestAllowances[Index]));
			const double MarketBeforeConsumption = Authority->GetKingdomBalanceExact(
				Kingdom, ESimulationResource::Wood, TEXT("MarketWoodAvailable"))
				+ BaselineImport + Harvest;
			const double Consumption = FMath::Min(
				RoutineConsumptionPerPersonPerDay * Config.PopulationPerKingdom / HoursPerGameDay,
				MarketBeforeConsumption);

			if (!Authority->ApplySystemTransfer(
					ESimulationResource::Wood,
					ExternalBoundaryAccount,
					MakeKingdomAccount(Kingdom, TEXT("ForestWood")),
					Growth,
					true,
					FString::Printf(TEXT("V17-GROWTH-%d-M%lld"), Index, Time.Minutes),
					0,
					OutError)
				|| !Authority->ApplySystemTransfer(
					ESimulationResource::Wood,
					ExternalBoundaryAccount,
					MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
					BaselineImport,
					true,
					FString::Printf(TEXT("V17-BASELINE-IMPORT-%d-M%lld"), Index, Time.Minutes),
					0,
					OutError)
				|| !Authority->ApplySystemTransfer(
					ESimulationResource::Wood,
					MakeKingdomAccount(Kingdom, TEXT("ForestWood")),
					MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
					Harvest,
					false,
					FString::Printf(TEXT("V17-HARVEST-%d-M%lld"), Index, Time.Minutes),
					IsHarvestCapActive(Kingdom, Time) ? HarvestCapPolicyID : 0,
					OutError)
				|| !Authority->ApplySystemTransfer(
					ESimulationResource::Wood,
					MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
					ExternalBoundaryAccount,
					Consumption,
					true,
					FString::Printf(TEXT("V17-CONSUMPTION-%d-M%lld"), Index, Time.Minutes),
					0,
					OutError))
			{
				return false;
			}
			HarvestAllowances[Index] = FMath::Max(0.0, HarvestAllowances[Index] - Harvest);
			if (!Authority->SetHarvestRemaining(
				Kingdom,
				FMath::FloorToInt64(HarvestAllowances[Index]),
				OutError))
			{
				return false;
			}

			const double Market = Authority->GetKingdomBalanceExact(
				Kingdom, ESimulationResource::Wood, TEXT("MarketWoodAvailable"));
			const double TargetMarketWood = 2.0 * Config.PopulationPerKingdom;
			const double TargetPrice = FMath::Clamp(
				FMath::Sqrt(TargetMarketWood / FMath::Max(Market, UE_DOUBLE_SMALL_NUMBER)),
				0.5,
				3.0);
			WoodPrices[Index] += (TargetPrice - WoodPrices[Index]) / HoursPerGameDay;
			if (!Authority->SetWoodPrice(Kingdom, WoodPrices[Index], OutError)) return false;
		}
		return true;
	}

	bool FV17UnifiedRuntime::PlaceStateImportOrder(FString& OutError)
	{
		const FKingdomSnapshot Snapshot = BuildKingdomSnapshot(Authority->GetCurrentTime(), EKingdom::A);
		const double ExpectedRepairWoodUse = FMath::Min(
			static_cast<double>(Snapshot.DamagedWaiting),
			RepairStartCapacityPerPersonPerDay * Config.PopulationPerKingdom * 3.0)
			* RepairWoodPerHome;
		const double TargetMarketWood = 2.0 * Config.PopulationPerKingdom;
		const double StockGapPerDay = FMath::Max(
			0.0,
			(TargetMarketWood
				- Snapshot.Stocks.MarketWoodAvailable
				- Snapshot.Stocks.WoodInTransit) / 3.0);
		double Quantity = FMath::Min(
			ExpectedRepairWoodUse + StockGapPerDay,
			StateImportDailyCapPerPerson * Config.PopulationPerKingdom);
		Quantity = FMath::Min(Quantity, ImportBudgetRemaining / StateImportPrice);
		Quantity = FMath::Min(
			Quantity,
			static_cast<double>(Snapshot.Stocks.TreasuryAvailable) / StateImportPrice);
		if (Quantity <= UE_DOUBLE_SMALL_NUMBER) return true;
		int64 Cost = FMath::CeilToInt64(Quantity * StateImportPrice);
		if (Cost > ImportBudgetRemaining || Cost > Snapshot.Stocks.TreasuryAvailable)
		{
			const int64 CoinLimit = FMath::Min(ImportBudgetRemaining, Snapshot.Stocks.TreasuryAvailable);
			Quantity = CoinLimit / StateImportPrice;
			Cost = FMath::CeilToInt64(Quantity * StateImportPrice);
		}
		if (Quantity <= UE_DOUBLE_SMALL_NUMBER) return true;
		if (!Authority->QueueStateImport(
			EKingdom::A, Quantity, Cost, StateImportPolicyID, OutError))
		{
			return false;
		}
		ImportBudgetRemaining -= Cost;
		return true;
	}

	bool FV17UnifiedRuntime::FreezeRepairAidEligibility(FString& OutError)
	{
		const int64 Required = PaymentCoins(
			static_cast<int32>(RepairWoodPerHome), WoodPrices[KingdomIndex(EKingdom::A)]);
		TArray<FV17AuthoritativeCellID> CellIDs;
		Authority->GetCells().GetKeys(CellIDs);
		CellIDs.Sort();
		int32 EligibleCount = 0;
		for (const FV17AuthoritativeCellID CellID : CellIDs)
		{
			const FV17AuthoritativeCellConfig* Cell = Authority->GetCells().Find(CellID);
			if (Cell == nullptr
				|| Cell->Count <= 0
				|| Cell->Key.Kingdom != EKingdom::A
				|| Cell->Key.IncomeBand != EIncomeBand::Low
				|| Cell->Key.HomeState != EHomeState::DamagedWaiting
				|| Cell->Key.bAidEligible
				|| (Authority->GetCellCash(CellID) + Authority->GetCellRepairCredit(CellID)) / Cell->Count >= Required)
			{
				continue;
			}
			EligibleCount += Cell->Count;
			FV17AuthoritativeCellID TargetCellID = 0;
			if (!Authority->SetJointCellAidEligibility(CellID, true, TargetCellID, OutError)) return false;
			FV17AuthoritativeCellID NormalizedCellID = 0;
			if (!Authority->NormalizeReadyCellBands(TargetCellID, NormalizedCellID, OutError)) return false;
		}
		return Authority->CreateInstantSystemEvent(
			TEXT("RepairAidEligibility"), EligibleCount, RepairAidPolicyID, OutError);
	}

	bool FV17UnifiedRuntime::PayRepairAid(FString& OutError)
	{
		const int64 Treasury = Authority->GetKingdomBalance(
			EKingdom::A, ESimulationResource::Coin, TEXT("TreasuryAvailable"));
		const int64 Budget = FMath::Min<int64>(
			FMath::FloorToInt64(0.40 * Config.PopulationPerKingdom), Treasury);
		int32 RemainingPaidCount = static_cast<int32>(
			Budget / static_cast<int64>(RepairAidPerHome));
		const int32 RequestedPaidCount = RemainingPaidCount;
		TArray<FV17AuthoritativeCellID> CellIDs;
		Authority->GetCells().GetKeys(CellIDs);
		CellIDs.Sort();
		for (const FV17AuthoritativeCellID CellID : CellIDs)
		{
			if (RemainingPaidCount <= 0) break;
			const FV17AuthoritativeCellConfig* Cell = Authority->GetCells().Find(CellID);
			if (Cell == nullptr || Cell->Count <= 0 || !Cell->Key.bAidEligible) continue;
			const int32 Count = FMath::Min(RemainingPaidCount, Cell->Count);
			FV17AuthoritativeCellID TargetCellID = 0;
			if (!Authority->GrantRepairAid(
				CellID,
				Count,
				static_cast<int32>(RepairAidPerHome),
				RepairAidPolicyID,
				TargetCellID,
				OutError))
			{
				return false;
			}
			FV17AuthoritativeCellID NormalizedCellID = 0;
			if (!Authority->NormalizeReadyCellBands(TargetCellID, NormalizedCellID, OutError)) return false;
			RemainingPaidCount -= Count;
		}
		const int32 PaidCount = RequestedPaidCount - RemainingPaidCount;
		return Authority->CreateInstantSystemEvent(
			TEXT("RepairAidPayment"), PaidCount, RepairAidPolicyID, OutError);
	}

	bool FV17UnifiedRuntime::ApplyPolicies(
		const FSimulationTime Time,
		FString& OutError)
	{
		if (Time.Minutes < 0 || Time.Minutes % MinutesPerDay != 0) return true;
		const int32 Day = static_cast<int32>(Time.Minutes / MinutesPerDay);
		switch (Scenario)
		{
		case EStage2Scenario::HarvestCap:
			if (Day == 2 && !Authority->CreateInstantSystemEvent(
				TEXT("HarvestCapAnnounced"), 1, HarvestCapPolicyID, OutError)) return false;
			if (Day == 3 && !Authority->CreateInstantSystemEvent(
				TEXT("HarvestCapActive"), 1, HarvestCapPolicyID, OutError)) return false;
			if (Day == 30 && !Authority->CreateInstantSystemEvent(
				TEXT("HarvestCapEnded"), 1, HarvestCapPolicyID, OutError)) return false;
			break;
		case EStage2Scenario::StateImport:
			if (Day == 2 && !Authority->CreateInstantSystemEvent(
				TEXT("StateImportAnnounced"), 1, StateImportPolicyID, OutError)) return false;
			if (Day >= 2 && Day <= 14 && !PlaceStateImportOrder(OutError)) return false;
			break;
		case EStage2Scenario::RepairAid:
			if (Day == 2 && !FreezeRepairAidEligibility(OutError)) return false;
			if (Day == 3 && !PayRepairAid(OutError)) return false;
			break;
		case EStage2Scenario::None:
		default:
			break;
		}
		return true;
	}

	bool FV17UnifiedRuntime::QueuePlannedFlows(FString& OutError)
	{
		TArray<FV17AuthoritativeCellID> CellIDs;
		Authority->GetCells().GetKeys(CellIDs);
		CellIDs.Sort();
		bool bQueued = false;
		for (const FV17AuthoritativeCellID CellID : CellIDs)
		{
			const FV17AuthoritativeCellConfig* Cell = Authority->GetCells().Find(CellID);
			if (Cell == nullptr || Cell->Count <= 0) continue;
			FResidentCoreState Representative;
			Representative.Kingdom = Cell->Key.Kingdom;
			Representative.Profession = Cell->Key.Profession;
			Representative.IncomeBand = Cell->Key.IncomeBand;
			Representative.HomeState = Cell->Key.HomeState;
			Representative.MacroIntent = Cell->Key.Intent;
			Representative.Cash = static_cast<int32>(Authority->GetCellCash(CellID) / Cell->Count);
			Representative.RepairCredit = static_cast<int32>(
				Authority->GetCellRepairCredit(CellID) / Cell->Count);
			Representative.InventoryWood = static_cast<int32>(Authority->GetCellWood(CellID) / Cell->Count);
			const FIndividualPlan Plan = FIndividualDomain::BuildPlan(
				Representative, BuildWorldFacts(Cell->Key.Kingdom));
			const EIndividualAction Action = Plan.Actions.IsEmpty()
				? EIndividualAction::Wait
				: Plan.Actions[0];
			FV17AuthoritativeClaimID ClaimID = 0;
			if (!Authority->QueueMacroAction(
				CellID,
				Action,
				Cell->Count,
				Action == EIndividualAction::ChopWood && IsHarvestCapActive(
					Cell->Key.Kingdom, Authority->GetCurrentTime()) ? HarvestCapPolicyID : 0,
				ClaimID,
				OutError))
			{
				return false;
			}
			++Diagnostics.PlanningEvaluationCount;
			++Diagnostics.CohortPlanningEvaluationCount;
			bQueued = true;
		}

		TArray<FResidentID> ActiveIDs;
		Authority->GetActiveResidentIDs(ActiveIDs);
		for (const FResidentID ResidentID : ActiveIDs)
		{
			if (!Authority->IsActiveReady(ResidentID)) continue;
			FIndividualActionState State;
			EIndividualAction CurrentAction = EIndividualAction::None;
			FEventID EventID = 0;
			const FV17IdentityRecord* Identity = Authority->FindIdentity(ResidentID);
			if (Identity == nullptr
				|| !Authority->GetActiveSnapshot(ResidentID, State, CurrentAction, EventID))
			{
				OutError = TEXT("An Active resident cannot be planned because its identity or current state is missing.");
				return false;
			}
			FResidentCoreState Resident;
			Resident.ResidentID = ResidentID;
			Resident.Kingdom = Identity->InitialKingdom;
			Resident.Profession = Identity->Profession;
			Resident.IncomeBand = Identity->IncomeBand;
			Resident.Cash = State.Cash;
			Resident.RepairCredit = State.RepairCredit;
			Resident.InventoryWood = State.Wood;
			Resident.HomeState = State.HomeState;
			const FIndividualPlan Plan = FIndividualDomain::BuildPlan(
				Resident, BuildWorldFacts(Resident.Kingdom));
			const EIndividualAction Action = Plan.Actions.IsEmpty()
				? EIndividualAction::Wait
				: Plan.Actions[0];
			FV17AuthoritativeClaimID ClaimID = 0;
			if (!Authority->QueueActiveAction(
				ResidentID,
				Action,
				Action == EIndividualAction::ChopWood && IsHarvestCapActive(
					Resident.Kingdom, Authority->GetCurrentTime()) ? HarvestCapPolicyID : 0,
				ClaimID,
				OutError))
			{
				return false;
			}
			++Diagnostics.PlanningEvaluationCount;
			++Diagnostics.ActiveMicroPlanningEvaluationCount;
			bQueued = true;
		}
		if (bQueued && !Authority->ResolveAndCommitClaims(OutError)) return false;
		ResolvePendingFirstActions();
		return true;
	}

	bool FV17UnifiedRuntime::ApplyActivationTrace(const FSimulationTime Time, FString& OutError)
	{
		bool bLift = false;
		int32 TraceDay = INDEX_NONE;
		if (Time.Minutes == FSimulationTime::FromDays(7).Minutes) { bLift = true; TraceDay = 7; }
		else if (Time.Minutes == FSimulationTime::FromDays(8).Minutes) { TraceDay = 7; }
		else if (Time.Minutes == FSimulationTime::FromDays(14).Minutes) { bLift = true; TraceDay = 14; }
		else if (Time.Minutes == FSimulationTime::FromDays(15).Minutes) { TraceDay = 14; }
		else if (Time.Minutes == FSimulationTime::FromDays(30).Minutes) { bLift = true; TraceDay = 30; }
		else if (Time.Minutes == FSimulationTime::FromDays(31).Minutes) { TraceDay = 30; }
		else if (Time.Minutes == FSimulationTime::FromDays(45).Minutes) { bLift = true; TraceDay = 45; }
		else if (Time.Minutes == FSimulationTime::FromDays(46).Minutes) { TraceDay = 45; }
		else return true;

		TArray<FResidentID> Residents;
		if (TraceDay == 14)
		{
			Residents = Day14ActivationResidents;
		}
		else
		{
			for (const FPersistentTestRecord& Record : ContinuitySample.Residents)
			{
				if ((TraceDay == 7 && Record.bDay7)
					|| (TraceDay == 30 && Record.bDay30)
					|| (TraceDay == 45 && Record.bDay45))
				{
					Residents.Add(Record.ResidentID);
				}
			}
			Residents.Sort();
		}
		for (const FResidentID ResidentID : Residents)
		{
			++Diagnostics.ActivationRequestCount;
			if (!bLift && PendingFirstActions.Contains(ResidentID))
			{
				RecordFirstAction(ResidentID, EIndividualAction::Wait, false);
			}
			const bool bSucceeded = bLift
				? Authority->LiftResident(ResidentID, Time, OutError)
				: Authority->RestrictResident(ResidentID, Time, OutError);
			if (!bSucceeded) return false;
			if (TraceDay == 14)
			{
				if (bLift) ++Diagnostics.Day14ActivationCount;
				else ++Diagnostics.Day14DeactivationCount;
			}
			if (bLift)
			{
				FUnifiedActivationObservation& Observation = ActivationObservations.AddDefaulted_GetRef();
				Observation.ResidentID = ResidentID;
				Observation.ActivationTime = Time;
				Observation.bSimpleReconstructed = false;
				PendingFirstActions.Add(ResidentID, ActivationObservations.Num() - 1);
				FIndividualActionState State;
				EIndividualAction Action = EIndividualAction::None;
				FEventID EventID = 0;
				if (Authority->GetActiveSnapshot(ResidentID, State, Action, EventID)
					&& Action != EIndividualAction::None)
				{
					RecordFirstAction(ResidentID, Action, true);
				}
			}
			++ResidentTouches;
		}
		MaxActiveCount = FMath::Max(MaxActiveCount, Authority->GetActiveMicroCount());
		OutError.Reset();
		return true;
	}

	void FV17UnifiedRuntime::ResolvePendingFirstActions()
	{
		TArray<FResidentID> ResidentIDs;
		PendingFirstActions.GetKeys(ResidentIDs);
		ResidentIDs.Sort();
		for (const FResidentID ResidentID : ResidentIDs)
		{
			FIndividualActionState State;
			EIndividualAction Action = EIndividualAction::None;
			FEventID EventID = 0;
			if (Authority->GetActiveSnapshot(ResidentID, State, Action, EventID)
				&& Action != EIndividualAction::None)
			{
				RecordFirstAction(ResidentID, Action, false);
			}
		}
	}

	void FV17UnifiedRuntime::RecordFirstAction(
		const FResidentID ResidentID,
		const EIndividualAction Action,
		const bool bContinuedEvent)
	{
		const int32* ObservationIndex = PendingFirstActions.Find(ResidentID);
		if (ObservationIndex == nullptr || !ActivationObservations.IsValidIndex(*ObservationIndex)) return;
		FUnifiedActivationObservation& Activation = ActivationObservations[*ObservationIndex];
		Activation.FirstAction = Action;
		Activation.bContinuedCommittedEvent = bContinuedEvent;

		FIndividualActionState State;
		EIndividualAction CurrentAction = EIndividualAction::None;
		FEventID EventID = 0;
		const FV17IdentityRecord* Identity = Authority->FindIdentity(ResidentID);
		if (Identity != nullptr
			&& Authority->GetActiveSnapshot(ResidentID, State, CurrentAction, EventID))
		{
			FUnifiedNPCObservation NPC;
			NPC.GameTime = Activation.ActivationTime;
			NPC.FirstAction = Action;
			FResidentCoreState& Resident = NPC.Resident;
			Resident.ResidentID = ResidentID;
			Resident.PersistentID = Identity->PersistentID;
			Resident.HomeID = Identity->HomeID;
			Resident.Kingdom = Identity->InitialKingdom;
			Resident.Profession = Identity->Profession;
			Resident.IncomeBand = Identity->IncomeBand;
			Resident.Cash = State.Cash;
			Resident.RepairCredit = State.RepairCredit;
			Resident.InventoryWood = State.Wood;
			Resident.HomeState = State.HomeState;
			Resident.CurrentGoal = FIndividualDomain::SelectGoal(Resident);
			Resident.CurrentAction = CurrentAction;
			Resident.MacroIntent = ToMacroIntent(CurrentAction);
			Resident.ActiveEventID = EventID;
			Resident.ActionStartTime = Activation.ActivationTime;
			Resident.ActionEndTime = FSimulationTime::FromMinutes(
				Activation.ActivationTime.Minutes + Authority->GetRemainingWorkMinutes(ResidentID));
			Resident.Representation = EResidentRepresentation::ActiveMicro;
			if (const FSimulationEventRecord* Event = Authority->GetEventStore().Find(EventID))
			{
				Resident.ActiveArriveID = Event->Event.ArriveID;
				Resident.ActiveReservationID = Event->Event.ReservationID;
				Resident.CausalPolicyID = Event->Event.PolicyID;
			}
			if (const FV17ContinuityCapsule* Capsule = Authority->FindCapsule(ResidentID))
			{
				if (!Capsule->KnownCompletedActions.IsEmpty())
				{
					Resident.LastCompletedAction = Capsule->KnownCompletedActions.Last();
				}
			}
			if (const FInitialResidentRecord* Initial = PopulationManifest.Residents.FindByPredicate(
				[ResidentID](const FInitialResidentRecord& Candidate)
				{
					return Candidate.ResidentID == ResidentID;
				}))
			{
				Resident.Name = Initial->Name;
			}
			if (Options.Mode != EUnifiedRunMode::Performance && Options.Observer != nullptr)
			{
				Options.Observer->OnNPCSnapshot(NPC);
			}
		}
		if (Options.Mode != EUnifiedRunMode::Performance && Options.EventSink != nullptr)
		{
			Options.EventSink->OnActivationObserved(Activation);
		}
		PendingFirstActions.Remove(ResidentID);
		++Diagnostics.FirstActionCount;
	}

	FKingdomStocks FV17UnifiedRuntime::BuildKingdomStocks(const EKingdom Kingdom) const
	{
		FKingdomStocks Stocks;
		Stocks.ForestCapacity = 20.0 * Config.PopulationPerKingdom;
		Stocks.ForestWood = Authority->GetKingdomBalanceExact(
			Kingdom, ESimulationResource::Wood, TEXT("ForestWood"));
		Stocks.MarketWoodAvailable = Authority->GetKingdomBalanceExact(
			Kingdom, ESimulationResource::Wood, TEXT("MarketWoodAvailable"));
		Stocks.MarketWoodReserved = Authority->GetKingdomBalanceExact(
			Kingdom, ESimulationResource::Wood, TEXT("MarketWoodReserved"));
		Stocks.WoodInTransit = Authority->GetKingdomBalanceExact(
			Kingdom, ESimulationResource::Wood, TEXT("WoodInTransit"));
		Stocks.WoodEmbeddedInRepairs = Authority->GetKingdomBalanceExact(
			Kingdom, ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs"));
		Stocks.WoodInRepairedHomes = Authority->GetKingdomBalanceExact(
			Kingdom, ESimulationResource::Wood, TEXT("WoodInRepairedHomes"));
		Stocks.TreasuryAvailable = Authority->GetKingdomBalance(
			Kingdom, ESimulationResource::Coin, TEXT("TreasuryAvailable"));
		Stocks.TreasuryReserved = Authority->GetKingdomBalance(
			Kingdom, ESimulationResource::Coin, TEXT("TreasuryReserved"));
		Stocks.MarketCoin = Authority->GetKingdomBalance(
			Kingdom, ESimulationResource::Coin, TEXT("MarketCoin"));
		Stocks.WoodPrice = WoodPrices[KingdomIndex(Kingdom)];

		for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Authority->GetCells())
		{
			if (Pair.Value.Key.Kingdom != Kingdom) continue;
			Stocks.ResidentInventoryWood += Authority->GetCellWood(Pair.Key);
			Stocks.ResidentRepairCredit += Authority->GetCellRepairCredit(Pair.Key);
		}
		for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : Authority->GetBatchEvents())
		{
			const FV17AuthoritativeBatchEvent& Event = Pair.Value;
			const FV17AuthoritativeCellConfig* Source = Authority->GetCells().Find(Event.SourceCellID);
			if (Event.Status != ESimulationEventState::Pending
				|| Source == nullptr
				|| Source->Key.Kingdom != Kingdom)
			{
				continue;
			}
			Stocks.ResidentInventoryWood += Authority->GetLedger().GetBalance(
				ESimulationResource::Wood, EventAccount(Event.BatchEventID, TEXT("Wood")));
			Stocks.ResidentRepairCredit += FMath::RoundToInt64(Authority->GetLedger().GetBalance(
				ESimulationResource::Coin, EventAccount(Event.BatchEventID, TEXT("RepairCredit"))));
		}
		TArray<FResidentID> ActiveIDs;
		Authority->GetActiveResidentIDs(ActiveIDs);
		for (const FResidentID ResidentID : ActiveIDs)
		{
			const FV17IdentityRecord* Identity = Authority->FindIdentity(ResidentID);
			if (Identity == nullptr || Identity->InitialKingdom != Kingdom) continue;
			if (Authority->IsActiveReady(ResidentID))
			{
				Stocks.ResidentInventoryWood += Authority->GetLedger().GetBalance(
					ESimulationResource::Wood, ActiveAccount(ResidentID, TEXT("Wood")));
				Stocks.ResidentRepairCredit += FMath::RoundToInt64(Authority->GetLedger().GetBalance(
					ESimulationResource::Coin, ActiveAccount(ResidentID, TEXT("RepairCredit"))));
			}
		}
		return Stocks;
	}

	FKingdomSnapshot FV17UnifiedRuntime::BuildKingdomSnapshot(
		const FSimulationTime Time,
		const EKingdom Kingdom) const
	{
		FKingdomSnapshot Snapshot;
		Snapshot.GameTime = Time;
		Snapshot.Kingdom = Kingdom;
		Snapshot.Stocks = BuildKingdomStocks(Kingdom);
		auto AddHome = [&Snapshot](const EHomeState HomeState, const int32 Count)
		{
			switch (HomeState)
			{
			case EHomeState::Healthy: Snapshot.Healthy += Count; break;
			case EHomeState::DamagedWaiting: Snapshot.DamagedWaiting += Count; break;
			case EHomeState::UnderRepair: Snapshot.UnderRepair += Count; break;
			case EHomeState::Repaired: Snapshot.Repaired += Count; break;
			}
		};
		for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Authority->GetCells())
		{
			if (Pair.Value.Key.Kingdom == Kingdom) AddHome(Pair.Value.Key.HomeState, Pair.Value.Count);
		}
		for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : Authority->GetBatchEvents())
		{
			const FV17AuthoritativeBatchEvent& Event = Pair.Value;
			const FV17AuthoritativeCellConfig* Source = Authority->GetCells().Find(Event.SourceCellID);
			if (Event.Status == ESimulationEventState::Pending
				&& Event.ActiveResidentID == 0
				&& Source != nullptr
				&& Source->Key.Kingdom == Kingdom)
			{
				AddHome(
					Event.Action == EIndividualAction::ContinueRepair
						? EHomeState::UnderRepair
						: Source->Key.HomeState,
					Event.ParticipantCount);
			}
		}
		TArray<FResidentID> ActiveIDs;
		Authority->GetActiveResidentIDs(ActiveIDs);
		for (const FResidentID ResidentID : ActiveIDs)
		{
			const FV17IdentityRecord* Identity = Authority->FindIdentity(ResidentID);
			FIndividualActionState State;
			EIndividualAction Action = EIndividualAction::None;
			FEventID EventID = 0;
			if (Identity != nullptr
				&& Identity->InitialKingdom == Kingdom
				&& Authority->GetActiveSnapshot(ResidentID, State, Action, EventID))
			{
				AddHome(State.HomeState, 1);
			}
		}
		Snapshot.LedgerTransactionCount = Authority->GetLedger().GetTransactions().Num();
		return Snapshot;
	}

	void FV17UnifiedRuntime::BuildCohortObservations(
		const FSimulationTime Time,
		TArray<FUnifiedCohortObservation>& OutObservations) const
	{
		TArray<FV17AuthoritativeCellID> CellIDs;
		Authority->GetCells().GetKeys(CellIDs);
		CellIDs.Sort();
		for (const FV17AuthoritativeCellID CellID : CellIDs)
		{
			const FV17AuthoritativeCellConfig& Cell = Authority->GetCells().FindChecked(CellID);
			if (Cell.Count <= 0) continue;
			FUnifiedCohortObservation& Observation = OutObservations.AddDefaulted_GetRef();
			Observation.GameTime = Time;
			Observation.CohortKey = OuterKeyString(Cell.Key);
			Observation.JointCellKey = JointKeyString(Cell.Key);
			Observation.JointCellID = CellID;
			Observation.Count = Cell.Count;
			Observation.CashSum = Authority->GetCellCash(CellID);
			Observation.RepairCreditSum = Authority->GetCellRepairCredit(CellID);
			Observation.WoodSum = Authority->GetCellWood(CellID);
			const int64 MeanCash = Observation.CashSum / Cell.Count;
			Observation.CashSquaredSum = MeanCash * MeanCash * Cell.Count;
			const int32 MeanWood = FMath::Clamp<int32>(Observation.WoodSum / Cell.Count, 0, 4);
			Observation.WoodCounts[MeanWood] = Cell.Count;
			Observation.MacroIntent = Cell.Key.Intent;
		}

		TArray<FEventID> EventIDs;
		Authority->GetBatchEvents().GetKeys(EventIDs);
		EventIDs.Sort();
		for (const FEventID EventID : EventIDs)
		{
			const FV17AuthoritativeBatchEvent& Event = Authority->GetBatchEvents().FindChecked(EventID);
			const FV17AuthoritativeCellConfig* Source = Authority->GetCells().Find(Event.SourceCellID);
			if (Event.Status != ESimulationEventState::Pending || Event.ActiveResidentID != 0 || Source == nullptr) continue;
			FV17AuthoritativeJointKey PendingKey = Source->Key;
			PendingKey.Intent = ToMacroIntent(Event.Action);
			if (Event.Action == EIndividualAction::ContinueRepair)
			{
				PendingKey.HomeState = EHomeState::UnderRepair;
			}
			FUnifiedCohortObservation& Observation = OutObservations.AddDefaulted_GetRef();
			Observation.GameTime = Time;
			Observation.CohortKey = OuterKeyString(PendingKey);
			Observation.JointCellKey = JointKeyString(PendingKey);
			Observation.JointCellID = Event.SourceCellID;
			Observation.Count = Event.ParticipantCount;
			Observation.PendingParticipantCount = Event.ParticipantCount;
			Observation.CashSum = FMath::RoundToInt64(Authority->GetLedger().GetBalance(
				ESimulationResource::Coin, EventAccount(EventID, TEXT("Cash"))));
			Observation.RepairCreditSum = FMath::RoundToInt64(Authority->GetLedger().GetBalance(
				ESimulationResource::Coin, EventAccount(EventID, TEXT("RepairCredit"))));
			Observation.WoodSum = FMath::RoundToInt64(Authority->GetLedger().GetBalance(
				ESimulationResource::Wood, EventAccount(EventID, TEXT("Wood"))));
			Observation.MacroIntent = PendingKey.Intent;
		}
	}

	FString FV17UnifiedRuntime::PolicyStateAt(const FSimulationTime ProcessedTime) const
	{
		const int64 Day2 = FSimulationTime::FromDays(2).Minutes;
		const int64 Day3 = FSimulationTime::FromDays(3).Minutes;
		switch (Scenario)
		{
		case EStage2Scenario::HarvestCap:
			if (ProcessedTime.Minutes >= FSimulationTime::FromDays(30).Minutes) return TEXT("Ended");
			if (ProcessedTime.Minutes >= Day3) return TEXT("Active");
			return ProcessedTime.Minutes >= Day2 ? TEXT("Announced") : TEXT("Inactive");
		case EStage2Scenario::StateImport:
			if (ProcessedTime.Minutes >= FSimulationTime::FromDays(15).Minutes) return TEXT("Ended");
			return ProcessedTime.Minutes >= Day2 ? TEXT("Active") : TEXT("Inactive");
		case EStage2Scenario::RepairAid:
			if (ProcessedTime.Minutes >= Day3) return TEXT("Paid");
			return ProcessedTime.Minutes >= Day2 ? TEXT("EligibilityFrozen") : TEXT("Inactive");
		case EStage2Scenario::None:
		default:
			return TEXT("None");
		}
	}

	void FV17UnifiedRuntime::PublishObservations(
		const FSimulationTime GameTime,
		const FSimulationTime ProcessedTime)
	{
		if (Options.Mode == EUnifiedRunMode::Performance || Options.Observer == nullptr) return;
		const double Start = FPlatformTime::Seconds();
		FUnifiedHourObservation Observation;
		Observation.GameTime = GameTime;
		Observation.KingdomA = BuildKingdomSnapshot(GameTime, EKingdom::A);
		Observation.KingdomB = BuildKingdomSnapshot(GameTime, EKingdom::B);
		Observation.PolicyState = PolicyStateAt(ProcessedTime);
		if (GameTime.Minutes % (6 * MinutesPerHour) == 0)
		{
			BuildCohortObservations(GameTime, Observation.Cohorts);
		}
		Options.Observer->OnHourCompleted(Observation);
		LastStepMeasurement.ObserverCpuMs += (FPlatformTime::Seconds() - Start) * 1000.0;
	}

	bool FV17UnifiedRuntime::StepHour(FString& OutError)
	{
		if (!Authority || IsComplete())
		{
			OutError = TEXT("The v1.7 unified runtime is not ready for another hour.");
			return false;
		}
		LastStepMeasurement = {};
		const double StepStart = FPlatformTime::Seconds();
		const double MacroStart = FPlatformTime::Seconds();
		const FSimulationTime Time = Authority->GetCurrentTime();
		if ((Time.Minutes != 0 || bEarthquakeApplied || ApplyEarthquake(OutError))
			&& ApplyPolicies(Time, OutError)
			&& ApplyEnvironment(Time, OutError)
			&& QueuePlannedFlows(OutError))
		{
			// All production work for the current hour committed successfully.
		}
		else
		{
			return false;
		}
		LastStepMeasurement.MacroCpuMs = (FPlatformTime::Seconds() - MacroStart) * 1000.0;

		const FSimulationTime End = FSimulationTime::FromMinutes(
			Authority->GetCurrentTime().Minutes + MinutesPerHour);
		if (!Authority->AdvanceTo(End, OutError)) return false;
		const double TransitionStart = FPlatformTime::Seconds();
		if (!ApplyActivationTrace(End, OutError)) return false;
		LastStepMeasurement.TransitionCpuMs = (FPlatformTime::Seconds() - TransitionStart) * 1000.0;
		PublishObservations(End, Time);

		if (Options.Mode != EUnifiedRunMode::Performance)
		{
			const double AuditStart = FPlatformTime::Seconds();
			++Diagnostics.FullAuditCount;
			if (!Authority->BuildAudit().IsHardErrorFree())
			{
				OutError = TEXT("The v1.7 hourly hard-error check failed.");
				return false;
			}
			LastStepMeasurement.AuditCpuMs = (FPlatformTime::Seconds() - AuditStart) * 1000.0;
		}
		LastStepMeasurement.GameTime = End;
		LastStepMeasurement.ActiveCount = Authority->GetActiveMicroCount();
		LastStepMeasurement.QueueLength = Authority->GetScheduler().NumPending();
		MaxActiveCount = FMath::Max(MaxActiveCount, LastStepMeasurement.ActiveCount);
		const double StepMs = (FPlatformTime::Seconds() - StepStart) * 1000.0;
		LastStepMeasurement.ProductionCpuMs = FMath::Max(
			0.0,
			StepMs - LastStepMeasurement.AuditCpuMs - LastStepMeasurement.ObserverCpuMs);
		CostBreakdown.ProductionCpuMs += LastStepMeasurement.ProductionCpuMs;
		CostBreakdown.MacroCpuMs += LastStepMeasurement.MacroCpuMs;
		CostBreakdown.TransitionCpuMs += LastStepMeasurement.TransitionCpuMs;
		CostBreakdown.AuditCpuMs += LastStepMeasurement.AuditCpuMs;
		CostBreakdown.ObserverCpuMs += LastStepMeasurement.ObserverCpuMs;
		OutError.Reset();
		return true;
	}

	bool FV17UnifiedRuntime::Finalize(FUnifiedRunResult& OutResult, FString& OutError)
	{
		if (!Authority || !IsComplete())
		{
			OutError = TEXT("The v1.7 unified runtime can only finalize at D60T00:00.");
			return false;
		}
		const double FinalizeStart = FPlatformTime::Seconds();
		const double AuditStart = FPlatformTime::Seconds();
		++Diagnostics.FullAuditCount;
		const FV17AuthoritativeAudit Audit = Authority->BuildAudit();
		const double FinalAuditMs = (FPlatformTime::Seconds() - AuditStart) * 1000.0;
		CostBreakdown.AuditCpuMs += FinalAuditMs;
		if (!Audit.IsHardErrorFree())
		{
			OutError = TEXT("The final v1.7 hard-error check failed.");
			return false;
		}

		OutResult = {};
		OutResult.Method = EUnifiedSimulationMethod::Proposed;
		OutResult.Scenario = Scenario;
		OutResult.Seed = Config.Seed;
		OutResult.PopulationPerKingdom = Config.PopulationPerKingdom;
		OutResult.Mode = Options.Mode;
		OutResult.bRetainCompletedEvents = Options.bRetainCompletedEvents;
		OutResult.bRecordSnapshots = Options.bRecordSnapshots;
		OutResult.bVerifyCohortApproximation = Options.bVerifyCohortApproximation;
		OutResult.bEnableMacroProfiling = Options.bEnableMacroProfiling;
		OutResult.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
		OutResult.ModelSpecVersion = TEXT("1.7");
		OutResult.LogSchemaVersion = TEXT("1.2");
		OutResult.AuthorityMode = TEXT("v1.7_authoritative");
		OutResult.JointStateVersion = TEXT("1.7");
		OutResult.ClaimAllocationVersion = TEXT("1.7");
		OutResult.CapsuleVersion = TEXT("1");
		OutResult.DeterministicDigestVersion = TEXT("1.7-domain-v2");
		OutResult.bFormalModelEligible = false;
		OutResult.ConfigHash = PopulationManifest.ConfigHash;
		OutResult.FinalTime = Authority->GetCurrentTime();
		OutResult.WarmupHourSteps = 7 * static_cast<int32>(HoursPerDay);
		OutResult.FormalHourSteps = 60 * static_cast<int32>(HoursPerDay);
		OutResult.V17Audit = Audit;
		OutResult.V17DeterministicDigest = Authority->BuildDeterministicDigest();
		const FKingdomSnapshot KingdomA = BuildKingdomSnapshot(OutResult.FinalTime, EKingdom::A);
		const FKingdomSnapshot KingdomB = BuildKingdomSnapshot(OutResult.FinalTime, EKingdom::B);
		OutResult.KingdomAStocks = KingdomA.Stocks;
		OutResult.KingdomBStocks = KingdomB.Stocks;
		OutResult.KingdomAHomeStates[static_cast<int32>(EHomeState::Healthy)] = KingdomA.Healthy;
		OutResult.KingdomAHomeStates[static_cast<int32>(EHomeState::DamagedWaiting)] = KingdomA.DamagedWaiting;
		OutResult.KingdomAHomeStates[static_cast<int32>(EHomeState::UnderRepair)] = KingdomA.UnderRepair;
		OutResult.KingdomAHomeStates[static_cast<int32>(EHomeState::Repaired)] = KingdomA.Repaired;
		OutResult.KingdomBHomeStates[static_cast<int32>(EHomeState::Healthy)] = KingdomB.Healthy;
		OutResult.KingdomBHomeStates[static_cast<int32>(EHomeState::DamagedWaiting)] = KingdomB.DamagedWaiting;
		OutResult.KingdomBHomeStates[static_cast<int32>(EHomeState::UnderRepair)] = KingdomB.UnderRepair;
		OutResult.KingdomBHomeStates[static_cast<int32>(EHomeState::Repaired)] = KingdomB.Repaired;
		for (const FScheduledEvent& Pending : Authority->GetScheduler().GetPendingEvents())
		{
			OutResult.PendingEventsAtOrBeforeEnd += Pending.ExecuteAt <= OutResult.FinalTime ? 1 : 0;
		}
		OutResult.CostBreakdown = CostBreakdown;
		FillDiagnostics(OutResult);
		OutResult.Transactions = Authority->GetLedger().GetTransactions();
		for (const TPair<FEventID, FSimulationEventRecord>& Pair : Authority->GetEventStore().GetEvents())
		{
			OutResult.Events.Add(Pair.Value);
		}
		OutResult.Events.Sort([](const FSimulationEventRecord& Left, const FSimulationEventRecord& Right)
		{
			return Left.EventID < Right.EventID;
		});
		OutResult.V17BatchEvents = Authority->GetBatchEvents();
		OutResult.V17LODTransitions = Authority->GetLODTransitions();
		OutResult.ActivationObservations = ActivationObservations;
		if (Options.Mode != EUnifiedRunMode::Performance && Options.EventSink != nullptr)
		{
			for (const FSimulationEventRecord& Event : OutResult.Events)
			{
				Options.EventSink->OnEventCommitted(Event);
			}
			for (const FLedgerTransaction& Transaction : OutResult.Transactions)
			{
				Options.EventSink->OnTransactionCommitted(Transaction);
			}
		}
		CostBreakdown.FinalizeCpuMs = FMath::Max(
			0.0,
			(FPlatformTime::Seconds() - FinalizeStart) * 1000.0 - FinalAuditMs);
		OutResult.CostBreakdown = CostBreakdown;
		OutError.Reset();
		return true;
	}

	void FV17UnifiedRuntime::FillDiagnostics(FUnifiedRunResult& OutResult) const
	{
		FUnifiedRunDiagnostics& OutDiagnostics = OutResult.Diagnostics;
		OutDiagnostics.V17IdentityCount = Authority->GetIdentityRegistry().Num();
		OutDiagnostics.V17IdentityScanCountPerHour = 0;
		OutDiagnostics.V17ResidentTouches = ResidentTouches;
		OutDiagnostics.V17BatchClaimCount = Authority->GetClaims().Num();
		OutDiagnostics.V17BatchEventCount = Authority->GetBatchEvents().Num();
		for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : Authority->GetBatchEvents())
		{
			OutDiagnostics.V17ParticipantCount += Pair.Value.ParticipantCount;
		}
		for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Authority->GetCells())
		{
			OutDiagnostics.V17NonEmptyJointCellCount += Pair.Value.Count > 0 ? 1 : 0;
		}
		OutDiagnostics.V17CapsuleCount = Authority->GetCapsules().Num();
		OutDiagnostics.V17ParticipantRefCount = Authority->GetParticipantRefs().Num();
		for (const FV17LODTransitionRecord& Transition : Authority->GetLODTransitions())
		{
			if (Transition.Result != EV17LODTransitionResult::Committed) continue;
			if (Transition.bLift) ++OutDiagnostics.V17LiftCount;
			else ++OutDiagnostics.V17RestrictCount;
		}
		OutDiagnostics.MaxActiveMicro = MaxActiveCount;
		OutDiagnostics.PlanningEvaluationCount = Diagnostics.PlanningEvaluationCount;
		OutDiagnostics.CohortPlanningEvaluationCount = Diagnostics.CohortPlanningEvaluationCount;
		OutDiagnostics.ActiveMicroPlanningEvaluationCount = Diagnostics.ActiveMicroPlanningEvaluationCount;
		OutDiagnostics.FullAuditCount = Diagnostics.FullAuditCount;
		OutDiagnostics.ActivationRequestCount = Diagnostics.ActivationRequestCount;
		OutDiagnostics.Day14ActivationCount = Diagnostics.Day14ActivationCount;
		OutDiagnostics.Day14DeactivationCount = Diagnostics.Day14DeactivationCount;
		OutDiagnostics.FirstActionCount = Diagnostics.FirstActionCount;
		OutDiagnostics.TransactionCount = Authority->GetLedger().GetTransactions().Num();
		OutDiagnostics.EventCount = Authority->GetEventStore().GetEvents().Num();
	}

	bool FV17UnifiedRuntime::IsComplete() const
	{
		return Authority && Authority->GetCurrentTime() == FSimulationTime::FromDays(60);
	}

	FSimulationTime FV17UnifiedRuntime::GetCurrentTime() const
	{
		return Authority ? Authority->GetCurrentTime() : FSimulationTime::FromDays(-7);
	}
}
