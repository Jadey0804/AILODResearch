// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "../Presentation/AILODVisualDemoRuntime.h"
#include "AILODVisualDemoSettings.generated.h"

class UStaticMesh;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="AILOD Visual Demo"))
class AILODRESEARCH_API UAILODVisualDemoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAILODVisualDemoSettings();
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

	UPROPERTY(Config, EditAnywhere, Category="Observation")
	bool bUseRadialNormalObservation = true;

	UPROPERTY(Config, EditAnywhere, Category="Telescope", meta=(ClampMin="100.0"))
	double TelescopeObservationDistanceMeters = 1500.0;

	UPROPERTY(Config, EditAnywhere, Category="Telescope", meta=(ClampMin="1.0"))
	double TelescopeMinimumDistanceMeters = 300.0;

	UPROPERTY(Config, EditAnywhere, Category="Telescope", meta=(ClampMin="0.1", ClampMax="20.0"))
	double TelescopeObservationHalfAngleDegrees = 2.0;

	UPROPERTY(Config, EditAnywhere, Category="Telescope", meta=(ClampMin="0.1", ClampMax="10.0"))
	double TelescopeFocusSeconds = 1.5;

	UPROPERTY(Config, EditAnywhere, Category="Telescope", meta=(ClampMin="50.0"))
	double TelescopeStreamingRadiusMeters = 300.0;

	UPROPERTY(Config, EditAnywhere, Category="Telescope|Camera", meta=(ClampMin="0.5", ClampMax="20.0", UIMin="1.0", UIMax="10.0"))
	double TelescopeCameraHeightMeters = 2.5;

	UPROPERTY(Config, EditAnywhere, Category="Telescope|Camera", meta=(ClampMin="-30.0", ClampMax="30.0", UIMin="-10.0", UIMax="10.0"))
	double TelescopeCameraPitchDegrees = 0.0;

	UPROPERTY(Config, EditAnywhere, Category="Telescope|Camera", meta=(ClampMin="10.0", ClampMax="90.0", UIMin="20.0", UIMax="70.0"))
	double TelescopeCameraFieldOfViewDegrees = 50.0;

	UPROPERTY(Config, EditAnywhere, Category="NPC Presentation", meta=(ClampMin="1", ClampMax="512"))
	int32 LowLevelProxyBudget = 128;

	UPROPERTY(Config, EditAnywhere, Category="NPC Presentation", meta=(ClampMin="0", ClampMax="44"))
	int32 NormalActiveActorBudget = 35;

	UPROPERTY(Config, EditAnywhere, Category="NPC Presentation", meta=(ClampMin="1.0", ClampMax="50.0"))
	double PlaceholderWalkRadiusMeters = 8.0;

	UPROPERTY(Config, EditAnywhere, Category="NPC Presentation", meta=(ClampMin="0.1", ClampMax="10.0"))
	double PlaceholderWalkSpeedMetersPerSecond = 1.5;

	UPROPERTY(Config, EditAnywhere, Category="NPC Presentation")
	double NPCGroundZCentimeters = 100.0;

	UPROPERTY(Config, EditAnywhere, Category="NPC Presentation")
	bool bShowResidentDebugLabels = true;

	UPROPERTY(Config, EditAnywhere, Category="NPC Presentation", meta=(DisplayName="Shared Resident Body Mesh"))
	TSoftObjectPtr<UStaticMesh> FullActorBodyMesh;

	UPROPERTY(Config, EditAnywhere, Category="NPC Presentation", meta=(DisplayName="Shared Resident Head Mesh"))
	TSoftObjectPtr<UStaticMesh> FullActorHeadMesh;

	UPROPERTY(Config, EditAnywhere, Category="PCG", meta=(ClampMin="0", ClampMax="4096"))
	int32 TreesPerDistrict = 128;

	UPROPERTY(Config, EditAnywhere, Category="PCG", meta=(ClampMin="1.0"))
	double RoadWidthMeters = 8.0;
};
