// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODPhase0Types.h"

namespace AILOD::DomainRules
{
	inline constexpr double HoursPerGameDay = 24.0;
	inline constexpr double ForestGrowthRatePerDay = 0.025;
	inline constexpr double BaselineHarvestPerPersonPerDay = 0.08;
	inline constexpr double BaselineImportPerPersonPerDay = 0.02;
	inline constexpr double RoutineConsumptionPerPersonPerDay = 0.10;
	inline constexpr double HarvestCapPerPersonPerDay = 0.06;
	inline constexpr double StateImportDailyCapPerPerson = 0.08;
	inline constexpr double StateImportPrice = 1.25;
	inline constexpr double RepairStartCapacityPerPersonPerDay = 0.01;
	inline constexpr double RepairWoodPerHome = 4.0;
	inline constexpr double RepairAidPerHome = 2.0;
	inline constexpr FPolicyID HarvestCapPolicyID = 1;
	inline constexpr FPolicyID StateImportPolicyID = 2;
	inline constexpr FPolicyID RepairAidPolicyID = 3;

	inline int64 PaymentCoins(const int32 WoodQuantity, const double WoodPrice)
	{
		return FMath::CeilToInt64(static_cast<double>(WoodQuantity) * WoodPrice);
	}
}
