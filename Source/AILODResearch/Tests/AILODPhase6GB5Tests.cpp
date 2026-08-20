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

#endif
