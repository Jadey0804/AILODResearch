// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "../Simulation/AILODDomainRules.h"
#include "../Simulation/AILODIndividualSimulation.h"
#include "../Simulation/AILODLogSchema.h"

namespace
{
	bool ReplayLedger(const AILOD::FStage3OracleRunResult& Result, FString& OutError)
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
				OutError = TEXT("Ledger contains a duplicate IdempotencyKey.");
				return false;
			}
			if (Transfer.GameTime.Minutes < PreviousGameTime)
			{
				OutError = TEXT("Ledger game time moved backwards.");
				return false;
			}
			IdempotencyKeys.Add(Transfer.IdempotencyKey);
			PreviousGameTime = Transfer.GameTime.Minutes;

			const bool bSourceInternal = Transfer.Source != ExternalBoundaryAccount;
			const bool bDestinationInternal = Transfer.Destination != ExternalBoundaryAccount;
			if (Transfer.bBoundaryFlow != (bSourceInternal != bDestinationInternal))
			{
				OutError = TEXT("BoundaryFlow flag does not match the endpoints.");
				return false;
			}
			if (bSourceInternal)
			{
				double& Balance = Replayed.FindOrAdd({ Transfer.Resource, Transfer.Source });
				Balance -= Transfer.Quantity;
				if (Balance < -UE_DOUBLE_SMALL_NUMBER)
				{
					OutError = TEXT("Ledger replay produced a negative stock.");
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
			const double* Balance = Replayed.Find(Pair.Key);
			if (Balance == nullptr || !FMath::IsNearlyEqual(*Balance, Pair.Value, 1.e-6))
			{
				OutError = FString::Printf(TEXT("Ledger replay mismatch for %s."), *Pair.Key.Account);
				return false;
			}
		}

		OutError.Reset();
		return true;
	}

	bool HasTransactionPrefix(const AILOD::FStage3OracleRunResult& Result, const TCHAR* Prefix)
	{
		return Result.Transactions.ContainsByPredicate([Prefix](const AILOD::FLedgerTransaction& Transaction)
		{
			return Transaction.Transfer.IdempotencyKey.StartsWith(Prefix);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase3UtilityGOAPTest,
	"AILODResearch.Phase3.UtilityAndGOAP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase3UtilityGOAPTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FOracleResidentState Resident;
	Resident.ResidentID = 1;
	Resident.Profession = EProfession::Worker;
	Resident.IncomeBand = EIncomeBand::Low;
	FIndividualWorldFacts World;
	World.MarketWoodAvailable = 200.0;
	World.ForestWood = 1600.0;
	World.HarvestAllowance = 1000.0;
	World.WoodPrice = 1.0;

	FIndividualPlan Plan = FIndividualDomain::BuildPlan(Resident, World);
	TestEqual(TEXT("Healthy resident selects RoutineLife"), Plan.Goal, EIndividualGoal::RoutineLife);
	TestEqual(TEXT("Healthy resident plans Routine"), Plan.Actions[0], EIndividualAction::Routine);

	Resident.HomeState = EHomeState::DamagedWaiting;
	Resident.InventoryWood = 4;
	Plan = FIndividualDomain::BuildPlan(Resident, World);
	TestEqual(TEXT("Damaged resident selects RestoreHome"), Plan.Goal, EIndividualGoal::RestoreHome);
	TestEqual(TEXT("Resident holding four Wood starts repair"), Plan.Actions[0], EIndividualAction::StartRepair);

	Resident.InventoryWood = 0;
	Resident.Cash = 4;
	Plan = FIndividualDomain::BuildPlan(Resident, World);
	TestEqual(TEXT("Affordable full purchase plans BuyWood"), Plan.Actions[0], EIndividualAction::BuyWood);

	World.MarketWoodAvailable = 3.9;
	Plan = FIndividualDomain::BuildPlan(Resident, World);
	TestEqual(TEXT("Partial Market stock cannot create a split purchase"), Plan.Actions[0], EIndividualAction::Wait);

	World.MarketWoodAvailable = 200.0;
	Resident.Cash = 0;
	Resident.Profession = EProfession::Logger;
	Plan = FIndividualDomain::BuildPlan(Resident, World);
	TestEqual(TEXT("Logger without funds plans ChopWood"), Plan.Actions[0], EIndividualAction::ChopWood);

	Resident.Profession = EProfession::Worker;
	Plan = FIndividualDomain::BuildPlan(Resident, World);
	TestEqual(TEXT("Worker without funds plans Work"), Plan.Actions[0], EIndividualAction::Work);

	Resident.HomeState = EHomeState::UnderRepair;
	Plan = FIndividualDomain::BuildPlan(Resident, World);
	TestEqual(TEXT("UnderRepair resident continues the committed repair"), Plan.Actions[0], EIndividualAction::ContinueRepair);

	TestEqual(
		TEXT("Payment rounds the total once"),
		DomainRules::PaymentCoins(3, 1.17),
		static_cast<int64>(4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase3OracleTest,
	"AILODResearch.Phase3.Oracle60Days",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase3OracleTest::RunTest(const FString& Parameters)
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
	TArray<FStage3OracleRunResult> Results;

	for (const EStage2Scenario Scenario : Scenarios)
	{
		FStage3OracleRunResult Result;
		FString Error;
		TestTrue(
			FString::Printf(TEXT("Oracle %s scenario runs"), ToString(Scenario)),
			FIndividualOracleRunner::Run(Config, Scenario, Result, Error));
		if (!Error.IsEmpty())
		{
			AddError(Error);
		}
		TestEqual(TEXT("Oracle remains frozen to 200 residents"), Result.Residents.Num(), 200);
		TestEqual(TEXT("Formal run ends at Day 60"), Result.FinalTime.Minutes, FSimulationTime::FromDays(60).Minutes);
		TestEqual(TEXT("Warm-up has 168 hourly steps"), Result.WarmupHourSteps, 7 * 24);
		TestEqual(TEXT("Formal period has 1,440 hourly steps"), Result.FormalHourSteps, 60 * 24);
		TestEqual(TEXT("A has all 30 damaged homes repaired"), Result.GetHomeStateCount(EKingdom::A, EHomeState::Repaired), 30);
		TestEqual(TEXT("A has no damaged homes still waiting"), Result.GetHomeStateCount(EKingdom::A, EHomeState::DamagedWaiting), 0);
		TestEqual(TEXT("A has no repair still active at Day 60"), Result.GetHomeStateCount(EKingdom::A, EHomeState::UnderRepair), 0);
		TestEqual(TEXT("B remains fully healthy"), Result.GetHomeStateCount(EKingdom::B, EHomeState::Healthy), 100);
		TestTrue(TEXT("Population and Wood hard-error gates pass"), Result.Audit.IsHardErrorFree());
		TestTrue(TEXT("Coin residual is zero"), FMath::IsNearlyZero(Result.CoinResidual, 1.e-6));
		TestTrue(TEXT("Ledger can reconstruct every final account"), ReplayLedger(Result, Error));

		for (const FOracleResidentState& Resident : Result.Residents)
		{
			const double CashBalance = Result.FinalBalances.FindRef({
				ESimulationResource::Coin,
				FString::Printf(TEXT("Resident.%lld.Cash"), Resident.ResidentID) });
			const double CreditBalance = Result.FinalBalances.FindRef({
				ESimulationResource::Coin,
				FString::Printf(TEXT("Resident.%lld.RepairCredit"), Resident.ResidentID) });
			const double WoodBalance = Result.FinalBalances.FindRef({
				ESimulationResource::Wood,
				FString::Printf(TEXT("Resident.%lld.Wood"), Resident.ResidentID) });
			TestEqual(TEXT("Resident Cash mirrors the Ledger"), static_cast<int32>(CashBalance), Resident.Cash);
			TestEqual(TEXT("Resident RepairCredit mirrors the Ledger"), static_cast<int32>(CreditBalance), Resident.RepairCredit);
			TestEqual(TEXT("Resident Wood mirrors the Ledger"), static_cast<int32>(WoodBalance), Resident.InventoryWood);
		}

		const FString LedgerTrace = FIndividualOracleRunner::SerializeLedgerTrace(
			Result,
			TEXT("PHASE3-ACCEPTANCE"),
			FString::Printf(TEXT("P3-%s-%d"), ToString(Scenario), Config.Seed));
		TArray<FString> Lines;
		LedgerTrace.ParseIntoArrayLines(Lines, true);
		TestEqual(TEXT("Oracle Ledger JSONL has one line per transaction"), Lines.Num(), Result.Transactions.Num());
		for (const FString& Line : Lines)
		{
			TSharedPtr<FJsonObject> JsonObject;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
			if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
			{
				AddError(TEXT("Oracle Ledger JSONL line failed to parse."));
				break;
			}
		}

		const FString OutputDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("AILOD/Phase3"),
			ToString(Scenario));
		IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		TestTrue(
			TEXT("Oracle Ledger JSONL saves"),
			FFileHelper::SaveStringToFile(
				LedgerTrace,
				*FPaths::Combine(OutputDirectory, LogSchema::LedgerTransactionsFile),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

		AddInfo(FString::Printf(
			TEXT("Phase3 Scenario=%s Digest=%s Transactions=%d Events=%d Work=%d Buy=%d Chop=%d RepairStart=%d Wait=%d Aid=%d Import=%.6f"),
			ToString(Scenario),
			*FIndividualOracleRunner::BuildDeterministicDigest(Result),
			Result.Transactions.Num(),
			Result.Events.Num(),
			Result.GetActionCount(EIndividualAction::Work),
			Result.GetActionCount(EIndividualAction::BuyWood),
			Result.GetActionCount(EIndividualAction::ChopWood),
			Result.GetActionCount(EIndividualAction::StartRepair),
			Result.GetActionCount(EIndividualAction::Wait),
			Result.AidPaidCount,
			Result.AdditionalImportedWood));

		Results.Add(MoveTemp(Result));
	}

	if (Results.Num() == 4)
	{
		const FStage3OracleRunResult& None = Results[0];
		const FStage3OracleRunResult& StateImport = Results[2];
		const FStage3OracleRunResult& RepairAid = Results[3];

		TestTrue(TEXT("Routine action is produced"), None.GetActionCount(EIndividualAction::Routine) > 0);
		TestTrue(TEXT("Work action is produced"), None.GetActionCount(EIndividualAction::Work) > 0);
		TestTrue(TEXT("BuyWood action is produced"), None.GetActionCount(EIndividualAction::BuyWood) > 0);
		TestTrue(TEXT("ChopWood action is produced"), None.GetActionCount(EIndividualAction::ChopWood) > 0);
		TestEqual(TEXT("Exactly 30 repairs start"), None.GetActionCount(EIndividualAction::StartRepair), 30);
		TestEqual(TEXT("Exactly 30 committed repairs complete"), None.GetActionCount(EIndividualAction::ContinueRepair, false), 30);
		TestTrue(TEXT("Wait action is produced by scarce repair capacity"), None.GetActionCount(EIndividualAction::Wait) > 0);
		TestTrue(TEXT("Work income is a Boundary Ledger flow"), HasTransactionPrefix(None, TEXT("WORK-INCOME-")));
		TestTrue(TEXT("BuyWood uses a Market reservation"), HasTransactionPrefix(None, TEXT("BUY-WOOD-RESERVE-")));
		TestTrue(TEXT("ChopWood transfers Forest Wood through Ledger"), HasTransactionPrefix(None, TEXT("CHOP-WOOD-")));
		TestTrue(TEXT("Repair start embeds Wood through Ledger"), HasTransactionPrefix(None, TEXT("REPAIR-START-")));
		TestTrue(TEXT("Repair completion settles embedded Wood once"), HasTransactionPrefix(None, TEXT("REPAIR-COMPLETE-")));
		TestTrue(TEXT("State Import remains available to Oracle"), StateImport.AdditionalImportedWood > 0.0);
		TestTrue(TEXT("Repair Aid pays at least one eligible resident"), RepairAid.AidPaidCount > 0);

		bool bFoundMixedPayment = false;
		for (int32 CreditIndex = 0; CreditIndex < RepairAid.Transactions.Num(); ++CreditIndex)
		{
			const FLedgerTransaction& Credit = RepairAid.Transactions[CreditIndex];
			if (!Credit.Transfer.IdempotencyKey.StartsWith(TEXT("BUY-WOOD-CREDIT-")))
			{
				continue;
			}
			const int32 CashIndex = RepairAid.Transactions.IndexOfByPredicate([&Credit](const FLedgerTransaction& Candidate)
			{
				return Candidate.Transfer.EventID == Credit.Transfer.EventID
					&& Candidate.Transfer.IdempotencyKey.StartsWith(TEXT("BUY-WOOD-CASH-"));
			});
			if (CashIndex != INDEX_NONE)
			{
				bFoundMixedPayment = true;
				TestTrue(TEXT("RepairCredit is paid before Cash for the same purchase"), CreditIndex < CashIndex);
				break;
			}
		}
		TestTrue(TEXT("Repair Aid scenario exercises a mixed Credit plus Cash purchase"), bFoundMixedPayment);

		TArray<FResidentID> Day0StartedResidents;
		for (const FIndividualActionTrace& Trace : None.ActionTrace)
		{
			if (Trace.GameTime.Minutes == 0 && Trace.bStarted)
			{
				Day0StartedResidents.Add(Trace.ResidentID);
			}
		}
		bool bHasDescendingPair = false;
		for (int32 Index = 1; Index < Day0StartedResidents.Num(); ++Index)
		{
			if (Day0StartedResidents[Index] < Day0StartedResidents[Index - 1])
			{
				bHasDescendingPair = true;
				break;
			}
		}
		TestTrue(TEXT("Day 0 competition order is not ResidentID order"), bHasDescendingPair);

		FStage3OracleRunResult Repeat;
		FString Error;
		TestTrue(TEXT("Repeated Oracle run completes"), FIndividualOracleRunner::Run(Config, EStage2Scenario::None, Repeat, Error));
		TestEqual(
			TEXT("Same Seed produces the same Oracle digest"),
			FIndividualOracleRunner::BuildDeterministicDigest(None),
			FIndividualOracleRunner::BuildDeterministicDigest(Repeat));
		TestEqual(
			TEXT("Same Seed produces byte-identical Oracle Ledger JSONL"),
			FIndividualOracleRunner::SerializeLedgerTrace(None, TEXT("DET"), TEXT("RUN")),
			FIndividualOracleRunner::SerializeLedgerTrace(Repeat, TEXT("DET"), TEXT("RUN")));
	}

	return true;
}

#endif
