// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODExperimentLogging.h"
#include "../Simulation/AILODLogSchema.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool LoadManifest(const FString& Directory, TSharedPtr<FJsonObject>& OutManifest)
	{
		FString Text;
		return FFileHelper::LoadFileToString(
			Text,
			*FPaths::Combine(Directory, AILOD::LogSchema::RunManifestFile))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), OutManifest)
			&& OutManifest.IsValid();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6HH1FormalProvenanceTest,
	"AILODResearch.Phase6H.FormalProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6HH1FormalProvenanceTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6HH1Checkpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);

	FUnifiedRunResult Result;
	Result.Method = EUnifiedSimulationMethod::Proposed;
	Result.Scenario = EStage2Scenario::None;
	Result.Seed = 20260810;
	Result.PopulationPerKingdom = 100;
	Result.Mode = EUnifiedRunMode::Performance;
	Result.bRetainCompletedEvents = false;
	Result.bRecordSnapshots = false;
	Result.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
	Result.ModelSpecVersion = TEXT("1.7");
	Result.LogSchemaVersion = TEXT("1.2");
	Result.AuthorityMode = TEXT("v1.7_authoritative");
	Result.JointStateVersion = TEXT("1.7");
	Result.ClaimAllocationVersion = TEXT("1.7");
	Result.CapsuleVersion = TEXT("1");
	Result.DeterministicDigestVersion = TEXT("1.7-domain-v2");
	Result.bFormalModelEligible = false;
	Result.ConfigHash = TEXT("H1-COMPONENT-CONFIG");
	Result.FinalTime = FSimulationTime::FromDays(60);
	Result.V17DeterministicDigest = TEXT("H1-DOMAIN-DIGEST-UNCHANGED");
	FUnifiedPerformanceSample& Sample = Result.PerformanceSamples.AddDefaulted_GetRef();
	Sample.GameTime = Result.FinalTime;
	Sample.AICpuMs = 1.0;

	auto MakeMetadata = [&TestRoot](const TCHAR* DirectoryName)
	{
		FUnifiedRunLogMetadata Metadata;
		Metadata.OutputDirectory = FPaths::Combine(TestRoot, DirectoryName);
		Metadata.ExperimentID = TEXT("PHASE6H-H1-COMPONENT");
		Metadata.RunID = DirectoryName;
		Metadata.PopulationManifestSHA256 = FString::ChrN(64, TEXT('A'));
		Metadata.DamageListSHA256 = FString::ChrN(64, TEXT('B'));
		Metadata.PersistentPoolSHA256 = FString::ChrN(64, TEXT('C'));
		Metadata.GitCommit = TEXT("phase-6h-local");
		Metadata.UEVersion = TEXT("5.4");
		Metadata.BuildType = TEXT("Development Editor");
		Metadata.Hardware = TEXT("automation-test-host");
		Metadata.LogMode = TEXT("H1Component");
		Metadata.StartTime = TEXT("2026-08-20T00:00:00Z");
		Metadata.EndTime = TEXT("2026-08-20T00:00:01Z");
		return Metadata;
	};

	FString Error;
	FUnifiedRunLogWriter EngineeringWriter;
	FUnifiedRunLogMetadata Engineering = MakeMetadata(TEXT("Engineering"));
	TestTrue(TEXT("Engineering manifest writes"), EngineeringWriter.WriteRun(Result, Engineering, Error));
	if (!Error.IsEmpty()) AddError(Error);

	FUnifiedRunLogWriter RequestedWriter;
	FUnifiedRunLogMetadata Requested = MakeMetadata(TEXT("RequestedBeforeApproval"));
	Requested.bFormalRunRequested = true;
	Requested.bFormalEnvironmentEligible = true;
	TestTrue(TEXT("Requested pre-approval manifest writes"), RequestedWriter.WriteRun(Result, Requested, Error));
	if (!Error.IsEmpty()) AddError(Error);

	TSharedPtr<FJsonObject> EngineeringManifest;
	TSharedPtr<FJsonObject> RequestedManifest;
	if (!LoadManifest(Engineering.OutputDirectory, EngineeringManifest)
		|| !LoadManifest(Requested.OutputDirectory, RequestedManifest))
	{
		AddError(TEXT("H1 could not reload both manifests."));
		return false;
	}
	TestEqual(TEXT("Formal request does not change the domain digest"),
		RequestedManifest->GetStringField(TEXT("deterministic_digest")),
		EngineeringManifest->GetStringField(TEXT("deterministic_digest")));
	TestEqual(TEXT("Manifest names the domain digest contract"),
		RequestedManifest->GetStringField(TEXT("deterministic_digest_version")),
		FString(TEXT("1.7-domain-v2")));
	TestFalse(TEXT("Engineering run is not requested as formal"),
		EngineeringManifest->GetBoolField(TEXT("formal_run_requested")));
	TestTrue(TEXT("The second run records the explicit formal request"),
		RequestedManifest->GetBoolField(TEXT("formal_run_requested")));
	TestFalse(TEXT("The v1.7 model remains closed until H5"),
		RequestedManifest->GetBoolField(TEXT("formal_model_eligible")));
	TestFalse(TEXT("A request alone cannot make a run formally valid"),
		RequestedManifest->GetBoolField(TEXT("valid_for_formal_experiment")));
	TestEqual(TEXT("The manifest explains why the requested run is still excluded"),
		RequestedManifest->GetStringField(TEXT("formal_eligibility_reason")),
		FString(TEXT("model_version_not_yet_formally_eligible")));

	Result.bFormalModelEligible = true;
	FUnifiedRunLogWriter EligibleWriter;
	FUnifiedRunLogMetadata Eligible = MakeMetadata(TEXT("Eligible"));
	Eligible.bFormalRunRequested = true;
	Eligible.bFormalEnvironmentEligible = true;
	TestTrue(TEXT("Eligible manifest writes"), EligibleWriter.WriteRun(Result, Eligible, Error));
	TSharedPtr<FJsonObject> EligibleManifest;
	if (!LoadManifest(Eligible.OutputDirectory, EligibleManifest))
	{
		AddError(TEXT("H1 could not reload the eligible manifest."));
		return false;
	}
	TestTrue(TEXT("All three formal gates produce a formally valid manifest"),
		EligibleManifest->GetBoolField(TEXT("valid_for_formal_experiment")));
	TestEqual(TEXT("Eligible manifest records a plain reason"),
		EligibleManifest->GetStringField(TEXT("formal_eligibility_reason")),
		FString(TEXT("eligible")));
	TestEqual(TEXT("Administrative eligibility still does not change the domain digest"),
		EligibleManifest->GetStringField(TEXT("deterministic_digest")),
		EngineeringManifest->GetStringField(TEXT("deterministic_digest")));
	return true;
}

#endif
