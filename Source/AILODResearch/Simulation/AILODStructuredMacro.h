// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODPhase0Manifest.h"
#include "AILODSimulationCore.h"

namespace AILOD
{
	enum class EStage2Scenario : uint8
	{
		None,
		HarvestCap,
		StateImport,
		RepairAid
	};

	const TCHAR* ToString(EStage2Scenario Scenario);

	struct FCohortKey
	{
		EKingdom Kingdom = EKingdom::A;
		EProfession Profession = EProfession::Worker;
		EIncomeBand IncomeBand = EIncomeBand::Low;
		EHomeState HomeState = EHomeState::Healthy;
		EMacroIntent MacroIntent = EMacroIntent::Routine;

		bool operator==(const FCohortKey& Other) const
		{
			return Kingdom == Other.Kingdom
				&& Profession == Other.Profession
				&& IncomeBand == Other.IncomeBand
				&& HomeState == Other.HomeState
				&& MacroIntent == Other.MacroIntent;
		}
	};

	FORCEINLINE uint32 GetTypeHash(const FCohortKey& Key)
	{
		uint32 Hash = ::GetTypeHash(static_cast<uint8>(Key.Kingdom));
		Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.Profession)));
		Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.IncomeBand)));
		Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.HomeState)));
		return HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.MacroIntent)));
	}

	struct FCohortBucket
	{
		int32 PopulationCount = 0;
		int64 CashSum = 0;
		int64 CashSquaredSum = 0;
		int64 RepairCreditSum = 0;
		int32 WoodCounts[5] = {};
		int32 AidEligibleCount = 0;
		int32 AidReceivedCount = 0;
		int32 RepairProgressBins[4] = {};
		TArray<FEventID> EventBatchRefs;
		FSimulationTime LastUpdateTime;
		double ResidualFlows[6] = {};
		uint32 RNGStreamKey = 0;
	};

	struct FKingdomStocks
	{
		double ForestCapacity = 0.0;
		double ForestWood = 0.0;
		double MarketWoodAvailable = 0.0;
		double MarketWoodReserved = 0.0;
		double WoodInTransit = 0.0;
		double ResidentInventoryWood = 0.0;
		double WoodEmbeddedInRepairs = 0.0;
		double WoodInRepairedHomes = 0.0;
		int64 TreasuryAvailable = 0;
		int64 TreasuryReserved = 0;
		int64 MarketCoin = 0;
		int64 ResidentRepairCredit = 0;
		double WoodPrice = 1.0;
	};

	struct FKingdomState
	{
		EKingdom Kingdom = EKingdom::A;
		int32 Population = 0;
		TMap<FCohortKey, FCohortBucket> Cohorts;
		FKingdomStocks Stocks;
		double UnmetRoutineConsumption = 0.0;
		double AdditionalImportedWood = 0.0;
		double CommercialHarvestedWood = 0.0;
		int32 AidPaidCount = 0;
	};

	struct FKingdomSnapshot
	{
		FSimulationTime GameTime;
		EKingdom Kingdom = EKingdom::A;
		FKingdomStocks Stocks;
		int32 Healthy = 0;
		int32 DamagedWaiting = 0;
		int32 UnderRepair = 0;
		int32 Repaired = 0;
		int32 LedgerTransactionCount = 0;
	};

	struct FStage2RunResult
	{
		int32 Seed = 0;
		FString ConfigHash;
		EStage2Scenario Scenario = EStage2Scenario::None;
		FSimulationTime FinalTime;
		int32 WarmupHourSteps = 0;
		int32 FormalHourSteps = 0;
		FKingdomState KingdomA;
		FKingdomState KingdomB;
		FConservationAudit Audit;
		TMap<FResourceAccountKey, double> InitialBalances;
		TMap<FResourceAccountKey, double> FinalBalances;
		TArray<FLedgerTransaction> Transactions;
		TArray<FSimulationEventRecord> Events;
		TArray<FKingdomSnapshot> Snapshots;
	};

	int32 GetHomeStateCount(const FKingdomState& Kingdom, EHomeState HomeState);
	int32 GetCohortPopulation(const FStage2RunResult& Result);
	FString MakeKingdomAccount(EKingdom Kingdom, const TCHAR* StockName);

	class FStructuredMacroRunner
	{
	public:
		static bool Run(
			const FPhase0Config& Config,
			EStage2Scenario Scenario,
			FStage2RunResult& OutResult,
			FString& OutError);

		static FString SerializeLedgerTrace(
			const FStage2RunResult& Result,
			const FString& ExperimentID,
			const FString& RunID);

		static FString BuildDeterministicDigest(const FStage2RunResult& Result);
	};
}
