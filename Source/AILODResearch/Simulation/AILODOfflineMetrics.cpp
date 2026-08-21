// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODOfflineMetrics.h"

#include "AILODLogSchema.h"
#include "AILODPhase0Types.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace AILOD
{
	namespace
	{
		struct FKingdomMetricRow
		{
			FString Key;
			double Values[7] = {};
		};

		struct FOfflineRun
		{
			FString Directory;
			FString ExperimentID;
			FString RunID;
			FString Method;
			FString Scenario;
			FString Mode;
			FString FinalGameTime;
			int32 Seed = 0;
			int32 PopulationPerKingdom = 0;
			TMap<FString, double> HardErrors;
			TMap<FString, FKingdomMetricRow> KingdomRows;
			TMap<FString, TArray<FString>> NPCSnapshots;
			double Behaviors[8] = {};
			TArray<double> AICpuSamples;
			TArray<double> MacroCpuSamples;
			TArray<double> MicroCpuSamples;
			TArray<double> TransitionCpuSamples;
			TArray<double> MemorySamples;
			TArray<double> ActiveCountSamples;
			TArray<double> QueueLengthSamples;
		};

		struct FMetricRow
		{
			const FOfflineRun* Run = nullptr;
			FString Name;
			double Value = 0.0;
			FString Scale;
			FString OraclePairRunID;
		};

		constexpr const TCHAR* TrajectoryNames[] =
		{
			TEXT("DamagedWaiting"), TEXT("UnderRepair"), TEXT("Repaired"), TEXT("ForestWood"),
			TEXT("MarketWoodAvailable"), TEXT("WoodPrice"), TEXT("TreasuryAvailable")
		};

		FString CsvString(const FString& Value)
		{
			return FString::Printf(TEXT("\"%s\""), *Value.Replace(TEXT("\""), TEXT("\"\"")));
		}

		bool ParseCsvLine(const FString& Line, TArray<FString>& OutFields)
		{
			OutFields.Reset();
			FString Field;
			bool bQuoted = false;
			for (int32 Index = 0; Index < Line.Len(); ++Index)
			{
				const TCHAR Character = Line[Index];
				if (bQuoted && Character == TEXT('"'))
				{
					if (Index + 1 < Line.Len() && Line[Index + 1] == TEXT('"'))
					{
						Field.AppendChar(TEXT('"'));
						++Index;
					}
					else bQuoted = false;
				}
				else if (!bQuoted && Character == TEXT('"') && Field.IsEmpty()) bQuoted = true;
				else if (!bQuoted && Character == TEXT(','))
				{
					OutFields.Add(MoveTemp(Field));
					Field.Reset();
				}
				else Field.AppendChar(Character);
			}
			if (bQuoted) return false;
			OutFields.Add(MoveTemp(Field));
			return true;
		}

		TMap<FString, int32> MakeHeaderMap(const TArray<FString>& Fields)
		{
			TMap<FString, int32> Result;
			for (int32 Index = 0; Index < Fields.Num(); ++Index) Result.Add(Fields[Index], Index);
			return Result;
		}

		bool RequireColumns(const TMap<FString, int32>& Header, const TArray<FString>& Names)
		{
			for (const FString& Name : Names) if (!Header.Contains(Name)) return false;
			return true;
		}

		bool LoadCsv(const FString& Path, TArray<TArray<FString>>& OutRows, TMap<FString, int32>& OutHeader, FString& OutError)
		{
			FString Text;
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				OutError = FString::Printf(TEXT("Missing required raw file: %s"), *Path);
				return false;
			}
			Text.ParseIntoArrayLines(Lines, true);
			TArray<FString> HeaderFields;
			if (Lines.IsEmpty() || !ParseCsvLine(Lines[0], HeaderFields))
			{
				OutError = FString::Printf(TEXT("Invalid CSV header: %s"), *Path);
				return false;
			}
			OutHeader = MakeHeaderMap(HeaderFields);
			OutRows.Reset();
			for (int32 Index = 1; Index < Lines.Num(); ++Index)
			{
				TArray<FString> Fields;
				if (!ParseCsvLine(Lines[Index], Fields) || Fields.Num() != HeaderFields.Num())
				{
					OutError = FString::Printf(TEXT("Invalid CSV row %d: %s"), Index, *Path);
					return false;
				}
				OutRows.Add(MoveTemp(Fields));
			}
			return true;
		}

		bool LoadJson(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
		{
			FString Text;
			return FFileHelper::LoadFileToString(Text, *Path)
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), OutObject)
				&& OutObject.IsValid();
		}

		bool LoadRun(const FString& Directory, FOfflineRun& OutRun, FString& OutError)
		{
			using namespace LogSchema;
			if (!FPaths::FileExists(FPaths::Combine(Directory, RunManifestFile)))
			{
				OutError = FString::Printf(TEXT("Run %s is missing required raw file %s."), *Directory, RunManifestFile);
				return false;
			}

			TSharedPtr<FJsonObject> Manifest;
			if (!LoadJson(FPaths::Combine(Directory, RunManifestFile), Manifest))
			{
				OutError = FString::Printf(TEXT("Run %s has an invalid run_manifest.json."), *Directory);
				return false;
			}
			const TSharedPtr<FJsonObject>* Parameters = nullptr;
			const TSharedPtr<FJsonObject>* HardErrors = nullptr;
			if (!Manifest->TryGetObjectField(TEXT("parameters"), Parameters) || Parameters == nullptr
				|| !Manifest->TryGetObjectField(TEXT("hard_errors"), HardErrors) || HardErrors == nullptr)
			{
				OutError = FString::Printf(TEXT("Run %s manifest lacks parameters or hard_errors."), *Directory);
				return false;
			}
			OutRun.Directory = Directory;
			OutRun.ExperimentID = Manifest->GetStringField(TEXT("experiment_id"));
			OutRun.RunID = Manifest->GetStringField(TEXT("run_id"));
			OutRun.Method = Manifest->GetStringField(TEXT("method"));
			OutRun.Scenario = Manifest->GetStringField(TEXT("scenario"));
			OutRun.Mode = (*Parameters)->GetStringField(TEXT("run_mode"));
			OutRun.Seed = static_cast<int32>(Manifest->GetNumberField(TEXT("seed")));
			OutRun.FinalGameTime = Manifest->GetStringField(TEXT("game_time"));
			OutRun.PopulationPerKingdom = static_cast<int32>((*Parameters)->GetNumberField(TEXT("population_per_kingdom")));
			for (const TCHAR* Name : { TEXT("task_reset"), TEXT("duplicate_completion"), TEXT("event_owner_conflict"), TEXT("duplicate_transaction"), TEXT("negative_stock"), TEXT("population_residual"), TEXT("wood_residual") })
			{
				OutRun.HardErrors.Add(Name, (*HardErrors)->GetNumberField(Name));
			}
			const bool bPerformance = OutRun.Mode == TEXT("Performance");
			if (!bPerformance && OutRun.Mode != TEXT("Accuracy") && OutRun.Mode != TEXT("Validation"))
			{
				OutError = FString::Printf(TEXT("Run %s has unsupported run mode %s."), *Directory, *OutRun.Mode);
				return false;
			}
			const TArray<FString> RequiredFiles = bPerformance
				? TArray<FString>{ PerformanceFile }
				: TArray<FString>{ KingdomTimeseriesFile, CohortTimeseriesFile, NPCSnapshotsFile, SimulationEventsFile, LODTransitionsFile, LedgerTransactionsFile };
			for (const FString& File : RequiredFiles)
			{
				if (!FPaths::FileExists(FPaths::Combine(Directory, File)))
				{
					OutError = FString::Printf(TEXT("Run %s is missing required raw file %s."), *Directory, *File);
					return false;
				}
			}

			TArray<TArray<FString>> Rows;
			TMap<FString, int32> Header;
			if (bPerformance)
			{
				if (!LoadCsv(FPaths::Combine(Directory, PerformanceFile), Rows, Header, OutError)
					|| !RequireColumns(Header, { TEXT("experiment_id"), TEXT("run_id"), TEXT("method"), TEXT("scenario"), TEXT("seed"), TEXT("ai_cpu_ms"), TEXT("macro_cpu_ms"), TEXT("micro_cpu_ms"), TEXT("transition_cpu_ms"), TEXT("memory_mb"), TEXT("active_count"), TEXT("queue_length") }))
				{
					if (OutError.IsEmpty()) OutError = TEXT("performance_1s.csv is missing metric columns.");
					return false;
				}
				for (const TArray<FString>& Row : Rows)
				{
					if (Row[Header[TEXT("experiment_id")]] != OutRun.ExperimentID
						|| Row[Header[TEXT("run_id")]] != OutRun.RunID
						|| Row[Header[TEXT("method")]] != OutRun.Method
						|| Row[Header[TEXT("scenario")]] != OutRun.Scenario
						|| FCString::Atoi(*Row[Header[TEXT("seed")]]) != OutRun.Seed)
					{
						OutError = FString::Printf(TEXT("Run %s has inconsistent performance sample identity."), *Directory);
						return false;
					}
					OutRun.AICpuSamples.Add(FCString::Atod(*Row[Header[TEXT("ai_cpu_ms")]]));
					OutRun.MacroCpuSamples.Add(FCString::Atod(*Row[Header[TEXT("macro_cpu_ms")]]));
					OutRun.MicroCpuSamples.Add(FCString::Atod(*Row[Header[TEXT("micro_cpu_ms")]]));
					OutRun.TransitionCpuSamples.Add(FCString::Atod(*Row[Header[TEXT("transition_cpu_ms")]]));
					OutRun.MemorySamples.Add(FCString::Atod(*Row[Header[TEXT("memory_mb")]]));
					OutRun.ActiveCountSamples.Add(FCString::Atod(*Row[Header[TEXT("active_count")]]));
					OutRun.QueueLengthSamples.Add(FCString::Atod(*Row[Header[TEXT("queue_length")]]));
				}
				if (OutRun.AICpuSamples.IsEmpty())
				{
					OutError = FString::Printf(TEXT("Run %s has no performance samples."), *Directory);
					return false;
				}
				return true;
			}
			if (!LoadCsv(FPaths::Combine(Directory, KingdomTimeseriesFile), Rows, Header, OutError)
				|| !RequireColumns(Header, { TEXT("game_time"), TEXT("kingdom"), TEXT("damaged_waiting"), TEXT("under_repair"), TEXT("repaired"), TEXT("forest_wood"), TEXT("market_wood_available"), TEXT("wood_price"), TEXT("treasury_available") }))
			{
				if (OutError.IsEmpty()) OutError = TEXT("kingdom_timeseries.csv is missing metric columns.");
				return false;
			}
			for (const TArray<FString>& Row : Rows)
			{
				if (Row[Header[TEXT("game_time")]].StartsWith(TEXT("D-"))) continue;
				FKingdomMetricRow Metric;
				Metric.Key = Row[Header[TEXT("game_time")]] + TEXT("|") + Row[Header[TEXT("kingdom")]];
				for (int32 Index = 0; Index < 7; ++Index)
				{
					const FString Column = Index == 0 ? TEXT("damaged_waiting") : Index == 1 ? TEXT("under_repair") : Index == 2 ? TEXT("repaired") : Index == 3 ? TEXT("forest_wood") : Index == 4 ? TEXT("market_wood_available") : Index == 5 ? TEXT("wood_price") : TEXT("treasury_available");
					Metric.Values[Index] = FCString::Atod(*Row[Header[Column]]);
				}
				OutRun.KingdomRows.Add(Metric.Key, Metric);
			}

			if (!LoadCsv(FPaths::Combine(Directory, NPCSnapshotsFile), Rows, Header, OutError)
				|| !RequireColumns(Header, { TEXT("game_time"), TEXT("persistent_id"), TEXT("name"), TEXT("home_id"), TEXT("kingdom"), TEXT("profession"), TEXT("income_band"), TEXT("money"), TEXT("repair_credit"), TEXT("inventory_wood"), TEXT("home_state"), TEXT("current_goal"), TEXT("event_id"), TEXT("reservation_id"), TEXT("remaining_work_hours"), TEXT("first_action") }))
			{
				if (OutError.IsEmpty()) OutError = TEXT("npc_snapshots.csv is missing continuity columns.");
				return false;
			}
			const FString InputPath = FPaths::Combine(FPaths::GetPath(FPaths::GetPath(Directory)), TEXT("Inputs"), FString::Printf(TEXT("Seed-%d"), OutRun.Seed), InitialPopulationManifestFile);
			TSharedPtr<FJsonObject> InitialPopulation;
			if (!LoadJson(InputPath, InitialPopulation))
			{
				OutError = FString::Printf(TEXT("Run %s is missing its shared Phase 0 population input."), *Directory);
				return false;
			}
			TMap<FString, FString> InitialIdentities;
			for (const TSharedPtr<FJsonValue>& Value : InitialPopulation->GetArrayField(TEXT("residents")))
			{
				const TSharedPtr<FJsonObject>& Resident = Value->AsObject();
				const FString ID = FString::Printf(TEXT("%.0f"), Resident->GetNumberField(TEXT("persistent_id")));
				InitialIdentities.Add(ID, FString::Printf(TEXT("%s|%.0f|%s|%s|%s"),
					*Resident->GetStringField(TEXT("name")), Resident->GetNumberField(TEXT("home_id")),
					*Resident->GetStringField(TEXT("kingdom")), *Resident->GetStringField(TEXT("profession")),
					*Resident->GetStringField(TEXT("income_band"))));
			}

			const TArray<FString> ContinuityColumns = { TEXT("persistent_id"), TEXT("home_id"), TEXT("kingdom"), TEXT("profession"), TEXT("money"), TEXT("repair_credit"), TEXT("inventory_wood"), TEXT("home_state"), TEXT("event_id"), TEXT("remaining_work_hours"), TEXT("reservation_id"), TEXT("current_goal"), TEXT("first_action") };
			TSet<FString> IdentityMismatches;
			for (const TArray<FString>& Row : Rows)
			{
				const FString ID = Row[Header[TEXT("persistent_id")]];
				const FString ObservedIdentity = FString::Printf(TEXT("%s|%s|%s|%s|%s"),
					*Row[Header[TEXT("name")]], *Row[Header[TEXT("home_id")]], *Row[Header[TEXT("kingdom")]],
					*Row[Header[TEXT("profession")]], *Row[Header[TEXT("income_band")]]);
				if (InitialIdentities.FindRef(ID) != ObservedIdentity) IdentityMismatches.Add(ID);
				TArray<FString> Values;
				for (const FString& Column : ContinuityColumns) Values.Add(Row[Header[Column]]);
				OutRun.NPCSnapshots.Add(Row[Header[TEXT("game_time")]] + TEXT("|") + Row[Header[TEXT("persistent_id")]], MoveTemp(Values));
			}
			OutRun.HardErrors.Add(TEXT("identity_mismatch"), IdentityMismatches.Num());

			FString EventText;
			if (!FFileHelper::LoadFileToString(EventText, *FPaths::Combine(Directory, SimulationEventsFile)))
			{
				OutError = TEXT("Could not read simulation_events.jsonl.");
				return false;
			}
			TArray<FString> EventLines;
			EventText.ParseIntoArrayLines(EventLines, true);
			for (const FString& Line : EventLines)
			{
				TSharedPtr<FJsonObject> Event;
				if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Event) || !Event.IsValid())
				{
					OutError = TEXT("simulation_events.jsonl contains invalid JSON.");
					return false;
				}
				const FString Type = Event->GetStringField(TEXT("type"));
				if (Event->GetStringField(TEXT("game_time")).StartsWith(TEXT("D-"))) continue;
				double ParticipantCount = 1.0;
				if (!Event->TryGetNumberField(TEXT("participant_count"), ParticipantCount))
				{
					Event->TryGetNumberField(TEXT("participants"), ParticipantCount);
				}
				const double Count = FMath::Max(1.0, ParticipantCount);
				if (Type == TEXT("Routine")) OutRun.Behaviors[0] += Count;
				else if (Type == TEXT("Work")) OutRun.Behaviors[1] += Count;
				else if (Type == TEXT("BuyWood")) OutRun.Behaviors[2] += Count;
				else if (Type == TEXT("ChopWood")) OutRun.Behaviors[3] += Count;
				else if (Type == TEXT("Repair"))
				{
					OutRun.Behaviors[4] += Count;
					OutRun.Behaviors[5] += Count;
				}
				else if (Type == TEXT("Wait")) OutRun.Behaviors[6] += Count;
				else if (Type == TEXT("RepairAidPayment")) OutRun.Behaviors[7] += Count;
			}
			return true;
		}

		FString RunKey(const FString& Method, const FString& Scenario, const int32 Seed)
		{
			return FString::Printf(TEXT("%s|%s|%d"), *Method, *Scenario, Seed);
		}

		FString PerformanceKey(const FString& Method, const FString& Scenario, const int32 Seed, const int32 PopulationPerKingdom)
		{
			return FString::Printf(TEXT("%s|%s|%d|%d"), *Method, *Scenario, Seed, PopulationPerKingdom);
		}

		double ScaleFor(const int32 Index, const int32 N)
		{
			if (Index <= 2) return N;
			if (Index == 3) return 16.0 * N;
			if (Index == 4) return 2.0 * N;
			if (Index == 5) return 1.0;
			return 5.0 * N;
		}

		FString ScaleNameFor(const int32 Index, const int32 N)
		{
			if (Index <= 2) return FString::Printf(TEXT("N=%d"), N);
			if (Index == 3) return FString::Printf(TEXT("16N=%d"), 16 * N);
			if (Index == 4) return FString::Printf(TEXT("2N=%d"), 2 * N);
			if (Index == 5) return TEXT("P0=1");
			return FString::Printf(TEXT("5N=%d"), 5 * N);
		}

		void AddTrajectoryMetrics(const FOfflineRun& Run, const FOfflineRun& Oracle, TArray<FMetricRow>& OutRows)
		{
			for (int32 MetricIndex = 0; MetricIndex < 7; ++MetricIndex)
			{
				double Sum = 0.0;
				int32 Count = 0;
				for (const TPair<FString, FKingdomMetricRow>& Pair : Run.KingdomRows)
				{
					if (const FKingdomMetricRow* OracleRow = Oracle.KingdomRows.Find(Pair.Key))
					{
						Sum += FMath::Abs(Pair.Value.Values[MetricIndex] - OracleRow->Values[MetricIndex]);
						++Count;
					}
				}
				OutRows.Add({ &Run, FString::Printf(TEXT("Trajectory.%s"), TrajectoryNames[MetricIndex]), Count > 0 ? Sum / Count / ScaleFor(MetricIndex, Run.PopulationPerKingdom) : 0.0, ScaleNameFor(MetricIndex, Run.PopulationPerKingdom), Oracle.RunID });
			}
		}

		bool AddPolicyMetrics(const FOfflineRun& Run, const TMap<FString, const FOfflineRun*>& Index, const FOfflineRun& OraclePolicy, TArray<FMetricRow>& OutRows, FString& OutError)
		{
			if (Run.Scenario == TEXT("None")) return true;
			const FOfflineRun* MethodNone = Index.FindRef(RunKey(Run.Method, TEXT("None"), Run.Seed));
			const FOfflineRun* OracleNone = Index.FindRef(RunKey(TEXT("Oracle"), TEXT("None"), Run.Seed));
			if (MethodNone == nullptr || OracleNone == nullptr)
			{
				OutError = FString::Printf(TEXT("Policy metric for %s requires matching Method/None and Oracle/None runs."), *Run.RunID);
				return false;
			}
			for (int32 MetricIndex = 0; MetricIndex < 7; ++MetricIndex)
			{
				double Sum = 0.0;
				int32 Count = 0;
				for (const TPair<FString, FKingdomMetricRow>& Pair : Run.KingdomRows)
				{
					const FKingdomMetricRow* MethodBase = MethodNone->KingdomRows.Find(Pair.Key);
					const FKingdomMetricRow* OraclePolicyRow = OraclePolicy.KingdomRows.Find(Pair.Key);
					const FKingdomMetricRow* OracleBase = OracleNone->KingdomRows.Find(Pair.Key);
					if (MethodBase && OraclePolicyRow && OracleBase)
					{
						const double MethodEffect = Pair.Value.Values[MetricIndex] - MethodBase->Values[MetricIndex];
						const double OracleEffect = OraclePolicyRow->Values[MetricIndex] - OracleBase->Values[MetricIndex];
						Sum += FMath::Abs(MethodEffect - OracleEffect);
						++Count;
					}
				}
				OutRows.Add({ &Run, FString::Printf(TEXT("PolicyEffect.%s"), TrajectoryNames[MetricIndex]), Count > 0 ? Sum / Count / ScaleFor(MetricIndex, Run.PopulationPerKingdom) : 0.0, ScaleNameFor(MetricIndex, Run.PopulationPerKingdom), OraclePolicy.RunID });
			}
			return true;
		}

		void AddBehaviorMetric(const FOfflineRun& Run, const FOfflineRun& Oracle, TArray<FMetricRow>& OutRows)
		{
			double RunTotal = 0.0;
			double OracleTotal = 0.0;
			for (int32 Index = 0; Index < 8; ++Index)
			{
				RunTotal += Run.Behaviors[Index];
				OracleTotal += Oracle.Behaviors[Index];
			}
			double TVD = 0.0;
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const double P = RunTotal > 0.0 ? Run.Behaviors[Index] / RunTotal : 0.0;
				const double Q = OracleTotal > 0.0 ? Oracle.Behaviors[Index] / OracleTotal : 0.0;
				TVD += FMath::Abs(P - Q);
			}
			OutRows.Add({ &Run, TEXT("Behavior.TVD"), 0.5 * TVD, TEXT("fixed_8_categories"), Oracle.RunID });
		}

		void AddContinuityMetrics(const FOfflineRun& Run, const FOfflineRun& Oracle, TArray<FMetricRow>& OutRows)
		{
			const TCHAR* Names[] = { TEXT("PersistentID"), TEXT("HomeID"), TEXT("Kingdom"), TEXT("Profession"), TEXT("Money"), TEXT("RepairCredit"), TEXT("InventoryWood"), TEXT("HomeState"), TEXT("EventID"), TEXT("TaskProgress"), TEXT("ReservationID"), TEXT("CurrentGoal"), TEXT("FirstAction") };
			int32 Compared = 0;
			int32 Mismatches[UE_ARRAY_COUNT(Names)] = {};
			double MoneyAbsoluteError = 0.0;
			double MoneyNormalizedError = 0.0;
			double RepairCreditAbsoluteError = 0.0;
			double RepairCreditNormalizedError = 0.0;
			double InventoryWoodAbsoluteError = 0.0;
			double InventoryWoodNormalizedError = 0.0;
			int32 TaskActiveStatusMismatches = 0;
			int32 ComparableActiveTasks = 0;
			double TaskRemainingHoursAbsoluteError = 0.0;
			int32 CommitmentSnapshots = 0;
			int32 CommitmentTaskActiveStatusMismatches = 0;
			int32 ComparableCommitmentTasks = 0;
			double CommitmentTaskRemainingHoursAbsoluteError = 0.0;
			for (const TPair<FString, TArray<FString>>& Pair : Run.NPCSnapshots)
			{
				if (const TArray<FString>* OracleValues = Oracle.NPCSnapshots.Find(Pair.Key))
				{
					++Compared;
					for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
					{
						if (Pair.Value[Index] != (*OracleValues)[Index]) ++Mismatches[Index];
					}
					const double Money = FCString::Atod(*Pair.Value[4]);
					const double OracleMoney = FCString::Atod(*(*OracleValues)[4]);
					const double RepairCredit = FCString::Atod(*Pair.Value[5]);
					const double OracleRepairCredit = FCString::Atod(*(*OracleValues)[5]);
					const double InventoryWood = FCString::Atod(*Pair.Value[6]);
					const double OracleInventoryWood = FCString::Atod(*(*OracleValues)[6]);
					MoneyAbsoluteError += FMath::Abs(Money - OracleMoney);
					MoneyNormalizedError += FMath::Abs(Money - OracleMoney) / FMath::Max(1.0, FMath::Abs(OracleMoney));
					RepairCreditAbsoluteError += FMath::Abs(RepairCredit - OracleRepairCredit);
					RepairCreditNormalizedError += FMath::Abs(RepairCredit - OracleRepairCredit) / FMath::Max(1.0, FMath::Abs(OracleRepairCredit));
					InventoryWoodAbsoluteError += FMath::Abs(InventoryWood - OracleInventoryWood);
					InventoryWoodNormalizedError += FMath::Abs(InventoryWood - OracleInventoryWood) / FMath::Max(1.0, FMath::Abs(OracleInventoryWood));

					const double RemainingHours = FCString::Atod(*Pair.Value[9]);
					const double OracleRemainingHours = FCString::Atod(*(*OracleValues)[9]);
					const bool bTaskActive = RemainingHours > UE_DOUBLE_SMALL_NUMBER;
					const bool bOracleTaskActive = OracleRemainingHours > UE_DOUBLE_SMALL_NUMBER;
					if (bTaskActive != bOracleTaskActive) ++TaskActiveStatusMismatches;
					if (bTaskActive && bOracleTaskActive && Pair.Value[11] == (*OracleValues)[11])
					{
						TaskRemainingHoursAbsoluteError += FMath::Abs(RemainingHours - OracleRemainingHours);
						++ComparableActiveTasks;
					}
					const bool bCommitmentGoal = Pair.Value[11] == TEXT("RestoreHome");
					const bool bOracleCommitmentGoal = (*OracleValues)[11] == TEXT("RestoreHome");
					if (bCommitmentGoal || bOracleCommitmentGoal)
					{
						++CommitmentSnapshots;
						if (bTaskActive != bOracleTaskActive) ++CommitmentTaskActiveStatusMismatches;
						if (bCommitmentGoal && bOracleCommitmentGoal && bTaskActive && bOracleTaskActive)
						{
							CommitmentTaskRemainingHoursAbsoluteError += FMath::Abs(
								RemainingHours - OracleRemainingHours);
							++ComparableCommitmentTasks;
						}
					}
				}
			}
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
			{
				const bool bInternalDiagnostic = Index == 8 || Index == 9 || Index == 10;
				const FString MetricName = bInternalDiagnostic
					? FString::Printf(TEXT("Continuity.Diagnostic.%sExactMismatchRate"), Names[Index])
					: FString::Printf(TEXT("Continuity.%sMismatchRate"), Names[Index]);
				const FString MetricScale = bInternalDiagnostic
					? FString::Printf(TEXT("paired_snapshots=%d;internal_representation_only"), Compared)
					: FString::Printf(TEXT("paired_snapshots=%d"), Compared);
				OutRows.Add({
					&Run,
					MetricName,
					Compared > 0 ? static_cast<double>(Mismatches[Index]) / Compared : 0.0,
					MetricScale,
					Oracle.RunID });
			}
			const double Denominator = Compared > 0 ? static_cast<double>(Compared) : 1.0;
			OutRows.Add({ &Run, TEXT("Continuity.PairedSnapshotCount"), static_cast<double>(Compared), TEXT("count"), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.MoneyMAE"), MoneyAbsoluteError / Denominator, TEXT("coin_per_paired_snapshot"), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.MoneyNormalizedMAE"), MoneyNormalizedError / Denominator, TEXT("abs_error_div_max_1_abs_oracle"), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.RepairCreditMAE"), RepairCreditAbsoluteError / Denominator, TEXT("coin_per_paired_snapshot"), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.RepairCreditNormalizedMAE"), RepairCreditNormalizedError / Denominator, TEXT("abs_error_div_max_1_abs_oracle"), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.InventoryWoodMAE"), InventoryWoodAbsoluteError / Denominator, TEXT("wood_per_paired_snapshot"), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.InventoryWoodNormalizedMAE"), InventoryWoodNormalizedError / Denominator, TEXT("abs_error_div_max_1_abs_oracle"), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.TaskActiveStatusMismatchRate"), Compared > 0 ? static_cast<double>(TaskActiveStatusMismatches) / Compared : 0.0, FString::Printf(TEXT("paired_snapshots=%d"), Compared), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.TaskRemainingHoursMAE"), ComparableActiveTasks > 0 ? TaskRemainingHoursAbsoluteError / ComparableActiveTasks : 0.0, FString::Printf(TEXT("hours;paired_same_goal_active_snapshots=%d"), ComparableActiveTasks), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.CommitmentTaskActiveStatusMismatchRate"), CommitmentSnapshots > 0 ? static_cast<double>(CommitmentTaskActiveStatusMismatches) / CommitmentSnapshots : 0.0, FString::Printf(TEXT("restore_home_relevant_paired_snapshots=%d"), CommitmentSnapshots), Oracle.RunID });
			OutRows.Add({ &Run, TEXT("Continuity.CommitmentTaskRemainingHoursMAE"), ComparableCommitmentTasks > 0 ? CommitmentTaskRemainingHoursAbsoluteError / ComparableCommitmentTasks : 0.0, FString::Printf(TEXT("hours;paired_restore_home_active_snapshots=%d"), ComparableCommitmentTasks), Oracle.RunID });
		}

		void AddHardErrors(const FOfflineRun& Run, TArray<FMetricRow>& OutRows)
		{
			for (const TPair<FString, double>& Pair : Run.HardErrors)
			{
				OutRows.Add({ &Run, FString::Printf(TEXT("HardError.%s"), *Pair.Key), Pair.Value, TEXT("count;target=0"), TEXT("") });
			}
		}

		double Mean(const TArray<double>& Values)
		{
			double Total = 0.0;
			for (const double Value : Values) Total += Value;
			return Values.IsEmpty() ? 0.0 : Total / Values.Num();
		}

		double Sum(const TArray<double>& Values)
		{
			double Total = 0.0;
			for (const double Value : Values) Total += Value;
			return Total;
		}

		double Percentile(const TArray<double>& Values, const double Fraction)
		{
			if (Values.IsEmpty()) return 0.0;
			TArray<double> Sorted = Values;
			Sorted.Sort();
			const int32 Index = FMath::Clamp(FMath::CeilToInt(Fraction * Sorted.Num()) - 1, 0, Sorted.Num() - 1);
			return Sorted[Index];
		}

		void AddPerformanceSeries(const FOfflineRun& Run, const TCHAR* Name, const TArray<double>& Values, const FString& Scale, TArray<FMetricRow>& OutRows)
		{
			OutRows.Add({ &Run, FString::Printf(TEXT("Performance.%s.Mean"), Name), Mean(Values), Scale, TEXT("") });
			OutRows.Add({ &Run, FString::Printf(TEXT("Performance.%s.P95"), Name), Percentile(Values, 0.95), Scale, TEXT("") });
			OutRows.Add({ &Run, FString::Printf(TEXT("Performance.%s.P99"), Name), Percentile(Values, 0.99), Scale, TEXT("") });
			OutRows.Add({ &Run, FString::Printf(TEXT("Performance.%s.Max"), Name), Percentile(Values, 1.0), Scale, TEXT("") });
		}

		void AddPerformanceMetrics(const FOfflineRun& Run, TArray<FMetricRow>& OutRows)
		{
			OutRows.Add({ &Run, TEXT("Performance.SampleCount"), static_cast<double>(Run.AICpuSamples.Num()), TEXT("one_second_wall_buckets;final_bucket_may_be_partial"), TEXT("") });
			AddPerformanceSeries(Run, TEXT("AICpuMs"), Run.AICpuSamples, TEXT("ms_per_wall_bucket"), OutRows);
			AddPerformanceSeries(Run, TEXT("MacroCpuMs"), Run.MacroCpuSamples, TEXT("ms_per_wall_bucket"), OutRows);
			AddPerformanceSeries(Run, TEXT("MicroCpuMs"), Run.MicroCpuSamples, TEXT("ms_per_wall_bucket"), OutRows);
			AddPerformanceSeries(Run, TEXT("TransitionCpuMs"), Run.TransitionCpuSamples, TEXT("ms_per_wall_bucket"), OutRows);
			OutRows.Add({ &Run, TEXT("Performance.AICpuMs.Total"), Sum(Run.AICpuSamples), TEXT("full_67_game_day_production_cpu_ms"), TEXT("") });
			OutRows.Add({ &Run, TEXT("Performance.MacroCpuMs.Total"), Sum(Run.MacroCpuSamples), TEXT("full_67_game_day_macro_cpu_ms"), TEXT("") });
			OutRows.Add({ &Run, TEXT("Performance.MicroCpuMs.Total"), Sum(Run.MicroCpuSamples), TEXT("full_67_game_day_micro_cpu_ms"), TEXT("") });
			OutRows.Add({ &Run, TEXT("Performance.TransitionCpuMs.Total"), Sum(Run.TransitionCpuSamples), TEXT("full_67_game_day_transition_cpu_ms"), TEXT("") });
			OutRows.Add({ &Run, TEXT("Performance.MemoryMB.Mean"), Mean(Run.MemorySamples), TEXT("process_used_physical_mb"), TEXT("") });
			OutRows.Add({ &Run, TEXT("Performance.MemoryMB.Peak"), Percentile(Run.MemorySamples, 1.0), TEXT("process_used_physical_mb"), TEXT("") });
			OutRows.Add({ &Run, TEXT("Performance.ActiveCount.Max"), Percentile(Run.ActiveCountSamples, 1.0), TEXT("count"), TEXT("") });
			OutRows.Add({ &Run, TEXT("Performance.QueueLength.Max"), Percentile(Run.QueueLengthSamples, 1.0), TEXT("count"), TEXT("") });
			AddHardErrors(Run, OutRows);
		}

		FString BuildCsv(const TArray<FMetricRow>& Rows)
		{
			FString Output = TEXT("schema_version,experiment_id,run_id,method,scenario,seed,game_time,metric_name,metric_value,scale,oracle_pair_run_id,ci_input\n");
			for (const FMetricRow& Metric : Rows)
			{
				const FOfflineRun& Run = *Metric.Run;
				const TArray<FString> Fields =
				{
					CsvString(SchemaVersion), CsvString(Run.ExperimentID), CsvString(Run.RunID),
					CsvString(Run.Method), CsvString(Run.Scenario), FString::FromInt(Run.Seed),
					CsvString(Run.FinalGameTime), CsvString(Metric.Name), FString::Printf(TEXT("%.12f"), Metric.Value),
					CsvString(Metric.Scale), CsvString(Metric.OraclePairRunID),
					CsvString(FString::Printf(TEXT("paired_seed=%d"), Run.Seed))
				};
				Output += FString::Join(Fields, TEXT(",")) + TEXT("\n");
			}
			return Output;
		}
	}

	bool FOfflineMetricsEvaluator::BuildSummary(const FString& ExperimentRoot, const FString& OutputPath, FString& OutError)
	{
		const FString RunsRoot = FPaths::Combine(ExperimentRoot, TEXT("Runs"));
		TArray<FString> Directories;
		IFileManager::Get().FindFiles(Directories, *FPaths::Combine(RunsRoot, TEXT("*")), false, true);
		Directories.Sort();
		if (Directories.IsEmpty())
		{
			OutError = TEXT("Offline metrics found no run directories.");
			return false;
		}

		TArray<FOfflineRun> Runs;
		TMap<FString, const FOfflineRun*> Index;
		Runs.SetNum(Directories.Num());
		for (int32 RunIndex = 0; RunIndex < Directories.Num(); ++RunIndex)
		{
			if (!LoadRun(FPaths::Combine(RunsRoot, Directories[RunIndex]), Runs[RunIndex], OutError)) return false;
		}

		TArray<FMetricRow> Metrics;
		const bool bPerformance = Runs[0].Mode == TEXT("Performance");
		for (const FOfflineRun& Run : Runs)
		{
			if ((Run.Mode == TEXT("Performance")) != bPerformance)
			{
				OutError = TEXT("Offline metrics cannot mix Accuracy/Validation and Performance runs in one experiment root.");
				return false;
			}
		}
		if (bPerformance)
		{
			TMap<FString, const FOfflineRun*> PerformanceIndex;
			for (const FOfflineRun& Run : Runs)
			{
				PerformanceIndex.Add(PerformanceKey(Run.Method, Run.Scenario, Run.Seed, Run.PopulationPerKingdom), &Run);
				AddPerformanceMetrics(Run, Metrics);
			}
			for (const FOfflineRun& Run : Runs)
			{
				const FOfflineRun* PerAgent = PerformanceIndex.FindRef(PerformanceKey(TEXT("PerAgent"), Run.Scenario, Run.Seed, Run.PopulationPerKingdom));
				if (PerAgent == nullptr)
				{
					OutError = FString::Printf(TEXT("Performance run %s has no matching PerAgent baseline."), *Run.RunID);
					return false;
				}
				const double RunMean = Mean(Run.AICpuSamples);
				const double RunP95 = Percentile(Run.AICpuSamples, 0.95);
				const double RunTotal = Sum(Run.AICpuSamples);
				Metrics.Add({ &Run, TEXT("Performance.SpeedupVsPerAgent.MeanAI"), RunMean > 0.0 ? Mean(PerAgent->AICpuSamples) / RunMean : 0.0, FString::Printf(TEXT("baseline=%s"), *PerAgent->RunID), TEXT("") });
				Metrics.Add({ &Run, TEXT("Performance.SpeedupVsPerAgent.P95AI"), RunP95 > 0.0 ? Percentile(PerAgent->AICpuSamples, 0.95) / RunP95 : 0.0, FString::Printf(TEXT("baseline=%s"), *PerAgent->RunID), TEXT("") });
				Metrics.Add({ &Run, TEXT("Performance.SpeedupVsPerAgent.TotalAI"), RunTotal > 0.0 ? Sum(PerAgent->AICpuSamples) / RunTotal : 0.0, FString::Printf(TEXT("baseline=%s;full_67_game_day_production_cpu_ms"), *PerAgent->RunID), TEXT("") });
			}
		}
		else
		{
			for (const FOfflineRun& Run : Runs)
			{
				const FString Key = RunKey(Run.Method, Run.Scenario, Run.Seed);
				if (Index.Contains(Key))
				{
					OutError = FString::Printf(TEXT("Duplicate method/scenario/seed run: %s"), *Key);
					return false;
				}
				Index.Add(Key, &Run);
			}
			for (const FOfflineRun& Run : Runs)
			{
				const FOfflineRun* Oracle = Index.FindRef(RunKey(TEXT("Oracle"), Run.Scenario, Run.Seed));
				if (Oracle == nullptr)
				{
					OutError = FString::Printf(TEXT("Run %s has no paired Oracle run."), *Run.RunID);
					return false;
				}
				AddTrajectoryMetrics(Run, *Oracle, Metrics);
				if (!AddPolicyMetrics(Run, Index, *Oracle, Metrics, OutError)) return false;
				AddBehaviorMetric(Run, *Oracle, Metrics);
				AddContinuityMetrics(Run, *Oracle, Metrics);
				AddHardErrors(Run, Metrics);
			}
		}
		if (!FFileHelper::SaveStringToFile(BuildCsv(Metrics), *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Could not write metrics summary: %s"), *OutputPath);
			return false;
		}
		OutError.Reset();
		return true;
	}
}
