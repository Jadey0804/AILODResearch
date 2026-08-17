// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODExperimentLogging.h"

#include "AILODLogSchema.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
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
				CsvString(SchemaVersion),
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
			Object->SetStringField(TEXT("schema_version"), SchemaVersion);
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
		if (!HasRequiredMetadata(Metadata))
		{
			OutError = TEXT("Run logging requires complete output, identity, input-hash, build, hardware, log-mode, and time metadata.");
			return false;
		}
		if (Hours.Num() != Result.WarmupHourSteps + Result.FormalHourSteps
			|| Events.Num() != Result.Diagnostics.EventCount
			|| Transactions.Num() != Result.Transactions.Num()
			|| LODTransitions.Num() != Result.LODTransitions.Num()
			|| Activations.Num() != Result.ActivationObservations.Num()
			|| NPCSnapshots.Num() != Result.ActivationObservations.Num())
		{
			OutError = TEXT("Run logging records do not reconcile with the authoritative run result.");
			return false;
		}

		TSharedRef<FJsonObject> Manifest = MakeCommonJson(Result, Metadata, Result.FinalTime);
		Manifest->SetStringField(TEXT("spec_version"), SpecVersion);
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
		TSharedRef<FJsonObject> Parameters = MakeShared<FJsonObject>();
		Parameters->SetNumberField(TEXT("population_per_kingdom"), Result.PopulationPerKingdom);
		Parameters->SetStringField(TEXT("run_mode"), RunModeName(Result.Mode));
		Parameters->SetBoolField(TEXT("retain_completed_events"), Result.bRetainCompletedEvents);
		Parameters->SetBoolField(TEXT("record_snapshots"), Result.bRecordSnapshots);
		Parameters->SetBoolField(TEXT("verify_cohort_approximation"), Result.bVerifyCohortApproximation);
		Parameters->SetStringField(TEXT("fault_injection"), FaultInjectionName(Result.FaultInjection));
		Manifest->SetObjectField(TEXT("parameters"), Parameters);

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

		FString CohortCsv = CsvHeader(LogSchema::CohortTimeseriesFields);
		for (const FUnifiedHourObservation& Hour : Hours)
		{
			for (const FUnifiedCohortObservation& Cohort : Hour.Cohorts)
			{
				TArray<FString> Fields = CommonCsvFields(Result, Metadata, Cohort.GameTime);
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
			Object->SetNumberField(TEXT("parent_event_id"), Event.ParentEventID);
			Object->SetStringField(TEXT("type"), Event.Type);
			Object->SetStringField(TEXT("owner"), Event.Owner);
			Object->SetStringField(TEXT("start_time"), Event.StartTime.ToString());
			Object->SetStringField(TEXT("end_time"), Event.EndTime.ToString());
			Object->SetNumberField(TEXT("participants"), Event.ParticipantCount);
			Object->SetStringField(TEXT("cause"), Event.Cause);
			Object->SetNumberField(TEXT("policy_id"), Event.PolicyID);
			EventJsonl += CondensedJson(Object) + TEXT("\n");
		}

		FString TransitionJsonl;
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

		OutError.Reset();
		return true;
	}
}
