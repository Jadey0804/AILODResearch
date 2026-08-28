// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualDemoSettings.h"

UAILODVisualDemoSettings::UAILODVisualDemoSettings()
{
	LowLevelProxyMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	FullActorBodyMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	FullActorHeadMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));
}

AILOD::FVisualDemoRuntimeConfig UAILODVisualDemoSettings::MakeRuntimeConfig() const
{
	AILOD::FVisualDemoRuntimeConfig Config;
	Config.SimulationSeed = SimulationSeed;
	Config.PopulationPerKingdom = PopulationPerKingdom;
	Config.Layout.LayoutVersion = LayoutVersion;
	Config.Layout.LayoutSeed = LayoutSeed;
	Config.Layout.ResidentsPerDistrict = ResidentsPerDistrict;
	Config.Layout.HomeSlotsPerDistrict = HomeSlotsPerDistrict;
	Config.Layout.DistrictSize = DistrictSizeMeters * 100.0;
	Config.Layout.KingdomGap = KingdomGapMeters * 100.0;
	Config.Layout.SpatialCellSize = SpatialCellSizeMeters * 100.0;
	Config.Observation.NormalProxyBudget = LowLevelProxyBudget;
	Config.Observation.NormalActiveBudget = NormalActiveActorBudget;
	Config.Presentation.LowLevelProxyCapacity = LowLevelProxyBudget
		+ Config.Observation.TelescopeProxyBudget;
	Config.Presentation.LocalRouteHalfLength = PlaceholderWalkRadiusMeters * 100.0;
	return Config;
}
