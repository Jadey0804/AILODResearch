// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "../Simulation/AILODLogSchema.h"
#include "../Simulation/AILODPhase0Manifest.h"

namespace
{
	bool ParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}
}

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
	FPersistentTestPool PersistentPoolA;
	FPersistentTestPool PersistentPoolB;
	FString Error;

	TestTrue(TEXT("First generation succeeds"), FPhase0ManifestGenerator::Generate(Config, PopulationA, DamageA, PersistentPoolA, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	TestTrue(TEXT("Second generation succeeds"), FPhase0ManifestGenerator::Generate(Config, PopulationB, DamageB, PersistentPoolB, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	const FString PopulationJsonA = FPhase0ManifestGenerator::SerializePopulation(PopulationA);
	const FString PopulationJsonB = FPhase0ManifestGenerator::SerializePopulation(PopulationB);
	const FString DamageJsonA = FPhase0ManifestGenerator::SerializeDamage(DamageA);
	const FString DamageJsonB = FPhase0ManifestGenerator::SerializeDamage(DamageB);
	const FString PersistentJsonA = FPhase0ManifestGenerator::SerializePersistentPool(PersistentPoolA);
	const FString PersistentJsonB = FPhase0ManifestGenerator::SerializePersistentPool(PersistentPoolB);

	TestEqual(TEXT("Initial population manifest is byte-identical for the same seed"), PopulationJsonA, PopulationJsonB);
	TestEqual(TEXT("Earthquake damage list is byte-identical for the same seed"), DamageJsonA, DamageJsonB);
	TestEqual(TEXT("Persistent test pool is byte-identical for the same seed"), PersistentJsonA, PersistentJsonB);
	TestEqual(TEXT("Config hash is reproducible"), PopulationA.ConfigHash, FPhase0ManifestGenerator::BuildConfigHash(Config));
	TestEqual(TEXT("Damage uses the population config hash"), DamageA.ConfigHash, PopulationA.ConfigHash);
	TestEqual(TEXT("Persistent pool uses the population config hash"), PersistentPoolA.ConfigHash, PopulationA.ConfigHash);

	TSharedPtr<FJsonObject> PopulationRoot;
	TSharedPtr<FJsonObject> DamageRoot;
	TSharedPtr<FJsonObject> PersistentRoot;
	TestTrue(TEXT("Population JSON parses"), ParseJsonObject(PopulationJsonA, PopulationRoot));
	TestTrue(TEXT("Damage JSON parses"), ParseJsonObject(DamageJsonA, DamageRoot));
	TestTrue(TEXT("Persistent pool JSON parses"), ParseJsonObject(PersistentJsonA, PersistentRoot));
	if (PopulationRoot.IsValid() && DamageRoot.IsValid() && PersistentRoot.IsValid())
	{
		TestEqual(TEXT("Population JSON config hash matches"), PopulationRoot->GetStringField(TEXT("config_hash")), PopulationA.ConfigHash);
		TestEqual(TEXT("Damage JSON config hash matches"), DamageRoot->GetStringField(TEXT("config_hash")), PopulationA.ConfigHash);
		TestEqual(TEXT("Persistent JSON config hash matches"), PersistentRoot->GetStringField(TEXT("config_hash")), PopulationA.ConfigHash);
	}

	TestEqual(TEXT("Manifest contains 200 residents"), PopulationA.Residents.Num(), 200);
	TestEqual(TEXT("Damage list contains 30 residents"), DamageA.DamagedResidents.Num(), 30);
	TestEqual(TEXT("Persistent pool contains 20 residents"), PersistentPoolA.Residents.Num(), PersistentPoolSize);

	int32 PopulationCounts[2][2][2] = {};
	int32 PersistentResidentCount = 0;
	TSet<FResidentID> ResidentIDs;
	TSet<FPersistentID> PersistentIDs;
	TSet<FString> PersistentNames;
	for (const FInitialResidentRecord& Resident : PopulationA.Residents)
	{
		const int32 KingdomIndex = Resident.Kingdom == EKingdom::A ? 0 : 1;
		const int32 ProfessionIndex = Resident.Profession == EProfession::Logger ? 0 : 1;
		const int32 IncomeIndex = Resident.IncomeBand == EIncomeBand::Low ? 0 : 1;
		++PopulationCounts[KingdomIndex][ProfessionIndex][IncomeIndex];

		TestTrue(TEXT("Resident IDs are unique"), !ResidentIDs.Contains(Resident.ResidentID));
		ResidentIDs.Add(Resident.ResidentID);
		TestEqual(TEXT("Home ID initially matches resident ID"), Resident.HomeID, Resident.ResidentID);
		TestEqual(TEXT("Initial repair credit is zero"), Resident.RepairCredit, 0);
		TestEqual(TEXT("Initial wood inventory is zero"), Resident.InventoryWood, 0);
		TestTrue(TEXT("Initial home state is Healthy"), Resident.HomeState == EHomeState::Healthy);
		TestEqual(TEXT("Initial event ID is unset"), Resident.EventID, static_cast<FEventID>(0));
		TestEqual(TEXT("Initial ArriveID is unset"), Resident.ArriveID, static_cast<FArriveID>(0));

		if (Resident.PersistentID == 0)
		{
			TestTrue(TEXT("Anonymous resident name is empty"), Resident.Name.IsEmpty());
		}
		else
		{
			++PersistentResidentCount;
			TestTrue(TEXT("Persistent IDs are unique"), !PersistentIDs.Contains(Resident.PersistentID));
			TestTrue(TEXT("Persistent names are unique"), !PersistentNames.Contains(Resident.Name));
			TestFalse(TEXT("Persistent resident name is set"), Resident.Name.IsEmpty());
			TestEqual(TEXT("Persistent ID initially matches resident ID"), Resident.PersistentID, Resident.ResidentID);
			PersistentIDs.Add(Resident.PersistentID);
			PersistentNames.Add(Resident.Name);
		}

		const bool bCashIsValid = Resident.IncomeBand == EIncomeBand::Low
			? Resident.Cash >= 0 && Resident.Cash <= 3
			: Resident.Cash >= 4 && Resident.Cash <= 7;
		TestTrue(TEXT("Initial cash is an integer in the frozen range"), bCashIsValid);
	}
	TestEqual(TEXT("Exactly 20 population records are Persistent"), PersistentResidentCount, PersistentPoolSize);

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

	int32 PoolCounts[2][2][2] = {};
	int32 Day7Counts[2] = {};
	int32 Day30Counts[2] = {};
	int32 Day45Counts[2] = {};
	TSet<FResidentID> PoolResidentIDs;
	TSet<FPersistentID> PoolPersistentIDs;
	TSet<FString> PoolNames;
	for (const FPersistentTestRecord& Record : PersistentPoolA.Residents)
	{
		const int32 KingdomIndex = Record.Kingdom == EKingdom::A ? 0 : 1;
		const int32 ProfessionIndex = Record.Profession == EProfession::Logger ? 0 : 1;
		const int32 IncomeIndex = Record.IncomeBand == EIncomeBand::Low ? 0 : 1;
		++PoolCounts[KingdomIndex][ProfessionIndex][IncomeIndex];
		if (Record.bDay7)
		{
			++Day7Counts[KingdomIndex];
		}
		if (Record.bDay30)
		{
			++Day30Counts[KingdomIndex];
		}
		if (Record.bDay45)
		{
			++Day45Counts[KingdomIndex];
		}
		TestEqual(TEXT("Day 7 and Day 30 membership is identical"), Record.bDay7, Record.bDay30);

		TestTrue(TEXT("Persistent pool resident IDs are unique"), !PoolResidentIDs.Contains(Record.ResidentID));
		TestTrue(TEXT("Persistent pool IDs are unique"), !PoolPersistentIDs.Contains(Record.PersistentID));
		TestTrue(TEXT("Persistent pool names are unique"), !PoolNames.Contains(Record.Name));
		PoolResidentIDs.Add(Record.ResidentID);
		PoolPersistentIDs.Add(Record.PersistentID);
		PoolNames.Add(Record.Name);

		const FInitialResidentRecord& PopulationRecord = PopulationA.Residents[static_cast<int32>(Record.ResidentID - 1)];
		TestEqual(TEXT("Persistent pool ID matches population"), Record.PersistentID, PopulationRecord.PersistentID);
		TestEqual(TEXT("Persistent pool name matches population"), Record.Name, PopulationRecord.Name);
		TestEqual(TEXT("Persistent pool home matches population"), Record.HomeID, PopulationRecord.HomeID);
	}

	for (int32 KingdomIndex = 0; KingdomIndex < 2; ++KingdomIndex)
	{
		TestEqual(TEXT("Persistent Logger Low count is 1 per kingdom"), PoolCounts[KingdomIndex][0][0], 1);
		TestEqual(TEXT("Persistent Logger NonLow count is 1 per kingdom"), PoolCounts[KingdomIndex][0][1], 1);
		TestEqual(TEXT("Persistent Worker Low count is 6 per kingdom"), PoolCounts[KingdomIndex][1][0], 6);
		TestEqual(TEXT("Persistent Worker NonLow count is 2 per kingdom"), PoolCounts[KingdomIndex][1][1], 2);
		TestEqual(TEXT("Day 7 uses 5 Persistent residents per kingdom"), Day7Counts[KingdomIndex], 5);
		TestEqual(TEXT("Day 30 uses the same 5 Persistent residents per kingdom"), Day30Counts[KingdomIndex], 5);
		TestEqual(TEXT("Day 45 uses all 10 Persistent residents per kingdom"), Day45Counts[KingdomIndex], 10);
	}

	const int32 FrozenLargePopulationsPerKingdom[] = { 1000, 5000, 10000 };
	for (const int32 PopulationPerKingdom : FrozenLargePopulationsPerKingdom)
	{
		FPhase0Config LargeConfig = Config;
		LargeConfig.PopulationPerKingdom = PopulationPerKingdom;
		FInitialPopulationManifest LargePopulation;
		FEarthquakeDamageList LargeDamage;
		FPersistentTestPool LargePersistentPool;
		TestTrue(
			FString::Printf(TEXT("Frozen scale N=%d generates"), PopulationPerKingdom),
			FPhase0ManifestGenerator::Generate(LargeConfig, LargePopulation, LargeDamage, LargePersistentPool, Error));
		TestEqual(
			FString::Printf(TEXT("Frozen scale N=%d resident count"), PopulationPerKingdom),
			LargePopulation.Residents.Num(),
			PopulationPerKingdom * 2);
		TestEqual(
			FString::Printf(TEXT("Frozen scale N=%d damage count"), PopulationPerKingdom),
			LargeDamage.DamagedResidents.Num(),
			PopulationPerKingdom * 30 / 100);
		TestEqual(
			FString::Printf(TEXT("Frozen scale N=%d Persistent count"), PopulationPerKingdom),
			LargePersistentPool.Residents.Num(),
			PersistentPoolSize);
	}

	const FString TestRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AILOD/Phase0/Determinism"));
	const FString RunAPath = FPaths::Combine(TestRoot, TEXT("RunA"));
	const FString RunBPath = FPaths::Combine(TestRoot, TEXT("RunB"));
	TestTrue(TEXT("Run A artifacts save"), FPhase0ManifestGenerator::SaveArtifacts(RunAPath, PopulationA, DamageA, PersistentPoolA, Error));
	TestTrue(TEXT("Run B artifacts save"), FPhase0ManifestGenerator::SaveArtifacts(RunBPath, PopulationB, DamageB, PersistentPoolB, Error));

	FString SavedPopulationA;
	FString SavedPopulationB;
	FString SavedDamageA;
	FString SavedDamageB;
	FString SavedPersistentA;
	FString SavedPersistentB;
	TestTrue(TEXT("Run A population artifact loads"), FFileHelper::LoadFileToString(SavedPopulationA, *(RunAPath / LogSchema::InitialPopulationManifestFile)));
	TestTrue(TEXT("Run B population artifact loads"), FFileHelper::LoadFileToString(SavedPopulationB, *(RunBPath / LogSchema::InitialPopulationManifestFile)));
	TestTrue(TEXT("Run A damage artifact loads"), FFileHelper::LoadFileToString(SavedDamageA, *(RunAPath / LogSchema::EarthquakeDamageListFile)));
	TestTrue(TEXT("Run B damage artifact loads"), FFileHelper::LoadFileToString(SavedDamageB, *(RunBPath / LogSchema::EarthquakeDamageListFile)));
	TestTrue(TEXT("Run A Persistent artifact loads"), FFileHelper::LoadFileToString(SavedPersistentA, *(RunAPath / LogSchema::PersistentTestPoolFile)));
	TestTrue(TEXT("Run B Persistent artifact loads"), FFileHelper::LoadFileToString(SavedPersistentB, *(RunBPath / LogSchema::PersistentTestPoolFile)));
	TestEqual(TEXT("Saved population artifacts are byte-identical"), SavedPopulationA, SavedPopulationB);
	TestEqual(TEXT("Saved damage artifacts are byte-identical"), SavedDamageA, SavedDamageB);
	TestEqual(TEXT("Saved Persistent artifacts are byte-identical"), SavedPersistentA, SavedPersistentB);

	AddInfo(FString::Printf(
		TEXT("Phase 0 v1.3 artifacts written to %s. ConfigHash=%s PopulationCRC=%08X DamageCRC=%08X PersistentCRC=%08X"),
		*TestRoot,
		*PopulationA.ConfigHash,
		FCrc::StrCrc32(*SavedPopulationA),
		FCrc::StrCrc32(*SavedDamageA),
		FCrc::StrCrc32(*SavedPersistentA)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAILODPhase0LogSchemaTest,
	"AILODResearch.Phase0.LogSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAILODPhase0LogSchemaTest::RunTest(const FString& Parameters)
{
	using namespace AILOD;
	using namespace AILOD::LogSchema;

	auto ValidateTable = [this](
		const TCHAR* TableName,
		const FFieldDefinition* Fields,
		const int32 FieldCount,
		const FFieldDefinition* RequiredFields,
		const int32 RequiredFieldCount)
	{
		TestTrue(FString::Printf(TEXT("%s has fields"), TableName), FieldCount > 0);
		TSet<FString> Names;
		for (int32 FieldIndex = 0; FieldIndex < FieldCount; ++FieldIndex)
		{
			const FFieldDefinition& Field = Fields[FieldIndex];
			TestTrue(FString::Printf(TEXT("%s field name is set"), TableName), Field.Name != nullptr && Field.Name[0] != TEXT('\0'));
			TestTrue(FString::Printf(TEXT("%s field type is set"), TableName), Field.Type != nullptr && Field.Type[0] != TEXT('\0'));
			TestTrue(FString::Printf(TEXT("%s has no duplicate field %s"), TableName, Field.Name), !Names.Contains(Field.Name));
			Names.Add(Field.Name);
		}

		for (int32 RequiredIndex = 0; RequiredIndex < RequiredFieldCount; ++RequiredIndex)
		{
			TestTrue(
				FString::Printf(TEXT("%s includes required field %s"), TableName, RequiredFields[RequiredIndex].Name),
				Names.Contains(RequiredFields[RequiredIndex].Name));
		}
	};

	ValidateTable(TEXT("InitialPopulation"), InitialPopulationFields, UE_ARRAY_COUNT(InitialPopulationFields), PreRunCommonFields, UE_ARRAY_COUNT(PreRunCommonFields));
	ValidateTable(TEXT("EarthquakeDamage"), EarthquakeDamageFields, UE_ARRAY_COUNT(EarthquakeDamageFields), PreRunCommonFields, UE_ARRAY_COUNT(PreRunCommonFields));
	ValidateTable(TEXT("PersistentTestPool"), PersistentTestPoolFields, UE_ARRAY_COUNT(PersistentTestPoolFields), PreRunCommonFields, UE_ARRAY_COUNT(PreRunCommonFields));
	ValidateTable(TEXT("RunManifest"), RunManifestFields, UE_ARRAY_COUNT(RunManifestFields), CommonFields, UE_ARRAY_COUNT(CommonFields));
	ValidateTable(TEXT("KingdomTimeseries"), KingdomTimeseriesFields, UE_ARRAY_COUNT(KingdomTimeseriesFields), CommonFields, UE_ARRAY_COUNT(CommonFields));
	ValidateTable(TEXT("CohortTimeseries"), CohortTimeseriesFields, UE_ARRAY_COUNT(CohortTimeseriesFields), CommonFields, UE_ARRAY_COUNT(CommonFields));
	ValidateTable(TEXT("NPCSnapshot"), NPCSnapshotFields, UE_ARRAY_COUNT(NPCSnapshotFields), CommonFields, UE_ARRAY_COUNT(CommonFields));
	ValidateTable(TEXT("SimulationEvent"), SimulationEventFields, UE_ARRAY_COUNT(SimulationEventFields), CommonFields, UE_ARRAY_COUNT(CommonFields));
	ValidateTable(TEXT("LODTransition"), LODTransitionFields, UE_ARRAY_COUNT(LODTransitionFields), CommonFields, UE_ARRAY_COUNT(CommonFields));
	ValidateTable(TEXT("LedgerTransaction"), LedgerTransactionFields, UE_ARRAY_COUNT(LedgerTransactionFields), CommonFields, UE_ARRAY_COUNT(CommonFields));
	ValidateTable(TEXT("Performance"), PerformanceFields, UE_ARRAY_COUNT(PerformanceFields), CommonFields, UE_ARRAY_COUNT(CommonFields));
	ValidateTable(TEXT("MetricsSummary"), MetricsSummaryFields, UE_ARRAY_COUNT(MetricsSummaryFields), CommonFields, UE_ARRAY_COUNT(CommonFields));

	const TSet<uint32> StreamTags =
	{
		RandomStreams::PopulationComposition,
		RandomStreams::InitialCash,
		RandomStreams::EarthquakeDamage,
		RandomStreams::PersistentSelection
	};
	TestEqual(TEXT("All four frozen random stream tags are unique"), StreamTags.Num(), 4);
	TestEqual(TEXT("Spec version is v1.3"), FString(SpecVersion), FString(TEXT("1.3")));
	TestEqual(TEXT("Schema version is v1.1"), FString(SchemaVersion), FString(TEXT("1.1")));

	return true;
}

#endif
