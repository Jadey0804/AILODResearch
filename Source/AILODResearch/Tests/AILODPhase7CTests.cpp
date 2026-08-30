// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Presentation/AILODVisualDemoRuntime.h"
#include "../Simulation/AILODPhase0Manifest.h"
#include "../Simulation/AILODV17AuthoritativeMacro.h"
#include "../Visual/AILODVisualDemoSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7CRuntimeClockTest,
	"AILODResearch.Phase7C.RuntimeClockBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7CRuntimeClockTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 100;

	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error))
	{
		AddError(FString::Printf(TEXT("Phase 7C runtime initialization failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("The Demo starts in the loading prewarm state"), Runtime.GetState(), EVisualDemoRuntimeState::Prewarming);

	for (int32 Frame = 0; Frame < 7 * 24; ++Frame)
	{
		if (!Runtime.Tick(10.0, Error)) return false;
		TestTrue(TEXT("Prewarm executes at most one StepHour per frame"), Runtime.GetLastTickStepCount() <= 1);
	}
	FUnifiedDemoSnapshot Snapshot;
	TestTrue(TEXT("A copied Day 0 snapshot is available"), Runtime.CopySnapshot(Snapshot));
	TestEqual(TEXT("The player-facing Demo begins at Day 0"), Snapshot.GameTime.Minutes, int64(0));
	TestEqual(TEXT("Day 0 changes the controller to running"), Runtime.GetState(), EVisualDemoRuntimeState::Running);

	if (!Runtime.RequestTimeScale(4, Error) || !Runtime.Tick(1.0, Error)) return false;
	TestEqual(TEXT("A 4x hitch still executes only one StepHour"), Runtime.GetLastTickStepCount(), 1);
	TestTrue(TEXT("Unprocessed 4x time remains queued"), Runtime.GetPendingHourSteps() >= 2.999);
	if (!Runtime.Tick(0.0, Error)) return false;
	TestEqual(TEXT("A later frame drains only one queued hour"), Runtime.GetLastTickStepCount(), 1);
	TestTrue(TEXT("The remaining queued hours are retained"), Runtime.GetPendingHourSteps() >= 1.999);

	if (!Runtime.RequestPaused(true, Error)) return false;
	if (!Runtime.CopySnapshot(Snapshot)) return false;
	const int64 PausedTime = Snapshot.GameTime.Minutes;
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		if (!Runtime.Tick(1.0, Error)) return false;
		TestEqual(TEXT("Paused frames execute no StepHour"), Runtime.GetLastTickStepCount(), 0);
	}
	if (!Runtime.CopySnapshot(Snapshot)) return false;
	TestEqual(TEXT("Pause does not advance authoritative time"), Snapshot.GameTime.Minutes, PausedTime);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7CReadOnlySnapshotTest,
	"AILODResearch.Phase7C.ReadOnlyUISnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7CReadOnlySnapshotTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 100;

	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error)) return false;
	for (int32 Frame = 0; Frame < 7 * 24; ++Frame)
	{
		if (!Runtime.Tick(0.0, Error)) return false;
	}

	FVisualObservationFrameInput View;
	View.NormalView.Origin = FVector2D(-50000.0, 0.0);
	View.NormalView.Forward = FVector2D(1.0, 0.0);
	View.NormalView.EnterDistance = 200000.0;
	View.NormalView.HalfAngleDegrees = 90.0;
	View.RealDeltaSeconds = 1.0;
	if (!Runtime.SubmitObservationFrame(View, Error)) return false;

	FUnifiedDemoSnapshot UIValue;
	if (!Runtime.CopySnapshot(UIValue) || UIValue.ActiveResidents.IsEmpty()) return false;
	const FString AuthorityName = UIValue.ActiveResidents[0].Name;
	UIValue.ActiveResidents[0].Name = TEXT("UI mutation attempt");
	UIValue.ActiveResidents[0].Cash = -1;

	FUnifiedDemoSnapshot FreshValue;
	if (!Runtime.CopySnapshot(FreshValue)) return false;
	TestEqual(TEXT("The UI receives a copied resident name"), FreshValue.ActiveResidents[0].Name, AuthorityName);
	TestNotEqual(TEXT("The UI cannot mutate authoritative cash through its copy"), FreshValue.ActiveResidents[0].Cash, -1);
	TestEqual(TEXT("The camera-derived Active set remains within the frozen cap"), FreshValue.ActiveCount <= 50, true);
	TestFalse(TEXT("The interactive snapshot is never formal data"), FreshValue.bFormalRun);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7CDay0ActiveEarthquakeContinuityTest,
	"AILODResearch.Phase7C.Day0ActiveEarthquakeContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7CDay0ActiveEarthquakeContinuityTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FPhase0Config SimulationConfig;
	SimulationConfig.Seed = 20260810;
	SimulationConfig.PopulationPerKingdom = 100;

	FInitialPopulationManifest Population;
	FEarthquakeDamageList Damage;
	FPersistentTestPool PersistentPool;
	FString Error;
	if (!FPhase0ManifestGenerator::Generate(
		SimulationConfig, Population, Damage, PersistentPool, Error)
		|| Damage.DamagedResidents.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Phase 7C could not build a deterministic earthquake target: %s"), *Error));
		return false;
	}
	const FEarthquakeDamageRecord Target = Damage.DamagedResidents[0];

	FUnifiedRunOptions Options;
	Options.Mode = EUnifiedRunMode::Demo;
	Options.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
	Options.bRecordSnapshots = false;
	Options.bRetainCompletedEvents = false;
	FUnifiedSimulationSession Session(
		SimulationConfig,
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::StateImport,
		Options);
	if (!Session.Initialize(Error))
	{
		AddError(FString::Printf(TEXT("Phase 7C Day 0 session initialization failed: %s"), *Error));
		return false;
	}
	for (int32 Hour = 0; Hour < 7 * 24; ++Hour)
	{
		if (!Session.StepHour(Error))
		{
			AddError(FString::Printf(TEXT("Phase 7C prewarm failed at hour %d: %s"), Hour, *Error));
			return false;
		}
	}
	TestEqual(TEXT("The deterministic observation boundary starts at Day 0"),
		Session.GetCurrentTime().Minutes, int64(0));

	FUnifiedDemoObservationRequest Request;
	Request.DesiredActiveResidentIDs = { Target.ResidentID };
	Request.TrackedResidentID = Target.ResidentID;
	if (!Session.SubmitDemoObservationRequest(Request, Error))
	{
		AddError(FString::Printf(TEXT("Phase 7C could not activate the damaged resident at Day 0: %s"), *Error));
		return false;
	}
	FUnifiedDemoSnapshot Before;
	if (!Session.BuildDemoSnapshot(Before, Error) || Before.ActiveResidents.Num() != 1)
	{
		AddError(FString::Printf(TEXT("Phase 7C could not read the Day 0 Active resident: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("The exact damaged ResidentID is Active before the earthquake"),
		Before.ActiveResidents[0].ResidentID, Target.ResidentID);
	TestEqual(TEXT("The Active resident keeps the manifest HomeID"),
		Before.ActiveResidents[0].HomeID, Target.HomeID);
	TestEqual(TEXT("The target home is Healthy before the Day 0 earthquake"),
		Before.ActiveResidents[0].HomeState, EHomeState::Healthy);

	if (!Session.StepHour(Error))
	{
		AddError(FString::Printf(TEXT("The Day 0 Active earthquake boundary failed: %s"), *Error));
		return false;
	}
	FUnifiedDemoSnapshot After;
	if (!Session.BuildDemoSnapshot(After, Error) || After.ActiveResidents.Num() != 1)
	{
		AddError(FString::Printf(TEXT("Phase 7C could not read the post-earthquake Active resident: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("The first formal hour finishes at Day 0 01:00"),
		After.GameTime.Minutes, int64(60));
	TestEqual(TEXT("The same damaged resident remains Active"),
		After.ActiveResidents[0].ResidentID, Target.ResidentID);
	TestEqual(TEXT("The same HomeID survives the earthquake boundary"),
		After.ActiveResidents[0].HomeID, Target.HomeID);
	TestEqual(TEXT("The Active resident reads the authoritative damaged HomeState"),
		After.ActiveResidents[0].HomeState, EHomeState::DamagedWaiting);
	TestEqual(TEXT("The full deterministic damage list is counted exactly once"),
		After.KingdomA.DamagedWaiting, Damage.DamagedResidents.Num());
	TestEqual(TEXT("The telescope/player tracking target remains stable"),
		After.TrackedResidentID, Target.ResidentID);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7CPendingActiveEarthquakeContinuityTest,
	"AILODResearch.Phase7C.PendingActiveEarthquakeContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7CPendingActiveEarthquakeContinuityTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	constexpr FV17AuthoritativeCellID HealthyCellID = 0x7C00;
	FV17AuthoritativeJointKey HealthyKey;
	HealthyKey.Kingdom = EKingdom::A;
	HealthyKey.Profession = EProfession::Worker;
	HealthyKey.IncomeBand = EIncomeBand::Low;
	HealthyKey.HomeState = EHomeState::Healthy;
	HealthyKey.Intent = EMacroIntent::Routine;
	const TArray<FV17AuthoritativeCellConfig> Cells =
	{
		{ HealthyCellID, HealthyKey, 2, 0, 0, 0 }
	};
	TArray<FV17IdentityRecord> Identities;
	for (FResidentID ResidentID = 1; ResidentID <= 2; ++ResidentID)
	{
		FV17IdentityRecord& Identity = Identities.AddDefaulted_GetRef();
		Identity.ResidentID = ResidentID;
		Identity.PersistentID = 700000 + ResidentID;
		Identity.HomeID = 710000 + ResidentID;
		Identity.InitialKingdom = EKingdom::A;
		Identity.Profession = EProfession::Worker;
		Identity.IncomeBand = EIncomeBand::Low;
	}
	const TArray<FV17AuthoritativeKingdomConfig> Kingdoms =
	{
		{ EKingdom::A, 0, 0, 0, 0, 0, 0, 0, 1.0 }
	};

	FV17AuthoritativeMacroSession Session(20260823);
	FString Error;
	if (!Session.InitializeWithIdentity(
		Cells, Identities, Kingdoms, FSimulationTime::FromHours(0), Error))
	{
		AddError(FString::Printf(TEXT("Phase 7C pending Active fixture initialization failed: %s"), *Error));
		return false;
	}
	Session.EnableExactAggregateResourceSplits();
	FV17AuthoritativeClaimID ClaimID = 0;
	if (!Session.QueueMacroAction(
		HealthyCellID, EIndividualAction::Routine, 2, 0, ClaimID, Error)
		|| !Session.ResolveAndCommitClaims(Error)
		|| !Session.LiftResident(1, FSimulationTime::FromHours(0), Error))
	{
		AddError(FString::Printf(TEXT("Phase 7C could not force a pending Active resident: %s"), *Error));
		return false;
	}

	FIndividualActionState BeforeState;
	EIndividualAction BeforeAction = EIndividualAction::None;
	FEventID BeforeEventID = 0;
	if (!Session.GetActiveSnapshot(1, BeforeState, BeforeAction, BeforeEventID)) return false;
	const int64 BeforeRemaining = Session.GetRemainingWorkMinutes(1);
	TestEqual(TEXT("The fixture resident is executing the original Routine task"),
		BeforeAction, EIndividualAction::Routine);
	TestTrue(TEXT("The fixture resident owns a pending event"), BeforeEventID > 0);
	TestTrue(TEXT("The fixture resident has unfinished work"), BeforeRemaining > 0);

	if (!Session.ApplyEarthquakeHomeDamage({ 1 }, Error))
	{
		AddError(FString::Printf(TEXT("Phase 7C pending Active earthquake update failed: %s"), *Error));
		return false;
	}
	FIndividualActionState DamagedState;
	EIndividualAction DamagedAction = EIndividualAction::None;
	FEventID DamagedEventID = 0;
	if (!Session.GetActiveSnapshot(1, DamagedState, DamagedAction, DamagedEventID)) return false;
	TestEqual(TEXT("The earthquake changes the pending Active resident's exact home"),
		DamagedState.HomeState, EHomeState::DamagedWaiting);
	TestEqual(TEXT("The earthquake does not reset the pending task"), DamagedAction, BeforeAction);
	TestEqual(TEXT("The earthquake preserves pending event ownership"), DamagedEventID, BeforeEventID);
	TestEqual(TEXT("The earthquake preserves remaining work"),
		Session.GetRemainingWorkMinutes(1), BeforeRemaining);
	TestTrue(TEXT("The pending Active representation passes the v1.9 hard audit"),
		Session.BuildAudit().IsHardErrorFree());

	if (!Session.RestrictResident(1, FSimulationTime::FromHours(0), Error))
	{
		AddError(FString::Printf(TEXT("Phase 7C could not Restrict the damaged pending resident: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("Restrict keeps the damaged pending HomeID authoritative"),
		Session.BuildAudit().IsHardErrorFree());
	if (!Session.AdvanceTo(FSimulationTime::FromMinutes(BeforeRemaining), Error))
	{
		AddError(FString::Printf(TEXT("Phase 7C damaged pending task completion failed: %s"), *Error));
		return false;
	}
	EHomeState FinalHomeState = EHomeState::Healthy;
	TestTrue(TEXT("The damaged HomeID still exists after the pending task completes"),
		Session.GetResidentHomeState(1, FinalHomeState));
	TestEqual(TEXT("Routine completion cannot revert the earthquake damage"),
		FinalHomeState, EHomeState::DamagedWaiting);
	TestTrue(TEXT("The completed damaged pending path passes the v1.9 hard audit"),
		Session.BuildAudit().IsHardErrorFree());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7CSharedLayoutSettingsTest,
	"AILODResearch.Phase7C.SharedLayoutSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7CSharedLayoutSettingsTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const UAILODVisualDemoSettings* Settings = GetDefault<UAILODVisualDemoSettings>();
	const FVisualDemoRuntimeConfig Config = Settings->MakeRuntimeConfig();
	TestEqual(TEXT("The shared visual default is 20k total residents"), Config.PopulationPerKingdom * 2, 20000);

	FPhase0Config SimulationConfig;
	SimulationConfig.Seed = Config.SimulationSeed;
	SimulationConfig.PopulationPerKingdom = Config.PopulationPerKingdom;
	FInitialPopulationManifest Population;
	FEarthquakeDamageList Damage;
	FPersistentTestPool PersistentPool;
	FString Error;
	if (!FPhase0ManifestGenerator::Generate(SimulationConfig, Population, Damage, PersistentPool, Error)) return false;
	FVisualWorldLayout Layout;
	if (!Layout.Build(Population, Config.Layout, Error)) return false;
	TestEqual(TEXT("The runtime and PCG shared default produces eight districts"), Layout.GetDistricts().Num(), 8);
	TestEqual(TEXT("The runtime and PCG shared default produces 512 home slots"), Layout.GetHomeSlots().Num(), 512);
	TestEqual(TEXT("The runtime and PCG shared default produces 32 road segments"), Layout.GetRoads().Num(), 32);
	TestEqual(TEXT("The runtime and PCG shared default produces 24 work anchors"), Layout.GetWorkAnchors().Num(), 24);
	return true;
}

#endif
