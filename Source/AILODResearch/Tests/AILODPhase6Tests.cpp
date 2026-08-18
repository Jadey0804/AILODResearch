// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

#include "../Simulation/AILODExperimentLogging.h"
#include "../Simulation/AILODExperimentRunner.h"
#include "../Simulation/AILODLogSchema.h"
#include "../Simulation/AILODOfflineMetrics.h"
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

	template <SIZE_T FieldCount>
	FString ExpectedCsvHeader(const AILOD::LogSchema::FFieldDefinition (&Fields)[FieldCount])
	{
		TArray<FString> Names;
		for (const AILOD::LogSchema::FFieldDefinition& Field : Fields)
		{
			Names.Add(Field.Name);
		}
		return FString::Join(Names, TEXT(","));
	}

	bool ParseCsvLine(const FString& Line, TArray<FString>& OutFields)
	{
		OutFields.Reset();
		FString Field;
		bool bQuoted = false;
		for (int32 Index = 0; Index < Line.Len(); ++Index)
		{
			const TCHAR Character = Line[Index];
			if (bQuoted)
			{
				if (Character == TEXT('"'))
				{
					if (Index + 1 < Line.Len() && Line[Index + 1] == TEXT('"'))
					{
						Field.AppendChar(TEXT('"'));
						++Index;
					}
					else
					{
						bQuoted = false;
					}
				}
				else
				{
					Field.AppendChar(Character);
				}
			}
			else if (Character == TEXT('"') && Field.IsEmpty())
			{
				bQuoted = true;
			}
			else if (Character == TEXT(','))
			{
				OutFields.Add(MoveTemp(Field));
				Field.Reset();
			}
			else
			{
				Field.AppendChar(Character);
			}
		}
		if (bQuoted)
		{
			return false;
		}
		OutFields.Add(MoveTemp(Field));
		return true;
	}

	AILOD::FUnifiedRunLogMetadata MakePhase6DMetadata(const FString& OutputDirectory)
	{
		AILOD::FUnifiedRunLogMetadata Metadata;
		Metadata.OutputDirectory = OutputDirectory;
		Metadata.ExperimentID = TEXT("PHASE6D-CHECKPOINT");
		Metadata.RunID = TEXT("P-SI-20260810");
		Metadata.PopulationManifestSHA256 = FString::ChrN(64, TEXT('1'));
		Metadata.DamageListSHA256 = FString::ChrN(64, TEXT('2'));
		Metadata.PersistentPoolSHA256 = FString::ChrN(64, TEXT('3'));
		Metadata.GitCommit = TEXT("eb44bf3");
		Metadata.UEVersion = TEXT("5.4");
		Metadata.BuildType = TEXT("Development");
		Metadata.Hardware = TEXT("Phase6D-AutomationFixture");
		Metadata.LogMode = TEXT("EngineeringAccuracy");
		Metadata.StartTime = TEXT("2026-08-17T00:00:00Z");
		Metadata.EndTime = TEXT("2026-08-17T00:01:00Z");
		return Metadata;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6DRawRunLoggingTest,
	"AILODResearch.Phase6.RawRunLogging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6DRawRunLoggingTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;
	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6DCheckpoint"));
	const FString RunAPath = FPaths::Combine(TestRoot, TEXT("RunA"));
	const FString RunBPath = FPaths::Combine(TestRoot, TEXT("RunB"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);

	FUnifiedRunOptions BaselineOptions;
	BaselineOptions.Mode = EUnifiedRunMode::Accuracy;
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
		AddError(FString::Printf(TEXT("Phase 6D unlogged baseline failed: %s"), *Error));
		return false;
	}

	auto RunLogged = [this, &BaselineOptions](FUnifiedRunLogWriter& Writer, FUnifiedRunResult& OutResult, FString& OutError)
	{
		FUnifiedRunOptions Options = BaselineOptions;
		Options.Observer = &Writer;
		Options.EventSink = &Writer;
		return FUnifiedSimulationRunner::Run(
			MakePhase6AConfig(),
			EUnifiedSimulationMethod::Proposed,
			EStage2Scenario::StateImport,
			Options,
			OutResult,
			OutError);
	};

	FUnifiedRunLogWriter WriterA;
	FUnifiedRunLogWriter WriterB;
	FUnifiedRunResult ResultA;
	FUnifiedRunResult ResultB;
	if (!RunLogged(WriterA, ResultA, Error) || !RunLogged(WriterB, ResultB, Error))
	{
		AddError(FString::Printf(TEXT("Phase 6D logged run failed: %s"), *Error));
		return false;
	}

	const FString BaselineDigest = FUnifiedSimulationRunner::BuildDeterministicDigest(Baseline);
	TestEqual(TEXT("Enabling raw logging does not change the simulation digest"), FUnifiedSimulationRunner::BuildDeterministicDigest(ResultA), BaselineDigest);
	TestEqual(TEXT("Repeating the logged Seed preserves the simulation digest"), FUnifiedSimulationRunner::BuildDeterministicDigest(ResultB), BaselineDigest);
	TestTrue(TEXT("Logged run A remains hard-error free"), ResultA.IsHardErrorFree());
	TestTrue(TEXT("Logged run B remains hard-error free"), ResultB.IsHardErrorFree());

	const FUnifiedRunLogMetadata MetadataA = MakePhase6DMetadata(RunAPath);
	const FUnifiedRunLogMetadata MetadataB = MakePhase6DMetadata(RunBPath);
	TestTrue(TEXT("Run A raw logs write"), WriterA.WriteRun(ResultA, MetadataA, Error));
	if (!Error.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Run A logging error: %s"), *Error));
	}
	TestTrue(TEXT("Run B raw logs write"), WriterB.WriteRun(ResultB, MetadataB, Error));
	if (!Error.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Run B logging error: %s"), *Error));
	}

	TestEqual(TEXT("Writer hours reconcile with the production session"), WriterA.GetHourObservationCount(), 67 * 24);
	TestEqual(TEXT("Writer events reconcile with the authoritative event count"), WriterA.GetEventCount(), static_cast<int32>(ResultA.Diagnostics.EventCount));
	TestEqual(TEXT("Writer transactions reconcile with the authoritative ledger"), WriterA.GetTransactionCount(), ResultA.Transactions.Num());
	TestEqual(TEXT("Writer transitions reconcile with the authoritative transition trace"), WriterA.GetLODTransitionCount(), ResultA.LODTransitions.Num());
	TestEqual(TEXT("Writer activations reconcile with the authoritative activation trace"), WriterA.GetActivationObservationCount(), ResultA.ActivationObservations.Num());
	TestEqual(TEXT("Writer NPC snapshots reconcile with finalized activations"), WriterA.GetNPCObservationCount(), ResultA.ActivationObservations.Num());

	const TArray<FString> ExpectedFiles =
	{
		RunManifestFile,
		KingdomTimeseriesFile,
		CohortTimeseriesFile,
		NPCSnapshotsFile,
		SimulationEventsFile,
		LODTransitionsFile,
		LedgerTransactionsFile
	};
	const TArray<FString> DeterministicDomainFiles =
	{
		KingdomTimeseriesFile,
		CohortTimeseriesFile,
		NPCSnapshotsFile,
		SimulationEventsFile,
		LODTransitionsFile,
		LedgerTransactionsFile
	};
	for (const FString& Directory : { RunAPath, RunBPath })
	{
		TArray<FString> ActualFiles;
		IFileManager::Get().FindFiles(ActualFiles, *FPaths::Combine(Directory, TEXT("*")), true, false);
		ActualFiles.Sort();
		TArray<FString> SortedExpected = ExpectedFiles;
		SortedExpected.Sort();
		TestEqual(*FString::Printf(TEXT("%s contains exactly the seven Phase 6D files"), *Directory), FString::Join(ActualFiles, TEXT("|")), FString::Join(SortedExpected, TEXT("|")));
		TestFalse(TEXT("Phase 6D does not emit metrics_summary.csv"), FPaths::FileExists(FPaths::Combine(Directory, MetricsSummaryFile)));
		TestFalse(TEXT("Phase 6D does not emit performance_1s.csv"), FPaths::FileExists(FPaths::Combine(Directory, PerformanceFile)));
	}

	for (const FString& FileName : DeterministicDomainFiles)
	{
		FString ContentsA;
		FString ContentsB;
		TestTrue(*FString::Printf(TEXT("Run A %s loads"), *FileName), FFileHelper::LoadFileToString(ContentsA, *FPaths::Combine(RunAPath, FileName)));
		TestTrue(*FString::Printf(TEXT("Run B %s loads"), *FileName), FFileHelper::LoadFileToString(ContentsB, *FPaths::Combine(RunBPath, FileName)));
		TestEqual(*FString::Printf(TEXT("Same Seed preserves deterministic %s bytes"), *FileName), ContentsB, ContentsA);
	}

	FString ManifestText;
	TestTrue(TEXT("Run manifest loads"), FFileHelper::LoadFileToString(ManifestText, *FPaths::Combine(RunAPath, RunManifestFile)));
	TSharedPtr<FJsonObject> Manifest;
	const TSharedRef<TJsonReader<>> ManifestReader = TJsonReaderFactory<>::Create(ManifestText);
	TestTrue(TEXT("Run manifest parses independently"), FJsonSerializer::Deserialize(ManifestReader, Manifest) && Manifest.IsValid());
	if (Manifest.IsValid())
	{
		for (const FFieldDefinition& Field : RunManifestFields)
		{
			TestTrue(*FString::Printf(TEXT("Run manifest contains %s"), Field.Name), Manifest->HasField(Field.Name));
		}
		TestEqual(TEXT("Manifest records the log mode"), Manifest->GetStringField(TEXT("log_mode")), MetadataA.LogMode);
		TestEqual(TEXT("Manifest records the config hash"), Manifest->GetStringField(TEXT("config_hash")), ResultA.ConfigHash);
		TestTrue(TEXT("Manifest records a valid run"), Manifest->GetBoolField(TEXT("valid")));
		const TSharedPtr<FJsonObject>* RunParameters = nullptr;
		TestTrue(TEXT("Manifest contains reproducible run parameters"), Manifest->TryGetObjectField(TEXT("parameters"), RunParameters) && RunParameters != nullptr);
		if (RunParameters != nullptr)
		{
			TestEqual(TEXT("Manifest records population per kingdom"), static_cast<int32>((*RunParameters)->GetNumberField(TEXT("population_per_kingdom"))), ResultA.PopulationPerKingdom);
			TestEqual(TEXT("Manifest records the run mode"), (*RunParameters)->GetStringField(TEXT("run_mode")), FString(TEXT("Accuracy")));
		}
		const TSharedPtr<FJsonObject>* Measurements = nullptr;
		TestTrue(TEXT("Manifest contains isolated cost measurements"), Manifest->TryGetObjectField(TEXT("measurement_summary"), Measurements) && Measurements != nullptr);
		if (Measurements != nullptr)
		{
			TestTrue(TEXT("Manifest records positive production cost"), (*Measurements)->GetNumberField(TEXT("production_cpu_ms")) > 0.0);
			TestTrue(TEXT("Manifest records non-negative audit cost"), (*Measurements)->GetNumberField(TEXT("audit_cpu_ms")) >= 0.0);
			TestTrue(TEXT("Manifest records non-negative serialization cost"), (*Measurements)->GetNumberField(TEXT("serialization_cpu_ms")) >= 0.0);
			TestTrue(TEXT("Manifest records non-negative file-write cost"), (*Measurements)->GetNumberField(TEXT("file_write_cpu_ms")) >= 0.0);
			TestEqual(
				TEXT("Manifest freezes the production CPU scope"),
				(*Measurements)->GetStringField(TEXT("ai_cpu_scope")),
				FString(TEXT("production_only_excludes_validation_audit_snapshot_observer_and_logging")));
		}
	}

	auto ValidateCsv = [this, &MetadataA](
		const FString& FileName,
		const FString& ExpectedHeader,
		const int32 ExpectedFieldCount,
		const int32 ExpectedDataRows)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(MetadataA.OutputDirectory, FileName)))
		{
			AddError(FString::Printf(TEXT("CSV %s failed to load."), *FileName));
			return;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		if (Lines.Num() != ExpectedDataRows + 1)
		{
			AddError(FString::Printf(TEXT("CSV %s row count is %d, expected %d."), *FileName, Lines.Num() - 1, ExpectedDataRows));
			return;
		}
		if (Lines[0] != ExpectedHeader)
		{
			AddError(FString::Printf(TEXT("CSV %s header does not match the frozen Schema."), *FileName));
			return;
		}
		for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
		{
			TArray<FString> Fields;
			if (!ParseCsvLine(Lines[LineIndex], Fields) || Fields.Num() != ExpectedFieldCount)
			{
				AddError(FString::Printf(TEXT("CSV %s row %d is not independently parseable."), *FileName, LineIndex));
				return;
			}
			if (Fields[0] != SchemaVersion
				|| Fields[1] != MetadataA.ExperimentID
				|| Fields[2] != MetadataA.RunID
				|| Fields[3] != TEXT("Proposed")
				|| Fields[4] != TEXT("StateImport")
				|| Fields[5] != TEXT("20260810")
				|| Fields[6].IsEmpty())
			{
				AddError(FString::Printf(TEXT("CSV %s row %d has invalid common identity fields."), *FileName, LineIndex));
				return;
			}
		}
	};

	ValidateCsv(KingdomTimeseriesFile, ExpectedCsvHeader(KingdomTimeseriesFields), UE_ARRAY_COUNT(KingdomTimeseriesFields), WriterA.GetHourObservationCount() * 2);
	ValidateCsv(CohortTimeseriesFile, ExpectedCsvHeader(CohortTimeseriesFields), UE_ARRAY_COUNT(CohortTimeseriesFields), WriterA.GetCohortObservationCount());
	ValidateCsv(NPCSnapshotsFile, ExpectedCsvHeader(NPCSnapshotFields), UE_ARRAY_COUNT(NPCSnapshotFields), WriterA.GetNPCObservationCount());

	auto ValidateJsonl = [this, &MetadataA](
		const FString& FileName,
		const FFieldDefinition* RequiredFields,
		const int32 RequiredFieldCount,
		const int32 ExpectedRows,
		const TCHAR* OrderedIDField)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(MetadataA.OutputDirectory, FileName)))
		{
			AddError(FString::Printf(TEXT("JSONL %s failed to load."), *FileName));
			return;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		if (Lines.Num() != ExpectedRows)
		{
			AddError(FString::Printf(TEXT("JSONL %s row count is %d, expected %d."), *FileName, Lines.Num(), ExpectedRows));
			return;
		}
		double PreviousID = 0.0;
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Lines[LineIndex]);
			if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
			{
				AddError(FString::Printf(TEXT("JSONL %s row %d failed to parse."), *FileName, LineIndex));
				return;
			}
			for (int32 FieldIndex = 0; FieldIndex < RequiredFieldCount; ++FieldIndex)
			{
				if (!Object->HasField(RequiredFields[FieldIndex].Name))
				{
					AddError(FString::Printf(TEXT("JSONL %s row %d is missing %s."), *FileName, LineIndex, RequiredFields[FieldIndex].Name));
					return;
				}
			}
			if (Object->GetStringField(TEXT("schema_version")) != SchemaVersion
				|| Object->GetStringField(TEXT("experiment_id")) != MetadataA.ExperimentID
				|| Object->GetStringField(TEXT("run_id")) != MetadataA.RunID
				|| Object->GetStringField(TEXT("method")) != TEXT("Proposed")
				|| Object->GetStringField(TEXT("scenario")) != TEXT("StateImport")
				|| static_cast<int32>(Object->GetNumberField(TEXT("seed"))) != 20260810
				|| Object->GetStringField(TEXT("game_time")).IsEmpty())
			{
				AddError(FString::Printf(TEXT("JSONL %s row %d has invalid common identity fields."), *FileName, LineIndex));
				return;
			}
			if (OrderedIDField != nullptr)
			{
				const double CurrentID = Object->GetNumberField(OrderedIDField);
				if (CurrentID <= PreviousID)
				{
					AddError(FString::Printf(TEXT("JSONL %s row order is not strictly increasing."), *FileName));
					return;
				}
				PreviousID = CurrentID;
			}
		}
	};

	ValidateJsonl(SimulationEventsFile, SimulationEventFields, UE_ARRAY_COUNT(SimulationEventFields), WriterA.GetEventCount(), TEXT("event_id"));
	ValidateJsonl(LODTransitionsFile, LODTransitionFields, UE_ARRAY_COUNT(LODTransitionFields), WriterA.GetLODTransitionCount(), nullptr);
	ValidateJsonl(LedgerTransactionsFile, LedgerTransactionFields, UE_ARRAY_COUNT(LedgerTransactionFields), WriterA.GetTransactionCount(), TEXT("transaction_id"));

	AddInfo(FString::Printf(
		TEXT("Phase6D logs=%s hours=%d cohorts=%d npcs=%d events=%d transitions=%d transactions=%d digest=%s"),
		*TestRoot,
		WriterA.GetHourObservationCount(),
		WriterA.GetCohortObservationCount(),
		WriterA.GetNPCObservationCount(),
		WriterA.GetEventCount(),
		WriterA.GetLODTransitionCount(),
		WriterA.GetTransactionCount(),
		*BaselineDigest));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6EExperimentRunnerAndMetricsTest,
	"AILODResearch.Phase6.ExperimentRunnerAndOfflineMetrics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6EExperimentRunnerAndMetricsTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;
	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6ECheckpoint"));
	const FString ReplayRoot = FPaths::Combine(TestRoot, TEXT("Replay"));
	const FString SummaryPath = FPaths::Combine(TestRoot, MetricsSummaryFile);
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);

	FExperimentMatrixRequest Request;
	Request.OutputRoot = TestRoot;
	Request.ExperimentID = TEXT("PHASE6E-ENGINEERING");
	Request.Methods = { EUnifiedSimulationMethod::Oracle, EUnifiedSimulationMethod::Proposed };
	Request.Scenarios = { EStage2Scenario::None, EStage2Scenario::StateImport };
	Request.Seeds = { 20260810 };
	Request.PopulationPerKingdom = 100;
	Request.GitCommit = TEXT("93282ed");
	Request.UEVersion = TEXT("5.4");
	Request.BuildType = TEXT("Development");
	Request.Hardware = TEXT("Phase6E-AutomationFixture");
	Request.LogMode = TEXT("EngineeringAccuracy");
	Request.StartTime = TEXT("2026-08-17T00:00:00Z");
	Request.EndTime = TEXT("2026-08-17T00:01:00Z");

	TArray<FExperimentRunRecord> Runs;
	FString Error;
	if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
	{
		AddError(FString::Printf(TEXT("Phase 6E engineering matrix failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("Engineering matrix contains exactly method x scenario x seed runs"), Runs.Num(), 4);
	TestTrue(TEXT("Runner writes the shared population input"), FPaths::FileExists(FPaths::Combine(TestRoot, TEXT("Inputs/Seed-20260810"), InitialPopulationManifestFile)));
	TestTrue(TEXT("Runner writes the shared damage input"), FPaths::FileExists(FPaths::Combine(TestRoot, TEXT("Inputs/Seed-20260810"), EarthquakeDamageListFile)));
	TestTrue(TEXT("Runner writes the shared continuity pool"), FPaths::FileExists(FPaths::Combine(TestRoot, TEXT("Inputs/Seed-20260810"), PersistentTestPoolFile)));

	const FExperimentRunRecord* ProposedPolicyRun = Runs.FindByPredicate([](const FExperimentRunRecord& Run)
	{
		return Run.RunID == TEXT("Proposed-StateImport-20260810");
	});
	if (ProposedPolicyRun == nullptr)
	{
		AddError(TEXT("Engineering matrix did not produce Proposed-StateImport-20260810."));
		return false;
	}
	for (const FExperimentRunRecord& Run : Runs)
	{
		TestFalse(*FString::Printf(TEXT("%s does not emit Phase 6F performance samples"), *Run.RunID), FPaths::FileExists(FPaths::Combine(Run.RunDirectory, PerformanceFile)));
		TestFalse(*FString::Printf(TEXT("%s keeps cross-run metrics outside the raw run"), *Run.RunID), FPaths::FileExists(FPaths::Combine(Run.RunDirectory, MetricsSummaryFile)));
	}

	FExperimentRunRecord Replay;
	if (!FExperimentRunner::ReplayFromManifest(FPaths::Combine(ProposedPolicyRun->RunDirectory, RunManifestFile), ReplayRoot, Replay, Error))
	{
		AddError(FString::Printf(TEXT("Manifest replay failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("Manifest replay reproduces the deterministic domain digest"), Replay.DeterministicDigest, ProposedPolicyRun->DeterministicDigest);
	for (const FString& File : { FString(KingdomTimeseriesFile), FString(CohortTimeseriesFile), FString(NPCSnapshotsFile), FString(SimulationEventsFile), FString(LODTransitionsFile), FString(LedgerTransactionsFile) })
	{
		FString Original;
		FString Rebuilt;
		TestTrue(*FString::Printf(TEXT("Original %s loads"), *File), FFileHelper::LoadFileToString(Original, *FPaths::Combine(ProposedPolicyRun->RunDirectory, File)));
		TestTrue(*FString::Printf(TEXT("Replayed %s loads"), *File), FFileHelper::LoadFileToString(Rebuilt, *FPaths::Combine(ReplayRoot, File)));
		TestEqual(*FString::Printf(TEXT("Manifest replay reproduces %s bytes"), *File), Rebuilt, Original);
	}

	TestTrue(TEXT("Offline evaluator builds metrics_summary.csv from raw logs"), FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error));
	if (!Error.IsEmpty()) AddError(FString::Printf(TEXT("Offline metric error: %s"), *Error));
	FString FirstSummary;
	TestTrue(TEXT("First metrics summary loads"), FFileHelper::LoadFileToString(FirstSummary, *SummaryPath));
	TestTrue(TEXT("Summary contains trajectory error"), FirstSummary.Contains(TEXT("Trajectory.DamagedWaiting")));
	TestTrue(TEXT("Summary contains policy effect error"), FirstSummary.Contains(TEXT("PolicyEffect.ForestWood")));
	TestTrue(TEXT("Summary contains behavior TVD"), FirstSummary.Contains(TEXT("Behavior.TVD")));
	TestTrue(TEXT("Summary contains continuity mismatch rates"), FirstSummary.Contains(TEXT("Continuity.MoneyMismatchRate")));
	TestTrue(TEXT("Summary contains hard-error counts"), FirstSummary.Contains(TEXT("HardError.wood_residual")));
	TestFalse(TEXT("Accuracy summary does not fabricate performance samples"), FirstSummary.Contains(TEXT("Performance.SampleCount")));

	TestTrue(TEXT("Metrics summary can be deleted"), IFileManager::Get().Delete(*SummaryPath));
	TestTrue(TEXT("Offline evaluator rebuilds metrics using only raw files"), FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error));
	FString RebuiltSummary;
	TestTrue(TEXT("Rebuilt metrics summary loads"), FFileHelper::LoadFileToString(RebuiltSummary, *SummaryPath));
	TestEqual(TEXT("Offline rebuild is byte-identical"), RebuiltSummary, FirstSummary);

	const FString NPCPath = FPaths::Combine(ProposedPolicyRun->RunDirectory, NPCSnapshotsFile);
	FString OriginalNPCs;
	TestTrue(TEXT("Identity-mismatch fixture loads NPC snapshots"), FFileHelper::LoadFileToString(OriginalNPCs, *NPCPath));
	const FString TamperedNPCs = OriginalNPCs.Replace(TEXT("Resident-"), TEXT("Tampered-"), ESearchCase::CaseSensitive);
	TestTrue(TEXT("Identity-mismatch fixture writes changed immutable names"), FFileHelper::SaveStringToFile(TamperedNPCs, *NPCPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	TestTrue(TEXT("Offline evaluator rebuilds after an identity mutation"), FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error));
	FString TamperedSummary;
	TestTrue(TEXT("Identity-mismatch summary loads"), FFileHelper::LoadFileToString(TamperedSummary, *SummaryPath));
	TArray<FString> TamperedLines;
	TamperedSummary.ParseIntoArrayLines(TamperedLines, true);
	const FString* IdentityLine = TamperedLines.FindByPredicate([](const FString& Line)
	{
		return Line.Contains(TEXT("Proposed-StateImport-20260810")) && Line.Contains(TEXT("HardError.identity_mismatch"));
	});
	TestTrue(TEXT("Identity mismatch is computed from raw snapshots and Phase 0 input"), IdentityLine != nullptr && IdentityLine->Contains(TEXT(",40.000000000000,")));
	TestTrue(TEXT("Identity-mismatch fixture restores NPC snapshots"), FFileHelper::SaveStringToFile(OriginalNPCs, *NPCPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	TestTrue(TEXT("Clean summary rebuilds after identity fixture restoration"), FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error));

	const FString LedgerPath = FPaths::Combine(ProposedPolicyRun->RunDirectory, LedgerTransactionsFile);
	const FString HiddenLedgerPath = LedgerPath + TEXT(".hidden");
	TestTrue(TEXT("Missing-file fixture hides a required raw log"), IFileManager::Get().Move(*HiddenLedgerPath, *LedgerPath));
	TestFalse(TEXT("Offline evaluator rejects a missing raw file"), FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error));
	TestTrue(TEXT("Missing raw file produces an explicit error"), Error.Contains(LedgerTransactionsFile));
	TestTrue(TEXT("Missing-file fixture restores the raw log"), IFileManager::Get().Move(*LedgerPath, *HiddenLedgerPath));

	const FString OraclePolicyPath = FPaths::Combine(TestRoot, TEXT("Runs/Oracle-StateImport-20260810"));
	const FString HiddenOraclePath = FPaths::Combine(TestRoot, TEXT("Oracle-StateImport-20260810.hidden"));
	TestTrue(TEXT("Pairing fixture hides the policy Oracle"), IFileManager::Get().Move(*HiddenOraclePath, *OraclePolicyPath, true, true, false, true));
	TestFalse(TEXT("Offline evaluator rejects a missing Oracle pair"), FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error));
	TestTrue(TEXT("Missing Oracle pair produces an explicit error"), Error.Contains(TEXT("no paired Oracle")));
	TestTrue(TEXT("Pairing fixture restores the policy Oracle"), IFileManager::Get().Move(*OraclePolicyPath, *HiddenOraclePath, true, true, false, true));

	const FString ProposedNonePath = FPaths::Combine(TestRoot, TEXT("Runs/Proposed-None-20260810"));
	const FString HiddenNonePath = FPaths::Combine(TestRoot, TEXT("Proposed-None-20260810.hidden"));
	TestTrue(TEXT("None-baseline fixture hides the method baseline"), IFileManager::Get().Move(*HiddenNonePath, *ProposedNonePath, true, true, false, true));
	TestFalse(TEXT("Offline evaluator rejects a missing None baseline"), FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error));
	TestTrue(TEXT("Missing None baseline produces an explicit error"), Error.Contains(TEXT("Method/None")));
	TestTrue(TEXT("None-baseline fixture restores the method baseline"), IFileManager::Get().Move(*ProposedNonePath, *HiddenNonePath, true, true, false, true));

	AddInfo(FString::Printf(TEXT("Phase6E root=%s runs=%d digest=%s summary_bytes=%d"), *TestRoot, Runs.Num(), *ProposedPolicyRun->DeterministicDigest, FirstSummary.Len()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6FModeBoundaryTest,
	"AILODResearch.Phase6.MeasurementModeBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6FModeBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	const FPhase0Config Config = MakePhase6AConfig();
	auto RunMode = [this, &Config](const EUnifiedRunMode Mode, FUnifiedRunResult& OutResult)
	{
		FUnifiedRunOptions Options;
		Options.Mode = Mode;
		Options.bRetainCompletedEvents = Mode != EUnifiedRunMode::Performance;
		Options.bRecordSnapshots = Mode != EUnifiedRunMode::Performance;
		Options.bVerifyCohortApproximation = Mode == EUnifiedRunMode::Validation;
		FString Error;
		if (!FUnifiedSimulationRunner::Run(Config, EUnifiedSimulationMethod::Proposed, EStage2Scenario::StateImport, Options, OutResult, Error))
		{
			AddError(FString::Printf(TEXT("Phase 6F mode run failed: %s"), *Error));
			return false;
		}
		return true;
	};

	FUnifiedRunResult Validation;
	FUnifiedRunResult Accuracy;
	FUnifiedRunResult Performance;
	if (!RunMode(EUnifiedRunMode::Validation, Validation)
		|| !RunMode(EUnifiedRunMode::Accuracy, Accuracy)
		|| !RunMode(EUnifiedRunMode::Performance, Performance))
	{
		return false;
	}

	const FString ValidationDigest = FUnifiedSimulationRunner::BuildDeterministicDigest(Validation);
	const FString AccuracyDigest = FUnifiedSimulationRunner::BuildDeterministicDigest(Accuracy);
	const FString PerformanceDigest = FUnifiedSimulationRunner::BuildDeterministicDigest(Performance);
	TestEqual(TEXT("Validation and Accuracy preserve the same domain result"), AccuracyDigest, ValidationDigest);
	TestEqual(TEXT("Performance and Accuracy preserve the same domain result"), PerformanceDigest, AccuracyDigest);
	TestTrue(TEXT("Validation is hard-error free"), Validation.IsHardErrorFree());
	TestTrue(TEXT("Accuracy is hard-error free"), Accuracy.IsHardErrorFree());
	TestTrue(TEXT("Performance is hard-error free"), Performance.IsHardErrorFree());

	TestTrue(TEXT("Validation performs the optional per-member approximation recompute"), Validation.Diagnostics.ValidationPlanningEvaluationCount > 0);
	TestEqual(TEXT("Accuracy does not perform per-member approximation recompute"), Accuracy.Diagnostics.ValidationPlanningEvaluationCount, int64(0));
	TestEqual(TEXT("Performance does not perform per-member approximation recompute"), Performance.Diagnostics.ValidationPlanningEvaluationCount, int64(0));
	TestEqual(TEXT("Validation performs initial plus hourly full audits"), Validation.Diagnostics.FullAuditCount, int64(1609));
	TestEqual(TEXT("Accuracy retains the hourly correctness gate"), Accuracy.Diagnostics.FullAuditCount, int64(1609));
	TestEqual(TEXT("Performance performs only initial and final full audits"), Performance.Diagnostics.FullAuditCount, int64(2));
	TestTrue(TEXT("Validation records resident snapshots"), Validation.Diagnostics.SnapshotResidentVisitCount > 0);
	TestTrue(TEXT("Accuracy records resident snapshots"), Accuracy.Diagnostics.SnapshotResidentVisitCount > 0);
	TestEqual(TEXT("Performance does not visit residents for snapshots"), Performance.Diagnostics.SnapshotResidentVisitCount, int64(0));

	TestTrue(TEXT("Validation production cost is recorded"), Validation.CostBreakdown.ProductionCpuMs > 0.0);
	TestTrue(TEXT("Accuracy production cost is recorded"), Accuracy.CostBreakdown.ProductionCpuMs > 0.0);
	TestTrue(TEXT("Performance production cost is recorded"), Performance.CostBreakdown.ProductionCpuMs > 0.0);
	TestTrue(TEXT("Validation recompute cost is isolated"), Validation.CostBreakdown.ValidationCpuMs > 0.0);
	TestEqual(TEXT("Accuracy has no validation recompute cost"), Accuracy.CostBreakdown.ValidationCpuMs, 0.0);
	TestEqual(TEXT("Performance has no validation recompute cost"), Performance.CostBreakdown.ValidationCpuMs, 0.0);
	TestEqual(TEXT("Performance has no snapshot cost"), Performance.CostBreakdown.SnapshotCpuMs, 0.0);
	TestEqual(TEXT("Performance has no observer cost"), Performance.CostBreakdown.ObserverCpuMs, 0.0);
	TestTrue(TEXT("Performance final audit cost is isolated"), Performance.CostBreakdown.AuditCpuMs > 0.0);

	AddInfo(FString::Printf(
		TEXT("Phase6F modes digest=%s validation_ms=%.3f accuracy_ms=%.3f performance_ms=%.3f validation_extra_ms=%.3f performance_audit_ms=%.3f"),
		*PerformanceDigest,
		Validation.CostBreakdown.ProductionCpuMs,
		Accuracy.CostBreakdown.ProductionCpuMs,
		Performance.CostBreakdown.ProductionCpuMs,
		Validation.CostBreakdown.ValidationCpuMs,
		Performance.CostBreakdown.AuditCpuMs));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6FPerformanceLoggingSmokeTest,
	"AILODResearch.Phase6.PerformanceLoggingScaleSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6FPerformanceLoggingSmokeTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;
	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6FCheckpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	FExperimentRunRecord Proposed200;
	bool bFoundProposed200 = false;
	int32 TotalRuns = 0;

	for (const int32 TotalPopulation : { 200, 2000, 10000, 20000 })
	{
		FExperimentMatrixRequest Request;
		Request.OutputRoot = FPaths::Combine(TestRoot, FString::Printf(TEXT("Population-%d"), TotalPopulation));
		Request.ExperimentID = FString::Printf(TEXT("PHASE6F-ENGINEERING-%d"), TotalPopulation);
		Request.Methods = { EUnifiedSimulationMethod::Proposed, EUnifiedSimulationMethod::Simple, EUnifiedSimulationMethod::PerAgent };
		Request.Scenarios = { EStage2Scenario::StateImport };
		Request.Seeds = { 20260810 };
		Request.PopulationPerKingdom = TotalPopulation / 2;
		Request.Mode = EUnifiedRunMode::Performance;
		Request.GitCommit = TEXT("6ee6873");
		Request.UEVersion = TEXT("5.4");
		Request.BuildType = TEXT("Development");
		Request.Hardware = TEXT("Phase6F-AutomationFixture");
		Request.LogMode = TEXT("EngineeringPerformance");
		Request.StartTime = TEXT("2026-08-17T00:00:00Z");
		Request.EndTime = TEXT("2026-08-17T00:01:00Z");

		TArray<FExperimentRunRecord> Runs;
		FString Error;
		if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
		{
			AddError(FString::Printf(TEXT("Phase 6F %d-person performance smoke failed: %s"), TotalPopulation, *Error));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%d-person smoke runs all deployable methods"), TotalPopulation), Runs.Num(), 3);
		TotalRuns += Runs.Num();

		for (const FExperimentRunRecord& Run : Runs)
		{
			TestEqual(*FString::Printf(TEXT("%s uses Performance mode"), *Run.RunID), static_cast<int32>(Run.Mode), static_cast<int32>(EUnifiedRunMode::Performance));
			TestEqual(*FString::Printf(TEXT("%s records the requested per-kingdom population"), *Run.RunID), Run.PopulationPerKingdom, TotalPopulation / 2);
			TestTrue(*FString::Printf(TEXT("%s is hard-error free"), *Run.RunID), Run.bHardErrorFree);
			TestTrue(*FString::Printf(TEXT("%s emits at least one performance sample"), *Run.RunID), Run.PerformanceSampleCount > 0);
			TestEqual(*FString::Printf(TEXT("%s skips validation recompute"), *Run.RunID), Run.Diagnostics.ValidationPlanningEvaluationCount, int64(0));
			TestEqual(*FString::Printf(TEXT("%s performs only initial and final full audits"), *Run.RunID), Run.Diagnostics.FullAuditCount, int64(2));
			TestEqual(*FString::Printf(TEXT("%s skips snapshot resident visits"), *Run.RunID), Run.Diagnostics.SnapshotResidentVisitCount, int64(0));
			TestTrue(*FString::Printf(TEXT("%s records production cost"), *Run.RunID), Run.CostBreakdown.ProductionCpuMs > 0.0);
			TestEqual(*FString::Printf(TEXT("%s isolates validation cost"), *Run.RunID), Run.CostBreakdown.ValidationCpuMs, 0.0);
			TestEqual(*FString::Printf(TEXT("%s isolates snapshot cost"), *Run.RunID), Run.CostBreakdown.SnapshotCpuMs, 0.0);
			TestEqual(*FString::Printf(TEXT("%s isolates observer cost"), *Run.RunID), Run.CostBreakdown.ObserverCpuMs, 0.0);

			TArray<FString> ActualFiles;
			IFileManager::Get().FindFiles(ActualFiles, *FPaths::Combine(Run.RunDirectory, TEXT("*")), true, false);
			ActualFiles.Sort();
			TArray<FString> ExpectedFiles = { PerformanceFile, RunManifestFile };
			ExpectedFiles.Sort();
			TestEqual(*FString::Printf(TEXT("%s writes only isolated performance artifacts"), *Run.RunID), FString::Join(ActualFiles, TEXT("|")), FString::Join(ExpectedFiles, TEXT("|")));

			FString PerformanceText;
			if (!FFileHelper::LoadFileToString(PerformanceText, *FPaths::Combine(Run.RunDirectory, PerformanceFile)))
			{
				AddError(FString::Printf(TEXT("%s performance_1s.csv failed to load."), *Run.RunID));
				return false;
			}
			TArray<FString> PerformanceLines;
			PerformanceText.ParseIntoArrayLines(PerformanceLines, true);
			TestEqual(*FString::Printf(TEXT("%s performance row count matches the runner"), *Run.RunID), PerformanceLines.Num(), Run.PerformanceSampleCount + 1);
			if (PerformanceLines.IsEmpty() || PerformanceLines[0] != ExpectedCsvHeader(PerformanceFields))
			{
				AddError(FString::Printf(TEXT("%s performance_1s.csv has an invalid header."), *Run.RunID));
				return false;
			}
			for (int32 LineIndex = 1; LineIndex < PerformanceLines.Num(); ++LineIndex)
			{
				TArray<FString> Fields;
				if (!ParseCsvLine(PerformanceLines[LineIndex], Fields) || Fields.Num() != UE_ARRAY_COUNT(PerformanceFields))
				{
					AddError(FString::Printf(TEXT("%s performance row %d is not independently parseable."), *Run.RunID, LineIndex));
					return false;
				}
				const double AICpuMs = FCString::Atod(*Fields[7]);
				const double MacroCpuMs = FCString::Atod(*Fields[8]);
				const double MicroCpuMs = FCString::Atod(*Fields[9]);
				const double TransitionCpuMs = FCString::Atod(*Fields[10]);
				const double MemoryMB = FCString::Atod(*Fields[11]);
				const int32 ActiveCount = FCString::Atoi(*Fields[12]);
				const int32 QueueLength = FCString::Atoi(*Fields[13]);
				if (Fields[0] != SchemaVersion
					|| Fields[1] != Request.ExperimentID
					|| Fields[2] != Run.RunID
					|| !Run.RunID.StartsWith(Fields[3] + TEXT("-"))
					|| Fields[4] != TEXT("StateImport")
					|| Fields[5] != TEXT("20260810")
					|| Fields[6].IsEmpty()
					|| !FMath::IsFinite(AICpuMs) || AICpuMs < 0.0
					|| !FMath::IsFinite(MacroCpuMs) || MacroCpuMs < 0.0
					|| !FMath::IsFinite(MicroCpuMs) || MicroCpuMs < 0.0
					|| !FMath::IsFinite(TransitionCpuMs) || TransitionCpuMs < 0.0
					|| !FMath::IsFinite(MemoryMB) || MemoryMB <= 0.0
					|| ActiveCount < 0 || ActiveCount > 50 || QueueLength < 0)
				{
					AddError(FString::Printf(TEXT("%s performance row %d violates the frozen schema or value bounds."), *Run.RunID, LineIndex));
					return false;
				}
			}

			FString ManifestText;
			TSharedPtr<FJsonObject> Manifest;
			if (!FFileHelper::LoadFileToString(ManifestText, *FPaths::Combine(Run.RunDirectory, RunManifestFile))
				|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest)
				|| !Manifest.IsValid())
			{
				AddError(FString::Printf(TEXT("%s performance manifest failed to parse."), *Run.RunID));
				return false;
			}
			const TSharedPtr<FJsonObject>* RunParameters = nullptr;
			const TSharedPtr<FJsonObject>* Measurements = nullptr;
			if (!Manifest->TryGetObjectField(TEXT("parameters"), RunParameters) || RunParameters == nullptr
				|| !Manifest->TryGetObjectField(TEXT("measurement_summary"), Measurements) || Measurements == nullptr)
			{
				AddError(FString::Printf(TEXT("%s performance manifest is missing parameters or measurement_summary."), *Run.RunID));
				return false;
			}
			TestTrue(*FString::Printf(TEXT("%s manifest is valid"), *Run.RunID), Manifest->GetBoolField(TEXT("valid")));
			TestEqual(*FString::Printf(TEXT("%s manifest preserves the deterministic digest"), *Run.RunID), Manifest->GetStringField(TEXT("deterministic_digest")), Run.DeterministicDigest);
			TestEqual(*FString::Printf(TEXT("%s manifest records Performance mode"), *Run.RunID), (*RunParameters)->GetStringField(TEXT("run_mode")), FString(TEXT("Performance")));
			TestFalse(*FString::Printf(TEXT("%s manifest disables completed-event retention"), *Run.RunID), (*RunParameters)->GetBoolField(TEXT("retain_completed_events")));
			TestFalse(*FString::Printf(TEXT("%s manifest disables snapshots"), *Run.RunID), (*RunParameters)->GetBoolField(TEXT("record_snapshots")));
			TestFalse(*FString::Printf(TEXT("%s manifest disables approximation recompute"), *Run.RunID), (*RunParameters)->GetBoolField(TEXT("verify_cohort_approximation")));
			TestFalse(*FString::Printf(TEXT("%s manifest disables macro profiling by default"), *Run.RunID), (*RunParameters)->GetBoolField(TEXT("enable_macro_profiling")));
			TestFalse(*FString::Printf(TEXT("%s manifest omits macro profile when disabled"), *Run.RunID), (*Measurements)->HasField(TEXT("macro_profile")));
			TestTrue(*FString::Printf(TEXT("%s manifest records positive production cost"), *Run.RunID), (*Measurements)->GetNumberField(TEXT("production_cpu_ms")) > 0.0);
			TestEqual(*FString::Printf(TEXT("%s manifest has zero validation cost"), *Run.RunID), (*Measurements)->GetNumberField(TEXT("validation_cpu_ms")), 0.0);
			TestEqual(*FString::Printf(TEXT("%s manifest has zero snapshot cost"), *Run.RunID), (*Measurements)->GetNumberField(TEXT("snapshot_cpu_ms")), 0.0);
			TestEqual(*FString::Printf(TEXT("%s manifest has zero observer cost"), *Run.RunID), (*Measurements)->GetNumberField(TEXT("observer_cpu_ms")), 0.0);
			TestTrue(*FString::Printf(TEXT("%s manifest isolates the final audit cost"), *Run.RunID), (*Measurements)->GetNumberField(TEXT("audit_cpu_ms")) > 0.0);
			TestTrue(*FString::Printf(TEXT("%s manifest records serialization cost"), *Run.RunID), (*Measurements)->GetNumberField(TEXT("serialization_cpu_ms")) >= 0.0);
			TestTrue(*FString::Printf(TEXT("%s manifest records file-write cost"), *Run.RunID), (*Measurements)->GetNumberField(TEXT("file_write_cpu_ms")) >= 0.0);
			TestEqual(
				*FString::Printf(TEXT("%s manifest freezes the AI CPU scope"), *Run.RunID),
				(*Measurements)->GetStringField(TEXT("ai_cpu_scope")),
				FString(TEXT("production_only_excludes_validation_audit_snapshot_observer_and_logging")));

			if (TotalPopulation == 200 && Run.RunID == TEXT("Proposed-StateImport-20260810"))
			{
				Proposed200 = Run;
				bFoundProposed200 = true;
				TestEqual(TEXT("200-person Proposed performance digest remains frozen"), Run.DeterministicDigest, FString(TEXT("D326B24A3D74128C955667DB42E8F1BADA9BC9CD")));
			}
			AddInfo(FString::Printf(
				TEXT("Phase6F population=%d run=%s samples=%d production_ms=%.3f macro_ms=%.3f micro_ms=%.3f audit_ms=%.3f digest=%s"),
				TotalPopulation,
				*Run.RunID,
				Run.PerformanceSampleCount,
				Run.CostBreakdown.ProductionCpuMs,
				Run.CostBreakdown.MacroCpuMs,
				Run.CostBreakdown.MicroCpuMs,
				Run.CostBreakdown.AuditCpuMs,
				*Run.DeterministicDigest));
		}

		const FString PerformanceSummaryPath = FPaths::Combine(Request.OutputRoot, MetricsSummaryFile);
		TestTrue(
			*FString::Printf(TEXT("%d-person performance summary rebuilds from isolated files"), TotalPopulation),
			FOfflineMetricsEvaluator::BuildSummary(Request.OutputRoot, PerformanceSummaryPath, Error));
		if (!Error.IsEmpty()) AddError(FString::Printf(TEXT("%d-person performance metric error: %s"), TotalPopulation, *Error));
		FString FirstPerformanceSummary;
		TestTrue(
			*FString::Printf(TEXT("%d-person performance summary loads"), TotalPopulation),
			FFileHelper::LoadFileToString(FirstPerformanceSummary, *PerformanceSummaryPath));
		TestTrue(*FString::Printf(TEXT("%d-person summary contains sample counts"), TotalPopulation), FirstPerformanceSummary.Contains(TEXT("Performance.SampleCount")));
		TestTrue(*FString::Printf(TEXT("%d-person summary contains AI P95"), TotalPopulation), FirstPerformanceSummary.Contains(TEXT("Performance.AICpuMs.P95")));
		TestTrue(*FString::Printf(TEXT("%d-person summary contains PerAgent speedup inputs"), TotalPopulation), FirstPerformanceSummary.Contains(TEXT("Performance.SpeedupVsPerAgent.P95AI")));
		TestFalse(*FString::Printf(TEXT("%d-person performance summary does not fabricate trajectory metrics"), TotalPopulation), FirstPerformanceSummary.Contains(TEXT("Trajectory.")));
		TestTrue(*FString::Printf(TEXT("%d-person performance summary can be deleted"), TotalPopulation), IFileManager::Get().Delete(*PerformanceSummaryPath));
		TestTrue(
			*FString::Printf(TEXT("%d-person performance summary rebuilds again from raw samples"), TotalPopulation),
			FOfflineMetricsEvaluator::BuildSummary(Request.OutputRoot, PerformanceSummaryPath, Error));
		FString RebuiltPerformanceSummary;
		TestTrue(
			*FString::Printf(TEXT("%d-person rebuilt performance summary loads"), TotalPopulation),
			FFileHelper::LoadFileToString(RebuiltPerformanceSummary, *PerformanceSummaryPath));
		TestEqual(
			*FString::Printf(TEXT("%d-person performance summary rebuild is byte-identical"), TotalPopulation),
			RebuiltPerformanceSummary,
			FirstPerformanceSummary);

		if (TotalPopulation == 200)
		{
			const FString PerformancePath = FPaths::Combine(Proposed200.RunDirectory, PerformanceFile);
			const FString HiddenPerformancePath = PerformancePath + TEXT(".hidden");
			TestTrue(TEXT("Performance missing-file fixture hides one sample file"), IFileManager::Get().Move(*HiddenPerformancePath, *PerformancePath));
			TestFalse(TEXT("Performance evaluator rejects a missing sample file"), FOfflineMetricsEvaluator::BuildSummary(Request.OutputRoot, PerformanceSummaryPath, Error));
			TestTrue(TEXT("Missing performance file produces an explicit error"), Error.Contains(PerformanceFile));
			TestTrue(TEXT("Performance missing-file fixture restores the sample file"), IFileManager::Get().Move(*PerformancePath, *HiddenPerformancePath));
			TestTrue(TEXT("Performance summary rebuilds after fixture restoration"), FOfflineMetricsEvaluator::BuildSummary(Request.OutputRoot, PerformanceSummaryPath, Error));
		}
	}

	TestEqual(TEXT("Phase 6F scale smoke runs exactly twelve engineering runs"), TotalRuns, 12);
	if (!bFoundProposed200)
	{
		AddError(TEXT("Phase 6F scale smoke did not retain the 200-person Proposed run for replay."));
		return false;
	}
	FExperimentRunRecord Replay;
	FString ReplayError;
	const FString ReplayRoot = FPaths::Combine(TestRoot, TEXT("Replay-200-Proposed"));
	TestTrue(
		TEXT("Performance manifest can replay the production session"),
		FExperimentRunner::ReplayFromManifest(FPaths::Combine(Proposed200.RunDirectory, RunManifestFile), ReplayRoot, Replay, ReplayError));
	if (!ReplayError.IsEmpty()) AddError(FString::Printf(TEXT("Performance manifest replay failed: %s"), *ReplayError));
	TestEqual(TEXT("Performance manifest replay preserves the deterministic digest"), Replay.DeterministicDigest, Proposed200.DeterministicDigest);
	TestTrue(TEXT("Performance manifest replay regenerates performance_1s.csv"), FPaths::FileExists(FPaths::Combine(ReplayRoot, PerformanceFile)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GAMacroProfileAtScaleTest,
	"AILODResearch.Phase6G.MacroProfileAtScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GAMacroProfileAtScaleTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;
	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6GACheckpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	FExperimentRunRecord Proposed2000;
	bool bFoundProposed2000 = false;

	for (const int32 TotalPopulation : { 2000, 10000, 20000 })
	{
		FString ExpectedDigest;
		switch (TotalPopulation)
		{
		case 2000:
			ExpectedDigest = TEXT("8DC871F8DE2969291D42C8CC49CB1F7E4433698E");
			break;
		case 10000:
			ExpectedDigest = TEXT("9F0DD3AC2AF2B8E3523DC30F3F2516F751BD5137");
			break;
		default:
			ExpectedDigest = TEXT("9AB01FA7115EF32D31443F8831004CE55DE22D0E");
			break;
		}

		FExperimentMatrixRequest Request;
		Request.OutputRoot = FPaths::Combine(TestRoot, FString::Printf(TEXT("Population-%d"), TotalPopulation));
		Request.ExperimentID = FString::Printf(TEXT("PHASE6GA-PROFILE-%d"), TotalPopulation);
		Request.Methods = { EUnifiedSimulationMethod::Proposed };
		Request.Scenarios = { EStage2Scenario::StateImport };
		Request.Seeds = { 20260810 };
		Request.PopulationPerKingdom = TotalPopulation / 2;
		Request.Mode = EUnifiedRunMode::Performance;
		Request.bEnableMacroProfiling = true;
		Request.GitCommit = TEXT("c629bb6");
		Request.UEVersion = TEXT("5.4");
		Request.BuildType = TEXT("Development");
		Request.Hardware = TEXT("Phase6GA-AutomationFixture");
		Request.LogMode = TEXT("EngineeringMacroProfile");
		Request.StartTime = TEXT("2026-08-17T00:00:00Z");
		Request.EndTime = TEXT("2026-08-17T00:01:00Z");

		TArray<FExperimentRunRecord> Runs;
		FString Error;
		if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
		{
			AddError(FString::Printf(TEXT("Phase 6G-A %d-person profile failed: %s"), TotalPopulation, *Error));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%d-person profile runs only Proposed"), TotalPopulation), Runs.Num(), 1);
		if (Runs.Num() != 1)
		{
			return false;
		}

		const FExperimentRunRecord& Run = Runs[0];
		const FUnifiedMacroProfile& Profile = Run.MacroProfile;
		const double ProfiledCpuMs = Profile.ResidentScanAndGroupingCpuMs
			+ Profile.RepresentativePlanningCpuMs
			+ Profile.MemberAllocationCpuMs
			+ Profile.CandidateSortCpuMs
			+ Profile.CompetitionSetupCpuMs
			+ Profile.CompetitionCheckCpuMs
			+ Profile.ActionCommitCpuMs;
		TestTrue(*FString::Printf(TEXT("%d-person profile is hard-error free"), TotalPopulation), Run.bHardErrorFree);
		TestEqual(*FString::Printf(TEXT("%d-person profile preserves the frozen digest"), TotalPopulation), Run.DeterministicDigest, ExpectedDigest);
		TestTrue(*FString::Printf(TEXT("%d-person profile records substage time"), TotalPopulation), ProfiledCpuMs > 0.0);
		TestTrue(
			*FString::Printf(TEXT("%d-person profile remains within total macro time"), TotalPopulation),
			ProfiledCpuMs <= Run.CostBreakdown.MacroCpuMs + 1.0);
		TestEqual(*FString::Printf(TEXT("%d-person profile covers every simulated hour"), TotalPopulation), Profile.ProfiledHourCount, int64(1608));
		TestEqual(
			*FString::Printf(TEXT("%d-person profile records the full resident scan count"), TotalPopulation),
			Profile.ResidentVisitCount,
			int64(1608) * TotalPopulation);
		TestTrue(*FString::Printf(TEXT("%d-person profile records cohort groups"), TotalPopulation), Profile.CohortGroupCount > 0);
		TestTrue(*FString::Printf(TEXT("%d-person profile records candidates"), TotalPopulation), Profile.CandidateCount > 0);

		TArray<FString> ActualFiles;
		IFileManager::Get().FindFiles(ActualFiles, *FPaths::Combine(Run.RunDirectory, TEXT("*")), true, false);
		ActualFiles.Sort();
		TArray<FString> ExpectedFiles = { PerformanceFile, RunManifestFile };
		ExpectedFiles.Sort();
		TestEqual(
			*FString::Printf(TEXT("%d-person profile does not add a new artifact type"), TotalPopulation),
			FString::Join(ActualFiles, TEXT("|")),
			FString::Join(ExpectedFiles, TEXT("|")));

		FString ManifestText;
		TSharedPtr<FJsonObject> Manifest;
		if (!FFileHelper::LoadFileToString(ManifestText, *FPaths::Combine(Run.RunDirectory, RunManifestFile))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest)
			|| !Manifest.IsValid())
		{
			AddError(FString::Printf(TEXT("%d-person profile manifest failed to parse."), TotalPopulation));
			return false;
		}
		const TSharedPtr<FJsonObject>* RunParameters = nullptr;
		const TSharedPtr<FJsonObject>* Measurements = nullptr;
		const TSharedPtr<FJsonObject>* LoggedProfile = nullptr;
		if (!Manifest->TryGetObjectField(TEXT("parameters"), RunParameters) || RunParameters == nullptr
			|| !Manifest->TryGetObjectField(TEXT("measurement_summary"), Measurements) || Measurements == nullptr
			|| !(*Measurements)->TryGetObjectField(TEXT("macro_profile"), LoggedProfile) || LoggedProfile == nullptr)
		{
			AddError(FString::Printf(TEXT("%d-person profile manifest is missing the macro profile."), TotalPopulation));
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%d-person manifest enables macro profiling"), TotalPopulation), (*RunParameters)->GetBoolField(TEXT("enable_macro_profiling")));
		TestEqual(
			*FString::Printf(TEXT("%d-person manifest records resident visits"), TotalPopulation),
			static_cast<int64>((*LoggedProfile)->GetNumberField(TEXT("resident_visit_count"))),
			Profile.ResidentVisitCount);
		TestEqual(
			*FString::Printf(TEXT("%d-person manifest records the same action-commit time"), TotalPopulation),
			(*LoggedProfile)->GetNumberField(TEXT("action_commit_cpu_ms")),
			Profile.ActionCommitCpuMs);

		AddInfo(FString::Printf(
			TEXT("Phase6GA population=%d macro_ms=%.3f profiled_ms=%.3f scan_group_ms=%.3f representative_ms=%.3f allocation_ms=%.3f sort_ms=%.3f competition_setup_ms=%.3f competition_check_ms=%.3f action_commit_ms=%.3f resident_visits=%lld cohorts=%lld candidates=%lld digest=%s"),
			TotalPopulation,
			Run.CostBreakdown.MacroCpuMs,
			ProfiledCpuMs,
			Profile.ResidentScanAndGroupingCpuMs,
			Profile.RepresentativePlanningCpuMs,
			Profile.MemberAllocationCpuMs,
			Profile.CandidateSortCpuMs,
			Profile.CompetitionSetupCpuMs,
			Profile.CompetitionCheckCpuMs,
			Profile.ActionCommitCpuMs,
			Profile.ResidentVisitCount,
			Profile.CohortGroupCount,
			Profile.CandidateCount,
			*Run.DeterministicDigest));

		if (TotalPopulation == 2000)
		{
			Proposed2000 = Run;
			bFoundProposed2000 = true;
		}
	}

	if (!bFoundProposed2000)
	{
		AddError(TEXT("Phase 6G-A profile did not retain the 2,000-person run for replay."));
		return false;
	}
	FExperimentRunRecord Replay;
	FString ReplayError;
	const FString ReplayRoot = FPaths::Combine(TestRoot, TEXT("Replay-2000-Proposed"));
	TestTrue(
		TEXT("Macro profile manifest can replay the production session"),
		FExperimentRunner::ReplayFromManifest(FPaths::Combine(Proposed2000.RunDirectory, RunManifestFile), ReplayRoot, Replay, ReplayError));
	if (!ReplayError.IsEmpty()) AddError(FString::Printf(TEXT("Macro profile replay failed: %s"), *ReplayError));
	TestEqual(TEXT("Macro profile replay preserves the deterministic digest"), Replay.DeterministicDigest, Proposed2000.DeterministicDigest);
	TestTrue(TEXT("Macro profile replay preserves profiling"), Replay.MacroProfile.ProfiledHourCount > 0);
	return true;
}

#endif
