// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Presentation/AILODVisualDemoRuntime.h"
#include "../Presentation/AILODVisualResidentPresentation.h"
#include "../Simulation/AILODPhase0Manifest.h"
#include "../Visual/AILODVisualDemoSettings.h"

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

	AILOD::FVisualObservationFrameInput MakeResidentView(const FVector2D& Target)
	{
		AILOD::FVisualObservationFrameInput View;
		View.NormalView.Origin = Target - FVector2D(15000.0, 0.0);
		View.NormalView.Forward = FVector2D(1.0, 0.0);
		View.NormalView.EnterDistance = 30000.0;
		View.NormalView.HalfAngleDegrees = 90.0;
		View.RealDeltaSeconds = 1.0;
		return View;
	}

	bool MakeFixedLayout(
		const int32 PopulationPerKingdom,
		AILOD::FVisualWorldLayout& OutLayout,
		FString& OutError)
	{
		AILOD::FPhase0Config SimulationConfig;
		SimulationConfig.Seed = 20260810;
		SimulationConfig.PopulationPerKingdom = PopulationPerKingdom;
		AILOD::FInitialPopulationManifest Population;
		AILOD::FEarthquakeDamageList Damage;
		AILOD::FPersistentTestPool PersistentPool;
		return AILOD::FPhase0ManifestGenerator::Generate(
			SimulationConfig, Population, Damage, PersistentPool, OutError)
			&& OutLayout.Build(Population, AILOD::FVisualWorldLayoutConfig{}, OutError);
	}

	TArray<AILOD::FResidentID> CopyActiveResidentIDs(
		const AILOD::FUnifiedDemoSnapshot& Snapshot)
	{
		TArray<AILOD::FResidentID> ResidentIDs;
		for (const AILOD::FUnifiedDemoResidentSnapshot& Resident : Snapshot.ActiveResidents)
		{
			ResidentIDs.Add(Resident.ResidentID);
		}
		return ResidentIDs;
	}

	TArray<AILOD::FResidentID> CopyPresentationResidentIDs(
		const TArray<AILOD::FVisualResidentPresentationEntry>& Entries)
	{
		TArray<AILOD::FResidentID> ResidentIDs;
		for (const AILOD::FVisualResidentPresentationEntry& Entry : Entries)
		{
			ResidentIDs.Add(Entry.ResidentID);
		}
		return ResidentIDs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7DRealResidentRepresentationTest,
	"AILODResearch.Phase7D.RealResidentRepresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7DRealResidentRepresentationTest::RunTest(const FString& Parameters)
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
		AddError(FString::Printf(TEXT("Phase 7D could not build its resident presentation frame: %s"), *Error));
		return false;
	}

	FUnifiedDemoSnapshot SimulationSnapshot;
	FVisualResidentPresentationFrame Frame;
	if (!Runtime.CopySnapshot(SimulationSnapshot) || !Runtime.CopyPresentationFrame(Frame)) return false;
	TestEqual(TEXT("Every Active resident gets one full NPC Actor entry"),
		Frame.ActiveActors.Num(), SimulationSnapshot.ActiveCount);
	TestTrue(TEXT("The full NPC Actor representation stays within the frozen cap"),
		Frame.ActiveActors.Num() <= 50);
	TestTrue(TEXT("The wide view retains low-level proxies beyond the 35 full Actors"),
		!Frame.LowLevelProxies.IsEmpty());

	TSet<FResidentID> DisplayedResidentIDs;
	for (const FVisualResidentPresentationEntry& Entry : Frame.ActiveActors)
	{
		TestTrue(TEXT("A full Actor entry contains a copied Active state"), Entry.bHasActiveState);
		TestEqual(TEXT("A full Actor and its copied state use the same ResidentID"),
			Entry.ResidentID, Entry.ActiveState.ResidentID);
		TestNotNull(TEXT("A full Actor ResidentID exists in the fixed layout"),
			Runtime.GetLayout().FindResident(Entry.ResidentID));
		TestFalse(TEXT("A full Actor ResidentID is not duplicated"), DisplayedResidentIDs.Contains(Entry.ResidentID));
		DisplayedResidentIDs.Add(Entry.ResidentID);
	}
	for (const FVisualResidentPresentationEntry& Entry : Frame.LowLevelProxies)
	{
		TestFalse(TEXT("A low-level proxy does not claim an Active state"), Entry.bHasActiveState);
		TestNotNull(TEXT("A low-level proxy ResidentID exists in the fixed layout"),
			Runtime.GetLayout().FindResident(Entry.ResidentID));
		TestFalse(TEXT("A proxy never duplicates an Active Actor or another proxy"),
			DisplayedResidentIDs.Contains(Entry.ResidentID));
		DisplayedResidentIDs.Add(Entry.ResidentID);
	}
	TestFalse(TEXT("The resident presentation frame rejects full-population camera scans"),
		Frame.Diagnostics.bScannedResidentCatalog);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7DActorPoolAtomicRebindingTest,
	"AILODResearch.Phase7D.ActorPoolAtomicRebinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7DActorPoolAtomicRebindingTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	TArray<FVisualResidentPresentationEntry> Entries;
	for (FResidentID ResidentID = 1; ResidentID <= 20; ++ResidentID)
	{
		FVisualResidentPresentationEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.ResidentID = ResidentID;
		Entry.bActiveActor = true;
		Entry.bHasActiveState = true;
		Entry.ActiveState.ResidentID = ResidentID;
	}

	FVisualActorPoolPlanner Pool;
	FVisualActorPoolPlan FirstPlan;
	FString Error;
	TArray<FVisualResidentPresentationEntry> FirstEntries;
	FirstEntries.Append(&Entries[0], 10);
	if (!Pool.Reconcile(FirstEntries, FirstPlan, Error)) return false;
	TestEqual(TEXT("The initial ten residents bind ten reusable Actor slots"), FirstPlan.BoundCount, 10);
	TestEqual(TEXT("The initial assignment performs ten bindings"), FirstPlan.ReboundCount, 10);

	const TArray<FResidentID> FirstSlots = FirstPlan.SlotResidentIDs;
	FVisualActorPoolPlan IdempotentPlan;
	Algo::Reverse(FirstEntries);
	if (!Pool.Reconcile(FirstEntries, IdempotentPlan, Error)) return false;
	TestEqual(TEXT("Reordering the same Active set keeps every Actor slot stable"),
		IdempotentPlan.SlotResidentIDs, FirstSlots);
	TestEqual(TEXT("An unchanged Active set performs no rebind"), IdempotentPlan.ReboundCount, 0);
	TestEqual(TEXT("An unchanged Active set releases no slot"), IdempotentPlan.ReleasedCount, 0);

	TArray<FVisualResidentPresentationEntry> ReplacementEntries;
	ReplacementEntries.Append(&Entries[5], 10);
	FVisualActorPoolPlan ReplacementPlan;
	if (!Pool.Reconcile(ReplacementEntries, ReplacementPlan, Error)) return false;
	TestEqual(TEXT("Replacing five residents releases five old slots"), ReplacementPlan.ReleasedCount, 5);
	TestEqual(TEXT("Replacing five residents binds five free slots"), ReplacementPlan.ReboundCount, 5);
	for (FResidentID ResidentID = 6; ResidentID <= 10; ++ResidentID)
	{
		TestEqual(TEXT("Residents retained by the replacement keep their original slot"),
			ReplacementPlan.SlotResidentIDs.IndexOfByKey(ResidentID),
			FirstSlots.IndexOfByKey(ResidentID));
	}

	TArray<FVisualResidentPresentationEntry> OverflowEntries;
	for (FResidentID ResidentID = 1; ResidentID <= 51; ++ResidentID)
	{
		FVisualResidentPresentationEntry& Entry = OverflowEntries.AddDefaulted_GetRef();
		Entry.ResidentID = ResidentID;
		Entry.bActiveActor = true;
		Entry.bHasActiveState = true;
		Entry.ActiveState.ResidentID = ResidentID;
	}
	const TArray<FResidentID> BeforeRejectedPlan = Pool.GetSlotResidentIDs();
	FVisualActorPoolPlan RejectedPlan;
	TestFalse(TEXT("A 51-Actor request is rejected"), Pool.Reconcile(OverflowEntries, RejectedPlan, Error));
	TestEqual(TEXT("A rejected request leaves the previous pool binding unchanged"),
		Pool.GetSlotResidentIDs(), BeforeRejectedPlan);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7DProxyStableSlotsTest,
	"AILODResearch.Phase7D.ProxyStableSlotsIncremental",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7DProxyStableSlotsTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	TArray<FVisualResidentPresentationEntry> Entries;
	for (FResidentID ResidentID = 1; ResidentID <= 15; ++ResidentID)
	{
		FVisualResidentPresentationEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.ResidentID = ResidentID;
		Entry.ProxyPosition = FVector2D(ResidentID * 100.0, 0.0);
	}

	FVisualProxySlotPlanner Planner(16);
	FVisualProxySlotPlan FirstPlan;
	FString Error;
	TArray<FVisualResidentPresentationEntry> FirstEntries;
	FirstEntries.Append(&Entries[0], 10);
	if (!Planner.Reconcile(FirstEntries, FirstPlan, Error)) return false;
	TestEqual(TEXT("The proxy presenter keeps its fixed slot capacity"), FirstPlan.SlotResidentIDs.Num(), 16);
	TestEqual(TEXT("Ten proxy entries occupy ten fixed slots"), FirstPlan.VisibleCount, 10);
	const TArray<FResidentID> FirstSlots = FirstPlan.SlotResidentIDs;

	Algo::Reverse(FirstEntries);
	FVisualProxySlotPlan ReorderedPlan;
	if (!Planner.Reconcile(FirstEntries, ReorderedPlan, Error)) return false;
	TestEqual(TEXT("Reordering the same proxy set preserves every instance index"),
		ReorderedPlan.SlotResidentIDs, FirstSlots);
	TestEqual(TEXT("An unchanged proxy set performs no rebind"), ReorderedPlan.ReboundCount, 0);
	TestEqual(TEXT("An unchanged proxy set releases no slot"), ReorderedPlan.ReleasedCount, 0);

	TArray<FVisualResidentPresentationEntry> ReplacementEntries;
	ReplacementEntries.Append(&Entries[5], 10);
	FVisualProxySlotPlan ReplacementPlan;
	if (!Planner.Reconcile(ReplacementEntries, ReplacementPlan, Error)) return false;
	TestEqual(TEXT("Replacing five proxies releases only five slots"), ReplacementPlan.ReleasedCount, 5);
	TestEqual(TEXT("Replacing five proxies binds only five slots"), ReplacementPlan.ReboundCount, 5);
	for (FResidentID ResidentID = 6; ResidentID <= 10; ++ResidentID)
	{
		TestEqual(TEXT("A retained proxy keeps its instanced-renderer slot index"),
			ReplacementPlan.SlotResidentIDs.IndexOfByKey(ResidentID),
			FirstSlots.IndexOfByKey(ResidentID));
	}

	TArray<FVisualResidentPresentationEntry> OverflowEntries = Entries;
	FVisualResidentPresentationEntry& Overflow = OverflowEntries.AddDefaulted_GetRef();
	Overflow.ResidentID = 16;
	FVisualResidentPresentationEntry& TooMany = OverflowEntries.AddDefaulted_GetRef();
	TooMany.ResidentID = 17;
	const TArray<FResidentID> BeforeRejected = Planner.GetSlotResidentIDs();
	FVisualProxySlotPlan RejectedPlan;
	TestFalse(TEXT("A proxy request above fixed capacity is rejected"),
		Planner.Reconcile(OverflowEntries, RejectedPlan, Error));
	TestEqual(TEXT("A rejected proxy request preserves the previous slot mapping"),
		Planner.GetSlotResidentIDs(), BeforeRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7DProxySelectionLiftTest,
	"AILODResearch.Phase7D.ProxySelectionPromotesReadOnlyView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7DProxySelectionLiftTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 100;
	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error)
		|| !AdvanceToDayZero(Runtime, Error)
		|| !SubmitWideResidentView(Runtime, Error)) return false;

	FVisualResidentPresentationFrame BeforeFrame;
	if (!Runtime.CopyPresentationFrame(BeforeFrame)
		|| BeforeFrame.LowLevelProxies.IsEmpty()) return false;
	const FResidentID ProxyResidentID = BeforeFrame.LowLevelProxies[0].ResidentID;
	if (!Runtime.RequestSelectedResident(ProxyResidentID, Error)) return false;
	FVisualObservationFrameInput SelectedOnlyView;
	SelectedOnlyView.bNormalViewEnabled = false;
	if (!Runtime.SubmitObservationFrame(SelectedOnlyView, Error)) return false;
	FUnifiedDemoSnapshot AfterSnapshot;
	FVisualResidentPresentationFrame AfterFrame;
	if (!Runtime.CopySnapshot(AfterSnapshot) || !Runtime.CopyPresentationFrame(AfterFrame)) return false;
	TestTrue(TEXT("Selecting a displayed proxy makes its next observation request Active"),
		AfterSnapshot.ActiveResidents.ContainsByPredicate(
			[ProxyResidentID](const FUnifiedDemoResidentSnapshot& Resident)
			{
				return Resident.ResidentID == ProxyResidentID;
			}));
	TestTrue(TEXT("The selected resident is exposed through a copied presentation value"),
		AfterFrame.bHasSelectedResident && AfterFrame.SelectedResidentID == ProxyResidentID);
	TestTrue(TEXT("A selected resident exposes exact state only after authoritative Lift"),
		AfterFrame.SelectedResident.bHasActiveState);

	AfterFrame.SelectedResident.HomeID = -1;
	FVisualResidentPresentationFrame FreshFrame;
	if (!Runtime.CopyPresentationFrame(FreshFrame)) return false;
	TestNotEqual(TEXT("A caller cannot mutate selection data through its copied frame"),
		FreshFrame.SelectedResident.HomeID, FHomeID(-1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7DAnchorAndPlaceholderRouteTest,
	"AILODResearch.Phase7D.AnchorAndPlaceholderRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7DAnchorAndPlaceholderRouteTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualWorldLayout Layout;
	FString Error;
	if (!MakeFixedLayout(100, Layout, Error)) return false;

	FUnifiedDemoSnapshot Snapshot;
	Snapshot.PopulationPerKingdom = 100;
	const EIndividualAction Actions[] =
	{
		EIndividualAction::ContinueRepair,
		EIndividualAction::Work,
		EIndividualAction::BuyWood,
		EIndividualAction::ChopWood,
		EIndividualAction::Wait,
		EIndividualAction::Routine
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Actions); ++Index)
	{
		const FVisualResidentPlacement& Placement = Layout.GetResidents()[Index];
		FUnifiedDemoResidentSnapshot& Resident = Snapshot.ActiveResidents.AddDefaulted_GetRef();
		Resident.ResidentID = Placement.ResidentID;
		Resident.HomeID = Placement.HomeID;
		Resident.AppearanceSeed = 1000 + Index;
		Resident.CurrentAction = Actions[Index];
	}
	Snapshot.ActiveCount = Snapshot.ActiveResidents.Num();

	FVisualResidentPresentationConfig Config;
	FVisualObservationPlan EmptyObservationPlan;
	FVisualResidentPresentationFrame Frame;
	if (!FVisualResidentPresentationPlanner::BuildFrame(
		Layout, EmptyObservationPlan, Snapshot, 0, Config, Frame, Error)) return false;
	TestEqual(TEXT("The synthetic snapshot maps six real residents"), Frame.ActiveActors.Num(), 6);
	if (Frame.ActiveActors.Num() != 6) return false;

	const FVisualHomeSlotRecord* RepairHome = Layout.FindHomeSlot(Frame.ActiveActors[0].VisualHomeSlotID);
	const FVisualWorkAnchorRecord* WorkAnchor = Layout.FindWorkAnchor(Frame.ActiveActors[1].WorkAnchorID);
	const FVisualResidentPlacement* BuyWoodPlacement = Layout.FindResident(Frame.ActiveActors[2].ResidentID);
	const FVisualResidentPlacement* ChopWoodPlacement = Layout.FindResident(Frame.ActiveActors[3].ResidentID);
	if (RepairHome == nullptr || WorkAnchor == nullptr
		|| BuyWoodPlacement == nullptr || ChopWoodPlacement == nullptr) return false;
	const FVisualWorkAnchorRecord* TimberPurchase = Layout.FindWorkAnchor(
		BuyWoodPlacement->DistrictID,
		EVisualWorkAnchorType::TimberPurchase);
	const FVisualWorkAnchorRecord* LumberCamp = Layout.FindWorkAnchor(
		ChopWoodPlacement->DistrictID,
		EVisualWorkAnchorType::LumberCamp);
	if (TimberPurchase == nullptr || LumberCamp == nullptr) return false;
	TestEqual(TEXT("A repair action faces the resident's fixed visual home slot"),
		Frame.ActiveActors[0].DestinationPosition, RepairHome->Position);
	TestEqual(TEXT("A work action faces the resident's fixed work anchor"),
		Frame.ActiveActors[1].DestinationPosition, WorkAnchor->Position);
	TestEqual(TEXT("BuyWood maps to the district's timber-purchase anchor"),
		Frame.ActiveActors[2].DestinationPosition, TimberPurchase->Position);
	TestEqual(TEXT("ChopWood maps to the district's lumber-camp anchor"),
		Frame.ActiveActors[3].DestinationPosition, LumberCamp->Position);
	TestFalse(TEXT("Wait uses a stationary placeholder"), Frame.ActiveActors[4].bPlaceholderMoves);
	TestTrue(TEXT("Routine permits only a short local walking placeholder"),
		Frame.ActiveActors[5].bPlaceholderMoves);
	for (const FVisualResidentPresentationEntry& Entry : Frame.ActiveActors)
	{
		TestTrue(TEXT("A placeholder route stays within the configured short road window"),
			FVector2D::Distance(Entry.RouteStart, Entry.RouteEnd)
			<= Config.LocalRouteHalfLength * 2.0 + 1.0);
	}

	FVisualResidentPresentationEntry MovingEntry = Frame.ActiveActors[5];
	FVisualResidentPresentationEntry StationaryEntry = MovingEntry;
	StationaryEntry.bPlaceholderMoves = false;
	const double RouteAlpha = 0.37;
	TestEqual(TEXT("An action transition keeps the same resident at the same local-route position"),
		FVisualResidentPresentationPlanner::ResolveLocalRoutePosition(MovingEntry, RouteAlpha),
		FVisualResidentPresentationPlanner::ResolveLocalRoutePosition(StationaryEntry, RouteAlpha));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7DPresentationPlaybackRateTest,
	"AILODResearch.Phase7D.PresentationPlaybackRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7DPresentationPlaybackRateTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 100;
	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error) || !AdvanceToDayZero(Runtime, Error)) return false;
	TestEqual(TEXT("The running presentation starts at 1x"), Runtime.GetPresentationPlaybackRate(), 1.0);
	if (!Runtime.RequestTimeScale(2, Error)) return false;
	TestEqual(TEXT("The 2x request also scales placeholder presentation time"),
		Runtime.GetPresentationPlaybackRate(), 2.0);
	if (!Runtime.RequestTimeScale(4, Error)) return false;
	TestEqual(TEXT("The 4x request also scales placeholder presentation time"),
		Runtime.GetPresentationPlaybackRate(), 4.0);
	if (!Runtime.RequestPaused(true, Error)) return false;
	TestEqual(TEXT("Pause freezes placeholder presentation time"),
		Runtime.GetPresentationPlaybackRate(), 0.0);
	if (!Runtime.RequestPaused(false, Error)) return false;
	TestEqual(TEXT("Resume restores the selected presentation rate"),
		Runtime.GetPresentationPlaybackRate(), 4.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7DRejectedObservationKeepsRunningTest,
	"AILODResearch.Phase7D.RejectedObservationKeepsRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7DRejectedObservationKeepsRunningTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualDemoRuntimeConfig Config;
	Config.PopulationPerKingdom = 100;
	Config.Layout.ResidentsPerDistrict = 50;
	FVisualDemoRuntime Runtime;
	FString Error;
	if (!Runtime.Initialize(Config, Error) || !AdvanceToDayZero(Runtime, Error)) return false;
	if (!Runtime.RequestPaused(true, Error)) return false;
	const TArray<FVisualResidentPlacement>& Residents = Runtime.GetLayout().GetResidents();
	TArray<const FVisualResidentPlacement*> DistrictTargets;
	TSet<FVisualDistrictID> SeenDistricts;
	for (const FVisualResidentPlacement& Resident : Residents)
	{
		if (SeenDistricts.Contains(Resident.DistrictID)) continue;
		SeenDistricts.Add(Resident.DistrictID);
		DistrictTargets.Add(&Resident);
		if (DistrictTargets.Num() == 3) break;
	}
	if (DistrictTargets.Num() < 3) return false;
	const FVisualObservationFrameInput BaselineView = MakeResidentView(DistrictTargets[0]->ProxyPosition);
	const FVisualObservationFrameInput ViewA = MakeResidentView(DistrictTargets[1]->ProxyPosition);
	const FVisualObservationFrameInput ViewB = MakeResidentView(DistrictTargets[2]->ProxyPosition);
	if (!Runtime.SubmitObservationFrame(BaselineView, Error)) return false;

	FUnifiedDemoSnapshot BeforeSnapshot;
	FVisualResidentPresentationFrame BeforeFrame;
	const FVisualObservationPlan BeforePlan = Runtime.GetLastObservationPlan();
	if (!Runtime.CopySnapshot(BeforeSnapshot)
		|| !Runtime.CopyPresentationFrame(BeforeFrame)) return false;
	TestTrue(TEXT("The rejected replacement fixture starts with a non-empty authoritative Active set"),
		!BeforeSnapshot.ActiveResidents.IsEmpty());
	Runtime.RejectNextObservationCommitForTest();
	if (!Runtime.SubmitObservationFrame(ViewA, Error)) return false;

	FUnifiedDemoSnapshot ViewASnapshot;
	FVisualResidentPresentationFrame ViewAFrame;
	const FVisualObservationPlan ViewACommittedPlan = Runtime.GetLastObservationPlan();
	const FVisualObservationPlan ViewAPresentationPlan = Runtime.GetCurrentPresentationObservationPlan();
	if (!Runtime.CopySnapshot(ViewASnapshot)
		|| !Runtime.CopyPresentationFrame(ViewAFrame)) return false;
	const TArray<FResidentID> ViewAProxyIDs = CopyPresentationResidentIDs(ViewAFrame.LowLevelProxies);
	TestTrue(TEXT("The injected atomic replacement rejection is reported as a warning"),
		!Runtime.GetLastObservationWarning().IsEmpty());
	TestEqual(TEXT("A rejected replacement keeps the Demo running"),
		Runtime.GetState(), EVisualDemoRuntimeState::Running);
	TestEqual(TEXT("A rejected replacement keeps the authoritative Active snapshot"),
		CopyActiveResidentIDs(ViewASnapshot), CopyActiveResidentIDs(BeforeSnapshot));
	TestEqual(TEXT("A rejected replacement keeps the committed presentation Active residents"),
		CopyPresentationResidentIDs(ViewAFrame.ActiveActors),
		CopyPresentationResidentIDs(BeforeFrame.ActiveActors));
	TestTrue(TEXT("A rejected replacement still publishes current-view low-level proxies"),
		!ViewAProxyIDs.IsEmpty());
	TestFalse(TEXT("A rejected replacement does not freeze the previous proxy frame"),
		ViewAProxyIDs == CopyPresentationResidentIDs(BeforeFrame.LowLevelProxies));
	TestEqual(TEXT("A rejected replacement keeps the committed observation request"),
		ViewACommittedPlan.ActiveRequest.DesiredActiveResidentIDs,
		BeforePlan.ActiveRequest.DesiredActiveResidentIDs);
	TestEqual(TEXT("A rejected replacement keeps the committed tracked ResidentID"),
		ViewACommittedPlan.TrackedResidentID,
		BeforePlan.TrackedResidentID);
	TestFalse(TEXT("A deferred replacement never enables a full-population scan"),
		ViewAFrame.Diagnostics.bScannedResidentCatalog);
	TestFalse(TEXT("The current proxy plan remains spatially bounded after rejection"),
		ViewAPresentationPlan.Diagnostics.NormalQuery.bScannedResidentCatalog);
	TestTrue(TEXT("A proxy-only publication does not commit the rejected Active history"),
		ViewAPresentationPlan.Diagnostics.bActiveSetChanged);
	TSet<FResidentID> ViewAActiveIDs;
	for (const FVisualResidentPresentationEntry& Entry : ViewAFrame.ActiveActors)
	{
		ViewAActiveIDs.Add(Entry.ResidentID);
	}
	for (const FVisualResidentPresentationEntry& Entry : ViewAFrame.LowLevelProxies)
	{
		TestFalse(TEXT("A rejected replacement never duplicates one ResidentID as proxy and Actor"),
			ViewAActiveIDs.Contains(Entry.ResidentID));
	}

	if (!Runtime.SubmitObservationFrame(ViewB, Error)) return false;
	FUnifiedDemoSnapshot ViewBSnapshot;
	FVisualResidentPresentationFrame ViewBFrame;
	if (!Runtime.CopySnapshot(ViewBSnapshot)
		|| !Runtime.CopyPresentationFrame(ViewBFrame)) return false;
	const TArray<FResidentID> ViewBProxyIDs = CopyPresentationResidentIDs(ViewBFrame.LowLevelProxies);
	TestTrue(TEXT("A suppressed retry still publishes proxies for a different paused-camera view"),
		!ViewBProxyIDs.IsEmpty());
	TestFalse(TEXT("Moving the paused camera changes the bounded proxy set"),
		ViewBProxyIDs == ViewAProxyIDs);
	TestEqual(TEXT("Proxy-only camera movement keeps the authoritative Active snapshot"),
		CopyActiveResidentIDs(ViewBSnapshot), CopyActiveResidentIDs(BeforeSnapshot));
	TestEqual(TEXT("Proxy-only camera movement does not advance game time"),
		ViewBSnapshot.GameTime.Minutes, BeforeSnapshot.GameTime.Minutes);

	if (!Runtime.SubmitObservationFrame(ViewA, Error)) return false;
	FUnifiedDemoSnapshot ReturnedSnapshot;
	FVisualResidentPresentationFrame ReturnedFrame;
	if (!Runtime.CopySnapshot(ReturnedSnapshot)
		|| !Runtime.CopyPresentationFrame(ReturnedFrame)) return false;
	TestEqual(TEXT("Returning the paused camera restores the same real proxy ResidentIDs"),
		CopyPresentationResidentIDs(ReturnedFrame.LowLevelProxies), ViewAProxyIDs);
	TestEqual(TEXT("The A-B-A proxy round trip keeps game time fixed"),
		ReturnedSnapshot.GameTime.Minutes, BeforeSnapshot.GameTime.Minutes);
	TestEqual(TEXT("The A-B-A proxy round trip keeps the committed Active request"),
		Runtime.GetLastObservationPlan().ActiveRequest.DesiredActiveResidentIDs,
		BeforePlan.ActiveRequest.DesiredActiveResidentIDs);
	TestTrue(TEXT("The deferred warning remains visible until authority can retry"),
		!Runtime.GetLastObservationWarning().IsEmpty());

	if (!Runtime.SubmitObservationFrame(BaselineView, Error)) return false;
	TestTrue(TEXT("Returning to the authoritative view clears the transient warning"),
		Runtime.GetLastObservationWarning().IsEmpty());
	TestEqual(TEXT("Returning to the authoritative view keeps the Demo running"),
		Runtime.GetState(), EVisualDemoRuntimeState::Running);
	FUnifiedDemoSnapshot RecoveredSnapshot;
	FVisualResidentPresentationFrame RecoveredFrame;
	if (!Runtime.CopySnapshot(RecoveredSnapshot)
		|| !Runtime.CopyPresentationFrame(RecoveredFrame)) return false;
	TestEqual(TEXT("The authoritative snapshot remains aligned with the committed request"),
		CopyActiveResidentIDs(RecoveredSnapshot),
		Runtime.GetLastObservationPlan().ActiveRequest.DesiredActiveResidentIDs);
	TestEqual(TEXT("Full NPC Actors remain aligned with the authoritative snapshot"),
		CopyPresentationResidentIDs(RecoveredFrame.ActiveActors),
		CopyActiveResidentIDs(RecoveredSnapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7DSharedPresentationSettingsTest,
	"AILODResearch.Phase7D.SharedPresentationSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7DSharedPresentationSettingsTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const UAILODVisualDemoSettings* Settings = GetDefault<UAILODVisualDemoSettings>();
	const FVisualDemoRuntimeConfig Config = Settings->MakeRuntimeConfig();
	TestEqual(TEXT("The normal camera selects at most 35 complete NPC Actors"),
		Config.Observation.NormalActiveBudget, 35);
	TestEqual(TEXT("The low-level proxy budget is explicit Demo configuration"),
		Config.Observation.NormalProxyBudget, 128);
	TestTrue(TEXT("Normal observation uses a positive real-time dwell before distant promotion"),
		Config.Observation.NormalPromotionDwellSeconds > 0.0);
	TestTrue(TEXT("Normal observation keeps a longer demotion grace than promotion dwell"),
		Config.Observation.NormalDemotionGraceSeconds
		> Config.Observation.NormalPromotionDwellSeconds);
	TestEqual(TEXT("The full NPC Actor pool keeps the frozen capacity"),
		Config.Presentation.ActiveActorCapacity, 50);
	TestTrue(TEXT("The configured low-level proxy capacity covers normal and telescope candidates"),
		Config.Presentation.LowLevelProxyCapacity
		>= Config.Observation.NormalProxyBudget + Config.Observation.TelescopeProxyBudget);
	return true;
}

#endif
