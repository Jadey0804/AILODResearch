// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "../Simulation/AILODLogSchema.h"
#include "../Simulation/AILODStructuredMacro.h"

namespace
{
	bool IsInternalAccount(const FString& Account)
	{
		return Account != AILOD::ExternalBoundaryAccount;
	}

	bool ReplayLedger(const AILOD::FStage2RunResult& Result, FString& OutError)
	{
		using namespace AILOD;
		TMap<FResourceAccountKey, double> Replayed = Result.InitialBalances;
		FTransactionID ExpectedTransactionID = 1;
		TSet<FString> IdempotencyKeys;
		int64 PreviousGameTime = TNumericLimits<int64>::Lowest();

		for (const FLedgerTransaction& Transaction : Result.Transactions)
		{
			const FLedgerTransferRequest& Transfer = Transaction.Transfer;
			if (Transaction.TransactionID != ExpectedTransactionID++)
			{
				OutError = TEXT("TransactionID sequence is not contiguous.");
				return false;
			}
			if (IdempotencyKeys.Contains(Transfer.IdempotencyKey))
			{
				OutError = TEXT("Ledger trace contains a duplicate IdempotencyKey.");
				return false;
			}
			if (Transfer.GameTime.Minutes < PreviousGameTime)
			{
				OutError = TEXT("Ledger trace game time moved backwards.");
				return false;
			}
			IdempotencyKeys.Add(Transfer.IdempotencyKey);
			PreviousGameTime = Transfer.GameTime.Minutes;

			const bool bSourceInternal = IsInternalAccount(Transfer.Source);
			const bool bDestinationInternal = IsInternalAccount(Transfer.Destination);
			if (Transfer.bBoundaryFlow != (bSourceInternal != bDestinationInternal))
			{
				OutError = TEXT("Ledger trace has an invalid BoundaryFlow flag.");
				return false;
			}
			if (Transfer.Quantity <= 0.0)
			{
				OutError = TEXT("Ledger trace has a non-positive quantity.");
				return false;
			}

			if (bSourceInternal)
			{
				double& Balance = Replayed.FindOrAdd({ Transfer.Resource, Transfer.Source });
				Balance -= Transfer.Quantity;
				if (Balance < -UE_DOUBLE_SMALL_NUMBER)
				{
					OutError = TEXT("Replayed ledger produced a negative source stock.");
					return false;
				}
			}
			if (bDestinationInternal)
			{
				Replayed.FindOrAdd({ Transfer.Resource, Transfer.Destination }) += Transfer.Quantity;
			}
		}

		for (const TPair<FResourceAccountKey, double>& Pair : Result.FinalBalances)
		{
			const double* ReplayedBalance = Replayed.Find(Pair.Key);
			if (ReplayedBalance == nullptr || !FMath::IsNearlyEqual(*ReplayedBalance, Pair.Value, 1.e-6))
			{
				OutError = FString::Printf(TEXT("Ledger replay mismatch for %s."), *Pair.Key.Account);
				return false;
			}
		}

		OutError.Reset();
		return true;
	}

	int32 GetBucketCount(
		const AILOD::FKingdomState& Kingdom,
		const AILOD::EProfession Profession,
		const AILOD::EIncomeBand IncomeBand,
		const AILOD::EHomeState HomeState)
	{
		using namespace AILOD;
		const FCohortKey Key{ Kingdom.Kingdom, Profession, IncomeBand, HomeState, EMacroIntent::Routine };
		if (const FCohortBucket* Bucket = Kingdom.Cohorts.Find(Key))
		{
			return Bucket->PopulationCount;
		}
		return 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase2FourScenariosTest,
	"AILODResearch.Phase2.FourScenarios60Days",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase2FourScenariosTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FPhase0Config Config;
	Config.Seed = 20260810;
	Config.PopulationPerKingdom = 100;
	const EStage2Scenario Scenarios[] =
	{
		EStage2Scenario::None,
		EStage2Scenario::HarvestCap,
		EStage2Scenario::StateImport,
		EStage2Scenario::RepairAid
	};
	TArray<FStage2RunResult> Results;

	for (const EStage2Scenario Scenario : Scenarios)
	{
		FStage2RunResult Result;
		FString Error;
		TestTrue(
			FString::Printf(TEXT("%s scenario runs"), ToString(Scenario)),
			FStructuredMacroRunner::Run(Config, Scenario, Result, Error));
		TestEqual(TEXT("Formal run ends at Day 60"), Result.FinalTime.Minutes, FSimulationTime::FromDays(60).Minutes);
		TestEqual(TEXT("Warm-up uses 168 hourly steps"), Result.WarmupHourSteps, 7 * 24);
		TestEqual(TEXT("Formal measurement uses 1,440 hourly steps"), Result.FormalHourSteps, 60 * 24);
		TestEqual(TEXT("Cohort population is conserved"), GetCohortPopulation(Result), 200);
		TestTrue(TEXT("Sparse Cohort count stays within the frozen maximum"), Result.KingdomA.Cohorts.Num() + Result.KingdomB.Cohorts.Num() <= 192);
		TestEqual(TEXT("A has exactly 30 damaged homes"), GetHomeStateCount(Result.KingdomA, EHomeState::DamagedWaiting), 30);
		TestEqual(TEXT("B remains fully healthy"), GetHomeStateCount(Result.KingdomB, EHomeState::Healthy), 100);
		TestEqual(TEXT("Stage 2 does not start repairs"), GetHomeStateCount(Result.KingdomA, EHomeState::UnderRepair), 0);
		TestEqual(TEXT("Stage 2 does not complete repairs"), GetHomeStateCount(Result.KingdomA, EHomeState::Repaired), 0);
		TestEqual(TEXT("Population residual is zero"), Result.Audit.PopulationResidual, 0);
		TestTrue(TEXT("Wood residual is zero"), FMath::IsNearlyZero(Result.Audit.WoodResidual, 1.e-6));
		TestEqual(TEXT("Duplicate transactions are zero"), Result.Audit.DuplicateTransactionCount, 0);
		TestEqual(TEXT("Negative stocks are zero"), Result.Audit.NegativeStockCount, 0);
		TestEqual(TEXT("Event owner conflicts are zero"), Result.Audit.EventOwnerConflictCount, 0);
		TestEqual(TEXT("Duplicate event completions are zero"), Result.Audit.DuplicateCompletionCount, 0);
		TestTrue(TEXT("All Stage 2 hard-error gates pass"), Result.Audit.IsHardErrorFree());

		for (const FKingdomSnapshot& Snapshot : Result.Snapshots)
		{
			const bool bNonNegative = Snapshot.Stocks.ForestWood >= -UE_DOUBLE_SMALL_NUMBER
				&& Snapshot.Stocks.MarketWoodAvailable >= -UE_DOUBLE_SMALL_NUMBER
				&& Snapshot.Stocks.MarketWoodReserved >= -UE_DOUBLE_SMALL_NUMBER
				&& Snapshot.Stocks.WoodInTransit >= -UE_DOUBLE_SMALL_NUMBER
				&& Snapshot.Stocks.WoodEmbeddedInRepairs >= -UE_DOUBLE_SMALL_NUMBER
				&& Snapshot.Stocks.WoodInRepairedHomes >= -UE_DOUBLE_SMALL_NUMBER
				&& Snapshot.Stocks.TreasuryAvailable >= 0
				&& Snapshot.Stocks.TreasuryReserved >= 0;
			if (!bNonNegative)
			{
				AddError(FString::Printf(TEXT("Negative stock at %s."), *Snapshot.GameTime.ToString()));
				break;
			}
		}

		TestTrue(TEXT("Every final Stock can be reconstructed from the Ledger trace"), ReplayLedger(Result, Error));

		const FString LedgerTrace = FStructuredMacroRunner::SerializeLedgerTrace(
			Result,
			TEXT("PHASE2-ACCEPTANCE"),
			FString::Printf(TEXT("P2-%s-%d"), ToString(Scenario), Config.Seed));
		TArray<FString> Lines;
		LedgerTrace.ParseIntoArrayLines(Lines, true);
		TestEqual(TEXT("Ledger JSONL has one line per transaction"), Lines.Num(), Result.Transactions.Num());
		for (const FString& Line : Lines)
		{
			TSharedPtr<FJsonObject> JsonObject;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
			if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
			{
				AddError(TEXT("Ledger JSONL line failed to parse."));
				break;
			}
		}

		const FString OutputDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("AILOD/Phase2"),
			ToString(Scenario));
		IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		const FString OutputPath = FPaths::Combine(OutputDirectory, LogSchema::LedgerTransactionsFile);
		TestTrue(TEXT("Ledger JSONL saves"), FFileHelper::SaveStringToFile(LedgerTrace, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

		AddInfo(FString::Printf(
			TEXT("Phase2 Scenario=%s Digest=%s Transactions=%d Events=%d AForest=%.6f AMarket=%.6f APrice=%.6f ATreasury=%lld ATransit=%.6f Unmet=%.6f"),
			ToString(Scenario),
			*FStructuredMacroRunner::BuildDeterministicDigest(Result),
			Result.Transactions.Num(),
			Result.Events.Num(),
			Result.KingdomA.Stocks.ForestWood,
			Result.KingdomA.Stocks.MarketWoodAvailable,
			Result.KingdomA.Stocks.WoodPrice,
			Result.KingdomA.Stocks.TreasuryAvailable,
			Result.KingdomA.Stocks.WoodInTransit,
			Result.KingdomA.UnmetRoutineConsumption));

		Results.Add(MoveTemp(Result));
	}

	if (Results.Num() == 4)
	{
		const FStage2RunResult& None = Results[0];
		const FStage2RunResult& HarvestCap = Results[1];
		const FStage2RunResult& StateImport = Results[2];
		const FStage2RunResult& RepairAid = Results[3];

		TestTrue(TEXT("None starts from a balanced Market"), FMath::IsNearlyEqual(None.KingdomA.Stocks.MarketWoodAvailable, 200.0, 1.e-6));
		TestTrue(TEXT("None keeps base WoodPrice at 1"), FMath::IsNearlyEqual(None.KingdomA.Stocks.WoodPrice, 1.0, 1.e-6));
		TestTrue(TEXT("Harvest Cap lowers A Market Wood"), HarvestCap.KingdomA.Stocks.MarketWoodAvailable < None.KingdomA.Stocks.MarketWoodAvailable);
		TestTrue(TEXT("Harvest Cap raises A WoodPrice"), HarvestCap.KingdomA.Stocks.WoodPrice > None.KingdomA.Stocks.WoodPrice);
		TestTrue(TEXT("Harvest Cap leaves more Wood in A Forest"), HarvestCap.KingdomA.Stocks.ForestWood > None.KingdomA.Stocks.ForestWood);
		TestTrue(TEXT("State Import adds Wood beyond the baseline flow"), FMath::IsNearlyEqual(StateImport.KingdomA.AdditionalImportedWood, 80.0, 1.e-6));
		TestTrue(TEXT("State Import raises A Market stock"), StateImport.KingdomA.Stocks.MarketWoodAvailable > None.KingdomA.Stocks.MarketWoodAvailable);
		TestEqual(TEXT("State Import spends its frozen 1.0N budget"), StateImport.KingdomA.Stocks.TreasuryAvailable, static_cast<int64>(400));
		TestEqual(TEXT("All import reservations settle by Day 60"), StateImport.KingdomA.Stocks.TreasuryReserved, static_cast<int64>(0));
		TestTrue(TEXT("All ordered import Wood arrives by Day 60"), FMath::IsNearlyZero(StateImport.KingdomA.Stocks.WoodInTransit, 1.e-6));
		TestEqual(TEXT("Repair Aid pays floor(0.40N/2)=20 homes"), RepairAid.KingdomA.AidPaidCount, 20);
		TestEqual(TEXT("Repair Aid moves 40 integer Coin into RepairCredit"), RepairAid.KingdomA.Stocks.ResidentRepairCredit, static_cast<int64>(40));
		TestEqual(TEXT("Repair Aid reduces Treasury by 40 Coin"), RepairAid.KingdomA.Stocks.TreasuryAvailable, static_cast<int64>(460));
		TestTrue(TEXT("Repair Aid does not create Wood"), FMath::IsNearlyEqual(RepairAid.KingdomA.Stocks.MarketWoodAvailable, None.KingdomA.Stocks.MarketWoodAvailable, 1.e-6));

		auto FindTransactionIndex = [](const FStage2RunResult& Result, const FString& IdempotencyKey)
		{
			return Result.Transactions.IndexOfByPredicate([&IdempotencyKey](const FLedgerTransaction& Transaction)
			{
				return Transaction.Transfer.IdempotencyKey == IdempotencyKey;
			});
		};
		const int32 GrowthIndex = FindTransactionIndex(None, TEXT("FOREST-GROWTH-A-M0"));
		const int32 BaselineImportIndex = FindTransactionIndex(None, TEXT("BASELINE-IMPORT-A-M0"));
		const int32 HarvestIndex = FindTransactionIndex(None, TEXT("COMMERCIAL-HARVEST-A-M0"));
		const int32 ConsumptionIndex = FindTransactionIndex(None, TEXT("ROUTINE-CONSUMPTION-A-M0"));
		TestTrue(TEXT("Approved hour order places Growth before Baseline Import"), GrowthIndex != INDEX_NONE && GrowthIndex < BaselineImportIndex);
		TestTrue(TEXT("Approved hour order places Baseline Import before Harvest"), BaselineImportIndex < HarvestIndex);
		TestTrue(TEXT("Approved hour order places Harvest before Routine Consumption"), HarvestIndex < ConsumptionIndex);

		const int32 FirstImportArrivalIndex = StateImport.Transactions.IndexOfByPredicate([](const FLedgerTransaction& Transaction)
		{
			return Transaction.Transfer.IdempotencyKey.StartsWith(TEXT("STATE-IMPORT-ARRIVE-"))
				&& Transaction.Transfer.GameTime == FSimulationTime::FromDays(5);
		});
		const int32 Day5GrowthIndex = FindTransactionIndex(StateImport, TEXT("FOREST-GROWTH-A-M7200"));
		TestTrue(TEXT("Day 5 import arrival is processed before hourly flows"), FirstImportArrivalIndex != INDEX_NONE && FirstImportArrivalIndex < Day5GrowthIndex);
		if (FirstImportArrivalIndex != INDEX_NONE)
		{
			const FLedgerTransferRequest& Arrival = StateImport.Transactions[FirstImportArrivalIndex].Transfer;
			TestEqual(TEXT("State Import arrives from WoodInTransit"), Arrival.Source, MakeKingdomAccount(EKingdom::A, TEXT("WoodInTransit")));
			TestEqual(TEXT("State Import arrives into MarketWoodAvailable"), Arrival.Destination, MakeKingdomAccount(EKingdom::A, TEXT("MarketWoodAvailable")));
		}

		const FCohortKey AidLoggerLowKey{ EKingdom::A, EProfession::Logger, EIncomeBand::Low, EHomeState::DamagedWaiting, EMacroIntent::Routine };
		const FCohortKey AidWorkerLowKey{ EKingdom::A, EProfession::Worker, EIncomeBand::Low, EHomeState::DamagedWaiting, EMacroIntent::Routine };
		const FCohortBucket* AidLoggerLow = RepairAid.KingdomA.Cohorts.Find(AidLoggerLowKey);
		const FCohortBucket* AidWorkerLow = RepairAid.KingdomA.Cohorts.Find(AidWorkerLowKey);
		TestTrue(TEXT("Repair Aid Logger Low batch exists"), AidLoggerLow != nullptr);
		TestTrue(TEXT("Repair Aid Worker Low batch exists"), AidWorkerLow != nullptr);
		if (AidLoggerLow != nullptr && AidWorkerLow != nullptr)
		{
			TestEqual(TEXT("All 4 damaged Logger Low residents are eligible"), AidLoggerLow->AidEligibleCount, 4);
			TestEqual(TEXT("All 4 earlier Logger Low arrivals receive Aid"), AidLoggerLow->AidReceivedCount, 4);
			TestEqual(TEXT("All 17 damaged Worker Low residents are eligible"), AidWorkerLow->AidEligibleCount, 17);
			TestEqual(TEXT("Remaining budget pays 16 Worker Low residents"), AidWorkerLow->AidReceivedCount, 16);
		}

		for (int32 ScenarioIndex = 1; ScenarioIndex < Results.Num(); ++ScenarioIndex)
		{
			const FKingdomStocks& Control = Results[ScenarioIndex].KingdomB.Stocks;
			TestTrue(TEXT("B control Forest is scenario-invariant"), FMath::IsNearlyEqual(Control.ForestWood, None.KingdomB.Stocks.ForestWood, 1.e-6));
			TestTrue(TEXT("B control Market is scenario-invariant"), FMath::IsNearlyEqual(Control.MarketWoodAvailable, None.KingdomB.Stocks.MarketWoodAvailable, 1.e-6));
			TestTrue(TEXT("B control Price is scenario-invariant"), FMath::IsNearlyEqual(Control.WoodPrice, None.KingdomB.Stocks.WoodPrice, 1.e-6));
		}

		TestEqual(TEXT("A damaged Logger Low count is frozen at 4"), GetBucketCount(None.KingdomA, EProfession::Logger, EIncomeBand::Low, EHomeState::DamagedWaiting), 4);
		TestEqual(TEXT("A damaged Logger NonLow count is frozen at 2"), GetBucketCount(None.KingdomA, EProfession::Logger, EIncomeBand::NonLow, EHomeState::DamagedWaiting), 2);
		TestEqual(TEXT("A damaged Worker Low count is frozen at 17"), GetBucketCount(None.KingdomA, EProfession::Worker, EIncomeBand::Low, EHomeState::DamagedWaiting), 17);
		TestEqual(TEXT("A damaged Worker NonLow count is frozen at 7"), GetBucketCount(None.KingdomA, EProfession::Worker, EIncomeBand::NonLow, EHomeState::DamagedWaiting), 7);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase2DeterminismScaleTest,
	"AILODResearch.Phase2.DeterminismAndScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase2DeterminismScaleTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FPhase0Config Config;
	Config.Seed = 20260810;
	Config.PopulationPerKingdom = 100;
	FStage2RunResult RunA;
	FStage2RunResult RunB;
	FString Error;
	TestTrue(TEXT("State Import determinism Run A completes"), FStructuredMacroRunner::Run(Config, EStage2Scenario::StateImport, RunA, Error));
	TestTrue(TEXT("State Import determinism Run B completes"), FStructuredMacroRunner::Run(Config, EStage2Scenario::StateImport, RunB, Error));
	TestEqual(
		TEXT("Same config and Seed produce the same Stage 2 digest"),
		FStructuredMacroRunner::BuildDeterministicDigest(RunA),
		FStructuredMacroRunner::BuildDeterministicDigest(RunB));
	TestEqual(
		TEXT("Same config and Seed produce byte-identical Ledger JSONL"),
		FStructuredMacroRunner::SerializeLedgerTrace(RunA, TEXT("DET"), TEXT("RUN")),
		FStructuredMacroRunner::SerializeLedgerTrace(RunB, TEXT("DET"), TEXT("RUN")));

	FPhase0Config LargeConfig = Config;
	LargeConfig.PopulationPerKingdom = 10000;
	FStage2RunResult LargeRun;
	TestTrue(TEXT("20k State Import scenario completes"), FStructuredMacroRunner::Run(LargeConfig, EStage2Scenario::StateImport, LargeRun, Error));
	TestEqual(TEXT("20k Cohort population is conserved"), GetCohortPopulation(LargeRun), 20000);
	TestEqual(TEXT("20k A earthquake damages 3,000 homes"), GetHomeStateCount(LargeRun.KingdomA, EHomeState::DamagedWaiting), 3000);
	TestTrue(TEXT("20k run has zero Wood residual"), FMath::IsNearlyZero(LargeRun.Audit.WoodResidual, 1.e-5));
	TestTrue(TEXT("20k run has no hard errors"), LargeRun.Audit.IsHardErrorFree());
	TestTrue(TEXT("20k Ledger can be replayed"), ReplayLedger(LargeRun, Error));
	AddInfo(FString::Printf(
		TEXT("Phase2 20k StateImport Digest=%s Transactions=%d Events=%d"),
		*FStructuredMacroRunner::BuildDeterministicDigest(LargeRun),
		LargeRun.Transactions.Num(),
		LargeRun.Events.Num()));

	return true;
}

#endif
