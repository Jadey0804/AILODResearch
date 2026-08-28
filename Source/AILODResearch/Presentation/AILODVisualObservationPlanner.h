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
	};

	struct FVisualObservationView
	{
		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D Forward = FVector2D(1.0, 0.0);
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

		const FVisualWorldLayout& Layout;
		FVisualObservationPlannerConfig Config;
		FResidentID TrackedResidentID = 0;
		TArray<FResidentID> PreviousNormalProxyIDs;
		TArray<FResidentID> PreviousNormalActiveIDs;
		TArray<FResidentID> PreviousTelescopeProxyIDs;
		TArray<FResidentID> PreviousDesiredActiveIDs;
		FResidentID PreviousTrackedResidentID = 0;
	};
}
