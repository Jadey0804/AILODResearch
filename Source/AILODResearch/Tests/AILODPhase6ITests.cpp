// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "../Simulation/AILODV17AuthoritativeMacro.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6IH6ExactHomeLiftTest,
	"AILODResearch.Phase6I.ExactHomeLift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6IH6ExactHomeLiftTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	constexpr FV17AuthoritativeCellID HealthyCellID = 0x6190;
	FV17AuthoritativeJointKey HealthyKey;
	HealthyKey.Kingdom = EKingdom::A;
	HealthyKey.Profession = EProfession::Worker;
	HealthyKey.IncomeBand = EIncomeBand::Low;
	HealthyKey.HomeState = EHomeState::Healthy;
	HealthyKey.Intent = EMacroIntent::Routine;
	HealthyKey.PurchasingPowerBand = 0;
	HealthyKey.WoodBand = 0;
	const TArray<FV17AuthoritativeCellConfig> Cells =
	{
		{ HealthyCellID, HealthyKey, 4, 8, 0, 0 }
	};
	TArray<FV17IdentityRecord> Identities;
	for (FResidentID ResidentID = 1; ResidentID <= 4; ++ResidentID)
	{
		FV17IdentityRecord Identity;
		Identity.ResidentID = ResidentID;
		Identity.PersistentID = 610000 + ResidentID;
		Identity.HomeID = 620000 + ResidentID;
		Identity.InitialKingdom = EKingdom::A;
		Identity.Profession = EProfession::Worker;
		Identity.IncomeBand = EIncomeBand::Low;
		Identities.Add(Identity);
	}
	const TArray<FV17AuthoritativeKingdomConfig> Kingdoms =
	{
		{ EKingdom::A, 0, 0, 0, 0, 0, 0, 0, 1.0 }
	};

	auto Run = [this, &Cells, &Identities, &Kingdoms, HealthyKey](FV17AuthoritativeMacroSession& Session)
	{
		FString Error;
		if (!Session.InitializeWithIdentity(
			Cells, Identities, Kingdoms, FSimulationTime::FromHours(0), Error))
		{
			AddError(FString::Printf(TEXT("H6 exact-home fixture initialization failed: %s"), *Error));
			return false;
		}
		FV17AuthoritativeJointKey DamagedKey = HealthyKey;
		DamagedKey.HomeState = EHomeState::DamagedWaiting;
		DamagedKey.Intent = EMacroIntent::Wait;
		FV17AuthoritativeCellID DamagedCellID = 0;
		if (!Session.MoveJointCellParticipants(
			HealthyCellID,
			DamagedKey,
			2,
			4,
			0,
			0,
			TEXT("H6-EARTHQUAKE"),
			0,
			DamagedCellID,
			Error)
			|| !Session.ApplyEarthquakeHomeDamage({ 2, 4 }, Error))
		{
			AddError(FString::Printf(TEXT("H6 exact earthquake assignment failed: %s"), *Error));
			return false;
		}

		for (const FResidentID ResidentID : { FResidentID(1), FResidentID(2), FResidentID(3), FResidentID(4) })
		{
			if (!Session.LiftResident(ResidentID, FSimulationTime::FromHours(0), Error))
			{
				AddError(FString::Printf(TEXT("H6 exact Lift failed for resident %lld: %s"), ResidentID, *Error));
				return false;
			}
			FIndividualActionState State;
			EIndividualAction Action = EIndividualAction::None;
			FEventID EventID = 0;
			Session.GetActiveSnapshot(ResidentID, State, Action, EventID);
			const EHomeState Expected = ResidentID == 2 || ResidentID == 4
				? EHomeState::DamagedWaiting
				: EHomeState::Healthy;
			TestEqual(
				*FString::Printf(TEXT("Resident %lld Lift reads the exact HomeID state"), ResidentID),
				State.HomeState,
				Expected);
		}
		TestTrue(TEXT("Exact HomeID Lift keeps population and housing totals aligned"),
			Session.BuildAudit().IsHardErrorFree());
		for (const FResidentID ResidentID : { FResidentID(1), FResidentID(2), FResidentID(3), FResidentID(4) })
		{
			if (!Session.RestrictResident(ResidentID, FSimulationTime::FromHours(0), Error)) return false;
		}
		TestTrue(TEXT("Exact HomeID Restrict writes back without housing residue"),
			Session.BuildAudit().IsHardErrorFree());
		TestTrue(TEXT("Home Continuity has its own tracked memory"),
			Session.BuildTrackedMemory().HomeContinuityBytes > 0);
		return true;
	};

	FV17AuthoritativeMacroSession RunA(20260821);
	FV17AuthoritativeMacroSession RunB(20260821);
	if (!Run(RunA) || !Run(RunB)) return false;
	TestEqual(TEXT("The same exact housing trace has the same digest"),
		RunA.BuildDeterministicDigest(), RunB.BuildDeterministicDigest());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6IH6ActiveBatchRemainderTest,
	"AILODResearch.Phase6I.ActiveBatchRemainder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6IH6ActiveBatchRemainderTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	constexpr FV17AuthoritativeCellID CellID = 0x6191;
	FV17AuthoritativeJointKey Key;
	Key.Kingdom = EKingdom::A;
	Key.Profession = EProfession::Worker;
	Key.IncomeBand = EIncomeBand::Low;
	Key.HomeState = EHomeState::Healthy;
	Key.Intent = EMacroIntent::Routine;
	Key.PurchasingPowerBand = 0;
	Key.WoodBand = 0;
	const TArray<FV17AuthoritativeCellConfig> Cells =
	{
		{ CellID, Key, 3, 2, 0, 0 }
	};
	TArray<FV17IdentityRecord> Identities;
	for (FResidentID ResidentID = 1; ResidentID <= 3; ++ResidentID)
	{
		FV17IdentityRecord Identity;
		Identity.ResidentID = ResidentID;
		Identity.PersistentID = 611000 + ResidentID;
		Identity.HomeID = 621000 + ResidentID;
		Identity.InitialKingdom = EKingdom::A;
		Identity.Profession = EProfession::Worker;
		Identity.IncomeBand = EIncomeBand::Low;
		Identities.Add(Identity);
	}
	const TArray<FV17AuthoritativeKingdomConfig> Kingdoms =
	{
		{ EKingdom::A, 0, 0, 0, 0, 0, 0, 0, 1.0 }
	};

	FV17AuthoritativeMacroSession Session(20260821);
	FString Error;
	FV17AuthoritativeClaimID ClaimID = 0;
	if (!Session.InitializeWithIdentity(
		Cells, Identities, Kingdoms, FSimulationTime::FromHours(0), Error))
	{
		AddError(FString::Printf(TEXT("H6 split-remainder fixture setup failed: %s"), *Error));
		return false;
	}
	Session.EnableExactAggregateResourceSplits();
	if (!Session.QueueMacroAction(
			CellID, EIndividualAction::Wait, 3, 0, ClaimID, Error)
		|| !Session.ResolveAndCommitClaims(Error))
	{
		AddError(FString::Printf(TEXT("H6 split-remainder fixture setup failed: %s"), *Error));
		return false;
	}
	for (FResidentID ResidentID = 1; ResidentID <= 3; ++ResidentID)
	{
		if (!Session.LiftResident(ResidentID, FSimulationTime::FromHours(0), Error))
		{
			AddError(FString::Printf(TEXT("H6 pending-event Lift failed for resident %lld: %s"), ResidentID, *Error));
			return false;
		}
	}
	if (!Session.AdvanceTo(FSimulationTime::FromHours(6), Error))
	{
		AddError(FString::Printf(TEXT("H6 split-remainder completion failed: %s"), *Error));
		return false;
	}

	int32 TotalCash = 0;
	for (FResidentID ResidentID = 1; ResidentID <= 3; ++ResidentID)
	{
		FIndividualActionState State;
		EIndividualAction Action = EIndividualAction::None;
		FEventID EventID = 0;
		if (!Session.GetActiveSnapshot(ResidentID, State, Action, EventID)) return false;
		TotalCash += State.Cash;
		TestEqual(
			*FString::Printf(TEXT("Resident %lld completed the split Wait"), ResidentID),
			Action,
			EIndividualAction::None);
	}
	TestEqual(TEXT("Split residents keep the two real coins after completion"), TotalCash, 2);
	TestTrue(TEXT("Split residents' displayed state still matches their real accounts"),
		Session.BuildAudit().IsHardErrorFree());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase6IH6RepairedActiveRestrictTest,
	"AILODResearch.Phase6I.RepairedActiveRestrict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase6IH6RepairedActiveRestrictTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	constexpr FV17AuthoritativeCellID DamagedCellID = 0x6192;
	FV17AuthoritativeJointKey Key;
	Key.Kingdom = EKingdom::A;
	Key.Profession = EProfession::Worker;
	Key.IncomeBand = EIncomeBand::Low;
	Key.HomeState = EHomeState::DamagedWaiting;
	Key.Intent = EMacroIntent::Wait;
	Key.PurchasingPowerBand = 0;
	Key.WoodBand = 2;
	const TArray<FV17AuthoritativeCellConfig> Cells =
	{
		{ DamagedCellID, Key, 1, 0, 0, 4 }
	};
	FV17IdentityRecord Identity;
	Identity.ResidentID = 1;
	Identity.PersistentID = 612001;
	Identity.HomeID = 622001;
	Identity.InitialKingdom = EKingdom::A;
	Identity.Profession = EProfession::Worker;
	Identity.IncomeBand = EIncomeBand::Low;
	const TArray<FV17IdentityRecord> Identities = { Identity };
	const TArray<FV17AuthoritativeKingdomConfig> Kingdoms =
	{
		{ EKingdom::A, 0, 0, 0, 0, 0, 0, 1, 1.0 }
	};

	FV17AuthoritativeMacroSession Session(20260821);
	FString Error;
	if (!Session.InitializeWithIdentity(
		Cells, Identities, Kingdoms, FSimulationTime::FromHours(0), Error))
	{
		AddError(FString::Printf(TEXT("H6 active-repair fixture initialization failed: %s"), *Error));
		return false;
	}
	Session.EnableExactAggregateResourceSplits();
	FV17AuthoritativeClaimID RepairClaimID = 0;
	if (!Session.LiftResident(1, FSimulationTime::FromHours(0), Error)
		|| !Session.QueueActiveAction(1, EIndividualAction::StartRepair, 0, RepairClaimID, Error)
		|| !Session.ResolveAndCommitClaims(Error)
		|| !Session.AdvanceTo(FSimulationTime::FromHours(48), Error))
	{
		AddError(FString::Printf(TEXT("H6 nearby repair failed: %s"), *Error));
		return false;
	}
	EHomeState HomeState = EHomeState::DamagedWaiting;
	TestTrue(TEXT("The nearby resident's exact home still exists after repair"),
		Session.GetResidentHomeState(1, HomeState));
	TestEqual(TEXT("The nearby resident's exact home is repaired"), HomeState, EHomeState::Repaired);

	FV17AuthoritativeClaimID WaitClaimID = 0;
	if (!Session.QueueActiveAction(1, EIndividualAction::Wait, 0, WaitClaimID, Error)
		|| !Session.ResolveAndCommitClaims(Error)
		|| !Session.RestrictResident(1, FSimulationTime::FromHours(48), Error))
	{
		AddError(FString::Printf(TEXT("H6 repaired resident could not return to a batch: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("A repaired resident returns to a repaired batch, not the old damaged batch"),
		Session.BuildAudit().IsHardErrorFree());
	return true;
}

#endif
