// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "../Simulation/AILODLogSchema.h"
#include "../Simulation/AILODPhase0Manifest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase0ManifestDeterminismTest,
	"AILODResearch.Phase0.ManifestDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase0ManifestDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;

	const FPhase0Config Config;
	FInitialPopulationManifest PopulationA;
	FInitialPopulationManifest PopulationB;
	FEarthquakeDamageList DamageA;
	FEarthquakeDamageList DamageB;
	FString Error;

	TestTrue(TEXT("First generation succeeds"), FPhase0ManifestGenerator::Generate(Config, PopulationA, DamageA, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	TestTrue(TEXT("Second generation succeeds"), FPhase0ManifestGenerator::Generate(Config, PopulationB, DamageB, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	const FString PopulationJsonA = FPhase0ManifestGenerator::SerializePopulation(PopulationA);
	const FString PopulationJsonB = FPhase0ManifestGenerator::SerializePopulation(PopulationB);
	const FString DamageJsonA = FPhase0ManifestGenerator::SerializeDamage(DamageA);
	const FString DamageJsonB = FPhase0ManifestGenerator::SerializeDamage(DamageB);

	TestEqual(TEXT("Initial population manifest is byte-identical for the same seed"), PopulationJsonA, PopulationJsonB);
	TestEqual(TEXT("Earthquake damage list is byte-identical for the same seed"), DamageJsonA, DamageJsonB);
	TestEqual(TEXT("Manifest contains 200 residents"), PopulationA.Residents.Num(), 200);
	TestEqual(TEXT("Damage list contains 30 residents"), DamageA.DamagedResidents.Num(), 30);

	int32 PopulationCounts[2][2][2] = {};
	TSet<FResidentID> ResidentIDs;
	for (const FInitialResidentRecord& Resident : PopulationA.Residents)
	{
		const int32 KingdomIndex = Resident.Kingdom == EKingdom::A ? 0 : 1;
		const int32 ProfessionIndex = Resident.Profession == EProfession::Logger ? 0 : 1;
		const int32 IncomeIndex = Resident.IncomeBand == EIncomeBand::Low ? 0 : 1;
		++PopulationCounts[KingdomIndex][ProfessionIndex][IncomeIndex];

		TestTrue(TEXT("Resident IDs are unique"), !ResidentIDs.Contains(Resident.ResidentID));
		ResidentIDs.Add(Resident.ResidentID);
		TestEqual(TEXT("Home ID initially matches resident ID"), Resident.HomeID, Resident.ResidentID);
		TestEqual(TEXT("Persistent ID starts unset"), Resident.PersistentID, static_cast<FPersistentID>(0));
		TestEqual(TEXT("Initial repair credit is zero"), Resident.RepairCredit, 0);
		TestEqual(TEXT("Initial wood inventory is zero"), Resident.InventoryWood, 0);
		TestTrue(TEXT("Initial home state is Healthy"), Resident.HomeState == EHomeState::Healthy);
		TestEqual(TEXT("Initial event ID is unset"), Resident.EventID, static_cast<FEventID>(0));
		TestEqual(TEXT("Initial ArriveID is unset"), Resident.ArriveID, static_cast<FArriveID>(0));

		const bool bCashIsValid = Resident.IncomeBand == EIncomeBand::Low
			? Resident.Cash >= 0 && Resident.Cash <= 3
			: Resident.Cash >= 4 && Resident.Cash <= 7;
		TestTrue(TEXT("Initial cash is an integer in the frozen range"), bCashIsValid);
	}

	for (int32 KingdomIndex = 0; KingdomIndex < 2; ++KingdomIndex)
	{
		TestEqual(TEXT("Logger Low population is 14"), PopulationCounts[KingdomIndex][0][0], 14);
		TestEqual(TEXT("Logger NonLow population is 6"), PopulationCounts[KingdomIndex][0][1], 6);
		TestEqual(TEXT("Worker Low population is 56"), PopulationCounts[KingdomIndex][1][0], 56);
		TestEqual(TEXT("Worker NonLow population is 24"), PopulationCounts[KingdomIndex][1][1], 24);
	}

	int32 DamageCounts[2][2] = {};
	TSet<FResidentID> DamagedResidentIDs;
	for (const FEarthquakeDamageRecord& Record : DamageA.DamagedResidents)
	{
		const int32 ProfessionIndex = Record.Profession == EProfession::Logger ? 0 : 1;
		const int32 IncomeIndex = Record.IncomeBand == EIncomeBand::Low ? 0 : 1;
		++DamageCounts[ProfessionIndex][IncomeIndex];

		TestTrue(TEXT("Damage list IDs are unique"), !DamagedResidentIDs.Contains(Record.ResidentID));
		DamagedResidentIDs.Add(Record.ResidentID);
		TestTrue(TEXT("Earthquake damage only selects Kingdom A"), Record.ResidentID <= Config.PopulationPerKingdom);
	}

	TestEqual(TEXT("Damaged Logger Low count is 4"), DamageCounts[0][0], 4);
	TestEqual(TEXT("Damaged Logger NonLow count is 2"), DamageCounts[0][1], 2);
	TestEqual(TEXT("Damaged Worker Low count is 17"), DamageCounts[1][0], 17);
	TestEqual(TEXT("Damaged Worker NonLow count is 7"), DamageCounts[1][1], 7);

	const int32 FrozenLargePopulationsPerKingdom[] = { 1000, 5000, 10000 };
	for (const int32 PopulationPerKingdom : FrozenLargePopulationsPerKingdom)
	{
		FPhase0Config LargeConfig = Config;
		LargeConfig.PopulationPerKingdom = PopulationPerKingdom;
		FInitialPopulationManifest LargePopulation;
		FEarthquakeDamageList LargeDamage;
		TestTrue(
			FString::Printf(TEXT("Frozen scale N=%d generates"), PopulationPerKingdom),
			FPhase0ManifestGenerator::Generate(LargeConfig, LargePopulation, LargeDamage, Error));
		TestEqual(
			FString::Printf(TEXT("Frozen scale N=%d resident count"), PopulationPerKingdom),
			LargePopulation.Residents.Num(),
			PopulationPerKingdom * 2);
		TestEqual(
			FString::Printf(TEXT("Frozen scale N=%d damage count"), PopulationPerKingdom),
			LargeDamage.DamagedResidents.Num(),
			PopulationPerKingdom * 30 / 100);
	}

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase0/Determinism"));
	const FString RunAPath = FPaths::Combine(TestRoot, TEXT("RunA"));
	const FString RunBPath = FPaths::Combine(TestRoot, TEXT("RunB"));
	TestTrue(TEXT("Run A artifacts save"), FPhase0ManifestGenerator::SaveArtifacts(RunAPath, PopulationA, DamageA, Error));
	TestTrue(TEXT("Run B artifacts save"), FPhase0ManifestGenerator::SaveArtifacts(RunBPath, PopulationB, DamageB, Error));

	FString SavedPopulationA;
	FString SavedPopulationB;
	FString SavedDamageA;
	FString SavedDamageB;
	TestTrue(TEXT("Run A population artifact loads"), FFileHelper::LoadFileToString(SavedPopulationA, *(RunAPath / LogSchema::InitialPopulationManifestFile)));
	TestTrue(TEXT("Run B population artifact loads"), FFileHelper::LoadFileToString(SavedPopulationB, *(RunBPath / LogSchema::InitialPopulationManifestFile)));
	TestTrue(TEXT("Run A damage artifact loads"), FFileHelper::LoadFileToString(SavedDamageA, *(RunAPath / LogSchema::EarthquakeDamageListFile)));
	TestTrue(TEXT("Run B damage artifact loads"), FFileHelper::LoadFileToString(SavedDamageB, *(RunBPath / LogSchema::EarthquakeDamageListFile)));
	TestEqual(TEXT("Saved population artifacts are byte-identical"), SavedPopulationA, SavedPopulationB);
	TestEqual(TEXT("Saved damage artifacts are byte-identical"), SavedDamageA, SavedDamageB);

	AddInfo(FString::Printf(
		TEXT("Phase 0 artifacts written to %s. Population CRC=%08X Damage CRC=%08X"),
		*TestRoot,
		FCrc::StrCrc32(*SavedPopulationA),
		FCrc::StrCrc32(*SavedDamageA)));

	return true;
}

#endif
