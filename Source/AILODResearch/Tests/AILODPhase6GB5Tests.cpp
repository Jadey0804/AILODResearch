// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODExperimentRunner.h"
#include "../Simulation/AILODLogSchema.h"
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

#endif
