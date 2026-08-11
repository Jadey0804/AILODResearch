// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODPhase0Manifest.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "AILODLogSchema.h"

namespace AILOD
{
	namespace
	{
		struct FCohortDefinition
		{
			EProfession Profession;
			EIncomeBand IncomeBand;
			int32 PopulationPercent;
			int32 SmallScaleDamageCount;
		};

		constexpr FCohortDefinition Cohorts[] =
		{
			{ EProfession::Logger, EIncomeBand::Low, 14, 4 },
			{ EProfession::Logger, EIncomeBand::NonLow, 6, 2 },
			{ EProfession::Worker, EIncomeBand::Low, 56, 17 },
			{ EProfession::Worker, EIncomeBand::NonLow, 24, 7 }
		};

		int32 MakeStreamSeed(const int32 BaseSeed, const uint32 StreamTag, const int32 Index)
		{
			uint32 Value = static_cast<uint32>(BaseSeed);
			Value ^= StreamTag + 0x9E3779B9u + (Value << 6u) + (Value >> 2u);
			Value ^= static_cast<uint32>(Index) * 0x85EBCA6Bu;
			return static_cast<int32>(Value & 0x7FFFFFFFu);
		}

		const TCHAR* ToString(const EKingdom Kingdom)
		{
			return Kingdom == EKingdom::A ? TEXT("A") : TEXT("B");
		}

		const TCHAR* ToString(const EProfession Profession)
		{
			return Profession == EProfession::Logger ? TEXT("Logger") : TEXT("Worker");
		}

		const TCHAR* ToString(const EIncomeBand IncomeBand)
		{
			return IncomeBand == EIncomeBand::Low ? TEXT("Low") : TEXT("NonLow");
		}

		const TCHAR* ToString(const EHomeState HomeState)
		{
			switch (HomeState)
			{
			case EHomeState::Healthy:
				return TEXT("Healthy");
			case EHomeState::DamagedWaiting:
				return TEXT("DamagedWaiting");
			case EHomeState::UnderRepair:
				return TEXT("UnderRepair");
			case EHomeState::Repaired:
				return TEXT("Repaired");
			}

			return TEXT("Unknown");
		}

		int32 GetCash(FRandomStream& Stream, const EIncomeBand IncomeBand)
		{
			return IncomeBand == EIncomeBand::Low
				? Stream.RandRange(0, 3)
				: Stream.RandRange(4, 7);
		}

		void SelectDamagedResidents(
			const FInitialPopulationManifest& Population,
			const int32 BaseSeed,
			const int32 CohortIndex,
			const int32 DamageCount,
			TArray<FEarthquakeDamageRecord>& OutDamage)
		{
			const FCohortDefinition& Cohort = Cohorts[CohortIndex];
			TArray<const FInitialResidentRecord*> Candidates;

			for (const FInitialResidentRecord& Resident : Population.Residents)
			{
				if (Resident.Kingdom == EKingdom::A
					&& Resident.Profession == Cohort.Profession
					&& Resident.IncomeBand == Cohort.IncomeBand)
				{
					Candidates.Add(&Resident);
				}
			}

			FRandomStream Stream(MakeStreamSeed(BaseSeed, 0xDA6A6E01u, CohortIndex));
			for (int32 SelectionIndex = 0; SelectionIndex < DamageCount; ++SelectionIndex)
			{
				const int32 SwapIndex = Stream.RandRange(SelectionIndex, Candidates.Num() - 1);
				Candidates.Swap(SelectionIndex, SwapIndex);

				const FInitialResidentRecord& Resident = *Candidates[SelectionIndex];
				FEarthquakeDamageRecord& Record = OutDamage.AddDefaulted_GetRef();
				Record.ResidentID = Resident.ResidentID;
				Record.HomeID = Resident.HomeID;
				Record.Profession = Resident.Profession;
				Record.IncomeBand = Resident.IncomeBand;
			}
		}
	}

	bool FPhase0ManifestGenerator::Generate(
		const FPhase0Config& Config,
		FInitialPopulationManifest& OutPopulation,
		FEarthquakeDamageList& OutDamage,
		FString& OutError)
	{
		OutPopulation = {};
		OutDamage = {};
		OutError.Reset();

		const bool bSupportedPopulation = Config.PopulationPerKingdom == 100
			|| Config.PopulationPerKingdom == 1000
			|| Config.PopulationPerKingdom == 5000
			|| Config.PopulationPerKingdom == 10000;
		if (!bSupportedPopulation)
		{
			OutError = TEXT("PopulationPerKingdom must match a frozen scale: 100, 1000, 5000, or 10000.");
			return false;
		}

		OutPopulation.Seed = Config.Seed;
		OutPopulation.PopulationPerKingdom = Config.PopulationPerKingdom;
		OutPopulation.Residents.Reserve(Config.PopulationPerKingdom * 2);

		FResidentID NextResidentID = 1;
		for (int32 KingdomIndex = 0; KingdomIndex < 2; ++KingdomIndex)
		{
			const EKingdom Kingdom = KingdomIndex == 0 ? EKingdom::A : EKingdom::B;
			for (int32 CohortIndex = 0; CohortIndex < UE_ARRAY_COUNT(Cohorts); ++CohortIndex)
			{
				const FCohortDefinition& Cohort = Cohorts[CohortIndex];
				const int32 CohortPopulation = Config.PopulationPerKingdom * Cohort.PopulationPercent / 100;
				FRandomStream CashStream(MakeStreamSeed(Config.Seed, 0xCA511001u, KingdomIndex * UE_ARRAY_COUNT(Cohorts) + CohortIndex));

				for (int32 ResidentIndex = 0; ResidentIndex < CohortPopulation; ++ResidentIndex)
				{
					FInitialResidentRecord& Resident = OutPopulation.Residents.AddDefaulted_GetRef();
					Resident.ResidentID = NextResidentID;
					Resident.HomeID = NextResidentID;
					Resident.Kingdom = Kingdom;
					Resident.Profession = Cohort.Profession;
					Resident.IncomeBand = Cohort.IncomeBand;
					Resident.Cash = GetCash(CashStream, Cohort.IncomeBand);
					++NextResidentID;
				}
			}
		}

		OutDamage.Seed = Config.Seed;
		for (int32 CohortIndex = 0; CohortIndex < UE_ARRAY_COUNT(Cohorts); ++CohortIndex)
		{
			const FCohortDefinition& Cohort = Cohorts[CohortIndex];
			const int32 CohortPopulation = Config.PopulationPerKingdom * Cohort.PopulationPercent / 100;
			const int32 DamageCount = Config.PopulationPerKingdom == 100
				? Cohort.SmallScaleDamageCount
				: CohortPopulation * 30 / 100;

			SelectDamagedResidents(OutPopulation, Config.Seed, CohortIndex, DamageCount, OutDamage.DamagedResidents);
		}

		OutDamage.DamagedResidents.Sort([](const FEarthquakeDamageRecord& Left, const FEarthquakeDamageRecord& Right)
		{
			return Left.ResidentID < Right.ResidentID;
		});

		return true;
	}

	FString FPhase0ManifestGenerator::SerializePopulation(const FInitialPopulationManifest& Manifest)
	{
		FString Output;
		Output += FString::Printf(
			TEXT("{\n  \"schema_version\": \"%s\",\n  \"spec_version\": \"%s\",\n  \"seed\": %d,\n  \"population_per_kingdom\": %d,\n  \"residents\": [\n"),
			SchemaVersion,
			SpecVersion,
			Manifest.Seed,
			Manifest.PopulationPerKingdom);

		for (int32 Index = 0; Index < Manifest.Residents.Num(); ++Index)
		{
			const FInitialResidentRecord& Resident = Manifest.Residents[Index];
			Output += FString::Printf(
				TEXT("    {\"resident_id\": %lld, \"home_id\": %lld, \"persistent_id\": %lld, \"kingdom\": \"%s\", \"profession\": \"%s\", \"income_band\": \"%s\", \"cash\": %d, \"repair_credit\": %d, \"inventory_wood\": %d, \"home_state\": \"%s\", \"event_id\": %lld, \"arrive_id\": %lld}%s\n"),
				Resident.ResidentID,
				Resident.HomeID,
				Resident.PersistentID,
				ToString(Resident.Kingdom),
				ToString(Resident.Profession),
				ToString(Resident.IncomeBand),
				Resident.Cash,
				Resident.RepairCredit,
				Resident.InventoryWood,
				ToString(Resident.HomeState),
				Resident.EventID,
				Resident.ArriveID,
				Index + 1 == Manifest.Residents.Num() ? TEXT("") : TEXT(","));
		}

		Output += TEXT("  ]\n}\n");
		return Output;
	}

	FString FPhase0ManifestGenerator::SerializeDamage(const FEarthquakeDamageList& DamageList)
	{
		FString Output;
		Output += FString::Printf(
			TEXT("{\n  \"schema_version\": \"%s\",\n  \"spec_version\": \"%s\",\n  \"seed\": %d,\n  \"damaged_residents\": [\n"),
			SchemaVersion,
			SpecVersion,
			DamageList.Seed);

		for (int32 Index = 0; Index < DamageList.DamagedResidents.Num(); ++Index)
		{
			const FEarthquakeDamageRecord& Record = DamageList.DamagedResidents[Index];
			Output += FString::Printf(
				TEXT("    {\"resident_id\": %lld, \"home_id\": %lld, \"profession\": \"%s\", \"income_band\": \"%s\"}%s\n"),
				Record.ResidentID,
				Record.HomeID,
				ToString(Record.Profession),
				ToString(Record.IncomeBand),
				Index + 1 == DamageList.DamagedResidents.Num() ? TEXT("") : TEXT(","));
		}

		Output += TEXT("  ]\n}\n");
		return Output;
	}

	bool FPhase0ManifestGenerator::SaveArtifacts(
		const FString& OutputDirectory,
		const FInitialPopulationManifest& Population,
		const FEarthquakeDamageList& DamageList,
		FString& OutError)
	{
		OutError.Reset();
		if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			OutError = FString::Printf(TEXT("Failed to create output directory: %s"), *OutputDirectory);
			return false;
		}

		const FString PopulationPath = OutputDirectory / LogSchema::InitialPopulationManifestFile;
		const FString DamagePath = OutputDirectory / LogSchema::EarthquakeDamageListFile;
		if (!FFileHelper::SaveStringToFile(SerializePopulation(Population), *PopulationPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			|| !FFileHelper::SaveStringToFile(SerializeDamage(DamageList), *DamagePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to save Phase 0 artifacts to: %s"), *OutputDirectory);
			return false;
		}

		return true;
	}
}
