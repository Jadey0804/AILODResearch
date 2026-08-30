// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualResidentPresentation.h"

namespace AILOD
{
	namespace
	{
		EVisualResidentAnchor AnchorForAction(const EIndividualAction Action)
		{
			switch (Action)
			{
			case EIndividualAction::Work:
			case EIndividualAction::BuyWood:
			case EIndividualAction::ChopWood:
				return EVisualResidentAnchor::Work;
			case EIndividualAction::StartRepair:
			case EIndividualAction::ContinueRepair:
				return EVisualResidentAnchor::Home;
			default:
				return EVisualResidentAnchor::ProxyRoad;
			}
		}

		bool UsesWalkingPlaceholder(const EIndividualAction Action)
		{
			return Action == EIndividualAction::Routine
				|| Action == EIndividualAction::Work
				|| Action == EIndividualAction::BuyWood
				|| Action == EIndividualAction::ChopWood;
		}

		bool BuildEntry(
			const FVisualWorldLayout& Layout,
			const FResidentID ResidentID,
			const FUnifiedDemoResidentSnapshot* ActiveState,
			const double LocalRouteHalfLength,
			FVisualResidentPresentationEntry& OutEntry,
			FString& OutError)
		{
			const FVisualResidentPlacement* Placement = Layout.FindResident(ResidentID);
			if (Placement == nullptr)
			{
				OutError = TEXT("A visual resident presentation entry requires a real ResidentID in the fixed layout.");
				return false;
			}
			const FVisualRoadRecord* Road = Layout.FindRoad(Placement->ProxyRoadID);
			const FVisualHomeSlotRecord* Home = Layout.FindHomeSlot(Placement->VisualHomeSlotID);
			const FVisualWorkAnchorRecord* Work = Layout.FindWorkAnchor(Placement->WorkAnchorID);
			if (Road == nullptr || Home == nullptr || Work == nullptr)
			{
				OutError = TEXT("A visual resident presentation entry could not resolve its fixed road, home slot, or work anchor.");
				return false;
			}
			if (ActiveState != nullptr && ActiveState->HomeID != Placement->HomeID)
			{
				OutError = TEXT("An Active visual actor cannot bind a ResidentID to a different HomeID.");
				return false;
			}

			OutEntry = {};
			OutEntry.ResidentID = ResidentID;
			OutEntry.HomeID = Placement->HomeID;
			OutEntry.VisualHomeSlotID = Placement->VisualHomeSlotID;
			OutEntry.WorkAnchorID = Placement->WorkAnchorID;
			OutEntry.ProxyRoadID = Placement->ProxyRoadID;
			OutEntry.ProxyPosition = Placement->ProxyPosition;

			const FVector2D RoadDelta = Road->End - Road->Start;
			const double RoadLength = RoadDelta.Size();
			const FVector2D RoadDirection = RoadLength > UE_SMALL_NUMBER
				? RoadDelta / RoadLength
				: FVector2D(1.0, 0.0);
			const double Along = FMath::Clamp(
				FVector2D::DotProduct(Placement->ProxyPosition - Road->Start, RoadDirection),
				0.0,
				RoadLength);
			OutEntry.RouteStart = Road->Start + RoadDirection * FMath::Max(0.0, Along - LocalRouteHalfLength);
			OutEntry.RouteEnd = Road->Start + RoadDirection * FMath::Min(RoadLength, Along + LocalRouteHalfLength);
			OutEntry.DestinationPosition = Placement->ProxyPosition;
			OutEntry.FacingDegrees = FMath::RadiansToDegrees(FMath::Atan2(RoadDirection.Y, RoadDirection.X));
			OutEntry.bPlaceholderMoves = ActiveState == nullptr;

			if (ActiveState != nullptr)
			{
				OutEntry.bActiveActor = true;
				OutEntry.bTracked = ActiveState->bTracked;
				OutEntry.bHasActiveState = true;
				OutEntry.ActiveState = *ActiveState;
				OutEntry.DestinationAnchor = AnchorForAction(ActiveState->CurrentAction);
				OutEntry.bPlaceholderMoves = UsesWalkingPlaceholder(ActiveState->CurrentAction);
				if (OutEntry.DestinationAnchor == EVisualResidentAnchor::Home)
				{
					OutEntry.DestinationPosition = Home->Position;
				}
				else if (OutEntry.DestinationAnchor == EVisualResidentAnchor::Work)
				{
					const EVisualWorkAnchorType DestinationType =
						ActiveState->CurrentAction == EIndividualAction::ChopWood
						? EVisualWorkAnchorType::LumberCamp
						: ActiveState->CurrentAction == EIndividualAction::BuyWood
							? EVisualWorkAnchorType::TimberPurchase
							: Work->Type;
					const FVisualWorkAnchorRecord* ActionWorkAnchor = Layout.FindWorkAnchor(
						Placement->DistrictID,
						DestinationType);
					if (ActionWorkAnchor == nullptr)
					{
						OutError = TEXT("An Active visual action could not resolve its district work anchor.");
						return false;
					}
					OutEntry.DestinationPosition = ActionWorkAnchor->Position;
				}
				const FVector2D DestinationDelta = OutEntry.DestinationPosition - OutEntry.ProxyPosition;
				if (!DestinationDelta.IsNearlyZero())
				{
					OutEntry.FacingDegrees = FMath::RadiansToDegrees(FMath::Atan2(
						DestinationDelta.Y,
						DestinationDelta.X));
				}
			}
			OutError.Reset();
			return true;
		}
	}

	bool FVisualResidentPresentationPlanner::BuildFrame(
		const FVisualWorldLayout& Layout,
		const FVisualObservationPlan& ObservationPlan,
		const FUnifiedDemoSnapshot& SimulationSnapshot,
		const FResidentID SelectedResidentID,
		const FVisualResidentPresentationConfig& Config,
		FVisualResidentPresentationFrame& OutFrame,
		FString& OutError)
	{
		OutFrame = {};
		if (!Layout.IsBuilt()
			|| Config.LowLevelProxyCapacity <= 0
			|| Config.ActiveActorCapacity != 50
			|| Config.LocalRouteHalfLength <= 0.0
			|| SimulationSnapshot.bFormalRun
			|| SimulationSnapshot.ModelSpecVersion != TEXT("1.9")
			|| SimulationSnapshot.DeterministicDigestVersion != TEXT("1.9-domain-v1")
			|| SimulationSnapshot.ActiveCount != SimulationSnapshot.ActiveResidents.Num()
			|| SimulationSnapshot.ActiveCount > Config.ActiveActorCapacity)
		{
			OutError = TEXT("Resident presentation requires a read-only v1.9 Demo snapshot and the frozen 50-Actor capacity.");
			return false;
		}
		if (ObservationPlan.Diagnostics.NormalQuery.bScannedResidentCatalog
			|| ObservationPlan.Diagnostics.TelescopeQuery.bScannedResidentCatalog)
		{
			OutError = TEXT("Resident presentation refuses an observation plan that scanned the complete resident catalog.");
			return false;
		}

		OutFrame.GameTime = SimulationSnapshot.GameTime;
		OutFrame.Diagnostics.ActorPoolCapacity = Config.ActiveActorCapacity;
		OutFrame.Diagnostics.VisitedResidentEntryCount =
			ObservationPlan.Diagnostics.NormalQuery.VisitedResidentEntryCount
			+ ObservationPlan.Diagnostics.TelescopeQuery.VisitedResidentEntryCount;

		TSet<FResidentID> ActiveResidentIDs;
		TArray<const FUnifiedDemoResidentSnapshot*> SortedActiveResidents;
		SortedActiveResidents.Reserve(SimulationSnapshot.ActiveResidents.Num());
		for (const FUnifiedDemoResidentSnapshot& Resident : SimulationSnapshot.ActiveResidents)
		{
			if (Resident.ResidentID <= 0 || ActiveResidentIDs.Contains(Resident.ResidentID))
			{
				OutError = TEXT("Every full NPC Actor requires one unique positive Active ResidentID.");
				return false;
			}
			ActiveResidentIDs.Add(Resident.ResidentID);
			SortedActiveResidents.Add(&Resident);
		}
		SortedActiveResidents.Sort([](
			const FUnifiedDemoResidentSnapshot& Left,
			const FUnifiedDemoResidentSnapshot& Right)
		{
			return Left.ResidentID < Right.ResidentID;
		});
		for (const FUnifiedDemoResidentSnapshot* Resident : SortedActiveResidents)
		{
			FVisualResidentPresentationEntry& Entry = OutFrame.ActiveActors.AddDefaulted_GetRef();
			if (!BuildEntry(Layout, Resident->ResidentID, Resident, Config.LocalRouteHalfLength, Entry, OutError))
			{
				OutFrame = {};
				return false;
			}
		}

		TSet<FResidentID> ProxyResidentIDs;
		auto AddProxyCandidates = [&](const TArray<FVisualProxyCandidate>& Candidates)
		{
			for (const FVisualProxyCandidate& Candidate : Candidates)
			{
				if (ActiveResidentIDs.Contains(Candidate.ResidentID)
					|| ProxyResidentIDs.Contains(Candidate.ResidentID))
				{
					continue;
				}
				if (OutFrame.LowLevelProxies.Num() >= Config.LowLevelProxyCapacity)
				{
					break;
				}
				FVisualResidentPresentationEntry Entry;
				if (!BuildEntry(Layout, Candidate.ResidentID, nullptr, Config.LocalRouteHalfLength, Entry, OutError))
				{
					return false;
				}
				ProxyResidentIDs.Add(Candidate.ResidentID);
				OutFrame.LowLevelProxies.Add(MoveTemp(Entry));
			}
			return true;
		};
		if (!AddProxyCandidates(ObservationPlan.NormalProxyCandidates)
			|| !AddProxyCandidates(ObservationPlan.TelescopeProxyCandidates))
		{
			OutFrame = {};
			return false;
		}

		OutFrame.SelectedResidentID = SelectedResidentID;
		if (SelectedResidentID != 0)
		{
			const FUnifiedDemoResidentSnapshot* SelectedActiveState = nullptr;
			for (const FUnifiedDemoResidentSnapshot& Resident : SimulationSnapshot.ActiveResidents)
			{
				if (Resident.ResidentID == SelectedResidentID)
				{
					SelectedActiveState = &Resident;
					break;
				}
			}
			if (!BuildEntry(
				Layout,
				SelectedResidentID,
				SelectedActiveState,
				Config.LocalRouteHalfLength,
				OutFrame.SelectedResident,
				OutError))
			{
				OutFrame = {};
				return false;
			}
			OutFrame.bHasSelectedResident = true;
		}

		OutFrame.Diagnostics.LowLevelProxyCount = OutFrame.LowLevelProxies.Num();
		OutFrame.Diagnostics.ActiveActorCount = OutFrame.ActiveActors.Num();
		OutError.Reset();
		return true;
	}

	FVector2D FVisualResidentPresentationPlanner::ResolveLocalRoutePosition(
		const FVisualResidentPresentationEntry& Entry,
		const double RouteAlpha)
	{
		return FVector2D::Distance(Entry.RouteStart, Entry.RouteEnd) > UE_SMALL_NUMBER
			? FMath::Lerp(Entry.RouteStart, Entry.RouteEnd, FMath::Clamp(RouteAlpha, 0.0, 1.0))
			: Entry.ProxyPosition;
	}

	FVisualResidentMotionState FVisualResidentPresentationPlanner::MakeInitialMotionState(
		const FVisualResidentPresentationEntry& Entry)
	{
		FVisualResidentMotionState State;
		State.ResidentID = Entry.ResidentID;
		const uint64 ResidentSeed = static_cast<uint64>(Entry.ResidentID)
			^ (static_cast<uint64>(Entry.ResidentID) >> 32);
		State.RouteAlpha = static_cast<double>(ResidentSeed % 1000ull) / 999.0;
		State.AnimationSeconds = static_cast<double>((ResidentSeed >> 10) % 628ull) / 100.0;
		State.FacingDegrees = Entry.FacingDegrees;
		State.RouteDirection = (ResidentSeed & 1ull) == 0ull ? 1 : -1;
		return State;
	}

	void FVisualResidentPresentationPlanner::AdvanceMotionState(
		const FVisualResidentPresentationEntry& Entry,
		const double DeltaSeconds,
		const double WalkSpeedCentimetersPerSecond,
		FVisualResidentMotionState& InOutState)
	{
		const double SafeDeltaSeconds = FMath::Max(0.0, DeltaSeconds);
		InOutState.AnimationSeconds += SafeDeltaSeconds;
		if (Entry.bPlaceholderMoves)
		{
			const double RouteLength = FVector2D::Distance(Entry.RouteStart, Entry.RouteEnd);
			if (RouteLength > UE_SMALL_NUMBER)
			{
				InOutState.RouteAlpha += InOutState.RouteDirection
					* FMath::Max(0.0, WalkSpeedCentimetersPerSecond)
					* SafeDeltaSeconds / RouteLength;
				if (InOutState.RouteAlpha >= 1.0)
				{
					InOutState.RouteAlpha = 2.0 - InOutState.RouteAlpha;
					InOutState.RouteDirection = -1;
				}
				else if (InOutState.RouteAlpha <= 0.0)
				{
					InOutState.RouteAlpha = -InOutState.RouteAlpha;
					InOutState.RouteDirection = 1;
				}
			}
		}

		double TargetFacingDegrees = Entry.FacingDegrees;
		if (Entry.bPlaceholderMoves)
		{
			const FVector2D Direction = (Entry.RouteEnd - Entry.RouteStart).GetSafeNormal()
				* static_cast<double>(InOutState.RouteDirection);
			if (!Direction.IsNearlyZero())
			{
				TargetFacingDegrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
			}
		}
		InOutState.FacingDegrees = FMath::FixedTurn(
			static_cast<float>(InOutState.FacingDegrees),
			static_cast<float>(TargetFacingDegrees),
			static_cast<float>(180.0 * SafeDeltaSeconds));
	}

	FVisualResidentMotionPose FVisualResidentPresentationPlanner::ResolveMotionPose(
		const FVisualResidentPresentationEntry& Entry,
		const FVisualResidentMotionState& State)
	{
		FVisualResidentMotionPose Pose;
		Pose.Position = ResolveLocalRoutePosition(Entry, State.RouteAlpha);
		Pose.FacingDegrees = State.FacingDegrees;
		Pose.GroundOffset = FMath::Sin(State.AnimationSeconds * 6.0) * 2.0;
		const uint64 ResidentSeed = static_cast<uint64>(Entry.ResidentID)
			^ (static_cast<uint64>(Entry.ResidentID) >> 32);
		Pose.HeightScale = 0.9 + static_cast<double>((ResidentSeed >> 8) % 21ull) / 100.0;
		return Pose;
	}

	FVisualProxySlotPlanner::FVisualProxySlotPlanner(const int32 InCapacity)
		: Capacity(InCapacity)
	{
		Reset();
	}

	bool FVisualProxySlotPlanner::Reconcile(
		const TArray<FVisualResidentPresentationEntry>& ProxyEntries,
		FVisualProxySlotPlan& OutPlan,
		FString& OutError)
	{
		OutPlan = {};
		if (Capacity <= 0 || ProxyEntries.Num() > Capacity)
		{
			OutError = TEXT("Low-level proxy presentation exceeded its fixed visual slot capacity.");
			return false;
		}

		TSet<FResidentID> DesiredResidentIDs;
		for (const FVisualResidentPresentationEntry& Entry : ProxyEntries)
		{
			if (Entry.ResidentID <= 0
				|| Entry.bActiveActor
				|| Entry.bHasActiveState
				|| DesiredResidentIDs.Contains(Entry.ResidentID))
			{
				OutError = TEXT("Low-level proxy slots require unique positive non-Active ResidentIDs.");
				return false;
			}
			DesiredResidentIDs.Add(Entry.ResidentID);
		}

		TArray<FResidentID> CandidateSlots = SlotResidentIDs;
		for (FResidentID& BoundResidentID : CandidateSlots)
		{
			if (BoundResidentID != 0 && !DesiredResidentIDs.Contains(BoundResidentID))
			{
				BoundResidentID = 0;
				++OutPlan.ReleasedCount;
			}
		}
		for (const FVisualResidentPresentationEntry& Entry : ProxyEntries)
		{
			if (CandidateSlots.Contains(Entry.ResidentID))
			{
				continue;
			}
			const int32 FreeSlot = CandidateSlots.IndexOfByKey(FResidentID(0));
			if (FreeSlot == INDEX_NONE)
			{
				OutError = TEXT("Low-level proxy slots could not atomically assign the requested visual set.");
				return false;
			}
			CandidateSlots[FreeSlot] = Entry.ResidentID;
			++OutPlan.ReboundCount;
		}

		SlotResidentIDs = MoveTemp(CandidateSlots);
		OutPlan.SlotResidentIDs = SlotResidentIDs;
		OutPlan.VisibleCount = DesiredResidentIDs.Num();
		OutError.Reset();
		return true;
	}

	void FVisualProxySlotPlanner::Reset()
	{
		SlotResidentIDs.Init(0, FMath::Max(0, Capacity));
	}

	FVisualActorPoolPlanner::FVisualActorPoolPlanner(const int32 InCapacity)
		: Capacity(InCapacity)
	{
		Reset();
	}

	bool FVisualActorPoolPlanner::Reconcile(
		const TArray<FVisualResidentPresentationEntry>& ActiveEntries,
		FVisualActorPoolPlan& OutPlan,
		FString& OutError)
	{
		OutPlan = {};
		if (Capacity != 50 || ActiveEntries.Num() > Capacity)
		{
			OutError = TEXT("The full NPC Actor pool must use the frozen capacity of 50.");
			return false;
		}
		TSet<FResidentID> DesiredResidentIDs;
		TArray<FResidentID> SortedDesiredResidentIDs;
		for (const FVisualResidentPresentationEntry& Entry : ActiveEntries)
		{
			if (!Entry.bActiveActor
				|| !Entry.bHasActiveState
				|| Entry.ResidentID <= 0
				|| Entry.ActiveState.ResidentID != Entry.ResidentID
				|| DesiredResidentIDs.Contains(Entry.ResidentID))
			{
				OutError = TEXT("Actor-pool reconciliation requires unique entries copied from real Active residents.");
				return false;
			}
			DesiredResidentIDs.Add(Entry.ResidentID);
			SortedDesiredResidentIDs.Add(Entry.ResidentID);
		}
		SortedDesiredResidentIDs.Sort();

		TArray<FResidentID> CandidateSlots = SlotResidentIDs;
		for (FResidentID& BoundResidentID : CandidateSlots)
		{
			if (BoundResidentID != 0 && !DesiredResidentIDs.Contains(BoundResidentID))
			{
				BoundResidentID = 0;
				++OutPlan.ReleasedCount;
			}
		}
		for (const FResidentID ResidentID : SortedDesiredResidentIDs)
		{
			if (CandidateSlots.Contains(ResidentID))
			{
				continue;
			}
			const int32 FreeSlot = CandidateSlots.IndexOfByKey(FResidentID(0));
			if (FreeSlot == INDEX_NONE)
			{
				OutError = TEXT("The full NPC Actor pool could not atomically assign the desired Active set.");
				return false;
			}
			CandidateSlots[FreeSlot] = ResidentID;
			++OutPlan.ReboundCount;
		}

		SlotResidentIDs = MoveTemp(CandidateSlots);
		OutPlan.SlotResidentIDs = SlotResidentIDs;
		OutPlan.BoundCount = DesiredResidentIDs.Num();
		OutError.Reset();
		return true;
	}

	void FVisualActorPoolPlanner::Reset()
	{
		SlotResidentIDs.Init(0, FMath::Max(0, Capacity));
	}
}
