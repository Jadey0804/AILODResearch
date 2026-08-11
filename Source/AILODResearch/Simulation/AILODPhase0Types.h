// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace AILOD
{
	inline constexpr TCHAR SpecVersion[] = TEXT("1.2");
	inline constexpr TCHAR SchemaVersion[] = TEXT("1.0");

	using FResidentID = int64;
	using FHomeID = int64;
	using FPersistentID = int64;
	using FEventID = int64;
	using FArriveID = int64;

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
		TArray<FInitialResidentRecord> Residents;
	};

	struct FEarthquakeDamageList
	{
		int32 Seed = 0;
		TArray<FEarthquakeDamageRecord> DamagedResidents;
	};
}
