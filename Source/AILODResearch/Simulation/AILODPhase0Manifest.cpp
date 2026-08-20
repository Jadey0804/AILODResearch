// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODPhase0Manifest.h"

#include "AILODLogSchema.h"
#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"

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

		void AllocateProportionalCounts(const int32 Total, int32* OutCounts)
		{
			TArray<int32> RemainderOrder;
			int32 Assigned = 0;
			for (int32 CohortIndex = 0; CohortIndex < UE_ARRAY_COUNT(Cohorts); ++CohortIndex)
			{
				const int32 WeightedTotal = Total * Cohorts[CohortIndex].PopulationPercent;
				OutCounts[CohortIndex] = WeightedTotal / 100;
				Assigned += OutCounts[CohortIndex];
				RemainderOrder.Add(CohortIndex);
			}

			RemainderOrder.Sort([Total](const int32 Left, const int32 Right)
			{
				const int32 LeftRemainder = Total * Cohorts[Left].PopulationPercent % 100;
				const int32 RightRemainder = Total * Cohorts[Right].PopulationPercent % 100;
				return LeftRemainder == RightRemainder ? Left < Right : LeftRemainder > RightRemainder;
			});

			for (int32 Index = 0; Assigned < Total; ++Index, ++Assigned)
			{
				++OutCounts[RemainderOrder[Index]];
			}
		}

		void SelectContinuitySample(
			FInitialPopulationManifest& Population,
			const int32 BaseSeed,
			FPersistentTestPool& OutPersistentPool)
		{
			constexpr int32 PoolPerKingdom = ContinuitySampleSize / 2;
			constexpr int32 Day7And30PerKingdom = Day7And30ContinuitySampleCount / 2;
			int32 PoolCounts[UE_ARRAY_COUNT(Cohorts)] = {};
			int32 Day7And30Counts[UE_ARRAY_COUNT(Cohorts)] = {};
			AllocateProportionalCounts(PoolPerKingdom, PoolCounts);
			AllocateProportionalCounts(Day7And30PerKingdom, Day7And30Counts);

			for (int32 KingdomIndex = 0; KingdomIndex < 2; ++KingdomIndex)
			{
				const EKingdom Kingdom = KingdomIndex == 0 ? EKingdom::A : EKingdom::B;
				for (int32 CohortIndex = 0; CohortIndex < UE_ARRAY_COUNT(Cohorts); ++CohortIndex)
				{
					const FCohortDefinition& Cohort = Cohorts[CohortIndex];
					TArray<FInitialResidentRecord*> Candidates;
					for (FInitialResidentRecord& Resident : Population.Residents)
					{
						if (Resident.Kingdom == Kingdom
							&& Resident.Profession == Cohort.Profession
							&& Resident.IncomeBand == Cohort.IncomeBand)
						{
							Candidates.Add(&Resident);
						}
					}

					FRandomStream Stream(MakeStreamSeed(
						BaseSeed,
						RandomStreams::PersistentSelection,
						KingdomIndex * UE_ARRAY_COUNT(Cohorts) + CohortIndex));
					for (int32 SelectionIndex = 0; SelectionIndex < PoolCounts[CohortIndex]; ++SelectionIndex)
					{
						const int32 SwapIndex = Stream.RandRange(SelectionIndex, Candidates.Num() - 1);
						Candidates.Swap(SelectionIndex, SwapIndex);

						const FInitialResidentRecord& Resident = *Candidates[SelectionIndex];

						FPersistentTestRecord& Record = OutPersistentPool.Residents.AddDefaulted_GetRef();
						Record.ResidentID = Resident.ResidentID;
						Record.HomeID = Resident.HomeID;
						Record.PersistentID = Resident.PersistentID;
						Record.Name = Resident.Name;
						Record.Kingdom = Resident.Kingdom;
						Record.Profession = Resident.Profession;
						Record.IncomeBand = Resident.IncomeBand;
						Record.bDay7 = SelectionIndex < Day7And30Counts[CohortIndex];
						Record.bDay30 = Record.bDay7;
						Record.bDay45 = true;
					}
				}
			}

			OutPersistentPool.Residents.Sort([](const FPersistentTestRecord& Left, const FPersistentTestRecord& Right)
			{
				return Left.PersistentID < Right.PersistentID;
			});
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

			FRandomStream Stream(MakeStreamSeed(BaseSeed, RandomStreams::EarthquakeDamage, CohortIndex));
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

	FString FPhase0ManifestGenerator::BuildConfigHash(const FPhase0Config& Config)
	{
		const FString ConfigText = FString::Printf(
			TEXT("spec=%s|schema=%s|seed=%d|population_per_kingdom=%d|")
			TEXT("cohorts=14,6,56,24|small_damage=4,2,17,7|cash_low=0,1,2,3|cash_nonlow=4,5,6,7|")
			TEXT("persistent_pool=%d|day7_day30=%d|streams=%08X,%08X,%08X,%08X"),
			SpecVersion,
			SchemaVersion,
			Config.Seed,
			Config.PopulationPerKingdom,
			ContinuitySampleSize,
			Day7And30ContinuitySampleCount,
			RandomStreams::PopulationComposition,
			RandomStreams::InitialCash,
			RandomStreams::EarthquakeDamage,
			RandomStreams::PersistentSelection);
		FTCHARToUTF8 Utf8(*ConfigText);
		return FSHA1::HashBuffer(Utf8.Get(), Utf8.Length()).ToString();
	}

	bool FPhase0ManifestGenerator::Generate(
		const FPhase0Config& Config,
		FInitialPopulationManifest& OutPopulation,
		FEarthquakeDamageList& OutDamage,
		FPersistentTestPool& OutPersistentPool,
		FString& OutError)
	{
		OutPopulation = {};
		OutDamage = {};
		OutPersistentPool = {};
		OutError.Reset();

		const bool bSupportedPopulation = Config.PopulationPerKingdom == 100
			|| Config.PopulationPerKingdom == 1000
			|| Config.PopulationPerKingdom == 5000
			|| Config.PopulationPerKingdom == 10000
			|| Config.PopulationPerKingdom == 25000
			|| Config.PopulationPerKingdom == 50000;
		if (!bSupportedPopulation)
		{
			OutError = TEXT("PopulationPerKingdom must match a frozen scale: 100, 1000, 5000, 10000, 25000, or 50000.");
			return false;
		}

		const FString ConfigHash = BuildConfigHash(Config);
		OutPopulation.Seed = Config.Seed;
		OutPopulation.PopulationPerKingdom = Config.PopulationPerKingdom;
		OutPopulation.ConfigHash = ConfigHash;
		OutPopulation.Residents.Reserve(Config.PopulationPerKingdom * 2);

		FResidentID NextResidentID = 1;
		for (int32 KingdomIndex = 0; KingdomIndex < 2; ++KingdomIndex)
		{
			const EKingdom Kingdom = KingdomIndex == 0 ? EKingdom::A : EKingdom::B;
			for (int32 CohortIndex = 0; CohortIndex < UE_ARRAY_COUNT(Cohorts); ++CohortIndex)
			{
				const FCohortDefinition& Cohort = Cohorts[CohortIndex];
				const int32 CohortPopulation = Config.PopulationPerKingdom * Cohort.PopulationPercent / 100;
				FRandomStream CashStream(MakeStreamSeed(
					Config.Seed,
					RandomStreams::InitialCash,
					KingdomIndex * UE_ARRAY_COUNT(Cohorts) + CohortIndex));

				for (int32 ResidentIndex = 0; ResidentIndex < CohortPopulation; ++ResidentIndex)
				{
					FInitialResidentRecord& Resident = OutPopulation.Residents.AddDefaulted_GetRef();
					Resident.ResidentID = NextResidentID;
					Resident.HomeID = NextResidentID;
					Resident.PersistentID = Resident.ResidentID;
					Resident.Name = MakeStableResidentName(Resident.ResidentID);
					Resident.Kingdom = Kingdom;
					Resident.Profession = Cohort.Profession;
					Resident.IncomeBand = Cohort.IncomeBand;
					Resident.Cash = GetCash(CashStream, Cohort.IncomeBand);
					++NextResidentID;
				}
			}
		}

		OutPersistentPool.Seed = Config.Seed;
		OutPersistentPool.ConfigHash = ConfigHash;
		SelectContinuitySample(OutPopulation, Config.Seed, OutPersistentPool);

		OutDamage.Seed = Config.Seed;
		OutDamage.ConfigHash = ConfigHash;
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
			TEXT("{\n  \"schema_version\": \"%s\",\n  \"spec_version\": \"%s\",\n  \"seed\": %d,\n  \"config_hash\": \"%s\",\n  \"population_per_kingdom\": %d,\n  \"residents\": [\n"),
			SchemaVersion,
			SpecVersion,
			Manifest.Seed,
			*Manifest.ConfigHash,
			Manifest.PopulationPerKingdom);

		for (int32 Index = 0; Index < Manifest.Residents.Num(); ++Index)
		{
			const FInitialResidentRecord& Resident = Manifest.Residents[Index];
			Output += FString::Printf(
				TEXT("    {\"resident_id\": %lld, \"home_id\": %lld, \"persistent_id\": %lld, \"name\": \"%s\", \"kingdom\": \"%s\", \"profession\": \"%s\", \"income_band\": \"%s\", \"cash\": %d, \"repair_credit\": %d, \"inventory_wood\": %d, \"home_state\": \"%s\", \"event_id\": %lld, \"arrive_id\": %lld}%s\n"),
				Resident.ResidentID,
				Resident.HomeID,
				Resident.PersistentID,
				*Resident.Name,
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
			TEXT("{\n  \"schema_version\": \"%s\",\n  \"spec_version\": \"%s\",\n  \"seed\": %d,\n  \"config_hash\": \"%s\",\n  \"damaged_residents\": [\n"),
			SchemaVersion,
			SpecVersion,
			DamageList.Seed,
			*DamageList.ConfigHash);

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

	FString FPhase0ManifestGenerator::SerializePersistentPool(const FPersistentTestPool& PersistentPool)
	{
		FString Output;
		Output += FString::Printf(
			TEXT("{\n  \"schema_version\": \"%s\",\n  \"spec_version\": \"%s\",\n  \"seed\": %d,\n  \"config_hash\": \"%s\",\n  \"persistent_residents\": [\n"),
			SchemaVersion,
			SpecVersion,
			PersistentPool.Seed,
			*PersistentPool.ConfigHash);

		for (int32 Index = 0; Index < PersistentPool.Residents.Num(); ++Index)
		{
			const FPersistentTestRecord& Record = PersistentPool.Residents[Index];
			Output += FString::Printf(
				TEXT("    {\"resident_id\": %lld, \"home_id\": %lld, \"persistent_id\": %lld, \"name\": \"%s\", \"kingdom\": \"%s\", \"profession\": \"%s\", \"income_band\": \"%s\", \"day7\": %s, \"day30\": %s, \"day45\": %s}%s\n"),
				Record.ResidentID,
				Record.HomeID,
				Record.PersistentID,
				*Record.Name,
				ToString(Record.Kingdom),
				ToString(Record.Profession),
				ToString(Record.IncomeBand),
				Record.bDay7 ? TEXT("true") : TEXT("false"),
				Record.bDay30 ? TEXT("true") : TEXT("false"),
				Record.bDay45 ? TEXT("true") : TEXT("false"),
				Index + 1 == PersistentPool.Residents.Num() ? TEXT("") : TEXT(","));
		}

		Output += TEXT("  ]\n}\n");
		return Output;
	}

	bool FPhase0ManifestGenerator::SaveArtifacts(
		const FString& OutputDirectory,
		const FInitialPopulationManifest& Population,
		const FEarthquakeDamageList& DamageList,
		const FPersistentTestPool& PersistentPool,
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
		const FString PersistentPoolPath = OutputDirectory / LogSchema::PersistentTestPoolFile;
		if (!FFileHelper::SaveStringToFile(SerializePopulation(Population), *PopulationPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			|| !FFileHelper::SaveStringToFile(SerializeDamage(DamageList), *DamagePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			|| !FFileHelper::SaveStringToFile(SerializePersistentPool(PersistentPool), *PersistentPoolPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to save Phase 0 artifacts to: %s"), *OutputDirectory);
			return false;
		}

		return true;
	}
}
