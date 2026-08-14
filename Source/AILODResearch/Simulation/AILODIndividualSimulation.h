// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODStructuredMacro.h"

namespace AILOD
{
	enum class EIndividualGoal : uint8
	{
		RestoreHome,
		RoutineLife
	};

	enum class EIndividualAction : uint8
	{
		None,
		Routine,
		Work,
		BuyWood,
		ChopWood,
		StartRepair,
		ContinueRepair,
		Wait
	};

	const TCHAR* ToString(EIndividualGoal Goal);
	const TCHAR* ToString(EIndividualAction Action);

	struct FIndividualWorldFacts
	{
		double MarketWoodAvailable = 0.0;
		double ForestWood = 0.0;
		double HarvestAllowance = 0.0;
		double WoodPrice = 1.0;
	};

	struct FIndividualPlan
	{
		EIndividualGoal Goal = EIndividualGoal::RoutineLife;
		TArray<EIndividualAction> Actions;
	};

	struct FOracleResidentState
	{
		FResidentID ResidentID = 0;
		EKingdom Kingdom = EKingdom::A;
		EProfession Profession = EProfession::Worker;
		EIncomeBand IncomeBand = EIncomeBand::Low;
		int32 Cash = 0;
		int32 RepairCredit = 0;
		int32 InventoryWood = 0;
		EHomeState HomeState = EHomeState::Healthy;
		EIndividualGoal CurrentGoal = EIndividualGoal::RoutineLife;
		EIndividualAction CurrentAction = EIndividualAction::None;
		EIndividualAction LastCompletedAction = EIndividualAction::None;
		FEventID ActiveEventID = 0;
		FArriveID ActiveArriveID = 0;
		FReservationID ActiveReservationID = 0;
		FSimulationTime ActionEndTime;
		bool bAidReceived = false;
	};

	struct FIndividualActionTrace
	{
		FSimulationTime GameTime;
		FResidentID ResidentID = 0;
		EIndividualGoal Goal = EIndividualGoal::RoutineLife;
		EIndividualAction Action = EIndividualAction::None;
		FEventID EventID = 0;
		FArriveID ArriveID = 0;
		bool bStarted = false;
	};

	struct FStage3OracleRunResult
	{
		int32 Seed = 0;
		FString ConfigHash;
		EStage2Scenario Scenario = EStage2Scenario::None;
		FSimulationTime FinalTime;
		int32 WarmupHourSteps = 0;
		int32 FormalHourSteps = 0;
		TArray<FOracleResidentState> Residents;
		FKingdomStocks KingdomAStocks;
		FKingdomStocks KingdomBStocks;
		FConservationAudit Audit;
		double CoinResidual = 0.0;
		TMap<FResourceAccountKey, double> InitialBalances;
		TMap<FResourceAccountKey, double> FinalBalances;
		TArray<FLedgerTransaction> Transactions;
		TArray<FSimulationEventRecord> Events;
		TArray<FIndividualActionTrace> ActionTrace;
		TArray<FKingdomSnapshot> Snapshots;
		int32 AidPaidCount = 0;
		double AdditionalImportedWood = 0.0;

		int32 GetActionCount(EIndividualAction Action, bool bStartedOnly = true) const;
		int32 GetHomeStateCount(EKingdom Kingdom, EHomeState HomeState) const;
	};

	class FIndividualDomain
	{
	public:
		static EIndividualGoal SelectGoal(const FOracleResidentState& Resident);
		static FIndividualPlan BuildPlan(
			const FOracleResidentState& Resident,
			const FIndividualWorldFacts& World);
	};

	class FIndividualOracleRunner
	{
	public:
		static bool Run(
			const FPhase0Config& Config,
			EStage2Scenario Scenario,
			FStage3OracleRunResult& OutResult,
			FString& OutError);

		static FString SerializeLedgerTrace(
			const FStage3OracleRunResult& Result,
			const FString& ExperimentID,
			const FString& RunID);

		static FString BuildDeterministicDigest(const FStage3OracleRunResult& Result);
	};
}
