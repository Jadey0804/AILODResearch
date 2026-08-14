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
			FPersistentTestPool& OutPersistentPool,
			FString& OutError);

		static FString BuildConfigHash(const FPhase0Config& Config);
		static FString SerializePopulation(const FInitialPopulationManifest& Manifest);
		static FString SerializeDamage(const FEarthquakeDamageList& DamageList);
		static FString SerializePersistentPool(const FPersistentTestPool& PersistentPool);

		static bool SaveArtifacts(
			const FString& OutputDirectory,
			const FInitialPopulationManifest& Population,
			const FEarthquakeDamageList& DamageList,
			const FPersistentTestPool& PersistentPool,
			FString& OutError);
	};
}
