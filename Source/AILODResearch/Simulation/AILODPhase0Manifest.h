// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODPhase0Types.h"

namespace AILOD
{
	class FPhase0ManifestGenerator
	{
	public:
		static bool Generate(
			const FPhase0Config& Config,
			FInitialPopulationManifest& OutPopulation,
			FEarthquakeDamageList& OutDamage,
			FString& OutError);

		static FString SerializePopulation(const FInitialPopulationManifest& Manifest);
		static FString SerializeDamage(const FEarthquakeDamageList& DamageList);

		static bool SaveArtifacts(
			const FString& OutputDirectory,
			const FInitialPopulationManifest& Population,
			const FEarthquakeDamageList& DamageList,
			FString& OutError);
	};
}
