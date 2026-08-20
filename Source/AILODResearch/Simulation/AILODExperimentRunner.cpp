// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODExperimentRunner.h"

#include "AILODExperimentLogging.h"
#include "AILODLogSchema.h"
#include "AILODPhase0Manifest.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
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
			const FUnifiedRunLogMetadata& Metadata,
			FExperimentRunRecord& OutRun,
			FString& OutError)
		{
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
			return true;
		}
	}

	bool FExperimentRunner::RunMatrix(
		const FExperimentMatrixRequest& Request,
		TArray<FExperimentRunRecord>& OutRuns,
		FString& OutError)
	{
		OutRuns.Reset();
		if (Request.OutputRoot.IsEmpty() || Request.ExperimentID.IsEmpty()
			|| Request.Methods.IsEmpty() || Request.Scenarios.IsEmpty() || Request.Seeds.IsEmpty()
			|| Request.GitCommit.IsEmpty() || Request.UEVersion.IsEmpty() || Request.BuildType.IsEmpty()
			|| Request.Hardware.IsEmpty() || Request.StartTime.IsEmpty() || Request.EndTime.IsEmpty())
		{
			OutError = TEXT("Experiment matrix requires output, identity, at least one method/scenario/seed, and complete environment metadata.");
			return false;
		}
		FUnifiedRunOptions Options;
		Options.Mode = Request.Mode;
		Options.bRetainCompletedEvents = Request.Mode != EUnifiedRunMode::Performance;
		Options.bRecordSnapshots = Request.Mode != EUnifiedRunMode::Performance;
		Options.bVerifyCohortApproximation = Request.Mode == EUnifiedRunMode::Validation;
		Options.bEnableMacroProfiling = Request.bEnableMacroProfiling;
		Options.bEnableV17ShadowCohort = Request.bEnableV17ShadowCohort;
		Options.ProposedModelVersion = Request.ProposedModelVersion;
		for (const int32 Seed : Request.Seeds)
		{
			FPhase0Config Config;
			Config.Seed = Seed;
			Config.PopulationPerKingdom = Request.PopulationPerKingdom;
			FString PopulationHash;
			FString DamageHash;
			FString PoolHash;
			const FString InputDirectory = FPaths::Combine(Request.OutputRoot, TEXT("Inputs"), FString::Printf(TEXT("Seed-%d"), Seed));
			if (!GenerateInputs(InputDirectory, Config, PopulationHash, DamageHash, PoolHash, OutError))
			{
				return false;
			}

			for (const EUnifiedSimulationMethod Method : Request.Methods)
			{
				for (const EStage2Scenario Scenario : Request.Scenarios)
				{
					FUnifiedRunLogMetadata Metadata;
					Metadata.ExperimentID = Request.ExperimentID;
					Metadata.RunID = FString::Printf(TEXT("%s-%s-%d"), ToString(Method), ToString(Scenario), Seed);
					Metadata.OutputDirectory = FPaths::Combine(Request.OutputRoot, TEXT("Runs"), Metadata.RunID);
					Metadata.PopulationManifestSHA256 = PopulationHash;
					Metadata.DamageListSHA256 = DamageHash;
					Metadata.PersistentPoolSHA256 = PoolHash;
					Metadata.GitCommit = Request.GitCommit;
					Metadata.UEVersion = Request.UEVersion;
					Metadata.BuildType = Request.BuildType;
					Metadata.Hardware = Request.Hardware;
					Metadata.LogMode = Request.LogMode;
					Metadata.StartTime = Request.StartTime;
					Metadata.EndTime = Request.EndTime;
					FExperimentRunRecord& Run = OutRuns.AddDefaulted_GetRef();
					if (!RunOne(Config, Method, Scenario, Options, Metadata, Run, OutError))
					{
						OutRuns.Pop();
						return false;
					}
				}
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
			&& Manifest->GetStringField(TEXT("authority_mode")) == TEXT("v1.7_authoritative"))
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
