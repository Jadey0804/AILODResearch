// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODExperimentRunner.h"
#include "../Simulation/AILODLogSchema.h"
#include "../Simulation/AILODOfflineMetrics.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB5ARunnerAuthorityTest,
	"AILODResearch.Phase6G.V17RunnerAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB5ARunnerAuthorityTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6GB5ACheckpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);

	FExperimentMatrixRequest Request;
	Request.OutputRoot = TestRoot;
	Request.ExperimentID = TEXT("PHASE6GB5A-ENGINEERING");
	Request.Methods = { EUnifiedSimulationMethod::Proposed };
	Request.Scenarios = { EStage2Scenario::None };
	Request.Seeds = { 20260810 };
	Request.PopulationPerKingdom = 100;
	Request.Mode = EUnifiedRunMode::Performance;
	Request.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
	Request.GitCommit = TEXT("phase-6g-b5a-local");
	Request.UEVersion = TEXT("5.4");
	Request.BuildType = TEXT("Development Editor");
	Request.Hardware = TEXT("automation-test-host");
	Request.LogMode = TEXT("B5AEngineeringSmoke");
	Request.StartTime = TEXT("2026-08-20T00:00:00Z");
	Request.EndTime = TEXT("2026-08-20T00:01:00Z");

	TArray<FExperimentRunRecord> Runs;
	FString Error;
	if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
	{
		AddError(FString::Printf(TEXT("The B5A v1.7 Runner smoke failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("B5A creates exactly one Proposed run"), Runs.Num(), 1);
	if (Runs.Num() != 1) return false;
	const FExperimentRunRecord& Run = Runs[0];
	TestTrue(TEXT("The v1.7 Runner completes with no hard error"), Run.bHardErrorFree);
	TestEqual(TEXT("The v1.7 Runner initializes all 200 stable identities"), Run.Diagnostics.V17IdentityCount, int64(200));
	TestEqual(TEXT("The hourly production path never scans the Identity Registry"),
		Run.Diagnostics.V17IdentityScanCountPerHour, int64(0));
	TestTrue(TEXT("The fixed activation trace keeps Active residents within 50"), Run.Diagnostics.MaxActiveMicro <= 50);
	TestTrue(TEXT("Batch Event objects stay below participant-weighted work"),
		Run.Diagnostics.V17BatchEventCount < Run.Diagnostics.V17ParticipantCount);

	const FString ManifestPath = FPaths::Combine(Run.RunDirectory, RunManifestFile);
	FString ManifestText;
	TSharedPtr<FJsonObject> Manifest;
	if (!FFileHelper::LoadFileToString(ManifestText, *ManifestPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest)
		|| !Manifest.IsValid())
	{
		AddError(TEXT("B5A could not read its run_manifest.json."));
		return false;
	}
	TestEqual(TEXT("The authoritative run writes Schema 1.2"),
		Manifest->GetStringField(TEXT("schema_version")), FString(TEXT("1.2")));
	TestEqual(TEXT("The authoritative run writes Spec 1.7"),
		Manifest->GetStringField(TEXT("spec_version")), FString(TEXT("1.7")));
	TestEqual(TEXT("The manifest names v1.7 as the Proposed model"),
		Manifest->GetStringField(TEXT("proposed_model_version")), FString(TEXT("1.7")));
	TestEqual(TEXT("The manifest names the sole state authority"),
		Manifest->GetStringField(TEXT("authority_mode")), FString(TEXT("v1.7_authoritative")));
	TestFalse(TEXT("B5A keeps formal-experiment validity closed until all B5 gates pass"),
		Manifest->GetBoolField(TEXT("valid_for_formal_experiment")));

	FExperimentRunRecord Replay;
	const FString ReplayRoot = FPaths::Combine(TestRoot, TEXT("Replay"));
	if (!FExperimentRunner::ReplayFromManifest(ManifestPath, ReplayRoot, Replay, Error))
	{
		AddError(FString::Printf(TEXT("The B5A manifest replay failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("Replay selects the same v1.7 authority and reproduces its Digest"),
		Replay.DeterministicDigest, Run.DeterministicDigest);
	TestEqual(TEXT("Replay keeps the no-hourly-identity-scan boundary"),
		Replay.Diagnostics.V17IdentityScanCountPerHour, int64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB5BAccuracyAndPolicyTest,
	"AILODResearch.Phase6G.V17AccuracyAndPolicies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB5BAccuracyAndPolicyTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6GB5BCheckpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	FExperimentMatrixRequest Request;
	Request.OutputRoot = TestRoot;
	Request.ExperimentID = TEXT("PHASE6GB5B-ENGINEERING");
	Request.Methods = { EUnifiedSimulationMethod::Oracle, EUnifiedSimulationMethod::Proposed };
	Request.Scenarios = {
		EStage2Scenario::None,
		EStage2Scenario::HarvestCap,
		EStage2Scenario::StateImport,
		EStage2Scenario::RepairAid };
	Request.Seeds = { 20260810 };
	Request.PopulationPerKingdom = 100;
	Request.Mode = EUnifiedRunMode::Accuracy;
	Request.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
	Request.GitCommit = TEXT("phase-6g-b5b-local");
	Request.UEVersion = TEXT("5.4");
	Request.BuildType = TEXT("Development Editor");
	Request.Hardware = TEXT("automation-test-host");
	Request.LogMode = TEXT("B5BEngineeringAccuracy");
	Request.StartTime = TEXT("2026-08-20T00:00:00Z");
	Request.EndTime = TEXT("2026-08-20T00:02:00Z");

	TArray<FExperimentRunRecord> Runs;
	FString Error;
	if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
	{
		AddError(FString::Printf(TEXT("The B5B 200-person Oracle comparison failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("B5B produces Oracle and Proposed for all four scenarios"), Runs.Num(), 8);
	for (const FExperimentRunRecord& Run : Runs)
	{
		TestTrue(*FString::Printf(TEXT("%s has no hard error"), *Run.RunID), Run.bHardErrorFree);
	}

	const FExperimentRunRecord* ProposedImport = Runs.FindByPredicate([](const FExperimentRunRecord& Run)
	{
		return Run.RunID == TEXT("Proposed-StateImport-20260810");
	});
	if (ProposedImport == nullptr)
	{
		AddError(TEXT("B5B did not produce the v1.7 StateImport run."));
		return false;
	}
	FString CohortCsv;
	TestTrue(TEXT("Schema 1.2 Cohort log loads"), FFileHelper::LoadFileToString(
		CohortCsv, *FPaths::Combine(ProposedImport->RunDirectory, CohortTimeseriesFile)));
	TestTrue(TEXT("Schema 1.2 Cohort log names Joint Cell and pending population fields"),
		CohortCsv.StartsWith(TEXT("schema_version,experiment_id,run_id,method,scenario,seed,game_time,outer_cohort_key,joint_cell_id,joint_cell_key,count,cash_sum,repair_credit_sum,wood_sum,pending_participant_count")));
	FString EventJsonl;
	TestTrue(TEXT("Schema 1.2 event log loads"), FFileHelper::LoadFileToString(
		EventJsonl, *FPaths::Combine(ProposedImport->RunDirectory, SimulationEventsFile)));
	TestTrue(TEXT("Schema 1.2 event log records Batch ownership and weighted participants"),
		EventJsonl.Contains(TEXT("\"owner_type\":\"Batch\""))
			&& EventJsonl.Contains(TEXT("\"participant_count\":"))
			&& EventJsonl.Contains(TEXT("\"batch_claim_id\":")));

	FExperimentRunRecord Replay;
	const FString ReplayRoot = FPaths::Combine(TestRoot, TEXT("Replay"));
	if (!FExperimentRunner::ReplayFromManifest(
		FPaths::Combine(ProposedImport->RunDirectory, RunManifestFile), ReplayRoot, Replay, Error))
	{
		AddError(FString::Printf(TEXT("The B5B v1.7 accuracy replay failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("B5B accuracy replay reproduces the v1.7 Digest"),
		Replay.DeterministicDigest, ProposedImport->DeterministicDigest);
	for (const FString& File : {
		FString(KingdomTimeseriesFile),
		FString(CohortTimeseriesFile),
		FString(NPCSnapshotsFile),
		FString(SimulationEventsFile),
		FString(LODTransitionsFile),
		FString(LedgerTransactionsFile) })
	{
		FString Original;
		FString Replayed;
		TestTrue(*FString::Printf(TEXT("B5B original %s loads"), *File), FFileHelper::LoadFileToString(
			Original, *FPaths::Combine(ProposedImport->RunDirectory, File)));
		TestTrue(*FString::Printf(TEXT("B5B replayed %s loads"), *File), FFileHelper::LoadFileToString(
			Replayed, *FPaths::Combine(ReplayRoot, File)));
		TestEqual(*FString::Printf(TEXT("B5B replay reproduces %s bytes"), *File), Replayed, Original);
	}

	const FString SummaryPath = FPaths::Combine(TestRoot, MetricsSummaryFile);
	if (!FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error))
	{
		AddError(FString::Printf(TEXT("The B5B offline metrics rebuild failed: %s"), *Error));
		return false;
	}
	FString Summary;
	TestTrue(TEXT("B5B metrics summary loads"), FFileHelper::LoadFileToString(Summary, *SummaryPath));
	TestTrue(TEXT("B5B reports trajectory error"), Summary.Contains(TEXT("Trajectory.DamagedWaiting")));
	TestTrue(TEXT("B5B reports policy-effect error"), Summary.Contains(TEXT("PolicyEffect.ForestWood")));
	TestTrue(TEXT("B5B reports participant-weighted behavior TVD"), Summary.Contains(TEXT("Behavior.TVD")));
	TestTrue(TEXT("B5B reports FirstAction continuity"), Summary.Contains(TEXT("Continuity.FirstActionMismatchRate")));
	TestFalse(TEXT("B5B metrics contain no NaN"), Summary.Contains(TEXT("nan"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("B5B metrics contain no infinity"), Summary.Contains(TEXT("inf"), ESearchCase::IgnoreCase));
	AddInfo(FString::Printf(
		TEXT("B5B root=%s runs=%d proposed_import_digest=%s summary_bytes=%d"),
		*TestRoot,
		Runs.Num(),
		*ProposedImport->DeterministicDigest,
		Summary.Len()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB5CScaleRegressionTest,
	"AILODResearch.Phase6G.V17ScaleRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB5CScaleRegressionTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6GB5CCheckpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	for (const int32 TotalPopulation : { 2000, 10000, 20000 })
	{
		const FString ScaleRoot = FPaths::Combine(TestRoot, FString::Printf(TEXT("Population-%d"), TotalPopulation));
		FExperimentMatrixRequest Request;
		Request.OutputRoot = ScaleRoot;
		Request.ExperimentID = FString::Printf(TEXT("PHASE6GB5C-ENGINEERING-%d"), TotalPopulation);
		Request.Methods = { EUnifiedSimulationMethod::Proposed, EUnifiedSimulationMethod::PerAgent };
		Request.Scenarios = { EStage2Scenario::StateImport };
		Request.Seeds = { 20260810 };
		Request.PopulationPerKingdom = TotalPopulation / 2;
		Request.Mode = EUnifiedRunMode::Performance;
		Request.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
		Request.GitCommit = TEXT("phase-6g-b5c-local");
		Request.UEVersion = TEXT("5.4");
		Request.BuildType = TEXT("Development Editor");
		Request.Hardware = TEXT("automation-test-host");
		Request.LogMode = TEXT("B5CEngineeringPerformance");
		Request.StartTime = TEXT("2026-08-20T00:00:00Z");
		Request.EndTime = TEXT("2026-08-20T00:03:00Z");

		TArray<FExperimentRunRecord> Runs;
		FString Error;
		if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
		{
			AddError(FString::Printf(TEXT("The B5C %d-person scale run failed: %s"), TotalPopulation, *Error));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("B5C %d runs Proposed and PerAgent"), TotalPopulation), Runs.Num(), 2);
		const FExperimentRunRecord* Proposed = Runs.FindByPredicate([](const FExperimentRunRecord& Run)
		{
			return Run.RunID.StartsWith(TEXT("Proposed-"));
		});
		const FExperimentRunRecord* PerAgent = Runs.FindByPredicate([](const FExperimentRunRecord& Run)
		{
			return Run.RunID.StartsWith(TEXT("PerAgent-"));
		});
		if (Proposed == nullptr || PerAgent == nullptr)
		{
			AddError(FString::Printf(TEXT("B5C %d-person output is missing Proposed or PerAgent."), TotalPopulation));
			return false;
		}
		const FString ExpectedDigest = TotalPopulation == 2000
			? TEXT("38995B7B27CB95192929E14F86FBE63977771520")
			: TotalPopulation == 10000
				? TEXT("00B082902227FA9EE37E9BE9BFA0605E09A7A1DD")
				: TEXT("7F317FB86A55F96AB8632A558515A56C41925E8B");
		TestEqual(*FString::Printf(TEXT("B5C %d freezes the v1.7 deterministic Digest"), TotalPopulation),
			Proposed->DeterministicDigest, ExpectedDigest);

		for (const FExperimentRunRecord* Run : { Proposed, PerAgent })
		{
			TestTrue(*FString::Printf(TEXT("%s has no hard error"), *Run->RunID), Run->bHardErrorFree);
			TestEqual(*FString::Printf(TEXT("%s uses Performance mode"), *Run->RunID),
				static_cast<int32>(Run->Mode), static_cast<int32>(EUnifiedRunMode::Performance));
			TestTrue(*FString::Printf(TEXT("%s records production cost"), *Run->RunID),
				Run->CostBreakdown.ProductionCpuMs > 0.0);
			TestTrue(*FString::Printf(TEXT("%s writes performance samples"), *Run->RunID),
				Run->PerformanceSampleCount > 0);
			TestEqual(*FString::Printf(TEXT("%s excludes validation cost"), *Run->RunID),
				Run->CostBreakdown.ValidationCpuMs, 0.0);
			TestEqual(*FString::Printf(TEXT("%s excludes snapshot cost"), *Run->RunID),
				Run->CostBreakdown.SnapshotCpuMs, 0.0);
			TestEqual(*FString::Printf(TEXT("%s excludes observer cost"), *Run->RunID),
				Run->CostBreakdown.ObserverCpuMs, 0.0);
		}

		TestEqual(*FString::Printf(TEXT("B5C %d initializes every stable identity"), TotalPopulation),
			Proposed->Diagnostics.V17IdentityCount, static_cast<int64>(TotalPopulation));
		TestEqual(*FString::Printf(TEXT("B5C %d never scans all identities during an hour"), TotalPopulation),
			Proposed->Diagnostics.V17IdentityScanCountPerHour, int64(0));
		TestTrue(*FString::Printf(TEXT("B5C %d keeps Active residents within 50"), TotalPopulation),
			Proposed->Diagnostics.MaxActiveMicro <= 50);
		TestEqual(*FString::Printf(TEXT("B5C %d performs only initial and final full audits"), TotalPopulation),
			Proposed->Diagnostics.FullAuditCount, int64(2));

		const FString ManifestPath = FPaths::Combine(Proposed->RunDirectory, RunManifestFile);
		FString ManifestText;
		TSharedPtr<FJsonObject> Manifest;
		if (!FFileHelper::LoadFileToString(ManifestText, *ManifestPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest)
			|| !Manifest.IsValid())
		{
			AddError(FString::Printf(TEXT("B5C %d could not read the Proposed manifest."), TotalPopulation));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("B5C %d writes Schema 1.2"), TotalPopulation),
			Manifest->GetStringField(TEXT("schema_version")), FString(TEXT("1.2")));
		TestEqual(*FString::Printf(TEXT("B5C %d writes Spec 1.7"), TotalPopulation),
			Manifest->GetStringField(TEXT("spec_version")), FString(TEXT("1.7")));
		TestEqual(*FString::Printf(TEXT("B5C %d records the v1.7 authority"), TotalPopulation),
			Manifest->GetStringField(TEXT("authority_mode")), FString(TEXT("v1.7_authoritative")));
		TestFalse(*FString::Printf(TEXT("B5C %d keeps formal validity closed"), TotalPopulation),
			Manifest->GetBoolField(TEXT("valid_for_formal_experiment")));
		const TSharedPtr<FJsonObject>* HardErrors = nullptr;
		const TSharedPtr<FJsonObject>* Measurements = nullptr;
		const TSharedPtr<FJsonObject>* V17Diagnostics = nullptr;
		if (!Manifest->TryGetObjectField(TEXT("hard_errors"), HardErrors) || HardErrors == nullptr)
		{
			AddError(FString::Printf(TEXT("B5C %d manifest is missing hard-error results."), TotalPopulation));
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*HardErrors)->Values)
		{
			TestEqual(*FString::Printf(TEXT("B5C %d hard error %s is zero"), TotalPopulation, *Field.Key),
				Field.Value->AsNumber(), 0.0);
		}
		if (!Manifest->TryGetObjectField(TEXT("measurement_summary"), Measurements) || Measurements == nullptr
			|| !(*Measurements)->TryGetObjectField(TEXT("v1_7_diagnostics"), V17Diagnostics) || V17Diagnostics == nullptr)
		{
			AddError(FString::Printf(TEXT("B5C %d manifest is missing v1.7 diagnostics."), TotalPopulation));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("B5C %d manifest records two full audits"), TotalPopulation),
			(*V17Diagnostics)->GetNumberField(TEXT("full_audit_count")), 2.0);

		TArray<FString> ActualFiles;
		IFileManager::Get().FindFiles(ActualFiles, *FPaths::Combine(Proposed->RunDirectory, TEXT("*")), true, false);
		ActualFiles.Sort();
		TArray<FString> ExpectedFiles = { PerformanceFile, RunManifestFile };
		ExpectedFiles.Sort();
		TestEqual(*FString::Printf(TEXT("B5C %d performance run writes only isolated artifacts"), TotalPopulation),
			FString::Join(ActualFiles, TEXT("|")), FString::Join(ExpectedFiles, TEXT("|")));
		FString PerformanceText;
		TestTrue(*FString::Printf(TEXT("B5C %d performance samples load"), TotalPopulation),
			FFileHelper::LoadFileToString(PerformanceText, *FPaths::Combine(Proposed->RunDirectory, PerformanceFile)));
		TestTrue(*FString::Printf(TEXT("B5C %d performance rows use Schema 1.2"), TotalPopulation),
			PerformanceText.Contains(TEXT("\n\"1.2\",")));

		const FString SummaryPath = FPaths::Combine(ScaleRoot, MetricsSummaryFile);
		if (!FOfflineMetricsEvaluator::BuildSummary(ScaleRoot, SummaryPath, Error))
		{
			AddError(FString::Printf(TEXT("B5C %d offline performance rebuild failed: %s"), TotalPopulation, *Error));
			return false;
		}
		FString FirstSummary;
		TestTrue(*FString::Printf(TEXT("B5C %d performance summary loads"), TotalPopulation),
			FFileHelper::LoadFileToString(FirstSummary, *SummaryPath));
		TestTrue(*FString::Printf(TEXT("B5C %d summary reports production samples"), TotalPopulation),
			FirstSummary.Contains(TEXT("Performance.AICpuMs.P95")));
		TestTrue(*FString::Printf(TEXT("B5C %d summary reports full-run production cost"), TotalPopulation),
			FirstSummary.Contains(TEXT("Performance.AICpuMs.Total")));
		TestTrue(*FString::Printf(TEXT("B5C %d summary reports the full-run PerAgent comparison"), TotalPopulation),
			FirstSummary.Contains(TEXT("Performance.SpeedupVsPerAgent.TotalAI")));
		TestTrue(*FString::Printf(TEXT("B5C %d summary can be deleted"), TotalPopulation),
			IFileManager::Get().Delete(*SummaryPath));
		TestTrue(*FString::Printf(TEXT("B5C %d summary rebuilds from raw files"), TotalPopulation),
			FOfflineMetricsEvaluator::BuildSummary(ScaleRoot, SummaryPath, Error));
		FString RebuiltSummary;
		TestTrue(*FString::Printf(TEXT("B5C %d rebuilt summary loads"), TotalPopulation),
			FFileHelper::LoadFileToString(RebuiltSummary, *SummaryPath));
		TestEqual(*FString::Printf(TEXT("B5C %d summary rebuild is byte-identical"), TotalPopulation),
			RebuiltSummary, FirstSummary);

		FExperimentRunRecord Replay;
		const FString ReplayRoot = FPaths::Combine(ScaleRoot, TEXT("Replay"));
		if (!FExperimentRunner::ReplayFromManifest(ManifestPath, ReplayRoot, Replay, Error))
		{
			AddError(FString::Printf(TEXT("B5C %d Proposed replay failed: %s"), TotalPopulation, *Error));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("B5C %d replay preserves the deterministic Digest"), TotalPopulation),
			Replay.DeterministicDigest, Proposed->DeterministicDigest);
		TestTrue(*FString::Printf(TEXT("B5C %d replay remains hard-error free"), TotalPopulation),
			Replay.bHardErrorFree);
		TestEqual(*FString::Printf(TEXT("B5C %d replay preserves resident touches"), TotalPopulation),
			Replay.Diagnostics.V17ResidentTouches, Proposed->Diagnostics.V17ResidentTouches);
		TestEqual(*FString::Printf(TEXT("B5C %d replay preserves batch event count"), TotalPopulation),
			Replay.Diagnostics.V17BatchEventCount, Proposed->Diagnostics.V17BatchEventCount);

		const double MeanProductionSpeedup = Proposed->CostBreakdown.ProductionCpuMs > 0.0
			? PerAgent->CostBreakdown.ProductionCpuMs / Proposed->CostBreakdown.ProductionCpuMs
			: 0.0;
		AddInfo(FString::Printf(
			TEXT("B5C population=%d digest=%s proposed_ms=%.3f per_agent_ms=%.3f engineering_speedup=%.3f identity_scans_per_hour=%lld resident_touches=%lld cells=%d claims=%lld events=%lld participants=%lld active=%d"),
			TotalPopulation,
			*Proposed->DeterministicDigest,
			Proposed->CostBreakdown.ProductionCpuMs,
			PerAgent->CostBreakdown.ProductionCpuMs,
			MeanProductionSpeedup,
			Proposed->Diagnostics.V17IdentityScanCountPerHour,
			Proposed->Diagnostics.V17ResidentTouches,
			Proposed->Diagnostics.V17NonEmptyJointCellCount,
			Proposed->Diagnostics.V17BatchClaimCount,
			Proposed->Diagnostics.V17BatchEventCount,
			Proposed->Diagnostics.V17ParticipantCount,
			Proposed->Diagnostics.MaxActiveMicro));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB5DStressLiteTest,
	"AILODResearch.Phase6G.V17StressLite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB5DStressLiteTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;

	constexpr int64 Baseline20KResidentTouches = 120;
	constexpr int32 Baseline20KNonEmptyCells = 9;
	constexpr int64 Baseline20KBatchClaims = 2827;
	constexpr int64 Baseline20KBatchEvents = 2905;
	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6GB5DCheckpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	FExperimentRunRecord Proposed100K;
	FString Proposed100KManifestPath;

	for (const int32 TotalPopulation : { 50000, 100000 })
	{
		const FString ScaleRoot = FPaths::Combine(TestRoot, FString::Printf(TEXT("Population-%d"), TotalPopulation));
		FExperimentMatrixRequest Request;
		Request.OutputRoot = ScaleRoot;
		Request.ExperimentID = FString::Printf(TEXT("PHASE6GB5D-LITE-ENGINEERING-%d"), TotalPopulation);
		Request.Methods = { EUnifiedSimulationMethod::Proposed };
		Request.Scenarios = { EStage2Scenario::StateImport };
		Request.Seeds = { 20260810 };
		Request.PopulationPerKingdom = TotalPopulation / 2;
		Request.Mode = EUnifiedRunMode::Performance;
		Request.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
		Request.GitCommit = TEXT("phase-6g-b5d-lite-local");
		Request.UEVersion = TEXT("5.4");
		Request.BuildType = TEXT("Development Editor");
		Request.Hardware = TEXT("automation-test-host");
		Request.LogMode = TEXT("B5DLiteEngineeringStress");
		Request.StartTime = TEXT("2026-08-20T00:00:00Z");
		Request.EndTime = TEXT("2026-08-20T00:04:00Z");

		TArray<FExperimentRunRecord> Runs;
		FString Error;
		if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
		{
			AddError(FString::Printf(TEXT("The B5D-Lite %d-person Proposed run failed: %s"), TotalPopulation, *Error));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d runs Proposed only"), TotalPopulation), Runs.Num(), 1);
		if (Runs.Num() != 1) return false;
		const FExperimentRunRecord& Run = Runs[0];
		const FString ExpectedDigest = TotalPopulation == 50000
			? TEXT("EE29C67F7A0C98A5C1138143CE09B3D064492599")
			: TEXT("C6231059510F7A6E89ABE529BDE80836CFBECC78");
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d freezes the v1.7 deterministic Digest"), TotalPopulation),
			Run.DeterministicDigest, ExpectedDigest);
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d has no hard error"), TotalPopulation), Run.bHardErrorFree);
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d initializes every identity"), TotalPopulation),
			Run.Diagnostics.V17IdentityCount, static_cast<int64>(TotalPopulation));
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d never scans all identities during an hour"), TotalPopulation),
			Run.Diagnostics.V17IdentityScanCountPerHour, int64(0));
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d keeps Active residents within 50"), TotalPopulation),
			Run.Diagnostics.MaxActiveMicro <= 50);
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d performs two full audits"), TotalPopulation),
			Run.Diagnostics.FullAuditCount, int64(2));
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d records initialization cost"), TotalPopulation),
			Run.CostBreakdown.InitializeCpuMs > 0.0);
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d records production cost"), TotalPopulation),
			Run.CostBreakdown.ProductionCpuMs > 0.0);
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d excludes validation cost"), TotalPopulation),
			Run.CostBreakdown.ValidationCpuMs, 0.0);
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d excludes snapshot cost"), TotalPopulation),
			Run.CostBreakdown.SnapshotCpuMs, 0.0);
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d excludes observer cost"), TotalPopulation),
			Run.CostBreakdown.ObserverCpuMs, 0.0);
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d writes performance samples"), TotalPopulation),
			Run.PerformanceSampleCount > 0);

		TestTrue(*FString::Printf(TEXT("B5D-Lite %d resident touches stay below twice the 20k baseline"), TotalPopulation),
			Run.Diagnostics.V17ResidentTouches <= Baseline20KResidentTouches * 2);
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d non-empty cells stay below twice the 20k baseline"), TotalPopulation),
			Run.Diagnostics.V17NonEmptyJointCellCount <= Baseline20KNonEmptyCells * 2);
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d batch claims stay below twice the 20k baseline"), TotalPopulation),
			Run.Diagnostics.V17BatchClaimCount <= Baseline20KBatchClaims * 2);
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d batch events stay below twice the 20k baseline"), TotalPopulation),
			Run.Diagnostics.V17BatchEventCount <= Baseline20KBatchEvents * 2);

		const FString ManifestPath = FPaths::Combine(Run.RunDirectory, RunManifestFile);
		FString ManifestText;
		TSharedPtr<FJsonObject> Manifest;
		if (!FFileHelper::LoadFileToString(ManifestText, *ManifestPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest)
			|| !Manifest.IsValid())
		{
			AddError(FString::Printf(TEXT("B5D-Lite %d could not read its run manifest."), TotalPopulation));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d writes Schema 1.2"), TotalPopulation),
			Manifest->GetStringField(TEXT("schema_version")), FString(TEXT("1.2")));
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d writes Spec 1.7"), TotalPopulation),
			Manifest->GetStringField(TEXT("spec_version")), FString(TEXT("1.7")));
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d records the v1.7 authority"), TotalPopulation),
			Manifest->GetStringField(TEXT("authority_mode")), FString(TEXT("v1.7_authoritative")));
		TestFalse(*FString::Printf(TEXT("B5D-Lite %d keeps formal validity closed"), TotalPopulation),
			Manifest->GetBoolField(TEXT("valid_for_formal_experiment")));
		const TSharedPtr<FJsonObject>* HardErrors = nullptr;
		if (!Manifest->TryGetObjectField(TEXT("hard_errors"), HardErrors) || HardErrors == nullptr)
		{
			AddError(FString::Printf(TEXT("B5D-Lite %d manifest is missing hard-error results."), TotalPopulation));
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*HardErrors)->Values)
		{
			TestEqual(*FString::Printf(TEXT("B5D-Lite %d hard error %s is zero"), TotalPopulation, *Field.Key),
				Field.Value->AsNumber(), 0.0);
		}

		TArray<FString> ActualFiles;
		IFileManager::Get().FindFiles(ActualFiles, *FPaths::Combine(Run.RunDirectory, TEXT("*")), true, false);
		ActualFiles.Sort();
		TArray<FString> ExpectedFiles = { PerformanceFile, RunManifestFile };
		ExpectedFiles.Sort();
		TestEqual(*FString::Printf(TEXT("B5D-Lite %d writes only isolated performance artifacts"), TotalPopulation),
			FString::Join(ActualFiles, TEXT("|")), FString::Join(ExpectedFiles, TEXT("|")));
		FString PerformanceText;
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d performance samples load"), TotalPopulation),
			FFileHelper::LoadFileToString(PerformanceText, *FPaths::Combine(Run.RunDirectory, PerformanceFile)));
		TestTrue(*FString::Printf(TEXT("B5D-Lite %d performance rows use Schema 1.2"), TotalPopulation),
			PerformanceText.Contains(TEXT("\n\"1.2\",")));

		AddInfo(FString::Printf(
			TEXT("B5D-Lite population=%d digest=%s initialize_ms=%.3f production_ms=%.3f audit_ms=%.3f identity_scans_per_hour=%lld resident_touches=%lld cells=%d claims=%lld events=%lld participants=%lld active=%d"),
			TotalPopulation,
			*Run.DeterministicDigest,
			Run.CostBreakdown.InitializeCpuMs,
			Run.CostBreakdown.ProductionCpuMs,
			Run.CostBreakdown.AuditCpuMs,
			Run.Diagnostics.V17IdentityScanCountPerHour,
			Run.Diagnostics.V17ResidentTouches,
			Run.Diagnostics.V17NonEmptyJointCellCount,
			Run.Diagnostics.V17BatchClaimCount,
			Run.Diagnostics.V17BatchEventCount,
			Run.Diagnostics.V17ParticipantCount,
			Run.Diagnostics.MaxActiveMicro));

		if (TotalPopulation == 100000)
		{
			Proposed100K = Run;
			Proposed100KManifestPath = ManifestPath;
		}
	}

	if (Proposed100KManifestPath.IsEmpty())
	{
		AddError(TEXT("B5D-Lite did not retain the 100k Proposed run for replay."));
		return false;
	}
	FExperimentRunRecord Replay;
	FString ReplayError;
	const FString ReplayRoot = FPaths::Combine(TestRoot, TEXT("Replay-100000-Proposed"));
	if (!FExperimentRunner::ReplayFromManifest(Proposed100KManifestPath, ReplayRoot, Replay, ReplayError))
	{
		AddError(FString::Printf(TEXT("B5D-Lite 100k replay failed: %s"), *ReplayError));
		return false;
	}
	TestEqual(TEXT("B5D-Lite 100k replay preserves the deterministic Digest"),
		Replay.DeterministicDigest, Proposed100K.DeterministicDigest);
	TestTrue(TEXT("B5D-Lite 100k replay remains hard-error free"), Replay.bHardErrorFree);
	TestEqual(TEXT("B5D-Lite 100k replay keeps hourly identity scans at zero"),
		Replay.Diagnostics.V17IdentityScanCountPerHour, int64(0));
	TestEqual(TEXT("B5D-Lite 100k replay preserves resident touches"),
		Replay.Diagnostics.V17ResidentTouches, Proposed100K.Diagnostics.V17ResidentTouches);
	TestEqual(TEXT("B5D-Lite 100k replay preserves batch claims"),
		Replay.Diagnostics.V17BatchClaimCount, Proposed100K.Diagnostics.V17BatchClaimCount);
	TestEqual(TEXT("B5D-Lite 100k replay preserves batch events"),
		Replay.Diagnostics.V17BatchEventCount, Proposed100K.Diagnostics.V17BatchEventCount);
	return true;
}

#endif
