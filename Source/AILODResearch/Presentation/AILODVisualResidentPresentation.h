// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODVisualObservationPlanner.h"

namespace AILOD
{
	enum class EVisualResidentAnchor : uint8
	{
		ProxyRoad,
		Home,
		Work
	};

	struct FVisualResidentPresentationConfig
	{
		int32 LowLevelProxyCapacity = 160;
		int32 ActiveActorCapacity = 50;
		double LocalRouteHalfLength = 800.0;
	};

	struct FVisualResidentPresentationEntry
	{
		FResidentID ResidentID = 0;
		FHomeID HomeID = 0;
		FVisualHomeSlotID VisualHomeSlotID = 0;
		FVisualWorkAnchorID WorkAnchorID = 0;
		FVisualRoadID ProxyRoadID = 0;
		FVector2D ProxyPosition = FVector2D::ZeroVector;
		FVector2D RouteStart = FVector2D::ZeroVector;
		FVector2D RouteEnd = FVector2D::ZeroVector;
		FVector2D DestinationPosition = FVector2D::ZeroVector;
		double FacingDegrees = 0.0;
		EVisualResidentAnchor DestinationAnchor = EVisualResidentAnchor::ProxyRoad;
		bool bActiveActor = false;
		bool bTracked = false;
		bool bPlaceholderMoves = false;
		bool bHasActiveState = false;
		FUnifiedDemoResidentSnapshot ActiveState;
	};

	struct FVisualResidentPresentationDiagnostics
	{
		int32 LowLevelProxyCount = 0;
		int32 ActiveActorCount = 0;
		int32 ActorPoolCapacity = 50;
		int32 VisitedResidentEntryCount = 0;
		bool bScannedResidentCatalog = false;
	};

	struct FVisualResidentPresentationFrame
	{
		FSimulationTime GameTime;
		TArray<FVisualResidentPresentationEntry> LowLevelProxies;
		TArray<FVisualResidentPresentationEntry> ActiveActors;
		FResidentID SelectedResidentID = 0;
		bool bHasSelectedResident = false;
		FVisualResidentPresentationEntry SelectedResident;
		FVisualResidentPresentationDiagnostics Diagnostics;
	};

	class FVisualResidentPresentationPlanner
	{
	public:
		static bool BuildFrame(
			const FVisualWorldLayout& Layout,
			const FVisualObservationPlan& ObservationPlan,
			const FUnifiedDemoSnapshot& SimulationSnapshot,
			FResidentID SelectedResidentID,
			const FVisualResidentPresentationConfig& Config,
			FVisualResidentPresentationFrame& OutFrame,
			FString& OutError);

		static FVector2D ResolveLocalRoutePosition(
			const FVisualResidentPresentationEntry& Entry,
			double RouteAlpha);
	};

	struct FVisualProxySlotPlan
	{
		TArray<FResidentID> SlotResidentIDs;
		int32 VisibleCount = 0;
		int32 ReleasedCount = 0;
		int32 ReboundCount = 0;
	};

	class FVisualProxySlotPlanner
	{
	public:
		explicit FVisualProxySlotPlanner(int32 InCapacity);

		bool Reconcile(
			const TArray<FVisualResidentPresentationEntry>& ProxyEntries,
			FVisualProxySlotPlan& OutPlan,
			FString& OutError);
		void Reset();
		int32 GetCapacity() const { return Capacity; }
		const TArray<FResidentID>& GetSlotResidentIDs() const { return SlotResidentIDs; }

	private:
		int32 Capacity = 0;
		TArray<FResidentID> SlotResidentIDs;
	};

	struct FVisualActorPoolPlan
	{
		TArray<FResidentID> SlotResidentIDs;
		int32 BoundCount = 0;
		int32 ReleasedCount = 0;
		int32 ReboundCount = 0;
	};

	class FVisualActorPoolPlanner
	{
	public:
		explicit FVisualActorPoolPlanner(int32 InCapacity = 50);

		bool Reconcile(
			const TArray<FVisualResidentPresentationEntry>& ActiveEntries,
			FVisualActorPoolPlan& OutPlan,
			FString& OutError);
		void Reset();
		const TArray<FResidentID>& GetSlotResidentIDs() const { return SlotResidentIDs; }

	private:
		int32 Capacity = 50;
		TArray<FResidentID> SlotResidentIDs;
	};
}
