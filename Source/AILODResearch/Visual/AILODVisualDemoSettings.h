// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "../Presentation/AILODVisualDemoRuntime.h"
#include "AILODVisualDemoSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="AILOD Visual Demo"))
class AILODRESEARCH_API UAILODVisualDemoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("AILOD"); }
	AILOD::FVisualDemoRuntimeConfig MakeRuntimeConfig() const;

	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="1"))
	int32 PopulationPerKingdom = 10000;

	UPROPERTY(Config, EditAnywhere, Category="Simulation")
	int32 SimulationSeed = 20260810;

	UPROPERTY(Config, EditAnywhere, Category="Layout")
	FString LayoutVersion = TEXT("phase7b-layout-v1");

	UPROPERTY(Config, EditAnywhere, Category="Layout")
	int32 LayoutSeed = 20260823;

	UPROPERTY(Config, EditAnywhere, Category="Layout", meta=(ClampMin="1"))
	int32 ResidentsPerDistrict = 2500;

	UPROPERTY(Config, EditAnywhere, Category="Layout", meta=(ClampMin="1"))
	int32 HomeSlotsPerDistrict = 64;

	UPROPERTY(Config, EditAnywhere, Category="Layout", meta=(ClampMin="100.0"))
	double DistrictSizeMeters = 1000.0;

	UPROPERTY(Config, EditAnywhere, Category="Layout", meta=(ClampMin="0.0"))
	double KingdomGapMeters = 2000.0;

	UPROPERTY(Config, EditAnywhere, Category="Layout", meta=(ClampMin="1.0"))
	double SpatialCellSizeMeters = 50.0;

	UPROPERTY(Config, EditAnywhere, Category="Observation", meta=(ClampMin="1.0"))
	double NormalObservationDistanceMeters = 200.0;

	UPROPERTY(Config, EditAnywhere, Category="Observation", meta=(ClampMin="1.0", ClampMax="90.0"))
	double NormalObservationHalfAngleDegrees = 60.0;

	UPROPERTY(Config, EditAnywhere, Category="PCG", meta=(ClampMin="0", ClampMax="4096"))
	int32 TreesPerDistrict = 128;

	UPROPERTY(Config, EditAnywhere, Category="PCG", meta=(ClampMin="1.0"))
	double RoadWidthMeters = 8.0;
};
