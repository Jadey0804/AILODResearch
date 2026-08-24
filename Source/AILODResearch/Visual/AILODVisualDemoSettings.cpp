// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualDemoSettings.h"

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
	return Config;
}
