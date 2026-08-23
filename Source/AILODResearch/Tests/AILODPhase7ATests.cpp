// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODExperimentLogging.h"
#include "../Simulation/AILODExperimentRunner.h"
#include "../Simulation/AILODUnifiedSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	AILOD::FPhase0Config MakeDemoConfig()
	{
		AILOD::FPhase0Config Config;
		Config.Seed = 20260810;
		Config.PopulationPerKingdom = 100;
		return Config;
	}

	AILOD::FUnifiedRunOptions MakeDemoOptions()
	{
		AILOD::FUnifiedRunOptions Options;
		Options.Mode = AILOD::EUnifiedRunMode::Demo;
		Options.ProposedModelVersion = AILOD::EProposedModelVersion::V17Authoritative;
		Options.bRecordSnapshots = false;
		Options.bRetainCompletedEvents = false;
		return Options;
	}

	TArray<AILOD::FResidentID> CopyActiveIDs(const AILOD::FUnifiedDemoSnapshot& Snapshot)
	{
		TArray<AILOD::FResidentID> ResidentIDs;
		ResidentIDs.Reserve(Snapshot.ActiveResidents.Num());
		for (const AILOD::FUnifiedDemoResidentSnapshot& Resident : Snapshot.ActiveResidents)
		{
			ResidentIDs.Add(Resident.ResidentID);
		}
		return ResidentIDs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7ADemoGuardsTest,
	"AILODResearch.Phase7A.DemoGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7ADemoGuardsTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FString Error;

	FUnifiedRunOptions ImplicitOldRuntimeOptions;
	ImplicitOldRuntimeOptions.Mode = EUnifiedRunMode::Demo;
	FUnifiedSimulationSession ImplicitOldRuntime(
		MakeDemoConfig(),
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::StateImport,
		ImplicitOldRuntimeOptions);
	TestFalse(TEXT("Demo rejects the old Proposed runtime default"), ImplicitOldRuntime.Initialize(Error));
	TestTrue(TEXT("The old-runtime rejection explains the explicit v1.9 requirement"),
		Error.Contains(TEXT("explicit v1.9")));

	FUnifiedSimulationSession WrongScenario(
		MakeDemoConfig(),
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::None,
		MakeDemoOptions());
	TestFalse(TEXT("The first Demo rejects a non-StateImport scenario"), WrongScenario.Initialize(Error));
	TestTrue(TEXT("The scenario rejection names StateImport"), Error.Contains(TEXT("StateImport")));

	FExperimentMatrixRequest FormalRunnerRequest;
	FormalRunnerRequest.Mode = EUnifiedRunMode::Demo;
	TArray<FExperimentRunRecord> Runs;
	TestFalse(TEXT("The formal experiment runner rejects Demo mode before producing runs"),
		FExperimentRunner::RunMatrix(FormalRunnerRequest, Runs, Error));
	TestEqual(TEXT("Rejected Demo mode leaves the formal run list empty"), Runs.Num(), 0);
	TestTrue(TEXT("The formal runner rejection explains the data boundary"),
		Error.Contains(TEXT("excluded from the formal experiment runner")));

	FUnifiedRunResult DemoResult;
	DemoResult.Mode = EUnifiedRunMode::Demo;
	FUnifiedRunLogMetadata FormalLogMetadata;
	FUnifiedRunLogWriter FormalLogWriter;
	TestFalse(TEXT("The experiment log writer also rejects a directly supplied Demo result"),
		FormalLogWriter.WriteRun(DemoResult, FormalLogMetadata, Error));
	TestTrue(TEXT("The log-writer rejection explains the record boundary"),
		Error.Contains(TEXT("excluded from the experiment run log writer")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7AAtomicObservationTest,
	"AILODResearch.Phase7A.AtomicObservationAndSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7AAtomicObservationTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FString Error;
	FUnifiedSimulationSession Session(
		MakeDemoConfig(),
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::StateImport,
		MakeDemoOptions());
	if (!Session.Initialize(Error))
	{
		AddError(FString::Printf(TEXT("Demo initialization failed: %s"), *Error));
		return false;
	}

	FUnifiedDemoSnapshot Snapshot;
	if (!Session.BuildDemoSnapshot(Snapshot, Error))
	{
		AddError(FString::Printf(TEXT("Initial Demo snapshot failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("The presentation protocol is 2.0"), Snapshot.DemoProtocolVersion, FString(TEXT("2.0")));
	TestEqual(TEXT("The model remains v1.9"), Snapshot.ModelSpecVersion, FString(TEXT("1.9")));
	TestEqual(TEXT("The domain digest contract remains v1.9-domain-v1"),
		Snapshot.DeterministicDigestVersion, FString(TEXT("1.9-domain-v1")));
	TestFalse(TEXT("An interactive snapshot is never a formal run"), Snapshot.bFormalRun);
	TestEqual(TEXT("The initialized Demo starts without the formal fixed trace"), Snapshot.ActiveCount, 0);

	FUnifiedDemoObservationRequest Request;
	Request.DesiredActiveResidentIDs = { 3, 1, 2, 2 };
	Request.TrackedResidentID = 1;
	if (!Session.SubmitDemoObservationRequest(Request, Error))
	{
		AddError(FString::Printf(TEXT("Atomic observation request failed: %s"), *Error));
		return false;
	}
	if (!Session.BuildDemoSnapshot(Snapshot, Error)) return false;
	const TArray<FResidentID> ExpectedIDs = { 1, 2, 3 };
	TestEqual(TEXT("Observation IDs are deduplicated and stably sorted"), CopyActiveIDs(Snapshot), ExpectedIDs);
	TestEqual(TEXT("The selected tracked resident is copied into the snapshot"), Snapshot.TrackedResidentID, FResidentID(1));
	TestEqual(TEXT("Exactly three Active residents are exposed"), Snapshot.ActiveCount, 3);
	TestTrue(TEXT("The tracked row is marked in the copied resident view"),
		Snapshot.ActiveResidents.Num() == 3 && Snapshot.ActiveResidents[0].bTracked);

	const FString OriginalName = Snapshot.ActiveResidents[0].Name;
	const int32 OriginalCash = Snapshot.ActiveResidents[0].Cash;
	Snapshot.ActiveResidents[0].Name = TEXT("UI attempted mutation");
	Snapshot.ActiveResidents[0].Cash = -999999;
	FUnifiedDemoSnapshot FreshSnapshot;
	if (!Session.BuildDemoSnapshot(FreshSnapshot, Error)) return false;
	TestEqual(TEXT("Changing a UI-side name copy cannot change authority"), FreshSnapshot.ActiveResidents[0].Name, OriginalName);
	TestEqual(TEXT("Changing a UI-side cash copy cannot change authority"), FreshSnapshot.ActiveResidents[0].Cash, OriginalCash);

	auto TestRejectedRequestKeepsOldSet = [this, &Session, &FreshSnapshot, &Error](
		const TCHAR* Label,
		const FUnifiedDemoObservationRequest& Rejected)
	{
		TestFalse(Label, Session.SubmitDemoObservationRequest(Rejected, Error));
		FUnifiedDemoSnapshot After;
		if (!Session.BuildDemoSnapshot(After, Error)) return false;
		TestEqual(TEXT("A rejected group replacement keeps the old Active set"),
			CopyActiveIDs(After), CopyActiveIDs(FreshSnapshot));
		TestEqual(TEXT("A rejected group replacement keeps the old tracked resident"),
			After.TrackedResidentID, FreshSnapshot.TrackedResidentID);
		return true;
	};

	FUnifiedDemoObservationRequest MissingIdentity;
	MissingIdentity.DesiredActiveResidentIDs = { 999999 };
	if (!TestRejectedRequestKeepsOldSet(TEXT("A missing identity rejects the whole replacement"), MissingIdentity)) return false;
	FUnifiedDemoObservationRequest OverCap;
	for (FResidentID ResidentID = 1; ResidentID <= 51; ++ResidentID)
	{
		OverCap.DesiredActiveResidentIDs.Add(ResidentID);
	}
	if (!TestRejectedRequestKeepsOldSet(TEXT("A 51-resident request rejects the whole replacement"), OverCap)) return false;
	FUnifiedDemoObservationRequest InvalidTracked;
	InvalidTracked.DesiredActiveResidentIDs = { 1, 2, 3 };
	InvalidTracked.TrackedResidentID = 4;
	if (!TestRejectedRequestKeepsOldSet(TEXT("Tracking outside the desired set rejects the whole replacement"), InvalidTracked)) return false;

	TArray<FUnifiedDemoObservationRecord> Records;
	if (!Session.CopyDemoObservationLog(Records, Error)) return false;
	TestEqual(TEXT("Every accepted or rejected observation attempt is recorded"), Records.Num(), 4);
	TestTrue(TEXT("The valid replacement is recorded as committed"), Records.Num() == 4 && Records[0].bCommitted);
	TestTrue(TEXT("Rejected replacements are recorded as rejected"),
		Records.Num() == 4 && !Records[1].bCommitted && !Records[2].bCommitted && !Records[3].bCommitted);

	FUnifiedDemoObservationRequest ReleaseAll;
	if (!Session.SubmitDemoObservationRequest(ReleaseAll, Error)
		|| !Session.BuildDemoSnapshot(FreshSnapshot, Error))
	{
		AddError(FString::Printf(TEXT("Demo release request failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("An empty desired set releases all visual Active residents"), FreshSnapshot.ActiveCount, 0);
	TestEqual(TEXT("Releasing all residents clears tracking"), FreshSnapshot.TrackedResidentID, FResidentID(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7AFixedTraceIsolationTest,
	"AILODResearch.Phase7A.FixedTraceIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7AFixedTraceIsolationTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FString Error;
	FUnifiedSimulationSession Session(
		MakeDemoConfig(),
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::StateImport,
		MakeDemoOptions());
	if (!Session.Initialize(Error)) return false;
	for (int32 Hour = 0; Hour < 14 * 24; ++Hour)
	{
		if (!Session.StepHour(Error))
		{
			AddError(FString::Printf(TEXT("Demo prewarm/step failed at hour %d: %s"), Hour, *Error));
			return false;
		}
	}

	FUnifiedDemoSnapshot Snapshot;
	if (!Session.BuildDemoSnapshot(Snapshot, Error)) return false;
	TestEqual(TEXT("At Day 7 Demo did not execute the formal activation trace"), Snapshot.ActiveCount, 0);

	FUnifiedDemoObservationRequest Request;
	Request.DesiredActiveResidentIDs = { 10, 20 };
	Request.TrackedResidentID = 10;
	if (!Session.SubmitDemoObservationRequest(Request, Error)) return false;
	for (int32 Hour = 0; Hour < 24; ++Hour)
	{
		if (!Session.StepHour(Error)) return false;
	}
	if (!Session.BuildDemoSnapshot(Snapshot, Error)) return false;
	const TArray<FResidentID> ExpectedIDs = { 10, 20 };
	TestEqual(TEXT("At Day 8 the formal restriction trace cannot replace the interactive set"),
		CopyActiveIDs(Snapshot), ExpectedIDs);
	TestEqual(TEXT("The interactive tracked resident survives the formal Day 8 boundary"),
		Snapshot.TrackedResidentID, FResidentID(10));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7ACommandReplayTest,
	"AILODResearch.Phase7A.CommandReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7ACommandReplayTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FString Error;
	FUnifiedSimulationSession RunA(
		MakeDemoConfig(), EUnifiedSimulationMethod::Proposed, EStage2Scenario::StateImport, MakeDemoOptions());
	FUnifiedSimulationSession RunB(
		MakeDemoConfig(), EUnifiedSimulationMethod::Proposed, EStage2Scenario::StateImport, MakeDemoOptions());
	if (!RunA.Initialize(Error) || !RunB.Initialize(Error)) return false;

	FUnifiedDemoObservationRequest FirstRequest;
	FirstRequest.DesiredActiveResidentIDs = { 1, 2, 3 };
	FirstRequest.TrackedResidentID = 1;
	if (!RunA.SubmitDemoObservationRequest(FirstRequest, Error)) return false;
	TArray<FUnifiedDemoObservationRecord> SourceRecords;
	if (!RunA.CopyDemoObservationLog(SourceRecords, Error)
		|| SourceRecords.Num() != 1
		|| !RunB.ReplayDemoObservationRecord(SourceRecords[0], Error))
	{
		AddError(FString::Printf(TEXT("The first Demo command could not be replayed: %s"), *Error));
		return false;
	}
	for (int32 Hour = 0; Hour < 3; ++Hour)
	{
		if (!RunA.StepHour(Error) || !RunB.StepHour(Error)) return false;
	}

	FUnifiedDemoObservationRequest SecondRequest;
	SecondRequest.DesiredActiveResidentIDs = { 2, 5 };
	SecondRequest.TrackedResidentID = 5;
	if (!RunA.SubmitDemoObservationRequest(SecondRequest, Error)
		|| !RunA.CopyDemoObservationLog(SourceRecords, Error)
		|| SourceRecords.Num() != 2
		|| !RunB.ReplayDemoObservationRecord(SourceRecords[1], Error))
	{
		AddError(FString::Printf(TEXT("The second Demo command could not be replayed: %s"), *Error));
		return false;
	}

	FUnifiedDemoSnapshot SnapshotA;
	FUnifiedDemoSnapshot SnapshotB;
	if (!RunA.BuildDemoSnapshot(SnapshotA, Error) || !RunB.BuildDemoSnapshot(SnapshotB, Error)) return false;
	TestEqual(TEXT("Replay reaches the same authoritative game time"), SnapshotB.GameTime.Minutes, SnapshotA.GameTime.Minutes);
	TestEqual(TEXT("Replay reaches the same tracked resident"), SnapshotB.TrackedResidentID, SnapshotA.TrackedResidentID);
	TestEqual(TEXT("Replay reaches the same stable Active set"), CopyActiveIDs(SnapshotB), CopyActiveIDs(SnapshotA));
	TestEqual(TEXT("Replay returns the same number of copied resident rows"),
		SnapshotB.ActiveResidents.Num(), SnapshotA.ActiveResidents.Num());
	for (int32 Index = 0; Index < SnapshotA.ActiveResidents.Num() && Index < SnapshotB.ActiveResidents.Num(); ++Index)
	{
		const FUnifiedDemoResidentSnapshot& A = SnapshotA.ActiveResidents[Index];
		const FUnifiedDemoResidentSnapshot& B = SnapshotB.ActiveResidents[Index];
		TestEqual(TEXT("Replay preserves resident identity"), B.PersistentID, A.PersistentID);
		TestEqual(TEXT("Replay preserves resident housing identity"), B.HomeID, A.HomeID);
		TestEqual(TEXT("Replay preserves resident cash"), B.Cash, A.Cash);
		TestEqual(TEXT("Replay preserves resident wood"), B.InventoryWood, A.InventoryWood);
		TestEqual(TEXT("Replay preserves resident action"), B.CurrentAction, A.CurrentAction);
		TestEqual(TEXT("Replay preserves remaining work"), B.RemainingWorkMinutes, A.RemainingWorkMinutes);
	}

	TArray<FUnifiedDemoObservationRecord> ReplayRecords;
	if (!RunB.CopyDemoObservationLog(ReplayRecords, Error)) return false;
	TestEqual(TEXT("Replay records the same two committed requests"), ReplayRecords.Num(), 2);
	TestTrue(TEXT("Both replayed requests committed"),
		ReplayRecords.Num() == 2 && ReplayRecords[0].bCommitted && ReplayRecords[1].bCommitted);
	return true;
}

#endif
