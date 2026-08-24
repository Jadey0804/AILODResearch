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
		State = EVisualDemoRuntimeState::Uninitialized;
		LastError.Reset();
		PendingHourSteps = 0.0;
		TimeScale = 1;
		LastTickStepCount = 0;
		bPaused = false;
		bHasSnapshot = false;

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
		ObservationPlanner = MakeUnique<FVisualObservationPlanner>(Layout);

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

		FVisualObservationPlan Plan;
		if (!ObservationPlanner->PlanFrame(Input, Plan, OutError))
		{
			return Fail(OutError, OutError);
		}
		LastObservationPlan = MoveTemp(Plan);
		if (!LastObservationPlan.Diagnostics.bActiveSetChanged)
		{
			OutError.Reset();
			return true;
		}
		if (!Session->SubmitDemoObservationRequest(LastObservationPlan.ActiveRequest, OutError))
		{
			return Fail(OutError, OutError);
		}
		return RefreshSnapshot(OutError);
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
}
