// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODExperimentCommandlet.h"
#include "../Simulation/AILODExperimentRunner.h"
#include "../Simulation/AILODLogSchema.h"
#include "../Simulation/AILODOfflineMetrics.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase8AFormalPipelineTest,
	"AILODResearch.Phase8A.FormalPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase8AFormalPipelineTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;

	TestFalse(
		TEXT("Phase 8 commandlet can run from a non-editor Shipping game executable"),
		GetDefault<UAILODExperimentCommandlet>()->IsEditor);

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase8AFormalPipeline"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);

	FExperimentMatrixRequest Request;
	Request.OutputRoot = TestRoot;
	Request.ExperimentID = TEXT("PHASE8A-ENGINEERING-PAIRING-TEST");
	Request.Methods = {
		EUnifiedSimulationMethod::Proposed,
		EUnifiedSimulationMethod::Simple,
		EUnifiedSimulationMethod::PerAgent };
	Request.Scenarios = { EStage2Scenario::StateImport };
	Request.Seeds = { 20260810 };
	Request.PopulationPerKingdom = 100;
	Request.Mode = EUnifiedRunMode::Performance;
	Request.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
	Request.GitCommit = TEXT("3314034");
	Request.UEVersion = TEXT("5.4");
	Request.BuildType = TEXT("Development Editor");
	Request.Hardware = TEXT("automation-test-host");
	Request.LogMode = TEXT("Phase8AEngineeringPerformance");
	Request.RepeatCount = 2;
	Request.bRandomizeRunOrder = true;
	Request.OrderSeed = 830802;

	TArray<FExperimentRunRecord> Runs;
	FString Error;
	if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
	{
		AddError(FString::Printf(TEXT("Phase 8A pairing matrix failed: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("Phase 8A pairing matrix executes three methods twice"), Runs.Num(), 6);

	const FString SummaryPath = FPaths::Combine(TestRoot, MetricsSummaryFile);
	if (!FOfflineMetricsEvaluator::BuildSummary(TestRoot, SummaryPath, Error))
	{
		AddError(FString::Printf(TEXT("Phase 8A summary rebuild failed: %s"), *Error));
		return false;
	}

	FString Summary;
	if (!FFileHelper::LoadFileToString(Summary, *SummaryPath))
	{
		AddError(TEXT("Phase 8A could not read its rebuilt summary."));
		return false;
	}
	TArray<FString> Lines;
	Summary.ParseIntoArrayLines(Lines, true);
	for (int32 RepeatIndex = 1; RepeatIndex <= 2; ++RepeatIndex)
	{
		const FString Repeat = FString::Printf(TEXT("R%02d"), RepeatIndex);
		for (const TCHAR* Method : { TEXT("Proposed"), TEXT("Simple"), TEXT("PerAgent") })
		{
			const FString RunID = FString::Printf(
				TEXT("%s-StateImport-20260810-%s"), Method, *Repeat);
			const FString Baseline = FString::Printf(
				TEXT("baseline=PerAgent-StateImport-20260810-%s;full_67_game_day_production_cpu_ms"),
				*Repeat);
			bool bFoundCorrectPair = false;
			for (const FString& Line : Lines)
			{
				if (Line.Contains(RunID)
					&& Line.Contains(TEXT("Performance.SpeedupVsPerAgent.TotalAI"))
					&& Line.Contains(Baseline))
				{
					bFoundCorrectPair = true;
					break;
				}
			}
			TestTrue(
				*FString::Printf(TEXT("%s uses the matching PerAgent repeat"), *RunID),
				bFoundCorrectPair);
		}
	}
	return true;
}

#endif
