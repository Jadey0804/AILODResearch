// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualDemoRuntime.h"

#include "../Simulation/AILODPhase0Manifest.h"

namespace AILOD
{
	bool FVisualDemoRuntime::Initialize(const FVisualDemoRuntimeConfig& InConfig, FString& OutError)
	{
		Config = InConfig;
		Layout = FVisualWorldLayout();
		ObservationPlanner.Reset();
		Session.Reset();
		Snapshot = FUnifiedDemoSnapshot();
		LastObservationPlan = FVisualObservationPlan();
		CurrentPresentationObservationPlan = FVisualObservationPlan();
		PresentationFrame = FVisualResidentPresentationFrame();
		SelectedResidentID = 0;
		State = EVisualDemoRuntimeState::Uninitialized;
		LastError.Reset();
		LastObservationWarning.Reset();
		PendingHourSteps = 0.0;
		ObservationCommitRetryCooldownSeconds = 0.0;
		TimeScale = 1;
		LastTickStepCount = 0;
		bPaused = false;
		bHasSnapshot = false;
		bHasPresentationFrame = false;

		FPhase0Config SimulationConfig;
		SimulationConfig.Seed = Config.SimulationSeed;
		SimulationConfig.PopulationPerKingdom = Config.PopulationPerKingdom;

		FInitialPopulationManifest Population;
		FEarthquakeDamageList Damage;
		FPersistentTestPool PersistentPool;
		if (!FPhase0ManifestGenerator::Generate(
			SimulationConfig, Population, Damage, PersistentPool, OutError))
		{
			return Fail(OutError, OutError);
		}
		if (!Layout.Build(Population, Config.Layout, OutError))
		{
			return Fail(OutError, OutError);
		}
		ObservationPlanner = MakeUnique<FVisualObservationPlanner>(Layout, Config.Observation);

		FUnifiedRunOptions Options;
		Options.Mode = EUnifiedRunMode::Demo;
		Options.ProposedModelVersion = EProposedModelVersion::V17Authoritative;
		Options.bRecordSnapshots = false;
		Options.bRetainCompletedEvents = false;
		Session = MakeUnique<FUnifiedSimulationSession>(
			SimulationConfig,
			EUnifiedSimulationMethod::Proposed,
			EStage2Scenario::StateImport,
			Options);
		if (!Session->Initialize(OutError))
		{
			return Fail(OutError, OutError);
		}
		if (!RefreshSnapshot(OutError))
		{
			return false;
		}

		State = Snapshot.GameTime.Minutes < 0
			? EVisualDemoRuntimeState::Prewarming
			: EVisualDemoRuntimeState::Running;
		OutError.Reset();
		return true;
	}

	bool FVisualDemoRuntime::Restart(FString& OutError)
	{
		return Initialize(Config, OutError);
	}

	bool FVisualDemoRuntime::Tick(const double RealDeltaSeconds, FString& OutError)
	{
		LastTickStepCount = 0;
		ObservationCommitRetryCooldownSeconds = FMath::Max(
			0.0,
			ObservationCommitRetryCooldownSeconds - FMath::Max(0.0, RealDeltaSeconds));
		if (State == EVisualDemoRuntimeState::Failed)
		{
			OutError = LastError;
			return false;
		}
		if (State == EVisualDemoRuntimeState::Prewarming)
		{
			if (!StepSession(OutError))
			{
				return false;
			}
			if (Snapshot.GameTime.Minutes >= 0)
			{
				State = EVisualDemoRuntimeState::Running;
				PendingHourSteps = 0.0;
			}
			return true;
		}
		if (State != EVisualDemoRuntimeState::Running || bPaused)
		{
			OutError.Reset();
			return true;
		}

		PendingHourSteps += FMath::Max(0.0, RealDeltaSeconds) * TimeScale;
		if (PendingHourSteps >= 1.0)
		{
			PendingHourSteps -= 1.0;
			return StepSession(OutError);
		}
		OutError.Reset();
		return true;
	}

	bool FVisualDemoRuntime::SubmitObservationFrame(
		const FVisualObservationFrameInput& Input,
		FString& OutError)
	{
		if (State != EVisualDemoRuntimeState::Running || !ObservationPlanner || !Session)
		{
			OutError = TEXT("Visual observation requires a running Day 0 to Day 60 Demo.");
			return false;
		}

		TUniquePtr<FVisualObservationPlanner> CandidatePlanner =
			MakeUnique<FVisualObservationPlanner>(*ObservationPlanner);
		if (Input.bClearTrackedResident && Input.TelescopePromotionResidentID != 0)
		{
			OutError = TEXT("A visual observation frame cannot clear and replace tracking at the same time.");
			return false;
		}
		if (Input.bClearTrackedResident)
		{
			CandidatePlanner->ClearTrackedResident();
		}
		else if (Input.TelescopePromotionResidentID != 0
			&& !CandidatePlanner->SetTrackedResident(Input.TelescopePromotionResidentID, OutError))
		{
			return false;
		}
		FVisualObservationPlan CandidatePlan;
		if (!CandidatePlanner->PlanFrame(Input, CandidatePlan, OutError))
		{
			return Fail(OutError, OutError);
		}
		if (!CandidatePlan.Diagnostics.bActiveSetChanged)
		{
			FVisualResidentPresentationFrame CandidatePresentationFrame;
			if (!FVisualResidentPresentationPlanner::BuildFrame(
				Layout,
				CandidatePlan,
				Snapshot,
				SelectedResidentID,
				Config.Presentation,
				CandidatePresentationFrame,
				OutError))
			{
				return Fail(OutError, OutError);
			}
			ObservationPlanner = MoveTemp(CandidatePlanner);
			LastObservationPlan = MoveTemp(CandidatePlan);
			CurrentPresentationObservationPlan = LastObservationPlan;
			PresentationFrame = MoveTemp(CandidatePresentationFrame);
			bHasPresentationFrame = true;
			ClearRejectedObservation();
			return true;
		}

		if (ObservationCommitRetryCooldownSeconds > 0.0)
		{
			return PublishProxyOnlyCandidate(*CandidatePlanner, CandidatePlan, OutError);
		}

		FString ObservationError;
		bool bObservationCommitted = false;
#if WITH_DEV_AUTOMATION_TESTS
		if (Config.bRejectNextObservationCommitForTest)
		{
			Config.bRejectNextObservationCommitForTest = false;
			ObservationError = TEXT("Injected Phase 7D atomic replacement rejection.");
		}
		else
#endif
		{
			bObservationCommitted = Session->SubmitDemoObservationRequest(
				CandidatePlan.ActiveRequest,
				ObservationError);
		}
		if (!bObservationCommitted)
		{
			if (!PublishProxyOnlyCandidate(*CandidatePlanner, CandidatePlan, OutError))
			{
				return false;
			}
			ObservationCommitRetryCooldownSeconds = 0.25;
			LastObservationWarning = FString::Printf(
				TEXT("Active-set replacement deferred; the previous authoritative set remains active. %s"),
				*ObservationError);
			OutError.Reset();
			return true;
		}
		ObservationPlanner = MoveTemp(CandidatePlanner);
		LastObservationPlan = MoveTemp(CandidatePlan);
		CurrentPresentationObservationPlan = LastObservationPlan;
		ClearRejectedObservation();
		return RefreshSnapshot(OutError);
	}

	bool FVisualDemoRuntime::PublishProxyOnlyCandidate(
		const FVisualObservationPlanner& CandidatePlanner,
		const FVisualObservationPlan& CandidatePlan,
		FString& OutError)
	{
		FVisualResidentPresentationFrame CandidatePresentationFrame;
		if (!FVisualResidentPresentationPlanner::BuildFrame(
			Layout,
			CandidatePlan,
			Snapshot,
			SelectedResidentID,
			Config.Presentation,
			CandidatePresentationFrame,
			OutError))
		{
			return Fail(OutError, OutError);
		}

		ObservationPlanner->CommitProxyHistoryFrom(CandidatePlanner);
		CurrentPresentationObservationPlan = CandidatePlan;
		PresentationFrame = MoveTemp(CandidatePresentationFrame);
		bHasPresentationFrame = true;
		OutError.Reset();
		return true;
	}

	bool FVisualDemoRuntime::RequestSelectedResident(
		const FResidentID ResidentID,
		FString& OutError)
	{
		if (State != EVisualDemoRuntimeState::Running || ResidentID <= 0)
		{
			OutError = TEXT("Resident selection requires a running Demo and a positive ResidentID.");
			return false;
		}
		bool bVisible = false;
		for (const FVisualResidentPresentationEntry& Entry : PresentationFrame.LowLevelProxies)
		{
			bVisible |= Entry.ResidentID == ResidentID;
		}
		for (const FVisualResidentPresentationEntry& Entry : PresentationFrame.ActiveActors)
		{
			bVisible |= Entry.ResidentID == ResidentID;
		}
		if (!bVisible)
		{
			OutError = TEXT("Only a currently displayed proxy or full NPC Actor can be selected.");
			return false;
		}
		const FResidentID PreviousSelectedResidentID = SelectedResidentID;
		SelectedResidentID = ResidentID;
		if (!RefreshPresentationFrame(OutError))
		{
			SelectedResidentID = PreviousSelectedResidentID;
			FString RestoreError;
			RefreshPresentationFrame(RestoreError);
			return false;
		}
		return true;
	}

	void FVisualDemoRuntime::ClearSelectedResident()
	{
		SelectedResidentID = 0;
		FString IgnoredError;
		RefreshPresentationFrame(IgnoredError);
	}

	bool FVisualDemoRuntime::RequestPaused(const bool bInPaused, FString& OutError)
	{
		if (State != EVisualDemoRuntimeState::Running)
		{
			OutError = TEXT("Pause requests require a running Day 0 to Day 60 Demo.");
			return false;
		}
		bPaused = bInPaused;
		OutError.Reset();
		return true;
	}

	bool FVisualDemoRuntime::RequestTimeScale(const int32 InTimeScale, FString& OutError)
	{
		if (InTimeScale != 1 && InTimeScale != 2 && InTimeScale != 4)
		{
			OutError = TEXT("The visual Demo time scale must be 1x, 2x, or 4x.");
			return false;
		}
		TimeScale = InTimeScale;
		OutError.Reset();
		return true;
	}

	bool FVisualDemoRuntime::CopySnapshot(FUnifiedDemoSnapshot& OutSnapshot) const
	{
		if (!bHasSnapshot)
		{
			return false;
		}
		OutSnapshot = Snapshot;
		return true;
	}

	bool FVisualDemoRuntime::CopyPresentationFrame(
		FVisualResidentPresentationFrame& OutFrame) const
	{
		if (!bHasPresentationFrame)
		{
			return false;
		}
		OutFrame = PresentationFrame;
		return true;
	}

	bool FVisualDemoRuntime::CopyObservationLog(
		TArray<FUnifiedDemoObservationRecord>& OutRecords,
		FString& OutError) const
	{
		if (!Session)
		{
			OutError = TEXT("The visual Demo has no authoritative session observation log.");
			return false;
		}
		return Session->CopyDemoObservationLog(OutRecords, OutError);
	}

	const TCHAR* FVisualDemoRuntime::GetStateName() const
	{
		switch (State)
		{
		case EVisualDemoRuntimeState::Uninitialized: return TEXT("Uninitialized");
		case EVisualDemoRuntimeState::Prewarming: return TEXT("Prewarming");
		case EVisualDemoRuntimeState::Running: return TEXT("Running");
		case EVisualDemoRuntimeState::Complete: return TEXT("Complete");
		case EVisualDemoRuntimeState::Failed: return TEXT("Failed");
		default: return TEXT("Unknown");
		}
	}

	bool FVisualDemoRuntime::StepSession(FString& OutError)
	{
		if (!Session || !Session->StepHour(OutError))
		{
			return Fail(OutError, OutError);
		}
		LastTickStepCount = 1;
		if (!RefreshSnapshot(OutError))
		{
			return false;
		}
		if (Session->IsComplete())
		{
			State = EVisualDemoRuntimeState::Complete;
			bPaused = true;
			PendingHourSteps = 0.0;
		}
		OutError.Reset();
		return true;
	}

	bool FVisualDemoRuntime::RefreshSnapshot(FString& OutError)
	{
		if (!Session || !Session->BuildDemoSnapshot(Snapshot, OutError))
		{
			return Fail(OutError, OutError);
		}
		bHasSnapshot = true;
		if (!RefreshPresentationFrame(OutError))
		{
			return Fail(OutError, OutError);
		}
		return true;
	}

	bool FVisualDemoRuntime::RefreshPresentationFrame(FString& OutError)
	{
		if (!bHasSnapshot || !FVisualResidentPresentationPlanner::BuildFrame(
			Layout,
			CurrentPresentationObservationPlan,
			Snapshot,
			SelectedResidentID,
			Config.Presentation,
			PresentationFrame,
			OutError))
		{
			bHasPresentationFrame = false;
			return false;
		}
		bHasPresentationFrame = true;
		OutError.Reset();
		return true;
	}

	bool FVisualDemoRuntime::Fail(const FString& Error, FString& OutError)
	{
		State = EVisualDemoRuntimeState::Failed;
		LastError = Error.IsEmpty() ? TEXT("Visual Demo runtime failed without an error message.") : Error;
		OutError = LastError;
		return false;
	}

	void FVisualDemoRuntime::ClearRejectedObservation()
	{
		LastObservationWarning.Reset();
		ObservationCommitRetryCooldownSeconds = 0.0;
	}
}
