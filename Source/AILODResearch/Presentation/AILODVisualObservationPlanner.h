// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODVisualWorldLayout.h"
#include "../Simulation/AILODUnifiedSimulation.h"

namespace AILOD
{
	struct FVisualObservationPlannerConfig
	{
		int32 NormalProxyBudget = 128;
		int32 NormalActiveBudget = 35;
		int32 TelescopeProxyBudget = 32;
		int32 TelescopeActiveBudget = 5;
		int32 ActiveHardCap = 50;
		int32 NormalQueryCandidateLimit = 512;
		int32 TelescopeQueryCandidateLimit = 256;
		double ExitDistanceMultiplier = 1.2;
		double NormalImmediatePromotionDistance = 3000.0;
		double NormalPromotionDwellSeconds = 0.75;
		double NormalDemotionGraceSeconds = 2.0;
	};

	struct FVisualObservationView
	{
		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D Forward = FVector2D(1.0, 0.0);
		double MinimumDistance = 0.0;
		double EnterDistance = 20000.0;
		double HalfAngleDegrees = 60.0;
	};

	struct FVisualObservationFrameInput
	{
		bool bNormalViewEnabled = true;
		bool bNormalViewUsesRadius = false;
		FVisualObservationView NormalView;
		bool bTelescopeEnabled = false;
		FVisualObservationView TelescopeView;
		FResidentID TelescopePromotionResidentID = 0;
		bool bClearTrackedResident = false;
		double RealDeltaSeconds = 0.0;
		FResidentID PriorityResidentID = 0;
		bool bHasNormalImmediateOrigin = false;
		FVector2D NormalImmediateOrigin = FVector2D::ZeroVector;
		TArray<FVector2D> NormalVisibleGroundPolygon;
	};

	struct FVisualTelescopeFocusStatus
	{
		FResidentID CenterResidentID = 0;
		double FocusedRealSeconds = 0.0;
		bool bStreamingReady = false;
	};

	class FVisualTelescopeFocusGate
	{
	public:
		FResidentID Update(
			bool bEnabled,
			FResidentID CenterResidentID,
			double RealDeltaSeconds,
			bool bStreamingReady,
			double RequiredFocusSeconds);
		void Reset();
		const FVisualTelescopeFocusStatus& GetStatus() const { return Status; }

	private:
		FVisualTelescopeFocusStatus Status;
	};

	struct FVisualProxyCandidate
	{
		FResidentID ResidentID = 0;
		FVector2D Position = FVector2D::ZeroVector;
		double Distance = 0.0;
		double ForwardAlignment = -1.0;
		bool bRetainedByHysteresis = false;
	};

	struct FVisualObservationDiagnostics
	{
		FVisualSpatialQueryDiagnostics NormalQuery;
		FVisualSpatialQueryDiagnostics TelescopeQuery;
		int32 NormalProxyCount = 0;
		int32 TelescopeProxyCount = 0;
		int32 DesiredActiveCount = 0;
		int32 RetainedNormalActiveCount = 0;
		int32 NormalVisibleCandidateCount = 0;
		int32 NormalEligibleActiveCount = 0;
		int32 NormalImmediateCandidateCount = 0;
		int32 NormalObservationStateCount = 0;
		bool bNormalActiveBudgetSaturated = false;
		bool bNormalImmediateBudgetOverflow = false;
		bool bActiveSetChanged = false;
	};

	struct FVisualObservationPlan
	{
		TArray<FVisualProxyCandidate> NormalProxyCandidates;
		TArray<FVisualProxyCandidate> TelescopeProxyCandidates;
		FResidentID TelescopeCenterResidentID = 0;
		FResidentID TrackedResidentID = 0;
		FVector2D TrackedPosition = FVector2D::ZeroVector;
		FUnifiedDemoObservationRequest ActiveRequest;
		FVisualObservationDiagnostics Diagnostics;
	};

	class FVisualObservationPlanner
	{
	public:
		FVisualObservationPlanner(
			const FVisualWorldLayout& InLayout,
			const FVisualObservationPlannerConfig& InConfig = {});

		bool SetTrackedResident(FResidentID ResidentID, FString& OutError);
		void ClearTrackedResident();
		void Reset();
		FResidentID GetTrackedResidentID() const { return TrackedResidentID; }

		bool PlanFrame(
			const FVisualObservationFrameInput& Input,
			FVisualObservationPlan& OutPlan,
			FString& OutError);
		void CommitProxyHistoryFrom(const FVisualObservationPlanner& PlannedCandidate);

	private:
		bool ValidateConfig(FString& OutError) const;
		bool QueryView(
			const FVisualObservationView& View,
			int32 CandidateLimit,
			bool bPreferCenter,
			TArray<FVisualSpatialCandidate>& OutCandidates,
			FVisualSpatialQueryDiagnostics& OutDiagnostics,
			FString& OutError) const;
		void SelectWithHysteresis(
			const TArray<FVisualSpatialCandidate>& Candidates,
			const TArray<FResidentID>& PreviousResidentIDs,
			double EnterDistance,
			int32 Budget,
			TArray<FVisualProxyCandidate>& OutSelected) const;
		bool SelectNormalActiveCandidates(
			const FVisualObservationFrameInput& Input,
			const TArray<FVisualSpatialCandidate>& QueriedCandidates,
			const TArray<FVisualSpatialCandidate>& VisibleCandidates,
			TArray<FVisualProxyCandidate>& OutSelected,
			FVisualObservationDiagnostics& OutDiagnostics,
			FString& OutError);

		struct FNormalObservationState
		{
			double VisibleRealSeconds = 0.0;
			double HiddenRealSeconds = 0.0;
		};

		const FVisualWorldLayout& Layout;
		FVisualObservationPlannerConfig Config;
		FResidentID TrackedResidentID = 0;
		TArray<FResidentID> PreviousNormalProxyIDs;
		TArray<FResidentID> PreviousNormalActiveIDs;
		TArray<FResidentID> PreviousTelescopeProxyIDs;
		TArray<FResidentID> PreviousDesiredActiveIDs;
		FResidentID PreviousTrackedResidentID = 0;
		TMap<FResidentID, FNormalObservationState> NormalObservationStates;
	};
}
