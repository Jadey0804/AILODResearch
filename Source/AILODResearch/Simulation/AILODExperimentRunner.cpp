// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODExperimentRunner.h"

#include "AILODExperimentLogging.h"
#include "AILODLogSchema.h"
#include "AILODPhase0Manifest.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace AILOD
{
	namespace
	{
		uint32 RotateRight(const uint32 Value, const uint32 Bits)
		{
			return (Value >> Bits) | (Value << (32u - Bits));
		}

		FString HashSHA256(const FString& Text)
		{
			FTCHARToUTF8 Utf8(*Text);
			TArray<uint8> Data;
			Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			const uint64 BitLength = static_cast<uint64>(Data.Num()) * 8u;
			Data.Add(0x80u);
			while (Data.Num() % 64 != 56) Data.Add(0u);
			for (int32 Shift = 56; Shift >= 0; Shift -= 8) Data.Add(static_cast<uint8>(BitLength >> Shift));

			static constexpr uint32 K[64] =
			{
				0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
				0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
				0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
				0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
				0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
				0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
				0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
				0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
			};
			uint32 H[8] = { 0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u };
			for (int32 Offset = 0; Offset < Data.Num(); Offset += 64)
			{
				uint32 W[64] = {};
				for (int32 Index = 0; Index < 16; ++Index)
				{
					const int32 Byte = Offset + Index * 4;
					W[Index] = static_cast<uint32>(Data[Byte]) << 24 | static_cast<uint32>(Data[Byte + 1]) << 16 | static_cast<uint32>(Data[Byte + 2]) << 8 | Data[Byte + 3];
				}
				for (int32 Index = 16; Index < 64; ++Index)
				{
					const uint32 S0 = RotateRight(W[Index - 15], 7) ^ RotateRight(W[Index - 15], 18) ^ (W[Index - 15] >> 3);
					const uint32 S1 = RotateRight(W[Index - 2], 17) ^ RotateRight(W[Index - 2], 19) ^ (W[Index - 2] >> 10);
					W[Index] = W[Index - 16] + S0 + W[Index - 7] + S1;
				}
				uint32 A = H[0], B = H[1], C = H[2], D = H[3], E = H[4], F = H[5], G = H[6], HH = H[7];
				for (int32 Index = 0; Index < 64; ++Index)
				{
					const uint32 S1 = RotateRight(E, 6) ^ RotateRight(E, 11) ^ RotateRight(E, 25);
					const uint32 Choice = (E & F) ^ (~E & G);
					const uint32 Temp1 = HH + S1 + Choice + K[Index] + W[Index];
					const uint32 S0 = RotateRight(A, 2) ^ RotateRight(A, 13) ^ RotateRight(A, 22);
					const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
					const uint32 Temp2 = S0 + Majority;
					HH = G; G = F; F = E; E = D + Temp1; D = C; C = B; B = A; A = Temp1 + Temp2;
				}
				H[0] += A; H[1] += B; H[2] += C; H[3] += D; H[4] += E; H[5] += F; H[6] += G; H[7] += HH;
			}
			FString Result;
			for (const uint32 Value : H) Result += FString::Printf(TEXT("%08X"), Value);
			return Result;
		}

		bool ParseMethod(const FString& Text, EUnifiedSimulationMethod& OutMethod)
		{
			for (const EUnifiedSimulationMethod Method : { EUnifiedSimulationMethod::Oracle, EUnifiedSimulationMethod::Proposed, EUnifiedSimulationMethod::PerAgent, EUnifiedSimulationMethod::Simple })
			{
				if (Text == ToString(Method))
				{
					OutMethod = Method;
					return true;
				}
			}
			return false;
		}

		bool ParseScenario(const FString& Text, EStage2Scenario& OutScenario)
		{
			for (const EStage2Scenario Scenario : { EStage2Scenario::None, EStage2Scenario::HarvestCap, EStage2Scenario::StateImport, EStage2Scenario::RepairAid })
			{
				if (Text == ToString(Scenario))
				{
					OutScenario = Scenario;
					return true;
				}
			}
			return false;
		}

		bool ParseRunMode(const FString& Text, EUnifiedRunMode& OutMode)
		{
			if (Text == TEXT("Validation")) OutMode = EUnifiedRunMode::Validation;
			else if (Text == TEXT("Accuracy")) OutMode = EUnifiedRunMode::Accuracy;
			else if (Text == TEXT("Performance")) OutMode = EUnifiedRunMode::Performance;
			else return false;
			return true;
		}

		FString CsvString(const FString& Value)
		{
			return FString::Printf(TEXT("\"%s\""), *Value.Replace(TEXT("\""), TEXT("\"\"")));
		}

		bool IsGitCommit(const FString& Value)
		{
			if (Value.Len() < 7 || Value.Len() > 40) return false;
			for (const TCHAR Character : Value)
			{
				if (!FChar::IsHexDigit(Character)) return false;
			}
			return true;
		}

		struct FInputHashes
		{
			FString Population;
			FString Damage;
			FString Pool;
		};

		bool WriteSchedule(
			const FExperimentMatrixRequest& Request,
			const TArray<FExperimentRunPlanEntry>& Schedule,
			FString& OutError)
		{
			if (!IFileManager::Get().MakeDirectory(*Request.OutputRoot, true))
			{
				OutError = TEXT("Experiment runner could not create its output root.");
				return false;
			}
			FString Csv = TEXT("experiment_protocol_version,experiment_id,schedule_index,run_id,method,scenario,seed,repeat_index,order_seed,randomized\n");
			for (const FExperimentRunPlanEntry& Entry : Schedule)
			{
				Csv += FString::Printf(
					TEXT("\"1.0\",%s,%d,%s,%s,%s,%d,%d,%d,%s\n"),
					*CsvString(Request.ExperimentID),
					Entry.ScheduleIndex,
					*CsvString(Entry.RunID),
					*CsvString(ToString(Entry.Method)),
					*CsvString(ToString(Entry.Scenario)),
					Entry.Seed,
					Entry.RepeatIndex,
					Request.OrderSeed,
					Request.bRandomizeRunOrder ? TEXT("true") : TEXT("false"));
			}
			if (!FFileHelper::SaveStringToFile(
				Csv,
				*FPaths::Combine(Request.OutputRoot, LogSchema::RunScheduleFile),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutError = TEXT("Experiment runner could not write run_schedule.csv.");
				return false;
			}
			return true;
		}

		void AppendFailure(
			const FExperimentMatrixRequest& Request,
			const FExperimentRunPlanEntry& Entry,
			const FString& Error)
		{
			const FString Path = FPaths::Combine(Request.OutputRoot, LogSchema::RunFailuresFile);
			FString Csv;
			if (!FPaths::FileExists(Path))
			{
				Csv = TEXT("experiment_protocol_version,experiment_id,schedule_index,run_id,repeat_index,error\n");
			}
			Csv += FString::Printf(
				TEXT("\"1.0\",%s,%d,%s,%d,%s\n"),
				*CsvString(Request.ExperimentID),
				Entry.ScheduleIndex,
				*CsvString(Entry.RunID),
				Entry.RepeatIndex,
				*CsvString(Error));
			FFileHelper::SaveStringToFile(
				Csv,
				*Path,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(),
				FILEWRITE_Append);
		}

		bool TryLoadCompletedRun(
			const FExperimentMatrixRequest& Request,
			const FExperimentRunPlanEntry& Entry,
			const FInputHashes& Hashes,
			FExperimentRunRecord& OutRun,
			bool& bOutFound,
			FString& OutError)
		{
			bOutFound = false;
			const FString RunDirectory = FPaths::Combine(Request.OutputRoot, TEXT("Runs"), Entry.RunID);
			const FString ManifestPath = FPaths::Combine(RunDirectory, LogSchema::RunManifestFile);
			if (!FPaths::FileExists(ManifestPath))
			{
				if (IFileManager::Get().DirectoryExists(*RunDirectory))
				{
					OutError = FString::Printf(TEXT("Resume found an incomplete run directory without a manifest: %s"), *Entry.RunID);
					return false;
				}
				return true;
			}
			FString Text;
			TSharedPtr<FJsonObject> Manifest;
			if (!FFileHelper::LoadFileToString(Text, *ManifestPath)
				|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest)
				|| !Manifest.IsValid())
			{
				OutError = FString::Printf(TEXT("Resume could not parse the manifest for %s."), *Entry.RunID);
				return false;
			}
			const bool bMatches = Manifest->HasTypedField<EJson::Boolean>(TEXT("valid"))
				&& Manifest->GetBoolField(TEXT("valid"))
				&& Manifest->GetStringField(TEXT("experiment_id")) == Request.ExperimentID
				&& Manifest->GetStringField(TEXT("run_id")) == Entry.RunID
				&& Manifest->GetStringField(TEXT("method")) == ToString(Entry.Method)
				&& Manifest->GetStringField(TEXT("scenario")) == ToString(Entry.Scenario)
				&& static_cast<int32>(Manifest->GetNumberField(TEXT("seed"))) == Entry.Seed
				&& Manifest->GetStringField(TEXT("population_manifest_sha256")) == Hashes.Population
				&& Manifest->GetStringField(TEXT("damage_list_sha256")) == Hashes.Damage
				&& Manifest->GetStringField(TEXT("persistent_pool_sha256")) == Hashes.Pool
				&& Manifest->GetStringField(TEXT("git_commit")) == Request.GitCommit
				&& Manifest->GetStringField(TEXT("build_type")) == Request.BuildType
				&& Manifest->GetStringField(TEXT("hardware")) == Request.Hardware
				&& Manifest->HasTypedField<EJson::Number>(TEXT("schedule_index"))
				&& static_cast<int32>(Manifest->GetNumberField(TEXT("schedule_index"))) == Entry.ScheduleIndex
				&& Manifest->HasTypedField<EJson::Number>(TEXT("repeat_index"))
				&& static_cast<int32>(Manifest->GetNumberField(TEXT("repeat_index"))) == Entry.RepeatIndex
				&& Manifest->HasTypedField<EJson::Boolean>(TEXT("formal_run_requested"))
				&& Manifest->GetBoolField(TEXT("formal_run_requested")) == Request.bFormalRunRequested
				&& Manifest->HasTypedField<EJson::String>(TEXT("deterministic_digest"))
				&& !Manifest->GetStringField(TEXT("deterministic_digest")).IsEmpty();
			if (!bMatches)
			{
				OutError = FString::Printf(TEXT("Resume refused a mismatched or invalid completed run: %s"), *Entry.RunID);
				return false;
			}
			const TSharedPtr<FJsonObject>* RunParameters = nullptr;
			if (!Manifest->TryGetObjectField(TEXT("parameters"), RunParameters) || RunParameters == nullptr)
			{
				OutError = FString::Printf(TEXT("Resume manifest is missing parameters: %s"), *Entry.RunID);
				return false;
			}
			EUnifiedRunMode Mode;
			if (!ParseRunMode((*RunParameters)->GetStringField(TEXT("run_mode")), Mode))
			{
				OutError = FString::Printf(TEXT("Resume manifest has an invalid run mode: %s"), *Entry.RunID);
				return false;
			}
			OutRun = {};
			OutRun.RunID = Entry.RunID;
			OutRun.RunDirectory = RunDirectory;
			OutRun.DeterministicDigest = Manifest->GetStringField(TEXT("deterministic_digest"));
			OutRun.Mode = Mode;
			OutRun.PopulationPerKingdom = static_cast<int32>((*RunParameters)->GetNumberField(TEXT("population_per_kingdom")));
			OutRun.bHardErrorFree = true;
			const TSharedPtr<FJsonObject>* Measurements = nullptr;
			const TSharedPtr<FJsonObject>* TrackedMemory = nullptr;
			if (Manifest->TryGetObjectField(TEXT("measurement_summary"), Measurements) && Measurements != nullptr
				&& (*Measurements)->TryGetObjectField(TEXT("tracked_authority_memory_bytes"), TrackedMemory)
				&& TrackedMemory != nullptr)
			{
				auto Bytes = [TrackedMemory](const TCHAR* Field)
				{
					return static_cast<uint64>((*TrackedMemory)->GetNumberField(Field));
				};
				OutRun.V17TrackedMemory.AuthorityFixedBytes = Bytes(TEXT("authority_fixed_bytes"));
				OutRun.V17TrackedMemory.IdentityRegistryBytes = Bytes(TEXT("identity_registry_bytes"));
				OutRun.V17TrackedMemory.HomeContinuityBytes = (*TrackedMemory)->HasField(TEXT("home_continuity_bytes"))
					? Bytes(TEXT("home_continuity_bytes"))
					: 0;
				OutRun.V17TrackedMemory.JointStateBytes = Bytes(TEXT("joint_state_bytes"));
				OutRun.V17TrackedMemory.ActiveStateBytes = Bytes(TEXT("active_state_bytes"));
				OutRun.V17TrackedMemory.CapsuleBytes = Bytes(TEXT("continuity_capsule_bytes"));
				OutRun.V17TrackedMemory.ParticipantRefBytes = Bytes(TEXT("participant_ref_bytes"));
				OutRun.V17TrackedMemory.BatchClaimBytes = Bytes(TEXT("batch_claim_bytes"));
				OutRun.V17TrackedMemory.BatchEventBytes = Bytes(TEXT("batch_event_bytes"));
				OutRun.V17TrackedMemory.SystemEventBytes = Bytes(TEXT("system_event_bytes"));
				OutRun.V17TrackedMemory.LedgerBytes = Bytes(TEXT("ledger_bytes"));
				OutRun.V17TrackedMemory.ReservationBytes = Bytes(TEXT("reservation_bytes"));
				OutRun.V17TrackedMemory.EventStoreBytes = Bytes(TEXT("event_store_bytes"));
				OutRun.V17TrackedMemory.SchedulerBytes = Bytes(TEXT("scheduler_bytes"));
				OutRun.V17TrackedMemory.LODTransitionBytes = Bytes(TEXT("lod_transition_bytes"));
				OutRun.V17TrackedMemory.TotalBytes = Bytes(TEXT("total_tracked_authority_bytes"));
			}
			OutRun.ScheduleIndex = Entry.ScheduleIndex;
			OutRun.RepeatIndex = Entry.RepeatIndex;
			OutRun.bSkippedExisting = true;
			bOutFound = true;
			return true;
		}

		bool GenerateInputs(
			const FString& InputDirectory,
			const FPhase0Config& Config,
			FString& OutPopulationHash,
			FString& OutDamageHash,
			FString& OutPoolHash,
			FString& OutError)
		{
			FInitialPopulationManifest Population;
			FEarthquakeDamageList Damage;
			FPersistentTestPool Pool;
			if (!FPhase0ManifestGenerator::Generate(Config, Population, Damage, Pool, OutError)
				|| !FPhase0ManifestGenerator::SaveArtifacts(InputDirectory, Population, Damage, Pool, OutError))
			{
				return false;
			}
			OutPopulationHash = HashSHA256(FPhase0ManifestGenerator::SerializePopulation(Population));
			OutDamageHash = HashSHA256(FPhase0ManifestGenerator::SerializeDamage(Damage));
			OutPoolHash = HashSHA256(FPhase0ManifestGenerator::SerializePersistentPool(Pool));
			return true;
		}

		bool RunOne(
			const FPhase0Config& Config,
			const EUnifiedSimulationMethod Method,
			const EStage2Scenario Scenario,
			const FUnifiedRunOptions& BaseOptions,
			FUnifiedRunLogMetadata Metadata,
			FExperimentRunRecord& OutRun,
			FString& OutError)
		{
			if (Metadata.StartTime.IsEmpty()) Metadata.StartTime = FDateTime::UtcNow().ToIso8601();
			FUnifiedRunLogWriter Writer;
			FUnifiedRunOptions Options = BaseOptions;
			if (Options.Mode != EUnifiedRunMode::Performance)
			{
				Options.Observer = &Writer;
				Options.EventSink = &Writer;
			}
			FUnifiedSimulationSession Session(Config, Method, Scenario, Options);
			if (!Session.Initialize(OutError))
			{
				return false;
			}
			TArray<FUnifiedPerformanceSample> PerformanceSamples;
			FUnifiedPerformanceSample CurrentSample;
			int32 CurrentSampleSteps = 0;
			double SampleStart = FPlatformTime::Seconds();
			auto FlushPerformanceSample = [&PerformanceSamples, &CurrentSample, &CurrentSampleSteps]()
			{
				if (CurrentSampleSteps <= 0) return;
				CurrentSample.MemoryMB = static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0);
				PerformanceSamples.Add(CurrentSample);
				CurrentSample = {};
				CurrentSampleSteps = 0;
			};
			while (!Session.IsComplete())
			{
				if (!Session.StepHour(OutError))
				{
					return false;
				}
				if (Options.Mode == EUnifiedRunMode::Performance)
				{
					const FUnifiedStepMeasurement& Step = Session.GetLastStepMeasurement();
					CurrentSample.GameTime = Step.GameTime;
					CurrentSample.AICpuMs += Step.ProductionCpuMs;
					CurrentSample.MacroCpuMs += Step.MacroCpuMs;
					CurrentSample.MicroCpuMs += Step.MicroCpuMs;
					CurrentSample.TransitionCpuMs += Step.TransitionCpuMs;
					CurrentSample.ActiveCount = Step.ActiveCount;
					CurrentSample.QueueLength = Step.QueueLength;
					++CurrentSampleSteps;
					const double Now = FPlatformTime::Seconds();
					if (Now - SampleStart >= 1.0)
					{
						FlushPerformanceSample();
						SampleStart = Now;
					}
				}
			}
			if (Options.Mode == EUnifiedRunMode::Performance) FlushPerformanceSample();
			FUnifiedRunResult Result;
			if (!Session.Finalize(Result, OutError))
			{
				return false;
			}
			Result.PerformanceSamples = MoveTemp(PerformanceSamples);
			if (Metadata.EndTime.IsEmpty()) Metadata.EndTime = FDateTime::UtcNow().ToIso8601();
			if (!Writer.WriteRun(Result, Metadata, OutError))
			{
				return false;
			}
			OutRun.RunID = Metadata.RunID;
			OutRun.RunDirectory = Metadata.OutputDirectory;
			OutRun.DeterministicDigest = FUnifiedSimulationRunner::BuildDeterministicDigest(Result);
			OutRun.Mode = Result.Mode;
			OutRun.PopulationPerKingdom = Result.PopulationPerKingdom;
			OutRun.bHardErrorFree = Result.IsHardErrorFree();
			OutRun.PerformanceSampleCount = Result.PerformanceSamples.Num();
			OutRun.Diagnostics = Result.Diagnostics;
			OutRun.CostBreakdown = Result.CostBreakdown;
			OutRun.MacroProfile = Result.MacroProfile;
			OutRun.V17ShadowProfile = Result.V17ShadowProfile;
			OutRun.V17TrackedMemory = Result.V17TrackedMemory;
			OutRun.ScheduleIndex = Metadata.ScheduleIndex;
			OutRun.RepeatIndex = Metadata.RepeatIndex;
			OutRun.bSkippedExisting = false;
			return true;
		}
	}

	bool FExperimentRunner::BuildSchedule(
		const FExperimentMatrixRequest& Request,
		TArray<FExperimentRunPlanEntry>& OutSchedule,
		FString& OutError)
	{
		OutSchedule.Reset();
		if (Request.Methods.IsEmpty() || Request.Scenarios.IsEmpty() || Request.Seeds.IsEmpty()
			|| Request.RepeatCount <= 0)
		{
			OutError = TEXT("Experiment schedule requires at least one method, scenario, seed, and repeat.");
			return false;
		}
		for (int32 RepeatIndex = 1; RepeatIndex <= Request.RepeatCount; ++RepeatIndex)
		{
			for (const int32 Seed : Request.Seeds)
			{
				for (const EUnifiedSimulationMethod Method : Request.Methods)
				{
					for (const EStage2Scenario Scenario : Request.Scenarios)
					{
						FExperimentRunPlanEntry& Entry = OutSchedule.AddDefaulted_GetRef();
						Entry.Method = Method;
						Entry.Scenario = Scenario;
						Entry.Seed = Seed;
						Entry.RepeatIndex = RepeatIndex;
						const FString BaseID = FString::Printf(TEXT("%s-%s-%d"), ToString(Method), ToString(Scenario), Seed);
						Entry.RunID = Request.RepeatCount > 1
							? FString::Printf(TEXT("%s-R%02d"), *BaseID, RepeatIndex)
							: BaseID;
					}
				}
			}
		}
		if (Request.bRandomizeRunOrder)
		{
			FRandomStream OrderStream(Request.OrderSeed);
			for (int32 Index = OutSchedule.Num() - 1; Index > 0; --Index)
			{
				OutSchedule.Swap(Index, OrderStream.RandRange(0, Index));
			}
		}
		TSet<FString> RunIDs;
		for (int32 Index = 0; Index < OutSchedule.Num(); ++Index)
		{
			FExperimentRunPlanEntry& Entry = OutSchedule[Index];
			Entry.ScheduleIndex = Index + 1;
			if (RunIDs.Contains(Entry.RunID))
			{
				OutError = FString::Printf(TEXT("Experiment schedule produced a duplicate RunID: %s"), *Entry.RunID);
				OutSchedule.Reset();
				return false;
			}
			RunIDs.Add(Entry.RunID);
		}
		OutError.Reset();
		return true;
	}

	bool FExperimentRunner::RunMatrix(
		const FExperimentMatrixRequest& Request,
		TArray<FExperimentRunRecord>& OutRuns,
		FString& OutError)
	{
		OutRuns.Reset();
		if (Request.Mode == EUnifiedRunMode::Demo)
		{
			OutError = TEXT("Interactive Demo runs are excluded from the formal experiment runner and its output directories.");
			return false;
		}
		if (Request.OutputRoot.IsEmpty() || Request.ExperimentID.IsEmpty()
			|| Request.GitCommit.IsEmpty() || Request.UEVersion.IsEmpty() || Request.BuildType.IsEmpty()
			|| Request.Hardware.IsEmpty())
		{
			OutError = TEXT("Experiment matrix requires output, identity, and complete build/hardware metadata.");
			return false;
		}
		if (Request.bFormalRunRequested
			&& (!Request.bFormalEnvironmentEligible
				|| !Request.bRandomizeRunOrder
				|| Request.OrderSeed == 0
				|| !IsGitCommit(Request.GitCommit)
				|| !Request.BuildType.Contains(TEXT("Shipping"), ESearchCase::IgnoreCase)))
		{
			OutError = TEXT("Formal runs require an approved Shipping environment, a 7-40 digit hexadecimal Git commit, and deterministic randomized order with a non-zero order seed.");
			return false;
		}

		TArray<FExperimentRunPlanEntry> Schedule;
		if (!BuildSchedule(Request, Schedule, OutError) || !WriteSchedule(Request, Schedule, OutError))
		{
			return false;
		}

		TMap<int32, FInputHashes> InputHashes;
		for (const int32 Seed : Request.Seeds)
		{
			if (InputHashes.Contains(Seed)) continue;
			FPhase0Config Config;
			Config.Seed = Seed;
			Config.PopulationPerKingdom = Request.PopulationPerKingdom;
			FInputHashes& Hashes = InputHashes.Add(Seed);
			const FString InputDirectory = FPaths::Combine(Request.OutputRoot, TEXT("Inputs"), FString::Printf(TEXT("Seed-%d"), Seed));
			if (!GenerateInputs(InputDirectory, Config, Hashes.Population, Hashes.Damage, Hashes.Pool, OutError))
			{
				return false;
			}
		}

		FUnifiedRunOptions Options;
		Options.Mode = Request.Mode;
		Options.bRetainCompletedEvents = Request.Mode != EUnifiedRunMode::Performance;
		Options.bRecordSnapshots = Request.Mode != EUnifiedRunMode::Performance;
		Options.bVerifyCohortApproximation = Request.Mode == EUnifiedRunMode::Validation;
		Options.bEnableMacroProfiling = Request.bEnableMacroProfiling;
		Options.bEnableV17ShadowCohort = Request.bEnableV17ShadowCohort;
		Options.ProposedModelVersion = Request.ProposedModelVersion;

		for (const FExperimentRunPlanEntry& Entry : Schedule)
		{
			const FInputHashes& Hashes = InputHashes.FindChecked(Entry.Seed);
			FExperimentRunRecord& Run = OutRuns.AddDefaulted_GetRef();
			if (Request.bResumeCompletedRuns)
			{
				bool bFound = false;
				if (!TryLoadCompletedRun(Request, Entry, Hashes, Run, bFound, OutError))
				{
					AppendFailure(Request, Entry, OutError);
					OutRuns.Pop();
					return false;
				}
				if (bFound) continue;
			}

			FPhase0Config Config;
			Config.Seed = Entry.Seed;
			Config.PopulationPerKingdom = Request.PopulationPerKingdom;
			FUnifiedRunLogMetadata Metadata;
			Metadata.ExperimentID = Request.ExperimentID;
			Metadata.RunID = Entry.RunID;
			Metadata.OutputDirectory = FPaths::Combine(Request.OutputRoot, TEXT("Runs"), Metadata.RunID);
			Metadata.PopulationManifestSHA256 = Hashes.Population;
			Metadata.DamageListSHA256 = Hashes.Damage;
			Metadata.PersistentPoolSHA256 = Hashes.Pool;
			Metadata.GitCommit = Request.GitCommit;
			Metadata.UEVersion = Request.UEVersion;
			Metadata.BuildType = Request.BuildType;
			Metadata.Hardware = Request.Hardware;
			Metadata.LogMode = Request.LogMode;
			Metadata.StartTime = Request.StartTime;
			Metadata.EndTime = Request.EndTime;
			Metadata.bFormalRunRequested = Request.bFormalRunRequested;
			Metadata.bFormalEnvironmentEligible = Request.bFormalEnvironmentEligible;
			Metadata.ScheduleIndex = Entry.ScheduleIndex;
			Metadata.RunOrdinal = Entry.ScheduleIndex;
			Metadata.RepeatIndex = Entry.RepeatIndex;
			Metadata.OrderSeed = Request.OrderSeed;
			Metadata.bRunOrderRandomized = Request.bRandomizeRunOrder;
			if (!RunOne(Config, Entry.Method, Entry.Scenario, Options, Metadata, Run, OutError))
			{
				AppendFailure(Request, Entry, OutError);
				OutRuns.Pop();
				return false;
			}
		}
		OutError.Reset();
		return true;
	}

	bool FExperimentRunner::ReplayFromManifest(
		const FString& ManifestPath,
		const FString& OutputDirectory,
		FExperimentRunRecord& OutRun,
		FString& OutError)
	{
		FString Text;
		TSharedPtr<FJsonObject> Manifest;
		if (!FFileHelper::LoadFileToString(Text, *ManifestPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest)
			|| !Manifest.IsValid())
		{
			OutError = TEXT("Replay could not read run_manifest.json.");
			return false;
		}
		const TSharedPtr<FJsonObject>* Parameters = nullptr;
		if (!Manifest->TryGetObjectField(TEXT("parameters"), Parameters) || Parameters == nullptr)
		{
			OutError = TEXT("Replay manifest is missing parameters.");
			return false;
		}
		EUnifiedSimulationMethod Method = EUnifiedSimulationMethod::Oracle;
		EStage2Scenario Scenario = EStage2Scenario::None;
		EUnifiedRunMode Mode = EUnifiedRunMode::Validation;
		if (!ParseMethod(Manifest->GetStringField(TEXT("method")), Method)
			|| !ParseScenario(Manifest->GetStringField(TEXT("scenario")), Scenario)
			|| !ParseRunMode((*Parameters)->GetStringField(TEXT("run_mode")), Mode))
		{
			OutError = TEXT("Replay manifest contains an unsupported method, scenario, or run mode.");
			return false;
		}

		FPhase0Config Config;
		Config.Seed = static_cast<int32>(Manifest->GetNumberField(TEXT("seed")));
		Config.PopulationPerKingdom = static_cast<int32>((*Parameters)->GetNumberField(TEXT("population_per_kingdom")));
		if (FPhase0ManifestGenerator::BuildConfigHash(Config) != Manifest->GetStringField(TEXT("config_hash")))
		{
			OutError = TEXT("Replay manifest config hash does not match its parameters.");
			return false;
		}
		FInitialPopulationManifest Population;
		FEarthquakeDamageList Damage;
		FPersistentTestPool Pool;
		if (!FPhase0ManifestGenerator::Generate(Config, Population, Damage, Pool, OutError)) return false;
		if (HashSHA256(FPhase0ManifestGenerator::SerializePopulation(Population)) != Manifest->GetStringField(TEXT("population_manifest_sha256"))
			|| HashSHA256(FPhase0ManifestGenerator::SerializeDamage(Damage)) != Manifest->GetStringField(TEXT("damage_list_sha256"))
			|| HashSHA256(FPhase0ManifestGenerator::SerializePersistentPool(Pool)) != Manifest->GetStringField(TEXT("persistent_pool_sha256")))
		{
			OutError = TEXT("Replay manifest input hashes do not match regenerated Phase 0 artifacts.");
			return false;
		}
		FUnifiedRunOptions Options;
		Options.Mode = Mode;
		Options.bRetainCompletedEvents = (*Parameters)->GetBoolField(TEXT("retain_completed_events"));
		Options.bRecordSnapshots = (*Parameters)->GetBoolField(TEXT("record_snapshots"));
		Options.bVerifyCohortApproximation = (*Parameters)->GetBoolField(TEXT("verify_cohort_approximation"));
		Options.bEnableMacroProfiling = (*Parameters)->HasField(TEXT("enable_macro_profiling"))
			&& (*Parameters)->GetBoolField(TEXT("enable_macro_profiling"));
		Options.bEnableV17ShadowCohort = (*Parameters)->HasField(TEXT("enable_v17_shadow_cohort"))
			&& (*Parameters)->GetBoolField(TEXT("enable_v17_shadow_cohort"));
		if (Manifest->HasTypedField<EJson::String>(TEXT("authority_mode"))
			&& (Manifest->GetStringField(TEXT("authority_mode")) == TEXT("v1.7_authoritative")
				|| Manifest->GetStringField(TEXT("authority_mode")) == TEXT("v1.9_home_continuity")))
		{
			Options.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
		}

		FUnifiedRunLogMetadata Metadata;
		Metadata.OutputDirectory = OutputDirectory;
		Metadata.ExperimentID = Manifest->GetStringField(TEXT("experiment_id"));
		Metadata.RunID = Manifest->GetStringField(TEXT("run_id"));
		Metadata.PopulationManifestSHA256 = Manifest->GetStringField(TEXT("population_manifest_sha256"));
		Metadata.DamageListSHA256 = Manifest->GetStringField(TEXT("damage_list_sha256"));
		Metadata.PersistentPoolSHA256 = Manifest->GetStringField(TEXT("persistent_pool_sha256"));
		Metadata.GitCommit = Manifest->GetStringField(TEXT("git_commit"));
		Metadata.UEVersion = Manifest->GetStringField(TEXT("ue_version"));
		Metadata.BuildType = Manifest->GetStringField(TEXT("build_type"));
		Metadata.Hardware = Manifest->GetStringField(TEXT("hardware"));
		Metadata.LogMode = Manifest->GetStringField(TEXT("log_mode"));
		Metadata.StartTime = Manifest->GetStringField(TEXT("start_time"));
		Metadata.EndTime = Manifest->GetStringField(TEXT("end_time"));
		Metadata.bFormalRunRequested = Manifest->HasTypedField<EJson::Boolean>(TEXT("formal_run_requested"))
			&& Manifest->GetBoolField(TEXT("formal_run_requested"));
		Metadata.bFormalEnvironmentEligible = Manifest->HasTypedField<EJson::Boolean>(TEXT("formal_environment_eligible"))
			&& Manifest->GetBoolField(TEXT("formal_environment_eligible"));
		Metadata.ExperimentProtocolVersion = Manifest->HasTypedField<EJson::String>(TEXT("experiment_protocol_version"))
			? Manifest->GetStringField(TEXT("experiment_protocol_version"))
			: TEXT("1.0");
		Metadata.ScheduleIndex = Manifest->HasTypedField<EJson::Number>(TEXT("schedule_index"))
			? static_cast<int32>(Manifest->GetNumberField(TEXT("schedule_index")))
			: 1;
		Metadata.RunOrdinal = Manifest->HasTypedField<EJson::Number>(TEXT("run_ordinal"))
			? static_cast<int32>(Manifest->GetNumberField(TEXT("run_ordinal")))
			: Metadata.ScheduleIndex;
		Metadata.RepeatIndex = Manifest->HasTypedField<EJson::Number>(TEXT("repeat_index"))
			? static_cast<int32>(Manifest->GetNumberField(TEXT("repeat_index")))
			: 1;
		Metadata.OrderSeed = Manifest->HasTypedField<EJson::Number>(TEXT("order_seed"))
			? static_cast<int32>(Manifest->GetNumberField(TEXT("order_seed")))
			: 0;
		Metadata.bRunOrderRandomized = Manifest->HasTypedField<EJson::Boolean>(TEXT("run_order_randomized"))
			&& Manifest->GetBoolField(TEXT("run_order_randomized"));
		if (!RunOne(Config, Method, Scenario, Options, Metadata, OutRun, OutError)) return false;
		if (!Manifest->HasTypedField<EJson::String>(TEXT("deterministic_digest"))
			|| OutRun.DeterministicDigest != Manifest->GetStringField(TEXT("deterministic_digest")))
		{
			OutError = TEXT("Replay completed but did not reproduce the manifest deterministic digest.");
			return false;
		}
		return true;
	}
}
