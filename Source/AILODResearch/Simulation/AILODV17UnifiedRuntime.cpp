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
		if (Scenario != EStage2Scenario::None)
		{
			OutError = TEXT("B5A only opens the v1.7 engineering Runner for the no-policy smoke scenario; B5B must connect policy behavior before other scenarios are allowed.");
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
		const double AuditStart = FPlatformTime::Seconds();
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

	bool FV17UnifiedRuntime::QueueReadyFlows(FString& OutError)
	{
		TArray<FV17AuthoritativeCellID> CellIDs;
		Authority->GetCells().GetKeys(CellIDs);
		CellIDs.Sort();
		bool bQueued = false;
		for (const FV17AuthoritativeCellID CellID : CellIDs)
		{
			const int32 Count = Authority->GetReadyCount(CellID);
			if (Count <= 0) continue;
			FV17AuthoritativeClaimID ClaimID = 0;
			if (!Authority->QueueMacroAction(
				CellID, EIndividualAction::Routine, Count, 0, ClaimID, OutError))
			{
				return false;
			}
			bQueued = true;
		}

		TArray<FResidentID> ActiveIDs;
		Authority->GetActiveResidentIDs(ActiveIDs);
		for (const FResidentID ResidentID : ActiveIDs)
		{
			if (!Authority->IsActiveReady(ResidentID)) continue;
			FV17AuthoritativeClaimID ClaimID = 0;
			if (!Authority->QueueActiveAction(
				ResidentID, EIndividualAction::Routine, 0, ClaimID, OutError))
			{
				return false;
			}
			bQueued = true;
		}
		return !bQueued || Authority->ResolveAndCommitClaims(OutError);
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
			const bool bSucceeded = bLift
				? Authority->LiftResident(ResidentID, Time, OutError)
				: Authority->RestrictResident(ResidentID, Time, OutError);
			if (!bSucceeded) return false;
			++ResidentTouches;
		}
		MaxActiveCount = FMath::Max(MaxActiveCount, Authority->GetActiveMicroCount());
		OutError.Reset();
		return true;
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
		if (!QueueReadyFlows(OutError)) return false;
		LastStepMeasurement.MacroCpuMs = (FPlatformTime::Seconds() - MacroStart) * 1000.0;

		const FSimulationTime End = FSimulationTime::FromMinutes(
			Authority->GetCurrentTime().Minutes + MinutesPerHour);
		if (!Authority->AdvanceTo(End, OutError)) return false;
		const double TransitionStart = FPlatformTime::Seconds();
		if (!ApplyActivationTrace(End, OutError)) return false;
		LastStepMeasurement.TransitionCpuMs = (FPlatformTime::Seconds() - TransitionStart) * 1000.0;

		if (Options.Mode != EUnifiedRunMode::Performance)
		{
			const double AuditStart = FPlatformTime::Seconds();
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
			0.0, StepMs - LastStepMeasurement.AuditCpuMs);
		CostBreakdown.ProductionCpuMs += LastStepMeasurement.ProductionCpuMs;
		CostBreakdown.MacroCpuMs += LastStepMeasurement.MacroCpuMs;
		CostBreakdown.TransitionCpuMs += LastStepMeasurement.TransitionCpuMs;
		CostBreakdown.AuditCpuMs += LastStepMeasurement.AuditCpuMs;
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
		OutResult.bValidForFormalExperiment = false;
		OutResult.ConfigHash = PopulationManifest.ConfigHash;
		OutResult.FinalTime = Authority->GetCurrentTime();
		OutResult.WarmupHourSteps = 7 * static_cast<int32>(HoursPerDay);
		OutResult.FormalHourSteps = 60 * static_cast<int32>(HoursPerDay);
		OutResult.V17Audit = Audit;
		OutResult.V17DeterministicDigest = Authority->BuildDeterministicDigest();
		OutResult.CostBreakdown = CostBreakdown;
		FillDiagnostics(OutResult);
		OutResult.Transactions = Authority->GetLedger().GetTransactions();
		CostBreakdown.FinalizeCpuMs = FMath::Max(
			0.0,
			(FPlatformTime::Seconds() - FinalizeStart) * 1000.0 - FinalAuditMs);
		OutResult.CostBreakdown = CostBreakdown;
		OutError.Reset();
		return true;
	}

	void FV17UnifiedRuntime::FillDiagnostics(FUnifiedRunResult& OutResult) const
	{
		FUnifiedRunDiagnostics& Diagnostics = OutResult.Diagnostics;
		Diagnostics.V17IdentityCount = Authority->GetIdentityRegistry().Num();
		Diagnostics.V17IdentityScanCountPerHour = 0;
		Diagnostics.V17ResidentTouches = ResidentTouches;
		Diagnostics.V17BatchClaimCount = Authority->GetClaims().Num();
		Diagnostics.V17BatchEventCount = Authority->GetBatchEvents().Num();
		for (const TPair<FEventID, FV17AuthoritativeBatchEvent>& Pair : Authority->GetBatchEvents())
		{
			Diagnostics.V17ParticipantCount += Pair.Value.ParticipantCount;
		}
		for (const TPair<FV17AuthoritativeCellID, FV17AuthoritativeCellConfig>& Pair : Authority->GetCells())
		{
			Diagnostics.V17NonEmptyJointCellCount += Pair.Value.Count > 0 ? 1 : 0;
		}
		Diagnostics.V17CapsuleCount = Authority->GetCapsules().Num();
		Diagnostics.V17ParticipantRefCount = Authority->GetParticipantRefs().Num();
		for (const FV17LODTransitionRecord& Transition : Authority->GetLODTransitions())
		{
			if (Transition.Result != EV17LODTransitionResult::Committed) continue;
			if (Transition.bLift) ++Diagnostics.V17LiftCount;
			else ++Diagnostics.V17RestrictCount;
		}
		Diagnostics.MaxActiveMicro = MaxActiveCount;
		Diagnostics.TransactionCount = Authority->GetLedger().GetTransactions().Num();
		Diagnostics.EventCount = Authority->GetBatchEvents().Num();
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
