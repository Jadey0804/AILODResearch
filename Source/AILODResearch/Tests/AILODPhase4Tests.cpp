// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODDomainRules.h"
#include "../Simulation/AILODStatePreservingLOD.h"

namespace
{
	bool IsContinuitySampleResident(
		const AILOD::FPersistentTestPool& Sample,
		const AILOD::FResidentID ResidentID)
	{
		return Sample.Residents.ContainsByPredicate([ResidentID](const AILOD::FPersistentTestRecord& Record)
		{
			return Record.ResidentID == ResidentID;
		});
	}

	AILOD::FResidentID FindResidentOutsideContinuitySample(
		const AILOD::FStatePreservingLODSystem& System,
		const int32 TotalPopulation)
	{
		for (AILOD::FResidentID ResidentID = 1; ResidentID <= TotalPopulation; ++ResidentID)
		{
			if (!IsContinuitySampleResident(System.GetContinuitySample(), ResidentID))
			{
				return ResidentID;
			}
		}
		return 0;
	}

	AILOD::FResidentID FindDamagedResidentOutsideContinuitySample(
		const AILOD::FStatePreservingLODSystem& System,
		const int32 TotalPopulation)
	{
		for (AILOD::FResidentID ResidentID = 1; ResidentID <= TotalPopulation; ++ResidentID)
		{
			const AILOD::FResidentCoreState* Resident = System.FindResident(ResidentID);
			if (Resident != nullptr
				&& Resident->HomeState == AILOD::EHomeState::DamagedWaiting
				&& !IsContinuitySampleResident(System.GetContinuitySample(), ResidentID))
			{
				return ResidentID;
			}
		}
		return 0;
	}

	TArray<AILOD::FResidentID> FindDamagedResidents(
		const AILOD::FStatePreservingLODSystem& System,
		const int32 TotalPopulation,
		const int32 Count)
	{
		TArray<AILOD::FResidentID> ResidentIDs;
		for (AILOD::FResidentID ResidentID = 1;
			ResidentID <= TotalPopulation && ResidentIDs.Num() < Count;
			++ResidentID)
		{
			const AILOD::FResidentCoreState* Resident = System.FindResident(ResidentID);
			if (Resident != nullptr && Resident->HomeState == AILOD::EHomeState::DamagedWaiting)
			{
				ResidentIDs.Add(ResidentID);
			}
		}
		return ResidentIDs;
	}

	int32 CountLedgerTransactionsWithPrefix(
		const AILOD::FStatePreservingLODSystem& System,
		const TCHAR* Prefix)
	{
		int32 Count = 0;
		for (const AILOD::FLedgerTransaction& Transaction : System.GetLedger().GetTransactions())
		{
			Count += Transaction.Transfer.IdempotencyKey.StartsWith(Prefix) ? 1 : 0;
		}
		return Count;
	}

	TArray<AILOD::FResidentID> GetContinuityCheckpointResidents(
		const AILOD::FPersistentTestPool& Sample,
		const int32 Day)
	{
		TArray<AILOD::FResidentID> ResidentIDs;
		for (const AILOD::FPersistentTestRecord& Record : Sample.Residents)
		{
			const bool bIncluded = Day == 7
				? Record.bDay7
				: Day == 30
					? Record.bDay30
					: Record.bDay45;
			if (bIncluded)
			{
				ResidentIDs.Add(Record.ResidentID);
			}
		}
		ResidentIDs.Sort();
		return ResidentIDs;
	}

	TArray<AILOD::FResidentID> GetActiveResidents(
		const AILOD::FStatePreservingLODSystem& System,
		const int32 TotalPopulation)
	{
		TArray<AILOD::FResidentID> ResidentIDs;
		for (AILOD::FResidentID ResidentID = 1; ResidentID <= TotalPopulation; ++ResidentID)
		{
			const AILOD::FResidentCoreState* Resident = System.FindResident(ResidentID);
			if (Resident != nullptr
				&& Resident->Representation == AILOD::EResidentRepresentation::ActiveMicro)
			{
				ResidentIDs.Add(ResidentID);
			}
		}
		return ResidentIDs;
	}

	bool SetResidentsActive(
		AILOD::FStatePreservingLODSystem& System,
		const TArray<AILOD::FResidentID>& ResidentIDs,
		const AILOD::FSimulationTime Time,
		const bool bActive,
		FString& OutError)
	{
		for (const AILOD::FResidentID ResidentID : ResidentIDs)
		{
			const bool bSucceeded = bActive
				? System.Activate(ResidentID, Time, OutError)
				: System.Deactivate(ResidentID, Time, OutError);
			if (!bSucceeded)
			{
				return false;
			}
		}
		return true;
	}

	bool InitializeDefaultSystem(
		AILOD::FStatePreservingLODSystem& OutSystem,
		FString& OutError)
	{
		AILOD::FPhase0Config Config;
		Config.Seed = 20260810;
		Config.PopulationPerKingdom = 100;
		return OutSystem.Initialize(Config, OutError);
	}

	bool RunFormalContinuityTrace(
		AILOD::FStatePreservingLODSystem& System,
		FString& OutError)
	{
		using namespace AILOD;
		if (!InitializeDefaultSystem(System, OutError))
		{
			return false;
		}
		const TArray<FResidentID> Day7Residents = GetContinuityCheckpointResidents(System.GetContinuitySample(), 7);
		const TArray<FResidentID> Day30Residents = GetContinuityCheckpointResidents(System.GetContinuitySample(), 30);
		const TArray<FResidentID> Day45Residents = GetContinuityCheckpointResidents(System.GetContinuitySample(), 45);
		return SetResidentsActive(System, Day7Residents, FSimulationTime::FromDays(7), true, OutError)
			&& SetResidentsActive(System, Day7Residents, FSimulationTime::FromDays(8), false, OutError)
			&& SetResidentsActive(System, Day30Residents, FSimulationTime::FromDays(30), true, OutError)
			&& SetResidentsActive(System, Day30Residents, FSimulationTime::FromDays(31), false, OutError)
			&& SetResidentsActive(System, Day45Residents, FSimulationTime::FromDays(45), true, OutError)
			&& SetResidentsActive(System, Day45Residents, FSimulationTime::FromDays(46), false, OutError);
	}

	bool RunDeterministicTrace(
		AILOD::FStatePreservingLODSystem& System,
		FString& OutDigest,
		FString& OutError)
	{
		using namespace AILOD;
		if (!InitializeDefaultSystem(System, OutError)
			|| !System.ApplyEarthquakeDamage(FSimulationTime::FromDays(0), OutError)
			|| !System.Activate(1, FSimulationTime::FromDays(7), OutError)
			|| !System.Activate(2, FSimulationTime::FromDays(7), OutError)
			|| !System.Deactivate(1, FSimulationTime::FromDays(7), OutError))
		{
			return false;
		}

		TArray<FUnifiedActionRequest> Requests =
		{
			{ 4, EIndividualAction::BuyWood, 2, EResidentRepresentation::CohortManaged },
			{ 1, EIndividualAction::BuyWood, 2, EResidentRepresentation::CohortManaged },
			{ 3, EIndividualAction::BuyWood, 2, EResidentRepresentation::CohortManaged }
		};
		if (!System.ResolveCompetition(
			FSimulationTime::FromDays(8),
			4,
			Requests,
			OutError))
		{
			return false;
		}

		OutDigest = System.BuildDeterministicDigest();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase4DynamicActivationTest,
	"AILODResearch.Phase4.DynamicActivationAndCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase4DynamicActivationTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FStatePreservingLODSystem System;
	FString Error;
	if (!InitializeDefaultSystem(System, Error))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Continuity sample remains fixed at 20 residents"), System.GetContinuitySample().Residents.Num(), 20);
	TMap<FResidentID, FResidentCoreState> InitialStates;
	for (FResidentID ResidentID = 1; ResidentID <= 200; ++ResidentID)
	{
		const FResidentCoreState* Resident = System.FindResident(ResidentID);
		if (Resident != nullptr)
		{
			InitialStates.Add(ResidentID, *Resident);
		}
	}
	TestEqual(TEXT("All 200 initial strong-state snapshots are captured"), InitialStates.Num(), 200);
	const FResidentID OutsideSampleID = FindResidentOutsideContinuitySample(System, 200);
	TestTrue(TEXT("A resident outside the formal continuity sample exists"), OutsideSampleID > 0);
	const FResidentCoreState* Before = System.FindResident(OutsideSampleID);
	if (Before == nullptr)
	{
		AddError(TEXT("The selected resident does not exist."));
		return false;
	}

	const FPersistentID StablePersistentID = Before->PersistentID;
	const FHomeID StableHomeID = Before->HomeID;
	const FString StableName = Before->Name;
	const int32 StableCash = Before->Cash;
	const int32 StableCredit = Before->RepairCredit;
	const int32 StableWood = Before->InventoryWood;

	const FSimulationTime IdentityCheckTime = FSimulationTime::FromDays(7);
	TestTrue(TEXT("A sample-external resident can activate"), System.Activate(OutsideSampleID, IdentityCheckTime, Error));
	TestTrue(TEXT("The sample-external resident can return to CohortManaged"), System.Deactivate(OutsideSampleID, IdentityCheckTime, Error));
	const FResidentCoreState* AfterRoundTrip = System.FindResident(OutsideSampleID);
	if (AfterRoundTrip != nullptr)
	{
		TestEqual(TEXT("PersistentID survives a sample-external round trip"), AfterRoundTrip->PersistentID, StablePersistentID);
		TestEqual(TEXT("HomeID survives a sample-external round trip"), AfterRoundTrip->HomeID, StableHomeID);
		TestEqual(TEXT("Name survives a sample-external round trip"), AfterRoundTrip->Name, StableName);
		TestEqual(TEXT("Cash survives a transition with no resource event"), AfterRoundTrip->Cash, StableCash);
		TestEqual(TEXT("RepairCredit survives a transition with no resource event"), AfterRoundTrip->RepairCredit, StableCredit);
		TestEqual(TEXT("Wood survives a transition with no resource event"), AfterRoundTrip->InventoryWood, StableWood);
	}

	TSet<FResidentID> RoundTrippedResidents;
	const FSimulationTime BatchRoundTripTime = FSimulationTime::FromDays(8);
	for (FResidentID BatchStart = 1; BatchStart <= 200; BatchStart += ActiveMicroCap)
	{
		const FResidentID BatchEnd = FMath::Min<FResidentID>(BatchStart + ActiveMicroCap - 1, 200);
		for (FResidentID ResidentID = BatchStart; ResidentID <= BatchEnd; ++ResidentID)
		{
			TestTrue(
				FString::Printf(TEXT("Resident %lld activates in its capped batch"), ResidentID),
				System.Activate(ResidentID, BatchRoundTripTime, Error));
			RoundTrippedResidents.Add(ResidentID);
		}
		TestTrue(TEXT("No activation batch exceeds 50 simultaneous residents"), System.GetActiveMicroCount() <= ActiveMicroCap);
		for (FResidentID ResidentID = BatchStart; ResidentID <= BatchEnd; ++ResidentID)
		{
			TestTrue(
				FString::Printf(TEXT("Resident %lld deactivates at the same timestamp"), ResidentID),
				System.Deactivate(ResidentID, BatchRoundTripTime, Error));
			const FResidentCoreState* Actual = System.FindResident(ResidentID);
			const FResidentCoreState* Expected = InitialStates.Find(ResidentID);
			if (Actual == nullptr || Expected == nullptr)
			{
				AddError(FString::Printf(TEXT("Resident %lld cannot be compared after its round trip."), ResidentID));
				continue;
			}
			TestEqual(TEXT("Round trip preserves ResidentID"), Actual->ResidentID, Expected->ResidentID);
			TestEqual(TEXT("Round trip preserves HomeID"), Actual->HomeID, Expected->HomeID);
			TestEqual(TEXT("Round trip preserves PersistentID"), Actual->PersistentID, Expected->PersistentID);
			TestEqual(TEXT("Round trip preserves Name"), Actual->Name, Expected->Name);
			TestEqual(TEXT("Round trip preserves Kingdom"), Actual->Kingdom, Expected->Kingdom);
			TestEqual(TEXT("Round trip preserves Profession"), Actual->Profession, Expected->Profession);
			TestEqual(TEXT("Round trip preserves IncomeBand"), Actual->IncomeBand, Expected->IncomeBand);
			TestEqual(TEXT("Round trip preserves Cash"), Actual->Cash, Expected->Cash);
			TestEqual(TEXT("Round trip preserves RepairCredit"), Actual->RepairCredit, Expected->RepairCredit);
			TestEqual(TEXT("Round trip preserves InventoryWood"), Actual->InventoryWood, Expected->InventoryWood);
			TestEqual(TEXT("Round trip preserves HomeState"), Actual->HomeState, Expected->HomeState);
			TestEqual(TEXT("Round trip preserves CurrentGoal"), Actual->CurrentGoal, Expected->CurrentGoal);
			TestEqual(TEXT("Round trip preserves CurrentAction"), Actual->CurrentAction, Expected->CurrentAction);
			TestEqual(TEXT("Round trip preserves LastCompletedAction"), Actual->LastCompletedAction, Expected->LastCompletedAction);
			TestEqual(TEXT("Round trip preserves MacroIntent"), Actual->MacroIntent, Expected->MacroIntent);
			TestEqual(TEXT("Round trip preserves EventID"), Actual->ActiveEventID, Expected->ActiveEventID);
			TestEqual(TEXT("Round trip preserves ParentEventID"), Actual->ParentEventID, Expected->ParentEventID);
			TestEqual(TEXT("Round trip preserves ArriveID"), Actual->ActiveArriveID, Expected->ActiveArriveID);
			TestEqual(TEXT("Round trip preserves ReservationID"), Actual->ActiveReservationID, Expected->ActiveReservationID);
			TestEqual(TEXT("Round trip preserves CausalPolicyID"), Actual->CausalPolicyID, Expected->CausalPolicyID);
			TestEqual(TEXT("Round trip preserves ActionStartTime"), Actual->ActionStartTime.Minutes, Expected->ActionStartTime.Minutes);
			TestEqual(TEXT("Round trip preserves ActionEndTime"), Actual->ActionEndTime.Minutes, Expected->ActionEndTime.Minutes);
			TestEqual(TEXT("Round trip preserves LastUpdateTime"), Actual->LastUpdateTime.Minutes, Expected->LastUpdateTime.Minutes);
			TestEqual(TEXT("Round trip preserves LocationAnchor"), Actual->LocationAnchor, Expected->LocationAnchor);
			TestEqual(TEXT("Round trip preserves RNGStreamKey"), Actual->RNGStreamKey, Expected->RNGStreamKey);
			TestEqual(TEXT("Round trip preserves Version"), Actual->Version, Expected->Version);
			TestEqual(TEXT("Round trip preserves AidReceived"), Actual->bAidReceived, Expected->bAidReceived);
			TestEqual(TEXT("Round trip returns to CohortManaged"), Actual->Representation, EResidentRepresentation::CohortManaged);
		}
		TestEqual(TEXT("Each completed batch returns every resident to CohortManaged"), System.GetActiveMicroCount(), 0);
	}
	TestEqual(TEXT("All 200 residents complete an Activate-Deactivate round trip"), RoundTrippedResidents.Num(), 200);
	TestTrue(TEXT("The round-tripped population exceeds the 20-person formal sample"), RoundTrippedResidents.Num() > System.GetContinuitySample().Residents.Num());
	TestTrue(TEXT("All four batches leave the canonical and Cohort states consistent"), System.Audit().IsHardErrorFree());

	const FSimulationTime ConcurrentCapTime = FSimulationTime::FromDays(9);
	for (FResidentID ResidentID = 1; ResidentID <= ActiveMicroCap; ++ResidentID)
	{
		TestTrue(
			FString::Printf(TEXT("Resident %lld activates for the concurrent-cap check"), ResidentID),
			System.Activate(ResidentID, ConcurrentCapTime, Error));
	}
	TestEqual(TEXT("Exactly 50 residents can be ActiveMicro simultaneously"), System.GetActiveMicroCount(), ActiveMicroCap);

	TestFalse(
		TEXT("The 51st simultaneous activation is rejected"),
		System.Activate(ActiveMicroCap + 1, ConcurrentCapTime, Error));
	TestEqual(TEXT("A rejected activation does not exceed the cap"), System.GetActiveMicroCount(), ActiveMicroCap);
	TestTrue(TEXT("The rejected activation is recorded"), System.GetTransitions().Num() > 0);
	if (System.GetTransitions().Num() > 0)
	{
		TestEqual(
			TEXT("The rejected activation records ActiveCapReached"),
			System.GetTransitions().Last().Result,
			ELODTransitionResult::ActiveCapReached);
	}

	TestTrue(TEXT("A slot can be released"), System.Deactivate(1, ConcurrentCapTime, Error));
	TestTrue(TEXT("A new resident can use the released slot"), System.Activate(ActiveMicroCap + 1, ConcurrentCapTime, Error));
	TestEqual(TEXT("The simultaneous count remains capped after replacement"), System.GetActiveMicroCount(), ActiveMicroCap);

	const FPopulationState Population = System.BuildPopulationState();
	TestEqual(TEXT("Population remains 200"), Population.Total, 200);
	TestEqual(TEXT("ActiveMicro population is 50"), Population.ActiveMicro, ActiveMicroCap);
	TestEqual(TEXT("All other residents remain CohortManaged"), Population.PersistentMacro, 200 - ActiveMicroCap);
	TestTrue(TEXT("Dynamic activation leaves all hard audits clean"), System.Audit().IsHardErrorFree());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase4FormalContinuityTraceTest,
	"AILODResearch.Phase4.FormalContinuityTrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase4FormalContinuityTraceTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FStatePreservingLODSystem RunA;
	FString Error;
	if (!InitializeDefaultSystem(RunA, Error))
	{
		AddError(Error);
		return false;
	}

	const FPersistentTestPool& Sample = RunA.GetContinuitySample();
	const TArray<FResidentID> Day7Residents = GetContinuityCheckpointResidents(Sample, 7);
	const TArray<FResidentID> Day30Residents = GetContinuityCheckpointResidents(Sample, 30);
	const TArray<FResidentID> Day45Residents = GetContinuityCheckpointResidents(Sample, 45);
	TestEqual(TEXT("Day 7 activates the 10 flagged continuity residents"), Day7Residents.Num(), 10);
	TestEqual(TEXT("Day 30 activates the 10 flagged continuity residents"), Day30Residents.Num(), 10);
	TestEqual(TEXT("Day 45 activates all 20 continuity residents"), Day45Residents.Num(), 20);
	TestTrue(TEXT("Day 7 and Day 30 contain exactly the same ResidentIDs"), Day7Residents == Day30Residents);

	struct FIdentitySnapshot
	{
		FPersistentID PersistentID = 0;
		FHomeID HomeID = 0;
		FString Name;
		EKingdom Kingdom = EKingdom::A;
		EProfession Profession = EProfession::Worker;
		EIncomeBand IncomeBand = EIncomeBand::Low;
	};
	TMap<FResidentID, FIdentitySnapshot> InitialIdentities;
	for (const FResidentID ResidentID : Day45Residents)
	{
		const FResidentCoreState* Resident = RunA.FindResident(ResidentID);
		if (Resident == nullptr)
		{
			AddError(FString::Printf(TEXT("Continuity resident %lld is missing."), ResidentID));
			continue;
		}
		InitialIdentities.Add(ResidentID, {
			Resident->PersistentID,
			Resident->HomeID,
			Resident->Name,
			Resident->Kingdom,
			Resident->Profession,
			Resident->IncomeBand });
	}

	auto AssertCheckpoint = [this, &RunA, &InitialIdentities](
		const TCHAR* CheckpointName,
		const TArray<FResidentID>& ExpectedResidents)
	{
		const TArray<FResidentID> ActualResidents = GetActiveResidents(RunA, 200);
		TestEqual(
			FString::Printf(TEXT("%s ActiveMicro count matches the sample flags"), CheckpointName),
			ActualResidents.Num(),
			ExpectedResidents.Num());
		TestTrue(
			FString::Printf(TEXT("%s ActiveMicro membership matches the sample flags"), CheckpointName),
			ActualResidents == ExpectedResidents);
		TestTrue(
			FString::Printf(TEXT("%s remains within the ActiveMicro cap"), CheckpointName),
			RunA.GetActiveMicroCount() <= ActiveMicroCap);

		for (const FResidentID ResidentID : ExpectedResidents)
		{
			const FResidentCoreState* Resident = RunA.FindResident(ResidentID);
			const FIdentitySnapshot* Initial = InitialIdentities.Find(ResidentID);
			if (Resident == nullptr || Initial == nullptr)
			{
				AddError(FString::Printf(TEXT("%s cannot resolve identity for resident %lld."), CheckpointName, ResidentID));
				continue;
			}
			TestEqual(TEXT("Checkpoint preserves PersistentID"), Resident->PersistentID, Initial->PersistentID);
			TestEqual(TEXT("Checkpoint preserves HomeID"), Resident->HomeID, Initial->HomeID);
			TestEqual(TEXT("Checkpoint preserves Name"), Resident->Name, Initial->Name);
			TestEqual(TEXT("Checkpoint preserves Kingdom"), Resident->Kingdom, Initial->Kingdom);
			TestEqual(TEXT("Checkpoint preserves Profession"), Resident->Profession, Initial->Profession);
			TestEqual(TEXT("Checkpoint preserves IncomeBand"), Resident->IncomeBand, Initial->IncomeBand);
		}
	};

	TestTrue(TEXT("Day 7 formal activation succeeds"), SetResidentsActive(RunA, Day7Residents, FSimulationTime::FromDays(7), true, Error));
	AssertCheckpoint(TEXT("Day 7"), Day7Residents);
	TestTrue(TEXT("Day 8 formal deactivation succeeds"), SetResidentsActive(RunA, Day7Residents, FSimulationTime::FromDays(8), false, Error));
	TestEqual(TEXT("Day 8 returns every formal resident to CohortManaged"), RunA.GetActiveMicroCount(), 0);

	TestTrue(TEXT("Day 30 formal activation succeeds"), SetResidentsActive(RunA, Day30Residents, FSimulationTime::FromDays(30), true, Error));
	AssertCheckpoint(TEXT("Day 30"), Day30Residents);
	TestTrue(TEXT("Day 31 formal deactivation succeeds"), SetResidentsActive(RunA, Day30Residents, FSimulationTime::FromDays(31), false, Error));
	TestEqual(TEXT("Day 31 returns every formal resident to CohortManaged"), RunA.GetActiveMicroCount(), 0);

	TestTrue(TEXT("Day 45 formal activation succeeds"), SetResidentsActive(RunA, Day45Residents, FSimulationTime::FromDays(45), true, Error));
	AssertCheckpoint(TEXT("Day 45"), Day45Residents);
	TestTrue(TEXT("Day 46 formal deactivation succeeds"), SetResidentsActive(RunA, Day45Residents, FSimulationTime::FromDays(46), false, Error));
	TestEqual(TEXT("Day 46 returns every formal resident to CohortManaged"), RunA.GetActiveMicroCount(), 0);
	TestTrue(TEXT("The completed formal trace has no hard error"), RunA.Audit().IsHardErrorFree());

	const TArray<FLODTransitionRecord>& TransitionsA = RunA.GetTransitions();
	TestEqual(TEXT("The formal trace records exactly 80 committed transitions"), TransitionsA.Num(), 80);
	for (int32 Index = 0; Index < TransitionsA.Num(); ++Index)
	{
		TestEqual(TEXT("Every formal transition commits"), TransitionsA[Index].Result, ELODTransitionResult::Committed);
		TestEqual(TEXT("Formal transition ArriveIDs are contiguous"), TransitionsA[Index].ArriveID, static_cast<FArriveID>(Index + 1));
		TestEqual(TEXT("Formal transitions commit at their requested time"), TransitionsA[Index].CommittedTime.Minutes, TransitionsA[Index].RequestedTime.Minutes);
	}

	FStatePreservingLODSystem RunB;
	TestTrue(TEXT("The same formal continuity trace runs a second time"), RunFormalContinuityTrace(RunB, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	const TArray<FLODTransitionRecord>& TransitionsB = RunB.GetTransitions();
	TestEqual(TEXT("Repeated formal trace has the same transition count"), TransitionsB.Num(), TransitionsA.Num());
	for (int32 Index = 0; Index < TransitionsA.Num() && Index < TransitionsB.Num(); ++Index)
	{
		TestEqual(TEXT("Repeated trace preserves transition resident"), TransitionsB[Index].PersistentID, TransitionsA[Index].PersistentID);
		TestEqual(TEXT("Repeated trace preserves transition source"), TransitionsB[Index].From, TransitionsA[Index].From);
		TestEqual(TEXT("Repeated trace preserves transition target"), TransitionsB[Index].To, TransitionsA[Index].To);
		TestEqual(TEXT("Repeated trace preserves transition ArriveID"), TransitionsB[Index].ArriveID, TransitionsA[Index].ArriveID);
		TestEqual(TEXT("Repeated trace preserves transition result"), TransitionsB[Index].Result, TransitionsA[Index].Result);
		TestEqual(TEXT("Repeated trace preserves transition time"), TransitionsB[Index].CommittedTime.Minutes, TransitionsA[Index].CommittedTime.Minutes);
	}
	TestEqual(TEXT("Repeated formal trace produces the same deterministic digest"), RunB.BuildDeterministicDigest(), RunA.BuildDeterministicDigest());
	TestTrue(TEXT("Repeated formal trace also has no hard error"), RunB.Audit().IsHardErrorFree());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase4RepairMidpointRoundTripTest,
	"AILODResearch.Phase4.RepairMidpointRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase4RepairMidpointRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::DomainRules;

	FStatePreservingLODSystem System;
	FString Error;
	if (!InitializeDefaultSystem(System, Error))
	{
		AddError(Error);
		return false;
	}

	const FSimulationTime RepairStartTime = FSimulationTime::FromDays(0);
	TestTrue(TEXT("Earthquake damage is applied"), System.ApplyEarthquakeDamage(RepairStartTime, Error));
	const FResidentID ResidentID = FindDamagedResidentOutsideContinuitySample(System, 200);
	TestTrue(TEXT("A damaged resident outside the formal sample is available"), ResidentID > 0);
	if (ResidentID <= 0)
	{
		return false;
	}

	const FResidentCoreState* InitialResident = System.FindResident(ResidentID);
	if (InitialResident == nullptr)
	{
		AddError(TEXT("The selected repair resident does not exist."));
		return false;
	}
	const EKingdom ResidentKingdom = InitialResident->Kingdom;
	const FString ResidentWoodAccount = FString::Printf(TEXT("Resident.%lld.Wood"), ResidentID);
	const FString EmbeddedWoodAccount = MakeKingdomAccount(ResidentKingdom, TEXT("WoodEmbeddedInRepairs"));
	const FString RepairedWoodAccount = MakeKingdomAccount(ResidentKingdom, TEXT("WoodInRepairedHomes"));

	TestTrue(
		TEXT("The test setup gives the resident exactly four Wood through the Ledger"),
		System.SeedResidentWoodForTest(
			ResidentID,
			static_cast<int32>(RepairWoodPerHome),
			RepairStartTime,
			Error));
	TestTrue(TEXT("The seeded CoreState and Cohort cache pass audit"), System.Audit().IsHardErrorFree());
	TestFalse(
		TEXT("Repair cannot bypass the unified competition queue"),
		System.StartRepair(ResidentID, RepairStartTime, Error, 0));
	TArray<FUnifiedActionRequest> RepairRequests =
	{
		{ ResidentID, EIndividualAction::StartRepair, 1, EResidentRepresentation::CohortManaged }
	};
	TestTrue(
		TEXT("The single Repair candidate is resolved through the unified queue"),
		System.ResolveCompetition(RepairStartTime, 1, RepairRequests, Error));
	TestTrue(TEXT("The single Repair candidate wins"), RepairRequests[0].bWon);
	TestTrue(TEXT("The Repair queue issues a non-zero ArriveID"), RepairRequests[0].ArriveID > 0);
	TestTrue(
		TEXT("The CohortManaged resident starts Repair with the winning request"),
		System.StartRepair(ResidentID, RepairStartTime, Error, RepairRequests[0].ArriveID));
	TestTrue(TEXT("Repair start leaves the derived Cohort cache exact"), System.Audit().IsHardErrorFree());

	const FResidentCoreState* StartedResident = System.FindResident(ResidentID);
	if (StartedResident == nullptr)
	{
		AddError(TEXT("The repair resident disappeared after starting Repair."));
		return false;
	}
	const FEventID StableEventID = StartedResident->ActiveEventID;
	const FArriveID StableArriveID = StartedResident->ActiveArriveID;
	const FSimulationTime StableEndTime = StartedResident->ActionEndTime;
	TestTrue(TEXT("Repair creates a stable EventID"), StableEventID > 0);
	TestTrue(TEXT("Repair creates a stable ArriveID"), StableArriveID > 0);
	TestEqual(TEXT("Repair duration is exactly two game days"), StableEndTime.Minutes - RepairStartTime.Minutes, 2 * MinutesPerDay);
	TestEqual(TEXT("Repair start consumes the resident's four Wood"), System.GetLedger().GetBalance(ESimulationResource::Wood, ResidentWoodAccount), 0.0);
	TestEqual(TEXT("Four Wood is held in the repair stock"), System.GetLedger().GetBalance(ESimulationResource::Wood, EmbeddedWoodAccount), RepairWoodPerHome);
	TestEqual(TEXT("Exactly one REPAIR-START transaction exists"), CountLedgerTransactionsWithPrefix(System, TEXT("REPAIR-START-")), 1);
	const int32 TransactionsBeforeTransitions = System.GetLedger().GetTransactions().Num();

	const FSimulationTime Midpoint = FSimulationTime::FromDays(1);
	TestTrue(TEXT("Simulation advances to the 50 percent repair midpoint"), System.AdvanceTo(Midpoint, Error));
	TestTrue(TEXT("Cohort cache remains exact when repair progress crosses bins"), System.Audit().IsHardErrorFree());
	TestEqual(TEXT("Exactly one game day remains at the midpoint"), System.GetRemainingWorkMinutes(ResidentID, Midpoint), MinutesPerDay);

	const FSimulationEventRecord* BeforeTransitionEvent = System.GetEventStore().Find(StableEventID);
	TestTrue(
		TEXT("The midpoint event is owned by the Cohort representation"),
		BeforeTransitionEvent != nullptr
			&& BeforeTransitionEvent->Event.Owner == FString::Printf(TEXT("Macro:%lld"), ResidentID));

	TestTrue(TEXT("The resident activates at the repair midpoint"), System.Activate(ResidentID, Midpoint, Error));
	const FSimulationEventRecord* ActiveEvent = System.GetEventStore().Find(StableEventID);
	TestTrue(
		TEXT("Activation transfers the same event to the ActiveMicro owner"),
		ActiveEvent != nullptr
			&& ActiveEvent->Event.Owner == FString::Printf(TEXT("Micro:%lld"), ResidentID));
	const FResidentCoreState* ActiveResident = System.FindResident(ResidentID);
	if (ActiveResident != nullptr)
	{
		TestEqual(TEXT("Activation preserves EventID"), ActiveResident->ActiveEventID, StableEventID);
		TestEqual(TEXT("Activation preserves ArriveID"), ActiveResident->ActiveArriveID, StableArriveID);
		TestEqual(TEXT("Activation preserves the repair end time"), ActiveResident->ActionEndTime.Minutes, StableEndTime.Minutes);
	}
	TestEqual(TEXT("Activation does not create a resource transaction"), System.GetLedger().GetTransactions().Num(), TransactionsBeforeTransitions);
	TestTrue(TEXT("Active midpoint state passes every audit"), System.Audit().IsHardErrorFree());

	TestTrue(TEXT("The resident returns to CohortManaged at the same midpoint"), System.Deactivate(ResidentID, Midpoint, Error));
	const FSimulationEventRecord* CohortEvent = System.GetEventStore().Find(StableEventID);
	TestTrue(
		TEXT("Deactivation transfers the same event back to the Cohort owner"),
		CohortEvent != nullptr
			&& CohortEvent->Event.Owner == FString::Printf(TEXT("Macro:%lld"), ResidentID));
	const FResidentCoreState* CohortResident = System.FindResident(ResidentID);
	if (CohortResident != nullptr)
	{
		TestEqual(TEXT("Deactivation preserves EventID"), CohortResident->ActiveEventID, StableEventID);
		TestEqual(TEXT("Deactivation preserves ArriveID"), CohortResident->ActiveArriveID, StableArriveID);
		TestEqual(TEXT("Deactivation preserves the remaining work"), System.GetRemainingWorkMinutes(ResidentID, Midpoint), MinutesPerDay);
	}
	TestEqual(TEXT("Both representation switches create no resource transaction"), System.GetLedger().GetTransactions().Num(), TransactionsBeforeTransitions);
	TestTrue(TEXT("Cohort midpoint state passes every audit after the round trip"), System.Audit().IsHardErrorFree());

	TestTrue(TEXT("Simulation advances to the original repair end time"), System.AdvanceTo(StableEndTime, Error));
	const FResidentCoreState* CompletedResident = System.FindResident(ResidentID);
	if (CompletedResident != nullptr)
	{
		TestEqual(TEXT("The resident reaches Repaired"), CompletedResident->HomeState, EHomeState::Repaired);
		TestEqual(TEXT("Completion clears the resident's active EventID"), CompletedResident->ActiveEventID, static_cast<FEventID>(0));
		TestEqual(TEXT("Completion records ContinueRepair once"), CompletedResident->LastCompletedAction, EIndividualAction::ContinueRepair);
	}
	const FSimulationEventRecord* CompletedEvent = System.GetEventStore().Find(StableEventID);
	TestTrue(
		TEXT("The original event completes instead of being replaced"),
		CompletedEvent != nullptr && CompletedEvent->State == ESimulationEventState::Completed);
	TestEqual(TEXT("Exactly one REPAIR-START transaction remains"), CountLedgerTransactionsWithPrefix(System, TEXT("REPAIR-START-")), 1);
	TestEqual(TEXT("Exactly one REPAIR-COMPLETE transaction exists"), CountLedgerTransactionsWithPrefix(System, TEXT("REPAIR-COMPLETE-")), 1);
	TestEqual(TEXT("No Wood remains duplicated in the embedded repair stock"), System.GetLedger().GetBalance(ESimulationResource::Wood, EmbeddedWoodAccount), 0.0);
	TestEqual(TEXT("Exactly four Wood reaches the repaired-home stock"), System.GetLedger().GetBalance(ESimulationResource::Wood, RepairedWoodAccount), RepairWoodPerHome);
	TestEqual(TEXT("The resident does not regain consumed Wood"), System.GetLedger().GetBalance(ESimulationResource::Wood, ResidentWoodAccount), 0.0);
	TestTrue(TEXT("Repair completion leaves all hard audits clean"), System.Audit().IsHardErrorFree());

	const int32 TransactionsAfterCompletion = System.GetLedger().GetTransactions().Num();
	TestTrue(TEXT("Advancing to the same time again is harmless"), System.AdvanceTo(StableEndTime, Error));
	TestEqual(TEXT("Repeated advancement cannot complete the repair twice"), System.GetLedger().GetTransactions().Num(), TransactionsAfterCompletion);
	TestEqual(TEXT("Repeated advancement keeps one completion transaction"), CountLedgerTransactionsWithPrefix(System, TEXT("REPAIR-COMPLETE-")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase4UnifiedCompetitionTest,
	"AILODResearch.Phase4.UnifiedCompetition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase4UnifiedCompetitionTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::DomainRules;

	FStatePreservingLODSystem RunA;
	FStatePreservingLODSystem RunB;
	FString Error;
	if (!InitializeDefaultSystem(RunA, Error) || !InitializeDefaultSystem(RunB, Error))
	{
		AddError(Error);
		return false;
	}

	TestTrue(TEXT("Run A applies the shared earthquake"), RunA.ApplyEarthquakeDamage(FSimulationTime::FromDays(0), Error));
	TestTrue(TEXT("Run B applies the shared earthquake"), RunB.ApplyEarthquakeDamage(FSimulationTime::FromDays(0), Error));
	const TArray<FResidentID> CandidateIDs = FindDamagedResidents(RunA, 200, 6);
	TestEqual(TEXT("Six damaged residents are available for Repair Capacity competition"), CandidateIDs.Num(), 6);
	if (CandidateIDs.Num() != 6)
	{
		return false;
	}

	const FSimulationTime SetupTime = FSimulationTime::FromDays(7);
	TestTrue(TEXT("Run A activates its first mixed candidate"), RunA.Activate(CandidateIDs[0], SetupTime, Error));
	TestTrue(TEXT("Run A activates its second mixed candidate"), RunA.Activate(CandidateIDs[1], SetupTime, Error));
	TestTrue(TEXT("Run B activates a different mixed candidate"), RunB.Activate(CandidateIDs[4], SetupTime, Error));
	TestTrue(TEXT("Run B activates another different mixed candidate"), RunB.Activate(CandidateIDs[5], SetupTime, Error));
	for (const FResidentID ResidentID : CandidateIDs)
	{
		TestTrue(
			TEXT("Run A prepares Repair Wood through the test Ledger boundary"),
			RunA.SeedResidentWoodForTest(ResidentID, static_cast<int32>(RepairWoodPerHome), SetupTime, Error));
		TestTrue(
			TEXT("Run B prepares identical Repair Wood through the test Ledger boundary"),
			RunB.SeedResidentWoodForTest(ResidentID, static_cast<int32>(RepairWoodPerHome), SetupTime, Error));
	}

	auto MakeRequest = [](const FStatePreservingLODSystem& System, const FResidentID ResidentID)
	{
		FUnifiedActionRequest Request;
		Request.ResidentID = ResidentID;
		Request.Action = EIndividualAction::StartRepair;
		Request.Quantity = 1;
		const FResidentCoreState* Resident = System.FindResident(ResidentID);
		Request.Representation = Resident != nullptr
			? Resident->Representation
			: EResidentRepresentation::CohortManaged;
		return Request;
	};

	TArray<FUnifiedActionRequest> RequestsA;
	TArray<FUnifiedActionRequest> RequestsB;
	for (const FResidentID ResidentID : CandidateIDs)
	{
		RequestsA.Add(MakeRequest(RunA, ResidentID));
	}
	for (int32 Index = CandidateIDs.Num() - 1; Index >= 0; --Index)
	{
		RequestsB.Add(MakeRequest(RunB, CandidateIDs[Index]));
	}

	const FSimulationTime CompetitionTime = FSimulationTime::FromDays(8);
	const int32 FrozenRepairCapacity = FMath::FloorToInt(RepairStartCapacityPerPersonPerDay * 100);
	TestEqual(TEXT("The frozen 100-person Repair start capacity is one"), FrozenRepairCapacity, 1);
	TestTrue(TEXT("Run A resolves shared Repair Capacity"), RunA.ResolveCompetition(CompetitionTime, FrozenRepairCapacity, RequestsA, Error));
	TestTrue(TEXT("Run B resolves the reversed mixed-LOD input"), RunB.ResolveCompetition(CompetitionTime, FrozenRepairCapacity, RequestsB, Error));
	TestEqual(TEXT("Both competitions retain the same candidate count"), RequestsA.Num(), RequestsB.Num());
	for (int32 Index = 0; Index < RequestsA.Num() && Index < RequestsB.Num(); ++Index)
	{
		TestEqual(TEXT("LOD representation and input order do not change sorted ResidentID"), RequestsA[Index].ResidentID, RequestsB[Index].ResidentID);
		TestEqual(TEXT("LOD representation and input order do not change OrderKey"), RequestsA[Index].OrderKey, RequestsB[Index].OrderKey);
		TestEqual(TEXT("LOD representation and input order do not change ArriveID"), RequestsA[Index].ArriveID, RequestsB[Index].ArriveID);
		TestEqual(TEXT("LOD representation and input order do not change the winner"), RequestsA[Index].bWon, RequestsB[Index].bWon);
	}
	TArray<FArriveID> StableArriveIDs;
	for (const FUnifiedActionRequest& Request : RequestsA)
	{
		StableArriveIDs.Add(Request.ArriveID);
	}
	TestTrue(TEXT("Run A can re-evaluate the same unfinished requests"), RunA.ResolveCompetition(CompetitionTime, FrozenRepairCapacity, RequestsA, Error));
	TestTrue(TEXT("Run B can re-evaluate the same unfinished requests"), RunB.ResolveCompetition(CompetitionTime, FrozenRepairCapacity, RequestsB, Error));
	for (int32 Index = 0; Index < RequestsA.Num(); ++Index)
	{
		TestEqual(TEXT("Re-evaluation preserves each existing ArriveID"), RequestsA[Index].ArriveID, StableArriveIDs[Index]);
	}
	TArray<FUnifiedActionRequest> SwappedArriveIDs = RequestsA;
	Swap(SwappedArriveIDs[0].ArriveID, SwappedArriveIDs[1].ArriveID);
	TestFalse(
		TEXT("The unified queue rejects ArriveIDs swapped between resident-action requests"),
		RunA.ResolveCompetition(CompetitionTime, FrozenRepairCapacity, SwappedArriveIDs, Error));
	TArray<FUnifiedActionRequest> DuplicateRequests = { RequestsA[0], RequestsA[0] };
	TestFalse(TEXT("The unified queue rejects a duplicate resident-action candidate"), RunA.ResolveCompetition(CompetitionTime, FrozenRepairCapacity, DuplicateRequests, Error));

	int32 WinnersA = 0;
	int32 WinnersB = 0;
	for (const FUnifiedActionRequest& Request : RequestsA)
	{
		WinnersA += Request.bWon ? 1 : 0;
	}
	for (const FUnifiedActionRequest& Request : RequestsB)
	{
		WinnersB += Request.bWon ? 1 : 0;
	}
	TestEqual(TEXT("Repair Capacity admits exactly one request in Run A"), WinnersA, FrozenRepairCapacity);
	TestEqual(TEXT("Repair Capacity admits exactly one request in Run B"), WinnersB, FrozenRepairCapacity);

	for (const FUnifiedActionRequest& Request : RequestsA)
	{
		if (Request.bWon)
		{
			TestTrue(
				TEXT("Run A commits each winning Repair through the shared Ledger"),
				RunA.StartRepair(Request.ResidentID, CompetitionTime, Error, Request.ArriveID));
		}
	}
	for (const FUnifiedActionRequest& Request : RequestsB)
	{
		if (Request.bWon)
		{
			TestTrue(
				TEXT("Run B commits each winning Repair through the shared Ledger"),
				RunB.StartRepair(Request.ResidentID, CompetitionTime, Error, Request.ArriveID));
		}
	}
	TestEqual(TEXT("Run A records the one winning Repair start"), CountLedgerTransactionsWithPrefix(RunA, TEXT("REPAIR-START-")), FrozenRepairCapacity);
	TestEqual(TEXT("Run B records the one winning Repair start"), CountLedgerTransactionsWithPrefix(RunB, TEXT("REPAIR-START-")), FrozenRepairCapacity);

	const TArray<FLedgerTransaction>& TransactionsA = RunA.GetLedger().GetTransactions();
	const TArray<FLedgerTransaction>& TransactionsB = RunB.GetLedger().GetTransactions();
	TestEqual(TEXT("Mixed LOD choices produce the same transaction count"), TransactionsA.Num(), TransactionsB.Num());
	for (int32 Index = 0; Index < TransactionsA.Num() && Index < TransactionsB.Num(); ++Index)
	{
		const FLedgerTransaction& A = TransactionsA[Index];
		const FLedgerTransaction& B = TransactionsB[Index];
		TestEqual(TEXT("TransactionID is reproducible"), A.TransactionID, B.TransactionID);
		TestEqual(TEXT("Transaction key is reproducible"), A.Transfer.IdempotencyKey, B.Transfer.IdempotencyKey);
		TestEqual(TEXT("Transaction time is reproducible"), A.Transfer.GameTime.Minutes, B.Transfer.GameTime.Minutes);
		TestEqual(TEXT("Transaction resource is reproducible"), A.Transfer.Resource, B.Transfer.Resource);
		TestEqual(TEXT("Transaction source is reproducible"), A.Transfer.Source, B.Transfer.Source);
		TestEqual(TEXT("Transaction destination is reproducible"), A.Transfer.Destination, B.Transfer.Destination);
		TestEqual(TEXT("Transaction quantity is reproducible"), A.Transfer.Quantity, B.Transfer.Quantity);
		TestEqual(TEXT("Transaction boundary flag is reproducible"), A.Transfer.bBoundaryFlow, B.Transfer.bBoundaryFlow);
		TestEqual(TEXT("Transaction EventID is reproducible"), A.Transfer.EventID, B.Transfer.EventID);
		TestEqual(TEXT("Transaction ArriveID is reproducible"), A.Transfer.ArriveID, B.Transfer.ArriveID);
		TestEqual(TEXT("Transaction PolicyID is reproducible"), A.Transfer.PolicyID, B.Transfer.PolicyID);
	}
	const FUnifiedActionRequest* CommittedWinner = RequestsA.FindByPredicate([](const FUnifiedActionRequest& Request)
	{
		return Request.bWon;
	});
	if (CommittedWinner != nullptr)
	{
		TArray<FUnifiedActionRequest> ReusedCommittedRequest = { *CommittedWinner };
		TestFalse(
			TEXT("A committed ArriveID cannot be reused as an unfinished competition request"),
			RunA.ResolveCompetition(CompetitionTime, FrozenRepairCapacity, ReusedCommittedRequest, Error));
	}
	TestTrue(TEXT("Run A competition commit leaves all hard audits clean"), RunA.Audit().IsHardErrorFree());
	TestTrue(TEXT("Run B competition commit leaves all hard audits clean"), RunB.Audit().IsHardErrorFree());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase4MacroBatchPlanningTest,
	"AILODResearch.Phase4.MacroBatchPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase4MacroBatchPlanningTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FStatePreservingLODSystem System;
	FString Error;
	if (!InitializeDefaultSystem(System, Error))
	{
		AddError(Error);
		return false;
	}

	FIndividualWorldFacts World;
	World.MarketWoodAvailable = 200.0;
	World.ForestWood = 1600.0;
	World.HarvestAllowance = 1000.0;
	World.WoodPrice = 1.0;
	FMacroDecisionBatch Batch;
	TestTrue(TEXT("A Cohort-managed decision batch can be built"), System.BuildMacroDecisionBatch(World, World, Batch, Error));
	TestEqual(TEXT("The initial batch covers all 200 CohortManaged residents"), Batch.ResidentCount, 200);
	TestTrue(TEXT("At least one representative plan is evaluated"), Batch.PlanningEvaluationCount > 0);
	TestTrue(TEXT("Representative planning performs fewer evaluations than per-resident planning"), Batch.PlanningEvaluationCount < Batch.ResidentCount);
	TestEqual(TEXT("Each decision group is evaluated exactly once"), Batch.PlanningEvaluationCount, Batch.Groups.Num());

	TSet<FResidentID> BatchedResidents;
	for (const FMacroDecisionGroup& Group : Batch.Groups)
	{
		TestTrue(TEXT("Every macro decision group has at least one resident"), Group.ResidentIDs.Num() > 0);
		TestTrue(TEXT("Every macro decision group selects an action"), Group.Action != EIndividualAction::None);
		for (const FResidentID ResidentID : Group.ResidentIDs)
		{
			TestFalse(TEXT("A resident appears in only one macro decision group"), BatchedResidents.Contains(ResidentID));
			BatchedResidents.Add(ResidentID);
		}
	}
	TestEqual(TEXT("All 200 residents appear exactly once in the batch"), BatchedResidents.Num(), 200);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase4DeterminismTest,
	"AILODResearch.Phase4.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase4DeterminismTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FStatePreservingLODSystem RunA;
	FStatePreservingLODSystem RunB;
	FString DigestA;
	FString DigestB;
	FString Error;
	TestTrue(TEXT("Deterministic Phase 4 trace A runs"), RunDeterministicTrace(RunA, DigestA, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	TestTrue(TEXT("Deterministic Phase 4 trace B runs"), RunDeterministicTrace(RunB, DigestB, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	TestEqual(TEXT("The same config and trace produce the same Phase 4 digest"), DigestA, DigestB);
	AddInfo(FString::Printf(TEXT("Phase4 v1.6 deterministic digest=%s"), *DigestA));
	TestEqual(TEXT("The same trace produces the same transition count"), RunA.GetTransitions().Num(), RunB.GetTransitions().Num());
	TestTrue(TEXT("Run A remains hard-error free"), RunA.Audit().IsHardErrorFree());
	TestTrue(TEXT("Run B remains hard-error free"), RunB.Audit().IsHardErrorFree());
	return true;
}

#endif
