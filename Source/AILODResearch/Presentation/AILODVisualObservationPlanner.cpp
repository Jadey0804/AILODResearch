// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualObservationPlanner.h"

namespace AILOD
{
	FResidentID FVisualTelescopeFocusGate::Update(
		const bool bEnabled,
		const FResidentID CenterResidentID,
		const double RealDeltaSeconds,
		const bool bStreamingReady,
		const double RequiredFocusSeconds)
	{
		if (!bEnabled || CenterResidentID <= 0 || RequiredFocusSeconds <= 0.0)
		{
			Reset();
			return 0;
		}
		if (Status.CenterResidentID != CenterResidentID)
		{
			Status = {};
			Status.CenterResidentID = CenterResidentID;
		}
		Status.FocusedRealSeconds += FMath::Max(0.0, RealDeltaSeconds);
		Status.bStreamingReady = bStreamingReady;
		return Status.bStreamingReady && Status.FocusedRealSeconds >= RequiredFocusSeconds
			? Status.CenterResidentID
			: 0;
	}

	void FVisualTelescopeFocusGate::Reset()
	{
		Status = {};
	}

	FVisualObservationPlanner::FVisualObservationPlanner(
		const FVisualWorldLayout& InLayout,
		const FVisualObservationPlannerConfig& InConfig)
		: Layout(InLayout)
		, Config(InConfig)
	{
	}

	bool FVisualObservationPlanner::ValidateConfig(FString& OutError) const
	{
		if (!Layout.IsBuilt()
			|| Config.NormalProxyBudget <= 0
			|| Config.NormalActiveBudget < 0
			|| Config.TelescopeProxyBudget <= 0
			|| Config.TelescopeActiveBudget <= 0
			|| Config.ActiveHardCap != 50
			|| Config.NormalQueryCandidateLimit < Config.NormalProxyBudget
			|| Config.TelescopeQueryCandidateLimit < Config.TelescopeProxyBudget
			|| Config.NormalActiveBudget + Config.TelescopeActiveBudget + 1 > Config.ActiveHardCap
			|| Config.ExitDistanceMultiplier < 1.0)
		{
			OutError = TEXT("Visual observation budgets require a built layout, the frozen Active cap of 50, and room for normal, telescope, and one tracked resident.");
			return false;
		}
		return true;
	}

	bool FVisualObservationPlanner::SetTrackedResident(
		const FResidentID ResidentID,
		FString& OutError)
	{
		if (ResidentID <= 0 || Layout.FindResident(ResidentID) == nullptr)
		{
			OutError = TEXT("The tracked visual resident must exist in the fixed Visual World Layout.");
			return false;
		}
		TrackedResidentID = ResidentID;
		OutError.Reset();
		return true;
	}

	void FVisualObservationPlanner::ClearTrackedResident()
	{
		TrackedResidentID = 0;
	}

	void FVisualObservationPlanner::Reset()
	{
		TrackedResidentID = 0;
		PreviousNormalProxyIDs.Reset();
		PreviousNormalActiveIDs.Reset();
		PreviousTelescopeProxyIDs.Reset();
		PreviousDesiredActiveIDs.Reset();
		PreviousTrackedResidentID = 0;
	}

	bool FVisualObservationPlanner::QueryView(
		const FVisualObservationView& View,
		const int32 CandidateLimit,
		const bool bPreferCenter,
		TArray<FVisualSpatialCandidate>& OutCandidates,
		FVisualSpatialQueryDiagnostics& OutDiagnostics,
		FString& OutError) const
	{
		FVisualConeQuery Query;
		Query.Origin = View.Origin;
		Query.Forward = View.Forward;
		Query.MaxDistance = View.EnterDistance * Config.ExitDistanceMultiplier;
		Query.HalfAngleDegrees = View.HalfAngleDegrees;
		Query.MaxResults = CandidateLimit;
		Query.ResultOrder = bPreferCenter
			? FVisualConeQuery::EResultOrder::CenterAlignment
			: FVisualConeQuery::EResultOrder::Distance;
		if (!Layout.QueryCone(Query, OutCandidates, OutDiagnostics, OutError))
		{
			return false;
		}
		if (View.MinimumDistance > 0.0)
		{
			OutCandidates.RemoveAll([&View](const FVisualSpatialCandidate& Candidate)
			{
				return Candidate.Distance < View.MinimumDistance;
			});
		}
		return true;
	}

	void FVisualObservationPlanner::SelectWithHysteresis(
		const TArray<FVisualSpatialCandidate>& Candidates,
		const TArray<FResidentID>& PreviousResidentIDs,
		const double EnterDistance,
		const int32 Budget,
		TArray<FVisualProxyCandidate>& OutSelected) const
	{
		OutSelected.Reset();
		if (Budget <= 0) return;
		TSet<FResidentID> PreviousSet;
		for (const FResidentID ResidentID : PreviousResidentIDs)
		{
			PreviousSet.Add(ResidentID);
		}
		for (const FVisualSpatialCandidate& Candidate : Candidates)
		{
			const bool bPrevious = PreviousSet.Contains(Candidate.ResidentID);
			if (!bPrevious && Candidate.Distance > EnterDistance) continue;
			FVisualProxyCandidate& Selected = OutSelected.AddDefaulted_GetRef();
			Selected.ResidentID = Candidate.ResidentID;
			Selected.Position = Candidate.Position;
			Selected.Distance = Candidate.Distance;
			Selected.ForwardAlignment = Candidate.ForwardAlignment;
			Selected.bRetainedByHysteresis = bPrevious && Candidate.Distance > EnterDistance;
		}
		OutSelected.Sort([&PreviousSet](const FVisualProxyCandidate& Left, const FVisualProxyCandidate& Right)
		{
			const bool bLeftPrevious = PreviousSet.Contains(Left.ResidentID);
			const bool bRightPrevious = PreviousSet.Contains(Right.ResidentID);
			if (bLeftPrevious != bRightPrevious) return bLeftPrevious;
			return Left.Distance == Right.Distance
				? Left.ResidentID < Right.ResidentID
				: Left.Distance < Right.Distance;
		});
		if (OutSelected.Num() > Budget)
		{
			OutSelected.SetNum(Budget, EAllowShrinking::No);
		}
	}

	bool FVisualObservationPlanner::PlanFrame(
		const FVisualObservationFrameInput& Input,
		FVisualObservationPlan& OutPlan,
		FString& OutError)
	{
		OutPlan = {};
		if (!ValidateConfig(OutError)) return false;
		if ((!Input.bNormalViewEnabled && !Input.bTelescopeEnabled && Input.TelescopePromotionResidentID != 0)
			|| (Input.bTelescopeEnabled && Input.TelescopePromotionResidentID < 0))
		{
			OutError = TEXT("Telescope promotion requires an enabled telescope and a non-negative ResidentID.");
			return false;
		}

		TArray<FVisualSpatialCandidate> NormalCandidates;
		TArray<FVisualSpatialCandidate> TelescopeCandidates;
		TArray<FVisualProxyCandidate> NormalActiveCandidates;
		if (Input.bNormalViewEnabled)
		{
			const bool bQuerySucceeded = Input.bNormalViewUsesRadius
				? Layout.QueryRadius(
					FVisualRadiusQuery{
						Input.NormalView.Origin,
						Input.NormalView.EnterDistance * Config.ExitDistanceMultiplier,
						Config.NormalQueryCandidateLimit },
					NormalCandidates,
					OutPlan.Diagnostics.NormalQuery,
					OutError)
				: QueryView(
					Input.NormalView,
					Config.NormalQueryCandidateLimit,
					false,
					NormalCandidates,
					OutPlan.Diagnostics.NormalQuery,
					OutError);
			if (!bQuerySucceeded)
			{
				return false;
			}
			SelectWithHysteresis(
				NormalCandidates,
				PreviousNormalProxyIDs,
				Input.NormalView.EnterDistance,
				Config.NormalProxyBudget,
				OutPlan.NormalProxyCandidates);
			SelectWithHysteresis(
				NormalCandidates,
				PreviousNormalActiveIDs,
				Input.NormalView.EnterDistance,
				Config.NormalActiveBudget,
				NormalActiveCandidates);
		}

		if (Input.bTelescopeEnabled)
		{
			if (!QueryView(
				Input.TelescopeView,
				Config.TelescopeQueryCandidateLimit,
				true,
				TelescopeCandidates,
				OutPlan.Diagnostics.TelescopeQuery,
				OutError))
			{
				return false;
			}
			TSet<FResidentID> PreviousTelescopeSet;
			for (const FResidentID ResidentID : PreviousTelescopeProxyIDs)
			{
				PreviousTelescopeSet.Add(ResidentID);
			}
			for (const FVisualSpatialCandidate& Candidate : TelescopeCandidates)
			{
				const bool bPrevious = PreviousTelescopeSet.Contains(Candidate.ResidentID);
				if (!bPrevious && Candidate.Distance > Input.TelescopeView.EnterDistance) continue;
				FVisualProxyCandidate& Proxy = OutPlan.TelescopeProxyCandidates.AddDefaulted_GetRef();
				Proxy.ResidentID = Candidate.ResidentID;
				Proxy.Position = Candidate.Position;
				Proxy.Distance = Candidate.Distance;
				Proxy.ForwardAlignment = Candidate.ForwardAlignment;
				Proxy.bRetainedByHysteresis = bPrevious && Candidate.Distance > Input.TelescopeView.EnterDistance;
			}
			OutPlan.TelescopeProxyCandidates.Sort([&PreviousTelescopeSet](
				const FVisualProxyCandidate& Left,
				const FVisualProxyCandidate& Right)
			{
				if (!FMath::IsNearlyEqual(Left.ForwardAlignment, Right.ForwardAlignment))
				{
					return Left.ForwardAlignment > Right.ForwardAlignment;
				}
				const bool bLeftPrevious = PreviousTelescopeSet.Contains(Left.ResidentID);
				const bool bRightPrevious = PreviousTelescopeSet.Contains(Right.ResidentID);
				if (bLeftPrevious != bRightPrevious) return bLeftPrevious;
				return Left.Distance == Right.Distance
					? Left.ResidentID < Right.ResidentID
					: Left.Distance < Right.Distance;
			});
			if (OutPlan.TelescopeProxyCandidates.Num() > Config.TelescopeProxyBudget)
			{
				OutPlan.TelescopeProxyCandidates.SetNum(Config.TelescopeProxyBudget, EAllowShrinking::No);
			}
			if (!OutPlan.TelescopeProxyCandidates.IsEmpty())
			{
				OutPlan.TelescopeCenterResidentID = OutPlan.TelescopeProxyCandidates[0].ResidentID;
			}
		}

		if (Input.TelescopePromotionResidentID != 0
			&& (!Input.bTelescopeEnabled
				|| Input.TelescopePromotionResidentID != OutPlan.TelescopeCenterResidentID))
		{
			OutError = TEXT("Only the current telescope center candidate can be promoted.");
			return false;
		}

		TSet<FResidentID> DesiredActiveSet;
		if (TrackedResidentID != 0)
		{
			DesiredActiveSet.Add(TrackedResidentID);
			const FVisualResidentPlacement* Tracked = Layout.FindResident(TrackedResidentID);
			OutPlan.TrackedResidentID = TrackedResidentID;
			OutPlan.TrackedPosition = Tracked->ProxyPosition;
		}
		for (const FVisualProxyCandidate& Candidate : NormalActiveCandidates)
		{
			DesiredActiveSet.Add(Candidate.ResidentID);
			OutPlan.Diagnostics.RetainedNormalActiveCount += Candidate.bRetainedByHysteresis ? 1 : 0;
		}
		const bool bUseTelescopeActiveContext = Input.TelescopePromotionResidentID != 0
			|| (TrackedResidentID != 0
				&& TrackedResidentID == OutPlan.TelescopeCenterResidentID);
		if (bUseTelescopeActiveContext)
		{
			const FResidentID TelescopeCenterResidentID = Input.TelescopePromotionResidentID != 0
				? Input.TelescopePromotionResidentID
				: TrackedResidentID;
			DesiredActiveSet.Add(TelescopeCenterResidentID);
			int32 TelescopeAdded = 1;
			for (const FVisualProxyCandidate& Candidate : OutPlan.TelescopeProxyCandidates)
			{
				if (Candidate.ResidentID == TelescopeCenterResidentID) continue;
				DesiredActiveSet.Add(Candidate.ResidentID);
				if (++TelescopeAdded >= Config.TelescopeActiveBudget) break;
			}
		}
		if (DesiredActiveSet.Num() > Config.ActiveHardCap)
		{
			OutError = TEXT("Visual observation planning exceeded the frozen Active cap of 50.");
			return false;
		}
		for (const FResidentID ResidentID : DesiredActiveSet)
		{
			OutPlan.ActiveRequest.DesiredActiveResidentIDs.Add(ResidentID);
		}
		OutPlan.ActiveRequest.DesiredActiveResidentIDs.Sort();
		OutPlan.ActiveRequest.TrackedResidentID = TrackedResidentID;
		OutPlan.Diagnostics.NormalProxyCount = OutPlan.NormalProxyCandidates.Num();
		OutPlan.Diagnostics.TelescopeProxyCount = OutPlan.TelescopeProxyCandidates.Num();
		OutPlan.Diagnostics.DesiredActiveCount = OutPlan.ActiveRequest.DesiredActiveResidentIDs.Num();
		OutPlan.Diagnostics.bActiveSetChanged = PreviousDesiredActiveIDs != OutPlan.ActiveRequest.DesiredActiveResidentIDs
			|| PreviousTrackedResidentID != TrackedResidentID;

		PreviousNormalProxyIDs.Reset();
		for (const FVisualProxyCandidate& Candidate : OutPlan.NormalProxyCandidates)
		{
			PreviousNormalProxyIDs.Add(Candidate.ResidentID);
		}
		PreviousNormalActiveIDs.Reset();
		for (const FVisualProxyCandidate& Candidate : NormalActiveCandidates)
		{
			PreviousNormalActiveIDs.Add(Candidate.ResidentID);
		}
		PreviousTelescopeProxyIDs.Reset();
		for (const FVisualProxyCandidate& Candidate : OutPlan.TelescopeProxyCandidates)
		{
			PreviousTelescopeProxyIDs.Add(Candidate.ResidentID);
		}
		PreviousDesiredActiveIDs = OutPlan.ActiveRequest.DesiredActiveResidentIDs;
		PreviousTrackedResidentID = TrackedResidentID;
		OutError.Reset();
		return true;
	}

	void FVisualObservationPlanner::CommitProxyHistoryFrom(
		const FVisualObservationPlanner& PlannedCandidate)
	{
		PreviousNormalProxyIDs = PlannedCandidate.PreviousNormalProxyIDs;
		PreviousTelescopeProxyIDs = PlannedCandidate.PreviousTelescopeProxyIDs;
	}
}
