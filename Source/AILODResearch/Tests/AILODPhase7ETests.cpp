// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Presentation/AILODVisualDemoRuntime.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool AdvanceVisualRuntimeToDayZero(AILOD::FVisualDemoRuntime& Runtime, FString& OutError)
	{
		while (Runtime.GetState() == AILOD::EVisualDemoRuntimeState::Prewarming)
		{
			if (!Runtime.Tick(0.0, OutError)) return false;
		}
		return Runtime.GetState() == AILOD::EVisualDemoRuntimeState::Running;
	}

	AILOD::FUnifiedRunOptions MakePhase7EDemoOptions()
	{
		AILOD::FUnifiedRunOptions Options;
		Options.Mode = AILOD::EUnifiedRunMode::Demo;
		Options.ProposedModelVersion = AILOD::EProposedModelVersion::V17Authoritative;
		Options.bRecordSnapshots = false;
		Options.bRetainCompletedEvents = false;
		return Options;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7ETelescopeLiftReplayTest,
	"AILODResearch.Phase7E.TelescopeLiftTrackReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7ETelescopeLiftReplayTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 100;
	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error) || !AdvanceVisualRuntimeToDayZero(Runtime, Error))
	{
		AddError(FString::Printf(TEXT("The Phase 7E runtime could not reach Day 0: %s"), *Error));
		return false;
	}
	if (!Runtime.RequestPaused(true, Error)) return false;

	const FVisualResidentPlacement& AimResident = Runtime.GetLayout().GetResidents()[50];
	FVisualObservationFrameInput Input;
	Input.bNormalViewEnabled = false;
	Input.bTelescopeEnabled = true;
	Input.TelescopeView.Origin = AimResident.ProxyPosition - FVector2D(50000.0, 0.0);
	Input.TelescopeView.Forward = FVector2D(1.0, 0.0);
	Input.TelescopeView.MinimumDistance = 30000.0;
	Input.TelescopeView.EnterDistance = 60000.0;
	Input.TelescopeView.HalfAngleDegrees = 0.1;
	if (!Runtime.SubmitObservationFrame(Input, Error)) return false;
	const FResidentID CenterResidentID =
		Runtime.GetCurrentPresentationObservationPlan().TelescopeCenterResidentID;
	TestTrue(TEXT("The telescope chooses one deterministic real center resident"),
		CenterResidentID > 0 && Runtime.GetLayout().FindResident(CenterResidentID) != nullptr);
	if (CenterResidentID <= 0) return false;
	const FVisualResidentPlacement* CenterPlacement = Runtime.GetLayout().FindResident(CenterResidentID);
	TestTrue(TEXT("The telescope center is outside the configured near-view exclusion distance"),
		CenterPlacement != nullptr
		&& FVector2D::Distance(Input.TelescopeView.Origin, CenterPlacement->ProxyPosition)
			>= Input.TelescopeView.MinimumDistance);
	FVisualDemoRuntime SameInputRuntime;
	if (!SameInputRuntime.Initialize(Config, Error)
		|| !AdvanceVisualRuntimeToDayZero(SameInputRuntime, Error)
		|| !SameInputRuntime.RequestPaused(true, Error)
		|| !SameInputRuntime.SubmitObservationFrame(Input, Error)) return false;
	TestEqual(TEXT("The same input, simulation seed, and layout reproduce the same center ResidentID"),
		SameInputRuntime.GetCurrentPresentationObservationPlan().TelescopeCenterResidentID,
		CenterResidentID);

	FVisualTelescopeFocusGate FocusGate;
	TestEqual(TEXT("Focus time alone cannot Lift before distant streaming is ready"),
		FocusGate.Update(true, CenterResidentID, 1.0, false, 1.5), FResidentID(0));
	TestEqual(TEXT("The same center Lifts after both the time and streaming gates pass"),
		FocusGate.Update(true, CenterResidentID, 0.5, true, 1.5), CenterResidentID);

	Input.TelescopePromotionResidentID = CenterResidentID;
	if (!Runtime.SubmitObservationFrame(Input, Error)) return false;
	FUnifiedDemoSnapshot LiftedSnapshot;
	if (!Runtime.CopySnapshot(LiftedSnapshot)) return false;
	TestEqual(TEXT("Lift atomically records the center as the tracked ResidentID"),
		LiftedSnapshot.TrackedResidentID, CenterResidentID);
	TestTrue(TEXT("The tracked resident occupies one of at most 50 Active slots"),
		LiftedSnapshot.ActiveCount <= 50
		&& LiftedSnapshot.ActiveResidents.ContainsByPredicate(
			[CenterResidentID](const FUnifiedDemoResidentSnapshot& Resident)
			{
				return Resident.ResidentID == CenterResidentID;
			}));

	FVisualObservationFrameInput TelescopeClosed;
	TelescopeClosed.bNormalViewEnabled = false;
	if (!Runtime.SubmitObservationFrame(TelescopeClosed, Error)) return false;
	FUnifiedDemoSnapshot ClosedSnapshot;
	if (!Runtime.CopySnapshot(ClosedSnapshot)) return false;
	TestEqual(TEXT("Closing the telescope retains the same tracked identity offscreen"),
		ClosedSnapshot.TrackedResidentID, CenterResidentID);

	TelescopeClosed.bClearTrackedResident = true;
	if (!Runtime.SubmitObservationFrame(TelescopeClosed, Error)) return false;
	FUnifiedDemoSnapshot ClearedSnapshot;
	if (!Runtime.CopySnapshot(ClearedSnapshot)) return false;
	TestEqual(TEXT("Explicit clear releases telescope tracking"),
		ClearedSnapshot.TrackedResidentID, FResidentID(0));

	TArray<FUnifiedDemoObservationRecord> Records;
	if (!Runtime.CopyObservationLog(Records, Error)) return false;
	TestEqual(TEXT("Only Lift and clear change authority and are saved as commands"),
		Records.Num(), 2);
	if (Records.Num() != 2) return false;
	TestEqual(TEXT("The saved Lift command tracks the deterministic center resident"),
		Records[0].Request.TrackedResidentID, CenterResidentID);
	TestEqual(TEXT("The saved clear command removes tracking"),
		Records[1].Request.TrackedResidentID, FResidentID(0));

	FPhase0Config SimulationConfig;
	SimulationConfig.Seed = Config.SimulationSeed;
	SimulationConfig.PopulationPerKingdom = Config.PopulationPerKingdom;
	FUnifiedSimulationSession ReplaySession(
		SimulationConfig,
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::StateImport,
		MakePhase7EDemoOptions());
	if (!ReplaySession.Initialize(Error)) return false;
	for (int32 Hour = 0; Hour < 7 * 24; ++Hour)
	{
		if (!ReplaySession.StepHour(Error)) return false;
	}
	for (const FUnifiedDemoObservationRecord& Record : Records)
	{
		if (!ReplaySession.ReplayDemoObservationRecord(Record, Error))
		{
			AddError(FString::Printf(TEXT("A saved Phase 7E command could not replay: %s"), *Error));
			return false;
		}
	}
	FUnifiedDemoSnapshot ReplaySnapshot;
	if (!ReplaySession.BuildDemoSnapshot(ReplaySnapshot, Error)) return false;
	TestEqual(TEXT("Replaying the same command sequence reproduces the final tracked state"),
		ReplaySnapshot.TrackedResidentID, ClearedSnapshot.TrackedResidentID);
	TestEqual(TEXT("Command replay preserves the final Active ResidentID set"),
		ReplaySnapshot.ActiveResidents.Num(), ClearedSnapshot.ActiveResidents.Num());
	return true;
}

#endif
