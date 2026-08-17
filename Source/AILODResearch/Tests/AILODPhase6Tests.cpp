// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODUnifiedSimulation.h"

namespace
{
	struct FPhase6ABaseline
	{
		AILOD::EUnifiedSimulationMethod Method;
		AILOD::EStage2Scenario Scenario;
		const TCHAR* Digest;
		int32 TransactionCount;
		int32 EventCount;
		int64 PlanningCount;
	};

	AILOD::FPhase0Config MakePhase6AConfig()
	{
		AILOD::FPhase0Config Config;
		Config.Seed = 20260810;
		Config.PopulationPerKingdom = 100;
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6ASessionParityTest,
	"AILODResearch.Phase6.SessionLifecycleAndParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6ASessionParityTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const FPhase6ABaseline Baselines[] =
	{
		{ EUnifiedSimulationMethod::Oracle, EStage2Scenario::None, TEXT("83B3353C53E50E596BA3271AE90D499A2297E1EC"), 13066, 40332, 40331 },
		{ EUnifiedSimulationMethod::Oracle, EStage2Scenario::HarvestCap, TEXT("DB7D90ED4C581C367ED1D91B0BC4C8EA82B80C62"), 12907, 40326, 40322 },
		{ EUnifiedSimulationMethod::Oracle, EStage2Scenario::StateImport, TEXT("CC2592820898E215ADD9691B64B9825818638853"), 13106, 40343, 40331 },
		{ EUnifiedSimulationMethod::Oracle, EStage2Scenario::RepairAid, TEXT("21A7BA1FF047EFC2A98C909E9BD99FA668AF8075"), 13086, 40361, 40358 },
		{ EUnifiedSimulationMethod::Proposed, EStage2Scenario::None, TEXT("1D9D5CE8617007E90463C21F24639CDE939F16D7"), 13069, 40323, 2782 },
		{ EUnifiedSimulationMethod::Proposed, EStage2Scenario::HarvestCap, TEXT("3DA2EF5439219E649F9EE5FA7C6723FCB2E06C9A"), 12907, 40326, 2782 },
		{ EUnifiedSimulationMethod::Proposed, EStage2Scenario::StateImport, TEXT("D326B24A3D74128C955667DB42E8F1BADA9BC9CD"), 13109, 40334, 2782 },
		{ EUnifiedSimulationMethod::Proposed, EStage2Scenario::RepairAid, TEXT("27DD9F42305630BD2417894164F8CDE6DF4725D4"), 13086, 40361, 2775 },
		{ EUnifiedSimulationMethod::PerAgent, EStage2Scenario::None, TEXT("9D5D1AF23E6EB0B119AF612DF564456E69F170C3"), 13066, 40332, 40331 },
		{ EUnifiedSimulationMethod::PerAgent, EStage2Scenario::HarvestCap, TEXT("68651039E3A2B88B7E719D4CA86035F3348B5566"), 12907, 40326, 40322 },
		{ EUnifiedSimulationMethod::PerAgent, EStage2Scenario::StateImport, TEXT("7CC66C5D5C848EDA313912740394096445DE008F"), 13106, 40343, 40331 },
		{ EUnifiedSimulationMethod::PerAgent, EStage2Scenario::RepairAid, TEXT("CA6FDA571E5FFB818BC78DA298ADFD70965CE76F"), 13086, 40361, 40358 },
		{ EUnifiedSimulationMethod::Simple, EStage2Scenario::None, TEXT("1636E56E6E4B9C43A7F2275EB69B5012B7FDF760"), 13093, 246, 180 },
		{ EUnifiedSimulationMethod::Simple, EStage2Scenario::HarvestCap, TEXT("C72A6297F310324806DC449535760009729F316C"), 12694, 278, 184 },
		{ EUnifiedSimulationMethod::Simple, EStage2Scenario::StateImport, TEXT("5D87C9919198C408EF9C7063DACD2162FF12E31B"), 13186, 258, 180 },
		{ EUnifiedSimulationMethod::Simple, EStage2Scenario::RepairAid, TEXT("385A79A3167F4BA01985A0D0CE3F782AA0E724F6"), 13094, 247, 180 }
	};

	const FPhase0Config Config = MakePhase6AConfig();
	FUnifiedRunOptions Options;
	Options.bRecordSnapshots = true;
	constexpr int32 ExpectedHourSteps = 67 * 24;
	bool bLifecycleGuardsChecked = false;

	for (const FPhase6ABaseline& Baseline : Baselines)
	{
		const FString Label = FString::Printf(TEXT("%s/%s"), ToString(Baseline.Method), ToString(Baseline.Scenario));
		FUnifiedRunResult BlockingResult;
		FString Error;
		if (!FUnifiedSimulationRunner::Run(
			Config,
			Baseline.Method,
			Baseline.Scenario,
			Options,
			BlockingResult,
			Error))
		{
			AddError(FString::Printf(TEXT("%s blocking wrapper failed: %s"), *Label, *Error));
			continue;
		}

		const FString BlockingDigest = FUnifiedSimulationRunner::BuildDeterministicDigest(BlockingResult);
		TestEqual(*FString::Printf(TEXT("%s blocking wrapper preserves the Phase 5.1 digest"), *Label), BlockingDigest, FString(Baseline.Digest));
		TestEqual(*FString::Printf(TEXT("%s blocking wrapper preserves transactions"), *Label), BlockingResult.Transactions.Num(), Baseline.TransactionCount);
		TestEqual(*FString::Printf(TEXT("%s blocking wrapper preserves events"), *Label), BlockingResult.Events.Num(), Baseline.EventCount);
		TestEqual(*FString::Printf(TEXT("%s blocking wrapper preserves production planning"), *Label), BlockingResult.Diagnostics.PlanningEvaluationCount, Baseline.PlanningCount);

		FUnifiedSimulationSession Session(Config, Baseline.Method, Baseline.Scenario, Options);
		if (!bLifecycleGuardsChecked)
		{
			FUnifiedRunResult PrematureResult;
			TestFalse(TEXT("A session cannot step before initialization"), Session.StepHour(Error));
			TestFalse(TEXT("A session cannot finalize before initialization"), Session.Finalize(PrematureResult, Error));
		}
		if (!Session.Initialize(Error))
		{
			AddError(FString::Printf(TEXT("%s session initialization failed: %s"), *Label, *Error));
			continue;
		}
		if (!bLifecycleGuardsChecked)
		{
			FUnifiedRunResult PrematureResult;
			TestFalse(TEXT("A session cannot initialize twice"), Session.Initialize(Error));
			TestFalse(TEXT("A session cannot finalize before D60"), Session.Finalize(PrematureResult, Error));
		}

		int32 Steps = 0;
		for (; Steps < ExpectedHourSteps && !Session.IsComplete(); ++Steps)
		{
			if (!Session.StepHour(Error))
			{
				AddError(FString::Printf(TEXT("%s session step %d failed: %s"), *Label, Steps, *Error));
				break;
			}
		}
		TestTrue(*FString::Printf(TEXT("%s reaches the complete state"), *Label), Session.IsComplete());
		TestEqual(*FString::Printf(TEXT("%s executes exactly 1608 hour steps"), *Label), Steps, ExpectedHourSteps);
		TestEqual(*FString::Printf(TEXT("%s exposes the completed step count"), *Label), Session.GetCompletedHourSteps(), ExpectedHourSteps);
		TestEqual(*FString::Printf(TEXT("%s exposes D60 as the current time"), *Label), Session.GetCurrentTime().Minutes, FSimulationTime::FromDays(60).Minutes);

		FUnifiedRunResult SessionResult;
		if (!Session.Finalize(SessionResult, Error))
		{
			AddError(FString::Printf(TEXT("%s session finalization failed: %s"), *Label, *Error));
			continue;
		}
		const FString SessionDigest = FUnifiedSimulationRunner::BuildDeterministicDigest(SessionResult);
		TestEqual(*FString::Printf(TEXT("%s step session matches the blocking wrapper"), *Label), SessionDigest, BlockingDigest);
		TestEqual(*FString::Printf(TEXT("%s step session preserves transactions"), *Label), SessionResult.Transactions.Num(), Baseline.TransactionCount);
		TestEqual(*FString::Printf(TEXT("%s step session preserves events"), *Label), SessionResult.Events.Num(), Baseline.EventCount);
		TestEqual(*FString::Printf(TEXT("%s step session preserves production planning"), *Label), SessionResult.Diagnostics.PlanningEvaluationCount, Baseline.PlanningCount);
		TestTrue(*FString::Printf(TEXT("%s step session remains hard-error free"), *Label), SessionResult.IsHardErrorFree());
		TestTrue(*FString::Printf(TEXT("%s records a final snapshot"), *Label), SessionResult.Snapshots.Num() >= 2);
		if (SessionResult.Snapshots.Num() >= 2)
		{
			TestEqual(*FString::Printf(TEXT("%s Kingdom A final snapshot is T+1 D60"), *Label), SessionResult.Snapshots[SessionResult.Snapshots.Num() - 2].GameTime.Minutes, FSimulationTime::FromDays(60).Minutes);
			TestEqual(*FString::Printf(TEXT("%s Kingdom B final snapshot is T+1 D60"), *Label), SessionResult.Snapshots.Last().GameTime.Minutes, FSimulationTime::FromDays(60).Minutes);
		}

		if (!bLifecycleGuardsChecked)
		{
			FUnifiedRunResult DuplicateResult;
			TestFalse(TEXT("A completed session cannot step again"), Session.StepHour(Error));
			TestFalse(TEXT("A finalized session cannot finalize twice"), Session.Finalize(DuplicateResult, Error));
			bLifecycleGuardsChecked = true;
		}
		AddInfo(FString::Printf(TEXT("Phase6A %s digest=%s steps=%d"), *Label, *SessionDigest, Steps));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6BBackendBoundaryTest,
	"AILODResearch.Phase6.BackendBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6BBackendBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const EUnifiedSimulationMethod Methods[] =
	{
		EUnifiedSimulationMethod::Oracle,
		EUnifiedSimulationMethod::Proposed,
		EUnifiedSimulationMethod::PerAgent,
		EUnifiedSimulationMethod::Simple
	};
	FUnifiedRunOptions Options;
	Options.Mode = EUnifiedRunMode::Performance;
	Options.bRetainCompletedEvents = false;
	Options.bRecordSnapshots = false;

	for (const EUnifiedSimulationMethod Method : Methods)
	{
		FUnifiedRunResult Result;
		FString Error;
		if (!FUnifiedSimulationRunner::Run(
			MakePhase6AConfig(),
			Method,
			EStage2Scenario::None,
			Options,
			Result,
			Error))
		{
			AddError(FString::Printf(TEXT("%s backend run failed: %s"), ToString(Method), *Error));
			continue;
		}

		TestEqual(*FString::Printf(TEXT("%s backend preserves its selected method"), ToString(Method)), Result.Method, Method);
		TestTrue(*FString::Printf(TEXT("%s backend remains hard-error free"), ToString(Method)), Result.IsHardErrorFree());
		if (Method == EUnifiedSimulationMethod::Proposed)
		{
			TestEqual(TEXT("Proposed retains one persistent CoreState per resident"), Result.Residents.Num(), 200);
			TestTrue(TEXT("Proposed enters cohort planning through its backend"), Result.Diagnostics.CohortPlanningEvaluationCount > 0);
		}
		else if (Method == EUnifiedSimulationMethod::Simple)
		{
			TestEqual(TEXT("Simple returns no persistent individual population"), Result.Residents.Num(), 0);
			TestTrue(TEXT("Simple reconstructs temporary Micro state through its backend"), Result.Diagnostics.SimpleMicroReconstructionCount > 0);
			TestEqual(TEXT("Simple writes back every temporary Micro state"), Result.Diagnostics.SimpleMicroWritebackCount, Result.Diagnostics.SimpleMicroReconstructionCount);
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("%s retains one persistent CoreState per resident"), ToString(Method)), Result.Residents.Num(), 200);
			TestEqual(*FString::Printf(TEXT("%s does not enter cohort planning"), ToString(Method)), Result.Diagnostics.CohortPlanningEvaluationCount, static_cast<int64>(0));
		}
	}

	FPhase0Config LargeOracleConfig = MakePhase6AConfig();
	LargeOracleConfig.PopulationPerKingdom = 1000;
	FUnifiedSimulationSession LargeOracleSession(
		LargeOracleConfig,
		EUnifiedSimulationMethod::Oracle,
		EStage2Scenario::StateImport,
		Options);
	FString Error;
	TestFalse(TEXT("Oracle backend still rejects more than 200 residents"), LargeOracleSession.Initialize(Error));
	TestTrue(TEXT("Oracle backend explains its frozen population boundary"), Error.Contains(TEXT("200 total residents")));

	return true;
}

#endif
