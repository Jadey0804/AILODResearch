// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODPhase0Manifest.h"
#include "../Simulation/AILODUnifiedSimulation.h"

namespace
{
	AILOD::FPhase0Config MakeConfig(const int32 TotalPopulation)
	{
		AILOD::FPhase0Config Config;
		Config.Seed = 20260810;
		Config.PopulationPerKingdom = TotalPopulation / 2;
		return Config;
	}

	bool RunUnified(
		const int32 TotalPopulation,
		const AILOD::EUnifiedSimulationMethod Method,
		const AILOD::EStage2Scenario Scenario,
		const AILOD::FUnifiedRunOptions& Options,
		AILOD::FUnifiedRunResult& OutResult,
		FString& OutError)
	{
		return AILOD::FUnifiedSimulationRunner::Run(
			MakeConfig(TotalPopulation),
			Method,
			Scenario,
			Options,
			OutResult,
			OutError);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase5UnifiedBackendMatrixTest,
	"AILODResearch.Phase5.UnifiedBackendMatrix200",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase5UnifiedBackendMatrixTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const EUnifiedSimulationMethod Methods[] =
	{
		EUnifiedSimulationMethod::Oracle,
		EUnifiedSimulationMethod::Proposed,
		EUnifiedSimulationMethod::PerAgent,
		EUnifiedSimulationMethod::Simple
	};
	const EStage2Scenario Scenarios[] =
	{
		EStage2Scenario::None,
		EStage2Scenario::HarvestCap,
		EStage2Scenario::StateImport,
		EStage2Scenario::RepairAid
	};
	FUnifiedRunOptions Options;
	Options.bRecordSnapshots = true;
	TArray<FUnifiedActivationObservation> ExpectedActivationTrace;

	for (const EUnifiedSimulationMethod Method : Methods)
	{
		for (const EStage2Scenario Scenario : Scenarios)
		{
			FUnifiedRunResult Result;
			FString Error;
			const FString Label = FString::Printf(TEXT("%s/%s"), ToString(Method), ToString(Scenario));
			if (!RunUnified(200, Method, Scenario, Options, Result, Error))
			{
				AddError(FString::Printf(TEXT("%s failed: %s"), *Label, *Error));
				continue;
			}
			TestEqual(*FString::Printf(TEXT("%s ends exactly at D60"), *Label), Result.FinalTime.Minutes, FSimulationTime::FromDays(60).Minutes);
			TestEqual(*FString::Printf(TEXT("%s executes 168 warm-up hours"), *Label), Result.WarmupHourSteps, 168);
			TestEqual(*FString::Printf(TEXT("%s executes 1440 formal hours"), *Label), Result.FormalHourSteps, 1440);
			TestTrue(*FString::Printf(TEXT("%s passes every hard-error gate"), *Label), Result.IsHardErrorFree());
			TestEqual(*FString::Printf(TEXT("%s conserves Kingdom A homes"), *Label),
				Result.GetHomeStateCount(EKingdom::A, EHomeState::Healthy)
				+ Result.GetHomeStateCount(EKingdom::A, EHomeState::DamagedWaiting)
				+ Result.GetHomeStateCount(EKingdom::A, EHomeState::UnderRepair)
				+ Result.GetHomeStateCount(EKingdom::A, EHomeState::Repaired), 100);
			TestEqual(*FString::Printf(TEXT("%s conserves Kingdom B homes"), *Label),
				Result.GetHomeStateCount(EKingdom::B, EHomeState::Healthy)
				+ Result.GetHomeStateCount(EKingdom::B, EHomeState::DamagedWaiting)
				+ Result.GetHomeStateCount(EKingdom::B, EHomeState::UnderRepair)
				+ Result.GetHomeStateCount(EKingdom::B, EHomeState::Repaired), 100);
			TestEqual(*FString::Printf(TEXT("%s records the hourly snapshots for two kingdoms"), *Label), Result.Snapshots.Num(), 2880);
			TestEqual(*FString::Printf(TEXT("%s has no event due at or before D60"), *Label), Result.PendingEventsAtOrBeforeEnd, 0);
			TestEqual(*FString::Printf(TEXT("%s records all 60 activation instances"), *Label), Result.ActivationObservations.Num(), 60);
			TestEqual(*FString::Printf(TEXT("%s records a FirstAction for every activation"), *Label), Result.Diagnostics.FirstActionCount, 60);
			TestEqual(*FString::Printf(TEXT("%s activates the frozen 20-person Day 14 sample"), *Label), Result.Diagnostics.Day14ActivationCount, 20);
			TestEqual(*FString::Printf(TEXT("%s deactivates the frozen 20-person Day 14 sample"), *Label), Result.Diagnostics.Day14DeactivationCount, 20);
			TestEqual(*FString::Printf(TEXT("%s records no committed-task reset"), *Label), Result.TaskResetCount, 0);
			if (ExpectedActivationTrace.Num() == 0)
			{
				ExpectedActivationTrace = Result.ActivationObservations;
			}
			else
			{
				TestEqual(*FString::Printf(TEXT("%s shares the activation trace length"), *Label), Result.ActivationObservations.Num(), ExpectedActivationTrace.Num());
				for (int32 Index = 0; Index < FMath::Min(Result.ActivationObservations.Num(), ExpectedActivationTrace.Num()); ++Index)
				{
					TestEqual(*FString::Printf(TEXT("%s activation %d shares ResidentID"), *Label, Index), Result.ActivationObservations[Index].ResidentID, ExpectedActivationTrace[Index].ResidentID);
					TestEqual(*FString::Printf(TEXT("%s activation %d shares GameTime"), *Label, Index), Result.ActivationObservations[Index].ActivationTime.Minutes, ExpectedActivationTrace[Index].ActivationTime.Minutes);
				}
			}
			if (Method == EUnifiedSimulationMethod::Simple)
			{
				TestEqual(TEXT("Simple creates no individual CoreState"), Result.Residents.Num(), 0);
				TestEqual(TEXT("Simple reports zero individual CoreState"), Result.SimpleIndividualCoreStateCount, 0);
				TestEqual(TEXT("Simple reconstructs each activation as temporary Micro state"), Result.Diagnostics.SimpleMicroReconstructionCount, 60);
				TestEqual(TEXT("Simple writes every temporary Micro state back to aggregates"), Result.Diagnostics.SimpleMicroWritebackCount, 60);
			}
			else
			{
				TestEqual(*FString::Printf(TEXT("%s retains all 200 resident CoreStates"), *Label), Result.Residents.Num(), 200);
			}
			AddInfo(FString::Printf(
				TEXT("Phase5 %s Digest=%s Transactions=%d Events=%d Planning=%lld"),
				*Label,
				*FUnifiedSimulationRunner::BuildDeterministicDigest(Result),
				Result.Transactions.Num(),
				Result.Events.Num(),
				Result.Diagnostics.PlanningEvaluationCount));
		}
	}

	FInitialPopulationManifest Population;
	FEarthquakeDamageList Damage;
	FPersistentTestPool Continuity;
	FString ManifestError;
	if (!FPhase0ManifestGenerator::Generate(MakeConfig(200), Population, Damage, Continuity, ManifestError))
	{
		AddError(ManifestError);
		return false;
	}
	TSet<FResidentID> ContinuityIDs;
	for (const FPersistentTestRecord& Record : Continuity.Residents)
	{
		ContinuityIDs.Add(Record.ResidentID);
	}
	int32 Day14Strata[2][2][2] = {};
	int32 Day14Count = 0;
	for (const FUnifiedActivationObservation& Observation : ExpectedActivationTrace)
	{
		if (!(Observation.ActivationTime == FSimulationTime::FromDays(14)))
		{
			continue;
		}
		++Day14Count;
		TestFalse(TEXT("Day 14 excludes the fixed continuity sample"), ContinuityIDs.Contains(Observation.ResidentID));
		const FInitialResidentRecord* Initial = Population.Residents.FindByPredicate([&Observation](const FInitialResidentRecord& Resident)
		{
			return Resident.ResidentID == Observation.ResidentID;
		});
		if (Initial == nullptr)
		{
			AddError(TEXT("Day 14 activation references an unknown manifest resident."));
			continue;
		}
		++Day14Strata[static_cast<int32>(Initial->Kingdom)][static_cast<int32>(Initial->Profession)][static_cast<int32>(Initial->IncomeBand)];
	}
	TestEqual(TEXT("Day 14 contains 20 residents"), Day14Count, 20);
	for (int32 Kingdom = 0; Kingdom < 2; ++Kingdom)
	{
		TestEqual(TEXT("Day 14 Logger Low stratum"), Day14Strata[Kingdom][0][0], 1);
		TestEqual(TEXT("Day 14 Logger NonLow stratum"), Day14Strata[Kingdom][0][1], 1);
		TestEqual(TEXT("Day 14 Worker Low stratum"), Day14Strata[Kingdom][1][0], 6);
		TestEqual(TEXT("Day 14 Worker NonLow stratum"), Day14Strata[Kingdom][1][1], 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase5FairnessAndDeterminismTest,
	"AILODResearch.Phase5.FairnessAndDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase5FairnessAndDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FUnifiedRunOptions Options;
	Options.bRecordSnapshots = false;
	Options.bVerifyCohortApproximation = true;
	FUnifiedRunResult ProposedA;
	FUnifiedRunResult ProposedB;
	FUnifiedRunResult PerAgent;
	FUnifiedRunResult PerformanceMode;
	FString Error;
	if (!RunUnified(200, EUnifiedSimulationMethod::Proposed, EStage2Scenario::HarvestCap, Options, ProposedA, Error)
		|| !RunUnified(200, EUnifiedSimulationMethod::Proposed, EStage2Scenario::HarvestCap, Options, ProposedB, Error)
		|| !RunUnified(200, EUnifiedSimulationMethod::PerAgent, EStage2Scenario::HarvestCap, Options, PerAgent, Error))
	{
		AddError(Error);
		return false;
	}
	FUnifiedRunOptions PerformanceOptions = Options;
	PerformanceOptions.Mode = EUnifiedRunMode::Performance;
	if (!RunUnified(200, EUnifiedSimulationMethod::Proposed, EStage2Scenario::HarvestCap, PerformanceOptions, PerformanceMode, Error))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Repeated Proposed runs produce the same digest"),
		FUnifiedSimulationRunner::BuildDeterministicDigest(ProposedA),
		FUnifiedSimulationRunner::BuildDeterministicDigest(ProposedB));
	TestTrue(TEXT("Proposed performs cohort planning"), ProposedA.Diagnostics.CohortPlanningEvaluationCount > 0);
	TestTrue(TEXT("Proposed performs Active Micro planning on the fixed trace"), ProposedA.Diagnostics.ActiveMicroPlanningEvaluationCount > 0);
	TestTrue(TEXT("Active Micro and CohortManaged residents enter at least one shared scarce-resource scope"),
		ProposedA.Diagnostics.MixedRepresentationCompetitionCount > 0);
	TestTrue(TEXT("Proposed performs fewer planner evaluations than Per-Agent"),
		ProposedA.Diagnostics.PlanningEvaluationCount < PerAgent.Diagnostics.PlanningEvaluationCount);
	TestTrue(TEXT("Validation observes a real controlled-approximation disagreement"),
		ProposedA.Diagnostics.CohortDecisionDisagreementCount > 0);
	TestTrue(TEXT("Controlled approximation exercises an individual legality fallback"),
		ProposedA.Diagnostics.CohortAllocationFallbackCount > 0);
	TestTrue(TEXT("Validation planner work is reported separately"),
		ProposedA.Diagnostics.ValidationPlanningEvaluationCount > 0);
	TestTrue(TEXT("The fixed activation trace never exceeds 50"), ProposedA.Diagnostics.MaxActiveMicro <= 50);
	TestEqual(TEXT("Performance mode does not change deterministic simulation output"),
		FUnifiedSimulationRunner::BuildDeterministicDigest(PerformanceMode),
		FUnifiedSimulationRunner::BuildDeterministicDigest(ProposedA));
	TestEqual(TEXT("Performance mode excludes per-member validation GOAP"), PerformanceMode.Diagnostics.ValidationPlanningEvaluationCount, 0ll);
	TestTrue(TEXT("Performance mode performs fewer full audits than Validation mode"),
		PerformanceMode.Diagnostics.FullAuditCount < ProposedA.Diagnostics.FullAuditCount);
	TestTrue(TEXT("Proposed retains per-resident events"), ProposedA.Events.ContainsByPredicate([](const FSimulationEventRecord& Event)
	{
		return Event.Event.ResidentID > 0;
	}));
	AddInfo(FString::Printf(
		TEXT("Phase5.1 Proposed/HarvestCap DecisionPlanning=%lld ValidationPlanning=%lld Disagreement=%lld AllocationFallback=%lld ValidationAudits=%lld PerformanceAudits=%lld Activation=%d"),
		ProposedA.Diagnostics.PlanningEvaluationCount,
		ProposedA.Diagnostics.ValidationPlanningEvaluationCount,
		ProposedA.Diagnostics.CohortDecisionDisagreementCount,
		ProposedA.Diagnostics.CohortAllocationFallbackCount,
		ProposedA.Diagnostics.FullAuditCount,
		PerformanceMode.Diagnostics.FullAuditCount,
		ProposedA.Diagnostics.FirstActionCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase5FailureBoundaryTest,
	"AILODResearch.Phase5.FailureBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase5FailureBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FUnifiedRunOptions Options;
	Options.bRecordSnapshots = false;
	FUnifiedRunResult Result;
	FString Error;
	TestFalse(TEXT("Oracle rejects a population larger than 200"),
		RunUnified(2000, EUnifiedSimulationMethod::Oracle, EStage2Scenario::None, Options, Result, Error));
	TestTrue(TEXT("Oracle rejection explains the frozen 200-person boundary"), Error.Contains(TEXT("200 total residents")));

	struct FFaultCase
	{
		EUnifiedFaultInjectionPoint Point = EUnifiedFaultInjectionPoint::None;
		EStage2Scenario Scenario = EStage2Scenario::None;
		const TCHAR* Label = TEXT("");
	};
	const FFaultCase FaultCases[] =
	{
		{ EUnifiedFaultInjectionPoint::BuyWoodPreflight, EStage2Scenario::None, TEXT("BuyWood") },
		{ EUnifiedFaultInjectionPoint::ChopWoodPreflight, EStage2Scenario::None, TEXT("ChopWood") },
		{ EUnifiedFaultInjectionPoint::StartRepairPreflight, EStage2Scenario::None, TEXT("StartRepair") },
		{ EUnifiedFaultInjectionPoint::StateImportPreflight, EStage2Scenario::StateImport, TEXT("StateImport") }
	};
	for (const FFaultCase& FaultCase : FaultCases)
	{
		FUnifiedRunOptions FaultOptions;
		FaultOptions.Mode = EUnifiedRunMode::Performance;
		FaultOptions.bRecordSnapshots = false;
		FaultOptions.bRetainCompletedEvents = false;
		FaultOptions.FaultInjection = FaultCase.Point;
		FUnifiedRunResult FaultResult;
		Error.Reset();
		if (!RunUnified(200, EUnifiedSimulationMethod::Oracle, FaultCase.Scenario, FaultOptions, FaultResult, Error))
		{
			AddError(FString::Printf(TEXT("%s injected rejection failed: %s"), FaultCase.Label, *Error));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fault is consumed once"), FaultCase.Label), FaultResult.Diagnostics.FaultInjectionCount, 1ll);
		TestEqual(*FString::Printf(TEXT("%s rejection leaves no action residue"), FaultCase.Label), FaultResult.Diagnostics.RejectedActionResidueCount, 0ll);
		TestTrue(*FString::Printf(TEXT("%s fault run remains hard-error free"), FaultCase.Label), FaultResult.IsHardErrorFree());
		AddInfo(FString::Printf(
			TEXT("Phase5.1 Fault=%s Injected=%lld Residue=%lld"),
			FaultCase.Label,
			FaultResult.Diagnostics.FaultInjectionCount,
			FaultResult.Diagnostics.RejectedActionResidueCount));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase5ScaleSmokeTest,
	"AILODResearch.Phase5.StateImportScaleSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase5ScaleSmokeTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const int32 TotalPopulations[] = { 2000, 10000, 20000 };
	const EUnifiedSimulationMethod Methods[] =
	{
		EUnifiedSimulationMethod::Simple,
		EUnifiedSimulationMethod::PerAgent,
		EUnifiedSimulationMethod::Proposed
	};
	FUnifiedRunOptions Options;
	Options.bRetainCompletedEvents = false;
	Options.bRecordSnapshots = false;
	Options.bVerifyCohortApproximation = false;
	Options.Mode = EUnifiedRunMode::Performance;

	for (const int32 TotalPopulation : TotalPopulations)
	{
		for (const EUnifiedSimulationMethod Method : Methods)
		{
			FUnifiedRunResult Result;
			FString Error;
			const FString Label = FString::Printf(TEXT("%s/%d"), ToString(Method), TotalPopulation);
			if (!RunUnified(TotalPopulation, Method, EStage2Scenario::StateImport, Options, Result, Error))
			{
				AddError(FString::Printf(TEXT("%s scale smoke failed: %s"), *Label, *Error));
				continue;
			}
			TestEqual(*FString::Printf(TEXT("%s reaches D60"), *Label), Result.FinalTime.Minutes, FSimulationTime::FromDays(60).Minutes);
			TestTrue(*FString::Printf(TEXT("%s is hard-error free"), *Label), Result.IsHardErrorFree());
			TestEqual(*FString::Printf(TEXT("%s completes every temporary activation write-back"), *Label),
				Result.Diagnostics.SimpleMicroReconstructionCount,
				Result.Diagnostics.SimpleMicroWritebackCount);
			TestEqual(*FString::Printf(TEXT("%s conserves the full population"), *Label),
				Result.GetHomeStateCount(EKingdom::A, EHomeState::Healthy)
				+ Result.GetHomeStateCount(EKingdom::A, EHomeState::DamagedWaiting)
				+ Result.GetHomeStateCount(EKingdom::A, EHomeState::UnderRepair)
				+ Result.GetHomeStateCount(EKingdom::A, EHomeState::Repaired)
				+ Result.GetHomeStateCount(EKingdom::B, EHomeState::Healthy)
				+ Result.GetHomeStateCount(EKingdom::B, EHomeState::DamagedWaiting)
				+ Result.GetHomeStateCount(EKingdom::B, EHomeState::UnderRepair)
				+ Result.GetHomeStateCount(EKingdom::B, EHomeState::Repaired), TotalPopulation);
		}
	}
	return true;
}

#endif
