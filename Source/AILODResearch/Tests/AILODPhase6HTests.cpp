// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODExperimentLogging.h"
#include "../Simulation/AILODExperimentRunner.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6HH3ScheduleAndResumeTest,
	"AILODResearch.Phase6H.ScheduleAndResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6HH3ScheduleAndResumeTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;

	FExperimentMatrixRequest ScheduleRequest;
	ScheduleRequest.Methods = { EUnifiedSimulationMethod::Oracle, EUnifiedSimulationMethod::Proposed };
	ScheduleRequest.Scenarios = { EStage2Scenario::None, EStage2Scenario::StateImport };
	ScheduleRequest.Seeds = { 20260810, 20260811 };
	ScheduleRequest.RepeatCount = 2;
	ScheduleRequest.bRandomizeRunOrder = true;
	ScheduleRequest.OrderSeed = 6108;
	TArray<FExperimentRunPlanEntry> ScheduleA;
	TArray<FExperimentRunPlanEntry> ScheduleB;
	FString Error;
	TestTrue(TEXT("H3 builds the first deterministic randomized schedule"),
		FExperimentRunner::BuildSchedule(ScheduleRequest, ScheduleA, Error));
	TestTrue(TEXT("H3 rebuilds the same deterministic randomized schedule"),
		FExperimentRunner::BuildSchedule(ScheduleRequest, ScheduleB, Error));
	TestEqual(TEXT("H3 schedule contains methods x scenarios x seeds x repeats"), ScheduleA.Num(), 16);
	TSet<FString> UniqueRunIDs;
	FString OrderA;
	FString OrderB;
	for (int32 Index = 0; Index < ScheduleA.Num(); ++Index)
	{
		OrderA += ScheduleA[Index].RunID + TEXT("|");
		OrderB += ScheduleB[Index].RunID + TEXT("|");
		UniqueRunIDs.Add(ScheduleA[Index].RunID);
		TestEqual(*FString::Printf(TEXT("H3 schedule index %d is explicit"), Index + 1),
			ScheduleA[Index].ScheduleIndex, Index + 1);
		TestTrue(*FString::Printf(TEXT("H3 repeated RunID %d names R01 or R02"), Index + 1),
			ScheduleA[Index].RunID.Contains(TEXT("-R01")) || ScheduleA[Index].RunID.Contains(TEXT("-R02")));
	}
	TestEqual(TEXT("H3 same order seed produces byte-identical order"), OrderB, OrderA);
	TestEqual(TEXT("H3 every repeated run has a unique RunID"), UniqueRunIDs.Num(), ScheduleA.Num());
	ScheduleRequest.OrderSeed = 6109;
	TArray<FExperimentRunPlanEntry> DifferentSchedule;
	TestTrue(TEXT("H3 builds a schedule for a different order seed"),
		FExperimentRunner::BuildSchedule(ScheduleRequest, DifferentSchedule, Error));
	FString DifferentOrder;
	for (const FExperimentRunPlanEntry& Entry : DifferentSchedule) DifferentOrder += Entry.RunID + TEXT("|");
	TestTrue(TEXT("H3 a different order seed changes this frozen matrix order"), DifferentOrder != OrderA);

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6HH3Checkpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	FExperimentMatrixRequest RunRequest;
	RunRequest.OutputRoot = TestRoot;
	RunRequest.ExperimentID = TEXT("PHASE6H-H3-ENGINEERING");
	RunRequest.Methods = { EUnifiedSimulationMethod::Oracle };
	RunRequest.Scenarios = { EStage2Scenario::None };
	RunRequest.Seeds = { 20260810 };
	RunRequest.PopulationPerKingdom = 100;
	RunRequest.Mode = EUnifiedRunMode::Performance;
	RunRequest.GitCommit = TEXT("52795a0");
	RunRequest.UEVersion = TEXT("5.4");
	RunRequest.BuildType = TEXT("Development Editor");
	RunRequest.Hardware = TEXT("automation-test-host");
	RunRequest.LogMode = TEXT("H3EngineeringPerformance");
	RunRequest.RepeatCount = 2;
	RunRequest.bRandomizeRunOrder = true;
	RunRequest.OrderSeed = 6108;
	TArray<FExperimentRunRecord> FirstRuns;
	if (!FExperimentRunner::RunMatrix(RunRequest, FirstRuns, Error))
	{
		AddError(FString::Printf(TEXT("H3 first matrix failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("H3 first matrix executes both repeats"), FirstRuns.Num(), 2);
	TestTrue(TEXT("H3 writes run_schedule.csv"), FPaths::FileExists(FPaths::Combine(TestRoot, RunScheduleFile)));
	for (const FExperimentRunRecord& Run : FirstRuns)
	{
		TestFalse(*FString::Printf(TEXT("H3 first %s is executed"), *Run.RunID), Run.bSkippedExisting);
		TSharedPtr<FJsonObject> Manifest;
		if (!LoadManifest(Run.RunDirectory, Manifest))
		{
			AddError(FString::Printf(TEXT("H3 could not load %s manifest."), *Run.RunID));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("H3 %s records protocol 1.0"), *Run.RunID),
			Manifest->GetStringField(TEXT("experiment_protocol_version")), FString(TEXT("1.0")));
		TestEqual(*FString::Printf(TEXT("H3 %s records its schedule index"), *Run.RunID),
			static_cast<int32>(Manifest->GetNumberField(TEXT("schedule_index"))), Run.ScheduleIndex);
		TestEqual(*FString::Printf(TEXT("H3 %s records its repeat index"), *Run.RunID),
			static_cast<int32>(Manifest->GetNumberField(TEXT("repeat_index"))), Run.RepeatIndex);
		TestEqual(*FString::Printf(TEXT("H3 %s records the order seed"), *Run.RunID),
			static_cast<int32>(Manifest->GetNumberField(TEXT("order_seed"))), RunRequest.OrderSeed);
		TestTrue(*FString::Printf(TEXT("H3 %s records randomized order"), *Run.RunID),
			Manifest->GetBoolField(TEXT("run_order_randomized")));
		TestFalse(*FString::Printf(TEXT("H3 %s auto-fills start time"), *Run.RunID),
			Manifest->GetStringField(TEXT("start_time")).IsEmpty());
		TestFalse(*FString::Printf(TEXT("H3 %s auto-fills end time"), *Run.RunID),
			Manifest->GetStringField(TEXT("end_time")).IsEmpty());
	}

	RunRequest.bResumeCompletedRuns = true;
	TArray<FExperimentRunRecord> ResumedRuns;
	if (!FExperimentRunner::RunMatrix(RunRequest, ResumedRuns, Error))
	{
		AddError(FString::Printf(TEXT("H3 resume failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("H3 resume returns both planned records"), ResumedRuns.Num(), 2);
	for (const FExperimentRunRecord& Run : ResumedRuns)
	{
		TestTrue(*FString::Printf(TEXT("H3 resume safely skips %s"), *Run.RunID), Run.bSkippedExisting);
	}

	RunRequest.GitCommit = TEXT("deadbee");
	TArray<FExperimentRunRecord> MismatchedRuns;
	TestFalse(TEXT("H3 resume refuses a completed run from different provenance"),
		FExperimentRunner::RunMatrix(RunRequest, MismatchedRuns, Error));
	TestTrue(TEXT("H3 resume mismatch explains the refusal"), Error.Contains(TEXT("mismatched")));
	TestTrue(TEXT("H3 records the failed resume attempt"), FPaths::FileExists(FPaths::Combine(TestRoot, RunFailuresFile)));

	FExperimentMatrixRequest InvalidFormal = RunRequest;
	InvalidFormal.OutputRoot = FPaths::Combine(TestRoot, TEXT("InvalidFormal"));
	InvalidFormal.bResumeCompletedRuns = false;
	InvalidFormal.bFormalRunRequested = true;
	InvalidFormal.bFormalEnvironmentEligible = false;
	TArray<FExperimentRunRecord> FormalRuns;
	TestFalse(TEXT("H3 Development environment cannot be mislabeled as formal"),
		FExperimentRunner::RunMatrix(InvalidFormal, FormalRuns, Error));
	TestTrue(TEXT("H3 formal rejection explains the required environment"), Error.Contains(TEXT("Shipping")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6HH4TrackedAuthorityMemoryTest,
	"AILODResearch.Phase6H.TrackedAuthorityMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6HH4TrackedAuthorityMemoryTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase6HH4Checkpoint"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	FExperimentMatrixRequest Request;
	Request.OutputRoot = TestRoot;
	Request.ExperimentID = TEXT("PHASE6H-H4-ENGINEERING");
	Request.Methods = { EUnifiedSimulationMethod::Proposed };
	Request.Scenarios = { EStage2Scenario::None };
	Request.Seeds = { 20260810 };
	Request.PopulationPerKingdom = 1000;
	Request.Mode = EUnifiedRunMode::Performance;
	Request.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
	Request.GitCommit = TEXT("ab59954");
	Request.UEVersion = TEXT("5.4");
	Request.BuildType = TEXT("Development Editor");
	Request.Hardware = TEXT("automation-test-host");
	Request.LogMode = TEXT("H4EngineeringMemory");
	Request.bRandomizeRunOrder = true;
	Request.OrderSeed = 6108;

	TArray<FExperimentRunRecord> Runs;
	FString Error;
	if (!FExperimentRunner::RunMatrix(Request, Runs, Error) || Runs.Num() != 1)
	{
		AddError(FString::Printf(TEXT("H4 memory run failed: %s"), *Error));
		return false;
	}
	const FExperimentRunRecord& Run = Runs[0];
	const FV17TrackedAuthorityMemory& Memory = Run.V17TrackedMemory;
	TestTrue(TEXT("H4 tracks the static identity registry"), Memory.IdentityRegistryBytes > 0);
	TestTrue(TEXT("H4 tracks joint state and its indexes"), Memory.JointStateBytes > 0);
	TestTrue(TEXT("H4 tracks continuity capsules"), Memory.CapsuleBytes > 0);
	TestTrue(TEXT("H4 tracks batch claims"), Memory.BatchClaimBytes > 0);
	TestTrue(TEXT("H4 tracks batch events"), Memory.BatchEventBytes > 0);
	TestTrue(TEXT("H4 tracks the aggregate ledger"), Memory.LedgerBytes > 0);
	TestEqual(TEXT("H4 total is exactly the sum of the named components"), Memory.TotalBytes, Memory.SumComponents());

	TSharedPtr<FJsonObject> Manifest;
	if (!LoadManifest(Run.RunDirectory, Manifest))
	{
		AddError(TEXT("H4 could not read the Proposed manifest."));
		return false;
	}
	const TSharedPtr<FJsonObject>* Measurements = nullptr;
	const TSharedPtr<FJsonObject>* TrackedMemory = nullptr;
	if (!Manifest->TryGetObjectField(TEXT("measurement_summary"), Measurements) || Measurements == nullptr
		|| !(*Measurements)->TryGetObjectField(TEXT("tracked_authority_memory_bytes"), TrackedMemory)
		|| TrackedMemory == nullptr)
	{
		AddError(TEXT("H4 manifest is missing the tracked authority memory breakdown."));
		return false;
	}
	TestEqual(TEXT("H4 manifest preserves the tracked total"),
		static_cast<uint64>((*TrackedMemory)->GetNumberField(TEXT("total_tracked_authority_bytes"))),
		Memory.TotalBytes);
	TestTrue(TEXT("H4 manifest clearly excludes whole-process memory"),
		(*TrackedMemory)->GetStringField(TEXT("scope")).Contains(TEXT("excludes_process")));
	AddInfo(FString::Printf(
		TEXT("H4 total_population=2000 tracked_total=%llu identity=%llu joint=%llu active=%llu capsule=%llu participant_ref=%llu claims=%llu batch_events=%llu system_events=%llu ledger=%llu reservations=%llu event_store=%llu scheduler=%llu lod_transitions=%llu fixed=%llu"),
		Memory.TotalBytes,
		Memory.IdentityRegistryBytes,
		Memory.JointStateBytes,
		Memory.ActiveStateBytes,
		Memory.CapsuleBytes,
		Memory.ParticipantRefBytes,
		Memory.BatchClaimBytes,
		Memory.BatchEventBytes,
		Memory.SystemEventBytes,
		Memory.LedgerBytes,
		Memory.ReservationBytes,
		Memory.EventStoreBytes,
		Memory.SchedulerBytes,
		Memory.LODTransitionBytes,
		Memory.AuthorityFixedBytes));

	Request.bResumeCompletedRuns = true;
	TArray<FExperimentRunRecord> ResumedRuns;
	if (!FExperimentRunner::RunMatrix(Request, ResumedRuns, Error) || ResumedRuns.Num() != 1)
	{
		AddError(FString::Printf(TEXT("H4 memory resume failed: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("H4 resume skips the complete run"), ResumedRuns[0].bSkippedExisting);
	TestEqual(TEXT("H4 resume reloads the tracked memory total"),
		ResumedRuns[0].V17TrackedMemory.TotalBytes, Memory.TotalBytes);
	return true;
}

#endif
