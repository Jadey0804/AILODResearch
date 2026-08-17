// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODUnifiedSimulation.h"

namespace AILOD
{
	struct FUnifiedRunLogMetadata
	{
		FString OutputDirectory;
		FString ExperimentID;
		FString RunID;
		FString PopulationManifestSHA256;
		FString DamageListSHA256;
		FString PersistentPoolSHA256;
		FString GitCommit;
		FString UEVersion;
		FString BuildType;
		FString Hardware;
		FString LogMode;
		FString StartTime;
		FString EndTime;
	};

	class FUnifiedRunLogWriter final
		: public IUnifiedSimulationObserver
		, public IUnifiedSimulationEventSink
	{
	public:
		virtual void OnHourCompleted(const FUnifiedHourObservation& Observation) override;
		virtual void OnNPCSnapshot(const FUnifiedNPCObservation& Observation) override;
		virtual void OnEventCommitted(const FSimulationEventRecord& Event) override;
		virtual void OnTransactionCommitted(const FLedgerTransaction& Transaction) override;
		virtual void OnLODTransitionCommitted(const FLODTransitionRecord& Transition) override;
		virtual void OnActivationObserved(const FUnifiedActivationObservation& Observation) override;

		bool WriteRun(
			const FUnifiedRunResult& Result,
			const FUnifiedRunLogMetadata& Metadata,
			FString& OutError) const;

		int32 GetHourObservationCount() const { return Hours.Num(); }
		int32 GetCohortObservationCount() const;
		int32 GetNPCObservationCount() const { return NPCSnapshots.Num(); }
		int32 GetEventCount() const { return Events.Num(); }
		int32 GetTransactionCount() const { return Transactions.Num(); }
		int32 GetLODTransitionCount() const { return LODTransitions.Num(); }
		int32 GetActivationObservationCount() const { return Activations.Num(); }

	private:
		TArray<FUnifiedHourObservation> Hours;
		TArray<FUnifiedNPCObservation> NPCSnapshots;
		TArray<FSimulationEventRecord> Events;
		TArray<FLedgerTransaction> Transactions;
		TArray<FLODTransitionRecord> LODTransitions;
		TArray<FUnifiedActivationObservation> Activations;
	};
}
