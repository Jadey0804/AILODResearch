// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODV17AuthoritativeMacro.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace AILOD;

	constexpr FV17AuthoritativeCellID MarketCellID = 0xB301;
	constexpr FV17AuthoritativeCellID ForestCellID = 0xB302;
	constexpr FV17AuthoritativeCellID RepairCellID = 0xB303;
	constexpr FV17AuthoritativeCellID RoutineCellID = 0xB304;
	constexpr FV17AuthoritativeCellID WorkCellID = 0xB305;
	constexpr FV17AuthoritativeCellID WaitCellID = 0xB306;
	constexpr FResidentID MarketActiveID = 301;
	constexpr FResidentID ForestActiveID = 302;
	constexpr FResidentID RepairActiveID = 303;

	struct FB3ClaimIDs
	{
		FV17AuthoritativeClaimID MarketMacro = 0;
		FV17AuthoritativeClaimID MarketActive = 0;
		FV17AuthoritativeClaimID ForestMacro = 0;
		FV17AuthoritativeClaimID ForestActive = 0;
		FV17AuthoritativeClaimID RepairMacro = 0;
		FV17AuthoritativeClaimID RepairActive = 0;
		FV17AuthoritativeClaimID RoutineMacro = 0;
		FV17AuthoritativeClaimID WorkMacro = 0;
		FV17AuthoritativeClaimID WaitMacro = 0;
	};

	FV17AuthoritativeJointKey MakeKey(
		const EProfession Profession,
		const EIncomeBand IncomeBand,
		const EHomeState HomeState,
		const int32 PowerBand,
		const int32 InWoodBand)
	{
		FV17AuthoritativeJointKey Key;
		Key.Kingdom = EKingdom::A;
		Key.Profession = Profession;
		Key.IncomeBand = IncomeBand;
		Key.HomeState = HomeState;
		Key.Intent = EMacroIntent::Routine;
		Key.PurchasingPowerBand = PowerBand;
		Key.WoodBand = InWoodBand;
		return Key;
	}

	bool InitializeB3Fixture(
		FAutomationTestBase& Test,
		FV17AuthoritativeMacroSession& Session)
	{
		const FV17AuthoritativeJointKey MarketKey = MakeKey(
			EProfession::Worker, EIncomeBand::Low, EHomeState::DamagedWaiting, 2, 0);
		const FV17AuthoritativeJointKey MarketActiveKey = MakeKey(
			EProfession::Worker, EIncomeBand::Low, EHomeState::DamagedWaiting, 2, 1);
		const FV17AuthoritativeJointKey ForestKey = MakeKey(
			EProfession::Logger, EIncomeBand::Low, EHomeState::DamagedWaiting, 0, 0);
		const FV17AuthoritativeJointKey RepairKey = MakeKey(
			EProfession::Worker, EIncomeBand::NonLow, EHomeState::DamagedWaiting, 0, 2);
		const TArray<FV17AuthoritativeCellConfig> Cells =
		{
			{ MarketCellID, MarketKey, 5, 50, 0, 0 },
			{ ForestCellID, ForestKey, 5, 0, 0, 0 },
			{ RepairCellID, RepairKey, 5, 0, 0, 20 },
			{ RoutineCellID, MakeKey(EProfession::Worker, EIncomeBand::Low, EHomeState::Healthy, 0, 0), 10, 0, 0, 0 },
			{ WorkCellID, MakeKey(EProfession::Worker, EIncomeBand::Low, EHomeState::DamagedWaiting, 0, 0), 10, 0, 0, 0 },
			{ WaitCellID, MakeKey(EProfession::Logger, EIncomeBand::Low, EHomeState::Healthy, 0, 0), 10, 0, 0, 0 }
		};
		const TArray<FV17AuthoritativeActiveConfig> ActiveResidents =
		{
			{ MarketActiveID, MarketCellID, MarketActiveKey, 10, 0, 3 },
			{ ForestActiveID, ForestCellID, ForestKey, 0, 0, 0 },
			{ RepairActiveID, RepairCellID, RepairKey, 0, 0, 4 }
		};
		const TArray<FV17AuthoritativeKingdomConfig> Kingdoms =
		{
			{ EKingdom::A, 12, 12, 12, 0, 0, 0, 3, 1.0 }
		};
		FString Error;
		if (!Session.Initialize(Cells, ActiveResidents, Kingdoms, FSimulationTime::FromHours(0), Error))
		{
			Test.AddError(FString::Printf(TEXT("B3 authoritative fixture initialization failed: %s"), *Error));
			return false;
		}
		return true;
	}

	bool QueueB3Fixture(
		FAutomationTestBase& Test,
		FV17AuthoritativeMacroSession& Session,
		FB3ClaimIDs& OutIDs,
		const bool bReverseOrder)
	{
		struct FQueueRequest
		{
			bool bActive = false;
			uint64 SourceID = 0;
			EIndividualAction Action = EIndividualAction::None;
			int32 Count = 0;
			FV17AuthoritativeClaimID* OutClaimID = nullptr;
		};
		TArray<FQueueRequest> Requests =
		{
			{ false, MarketCellID, EIndividualAction::BuyWood, 5, &OutIDs.MarketMacro },
			{ true, MarketActiveID, EIndividualAction::BuyWood, 1, &OutIDs.MarketActive },
			{ false, ForestCellID, EIndividualAction::ChopWood, 5, &OutIDs.ForestMacro },
			{ true, ForestActiveID, EIndividualAction::ChopWood, 1, &OutIDs.ForestActive },
			{ false, RepairCellID, EIndividualAction::StartRepair, 5, &OutIDs.RepairMacro },
			{ true, RepairActiveID, EIndividualAction::StartRepair, 1, &OutIDs.RepairActive },
			{ false, RoutineCellID, EIndividualAction::Routine, 10, &OutIDs.RoutineMacro },
			{ false, WorkCellID, EIndividualAction::Work, 10, &OutIDs.WorkMacro },
			{ false, WaitCellID, EIndividualAction::Wait, 10, &OutIDs.WaitMacro }
		};
		if (bReverseOrder)
		{
			Algo::Reverse(Requests);
		}

		for (const FQueueRequest& Request : Requests)
		{
			FString Error;
			const bool bQueued = Request.bActive
				? Session.QueueActiveAction(
					static_cast<FResidentID>(Request.SourceID), Request.Action, 0, *Request.OutClaimID, Error)
				: Session.QueueMacroAction(
					Request.SourceID, Request.Action, Request.Count, 0, *Request.OutClaimID, Error);
			if (!bQueued)
			{
				Test.AddError(FString::Printf(TEXT("B3 action Flow queue failed: %s"), *Error));
				return false;
			}
		}
		return true;
	}

	int32 Granted(
		const FV17AuthoritativeMacroSession& Session,
		const FV17AuthoritativeClaimID Left,
		const FV17AuthoritativeClaimID Right)
	{
		return Session.GetClaimGrantedCount(Left) + Session.GetClaimGrantedCount(Right);
	}

	int32 Rejected(
		const FV17AuthoritativeMacroSession& Session,
		const FV17AuthoritativeClaimID Left,
		const FV17AuthoritativeClaimID Right)
	{
		return Session.GetClaimRejectedCount(Left) + Session.GetClaimRejectedCount(Right);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6GB3AuthoritativeResourceBatchTest,
	"AILODResearch.Phase6G.V17AuthoritativeResourceBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6GB3AuthoritativeResourceBatchTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	FV17AuthoritativeMacroSession RunA(20260810);
	if (!InitializeB3Fixture(*this, RunA)) return false;
	const FString InitialDigest = RunA.BuildDeterministicDigest();
	TestEqual(TEXT("Initial B3 authority has the frozen digest"), InitialDigest,
		FString(TEXT("99A149ED3562B40F5B50753F8901503F2AFA2B9F")));
	FB3ClaimIDs IDsA;
	if (!QueueB3Fixture(*this, RunA, IDsA, false)) return false;
	const FString QueuedDigest = RunA.BuildDeterministicDigest();
	TestEqual(TEXT("Queued B3 authority has the frozen digest"), QueuedDigest,
		FString(TEXT("0E04957B7078C06BC7C05678E07CEF1A0ED5F70E")));

	FV17AuthoritativeMacroSession RunB(20260810);
	if (!InitializeB3Fixture(*this, RunB)) return false;
	FB3ClaimIDs IDsB;
	if (!QueueB3Fixture(*this, RunB, IDsB, true)) return false;
	TestEqual(TEXT("Reversing Flow arrival order leaves the queued state identical"), RunB.BuildDeterministicDigest(), QueuedDigest);

	FString Error;
	TestTrue(TEXT("Run A commits all same-hour Claims as one atomic operation"), RunA.ResolveAndCommitClaims(Error));
	TestTrue(TEXT("Run B commits all same-hour Claims in stable order"), RunB.ResolveAndCommitClaims(Error));
	const FString CommittedDigest = RunA.BuildDeterministicDigest();
	TestEqual(TEXT("Committed B3 authority has the frozen digest"), CommittedDigest,
		FString(TEXT("74736CFA1DD97E9B989481935E7D5FB2AF281DD1")));
	TestEqual(TEXT("Reversing Flow arrival order leaves allocation and commit identical"), RunB.BuildDeterministicDigest(), CommittedDigest);

	TestEqual(TEXT("Market gives exactly three complete four-Wood purchases"), Granted(RunA, IDsA.MarketMacro, IDsA.MarketActive), 3);
	TestEqual(TEXT("Market sends exactly three rejected participants to integer Wait Flows"), Rejected(RunA, IDsA.MarketMacro, IDsA.MarketActive), 3);
	TestEqual(TEXT("The frozen remainder rule grants three Macro Market participants"), RunA.GetClaimGrantedCount(IDsA.MarketMacro), 3);
	TestEqual(TEXT("The Count=1 Market participant is not given a reserved Active slot"), RunA.GetClaimGrantedCount(IDsA.MarketActive), 0);
	TestEqual(TEXT("Forest gives exactly three complete four-Wood harvests"), Granted(RunA, IDsA.ForestMacro, IDsA.ForestActive), 3);
	TestEqual(TEXT("Forest sends exactly three rejected participants to integer Wait Flows"), Rejected(RunA, IDsA.ForestMacro, IDsA.ForestActive), 3);
	TestEqual(TEXT("Repair capacity starts exactly three complete repairs"), Granted(RunA, IDsA.RepairMacro, IDsA.RepairActive), 3);
	TestEqual(TEXT("Repair capacity sends exactly three rejected participants to integer Wait Flows"), Rejected(RunA, IDsA.RepairMacro, IDsA.RepairActive), 3);
	TestEqual(TEXT("All original Claims account for requested = granted + rejected"),
		RunA.BuildAudit().BatchRequestedGrantResidualCount, 0);
	TestEqual(TEXT("Macro Market Claim uses its real four-Wood unit demand"),
		RunA.GetClaims().FindChecked(IDsA.MarketMacro).PerParticipantDemand, 4);
	TestEqual(TEXT("Count=1 Market Claim keeps its different one-Wood unit demand"),
		RunA.GetClaims().FindChecked(IDsA.MarketActive).PerParticipantDemand, 1);
	const int32 ActiveScarceGrants = RunA.GetClaimGrantedCount(IDsA.MarketActive)
		+ RunA.GetClaimGrantedCount(IDsA.ForestActive)
		+ RunA.GetClaimGrantedCount(IDsA.RepairActive);
	TestTrue(TEXT("Count=1 Active requests are not automatically guaranteed all scarce resources"), ActiveScarceGrants < 3);

	TestEqual(TEXT("All 48 participants are represented by pending batch work"), RunA.GetPendingParticipantCount(), 48);
	TestEqual(TEXT("Forty-eight participants create only twelve event objects"), RunA.GetBatchEvents().Num(), 12);
	TestEqual(TEXT("The scheduler also keeps only twelve batch entries"), RunA.GetScheduler().NumPending(), 12);
	TestEqual(TEXT("Routine keeps ten participant-weighted actions"), RunA.GetActionParticipantCount(EIndividualAction::Routine), 10);
	TestEqual(TEXT("Work keeps ten participant-weighted actions"), RunA.GetActionParticipantCount(EIndividualAction::Work), 10);
	TestEqual(TEXT("Rejected scarce-resource requests add nine Wait participants"), RunA.GetActionParticipantCount(EIndividualAction::Wait), 19);
	TestEqual(TEXT("BuyWood keeps three participant-weighted actions"), RunA.GetActionParticipantCount(EIndividualAction::BuyWood), 3);
	TestEqual(TEXT("ChopWood keeps three participant-weighted actions"), RunA.GetActionParticipantCount(EIndividualAction::ChopWood), 3);
	TestEqual(TEXT("Repair keeps three participant-weighted continuation actions"), RunA.GetActionParticipantCount(EIndividualAction::ContinueRepair), 3);
	TestEqual(TEXT("Market Wood is reserved once per granted Claim, not once per person"),
		RunA.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("MarketWoodAvailable")), int64(0));
	TestEqual(TEXT("Forest Wood is reserved once per granted Claim, not once per person"),
		RunA.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("ForestWood")), int64(0));
	TestEqual(TEXT("Three repair starts move exactly twelve Wood into repair work"),
		RunA.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs")), int64(12));
	TestEqual(TEXT("Repair capacity is consumed by granted participants only"), RunA.GetRepairCapacityRemaining(EKingdom::A), 0);
	TestEqual(TEXT("Daily harvest allowance is consumed by granted participants only"), RunA.GetHarvestRemaining(EKingdom::A), int64(0));
	TestEqual(TEXT("Three purchases pay twelve integer Coin at commit"),
		RunA.GetKingdomBalance(EKingdom::A, ESimulationResource::Coin, TEXT("MarketCoin")), int64(12));
	TestTrue(TEXT("Committed authority has no population, resource, event or reservation mismatch"), RunA.BuildAudit().IsHardErrorFree());

	FV17AuthoritativeMacroSession FailureRun(20260810);
	if (!InitializeB3Fixture(*this, FailureRun)) return false;
	FB3ClaimIDs FailureIDs;
	if (!QueueB3Fixture(*this, FailureRun, FailureIDs, false)) return false;
	const FString BeforeFailedCommit = FailureRun.BuildDeterministicDigest();
	TestFalse(TEXT("Injected failure stops the multi-Claim commit"), FailureRun.ResolveAndCommitClaims(
		Error, EV17AuthoritativeFailurePoint::AfterFirstCommittedClaim));
	TestTrue(TEXT("Injected commit failure reports the exact checkpoint"), Error.Contains(TEXT("first complete Batch Claim")));
	TestEqual(TEXT("Failed commit restores Joint Cells, Ledger, Events, Scheduler and Reservations"),
		FailureRun.BuildDeterministicDigest(), BeforeFailedCommit);
	TestEqual(TEXT("Failed commit leaves no event object"), FailureRun.GetBatchEvents().Num(), 0);
	TestEqual(TEXT("Failed commit leaves no Ledger transaction"), FailureRun.GetLedger().GetTransactions().Num(), 0);
	TestEqual(TEXT("Failed commit leaves no Reservation"), FailureRun.GetReservations().GetReservations().Num(), 0);
	TestTrue(TEXT("The same queued Claims can be retried after rollback"), FailureRun.ResolveAndCommitClaims(Error));
	TestEqual(TEXT("Retry after commit rollback reaches the normal committed state"),
		FailureRun.BuildDeterministicDigest(), CommittedDigest);

	const FString BeforeFailedCompletion = RunA.BuildDeterministicDigest();
	TestFalse(TEXT("Injected failure stops completion after a resource write"), RunA.AdvanceTo(
		FSimulationTime::FromHours(48), Error, EV17AuthoritativeFailurePoint::AfterFirstCompletionResourceWrite));
	TestTrue(TEXT("Injected completion failure reports the exact checkpoint"), Error.Contains(TEXT("completion resource write")));
	TestEqual(TEXT("Failed completion restores time, resources, Reservations, Events and Scheduler"),
		RunA.BuildDeterministicDigest(), BeforeFailedCompletion);
	TestTrue(TEXT("Completion retry succeeds through hour 48"), RunA.AdvanceTo(FSimulationTime::FromHours(48), Error));
	TestTrue(TEXT("Run B completes the same authoritative session"), RunB.AdvanceTo(FSimulationTime::FromHours(48), Error));
	const FString CompletedDigest = RunA.BuildDeterministicDigest();
	TestEqual(TEXT("Completed B3 authority has the frozen digest"), CompletedDigest,
		FString(TEXT("1E9E92F077220DF3A44DFB8B7BF2E866D873F342")));
	TestEqual(TEXT("Completion replay remains deterministic"), RunB.BuildDeterministicDigest(), CompletedDigest);
	TestEqual(TEXT("No participant remains busy after all actions finish"), RunA.GetPendingParticipantCount(), 0);
	TestEqual(TEXT("No scheduled batch remains after all actions finish"), RunA.GetScheduler().NumPending(), 0);
	TestTrue(TEXT("All three pre-seeded Active residents become ready again"),
		RunA.IsActiveReady(MarketActiveID) && RunA.IsActiveReady(ForestActiveID) && RunA.IsActiveReady(RepairActiveID));
	TestEqual(TEXT("All reserved Market Wood is delivered"),
		RunA.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("MarketWoodReserved")), int64(0));
	TestEqual(TEXT("All reserved Forest Wood is delivered"),
		RunA.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("ForestWoodReserved")), int64(0));
	TestEqual(TEXT("No repair Wood remains stuck after hour 48"),
		RunA.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs")), int64(0));
	TestEqual(TEXT("Three completed homes hold exactly twelve repair Wood"),
		RunA.GetKingdomBalance(EKingdom::A, ESimulationResource::Wood, TEXT("WoodInRepairedHomes")), int64(12));
	TestEqual(TEXT("A new game day restores the configured repair capacity"), RunA.GetRepairCapacityRemaining(EKingdom::A), 3);
	TestEqual(TEXT("A new game day restores the configured harvest allowance"), RunA.GetHarvestRemaining(EKingdom::A), int64(12));
	TestTrue(TEXT("Completed authority has no population, resource, event or reservation mismatch"),
		RunA.BuildAudit().IsHardErrorFree());

	AddInfo(FString::Printf(
		TEXT("Phase6GB3 initial=%s queued=%s committed=%s completed=%s active_grants=%d events=%d transactions=%d reservations=%d"),
		*InitialDigest,
		*QueuedDigest,
		*CommittedDigest,
		*CompletedDigest,
		ActiveScarceGrants,
		RunA.GetBatchEvents().Num(),
		RunA.GetLedger().GetTransactions().Num(),
		RunA.GetReservations().GetReservations().Num()));
	return true;
}

#endif
