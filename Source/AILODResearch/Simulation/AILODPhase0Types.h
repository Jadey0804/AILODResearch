// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace AILOD
{
	inline constexpr TCHAR SpecVersion[] = TEXT("1.3");
	inline constexpr TCHAR SchemaVersion[] = TEXT("1.1");
	inline constexpr int32 PersistentPoolSize = 20;
	inline constexpr int32 Day7And30PersistentCount = 10;

	using FResidentID = int64;
	using FHomeID = int64;
	using FPersistentID = int64;
	using FEventID = int64;
	using FArriveID = int64;
	using FTransactionID = int64;
	using FReservationID = int64;
	using FPolicyID = int64;
	using FIdempotencyKey = FString;

	namespace RandomStreams
	{
		inline constexpr uint32 PopulationComposition = 0x504F5001u;
		inline constexpr uint32 InitialCash = 0xCA511001u;
		inline constexpr uint32 EarthquakeDamage = 0xDA6A6E01u;
		inline constexpr uint32 PersistentSelection = 0x50455201u;
	}

	enum class EKingdom : uint8
	{
		A,
		B
	};

	enum class EProfession : uint8
	{
		Logger,
		Worker
	};

	enum class EIncomeBand : uint8
	{
		Low,
		NonLow
	};

	enum class EHomeState : uint8
	{
		Healthy,
		DamagedWaiting,
		UnderRepair,
		Repaired
	};

	enum class EMacroIntent : uint8
	{
		Routine,
		Work,
		BuyWood,
		ChopWood,
		Repair,
		Wait
	};

	struct FPhase0Config
	{
		int32 Seed = 20260810;
		int32 PopulationPerKingdom = 100;
	};

	struct FInitialResidentRecord
	{
		FResidentID ResidentID = 0;
		FHomeID HomeID = 0;
		FPersistentID PersistentID = 0;
		FString Name;
		EKingdom Kingdom = EKingdom::A;
		EProfession Profession = EProfession::Worker;
		EIncomeBand IncomeBand = EIncomeBand::Low;
		int32 Cash = 0;
		int32 RepairCredit = 0;
		int32 InventoryWood = 0;
		EHomeState HomeState = EHomeState::Healthy;
		FEventID EventID = 0;
		FArriveID ArriveID = 0;
	};

	struct FEarthquakeDamageRecord
	{
		FResidentID ResidentID = 0;
		FHomeID HomeID = 0;
		EProfession Profession = EProfession::Worker;
		EIncomeBand IncomeBand = EIncomeBand::Low;
	};

	struct FInitialPopulationManifest
	{
		int32 Seed = 0;
		int32 PopulationPerKingdom = 0;
		FString ConfigHash;
		TArray<FInitialResidentRecord> Residents;
	};

	struct FEarthquakeDamageList
	{
		int32 Seed = 0;
		FString ConfigHash;
		TArray<FEarthquakeDamageRecord> DamagedResidents;
	};

	struct FPersistentTestRecord
	{
		FResidentID ResidentID = 0;
		FHomeID HomeID = 0;
		FPersistentID PersistentID = 0;
		FString Name;
		EKingdom Kingdom = EKingdom::A;
		EProfession Profession = EProfession::Worker;
		EIncomeBand IncomeBand = EIncomeBand::Low;
		bool bDay7 = false;
		bool bDay30 = false;
		bool bDay45 = true;
	};

	struct FPersistentTestPool
	{
		int32 Seed = 0;
		FString ConfigHash;
		TArray<FPersistentTestRecord> Residents;
	};
}
