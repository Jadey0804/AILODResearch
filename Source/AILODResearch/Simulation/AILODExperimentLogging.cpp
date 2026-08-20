// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODExperimentLogging.h"

#include "AILODLogSchema.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace AILOD
{
	namespace
	{
		FString CsvString(const FString& Value)
		{
			FString Escaped = Value.Replace(TEXT("\""), TEXT("\"\""));
			return FString::Printf(TEXT("\"%s\""), *Escaped);
		}

		void AppendCsvRow(FString& Output, const TArray<FString>& Fields)
		{
			Output += FString::Join(Fields, TEXT(","));
			Output += TEXT("\n");
		}

		template <SIZE_T FieldCount>
		FString CsvHeader(const LogSchema::FFieldDefinition (&Fields)[FieldCount])
		{
			TArray<FString> Names;
			Names.Reserve(FieldCount);
			for (const LogSchema::FFieldDefinition& Field : Fields)
			{
				Names.Add(Field.Name);
			}
			return FString::Join(Names, TEXT(",")) + TEXT("\n");
		}

		TArray<FString> CommonCsvFields(
			const FUnifiedRunResult& Result,
			const FUnifiedRunLogMetadata& Metadata,
			const FSimulationTime GameTime)
		{
			return
			{
				CsvString(Result.LogSchemaVersion),
				CsvString(Metadata.ExperimentID),
				CsvString(Metadata.RunID),
				CsvString(ToString(Result.Method)),
				CsvString(ToString(Result.Scenario)),
				FString::FromInt(Result.Seed),
				CsvString(GameTime.ToString())
			};
		}

		const TCHAR* KingdomName(const EKingdom Kingdom)
		{
			return Kingdom == EKingdom::A ? TEXT("A") : TEXT("B");
		}

		const TCHAR* ProfessionName(const EProfession Profession)
		{
			return Profession == EProfession::Logger ? TEXT("Logger") : TEXT("Worker");
		}

		const TCHAR* IncomeBandName(const EIncomeBand IncomeBand)
		{
			return IncomeBand == EIncomeBand::Low ? TEXT("Low") : TEXT("NonLow");
		}

		const TCHAR* HomeStateName(const EHomeState HomeState)
		{
			switch (HomeState)
			{
			case EHomeState::Healthy: return TEXT("Healthy");
			case EHomeState::DamagedWaiting: return TEXT("DamagedWaiting");
			case EHomeState::UnderRepair: return TEXT("UnderRepair");
			case EHomeState::Repaired: return TEXT("Repaired");
			default: return TEXT("Unknown");
			}
		}

		const TCHAR* MacroIntentName(const EMacroIntent Intent)
		{
			switch (Intent)
			{
			case EMacroIntent::Routine: return TEXT("Routine");
			case EMacroIntent::Work: return TEXT("Work");
			case EMacroIntent::BuyWood: return TEXT("BuyWood");
			case EMacroIntent::ChopWood: return TEXT("ChopWood");
			case EMacroIntent::Repair: return TEXT("Repair");
			case EMacroIntent::Wait: return TEXT("Wait");
			default: return TEXT("Unknown");
			}
		}

		const TCHAR* ResourceName(const ESimulationResource Resource)
		{
			return Resource == ESimulationResource::Wood ? TEXT("Wood") : TEXT("Coin");
		}

		const TCHAR* RepresentationName(const EResidentRepresentation Representation)
		{
			return Representation == EResidentRepresentation::ActiveMicro ? TEXT("ActiveMicro") : TEXT("CohortManaged");
		}

		const TCHAR* TransitionResultName(const ELODTransitionResult Result)
		{
			switch (Result)
			{
			case ELODTransitionResult::Committed: return TEXT("Committed");
			case ELODTransitionResult::AlreadyInState: return TEXT("AlreadyInState");
			case ELODTransitionResult::ResidentNotFound: return TEXT("ResidentNotFound");
			case ELODTransitionResult::ActiveCapReached: return TEXT("ActiveCapReached");
			case ELODTransitionResult::EventOwnerConflict: return TEXT("EventOwnerConflict");
			case ELODTransitionResult::CohortCacheMismatch: return TEXT("CohortCacheMismatch");
			default: return TEXT("Unknown");
			}
		}

		const TCHAR* V17TransitionResultName(const EV17LODTransitionResult Result)
		{
			switch (Result)
			{
			case EV17LODTransitionResult::Committed: return TEXT("Committed");
			case EV17LODTransitionResult::ResidentNotFound: return TEXT("ResidentNotFound");
			case EV17LODTransitionResult::AlreadyActive: return TEXT("AlreadyActive");
			case EV17LODTransitionResult::AlreadyRestricted: return TEXT("AlreadyRestricted");
			case EV17LODTransitionResult::ActiveCapReached: return TEXT("ActiveCapReached");
			case EV17LODTransitionResult::NoEligibleJointCell: return TEXT("NoEligibleJointCell");
			case EV17LODTransitionResult::StateExtractionFailed: return TEXT("StateExtractionFailed");
			case EV17LODTransitionResult::EventSplitFailed: return TEXT("EventSplitFailed");
			case EV17LODTransitionResult::EventMergeFailed: return TEXT("EventMergeFailed");
			default: return TEXT("Unknown");
			}
		}

		const TCHAR* RunModeName(const EUnifiedRunMode Mode)
		{
			switch (Mode)
			{
			case EUnifiedRunMode::Validation: return TEXT("Validation");
			case EUnifiedRunMode::Accuracy: return TEXT("Accuracy");
			case EUnifiedRunMode::Performance: return TEXT("Performance");
			default: return TEXT("Unknown");
			}
		}

		const TCHAR* FaultInjectionName(const EUnifiedFaultInjectionPoint Point)
		{
			switch (Point)
			{
			case EUnifiedFaultInjectionPoint::None: return TEXT("None");
			case EUnifiedFaultInjectionPoint::BuyWoodPreflight: return TEXT("BuyWoodPreflight");
			case EUnifiedFaultInjectionPoint::ChopWoodPreflight: return TEXT("ChopWoodPreflight");
			case EUnifiedFaultInjectionPoint::StartRepairPreflight: return TEXT("StartRepairPreflight");
			case EUnifiedFaultInjectionPoint::StateImportPreflight: return TEXT("StateImportPreflight");
			default: return TEXT("Unknown");
			}
		}

		TSharedRef<FJsonObject> MakeCommonJson(
			const FUnifiedRunResult& Result,
			const FUnifiedRunLogMetadata& Metadata,
			const FSimulationTime GameTime)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("schema_version"), Result.LogSchemaVersion);
			Object->SetStringField(TEXT("experiment_id"), Metadata.ExperimentID);
			Object->SetStringField(TEXT("run_id"), Metadata.RunID);
			Object->SetStringField(TEXT("method"), ToString(Result.Method));
			Object->SetStringField(TEXT("scenario"), ToString(Result.Scenario));
			Object->SetNumberField(TEXT("seed"), Result.Seed);
			Object->SetStringField(TEXT("game_time"), GameTime.ToString());
			return Object;
		}

		FString CondensedJson(const TSharedRef<FJsonObject>& Object)
		{
			FString Output;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(Object, Writer);
			return Output;
		}

		FString PrettyJson(const TSharedRef<FJsonObject>& Object)
		{
			FString Output;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
			FJsonSerializer::Serialize(Object, Writer);
			return Output + TEXT("\n");
		}

		bool IsSHA256(const FString& Value)
		{
			if (Value.Len() != 64)
			{
				return false;
			}
			for (const TCHAR Character : Value)
			{
				if (!FChar::IsHexDigit(Character))
				{
					return false;
				}
			}
			return true;
		}

		bool HasRequiredMetadata(const FUnifiedRunLogMetadata& Metadata)
		{
			return !Metadata.OutputDirectory.IsEmpty()
				&& !Metadata.ExperimentID.IsEmpty()
				&& !Metadata.RunID.IsEmpty()
				&& IsSHA256(Metadata.PopulationManifestSHA256)
				&& IsSHA256(Metadata.DamageListSHA256)
				&& IsSHA256(Metadata.PersistentPoolSHA256)
				&& !Metadata.GitCommit.IsEmpty()
				&& !Metadata.UEVersion.IsEmpty()
				&& !Metadata.BuildType.IsEmpty()
				&& !Metadata.Hardware.IsEmpty()
				&& !Metadata.LogMode.IsEmpty()
				&& !Metadata.StartTime.IsEmpty()
				&& !Metadata.EndTime.IsEmpty();
		}
	}

	void FUnifiedRunLogWriter::OnHourCompleted(const FUnifiedHourObservation& Observation)
	{
		Hours.Add(Observation);
	}

	void FUnifiedRunLogWriter::OnNPCSnapshot(const FUnifiedNPCObservation& Observation)
	{
		NPCSnapshots.Add(Observation);
	}

	void FUnifiedRunLogWriter::OnEventCommitted(const FSimulationEventRecord& Event)
	{
		Events.Add(Event);
	}

	void FUnifiedRunLogWriter::OnTransactionCommitted(const FLedgerTransaction& Transaction)
	{
		Transactions.Add(Transaction);
	}

	void FUnifiedRunLogWriter::OnLODTransitionCommitted(const FLODTransitionRecord& Transition)
	{
		LODTransitions.Add(Transition);
	}

	void FUnifiedRunLogWriter::OnActivationObserved(const FUnifiedActivationObservation& Observation)
	{
		Activations.Add(Observation);
	}

	int32 FUnifiedRunLogWriter::GetCohortObservationCount() const
	{
		int32 Count = 0;
		for (const FUnifiedHourObservation& Hour : Hours)
		{
			Count += Hour.Cohorts.Num();
		}
		return Count;
	}

	bool FUnifiedRunLogWriter::WriteRun(
		const FUnifiedRunResult& Result,
		const FUnifiedRunLogMetadata& Metadata,
		FString& OutError) const
	{
		const double SerializationStart = FPlatformTime::Seconds();
		if (!HasRequiredMetadata(Metadata))
		{
			OutError = TEXT("Run logging requires complete output, identity, input-hash, build, hardware, log-mode, and time metadata.");
			return false;
		}
		const bool bV17Authoritative = Result.ProposedModelVersion == EProposedModelVersion::V17Authoritative;
		const int32 ExpectedLODTransitionCount = bV17Authoritative
			? Result.V17LODTransitions.Num()
			: Result.LODTransitions.Num();
		if (Result.Mode != EUnifiedRunMode::Performance
			&& (Hours.Num() != Result.WarmupHourSteps + Result.FormalHourSteps
			|| Events.Num() != Result.Diagnostics.EventCount
			|| Transactions.Num() != Result.Transactions.Num()
			|| (!bV17Authoritative && LODTransitions.Num() != ExpectedLODTransitionCount)
			|| Activations.Num() != Result.ActivationObservations.Num()
			|| NPCSnapshots.Num() != Result.ActivationObservations.Num()))
		{
			OutError = TEXT("Run logging records do not reconcile with the authoritative run result.");
			return false;
		}
		if (Result.Mode == EUnifiedRunMode::Performance
			&& (!Hours.IsEmpty() || !NPCSnapshots.IsEmpty() || !Events.IsEmpty() || !Transactions.IsEmpty()
				|| !LODTransitions.IsEmpty() || !Activations.IsEmpty() || Result.PerformanceSamples.IsEmpty()))
		{
			OutError = TEXT("Performance logging requires isolated one-second samples and no full raw-domain observer records.");
			return false;
		}

		TSharedRef<FJsonObject> Manifest = MakeCommonJson(Result, Metadata, Result.FinalTime);
		Manifest->SetStringField(TEXT("spec_version"), Result.ModelSpecVersion);
		Manifest->SetStringField(TEXT("config_hash"), Result.ConfigHash);
		Manifest->SetStringField(TEXT("population_manifest_sha256"), Metadata.PopulationManifestSHA256);
		Manifest->SetStringField(TEXT("damage_list_sha256"), Metadata.DamageListSHA256);
		Manifest->SetStringField(TEXT("persistent_pool_sha256"), Metadata.PersistentPoolSHA256);
		Manifest->SetStringField(TEXT("git_commit"), Metadata.GitCommit);
		Manifest->SetStringField(TEXT("ue_version"), Metadata.UEVersion);
		Manifest->SetStringField(TEXT("build_type"), Metadata.BuildType);
		Manifest->SetStringField(TEXT("hardware"), Metadata.Hardware);
		Manifest->SetStringField(TEXT("log_mode"), Metadata.LogMode);
		Manifest->SetStringField(TEXT("start_time"), Metadata.StartTime);
		Manifest->SetStringField(TEXT("end_time"), Metadata.EndTime);
		Manifest->SetBoolField(TEXT("valid"), Result.IsHardErrorFree());
		Manifest->SetStringField(TEXT("deterministic_digest"), FUnifiedSimulationRunner::BuildDeterministicDigest(Result));
		if (Result.ProposedModelVersion == EProposedModelVersion::V17Authoritative)
		{
			Manifest->SetStringField(TEXT("proposed_model_version"), TEXT("1.7"));
			Manifest->SetStringField(TEXT("authoritative_model_version"), TEXT("1.7"));
			Manifest->SetStringField(TEXT("authority_mode"), Result.AuthorityMode);
			Manifest->SetStringField(TEXT("joint_state_version"), Result.JointStateVersion);
			Manifest->SetStringField(TEXT("claim_allocation_version"), Result.ClaimAllocationVersion);
			Manifest->SetStringField(TEXT("capsule_version"), Result.CapsuleVersion);
			Manifest->SetBoolField(TEXT("valid_for_formal_experiment"), Result.bValidForFormalExperiment);
		}
		else if (Result.bEnableV17ShadowCohort)
		{
			Manifest->SetStringField(TEXT("proposed_model_version"), TEXT("1.7"));
			Manifest->SetStringField(TEXT("authoritative_model_version"), TEXT("1.6"));
			Manifest->SetStringField(TEXT("authority_mode"), TEXT("v1.7_shadow"));
			Manifest->SetStringField(TEXT("joint_state_version"), TEXT("1.7-shadow"));
			Manifest->SetStringField(TEXT("claim_allocation_version"), TEXT("1.7-shadow"));
			Manifest->SetStringField(TEXT("capsule_version"), TEXT("not_active"));
			Manifest->SetBoolField(TEXT("valid_for_formal_experiment"), false);
		}
		else
		{
			Manifest->SetStringField(TEXT("proposed_model_version"),
				Result.Method == EUnifiedSimulationMethod::Proposed ? TEXT("1.6") : TEXT("not_applicable"));
			Manifest->SetStringField(TEXT("authoritative_model_version"), TEXT("1.6"));
			Manifest->SetStringField(TEXT("authority_mode"),
				Result.Method == EUnifiedSimulationMethod::Proposed ? TEXT("v1.6_current") : TEXT("method_native"));
			Manifest->SetStringField(TEXT("joint_state_version"), TEXT("not_applicable"));
			Manifest->SetStringField(TEXT("claim_allocation_version"), TEXT("not_applicable"));
			Manifest->SetStringField(TEXT("capsule_version"), TEXT("not_applicable"));
			Manifest->SetBoolField(TEXT("valid_for_formal_experiment"), Result.IsHardErrorFree());
		}
		TSharedRef<FJsonObject> HardErrors = MakeShared<FJsonObject>();
		if (Result.ProposedModelVersion == EProposedModelVersion::V17Authoritative)
		{
			HardErrors->SetNumberField(TEXT("task_reset"), Result.V17Audit.TaskResetCount);
			HardErrors->SetNumberField(TEXT("duplicate_completion"), Result.V17Audit.DuplicateCompletionCount);
			HardErrors->SetNumberField(TEXT("event_owner_conflict"), Result.V17Audit.OwnerConflictCount);
			HardErrors->SetNumberField(TEXT("duplicate_transaction"), Result.V17Audit.DuplicateTransactionCount);
			HardErrors->SetNumberField(TEXT("negative_stock"), Result.V17Audit.NegativeStockCount);
			HardErrors->SetNumberField(TEXT("population_residual"), Result.V17Audit.PopulationResidual);
			HardErrors->SetNumberField(TEXT("wood_residual"), Result.V17Audit.WoodResidual);
			HardErrors->SetNumberField(TEXT("identity_mismatch"), Result.V17Audit.IdentityMismatchCount);
			HardErrors->SetNumberField(
				TEXT("joint_cell_resource_band_mismatch"),
				Result.V17Audit.JointCellResourceBandMismatchCount);
			HardErrors->SetNumberField(TEXT("batch_split_merge_residual"), Result.V17Audit.BatchSplitMergeResidualCount);
		}
		else
		{
			HardErrors->SetNumberField(TEXT("task_reset"), Result.TaskResetCount);
			HardErrors->SetNumberField(TEXT("duplicate_completion"), Result.Audit.DuplicateCompletionCount);
			HardErrors->SetNumberField(TEXT("event_owner_conflict"), Result.Audit.EventOwnerConflictCount);
			HardErrors->SetNumberField(TEXT("duplicate_transaction"), Result.Audit.DuplicateTransactionCount);
			HardErrors->SetNumberField(TEXT("negative_stock"), Result.Audit.NegativeStockCount);
			HardErrors->SetNumberField(TEXT("population_residual"), Result.Audit.PopulationResidual);
			HardErrors->SetNumberField(TEXT("wood_residual"), Result.Audit.WoodResidual);
		}
		Manifest->SetObjectField(TEXT("hard_errors"), HardErrors);
		TSharedRef<FJsonObject> Parameters = MakeShared<FJsonObject>();
		Parameters->SetNumberField(TEXT("population_per_kingdom"), Result.PopulationPerKingdom);
		Parameters->SetStringField(TEXT("run_mode"), RunModeName(Result.Mode));
		Parameters->SetBoolField(TEXT("retain_completed_events"), Result.bRetainCompletedEvents);
		Parameters->SetBoolField(TEXT("record_snapshots"), Result.bRecordSnapshots);
		Parameters->SetBoolField(TEXT("verify_cohort_approximation"), Result.bVerifyCohortApproximation);
		Parameters->SetBoolField(TEXT("enable_macro_profiling"), Result.bEnableMacroProfiling);
		Parameters->SetBoolField(TEXT("enable_v17_shadow_cohort"), Result.bEnableV17ShadowCohort);
		Parameters->SetStringField(TEXT("proposed_model_version"), ToString(Result.ProposedModelVersion));
		Parameters->SetStringField(TEXT("fault_injection"), FaultInjectionName(Result.FaultInjection));
		Manifest->SetObjectField(TEXT("parameters"), Parameters);
		TSharedRef<FJsonObject> Measurements = MakeShared<FJsonObject>();
		Measurements->SetNumberField(TEXT("initialize_cpu_ms"), Result.CostBreakdown.InitializeCpuMs);
		Measurements->SetNumberField(TEXT("production_cpu_ms"), Result.CostBreakdown.ProductionCpuMs);
		Measurements->SetNumberField(TEXT("macro_cpu_ms"), Result.CostBreakdown.MacroCpuMs);
		Measurements->SetNumberField(TEXT("micro_cpu_ms"), Result.CostBreakdown.MicroCpuMs);
		Measurements->SetNumberField(TEXT("transition_cpu_ms"), Result.CostBreakdown.TransitionCpuMs);
		Measurements->SetNumberField(TEXT("validation_cpu_ms"), Result.CostBreakdown.ValidationCpuMs);
		Measurements->SetNumberField(TEXT("audit_cpu_ms"), Result.CostBreakdown.AuditCpuMs);
		Measurements->SetNumberField(TEXT("snapshot_cpu_ms"), Result.CostBreakdown.SnapshotCpuMs);
		Measurements->SetNumberField(TEXT("observer_cpu_ms"), Result.CostBreakdown.ObserverCpuMs);
		Measurements->SetNumberField(TEXT("finalize_cpu_ms"), Result.CostBreakdown.FinalizeCpuMs);
		Measurements->SetNumberField(TEXT("serialization_cpu_ms"), 0.0);
		Measurements->SetNumberField(TEXT("file_write_cpu_ms"), 0.0);
		Measurements->SetStringField(TEXT("ai_cpu_scope"), TEXT("production_only_excludes_validation_audit_snapshot_observer_and_logging"));
		if (Result.ProposedModelVersion == EProposedModelVersion::V17Authoritative)
		{
			TSharedRef<FJsonObject> Diagnostics = MakeShared<FJsonObject>();
			Diagnostics->SetNumberField(TEXT("identity_count"), Result.Diagnostics.V17IdentityCount);
			Diagnostics->SetNumberField(TEXT("identity_scan_count_per_hour"), Result.Diagnostics.V17IdentityScanCountPerHour);
			Diagnostics->SetNumberField(TEXT("resident_touches"), Result.Diagnostics.V17ResidentTouches);
			Diagnostics->SetNumberField(TEXT("full_audit_count"), Result.Diagnostics.FullAuditCount);
			Diagnostics->SetNumberField(TEXT("non_empty_joint_cell_count"), Result.Diagnostics.V17NonEmptyJointCellCount);
			Diagnostics->SetNumberField(TEXT("batch_claim_count"), Result.Diagnostics.V17BatchClaimCount);
			Diagnostics->SetNumberField(TEXT("batch_event_count"), Result.Diagnostics.V17BatchEventCount);
			Diagnostics->SetNumberField(TEXT("participant_count"), Result.Diagnostics.V17ParticipantCount);
			Diagnostics->SetNumberField(TEXT("capsule_count"), Result.Diagnostics.V17CapsuleCount);
			Diagnostics->SetNumberField(TEXT("participant_ref_count"), Result.Diagnostics.V17ParticipantRefCount);
			Diagnostics->SetNumberField(TEXT("lift_count"), Result.Diagnostics.V17LiftCount);
			Diagnostics->SetNumberField(TEXT("restrict_count"), Result.Diagnostics.V17RestrictCount);
			Diagnostics->SetNumberField(TEXT("max_active"), Result.Diagnostics.MaxActiveMicro);
			Measurements->SetObjectField(TEXT("v1_7_diagnostics"), Diagnostics);
		}
		if (Result.bEnableMacroProfiling)
		{
			TSharedRef<FJsonObject> MacroProfile = MakeShared<FJsonObject>();
			MacroProfile->SetNumberField(TEXT("resident_scan_and_grouping_cpu_ms"), Result.MacroProfile.ResidentScanAndGroupingCpuMs);
			MacroProfile->SetNumberField(TEXT("representative_planning_cpu_ms"), Result.MacroProfile.RepresentativePlanningCpuMs);
			MacroProfile->SetNumberField(TEXT("member_allocation_cpu_ms"), Result.MacroProfile.MemberAllocationCpuMs);
			MacroProfile->SetNumberField(TEXT("candidate_sort_cpu_ms"), Result.MacroProfile.CandidateSortCpuMs);
			MacroProfile->SetNumberField(TEXT("competition_setup_cpu_ms"), Result.MacroProfile.CompetitionSetupCpuMs);
			MacroProfile->SetNumberField(TEXT("competition_check_cpu_ms"), Result.MacroProfile.CompetitionCheckCpuMs);
			MacroProfile->SetNumberField(TEXT("action_commit_cpu_ms"), Result.MacroProfile.ActionCommitCpuMs);
			MacroProfile->SetNumberField(TEXT("profiled_hour_count"), Result.MacroProfile.ProfiledHourCount);
			MacroProfile->SetNumberField(TEXT("resident_visit_count"), Result.MacroProfile.ResidentVisitCount);
			MacroProfile->SetNumberField(TEXT("cohort_group_count"), Result.MacroProfile.CohortGroupCount);
			MacroProfile->SetNumberField(TEXT("candidate_count"), Result.MacroProfile.CandidateCount);
			Measurements->SetObjectField(TEXT("macro_profile"), MacroProfile);
		}
		if (Result.bEnableV17ShadowCohort)
		{
			const FUnifiedV17ShadowProfile& Profile = Result.V17ShadowProfile;
			TSharedRef<FJsonObject> Shadow = MakeShared<FJsonObject>();
			Shadow->SetNumberField(TEXT("initialize_cpu_ms"), Profile.InitializeCpuMs);
			Shadow->SetNumberField(TEXT("cpu_ms"), Profile.CpuMs);
			Shadow->SetNumberField(TEXT("hour_count"), Profile.HourCount);
			Shadow->SetNumberField(TEXT("identity_count"), Profile.IdentityCount);
			Shadow->SetNumberField(TEXT("identity_visit_count"), Profile.IdentityVisitCount);
			Shadow->SetNumberField(TEXT("cohort_observation_count"), Profile.CohortObservationCount);
			Shadow->SetNumberField(TEXT("joint_cell_observation_count"), Profile.JointCellObservationCount);
			Shadow->SetNumberField(TEXT("action_flow_count"), Profile.ActionFlowCount);
			Shadow->SetNumberField(TEXT("batch_claim_count"), Profile.BatchClaimCount);
			Shadow->SetNumberField(TEXT("requested_participant_count"), Profile.RequestedParticipantCount);
			Shadow->SetNumberField(TEXT("granted_participant_count"), Profile.GrantedParticipantCount);
			Shadow->SetNumberField(TEXT("rejected_participant_count"), Profile.RejectedParticipantCount);
			Shadow->SetNumberField(TEXT("pending_participant_observation_count"), Profile.PendingParticipantObservationCount);
			Shadow->SetNumberField(TEXT("identity_mismatch_count"), Profile.IdentityMismatchCount);
			Shadow->SetNumberField(TEXT("population_residual_count"), Profile.PopulationResidualCount);
			Shadow->SetNumberField(TEXT("resource_residual_count"), Profile.ResourceResidualCount);
			Shadow->SetNumberField(TEXT("home_state_residual_count"), Profile.HomeStateResidualCount);
			Shadow->SetNumberField(TEXT("pending_participant_residual_count"), Profile.PendingParticipantResidualCount);
			Shadow->SetNumberField(TEXT("action_flow_residual_count"), Profile.ActionFlowResidualCount);
			Shadow->SetNumberField(TEXT("batch_result_residual_count"), Profile.BatchResultResidualCount);
			Shadow->SetNumberField(TEXT("capacity_overflow_count"), Profile.CapacityOverflowCount);
			Shadow->SetNumberField(TEXT("max_cohort_count"), Profile.MaxCohortCount);
			Shadow->SetNumberField(TEXT("max_joint_cell_count"), Profile.MaxJointCellCount);
			Shadow->SetNumberField(TEXT("max_action_flow_count"), Profile.MaxActionFlowCount);
			Shadow->SetNumberField(TEXT("max_batch_claim_count"), Profile.MaxBatchClaimCount);
			Shadow->SetNumberField(TEXT("max_pending_participant_count"), Profile.MaxPendingParticipantCount);
			Measurements->SetObjectField(TEXT("v17_shadow"), Shadow);
		}
		Manifest->SetObjectField(TEXT("measurement_summary"), Measurements);

		if (Result.Mode == EUnifiedRunMode::Performance)
		{
			FString PerformanceCsv = CsvHeader(LogSchema::PerformanceFields);
			for (const FUnifiedPerformanceSample& Sample : Result.PerformanceSamples)
			{
				TArray<FString> Fields = CommonCsvFields(Result, Metadata, Sample.GameTime);
				Fields.Append(
				{
					FString::Printf(TEXT("%.9f"), Sample.AICpuMs),
					FString::Printf(TEXT("%.9f"), Sample.MacroCpuMs),
					FString::Printf(TEXT("%.9f"), Sample.MicroCpuMs),
					FString::Printf(TEXT("%.9f"), Sample.TransitionCpuMs),
					FString::Printf(TEXT("%.9f"), Sample.MemoryMB),
					FString::FromInt(Sample.ActiveCount),
					FString::FromInt(Sample.QueueLength)
				});
				AppendCsvRow(PerformanceCsv, Fields);
			}
			Measurements->SetNumberField(TEXT("serialization_cpu_ms"), (FPlatformTime::Seconds() - SerializationStart) * 1000.0);
			if (!IFileManager::Get().MakeDirectory(*Metadata.OutputDirectory, true))
			{
				OutError = TEXT("Performance logging could not create the output directory.");
				return false;
			}
			const auto Save = [&Metadata](const TCHAR* FileName, const FString& Contents)
			{
				return FFileHelper::SaveStringToFile(Contents, *FPaths::Combine(Metadata.OutputDirectory, FileName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			};
			const double FileWriteStart = FPlatformTime::Seconds();
			if (!Save(LogSchema::RunManifestFile, PrettyJson(Manifest))
				|| !Save(LogSchema::PerformanceFile, PerformanceCsv))
			{
				OutError = TEXT("Performance logging failed while writing the manifest or one-second samples.");
				return false;
			}
			Measurements->SetNumberField(TEXT("file_write_cpu_ms"), (FPlatformTime::Seconds() - FileWriteStart) * 1000.0);
			if (!Save(LogSchema::RunManifestFile, PrettyJson(Manifest)))
			{
				OutError = TEXT("Performance logging failed while finalizing measurement metadata.");
				return false;
			}
			OutError.Reset();
			return true;
		}

		FString KingdomCsv = CsvHeader(LogSchema::KingdomTimeseriesFields);
		for (const FUnifiedHourObservation& Hour : Hours)
		{
			for (const FKingdomSnapshot* Snapshot : { &Hour.KingdomA, &Hour.KingdomB })
			{
				TArray<FString> Fields = CommonCsvFields(Result, Metadata, Hour.GameTime);
				Fields.Append(
				{
					CsvString(KingdomName(Snapshot->Kingdom)),
					FString::Printf(TEXT("%.9f"), Snapshot->Stocks.ForestWood),
					FString::Printf(TEXT("%.9f"), Snapshot->Stocks.MarketWoodAvailable),
					FString::Printf(TEXT("%.9f"), Snapshot->Stocks.MarketWoodReserved),
					FString::Printf(TEXT("%.9f"), Snapshot->Stocks.WoodInTransit),
					FString::Printf(TEXT("%.9f"), Snapshot->Stocks.WoodEmbeddedInRepairs),
					FString::Printf(TEXT("%.9f"), Snapshot->Stocks.WoodInRepairedHomes),
					FString::Printf(TEXT("%lld"), Snapshot->Stocks.TreasuryAvailable),
					FString::Printf(TEXT("%lld"), Snapshot->Stocks.TreasuryReserved),
					FString::Printf(TEXT("%lld"), Snapshot->Stocks.MarketCoin),
					FString::Printf(TEXT("%.9f"), Snapshot->Stocks.WoodPrice),
					FString::FromInt(Snapshot->Healthy),
					FString::FromInt(Snapshot->DamagedWaiting),
					FString::FromInt(Snapshot->UnderRepair),
					FString::FromInt(Snapshot->Repaired),
					CsvString(Hour.PolicyState)
				});
				AppendCsvRow(KingdomCsv, Fields);
			}
		}

		FString CohortCsv = bV17Authoritative
			? CsvHeader(LogSchema::V17CohortTimeseriesFields)
			: CsvHeader(LogSchema::CohortTimeseriesFields);
		for (const FUnifiedHourObservation& Hour : Hours)
		{
			for (const FUnifiedCohortObservation& Cohort : Hour.Cohorts)
			{
				TArray<FString> Fields = CommonCsvFields(Result, Metadata, Cohort.GameTime);
				if (bV17Authoritative)
				{
					Fields.Append(
					{
						CsvString(Cohort.CohortKey),
						FString::Printf(TEXT("%llu"), Cohort.JointCellID),
						CsvString(Cohort.JointCellKey),
						FString::FromInt(Cohort.Count),
						FString::Printf(TEXT("%lld"), Cohort.CashSum),
						FString::Printf(TEXT("%lld"), Cohort.RepairCreditSum),
						FString::Printf(TEXT("%lld"), Cohort.WoodSum),
						FString::FromInt(Cohort.PendingParticipantCount)
					});
				}
				else
				{
					Fields.Append(
					{
						CsvString(Cohort.CohortKey),
						FString::FromInt(Cohort.Count),
						FString::Printf(TEXT("%lld"), Cohort.CashSum),
						FString::Printf(TEXT("%lld"), Cohort.CashSquaredSum),
						FString::Printf(TEXT("%lld"), Cohort.RepairCreditSum),
						FString::FromInt(Cohort.WoodCounts[0]),
						FString::FromInt(Cohort.WoodCounts[1]),
						FString::FromInt(Cohort.WoodCounts[2]),
						FString::FromInt(Cohort.WoodCounts[3]),
						FString::FromInt(Cohort.WoodCounts[4]),
						CsvString(MacroIntentName(Cohort.MacroIntent))
					});
				}
				AppendCsvRow(CohortCsv, Fields);
			}
		}

		FString NPCCsv = CsvHeader(LogSchema::NPCSnapshotFields);
		for (const FUnifiedNPCObservation& NPC : NPCSnapshots)
		{
			const FResidentCoreState& Resident = NPC.Resident;
			TArray<FString> Fields = CommonCsvFields(Result, Metadata, NPC.GameTime);
			const double RemainingHours = FMath::Max(0.0, static_cast<double>(Resident.ActionEndTime.Minutes - NPC.GameTime.Minutes) / MinutesPerHour);
			Fields.Append(
			{
				FString::Printf(TEXT("%lld"), Resident.PersistentID),
				CsvString(Resident.Name),
				FString::Printf(TEXT("%lld"), Resident.HomeID),
				CsvString(KingdomName(Resident.Kingdom)),
				CsvString(ProfessionName(Resident.Profession)),
				CsvString(IncomeBandName(Resident.IncomeBand)),
				FString::FromInt(Resident.Cash),
				FString::FromInt(Resident.RepairCredit),
				FString::FromInt(Resident.InventoryWood),
				CsvString(HomeStateName(Resident.HomeState)),
				CsvString(ToString(Resident.CurrentGoal)),
				CsvString(ToString(Resident.LastCompletedAction)),
				CsvString(MacroIntentName(Resident.MacroIntent)),
				FString::Printf(TEXT("%lld"), Resident.ActiveEventID),
				FString::Printf(TEXT("%lld"), Resident.ActiveReservationID),
				FString::Printf(TEXT("%.9f"), RemainingHours),
				CsvString(ToString(NPC.FirstAction))
			});
			AppendCsvRow(NPCCsv, Fields);
		}

		FString EventJsonl;
		for (const FSimulationEventRecord& Record : Events)
		{
			const FSimulationEventRequest& Event = Record.Event;
			TSharedRef<FJsonObject> Object = MakeCommonJson(Result, Metadata, Event.StartTime);
			Object->SetNumberField(TEXT("event_id"), Record.EventID);
			Object->SetNumberField(TEXT("arrive_id"), Event.ArriveID);
			const FString LoggedType = bV17Authoritative && Event.Type == TEXT("BatchRepair")
				? TEXT("Repair")
				: bV17Authoritative && Event.Type == TEXT("BatchRejectedWait")
					? TEXT("Wait")
					: Event.Type;
			Object->SetStringField(TEXT("type"), LoggedType);
			Object->SetStringField(TEXT("owner"), Event.Owner);
			Object->SetStringField(TEXT("start_time"), Event.StartTime.ToString());
			Object->SetStringField(TEXT("end_time"), Event.EndTime.ToString());
			if (bV17Authoritative)
			{
				const FV17AuthoritativeBatchEvent* Batch = Result.V17BatchEvents.Find(Record.EventID);
				Object->SetStringField(
					TEXT("owner_type"), Event.ResidentID > 0 ? TEXT("Individual") : TEXT("Batch"));
				Object->SetNumberField(TEXT("participant_count"), Event.ParticipantCount);
				Object->SetNumberField(TEXT("batch_claim_id"), Batch != nullptr ? Batch->BatchClaimID : 0);
				Object->SetNumberField(
					TEXT("parent_batch_event_id"), Batch != nullptr ? Batch->ParentBatchEventID : Event.ParentEventID);
				Object->SetNumberField(TEXT("source_cell_id"), Batch != nullptr ? Batch->SourceCellID : 0);
				Object->SetNumberField(TEXT("target_cell_id"), Batch != nullptr ? Batch->TargetCellID : 0);
				Object->SetNumberField(TEXT("inherited_order_key"), Batch != nullptr ? Batch->InheritedOrderKey : 0);
				Object->SetNumberField(TEXT("target_flow_count"), Batch != nullptr ? Batch->TargetFlows.Num() : 0);
				TArray<TSharedPtr<FJsonValue>> TargetFlows;
				if (Batch != nullptr)
				{
					for (const FV17AuthoritativeBatchEvent::FTargetFlow& Flow : Batch->TargetFlows)
					{
						TSharedRef<FJsonObject> FlowObject = MakeShared<FJsonObject>();
						FlowObject->SetNumberField(TEXT("target_cell_id"), Flow.TargetCellID);
						FlowObject->SetNumberField(TEXT("participant_count"), Flow.ParticipantCount);
						FlowObject->SetNumberField(TEXT("cash_total"), Flow.CashTotal);
						FlowObject->SetNumberField(TEXT("repair_credit_total"), Flow.RepairCreditTotal);
						FlowObject->SetNumberField(TEXT("wood_total"), Flow.WoodTotal);
						TargetFlows.Add(MakeShared<FJsonValueObject>(FlowObject));
					}
				}
				Object->SetArrayField(TEXT("target_flows"), TargetFlows);
			}
			else
			{
				Object->SetNumberField(TEXT("parent_event_id"), Event.ParentEventID);
				Object->SetNumberField(TEXT("participants"), Event.ParticipantCount);
			}
			Object->SetStringField(TEXT("cause"), Event.Cause);
			Object->SetNumberField(TEXT("policy_id"), Event.PolicyID);
			EventJsonl += CondensedJson(Object) + TEXT("\n");
		}

		FString TransitionJsonl;
		if (bV17Authoritative)
		{
			for (const FV17LODTransitionRecord& Transition : Result.V17LODTransitions)
			{
				TSharedRef<FJsonObject> Object = MakeCommonJson(Result, Metadata, Transition.GameTime);
				Object->SetNumberField(TEXT("resident_id"), Transition.ResidentID);
				Object->SetStringField(TEXT("direction"), Transition.bLift ? TEXT("Lift") : TEXT("Restrict"));
				Object->SetNumberField(TEXT("selected_cell_id"), Transition.SelectedCellID);
				Object->SetNumberField(TEXT("batch_event_id"), Transition.BatchEventID);
				Object->SetNumberField(TEXT("parent_batch_event_id"), Transition.ParentBatchEventID);
				Object->SetBoolField(TEXT("used_fallback"), Transition.bUsedFallback);
				Object->SetStringField(TEXT("result"), V17TransitionResultName(Transition.Result));
				TransitionJsonl += CondensedJson(Object) + TEXT("\n");
			}
		}
		else
		{
			for (const FLODTransitionRecord& Transition : LODTransitions)
			{
				TSharedRef<FJsonObject> Object = MakeCommonJson(Result, Metadata, Transition.CommittedTime);
				Object->SetNumberField(TEXT("persistent_id"), Transition.PersistentID);
				Object->SetStringField(TEXT("from"), RepresentationName(Transition.From));
				Object->SetStringField(TEXT("to"), RepresentationName(Transition.To));
				Object->SetStringField(TEXT("requested_time"), Transition.RequestedTime.ToString());
				Object->SetStringField(TEXT("committed_time"), Transition.CommittedTime.ToString());
				Object->SetNumberField(TEXT("latency_ms"), 0.0);
				Object->SetStringField(TEXT("bucket"), Transition.Bucket);
				Object->SetNumberField(TEXT("arrive_id"), Transition.ArriveID);
				Object->SetStringField(TEXT("result"), TransitionResultName(Transition.Result));
				TransitionJsonl += CondensedJson(Object) + TEXT("\n");
			}
		}

		FString TransactionJsonl;
		for (const FLedgerTransaction& Transaction : Transactions)
		{
			const FLedgerTransferRequest& Transfer = Transaction.Transfer;
			TSharedRef<FJsonObject> Object = MakeCommonJson(Result, Metadata, Transfer.GameTime);
			Object->SetNumberField(TEXT("transaction_id"), Transaction.TransactionID);
			Object->SetStringField(TEXT("idempotency_key"), Transfer.IdempotencyKey);
			Object->SetNumberField(TEXT("event_id"), Transfer.EventID);
			Object->SetNumberField(TEXT("arrive_id"), Transfer.ArriveID);
			Object->SetStringField(TEXT("resource"), ResourceName(Transfer.Resource));
			Object->SetStringField(TEXT("source"), Transfer.Source);
			Object->SetStringField(TEXT("destination"), Transfer.Destination);
			Object->SetNumberField(TEXT("quantity"), Transfer.Quantity);
			Object->SetBoolField(TEXT("boundary_flag"), Transfer.bBoundaryFlow);
			Object->SetNumberField(TEXT("policy_id"), Transfer.PolicyID);
			TransactionJsonl += CondensedJson(Object) + TEXT("\n");
		}

		if (!IFileManager::Get().MakeDirectory(*Metadata.OutputDirectory, true))
		{
			OutError = TEXT("Run logging could not create the output directory.");
			return false;
		}
		const auto Save = [&Metadata](const TCHAR* FileName, const FString& Contents)
		{
			return FFileHelper::SaveStringToFile(
				Contents,
				*FPaths::Combine(Metadata.OutputDirectory, FileName),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		};
		Measurements->SetNumberField(TEXT("serialization_cpu_ms"), (FPlatformTime::Seconds() - SerializationStart) * 1000.0);
		const double FileWriteStart = FPlatformTime::Seconds();
		if (!Save(LogSchema::RunManifestFile, PrettyJson(Manifest))
			|| !Save(LogSchema::KingdomTimeseriesFile, KingdomCsv)
			|| !Save(LogSchema::CohortTimeseriesFile, CohortCsv)
			|| !Save(LogSchema::NPCSnapshotsFile, NPCCsv)
			|| !Save(LogSchema::SimulationEventsFile, EventJsonl)
			|| !Save(LogSchema::LODTransitionsFile, TransitionJsonl)
			|| !Save(LogSchema::LedgerTransactionsFile, TransactionJsonl))
		{
			OutError = TEXT("Run logging failed while writing the manifest or raw domain logs.");
			return false;
		}
		Measurements->SetNumberField(TEXT("file_write_cpu_ms"), (FPlatformTime::Seconds() - FileWriteStart) * 1000.0);
		if (!Save(LogSchema::RunManifestFile, PrettyJson(Manifest)))
		{
			OutError = TEXT("Run logging failed while finalizing measurement metadata.");
			return false;
		}

		OutError.Reset();
		return true;
	}
}
