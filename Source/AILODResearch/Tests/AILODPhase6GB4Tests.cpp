// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODV17AuthoritativeMacro.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace AILOD;

	struct FDynamicTraceFixture
	{
		TArray<FV17AuthoritativeCellConfig> Cells;
		TArray<FV17IdentityRecord> Identities;
		TArray<FV17AuthoritativeKingdomConfig> Kingdoms;
		TArray<FResidentID> Day7;
		TArray<FResidentID> Day14;
		TArray<FResidentID> Day45;
		FResidentID OutOfSampleResidentID = 0;
	};

	FV17AuthoritativeJointKey MakeKey(
		const EKingdom Kingdom,
		const EProfession Profession,
		const EIncomeBand IncomeBand,
		const EHomeState HomeState,
		const int32 PowerBand,
		const int32 WoodBand)
	{
		FV17AuthoritativeJointKey Key;
		Key.Kingdom = Kingdom;
		Key.Profession = Profession;
		Key.IncomeBand = IncomeBand;
		Key.HomeState = HomeState;
		Key.Intent = EMacroIntent::Routine;
		Key.PurchasingPowerBand = PowerBand;
		Key.WoodBand = WoodBand;
		return Key;
	}

	FString StratumKey(
		const EKingdom Kingdom,
		const EProfession Profession,
		const EIncomeBand IncomeBand)
	{
		return FString::Printf(
			TEXT("%d,%d,%d"),
			static_cast<int32>(Kingdom),
			static_cast<int32>(Profession),
			static_cast<int32>(IncomeBand));
	}

	FDynamicTraceFixture BuildDynamicTraceFixture()
	{
		FDynamicTraceFixture Fixture;
		Fixture.Kingdoms =
		{
			{ EKingdom::A, 0, 0, 0, 0, 0, 0, 100, 1.0 },
			{ EKingdom::B, 0, 0, 0, 0, 0, 0, 100, 1.0 }
		};
		TMap<FString, TArray<FResidentID>> Strata;
		FResidentID NextResidentID = 1;
		uint64 NextCellID = 0xB410;
		for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
		{
			for (const EProfession Profession : { EProfession::Logger, EProfession::Worker })
			{
				for (const EIncomeBand IncomeBand : { EIncomeBand::Low, EIncomeBand::NonLow })
				{
					const FV17AuthoritativeCellID CellID = NextCellID++;
					Fixture.Cells.Add({
						CellID,
						MakeKey(Kingdom, Profession, IncomeBand, EHomeState::Healthy, 0, 0),
						12,
						24,
						0,
						0
					});
					TArray<FResidentID>& Members = Strata.FindOrAdd(StratumKey(Kingdom, Profession, IncomeBand));
					for (int32 Index = 0; Index < 12; ++Index)
					{
						FV17IdentityRecord Identity;
						Identity.ResidentID = NextResidentID;
						Identity.PersistentID = 100000 + NextResidentID;
						Identity.NameSeed = 200000 + static_cast<uint32>(NextResidentID);
						Identity.AppearanceSeed = 300000 + static_cast<uint32>(NextResidentID);
						Identity.HomeID = 400000 + NextResidentID;
						Identity.InitialKingdom = Kingdom;
						Identity.Profession = Profession;
						Identity.IncomeBand = IncomeBand;
						Fixture.Identities.Add(Identity);
						Members.Add(NextResidentID++);
					}
				}
			}
		}

		for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
		{
			const TArray<FResidentID>& LoggerLow = Strata.FindChecked(StratumKey(
				Kingdom, EProfession::Logger, EIncomeBand::Low));
			for (int32 Index = 0; Index < 5; ++Index) Fixture.Day7.Add(LoggerLow[Index]);
			for (int32 Index = 0; Index < 10; ++Index) Fixture.Day45.Add(LoggerLow[Index]);

			const struct
			{
				EProfession Profession;
				EIncomeBand IncomeBand;
				int32 Count;
			} Quotas[] =
			{
				{ EProfession::Logger, EIncomeBand::Low, 1 },
				{ EProfession::Logger, EIncomeBand::NonLow, 1 },
				{ EProfession::Worker, EIncomeBand::Low, 6 },
				{ EProfession::Worker, EIncomeBand::NonLow, 2 }
			};
			for (const auto& Quota : Quotas)
			{
				const TArray<FResidentID>& Members = Strata.FindChecked(StratumKey(
					Kingdom, Quota.Profession, Quota.IncomeBand));
				int32 Added = 0;
				for (const FResidentID ResidentID : Members)
				{
					if (!Fixture.Day45.Contains(ResidentID))
					{
						Fixture.Day14.Add(ResidentID);
						if (++Added == Quota.Count) break;
					}
				}
			}
		}
		Fixture.Day7.Sort();
		Fixture.Day14.Sort();
		Fixture.Day45.Sort();
		Fixture.OutOfSampleResidentID = Strata.FindChecked(StratumKey(
			EKingdom::B, EProfession::Worker, EIncomeBand::NonLow)).Last();
		return Fixture;
	}

	bool InitializeDynamicFixture(
		FAutomationTestBase& Test,
		FV17AuthoritativeMacroSession& Session,
		const FDynamicTraceFixture& Fixture)
	{
		FString Error;
		if (!Session.InitializeWithIdentity(
			Fixture.Cells,
			Fixture.Identities,
			Fixture.Kingdoms,
			FSimulationTime::FromDays(0),
			Error))
		{
			Test.AddError(FString::Printf(TEXT("B4 dynamic fixture initialization failed: %s"), *Error));
			return false;
		}
		return true;
	}

	bool ApplyResidentSet(
		FAutomationTestBase& Test,
		FV17AuthoritativeMacroSession& Session,
		const TArray<FResidentID>& Residents,
		const FSimulationTime Time,
		const bool bLift)
	{
		FString Error;
		for (const FResidentID ResidentID : Residents)
		{
			const bool bSucceeded = bLift
				? Session.LiftResident(ResidentID, Time, Error)
				: Session.RestrictResident(ResidentID, Time, Error);
			if (!bSucceeded)
			{
				Test.AddError(FString::Printf(
					TEXT("B4 fixed trace %s failed for resident %lld at minute %lld: %s"),
					bLift ? TEXT("Lift") : TEXT("Restrict"),
					ResidentID,
					Time.Minutes,
					*Error));
				return false;
			}
		}
		return true;
	}

	bool RunFixedTrace(
		FAutomationTestBase& Test,
		FV17AuthoritativeMacroSession& Session,
		const FDynamicTraceFixture& Fixture,
		int32& OutMaxActive)
	{
		struct FTracePoint
		{
			int32 Day = 0;
			const TArray<FResidentID>* Residents = nullptr;
			bool bLift = true;
		};
		const FTracePoint Trace[] =
		{
			{ 7, &Fixture.Day7, true },
			{ 8, &Fixture.Day7, false },
			{ 14, &Fixture.Day14, true },
			{ 15, &Fixture.Day14, false },
			{ 30, &Fixture.Day7, true },
			{ 31, &Fixture.Day7, false },
			{ 45, &Fixture.Day45, true },
			{ 46, &Fixture.Day45, false }
		};
		FString Error;
		OutMaxActive = 0;
		for (const FTracePoint& Point : Trace)
		{
			const FSimulationTime Time = FSimulationTime::FromDays(Point.Day);
			if (!Session.AdvanceTo(Time, Error))
			{
				Test.AddError(FString::Printf(TEXT("B4 fixed trace could not reach Day %d: %s"), Point.Day, *Error));
				return false;
			}
			if (!ApplyResidentSet(Test, Session, *Point.Residents, Time, Point.bLift)) return false;
			OutMaxActive = FMath::Max(OutMaxActive, Session.GetActiveMicroCount());
			if (!Session.BuildAudit().IsHardErrorFree())
			{
				Test.AddError(FString::Printf(TEXT("B4 fixed trace audit failed at Day %d."), Point.Day));
				return false;
			}
		}
		return true;
	}

	int64 TotalCellResource(
		const FV17AuthoritativeMacroSession& Session,
		const FDynamicTraceFixture& Fixture,
		const bool bWood)
	{
		int64 Total = 0;
		for (const FV17AuthoritativeCellConfig& Cell : Fixture.Cells)
		{
			Total += bWood
				? Session.GetCellWood(Cell.CellID)
				: Session.GetCellCash(Cell.CellID) + Session.GetCellRepairCredit(Cell.CellID);
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB4DynamicTraceAndCapTest,
	"AILODResearch.Phase6G.V17DynamicTraceAndCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB4DynamicTraceAndCapTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const FDynamicTraceFixture Fixture = BuildDynamicTraceFixture();
	TestEqual(TEXT("Day 7 uses the frozen ten-person continuity sample"), Fixture.Day7.Num(), 10);
	TestEqual(TEXT("Day 14 uses twenty people outside the continuity sample"), Fixture.Day14.Num(), 20);
	TestEqual(TEXT("Day 45 uses all twenty continuity residents"), Fixture.Day45.Num(), 20);
	for (const FResidentID ResidentID : Fixture.Day14)
	{
		TestFalse(TEXT("Day 14 excludes the fixed continuity residents"), Fixture.Day45.Contains(ResidentID));
	}

	FV17AuthoritativeMacroSession TraceA(20260810);
	FV17AuthoritativeMacroSession TraceB(20260810);
	if (!InitializeDynamicFixture(*this, TraceA, Fixture)
		|| !InitializeDynamicFixture(*this, TraceB, Fixture))
	{
		return false;
	}
	int32 MaxActiveA = 0;
	int32 MaxActiveB = 0;
	if (!RunFixedTrace(*this, TraceA, Fixture, MaxActiveA)
		|| !RunFixedTrace(*this, TraceB, Fixture, MaxActiveB))
	{
		return false;
	}
	const FString FixedTraceDigest = TraceA.BuildDeterministicDigest();
	TestEqual(TEXT("The fixed B4 trace has the frozen digest"), FixedTraceDigest,
		FString(TEXT("EC735B18390C50437E52BF77B5C79D3BDB3D1903")));
	TestEqual(TEXT("The same Seed and fixed trace replay exactly"), TraceB.BuildDeterministicDigest(), FixedTraceDigest);
	TestEqual(TEXT("The fixed trace creates exactly sixty Lift records"),
		TraceA.GetLODTransitions().FilterByPredicate([](const FV17LODTransitionRecord& Record)
		{
			return Record.bLift && Record.Result == EV17LODTransitionResult::Committed;
		}).Num(), 60);
	TestEqual(TEXT("The fixed trace returns every resident to Cohort state"), TraceA.GetActiveMicroCount(), 0);
	TestEqual(TEXT("The largest fixed trace sample contains twenty Active residents"), MaxActiveA, 20);
	TestTrue(TEXT("The fixed trace leaves identity, population, resources and event links consistent"),
		TraceA.BuildAudit().IsHardErrorFree());

	FV17AuthoritativeMacroSession ContinuityRun(20260810);
	if (!InitializeDynamicFixture(*this, ContinuityRun, Fixture)) return false;
	const FV17IdentityRecord IdentityBefore = *ContinuityRun.FindIdentity(Fixture.OutOfSampleResidentID);
	const int64 CoinBefore = TotalCellResource(ContinuityRun, Fixture, false);
	const int64 WoodBefore = TotalCellResource(ContinuityRun, Fixture, true);
	const int32 PendingBefore = ContinuityRun.GetPendingParticipantCount();
	FString Error;
	const FString BeforeFailedLift = ContinuityRun.BuildDeterministicDigest();
	TestFalse(TEXT("A forced Lift failure is reported"), ContinuityRun.LiftResident(
		Fixture.OutOfSampleResidentID,
		FSimulationTime::FromDays(0),
		Error,
		EV17LODTransitionFailurePoint::LiftAfterLedgerTransfer));
	TestEqual(TEXT("A failed Lift leaves the whole authority unchanged"),
		ContinuityRun.BuildDeterministicDigest(), BeforeFailedLift);

	TestTrue(TEXT("An identity outside the formal samples can still be Lifted by ResidentID"),
		ContinuityRun.LiftResident(Fixture.OutOfSampleResidentID, FSimulationTime::FromDays(0), Error));
	const FString BeforeFailedRestrict = ContinuityRun.BuildDeterministicDigest();
	TestFalse(TEXT("A forced Restrict failure is reported"), ContinuityRun.RestrictResident(
		Fixture.OutOfSampleResidentID,
		FSimulationTime::FromDays(0),
		Error,
		EV17LODTransitionFailurePoint::RestrictAfterLedgerTransfer));
	TestEqual(TEXT("A failed Restrict restores the resident, resources and Capsule"),
		ContinuityRun.BuildDeterministicDigest(), BeforeFailedRestrict);
	TestTrue(TEXT("The same-time Restrict succeeds after rollback"), ContinuityRun.RestrictResident(
		Fixture.OutOfSampleResidentID, FSimulationTime::FromDays(0), Error));

	const FV17IdentityRecord* IdentityAfter = ContinuityRun.FindIdentity(Fixture.OutOfSampleResidentID);
	const FV17ContinuityCapsule* Capsule = ContinuityRun.FindCapsule(Fixture.OutOfSampleResidentID);
	TestTrue(TEXT("The permanent identity is not regenerated by a zero-time round trip"),
		IdentityAfter != nullptr
			&& IdentityAfter->PersistentID == IdentityBefore.PersistentID
			&& IdentityAfter->NameSeed == IdentityBefore.NameSeed
			&& IdentityAfter->AppearanceSeed == IdentityBefore.AppearanceSeed
			&& IdentityAfter->HomeID == IdentityBefore.HomeID);
	TestTrue(TEXT("The first observation creates one small continuity record"), Capsule != nullptr);
	TestEqual(TEXT("The zero-time round trip restores all Coin and Credit"),
		TotalCellResource(ContinuityRun, Fixture, false), CoinBefore);
	TestEqual(TEXT("The zero-time round trip restores all Wood"),
		TotalCellResource(ContinuityRun, Fixture, true), WoodBefore);
	TestEqual(TEXT("The zero-time round trip does not change pending participant totals"),
		ContinuityRun.GetPendingParticipantCount(), PendingBefore);
	TestTrue(TEXT("The zero-time round trip leaves every hard check clean"),
		ContinuityRun.BuildAudit().IsHardErrorFree());

	TestTrue(TEXT("The cap test advances to the next day"), ContinuityRun.AdvanceTo(FSimulationTime::FromDays(1), Error));
	TArray<FResidentID> CapResidents;
	for (int32 Index = 0; Index < 50; ++Index)
	{
		const FResidentID ResidentID = Fixture.Identities[Index].ResidentID;
		CapResidents.Add(ResidentID);
		TestTrue(TEXT("Each of the first fifty residents can enter Active state"), ContinuityRun.LiftResident(
			ResidentID, FSimulationTime::FromDays(1), Error));
	}
	TestEqual(TEXT("Exactly fifty residents are Active at the cap"), ContinuityRun.GetActiveMicroCount(), 50);
	TestFalse(TEXT("The fifty-first Lift is rejected without changing authority"), ContinuityRun.LiftResident(
		Fixture.Identities[50].ResidentID, FSimulationTime::FromDays(1), Error));
	TestEqual(TEXT("A rejected fifty-first Lift does not exceed the cap"), ContinuityRun.GetActiveMicroCount(), 50);
	for (const FResidentID ResidentID : CapResidents)
	{
		TestTrue(TEXT("Cap-test residents can all return to Cohort state"), ContinuityRun.RestrictResident(
			ResidentID, FSimulationTime::FromDays(1), Error));
	}
	TestEqual(TEXT("The cap test returns every resident to Cohort state"), ContinuityRun.GetActiveMicroCount(), 0);
	TestEqual(TEXT("The cap test preserves all Coin and Credit"),
		TotalCellResource(ContinuityRun, Fixture, false), CoinBefore);
	TestEqual(TEXT("The cap test preserves all Wood"),
		TotalCellResource(ContinuityRun, Fixture, true), WoodBefore);
	TestTrue(TEXT("The cap test leaves identity, Capsule and resource checks clean"),
		ContinuityRun.BuildAudit().IsHardErrorFree());

	const FString ContinuityDigest = ContinuityRun.BuildDeterministicDigest();
	TestEqual(TEXT("The zero-time, rollback and cap checks have the frozen digest"), ContinuityDigest,
		FString(TEXT("E90151525DC4525270BC22091D6ED0BC5E96CE00")));
	AddInfo(FString::Printf(
		TEXT("Phase6GB4 fixed_trace=%s continuity=%s max_active=%d identities=%d capsules=%d transitions=%d"),
		*FixedTraceDigest,
		*ContinuityDigest,
		MaxActiveA,
		ContinuityRun.GetIdentityRegistry().Num(),
		ContinuityRun.GetCapsules().Num(),
		ContinuityRun.GetLODTransitions().Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB4RepairSplitMergeTest,
	"AILODResearch.Phase6G.V17RepairSplitMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB4RepairSplitMergeTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	constexpr FV17AuthoritativeCellID RepairCellID = 0xB480;
	TArray<FV17IdentityRecord> Identities;
	for (FResidentID ResidentID = 1; ResidentID <= 6; ++ResidentID)
	{
		FV17IdentityRecord Identity;
		Identity.ResidentID = ResidentID;
		Identity.PersistentID = 500000 + ResidentID;
		Identity.NameSeed = 600000 + static_cast<uint32>(ResidentID);
		Identity.AppearanceSeed = 700000 + static_cast<uint32>(ResidentID);
		Identity.HomeID = 800000 + ResidentID;
		Identity.InitialKingdom = EKingdom::A;
		Identity.Profession = EProfession::Worker;
		Identity.IncomeBand = EIncomeBand::NonLow;
		Identities.Add(Identity);
	}
	const TArray<FV17AuthoritativeCellConfig> Cells =
	{
		{ RepairCellID, MakeKey(
			EKingdom::A,
			EProfession::Worker,
			EIncomeBand::NonLow,
			EHomeState::DamagedWaiting,
			0,
			2), 6, 0, 0, 24 }
	};
	const TArray<FV17AuthoritativeKingdomConfig> Kingdoms =
	{
		{ EKingdom::A, 0, 0, 0, 0, 0, 0, 6, 1.0 }
	};
	FV17AuthoritativeMacroSession Session(20260810);
	FString Error;
	if (!Session.InitializeWithIdentity(Cells, Identities, Kingdoms, FSimulationTime::FromHours(0), Error))
	{
		AddError(FString::Printf(TEXT("B4 repair fixture initialization failed: %s"), *Error));
		return false;
	}

	TestTrue(TEXT("Resident 1 enters detailed simulation before repair planning"),
		Session.LiftResident(1, FSimulationTime::FromHours(0), Error));
	FV17AuthoritativeClaimID MacroClaimID = 0;
	FV17AuthoritativeClaimID ActiveClaimID = 0;
	TestTrue(TEXT("Five off-screen residents request one repair batch"), Session.QueueMacroAction(
		RepairCellID, EIndividualAction::StartRepair, 5, 0, MacroClaimID, Error));
	TestTrue(TEXT("The nearby resident requests repair through the same competition"), Session.QueueActiveAction(
		1, EIndividualAction::StartRepair, 0, ActiveClaimID, Error));
	TestTrue(TEXT("All six repairs are committed together"), Session.ResolveAndCommitClaims(Error));
	for (FResidentID ResidentID = 1; ResidentID <= 6; ++ResidentID)
	{
		EHomeState ExactHomeState = EHomeState::Healthy;
		TestTrue(*FString::Printf(TEXT("Repair resident %lld keeps an exact HomeID state"), ResidentID),
			Session.GetResidentHomeState(ResidentID, ExactHomeState));
		TestEqual(*FString::Printf(TEXT("Repair resident %lld is marked UnderRepair"), ResidentID),
			ExactHomeState, EHomeState::UnderRepair);
	}
	TestEqual(TEXT("Repair start updates exactly six concrete homes"),
		Session.GetHomeStateUpdateCount(), int64(6));
	TestEqual(TEXT("The two repair requests represent six pending residents"), Session.GetPendingParticipantCount(), 6);
	TestEqual(TEXT("Before Restrict there is one macro event and one personal event"), Session.GetBatchEvents().Num(), 2);
	TestEqual(TEXT("Repair Wood is deducted exactly once at action start"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs")), int64(24));

	TestTrue(TEXT("The nearby resident can leave while repair is unfinished"),
		Session.RestrictResident(1, FSimulationTime::FromHours(0), Error));
	const FV17ParticipantRef* InitialRef = Session.FindParticipantRef(1);
	TestTrue(TEXT("One small reference remembers which batch contains that resident"), InitialRef != nullptr);
	TestEqual(TEXT("The personal event merges back into the compatible repair batch"), Session.GetBatchEvents().Num(), 1);
	TestEqual(TEXT("The merged repair keeps all six participants"), Session.GetPendingParticipantCount(), 6);
	TestEqual(TEXT("Merging does not deduct repair Wood again"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs")), int64(24));
	TestTrue(TEXT("The merged repair state passes every hard check"), Session.BuildAudit().IsHardErrorFree());

	TestTrue(TEXT("Repair advances to its midpoint"), Session.AdvanceTo(FSimulationTime::FromHours(24), Error));
	const FString BeforeFailedSplit = Session.BuildDeterministicDigest();
	TestFalse(TEXT("A forced split failure is reported"), Session.LiftResident(
		1,
		FSimulationTime::FromHours(24),
		Error,
		EV17LODTransitionFailurePoint::LiftAfterEventSplit));
	TestEqual(TEXT("A failed split restores batch count, progress, resources and reference"),
		Session.BuildDeterministicDigest(), BeforeFailedSplit);
	TestTrue(TEXT("The first midpoint Lift succeeds after rollback"),
		Session.LiftResident(1, FSimulationTime::FromHours(24), Error));
	TestEqual(TEXT("Splitting one resident still represents six pending people"), Session.GetPendingParticipantCount(), 6);
	TestEqual(TEXT("The lifted resident keeps exactly twenty-four hours of repair work"),
		Session.GetRemainingWorkMinutes(1), int64(24 * MinutesPerHour));
	TestEqual(TEXT("One parent batch and one personal child event now exist"), Session.GetBatchEvents().Num(), 2);
	TestEqual(TEXT("Splitting does not deduct repair Wood again"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs")), int64(24));

	const FString BeforeFailedMerge = Session.BuildDeterministicDigest();
	TestFalse(TEXT("A forced merge failure is reported"), Session.RestrictResident(
		1,
		FSimulationTime::FromHours(24),
		Error,
		EV17LODTransitionFailurePoint::RestrictAfterEventMerge));
	TestEqual(TEXT("A failed merge restores the personal event and all progress"),
		Session.BuildDeterministicDigest(), BeforeFailedMerge);
	TestTrue(TEXT("The first midpoint Restrict succeeds after rollback"),
		Session.RestrictResident(1, FSimulationTime::FromHours(24), Error));
	TestEqual(TEXT("The child merges back to one repair batch"), Session.GetBatchEvents().Num(), 1);
	TestEqual(TEXT("The first round trip keeps six pending participants"), Session.GetPendingParticipantCount(), 6);
	TestTrue(TEXT("The first repair round trip leaves every hard check clean"), Session.BuildAudit().IsHardErrorFree());

	TestTrue(TEXT("Repair advances six more hours"), Session.AdvanceTo(FSimulationTime::FromHours(30), Error));
	TestTrue(TEXT("The second midpoint Lift succeeds"), Session.LiftResident(
		1, FSimulationTime::FromHours(30), Error));
	TestEqual(TEXT("The second Lift keeps exactly eighteen hours of repair work"),
		Session.GetRemainingWorkMinutes(1), int64(18 * MinutesPerHour));
	TestTrue(TEXT("The second midpoint Restrict succeeds"), Session.RestrictResident(
		1, FSimulationTime::FromHours(30), Error));
	TestEqual(TEXT("The second round trip returns to one batch"), Session.GetBatchEvents().Num(), 1);
	TestEqual(TEXT("The second round trip keeps all six participants and original completion time"),
		Session.GetPendingParticipantCount(), 6);
	TestEqual(TEXT("Two repair round trips still hold only the original twenty-four Wood"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs")), int64(24));
	TestTrue(TEXT("The second repair round trip leaves every hard check clean"), Session.BuildAudit().IsHardErrorFree());

	TestTrue(TEXT("The batch completes at the original forty-eight-hour deadline"),
		Session.AdvanceTo(FSimulationTime::FromHours(48), Error));
	TestEqual(TEXT("No repair participant remains pending"), Session.GetPendingParticipantCount(), 0);
	TestEqual(TEXT("No repair Wood remains stuck in unfinished work"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs")), int64(0));
	TestEqual(TEXT("Exactly twenty-four Wood reaches repaired homes"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodInRepairedHomes")), int64(24));
	for (FResidentID ResidentID = 1; ResidentID <= 6; ++ResidentID)
	{
		EHomeState ExactHomeState = EHomeState::Healthy;
		TestTrue(*FString::Printf(TEXT("Completed resident %lld still has a HomeID state"), ResidentID),
			Session.GetResidentHomeState(ResidentID, ExactHomeState));
		TestEqual(*FString::Printf(TEXT("Completed resident %lld is marked Repaired"), ResidentID),
			ExactHomeState, EHomeState::Repaired);
	}
	TestEqual(TEXT("Repair start and completion perform twelve concrete home updates"),
		Session.GetHomeStateUpdateCount(), int64(12));
	TestTrue(TEXT("The completed resident no longer needs a pending-batch reference"), Session.FindParticipantRef(1) == nullptr);
	const FV17ContinuityCapsule* CompletedCapsule = Session.FindCapsule(1);
	TestTrue(TEXT("The resident's small memory records the completed repair"),
		CompletedCapsule != nullptr
			&& CompletedCapsule->BatchCursor == 0
			&& CompletedCapsule->KnownCompletedActions.Contains(EIndividualAction::ContinueRepair));

	TestTrue(TEXT("The resident can be reconstructed after the off-screen repair completes"),
		Session.LiftResident(1, FSimulationTime::FromHours(48), Error));
	FIndividualActionState ReconstructedState;
	EIndividualAction ReconstructedAction = EIndividualAction::None;
	FEventID ReconstructedEventID = 0;
	TestTrue(TEXT("The reconstructed detailed state is available"), Session.GetActiveSnapshot(
		1, ReconstructedState, ReconstructedAction, ReconstructedEventID));
	TestEqual(TEXT("The reconstructed home is still repaired"), ReconstructedState.HomeState, EHomeState::Repaired);
	TestEqual(TEXT("The completed repair is not restarted"), ReconstructedAction, EIndividualAction::None);
	TestEqual(TEXT("The completed repair has no pending personal event"), ReconstructedEventID, FEventID(0));
	TestTrue(TEXT("The resident can return to the repaired Cohort"),
		Session.RestrictResident(1, FSimulationTime::FromHours(48), Error));
	TestTrue(TEXT("Final identity, population, resource, progress and event checks are all clean"),
		Session.BuildAudit().IsHardErrorFree());

	const FString CompletedDigest = Session.BuildDeterministicDigest();
	TestEqual(TEXT("The repair split and merge run has the frozen digest"), CompletedDigest,
		FString(TEXT("40DA98D3CA9439791664A301BCF607CB6D43C5E1")));
	AddInfo(FString::Printf(
		TEXT("Phase6GB4 repair=%s events=%d transactions=%d capsules=%d refs=%d"),
		*CompletedDigest,
		Session.GetBatchEvents().Num(),
		Session.GetLedger().GetTransactions().Num(),
		Session.GetCapsules().Num(),
		Session.GetParticipantRefs().Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB4ReservationSplitMergeTest,
	"AILODResearch.Phase6G.V17ReservationSplitMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB4ReservationSplitMergeTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	constexpr FV17AuthoritativeCellID BuyCellID = 0xB490;
	TArray<FV17IdentityRecord> Identities;
	for (FResidentID ResidentID = 1; ResidentID <= 4; ++ResidentID)
	{
		FV17IdentityRecord Identity;
		Identity.ResidentID = ResidentID;
		Identity.PersistentID = 900000 + ResidentID;
		Identity.NameSeed = 910000 + static_cast<uint32>(ResidentID);
		Identity.AppearanceSeed = 920000 + static_cast<uint32>(ResidentID);
		Identity.HomeID = 930000 + ResidentID;
		Identity.InitialKingdom = EKingdom::A;
		Identity.Profession = EProfession::Worker;
		Identity.IncomeBand = EIncomeBand::Low;
		Identities.Add(Identity);
	}
	const TArray<FV17AuthoritativeCellConfig> Cells =
	{
		{ BuyCellID, MakeKey(
			EKingdom::A,
			EProfession::Worker,
			EIncomeBand::Low,
			EHomeState::DamagedWaiting,
			2,
			0), 4, 32, 0, 0 }
	};
	const TArray<FV17AuthoritativeKingdomConfig> Kingdoms =
	{
		{ EKingdom::A, 16, 0, 0, 0, 0, 0, 0, 1.0 }
	};
	FV17AuthoritativeMacroSession Session(20260810);
	FString Error;
	if (!Session.InitializeWithIdentity(Cells, Identities, Kingdoms, FSimulationTime::FromHours(0), Error))
	{
		AddError(FString::Printf(TEXT("B4 reservation fixture initialization failed: %s"), *Error));
		return false;
	}

	auto ActiveReservationQuantity = [&Session]()
	{
		double Quantity = 0.0;
		for (const TPair<FReservationID, FReservationRecord>& Pair : Session.GetReservations().GetReservations())
		{
			if (Pair.Value.State == EReservationState::Active) Quantity += Pair.Value.Request.Quantity;
		}
		return FMath::RoundToInt64(Quantity);
	};
	auto JointCellWood = [&Session]()
	{
		int64 Quantity = 0;
		for (const TPair<FResourceAccountKey, double>& Pair : Session.GetLedger().GetBalances())
		{
			if (Pair.Key.Resource == ESimulationResource::Wood
				&& Pair.Key.Account.StartsWith(TEXT("V17.JointCell."))
				&& Pair.Key.Account.EndsWith(TEXT(".Wood")))
			{
				Quantity += FMath::RoundToInt64(Pair.Value);
			}
		}
		return Quantity;
	};

	TestTrue(TEXT("One resident enters detailed simulation before the Wood purchase"),
		Session.LiftResident(1, FSimulationTime::FromHours(0), Error));
	FV17AuthoritativeClaimID MacroClaimID = 0;
	FV17AuthoritativeClaimID ActiveClaimID = 0;
	TestTrue(TEXT("Three off-screen residents request one purchase batch"), Session.QueueMacroAction(
		BuyCellID, EIndividualAction::BuyWood, 3, 0, MacroClaimID, Error));
	TestTrue(TEXT("The nearby resident requests the same purchase through Count=1"), Session.QueueActiveAction(
		1, EIndividualAction::BuyWood, 0, ActiveClaimID, Error));
	TestTrue(TEXT("All four purchases reserve Wood in one competition"), Session.ResolveAndCommitClaims(Error));
	TestEqual(TEXT("All sixteen available Wood units are reserved"), ActiveReservationQuantity(), int64(16));
	TestEqual(TEXT("The reserved stock holds the same sixteen Wood units"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("MarketWoodReserved")), int64(16));

	TestTrue(TEXT("The nearby buyer can leave while delivery is pending"),
		Session.RestrictResident(1, FSimulationTime::FromHours(0), Error));
	TestEqual(TEXT("Merging the buyer keeps one live reservation with all sixteen Wood"),
		ActiveReservationQuantity(), int64(16));
	TestEqual(TEXT("Merging the buyer returns to one scheduled delivery"), Session.GetScheduler().NumPending(), 1);
	TestTrue(TEXT("The merged reservation passes every hard check"), Session.BuildAudit().IsHardErrorFree());

	TestTrue(TEXT("The delivery advances to its halfway point"),
		Session.AdvanceTo(FSimulationTime::FromMinutes(30), Error));
	TestTrue(TEXT("Lifting the buyer splits one four-Wood reservation"),
		Session.LiftResident(1, FSimulationTime::FromMinutes(30), Error));
	TestEqual(TEXT("Parent plus child reservations still total sixteen Wood"),
		ActiveReservationQuantity(), int64(16));
	TestEqual(TEXT("The split creates exactly two scheduled deliveries"), Session.GetScheduler().NumPending(), 2);
	TestEqual(TEXT("The split does not reserve or charge Wood twice"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("MarketWoodReserved")), int64(16));
	TestTrue(TEXT("The split reservation passes every hard check"), Session.BuildAudit().IsHardErrorFree());

	TestTrue(TEXT("Restrict merges the four-Wood child reservation back"),
		Session.RestrictResident(1, FSimulationTime::FromMinutes(30), Error));
	TestEqual(TEXT("The re-merged live reservation still totals sixteen Wood"),
		ActiveReservationQuantity(), int64(16));
	TestEqual(TEXT("The re-merged delivery returns to one scheduler entry"), Session.GetScheduler().NumPending(), 1);
	TestTrue(TEXT("The re-merged reservation passes every hard check"), Session.BuildAudit().IsHardErrorFree());

	TestTrue(TEXT("The purchase completes at the original one-hour deadline"),
		Session.AdvanceTo(FSimulationTime::FromHours(1), Error));
	TestEqual(TEXT("No live reservation remains after delivery"), ActiveReservationQuantity(), int64(0));
	TestEqual(TEXT("The reserved Market stock is empty after delivery"),
		Session.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("MarketWoodReserved")), int64(0));
	TestEqual(TEXT("The four residents receive exactly sixteen Wood"), JointCellWood(), int64(16));
	TestEqual(TEXT("All four purchase participants finish"), Session.GetPendingParticipantCount(), 0);
	TestTrue(TEXT("Final reservation, resource, population and event checks are clean"),
		Session.BuildAudit().IsHardErrorFree());

	const FString CompletedDigest = Session.BuildDeterministicDigest();
	TestEqual(TEXT("The reservation split and merge run has the frozen digest"), CompletedDigest,
		FString(TEXT("2DD02271B46A37AFC5545CB78A0053A89BBDA9C6")));
	AddInfo(FString::Printf(
		TEXT("Phase6GB4 reservation=%s reservations=%d transactions=%d events=%d"),
		*CompletedDigest,
		Session.GetReservations().GetReservations().Num(),
		Session.GetLedger().GetTransactions().Num(),
		Session.GetBatchEvents().Num()));
	return true;
}

#endif
