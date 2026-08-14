// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODSimulationCore.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase1EmptyScenarioTest,
	"AILODResearch.Phase1.EmptyScenario60Days",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase1EmptyScenarioTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	const int32 FrozenPopulationsPerKingdom[] = { 100, 1000, 5000, 10000 };
	for (const int32 PopulationPerKingdom : FrozenPopulationsPerKingdom)
	{
		FEmptyScenarioResult Result;
		FString Error;
		TestTrue(
			FString::Printf(TEXT("Empty scenario N=%d runs"), PopulationPerKingdom),
			FEmptyScenarioRunner::Run60Days(PopulationPerKingdom, Result, Error));
		TestEqual(TEXT("Empty scenario finishes at Day 60"), Result.FinalTime.Minutes, FSimulationTime::FromDays(60).Minutes);
		TestEqual(TEXT("Empty scenario advances 1,440 hourly steps"), Result.HourSteps, 60 * 24);
		TestEqual(TEXT("Population total matches both kingdoms"), Result.Population.Total, PopulationPerKingdom * 2);
		TestEqual(TEXT("Population residual is zero"), Result.Audit.PopulationResidual, 0);
		TestTrue(TEXT("Wood residual is zero"), FMath::IsNearlyZero(Result.Audit.WoodResidual, UE_DOUBLE_SMALL_NUMBER));
		TestEqual(TEXT("Duplicate transaction count is zero"), Result.Audit.DuplicateTransactionCount, 0);
		TestEqual(TEXT("Negative stock count is zero"), Result.Audit.NegativeStockCount, 0);
		TestTrue(TEXT("All empty-scenario hard-error gates pass"), Result.Audit.IsHardErrorFree());
		AddInfo(FString::Printf(
			TEXT("EmptyScenario TotalPopulation=%d FinalTime=%s HourSteps=%d PopulationResidual=%d WoodResidual=%.9f DuplicateTransactions=%d NegativeStocks=%d"),
			Result.Population.Total,
			*Result.FinalTime.ToString(),
			Result.HourSteps,
			Result.Audit.PopulationResidual,
			Result.Audit.WoodResidual,
			Result.Audit.DuplicateTransactionCount,
			Result.Audit.NegativeStockCount));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase1CoreContractsTest,
	"AILODResearch.Phase1.CoreContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase1CoreContractsTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FString Error;
	FSimulationClock Clock;
	TestTrue(TEXT("Clock advances to an exact mid-hour time"), Clock.AdvanceTo(FSimulationTime::FromMinutes(12 * 60 + 17), Error));
	TestEqual(TEXT("Clock formats the authoritative time"), Clock.Now().ToString(), FString(TEXT("D00T12:17")));
	TestFalse(TEXT("Clock rejects backwards time"), Clock.AdvanceTo(FSimulationTime::FromHours(12), Error));
	TestEqual(TEXT("Rejected backwards advance does not change time"), Clock.Now().Minutes, static_cast<int64>(12 * 60 + 17));

	FSimulationScheduler Scheduler;
	const FArriveID FirstArrival = Scheduler.IssueArriveID();
	const FArriveID SecondArrival = Scheduler.IssueArriveID();
	const FArriveID ThirdArrival = Scheduler.IssueArriveID();
	TestTrue(TEXT("Later arrival can be inserted first"), Scheduler.Schedule({ 200, SecondArrival, FSimulationTime::FromHours(5) }, {}, Error));
	TestTrue(TEXT("Earlier arrival can be inserted second"), Scheduler.Schedule({ 100, FirstArrival, FSimulationTime::FromHours(5) }, {}, Error));
	TestTrue(TEXT("Later timestamp can be scheduled"), Scheduler.Schedule({ 300, ThirdArrival, FSimulationTime::FromHours(6) }, {}, Error));
	TArray<FScheduledEvent> DueEvents;
	Scheduler.PopDueThrough(FSimulationTime::FromHours(5), DueEvents);
	TestEqual(TEXT("Two events are due through Hour 5"), DueEvents.Num(), 2);
	if (DueEvents.Num() == 2)
	{
		TestEqual(TEXT("Same-time competition uses lower ArriveID first"), DueEvents[0].EventID, static_cast<FEventID>(100));
		TestEqual(TEXT("Second arrival follows"), DueEvents[1].EventID, static_cast<FEventID>(200));
	}
	TestEqual(TEXT("One future event remains queued"), Scheduler.NumPending(), 1);

	FResourceLedger Ledger;
	TestTrue(TEXT("Initial Forest account is valid"), Ledger.InitializeAccount(ESimulationResource::Wood, TEXT("A.Forest"), 100.0, Error));
	TestTrue(TEXT("Initial Market account is valid"), Ledger.InitializeAccount(ESimulationResource::Wood, TEXT("A.Market"), 0.0, Error));
	TestTrue(TEXT("Initial Wallet Coin account is valid"), Ledger.InitializeAccount(ESimulationResource::Coin, TEXT("Resident.Wallet"), 10.0, Error));
	TestTrue(TEXT("Initial Treasury Coin account is valid"), Ledger.InitializeAccount(ESimulationResource::Coin, TEXT("A.Treasury"), 0.0, Error));
	Ledger.SealInitialState();

	FTransactionID TransactionID = 0;
	FLedgerTransferRequest Transfer;
	Transfer.IdempotencyKey = TEXT("TEST-HARVEST-1");
	Transfer.Source = TEXT("A.Forest");
	Transfer.Destination = TEXT("A.Market");
	Transfer.Quantity = 10.0;
	TestTrue(TEXT("Internal Wood transfer commits"), Ledger.SubmitTransfer(Transfer, TransactionID, Error));
	TestEqual(TEXT("First committed TransactionID is stable"), TransactionID, static_cast<FTransactionID>(1));

	FLedgerTransferRequest Import = Transfer;
	Import.IdempotencyKey = TEXT("TEST-IMPORT-1");
	Import.Source = ExternalBoundaryAccount;
	Import.Destination = TEXT("A.Market");
	Import.Quantity = 5.0;
	Import.bBoundaryFlow = true;
	TestTrue(TEXT("Boundary import commits"), Ledger.SubmitTransfer(Import, TransactionID, Error));

	FLedgerTransferRequest Consumption = Transfer;
	Consumption.IdempotencyKey = TEXT("TEST-CONSUME-1");
	Consumption.Source = TEXT("A.Market");
	Consumption.Destination = ExternalBoundaryAccount;
	Consumption.Quantity = 3.0;
	Consumption.bBoundaryFlow = true;
	TestTrue(TEXT("Boundary consumption commits"), Ledger.SubmitTransfer(Consumption, TransactionID, Error));
	TestTrue(TEXT("Wood residual remains zero across internal and boundary flows"), FMath::IsNearlyZero(Ledger.ComputeResidual(ESimulationResource::Wood), UE_DOUBLE_SMALL_NUMBER));

	const double MarketBeforeDuplicate = Ledger.GetBalance(ESimulationResource::Wood, TEXT("A.Market"));
	TestFalse(TEXT("Duplicate IdempotencyKey is rejected"), Ledger.SubmitTransfer(Import, TransactionID, Error));
	TestEqual(TEXT("Rejected duplicate does not change stock"), Ledger.GetBalance(ESimulationResource::Wood, TEXT("A.Market")), MarketBeforeDuplicate);
	TestEqual(TEXT("Duplicate transaction attempt is counted"), Ledger.GetDuplicateTransactionCount(), 1);

	FLedgerTransferRequest FractionalCoin;
	FractionalCoin.IdempotencyKey = TEXT("TEST-COIN-FRACTION");
	FractionalCoin.Resource = ESimulationResource::Coin;
	FractionalCoin.Source = TEXT("Resident.Wallet");
	FractionalCoin.Destination = TEXT("A.Treasury");
	FractionalCoin.Quantity = 1.5;
	TestFalse(TEXT("Fractional Coin transfer is rejected"), Ledger.SubmitTransfer(FractionalCoin, TransactionID, Error));
	FractionalCoin.IdempotencyKey = TEXT("TEST-COIN-INTEGER");
	FractionalCoin.Quantity = 2.0;
	TestTrue(TEXT("Integer Coin transfer commits"), Ledger.SubmitTransfer(FractionalCoin, TransactionID, Error));
	TestTrue(TEXT("Coin residual remains zero"), FMath::IsNearlyZero(Ledger.ComputeResidual(ESimulationResource::Coin), UE_DOUBLE_SMALL_NUMBER));

	FReservationStore Reservations;
	FReservationRequest ReservationRequest;
	ReservationRequest.IdempotencyKey = TEXT("TEST-RESERVE-1");
	ReservationRequest.SourceAccount = TEXT("A.Market");
	ReservationRequest.ReservedAccount = TEXT("A.MarketReserved");
	ReservationRequest.Quantity = 4.0;
	ReservationRequest.ArriveID = FirstArrival;
	FReservationID ReservationID = 0;
	TestTrue(TEXT("Reservation moves Wood into a reserved account"), Reservations.CreateReservation(ReservationRequest, Ledger, ReservationID, Error));
	TestTrue(TEXT("Reservation commits to its destination once"), Reservations.CommitReservation(ReservationID, TEXT("Resident.Inventory"), TEXT("TEST-RESERVE-COMMIT-1"), Clock.Now(), Ledger, Error));
	const FReservationRecord* CommittedReservation = Reservations.Find(ReservationID);
	TestTrue(TEXT("Committed reservation remains recorded"), CommittedReservation != nullptr && CommittedReservation->State == EReservationState::Committed);

	ReservationRequest.IdempotencyKey = TEXT("TEST-RESERVE-2");
	ReservationRequest.Quantity = 2.0;
	TestTrue(TEXT("Second reservation can be created"), Reservations.CreateReservation(ReservationRequest, Ledger, ReservationID, Error));
	TestTrue(TEXT("Reservation release returns Wood to its source"), Reservations.ReleaseReservation(ReservationID, TEXT("TEST-RESERVE-RELEASE-2"), Clock.Now(), Ledger, Error));
	TestTrue(TEXT("Reservations preserve Wood conservation"), FMath::IsNearlyZero(Ledger.ComputeResidual(ESimulationResource::Wood), UE_DOUBLE_SMALL_NUMBER));
	TestEqual(TEXT("Ledger never contains a negative stock"), Ledger.CountNegativeStocks(), 0);

	FSimulationEventStore EventStore;
	FSimulationEventRequest EventRequest;
	EventRequest.Type = TEXT("Repair");
	EventRequest.Owner = TEXT("Macro:A");
	EventRequest.StartTime = FSimulationTime::FromDays(1);
	EventRequest.EndTime = FSimulationTime::FromDays(3);
	EventRequest.ReservationID = ReservationID;
	EventRequest.ArriveID = FirstArrival;
	EventRequest.ParticipantCount = 1;
	FEventID EventID = 0;
	TestTrue(TEXT("Event Store creates a stable EventID"), EventStore.CreateEvent(EventRequest, EventID, Error));
	TestEqual(TEXT("First EventID is stable"), EventID, static_cast<FEventID>(1));
	TestFalse(TEXT("Owner transfer rejects an unexpected current owner"), EventStore.TransferOwner(EventID, TEXT("WrongOwner"), TEXT("Micro:1"), Error));
	TestEqual(TEXT("Owner conflict is counted"), EventStore.GetOwnerConflictCount(), 1);
	TestTrue(TEXT("Owner transfer succeeds with the expected owner"), EventStore.TransferOwner(EventID, TEXT("Macro:A"), TEXT("Micro:1"), Error));
	const FSimulationEventRecord* Event = EventStore.Find(EventID);
	TestTrue(TEXT("Event has exactly one current owner"), Event != nullptr && Event->Event.Owner == TEXT("Micro:1"));
	TestTrue(TEXT("Event completes once"), EventStore.CompleteEvent(EventID, Error));
	TestFalse(TEXT("Duplicate event completion is rejected"), EventStore.CompleteEvent(EventID, Error));
	TestEqual(TEXT("Duplicate completion attempt is counted"), EventStore.GetDuplicateCompletionCount(), 1);

	return true;
}

#endif
