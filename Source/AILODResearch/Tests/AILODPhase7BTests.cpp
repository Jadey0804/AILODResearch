// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Presentation/AILODVisualObservationPlanner.h"
#include "../Presentation/AILODVisualWorldLayout.h"
#include "../Simulation/AILODUnifiedSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	AILOD::FInitialPopulationManifest MakeVisualPopulation(const int32 PopulationPerKingdom)
	{
		using namespace AILOD;
		FInitialPopulationManifest Population;
		Population.Seed = 20260810;
		Population.PopulationPerKingdom = PopulationPerKingdom;
		Population.ConfigHash = FString::Printf(TEXT("phase7b-fixture-%d"), PopulationPerKingdom);
		Population.Residents.Reserve(PopulationPerKingdom * 2);
		FResidentID ResidentID = 1;
		for (int32 KingdomIndex = 0; KingdomIndex < 2; ++KingdomIndex)
		{
			for (int32 LocalIndex = 0; LocalIndex < PopulationPerKingdom; ++LocalIndex)
			{
				FInitialResidentRecord& Resident = Population.Residents.AddDefaulted_GetRef();
				Resident.ResidentID = ResidentID;
				Resident.HomeID = 1000000 + ResidentID;
				Resident.PersistentID = 2000000 + ResidentID;
				Resident.Name = MakeStableResidentName(ResidentID);
				Resident.Kingdom = KingdomIndex == 0 ? EKingdom::A : EKingdom::B;
				Resident.Profession = LocalIndex % 4 == 0 ? EProfession::Logger : EProfession::Worker;
				Resident.IncomeBand = LocalIndex % 5 == 0 ? EIncomeBand::NonLow : EIncomeBand::Low;
				++ResidentID;
			}
		}
		return Population;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7BFixedLayoutContractTest,
	"AILODResearch.Phase7B.FixedLayoutContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7BFixedLayoutContractTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const FInitialPopulationManifest Population = MakeVisualPopulation(10000);
	FVisualWorldLayout LayoutA;
	FVisualWorldLayout LayoutB;
	FVisualWorldLayout LayoutDifferentSeed;
	FVisualWorldLayoutConfig Config;
	FString Error;
	if (!LayoutA.Build(Population, Config, Error) || !LayoutB.Build(Population, Config, Error))
	{
		AddError(FString::Printf(TEXT("The fixed visual layout could not build: %s"), *Error));
		return false;
	}
	FVisualWorldLayoutConfig DifferentSeedConfig = Config;
	++DifferentSeedConfig.LayoutSeed;
	if (!LayoutDifferentSeed.Build(Population, DifferentSeedConfig, Error)) return false;

	TestEqual(TEXT("20k residents are retained as static visual identities"), LayoutA.GetResidents().Num(), 20000);
	TestEqual(TEXT("20k expands to four districts per kingdom"), LayoutA.GetDistricts().Num(), 8);
	TestEqual(TEXT("Each district exposes four semantic road segments"), LayoutA.GetRoads().Num(), 32);
	TestEqual(TEXT("Visible home slots are shared instead of creating one building per resident"),
		LayoutA.GetHomeSlots().Num(), 512);
	TestEqual(TEXT("Each district exposes lumber, purchase, and market anchors"), LayoutA.GetWorkAnchors().Num(), 24);
	TestTrue(TEXT("The same population, layout version, and seed reproduce the same layout digest"),
		LayoutA.BuildDeterministicDigest() == LayoutB.BuildDeterministicDigest());
	TestTrue(TEXT("A different layout seed changes the presentation digest"),
		LayoutA.BuildDeterministicDigest() != LayoutDifferentSeed.BuildDeterministicDigest());

	const FVisualResidentPlacement* Resident = LayoutA.FindResident(12345);
	TestNotNull(TEXT("Every real ResidentID has one fixed presentation placement"), Resident);
	if (Resident == nullptr) return false;
	TestNotNull(TEXT("A resident's shared VisualHomeSlotID resolves without scanning mesh actors"),
		LayoutA.FindHomeSlot(Resident->VisualHomeSlotID));
	TestNotNull(TEXT("A resident's proxy road resolves from the same fixed layout"),
		LayoutA.FindRoad(Resident->ProxyRoadID));
	TestNotNull(TEXT("A resident's work anchor resolves from the fixed layout"),
		LayoutA.FindWorkAnchor(Resident->WorkAnchorID));
	FVisualHomeSlotID MappedVisualHomeSlotID = 0;
	TestTrue(TEXT("The resident's logical HomeID resolves to the same shared visual home slot"),
		LayoutA.FindVisualHomeSlotForHome(Resident->HomeID, MappedVisualHomeSlotID));
	TestEqual(TEXT("HomeID and ResidentID views agree on the visual home slot"),
		MappedVisualHomeSlotID, Resident->VisualHomeSlotID);
	TestTrue(TEXT("The fixed layout uses far fewer presentation homes than logical residents"),
		LayoutA.GetHomeSlots().Num() < LayoutA.GetResidents().Num() / 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7BSpatialHysteresisTest,
	"AILODResearch.Phase7B.SpatialHysteresisAndTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7BSpatialHysteresisTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualWorldLayout Layout;
	FString Error;
	if (!Layout.Build(MakeVisualPopulation(1000), FVisualWorldLayoutConfig{}, Error)) return false;
	FVisualObservationPlannerConfig PlannerConfig;
	PlannerConfig.NormalProxyBudget = 1;
	PlannerConfig.NormalActiveBudget = 1;
	FVisualObservationPlanner Planner(Layout, PlannerConfig);

	const FVisualResidentPlacement& InitialTarget = Layout.GetResidents()[100];
	FVisualObservationFrameInput Input;
	Input.NormalView.Origin = InitialTarget.ProxyPosition - FVector2D(9000.0, 0.0);
	Input.NormalView.Forward = FVector2D(1.0, 0.0);
	Input.NormalView.EnterDistance = 10000.0;
	Input.NormalView.HalfAngleDegrees = 0.25;
	Input.RealDeltaSeconds = 1.0;
	FVisualObservationPlan First;
	if (!Planner.PlanFrame(Input, First, Error)) return false;
	TestEqual(TEXT("The one-person normal Active budget returns one real resident"),
		First.ActiveRequest.DesiredActiveResidentIDs.Num(), 1);
	if (First.ActiveRequest.DesiredActiveResidentIDs.Num() != 1) return false;
	const FResidentID RetainedResidentID = First.ActiveRequest.DesiredActiveResidentIDs[0];
	const FVisualResidentPlacement* RetainedResident = Layout.FindResident(RetainedResidentID);
	if (RetainedResident == nullptr) return false;

	Input.NormalView.Origin = RetainedResident->ProxyPosition - FVector2D(11000.0, 0.0);
	FVisualObservationPlan InsideExitBand;
	if (!Planner.PlanFrame(Input, InsideExitBand, Error)) return false;
	TestTrue(TEXT("A previous selection stays active inside the 20 percent exit band"),
		InsideExitBand.ActiveRequest.DesiredActiveResidentIDs.Contains(RetainedResidentID));
	TestTrue(TEXT("The retained proxy explicitly reports hysteresis"),
		!InsideExitBand.NormalProxyCandidates.IsEmpty()
		&& InsideExitBand.NormalProxyCandidates[0].ResidentID == RetainedResidentID
		&& InsideExitBand.NormalProxyCandidates[0].bRetainedByHysteresis);

	Input.NormalView.Origin = RetainedResident->ProxyPosition - FVector2D(13000.0, 0.0);
	FVisualObservationPlan OutsideExitBand;
	if (!Planner.PlanFrame(Input, OutsideExitBand, Error)) return false;
	TestFalse(TEXT("The previous selection leaves after crossing the 20 percent exit band"),
		OutsideExitBand.ActiveRequest.DesiredActiveResidentIDs.Contains(RetainedResidentID));
	TestFalse(TEXT("A view query never scans the complete resident catalog"),
		OutsideExitBand.Diagnostics.NormalQuery.bScannedResidentCatalog);
	TestTrue(TEXT("A local view visits fewer resident entries than the 2k catalog"),
		OutsideExitBand.Diagnostics.NormalQuery.VisitedResidentEntryCount < Layout.GetResidents().Num());

	if (!Planner.SetTrackedResident(RetainedResidentID, Error)) return false;
	FVisualObservationFrameInput NoViews;
	NoViews.bNormalViewEnabled = false;
	FVisualObservationPlan TrackedOffscreen;
	if (!Planner.PlanFrame(NoViews, TrackedOffscreen, Error)) return false;
	TestTrue(TEXT("The one tracked resident stays in the Active request while offscreen"),
		TrackedOffscreen.ActiveRequest.DesiredActiveResidentIDs.Contains(RetainedResidentID));
	TestEqual(TEXT("Tracking preserves the same fixed spatial identity"),
		TrackedOffscreen.TrackedPosition, RetainedResident->ProxyPosition);
	Planner.ClearTrackedResident();
	FVisualObservationPlan Cleared;
	if (!Planner.PlanFrame(NoViews, Cleared, Error)) return false;
	TestEqual(TEXT("Clearing tracking releases an otherwise offscreen resident"),
		Cleared.ActiveRequest.DesiredActiveResidentIDs.Num(), 0);
	TestTrue(TEXT("A tracking-only change still requires an authoritative request"),
		Cleared.Diagnostics.bActiveSetChanged);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7BTelescopeSessionBridgeTest,
	"AILODResearch.Phase7B.TelescopeSessionBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7BTelescopeSessionBridgeTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FVisualWorldLayout Layout;
	FString Error;
	if (!Layout.Build(MakeVisualPopulation(100), FVisualWorldLayoutConfig{}, Error)) return false;
	FVisualObservationPlanner Planner(Layout);
	const FVisualResidentPlacement& AimResident = Layout.GetResidents()[50];
	FVisualObservationFrameInput Input;
	Input.bNormalViewEnabled = false;
	Input.bTelescopeEnabled = true;
	Input.TelescopeView.Origin = AimResident.ProxyPosition - FVector2D(50000.0, 0.0);
	Input.TelescopeView.Forward = FVector2D(1.0, 0.0);
	Input.TelescopeView.EnterDistance = 60000.0;
	Input.TelescopeView.HalfAngleDegrees = 0.1;
	FVisualObservationPlan CandidatePlan;
	if (!Planner.PlanFrame(Input, CandidatePlan, Error)) return false;
	TestTrue(TEXT("The long-range telescope finds a real center ResidentID"),
		CandidatePlan.TelescopeCenterResidentID > 0
		&& Layout.FindResident(CandidatePlan.TelescopeCenterResidentID) != nullptr);
	if (CandidatePlan.TelescopeCenterResidentID <= 0) return false;

	if (!Planner.SetTrackedResident(CandidatePlan.TelescopeCenterResidentID, Error)) return false;
	Input.TelescopePromotionResidentID = CandidatePlan.TelescopeCenterResidentID;
	FVisualObservationPlan PromotionPlan;
	if (!Planner.PlanFrame(Input, PromotionPlan, Error))
	{
		AddError(FString::Printf(TEXT("Telescope promotion planning failed: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("Telescope promotion includes the center resident"),
		PromotionPlan.ActiveRequest.DesiredActiveResidentIDs.Contains(Input.TelescopePromotionResidentID));
	TestTrue(TEXT("Telescope promotion and tracking stay within the five-person telescope budget"),
		PromotionPlan.ActiveRequest.DesiredActiveResidentIDs.Num() <= 5);
	TestEqual(TEXT("The promoted center becomes the sole tracked ResidentID"),
		PromotionPlan.ActiveRequest.TrackedResidentID, Input.TelescopePromotionResidentID);

	FPhase0Config SimulationConfig;
	SimulationConfig.Seed = 20260810;
	SimulationConfig.PopulationPerKingdom = 100;
	FUnifiedSimulationSession Session(
		SimulationConfig,
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::StateImport,
		MakeDemoOptions());
	if (!Session.Initialize(Error)
		|| !Session.SubmitDemoObservationRequest(PromotionPlan.ActiveRequest, Error))
	{
		AddError(FString::Printf(TEXT("The Phase 7B plan could not cross the Phase 7A session boundary: %s"), *Error));
		return false;
	}
	FUnifiedDemoSnapshot Snapshot;
	if (!Session.BuildDemoSnapshot(Snapshot, Error)) return false;
	TestEqual(TEXT("The session commits the planned telescope Active count"),
		Snapshot.ActiveCount, PromotionPlan.ActiveRequest.DesiredActiveResidentIDs.Num());
	TestEqual(TEXT("The session commits the same tracked telescope resident"),
		Snapshot.TrackedResidentID, PromotionPlan.ActiveRequest.TrackedResidentID);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7BScaleGridBoundaryTest,
	"AILODResearch.Phase7B.ScaleGridBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7BScaleGridBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FString Error;
	FVisualWorldLayout Layout20k;
	FVisualWorldLayout Layout100k;
	FInitialPopulationManifest Population20k = MakeVisualPopulation(10000);
	const double Build20kStart = FPlatformTime::Seconds();
	if (!Layout20k.Build(Population20k, FVisualWorldLayoutConfig{}, Error)) return false;
	const double Build20kMs = (FPlatformTime::Seconds() - Build20kStart) * 1000.0;
	Population20k = {};
	FInitialPopulationManifest Population100k = MakeVisualPopulation(50000);
	const double Build100kStart = FPlatformTime::Seconds();
	if (!Layout100k.Build(Population100k, FVisualWorldLayoutConfig{}, Error)) return false;
	const double Build100kMs = (FPlatformTime::Seconds() - Build100kStart) * 1000.0;
	Population100k = {};

	auto RunLocalQuery = [&Error](
		const FVisualWorldLayout& Layout,
		FVisualSpatialQueryDiagnostics& OutDiagnostics,
		double& OutQueryMs)
	{
		const FVisualResidentPlacement& Target = Layout.GetResidents()[100];
		FVisualConeQuery Query;
		Query.Origin = Target.ProxyPosition - FVector2D(10000.0, 0.0);
		Query.Forward = FVector2D(1.0, 0.0);
		Query.MaxDistance = 20000.0;
		Query.HalfAngleDegrees = 45.0;
		Query.MaxResults = 512;
		TArray<FVisualSpatialCandidate> Candidates;
		const double QueryStart = FPlatformTime::Seconds();
		const bool bResult = Layout.QueryCone(Query, Candidates, OutDiagnostics, Error);
		OutQueryMs = (FPlatformTime::Seconds() - QueryStart) * 1000.0;
		return bResult && !Candidates.IsEmpty();
	};

	FVisualSpatialQueryDiagnostics Query20k;
	FVisualSpatialQueryDiagnostics Query100k;
	double Query20kMs = 0.0;
	double Query100kMs = 0.0;
	if (!RunLocalQuery(Layout20k, Query20k, Query20kMs)
		|| !RunLocalQuery(Layout100k, Query100k, Query100kMs))
	{
		AddError(FString::Printf(TEXT("The scale-grid query failed: %s"), *Error));
		return false;
	}

	TestEqual(TEXT("20k uses eight bounded districts"), Layout20k.GetDistricts().Num(), 8);
	TestEqual(TEXT("100k expands the world to forty bounded districts"), Layout100k.GetDistricts().Num(), 40);
	TestEqual(TEXT("20k presentation homes remain far below resident count"), Layout20k.GetHomeSlots().Num(), 512);
	TestEqual(TEXT("100k presentation homes scale by district rather than one per resident"), Layout100k.GetHomeSlots().Num(), 2560);
	TestFalse(TEXT("20k query does not scan the resident catalog"), Query20k.bScannedResidentCatalog);
	TestFalse(TEXT("100k query does not scan the resident catalog"), Query100k.bScannedResidentCatalog);
	TestTrue(TEXT("20k local query visits less than ten percent of residents"),
		Query20k.VisitedResidentEntryCount < Layout20k.GetResidents().Num() / 10);
	TestTrue(TEXT("100k local query visits less than ten percent of residents"),
		Query100k.VisitedResidentEntryCount < Layout100k.GetResidents().Num() / 10);
	TestTrue(TEXT("100k local query remains bounded after the world expands"),
		Query100k.VisitedResidentEntryCount < 5000);
	TestTrue(TEXT("The same query shape visits a population-independent number of grid cells"),
		FMath::Abs(Query100k.VisitedCellCount - Query20k.VisitedCellCount) <= 2);
	AddInfo(FString::Printf(
		TEXT("Phase7B scale diagnostics: build20k=%.3fms build100k=%.3fms query20k=%.3fms query100k=%.3fms entries20k=%d entries100k=%d cells20k=%d cells100k=%d"),
		Build20kMs,
		Build100kMs,
		Query20kMs,
		Query100kMs,
		Query20k.VisitedResidentEntryCount,
		Query100k.VisitedResidentEntryCount,
		Query20k.VisitedCellCount,
		Query100k.VisitedCellCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase7BObservationCommitCostTest,
	"AILODResearch.Phase7B.ObservationCommitCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase7BObservationCommitCostTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	struct FCommitMeasurement
	{
		double InitializeMs = 0.0;
		double FirstCommitMs = 0.0;
		double IdempotentCommitMs = 0.0;
		int32 ActiveCount = 0;
		int32 VisitedEntries = 0;
	};

	auto Measure = [this](const int32 PopulationPerKingdom, FCommitMeasurement& OutMeasurement)
	{
		FString Error;
		FVisualWorldLayout Layout;
		if (!Layout.Build(MakeVisualPopulation(PopulationPerKingdom), FVisualWorldLayoutConfig{}, Error))
		{
			AddError(Error);
			return false;
		}
		FVisualObservationPlanner Planner(Layout);
		const FVisualResidentPlacement& Target = Layout.GetResidents()[100];
		FVisualObservationFrameInput Input;
		Input.NormalView.Origin = Target.ProxyPosition - FVector2D(10000.0, 0.0);
		Input.NormalView.Forward = FVector2D(1.0, 0.0);
		Input.NormalView.EnterDistance = 20000.0;
		Input.NormalView.HalfAngleDegrees = 45.0;
		Input.RealDeltaSeconds = 1.0;
		FVisualObservationPlan Plan;
		if (!Planner.PlanFrame(Input, Plan, Error) || Plan.ActiveRequest.DesiredActiveResidentIDs.IsEmpty())
		{
			AddError(Error.IsEmpty() ? TEXT("The commit-cost planner returned no Active residents.") : Error);
			return false;
		}

		FPhase0Config SimulationConfig;
		SimulationConfig.Seed = 20260810;
		SimulationConfig.PopulationPerKingdom = PopulationPerKingdom;
		FUnifiedSimulationSession Session(
			SimulationConfig,
			EUnifiedSimulationMethod::Proposed,
			EStage2Scenario::StateImport,
			MakeDemoOptions());
		const double InitializeStart = FPlatformTime::Seconds();
		if (!Session.Initialize(Error))
		{
			AddError(Error);
			return false;
		}
		OutMeasurement.InitializeMs = (FPlatformTime::Seconds() - InitializeStart) * 1000.0;
		const double FirstCommitStart = FPlatformTime::Seconds();
		if (!Session.SubmitDemoObservationRequest(Plan.ActiveRequest, Error))
		{
			AddError(Error);
			return false;
		}
		OutMeasurement.FirstCommitMs = (FPlatformTime::Seconds() - FirstCommitStart) * 1000.0;
		const double IdempotentCommitStart = FPlatformTime::Seconds();
		if (!Session.SubmitDemoObservationRequest(Plan.ActiveRequest, Error))
		{
			AddError(Error);
			return false;
		}
		OutMeasurement.IdempotentCommitMs = (FPlatformTime::Seconds() - IdempotentCommitStart) * 1000.0;
		FUnifiedDemoSnapshot Snapshot;
		if (!Session.BuildDemoSnapshot(Snapshot, Error)) return false;
		OutMeasurement.ActiveCount = Snapshot.ActiveCount;
		OutMeasurement.VisitedEntries = Plan.Diagnostics.NormalQuery.VisitedResidentEntryCount;
		return Snapshot.ActiveCount == Plan.ActiveRequest.DesiredActiveResidentIDs.Num();
	};

	FCommitMeasurement Population2k;
	FCommitMeasurement Population20k;
	if (!Measure(1000, Population2k) || !Measure(10000, Population20k)) return false;
	TestTrue(TEXT("2k commits a non-empty observation set within the 35-person soft budget"),
		Population2k.ActiveCount > 0 && Population2k.ActiveCount <= 35);
	TestTrue(TEXT("20k commits a non-empty observation set within the 35-person soft budget"),
		Population20k.ActiveCount > 0 && Population20k.ActiveCount <= 35);
	TestTrue(TEXT("20k planning still visits only local entries before committing"),
		Population20k.VisitedEntries < 2000);
	AddInfo(FString::Printf(
		TEXT("Phase7B commit diagnostics: init2k=%.3fms first2k=%.3fms noop2k=%.3fms active2k=%d entries2k=%d init20k=%.3fms first20k=%.3fms noop20k=%.3fms active20k=%d entries20k=%d"),
		Population2k.InitializeMs,
		Population2k.FirstCommitMs,
		Population2k.IdempotentCommitMs,
		Population2k.ActiveCount,
		Population2k.VisitedEntries,
		Population20k.InitializeMs,
		Population20k.FirstCommitMs,
		Population20k.IdempotentCommitMs,
		Population20k.ActiveCount,
		Population20k.VisitedEntries));
	return true;
}

#endif
