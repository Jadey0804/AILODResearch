// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODVisualObservationPlanner.h"

namespace AILOD
{
	struct FVisualDemoRuntimeConfig
	{
		int32 SimulationSeed = 20260810;
		int32 PopulationPerKingdom = 10000;
		FVisualWorldLayoutConfig Layout;
	};

	enum class EVisualDemoRuntimeState : uint8
	{
		Uninitialized,
		Prewarming,
		Running,
		Complete,
		Failed
	};

	class FVisualDemoRuntime
	{
	public:
		bool Initialize(const FVisualDemoRuntimeConfig& InConfig, FString& OutError);
		bool Restart(FString& OutError);
		bool Tick(double RealDeltaSeconds, FString& OutError);
		bool SubmitObservationFrame(const FVisualObservationFrameInput& Input, FString& OutError);
		bool RequestPaused(bool bInPaused, FString& OutError);
		bool RequestTimeScale(int32 InTimeScale, FString& OutError);

		bool CopySnapshot(FUnifiedDemoSnapshot& OutSnapshot) const;
		EVisualDemoRuntimeState GetState() const { return State; }
		const TCHAR* GetStateName() const;
		bool IsPaused() const { return bPaused; }
		int32 GetTimeScale() const { return TimeScale; }
		double GetPendingHourSteps() const { return PendingHourSteps; }
		int32 GetLastTickStepCount() const { return LastTickStepCount; }
		const FString& GetLastError() const { return LastError; }
		const FVisualWorldLayout& GetLayout() const { return Layout; }
		const FVisualObservationPlan& GetLastObservationPlan() const { return LastObservationPlan; }

	private:
		bool StepSession(FString& OutError);
		bool RefreshSnapshot(FString& OutError);
		bool Fail(const FString& Error, FString& OutError);

		FVisualDemoRuntimeConfig Config;
		FVisualWorldLayout Layout;
		TUniquePtr<FVisualObservationPlanner> ObservationPlanner;
		TUniquePtr<FUnifiedSimulationSession> Session;
		FUnifiedDemoSnapshot Snapshot;
		FVisualObservationPlan LastObservationPlan;
		EVisualDemoRuntimeState State = EVisualDemoRuntimeState::Uninitialized;
		FString LastError;
		double PendingHourSteps = 0.0;
		int32 TimeScale = 1;
		int32 LastTickStepCount = 0;
		bool bPaused = false;
		bool bHasSnapshot = false;
	};
}
