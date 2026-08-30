// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Presentation/AILODVisualDemoRuntime.h"
#include "../Presentation/AILODVisualResidentPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool AdvanceToDayZero(AILOD::FVisualDemoRuntime& Runtime, FString& OutError)
	{
		for (int32 Frame = 0; Frame < 7 * 24; ++Frame)
		{
			if (!Runtime.Tick(0.0, OutError)) return false;
		}
		return Runtime.GetState() == AILOD::EVisualDemoRuntimeState::Running;
	}

	bool SubmitWideResidentView(AILOD::FVisualDemoRuntime& Runtime, FString& OutError)
	{
		const AILOD::FVisualWorldLayout& Layout = Runtime.GetLayout();
		if (Layout.GetResidents().IsEmpty()) return false;
		const FVector2D Target = Layout.GetResidents()[0].ProxyPosition;
		AILOD::FVisualObservationFrameInput View;
		View.NormalView.Origin = Target - FVector2D(150000.0, 0.0);
		View.NormalView.Forward = FVector2D(1.0, 0.0);
		View.NormalView.EnterDistance = 250000.0;
		View.NormalView.HalfAngleDegrees = 90.0;
		View.RealDeltaSeconds = 1.0;
		return Runtime.SubmitObservationFrame(View, OutError);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7FASharedMotionContinuityTest,
	"AILODResearch.Phase7FA.SharedMotionContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7FASharedMotionContinuityTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualResidentPresentationEntry ProxyEntry;
	ProxyEntry.ResidentID = 123;
	ProxyEntry.ProxyPosition = FVector2D(800.0, 0.0);
	ProxyEntry.RouteStart = FVector2D::ZeroVector;
	ProxyEntry.RouteEnd = FVector2D(1600.0, 0.0);
	ProxyEntry.FacingDegrees = 0.0;
	ProxyEntry.bPlaceholderMoves = true;

	FVisualResidentMotionState MotionState =
		FVisualResidentPresentationPlanner::MakeInitialMotionState(ProxyEntry);
	FVisualResidentPresentationPlanner::AdvanceMotionState(
		ProxyEntry, 0.5, 150.0, MotionState);
	const FVisualResidentMotionPose ProxyPose =
		FVisualResidentPresentationPlanner::ResolveMotionPose(ProxyEntry, MotionState);

	FVisualResidentPresentationEntry ActiveEntry = ProxyEntry;
	ActiveEntry.bActiveActor = true;
	ActiveEntry.bHasActiveState = true;
	ActiveEntry.ActiveState.ResidentID = ProxyEntry.ResidentID;
	const FVisualResidentMotionPose ActivePose =
		FVisualResidentPresentationPlanner::ResolveMotionPose(ActiveEntry, MotionState);
	TestEqual(TEXT("Proxy-to-Actor promotion keeps the same route position"),
		ActivePose.Position, ProxyPose.Position);
	TestEqual(TEXT("Proxy-to-Actor promotion keeps the same facing"),
		ActivePose.FacingDegrees, ProxyPose.FacingDegrees);
	TestEqual(TEXT("Proxy-to-Actor promotion keeps the same vertical motion"),
		ActivePose.GroundOffset, ProxyPose.GroundOffset);
	TestEqual(TEXT("Both representations derive the same resident height"),
		ActivePose.HeightScale, ProxyPose.HeightScale);

	ActiveEntry.bPlaceholderMoves = false;
	const double FrozenRouteAlpha = MotionState.RouteAlpha;
	FVisualResidentPresentationPlanner::AdvanceMotionState(
		ActiveEntry, 1.0, 150.0, MotionState);
	TestEqual(TEXT("A stationary Active action freezes the inherited route progress"),
		MotionState.RouteAlpha, FrozenRouteAlpha);
	const FVisualResidentMotionPose DowngradedPose =
		FVisualResidentPresentationPlanner::ResolveMotionPose(ProxyEntry, MotionState);
	const FVisualResidentMotionPose StationaryPose =
		FVisualResidentPresentationPlanner::ResolveMotionPose(ActiveEntry, MotionState);
	TestEqual(TEXT("Actor-to-proxy downgrade starts from the same route position"),
		DowngradedPose.Position, StationaryPose.Position);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7FAPresentationRateMotionTest,
	"AILODResearch.Phase7FA.PresentationRateMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7FAPresentationRateMotionTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualResidentPresentationEntry Entry;
	Entry.ResidentID = 8;
	Entry.RouteStart = FVector2D::ZeroVector;
	Entry.RouteEnd = FVector2D(10000.0, 0.0);
	Entry.bPlaceholderMoves = true;

	FVisualResidentMotionState OneX;
	OneX.ResidentID = Entry.ResidentID;
	OneX.RouteAlpha = 0.5;
	OneX.RouteDirection = 1;
	FVisualResidentMotionState FourX = OneX;
	FVisualResidentMotionState Paused = OneX;
	FVisualResidentPresentationPlanner::AdvanceMotionState(Entry, 0.1, 150.0, OneX);
	FVisualResidentPresentationPlanner::AdvanceMotionState(Entry, 0.4, 150.0, FourX);
	FVisualResidentPresentationPlanner::AdvanceMotionState(Entry, 0.0, 150.0, Paused);
	TestTrue(TEXT("4x advances the shared route four times as far as 1x"),
		FMath::IsNearlyEqual(
			FourX.RouteAlpha - 0.5,
			(OneX.RouteAlpha - 0.5) * 4.0));
	TestEqual(TEXT("Pause leaves the shared route progress unchanged"),
		Paused.RouteAlpha, 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7FABoundedGenericProxyTest,
	"AILODResearch.Phase7FA.BoundedGenericProxyMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7FABoundedGenericProxyTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 100;
	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error)
		|| !AdvanceToDayZero(Runtime, Error)
		|| !SubmitWideResidentView(Runtime, Error))
	{
		AddError(FString::Printf(TEXT("Phase 7F-A could not build its bounded presentation frame: %s"), *Error));
		return false;
	}

	FVisualResidentPresentationFrame Frame;
	if (!Runtime.CopyPresentationFrame(Frame)) return false;
	TestTrue(TEXT("The camera produces generic low-level proxies"), !Frame.LowLevelProxies.IsEmpty());
	for (const FVisualResidentPresentationEntry& Entry : Frame.LowLevelProxies)
	{
		TestTrue(TEXT("A low-level proxy receives only generic slow-walk motion"), Entry.bPlaceholderMoves);
		TestFalse(TEXT("Generic proxy motion does not claim exact Active state"), Entry.bHasActiveState);
	}
	TestTrue(TEXT("Visible proxy work stays within its independent fixed capacity"),
		Frame.LowLevelProxies.Num() <= Config.Presentation.LowLevelProxyCapacity);
	TestTrue(TEXT("Complete NPC work stays within the frozen 50-Actor capacity"),
		Frame.ActiveActors.Num() <= Config.Presentation.ActiveActorCapacity);
	TestTrue(TEXT("The bounded presentation frame never scans the full resident catalog"),
		!Frame.Diagnostics.bScannedResidentCatalog);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7FANormalObservationDwellTest,
	"AILODResearch.Phase7FA.NormalObservationDwellAndGrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7FANormalObservationDwellTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 100;
	Config.Observation.NormalActiveBudget = 4;
	Config.Observation.NormalPromotionDwellSeconds = 1.0;
	Config.Observation.NormalDemotionGraceSeconds = 2.0;
	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error) || !AdvanceToDayZero(Runtime, Error)) return false;

	FVisualObservationFrameInput View;
	View.bNormalViewUsesRadius = true;
	View.NormalView.Origin = Runtime.GetLayout().GetResidents()[0].ProxyPosition;
	View.NormalView.EnterDistance = 100000.0;
	View.RealDeltaSeconds = 0.4;
	if (!Runtime.SubmitObservationFrame(View, Error)) return false;
	FUnifiedDemoSnapshot BeforeDwell;
	if (!Runtime.CopySnapshot(BeforeDwell)) return false;
	TestEqual(TEXT("Visible proxies do not Lift before the normal observation dwell"),
		BeforeDwell.ActiveCount, 0);

	View.RealDeltaSeconds = 0.6;
	if (!Runtime.SubmitObservationFrame(View, Error)) return false;
	FUnifiedDemoSnapshot AfterDwell;
	if (!Runtime.CopySnapshot(AfterDwell)) return false;
	TestEqual(TEXT("Continuous observation fills only the bounded normal Active budget"),
		AfterDwell.ActiveCount, Config.Observation.NormalActiveBudget);
	const TArray<FResidentID> DwelledActiveIDs = Runtime.GetLastObservationPlan().ActiveRequest.DesiredActiveResidentIDs;

	View.NormalVisibleGroundPolygon =
	{
		FVector2D(100000000.0, 100000000.0),
		FVector2D(100001000.0, 100000000.0),
		FVector2D(100001000.0, 100001000.0),
		FVector2D(100000000.0, 100001000.0)
	};
	View.RealDeltaSeconds = 1.0;
	if (!Runtime.SubmitObservationFrame(View, Error)) return false;
	TestEqual(TEXT("A short loss of visibility retains the same Active residents"),
		Runtime.GetLastObservationPlan().ActiveRequest.DesiredActiveResidentIDs,
		DwelledActiveIDs);

	View.RealDeltaSeconds = 1.1;
	if (!Runtime.SubmitObservationFrame(View, Error)) return false;
	FUnifiedDemoSnapshot AfterGrace;
	if (!Runtime.CopySnapshot(AfterGrace)) return false;
	TestEqual(TEXT("Residents Restrict after the real-time demotion grace expires"),
		AfterGrace.ActiveCount, 0);
	TestFalse(TEXT("Dwell and grace never enable a full-population scan"),
		Runtime.GetCurrentPresentationObservationPlan().Diagnostics.NormalQuery.bScannedResidentCatalog);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7FAVisibleSelectionLiftTest,
	"AILODResearch.Phase7FA.VisibleFootprintAndSelectionLift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7FAVisibleSelectionLiftTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 100;
	Config.Observation.NormalPromotionDwellSeconds = 10.0;
	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error) || !AdvanceToDayZero(Runtime, Error)) return false;

	const FVector2D Target = Runtime.GetLayout().GetResidents()[0].ProxyPosition;
	FVisualObservationFrameInput View;
	View.bNormalViewUsesRadius = true;
	View.NormalView.Origin = Target;
	View.NormalView.EnterDistance = 100000.0;
	View.NormalVisibleGroundPolygon =
	{
		Target + FVector2D(-1000.0, -1000.0),
		Target + FVector2D(1000.0, -1000.0),
		Target + FVector2D(1000.0, 1000.0),
		Target + FVector2D(-1000.0, 1000.0)
	};
	if (!Runtime.SubmitObservationFrame(View, Error)) return false;
	FVisualResidentPresentationFrame ProxyFrame;
	if (!Runtime.CopyPresentationFrame(ProxyFrame) || ProxyFrame.LowLevelProxies.IsEmpty()) return false;
	for (const FVisualResidentPresentationEntry& Entry : ProxyFrame.LowLevelProxies)
	{
		TestTrue(TEXT("The screen-ground footprint rejects off-screen normal proxies"),
			FMath::Abs(Entry.ProxyPosition.X - Target.X) <= 1000.0
			&& FMath::Abs(Entry.ProxyPosition.Y - Target.Y) <= 1000.0);
	}

	const FResidentID SelectedResidentID = ProxyFrame.LowLevelProxies[0].ResidentID;
	if (!Runtime.RequestSelectedResident(SelectedResidentID, Error)
		|| !Runtime.SubmitObservationFrame(View, Error)) return false;
	FUnifiedDemoSnapshot SelectedSnapshot;
	FVisualResidentPresentationFrame SelectedFrame;
	if (!Runtime.CopySnapshot(SelectedSnapshot) || !Runtime.CopyPresentationFrame(SelectedFrame)) return false;
	TestTrue(TEXT("Selecting a visible proxy bypasses dwell and requests authoritative Lift"),
		SelectedSnapshot.ActiveResidents.ContainsByPredicate(
			[SelectedResidentID](const FUnifiedDemoResidentSnapshot& Resident)
			{
				return Resident.ResidentID == SelectedResidentID;
			}));
	TestTrue(TEXT("The selected presentation exposes exact Active state"),
		SelectedFrame.bHasSelectedResident && SelectedFrame.SelectedResident.bHasActiveState);
	TestFalse(TEXT("The selected ResidentID is never drawn as both proxy and Actor"),
		SelectedFrame.LowLevelProxies.ContainsByPredicate(
			[SelectedResidentID](const FVisualResidentPresentationEntry& Entry)
			{
				return Entry.ResidentID == SelectedResidentID;
			}));
	TestTrue(TEXT("Selection Lift stays within the frozen Active cap"),
		SelectedSnapshot.ActiveCount <= Config.Observation.ActiveHardCap);
	return true;
}

#endif
