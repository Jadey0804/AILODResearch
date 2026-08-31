// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODExperimentCommandlet.h"

#include "AILODExperimentRunner.h"
#include "AILODLogSchema.h"
#include "AILODOfflineMetrics.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/EngineVersion.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAILODExperimentCommandlet, Log, All);

namespace
{
	bool SplitList(const FString& Text, TArray<FString>& OutValues)
	{
		FString Normalized = Text.Replace(TEXT(","), TEXT("+"));
		Normalized.ParseIntoArray(OutValues, TEXT("+"), true);
		for (FString& Value : OutValues) Value.TrimStartAndEndInline();
		return !OutValues.IsEmpty();
	}

	bool ParseMethods(const FString& Text, TArray<AILOD::EUnifiedSimulationMethod>& OutMethods)
	{
		TArray<FString> Values;
		if (!SplitList(Text, Values)) return false;
		for (const FString& Value : Values)
		{
			if (Value.Equals(TEXT("Oracle"), ESearchCase::IgnoreCase)) OutMethods.Add(AILOD::EUnifiedSimulationMethod::Oracle);
			else if (Value.Equals(TEXT("Proposed"), ESearchCase::IgnoreCase)) OutMethods.Add(AILOD::EUnifiedSimulationMethod::Proposed);
			else if (Value.Equals(TEXT("PerAgent"), ESearchCase::IgnoreCase)) OutMethods.Add(AILOD::EUnifiedSimulationMethod::PerAgent);
			else if (Value.Equals(TEXT("Simple"), ESearchCase::IgnoreCase)) OutMethods.Add(AILOD::EUnifiedSimulationMethod::Simple);
			else return false;
		}
		return true;
	}

	bool ParseScenarios(const FString& Text, TArray<AILOD::EStage2Scenario>& OutScenarios)
	{
		TArray<FString> Values;
		if (!SplitList(Text, Values)) return false;
		for (const FString& Value : Values)
		{
			if (Value.Equals(TEXT("None"), ESearchCase::IgnoreCase)) OutScenarios.Add(AILOD::EStage2Scenario::None);
			else if (Value.Equals(TEXT("HarvestCap"), ESearchCase::IgnoreCase)) OutScenarios.Add(AILOD::EStage2Scenario::HarvestCap);
			else if (Value.Equals(TEXT("StateImport"), ESearchCase::IgnoreCase)) OutScenarios.Add(AILOD::EStage2Scenario::StateImport);
			else if (Value.Equals(TEXT("RepairAid"), ESearchCase::IgnoreCase)) OutScenarios.Add(AILOD::EStage2Scenario::RepairAid);
			else return false;
		}
		return true;
	}

	bool ParseSeeds(const FString& Text, TArray<int32>& OutSeeds)
	{
		TArray<FString> Values;
		if (!SplitList(Text, Values)) return false;
		for (const FString& Value : Values)
		{
			if (!Value.IsNumeric()) return false;
			OutSeeds.Add(FCString::Atoi(*Value));
		}
		return true;
	}

	bool ParseMode(const FString& Text, AILOD::EUnifiedRunMode& OutMode)
	{
		if (Text.Equals(TEXT("Validation"), ESearchCase::IgnoreCase)) OutMode = AILOD::EUnifiedRunMode::Validation;
		else if (Text.Equals(TEXT("Accuracy"), ESearchCase::IgnoreCase)) OutMode = AILOD::EUnifiedRunMode::Accuracy;
		else if (Text.Equals(TEXT("Performance"), ESearchCase::IgnoreCase)) OutMode = AILOD::EUnifiedRunMode::Performance;
		else return false;
		return true;
	}

	bool IsGitCommit(const FString& Value)
	{
		if (Value.Len() < 7 || Value.Len() > 40) return false;
		for (const TCHAR Character : Value) if (!FChar::IsHexDigit(Character)) return false;
		return true;
	}

	FString BuildTypeName()
	{
#if UE_BUILD_SHIPPING
		return TEXT("Shipping");
#elif UE_BUILD_TEST
		return TEXT("Test");
#elif UE_BUILD_DEBUG
		return TEXT("Debug");
#else
		return TEXT("Development");
#endif
	}

	FString HardwareName()
	{
		const FPlatformMemoryConstants Memory = FPlatformMemory::GetConstants();
		return FString::Printf(
			TEXT("%s|logical_cores=%d|physical_memory_mb=%llu"),
			*FPlatformMisc::GetCPUBrand(),
			FPlatformMisc::NumberOfCoresIncludingHyperthreads(),
			Memory.TotalPhysical / (1024ull * 1024ull));
	}
}

UAILODExperimentCommandlet::UAILODExperimentCommandlet()
{
	IsClient = false;
	IsEditor = false;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UAILODExperimentCommandlet::Main(const FString& Params)
{
	using namespace AILOD;

	FExperimentMatrixRequest Request;
	FString MethodsText;
	FString ScenariosText;
	FString SeedsText;
	FString ModeText = TEXT("Accuracy");
	if (!FParse::Value(*Params, TEXT("OutputRoot="), Request.OutputRoot)
		|| !FParse::Value(*Params, TEXT("ExperimentID="), Request.ExperimentID)
		|| !FParse::Value(*Params, TEXT("Methods="), MethodsText)
		|| !FParse::Value(*Params, TEXT("Scenarios="), ScenariosText)
		|| !FParse::Value(*Params, TEXT("Seeds="), SeedsText)
		|| !FParse::Value(*Params, TEXT("GitCommit="), Request.GitCommit)
		|| !ParseMethods(MethodsText, Request.Methods)
		|| !ParseScenarios(ScenariosText, Request.Scenarios)
		|| !ParseSeeds(SeedsText, Request.Seeds))
	{
		UE_LOG(LogAILODExperimentCommandlet, Error,
			TEXT("Required: OutputRoot, ExperimentID, Methods, Scenarios, Seeds, GitCommit. Lists use + or comma."));
		return 2;
	}
	FParse::Value(*Params, TEXT("Mode="), ModeText);
	if (!ParseMode(ModeText, Request.Mode))
	{
		UE_LOG(LogAILODExperimentCommandlet, Error, TEXT("Mode must be Validation, Accuracy, or Performance."));
		return 2;
	}
	FParse::Value(*Params, TEXT("PopulationPerKingdom="), Request.PopulationPerKingdom);
	FParse::Value(*Params, TEXT("RepeatCount="), Request.RepeatCount);
	FParse::Value(*Params, TEXT("OrderSeed="), Request.OrderSeed);
	FParse::Value(*Params, TEXT("LogMode="), Request.LogMode);
	Request.bRandomizeRunOrder = FParse::Param(*Params, TEXT("Randomize"));
	Request.bResumeCompletedRuns = FParse::Param(*Params, TEXT("Resume"));
	Request.bFormalRunRequested = FParse::Param(*Params, TEXT("Formal"));
	Request.bEnableMacroProfiling = FParse::Param(*Params, TEXT("MacroProfile"));
	Request.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
	Request.OutputRoot = FPaths::ConvertRelativePathToFull(Request.OutputRoot);
	Request.UEVersion = FEngineVersion::Current().ToString();
	Request.BuildType = BuildTypeName();
	Request.Hardware = HardwareName();
	Request.bFormalEnvironmentEligible = Request.bFormalRunRequested
		&& Request.bRandomizeRunOrder
		&& Request.OrderSeed != 0
		&& IsGitCommit(Request.GitCommit)
		&& Request.BuildType.Contains(TEXT("Shipping"), ESearchCase::IgnoreCase);

	TArray<FExperimentRunRecord> Runs;
	FString Error;
	if (!FExperimentRunner::RunMatrix(Request, Runs, Error))
	{
		UE_LOG(LogAILODExperimentCommandlet, Error, TEXT("Experiment failed: %s"), *Error);
		return 1;
	}
	if (FParse::Param(*Params, TEXT("BuildSummary")))
	{
		if (!FOfflineMetricsEvaluator::BuildSummary(
			Request.OutputRoot,
			FPaths::Combine(Request.OutputRoot, LogSchema::MetricsSummaryFile),
			Error))
		{
			UE_LOG(LogAILODExperimentCommandlet, Error, TEXT("Metric rebuild failed: %s"), *Error);
			return 1;
		}
	}
	int32 Skipped = 0;
	for (const FExperimentRunRecord& Run : Runs) Skipped += Run.bSkippedExisting ? 1 : 0;
	UE_LOG(LogAILODExperimentCommandlet, Display,
		TEXT("Experiment completed: runs=%d skipped_existing=%d output=%s"),
		Runs.Num(), Skipped, *Request.OutputRoot);
	return 0;
}
