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

	class FPhase6CRecorder final
		: public AILOD::IUnifiedSimulationObserver
		, public AILOD::IUnifiedSimulationEventSink
	{
	public:
		virtual void OnHourCompleted(const AILOD::FUnifiedHourObservation& Observation) override
		{
			Hours.Add(Observation);
		}

		virtual void OnNPCSnapshot(const AILOD::FUnifiedNPCObservation& Observation) override
		{
			NPCSnapshots.Add(Observation);
		}

		virtual void OnEventCommitted(const AILOD::FSimulationEventRecord& Event) override
		{
			Events.Add(Event);
		}

		virtual void OnTransactionCommitted(const AILOD::FLedgerTransaction& Transaction) override
		{
			Transactions.Add(Transaction);
		}

		virtual void OnLODTransitionCommitted(const AILOD::FLODTransitionRecord& Transition) override
		{
			LODTransitions.Add(Transition);
		}

		virtual void OnActivationObserved(const AILOD::FUnifiedActivationObservation& Observation) override
		{
			Activations.Add(Observation);
		}

		TArray<AILOD::FUnifiedHourObservation> Hours;
		TArray<AILOD::FUnifiedNPCObservation> NPCSnapshots;
		TArray<AILOD::FSimulationEventRecord> Events;
		TArray<AILOD::FLedgerTransaction> Transactions;
		TArray<AILOD::FLODTransitionRecord> LODTransitions;
		TArray<AILOD::FUnifiedActivationObservation> Activations;
	};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6CReadOnlyObservationTest,
	"AILODResearch.Phase6.ReadOnlyObservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6CReadOnlyObservationTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	FUnifiedRunOptions BaselineOptions;
	BaselineOptions.bRecordSnapshots = true;
	FUnifiedRunResult Baseline;
	FString Error;
	if (!FUnifiedSimulationRunner::Run(
		MakePhase6AConfig(),
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::StateImport,
		BaselineOptions,
		Baseline,
		Error))
	{
		AddError(FString::Printf(TEXT("Unobserved baseline failed: %s"), *Error));
		return false;
	}

	FPhase6CRecorder Recorder;
	FUnifiedRunOptions ObservedOptions = BaselineOptions;
	ObservedOptions.Observer = &Recorder;
	ObservedOptions.EventSink = &Recorder;
	FUnifiedRunResult Observed;
	if (!FUnifiedSimulationRunner::Run(
		MakePhase6AConfig(),
		EUnifiedSimulationMethod::Proposed,
		EStage2Scenario::StateImport,
		ObservedOptions,
		Observed,
		Error))
	{
		AddError(FString::Printf(TEXT("Observed run failed: %s"), *Error));
		return false;
	}

	TestEqual(TEXT("Read-only observation does not change the deterministic digest"),
		FUnifiedSimulationRunner::BuildDeterministicDigest(Observed),
		FUnifiedSimulationRunner::BuildDeterministicDigest(Baseline));
	TestEqual(TEXT("Read-only observation does not change transaction count"), Observed.Transactions.Num(), Baseline.Transactions.Num());
	TestEqual(TEXT("Read-only observation does not change event count"), Observed.Events.Num(), Baseline.Events.Num());
	TestEqual(TEXT("Read-only observation does not change activation count"), Observed.ActivationObservations.Num(), Baseline.ActivationObservations.Num());
	TestEqual(TEXT("Read-only observation does not change LOD transition count"), Observed.LODTransitions.Num(), Baseline.LODTransitions.Num());
	TestEqual(TEXT("Observer reads are excluded from production ledger-query diagnostics"), Observed.Diagnostics.LedgerQueryCount, Baseline.Diagnostics.LedgerQueryCount);
	TestTrue(TEXT("Observed run remains hard-error free"), Observed.IsHardErrorFree());

	TestEqual(TEXT("Observer receives every T+1 hourly state"), Recorder.Hours.Num(), 67 * 24);
	for (int32 Index = 0; Index < Recorder.Hours.Num(); ++Index)
	{
		const FUnifiedHourObservation& Hour = Recorder.Hours[Index];
		const int64 ExpectedTime = FSimulationTime::FromDays(-7).Minutes + (Index + 1) * MinutesPerHour;
		TestEqual(*FString::Printf(TEXT("Hour callback %d is monotonic T+1"), Index), Hour.GameTime.Minutes, ExpectedTime);
		TestEqual(*FString::Printf(TEXT("Hour callback %d aligns Kingdom A"), Index), Hour.KingdomA.GameTime.Minutes, Hour.GameTime.Minutes);
		TestEqual(*FString::Printf(TEXT("Hour callback %d aligns Kingdom B"), Index), Hour.KingdomB.GameTime.Minutes, Hour.GameTime.Minutes);
		if (Hour.GameTime.Minutes % (6 * MinutesPerHour) == 0)
		{
			TestTrue(*FString::Printf(TEXT("Hour callback %d includes non-empty 6h cohorts"), Index), Hour.Cohorts.Num() > 0);
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("Hour callback %d omits off-cadence cohorts"), Index), Hour.Cohorts.Num(), 0);
		}
	}

	TestEqual(TEXT("Event sink records every authoritative event"), Recorder.Events.Num(), Observed.Events.Num());
	TestEqual(TEXT("Event sink records every authoritative transaction"), Recorder.Transactions.Num(), Observed.Transactions.Num());
	TestEqual(TEXT("Event sink records every authoritative LOD transition"), Recorder.LODTransitions.Num(), Observed.LODTransitions.Num());
	TestEqual(TEXT("Event sink records every finalized activation"), Recorder.Activations.Num(), Observed.ActivationObservations.Num());
	TestEqual(TEXT("Observer records one NPC snapshot per finalized activation"), Recorder.NPCSnapshots.Num(), Observed.ActivationObservations.Num());

	for (int32 Index = 0; Index < Recorder.Events.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Event callback %d preserves EventID order"), Index), Recorder.Events[Index].EventID, Observed.Events[Index].EventID);
	}
	for (int32 Index = 0; Index < Recorder.Transactions.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Transaction callback %d preserves TransactionID order"), Index), Recorder.Transactions[Index].TransactionID, Observed.Transactions[Index].TransactionID);
	}
	for (int32 Index = 0; Index < Recorder.LODTransitions.Num(); ++Index)
	{
		const FLODTransitionRecord& Callback = Recorder.LODTransitions[Index];
		const FLODTransitionRecord& Authority = Observed.LODTransitions[Index];
		TestEqual(*FString::Printf(TEXT("LOD callback %d preserves resident identity"), Index), Callback.PersistentID, Authority.PersistentID);
		TestEqual(*FString::Printf(TEXT("LOD callback %d preserves transition result"), Index), Callback.Result, ELODTransitionResult::Committed);
		TestEqual(*FString::Printf(TEXT("LOD callback %d commits without time drift"), Index), Callback.CommittedTime.Minutes, Callback.RequestedTime.Minutes);
	}
	for (int32 Index = 0; Index < Recorder.Activations.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Activation callback %d preserves resident order"), Index), Recorder.Activations[Index].ResidentID, Observed.ActivationObservations[Index].ResidentID);
		TestTrue(*FString::Printf(TEXT("NPC snapshot %d contains a finalized first action"), Index), Recorder.NPCSnapshots[Index].FirstAction != EIndividualAction::None);
		TestEqual(*FString::Printf(TEXT("NPC snapshot %d uses the activation time"), Index), Recorder.NPCSnapshots[Index].GameTime.Minutes, Recorder.Activations[Index].ActivationTime.Minutes);
	}

	return true;
}

#endif
