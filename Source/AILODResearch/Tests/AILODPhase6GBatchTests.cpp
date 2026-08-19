// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODV17BatchPrototype.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB2ANoResourceBatchSliceTest,
	"AILODResearch.Phase6G.V17NoResourceBatchSlice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB2ANoResourceBatchSliceTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	constexpr FV17JointCellID RoutineCellID = 0xA001;
	constexpr FV17JointCellID WaitCellID = 0xB001;
	const TArray<FV17BatchPrototypeCell> Cells =
	{
		{ RoutineCellID, 2000 },
		{ WaitCellID, 1000 }
	};

	auto InitializePrototype = [this, &Cells](FV17NoResourceBatchPrototype& Prototype)
	{
		FString Error;
		if (!Prototype.Initialize(Cells, FSimulationTime::FromHours(0), Error))
		{
			AddError(FString::Printf(TEXT("B2A prototype initialization failed: %s"), *Error));
			return false;
		}
		return true;
	};

	auto SubmitFixture = [this, RoutineCellID, WaitCellID](
		FV17NoResourceBatchPrototype& Prototype,
		FEventID& OutRoutineEventID,
		FEventID& OutWaitEventID)
	{
		FString Error;
		if (!Prototype.SubmitBatch(RoutineCellID, EIndividualAction::Routine, 1000, OutRoutineEventID, Error))
		{
			AddError(FString::Printf(TEXT("B2A Routine batch failed: %s"), *Error));
			return false;
		}
		if (!Prototype.SubmitBatch(WaitCellID, EIndividualAction::Wait, 600, OutWaitEventID, Error))
		{
			AddError(FString::Printf(TEXT("B2A Wait batch failed: %s"), *Error));
			return false;
		}
		return true;
	};

	FV17NoResourceBatchPrototype RunA(20260810);
	if (!InitializePrototype(RunA)) return false;
	const FString InitialDigest = RunA.BuildDeterministicDigest();
	TestEqual(TEXT("Initial B2A state has the frozen digest"), InitialDigest, FString(TEXT("0359B40D3F145C8E40D2DD6F44D8DA1F27AB1B5F")));
	FEventID RoutineEventID = 0;
	FEventID WaitEventID = 0;
	if (!SubmitFixture(RunA, RoutineEventID, WaitEventID)) return false;

	TestEqual(TEXT("One Routine batch receives one EventID"), RoutineEventID, FEventID(1));
	TestEqual(TEXT("One Wait batch receives one EventID"), WaitEventID, FEventID(2));
	TestEqual(TEXT("1,600 participants create only two event objects"), RunA.GetEventStore().GetEvents().Num(), 2);
	TestEqual(TEXT("1,600 participants create only two scheduled entries"), RunA.GetPendingEventCount(), 2);
	TestEqual(TEXT("1,600 participants create only two batch claims"), RunA.GetBatchClaims().Num(), 2);
	TestEqual(TEXT("Routine batch keeps one participant-weighted count"), RunA.GetParticipantWeightedActionCount(EIndividualAction::Routine), int64(1000));
	TestEqual(TEXT("Wait batch keeps one participant-weighted count"), RunA.GetParticipantWeightedActionCount(EIndividualAction::Wait), int64(600));
	TestEqual(TEXT("Routine source cell removes exactly 1,000 participants"), RunA.GetReadyCount(RoutineCellID), 1000);
	TestEqual(TEXT("Wait source cell removes exactly 600 participants"), RunA.GetReadyCount(WaitCellID), 400);
	TestEqual(TEXT("Pending events represent all 1,600 busy participants"), RunA.GetPendingParticipantCount(), 1600);
	TestTrue(TEXT("Population and batch counts reconcile after submission"), RunA.BuildAudit().IsHardErrorFree());

	const FV17BatchPrototypeEvent* RoutineBatch = RunA.GetBatchEvents().Find(RoutineEventID);
	const FV17BatchPrototypeEvent* WaitBatch = RunA.GetBatchEvents().Find(WaitEventID);
	const FSimulationEventRecord* StoredRoutine = RunA.GetEventStore().Find(RoutineEventID);
	const FSimulationEventRecord* StoredWait = RunA.GetEventStore().Find(WaitEventID);
	TestTrue(TEXT("Routine batch record exists"), RoutineBatch != nullptr);
	TestTrue(TEXT("Wait batch record exists"), WaitBatch != nullptr);
	TestTrue(TEXT("Routine event stores 1,000 anonymous participants"),
		StoredRoutine != nullptr && StoredRoutine->Event.ResidentID == 0 && StoredRoutine->Event.ParticipantCount == 1000);
	TestTrue(TEXT("Wait event stores 600 anonymous participants"),
		StoredWait != nullptr && StoredWait->Event.ResidentID == 0 && StoredWait->Event.ParticipantCount == 600);
	if (RoutineBatch != nullptr && WaitBatch != nullptr)
	{
		const FV17BatchPrototypeClaim* RoutineClaim = RunA.GetBatchClaims().Find(RoutineBatch->BatchClaimID);
		const FV17BatchPrototypeClaim* WaitClaim = RunA.GetBatchClaims().Find(WaitBatch->BatchClaimID);
		TestTrue(TEXT("Routine batch keeps its complete no-resource claim"), RoutineClaim != nullptr
			&& RoutineClaim->ResourceScope == TEXT("None")
			&& RoutineClaim->PerParticipantDemand == 0
			&& RoutineClaim->RequestedCount == 1000);
		TestTrue(TEXT("Wait batch keeps its complete no-resource claim"), WaitClaim != nullptr
			&& WaitClaim->ResourceScope == TEXT("None")
			&& WaitClaim->PerParticipantDemand == 0
			&& WaitClaim->RequestedCount == 600);
		TestEqual(TEXT("Routine batch stores its participant count once"), RoutineBatch->ParticipantCount, 1000);
		TestEqual(TEXT("Wait batch stores its participant count once"), WaitBatch->ParticipantCount, 600);
		TestEqual(TEXT("Routine uses the shared eight-hour duration"), RoutineBatch->EndTime.Minutes, int64(8 * MinutesPerHour));
		TestEqual(TEXT("Wait uses the shared six-hour duration"), WaitBatch->EndTime.Minutes, int64(6 * MinutesPerHour));
		TestEqual(TEXT("Routine request is fully accounted"), RoutineBatch->RequestedCount, RoutineBatch->GrantedCount + RoutineBatch->RejectedCount);
		TestEqual(TEXT("Wait request is fully accounted"), WaitBatch->RequestedCount, WaitBatch->GrantedCount + WaitBatch->RejectedCount);
	}

	const FString SubmittedDigest = RunA.BuildDeterministicDigest();
	TestEqual(TEXT("Submitted B2A state has the frozen digest"), SubmittedDigest, FString(TEXT("32477ACC7DB3B9984459EC2AE1E05C428E2398C0")));
	FString RejectionError;
	FEventID RejectedEventID = 0;
	TestFalse(TEXT("B2A rejects Work before changing state"),
		RunA.SubmitBatch(RoutineCellID, EIndividualAction::Work, 1, RejectedEventID, RejectionError));
	TestEqual(TEXT("Rejected Work creates no EventID"), RejectedEventID, FEventID(0));
	TestEqual(TEXT("Rejected Work leaves the complete state unchanged"), RunA.BuildDeterministicDigest(), SubmittedDigest);
	TestFalse(TEXT("B2A rejects a participant count larger than the source cell"),
		RunA.SubmitBatch(RoutineCellID, EIndividualAction::Wait, 1001, RejectedEventID, RejectionError));
	TestEqual(TEXT("Rejected oversized batch leaves the complete state unchanged"), RunA.BuildDeterministicDigest(), SubmittedDigest);
	TestFalse(TEXT("B2A rejects the same Routine claim twice"),
		RunA.SubmitBatch(RoutineCellID, EIndividualAction::Routine, 1000, RejectedEventID, RejectionError));
	TestEqual(TEXT("Rejected duplicate claim leaves the complete state unchanged"), RunA.BuildDeterministicDigest(), SubmittedDigest);
	TestEqual(TEXT("Rejected requests create no fallback event"), RunA.GetEventStore().GetEvents().Num(), 2);
	TestTrue(TEXT("Rejected requests leave no population or event mismatch"), RunA.BuildAudit().IsHardErrorFree());

	FV17NoResourceBatchPrototype RunB(20260810);
	if (!InitializePrototype(RunB)) return false;
	FEventID RoutineEventIDB = 0;
	FEventID WaitEventIDB = 0;
	if (!SubmitFixture(RunB, RoutineEventIDB, WaitEventIDB)) return false;
	TestEqual(TEXT("Identical submissions produce the same deterministic state"), RunB.BuildDeterministicDigest(), SubmittedDigest);

	FString AdvanceError;
	TestTrue(TEXT("Run A advances to the Wait completion"), RunA.AdvanceTo(FSimulationTime::FromHours(6), AdvanceError));
	TestTrue(TEXT("Run B advances to the Wait completion"), RunB.AdvanceTo(FSimulationTime::FromHours(6), AdvanceError));
	TestEqual(TEXT("Wait participants return to their source cell"), RunA.GetReadyCount(WaitCellID), 1000);
	TestEqual(TEXT("Routine participants remain busy at hour six"), RunA.GetPendingParticipantCount(), 1000);
	TestEqual(TEXT("Only Routine remains scheduled at hour six"), RunA.GetPendingEventCount(), 1);
	TestEqual(TEXT("Exactly one batch has completed at hour six"), RunA.GetCompletedEventCount(), 1);
	const FV17BatchPrototypeEvent* RoutineAtHourSix = RunA.GetBatchEvents().Find(RoutineEventID);
	TestTrue(TEXT("Routine batch records two hours of work remaining at hour six"),
		RoutineAtHourSix != nullptr && RoutineAtHourSix->RemainingWorkMinutes == 2 * MinutesPerHour);
	TestTrue(TEXT("Hour-six population and event counts reconcile"), RunA.BuildAudit().IsHardErrorFree());
	const FString HourSixDigest = RunA.BuildDeterministicDigest();
	TestEqual(TEXT("Hour-six B2A state has the frozen digest"), HourSixDigest, FString(TEXT("23B042A5E11D6722B45FFA39A38CFF467B1505F9")));
	TestEqual(TEXT("Hour-six replay is deterministic"), RunB.BuildDeterministicDigest(), HourSixDigest);

	TestTrue(TEXT("Run A advances to the Routine completion"), RunA.AdvanceTo(FSimulationTime::FromHours(8), AdvanceError));
	TestTrue(TEXT("Run B advances to the Routine completion"), RunB.AdvanceTo(FSimulationTime::FromHours(8), AdvanceError));
	TestEqual(TEXT("Routine participants return to their source cell"), RunA.GetReadyCount(RoutineCellID), 2000);
	TestEqual(TEXT("Wait source cell retains its full population"), RunA.GetReadyCount(WaitCellID), 1000);
	TestEqual(TEXT("No participants remain pending after both batches complete"), RunA.GetPendingParticipantCount(), 0);
	TestEqual(TEXT("No scheduled event remains after both batches complete"), RunA.GetPendingEventCount(), 0);
	TestEqual(TEXT("Two batch objects remain as completed history"), RunA.GetCompletedEventCount(), 2);
	TestEqual(TEXT("Completion does not expand batch objects per participant"), RunA.GetEventStore().GetEvents().Num(), 2);
	TestTrue(TEXT("Final population and event counts reconcile"), RunA.BuildAudit().IsHardErrorFree());
	const FString CompletedDigest = RunA.BuildDeterministicDigest();
	TestEqual(TEXT("Completed B2A state has the frozen digest"), CompletedDigest, FString(TEXT("1DDD347DFEC95E8AAA34789C40C9959D9C6DEC81")));
	TestEqual(TEXT("Completed replay is deterministic"), RunB.BuildDeterministicDigest(), CompletedDigest);

	AddInfo(FString::Printf(
		TEXT("Phase6GB2A initial=%s submitted=%s hour6=%s completed=%s events=%d weighted_participants=%lld"),
		*InitialDigest,
		*SubmittedDigest,
		*HourSixDigest,
		*CompletedDigest,
		RunA.GetEventStore().GetEvents().Num(),
		RunA.GetParticipantWeightedActionCount(EIndividualAction::Routine)
			+ RunA.GetParticipantWeightedActionCount(EIndividualAction::Wait)));
	return true;
}

#endif
