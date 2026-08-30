// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODVisualObservationPlanner.h"
#include "AILODVisualResidentPresentation.h"

namespace AILOD
{
	struct FVisualDemoRuntimeConfig
	{
		int32 SimulationSeed = 20260810;
		int32 PopulationPerKingdom = 10000;
		FVisualWorldLayoutConfig Layout;
		FVisualObservationPlannerConfig Observation;
		FVisualResidentPresentationConfig Presentation;
#if WITH_DEV_AUTOMATION_TESTS
		bool bRejectNextObservationCommitForTest = false;
#endif
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
		bool RequestSelectedResident(FResidentID ResidentID, FString& OutError);
		void ClearSelectedResident();
		bool RequestPaused(bool bInPaused, FString& OutError);
		bool RequestTimeScale(int32 InTimeScale, FString& OutError);

		bool CopySnapshot(FUnifiedDemoSnapshot& OutSnapshot) const;
		bool CopyPresentationFrame(FVisualResidentPresentationFrame& OutFrame) const;
		bool CopyObservationLog(
			TArray<FUnifiedDemoObservationRecord>& OutRecords,
			FString& OutError) const;
		EVisualDemoRuntimeState GetState() const { return State; }
		const TCHAR* GetStateName() const;
		bool IsPaused() const { return bPaused; }
		int32 GetTimeScale() const { return TimeScale; }
		double GetPresentationPlaybackRate() const
		{
			return State == EVisualDemoRuntimeState::Running && !bPaused
				? static_cast<double>(TimeScale)
				: 0.0;
		}
		double GetPendingHourSteps() const { return PendingHourSteps; }
		int32 GetLastTickStepCount() const { return LastTickStepCount; }
		const FString& GetLastError() const { return LastError; }
		const FString& GetLastObservationWarning() const { return LastObservationWarning; }
		const FVisualWorldLayout& GetLayout() const { return Layout; }
		const FVisualObservationPlan& GetLastObservationPlan() const { return LastObservationPlan; }
		const FVisualObservationPlan& GetCurrentPresentationObservationPlan() const
		{
			return CurrentPresentationObservationPlan;
		}
#if WITH_DEV_AUTOMATION_TESTS
		void RejectNextObservationCommitForTest()
		{
			Config.bRejectNextObservationCommitForTest = true;
		}
#endif

	private:
		bool StepSession(FString& OutError);
		bool RefreshSnapshot(FString& OutError);
		bool RefreshPresentationFrame(FString& OutError);
		bool PublishProxyOnlyCandidate(
			const FVisualObservationPlanner& CandidatePlanner,
			const FVisualObservationPlan& CandidatePlan,
			FString& OutError);
		bool Fail(const FString& Error, FString& OutError);
		void ClearRejectedObservation();

		FVisualDemoRuntimeConfig Config;
		FVisualWorldLayout Layout;
		TUniquePtr<FVisualObservationPlanner> ObservationPlanner;
		TUniquePtr<FUnifiedSimulationSession> Session;
		FUnifiedDemoSnapshot Snapshot;
		FVisualObservationPlan LastObservationPlan;
		FVisualObservationPlan CurrentPresentationObservationPlan;
		FVisualResidentPresentationFrame PresentationFrame;
		FResidentID SelectedResidentID = 0;
		EVisualDemoRuntimeState State = EVisualDemoRuntimeState::Uninitialized;
		FString LastError;
		FString LastObservationWarning;
		double PendingHourSteps = 0.0;
		double ObservationCommitRetryCooldownSeconds = 0.0;
		int32 TimeScale = 1;
		int32 LastTickStepCount = 0;
		bool bPaused = false;
		bool bHasSnapshot = false;
		bool bHasPresentationFrame = false;
	};
}
