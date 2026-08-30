// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Presentation/AILODVisualResidentPresentation.h"
#include "AILODVisualPopulationPresenter.generated.h"

class AAILODVisualResidentActor;
class UInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMesh;

UCLASS(NotBlueprintable)
class AILODRESEARCH_API AAILODVisualPopulationPresenter : public AActor
{
	GENERATED_BODY()

public:
	AAILODVisualPopulationPresenter();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool InitializePresentation(
		UStaticMesh* ResidentBodyMesh,
		UStaticMesh* ResidentHeadMesh,
		int32 LowLevelProxyCapacity,
		double GroundZCentimeters,
		double WalkSpeedCentimetersPerSecond,
		FString& OutError);
	bool ApplyPresentationFrame(
		const AILOD::FVisualResidentPresentationFrame& Frame,
		FString& OutError);
	void AdvancePresentation(float DeltaSeconds, double PlaybackRate);
	void ResetPresentation();
	AILOD::FResidentID ResolveProxyResidentID(const FHitResult& Hit) const;
	AILOD::FResidentID ResolveProxyResidentIDByInstanceIndex(int32 InstanceIndex) const;
	int32 FindProxySlot(AILOD::FResidentID ResidentID) const;
	int32 FindActorSlot(AILOD::FResidentID ResidentID) const;
	bool FindResidentLabelLocation(AILOD::FResidentID ResidentID, FVector& OutLocation) const;

	int32 GetBoundActorCount() const { return BoundActorCount; }
	int32 GetActorPoolCapacity() const { return ResidentActorPool.Num(); }
	int32 GetProxyCount() const { return VisibleProxyCount; }
	int32 GetProxySlotCapacity() const { return ProxyResidentIDs.Num(); }
	int32 GetLastReleasedCount() const { return LastReleasedCount; }
	int32 GetLastReboundCount() const { return LastReboundCount; }
	int64 GetTotalReleasedCount() const { return TotalReleasedCount; }
	int64 GetTotalReboundCount() const { return TotalReboundCount; }
	int32 GetMotionStateCount() const { return MotionStates.Num(); }
	int32 GetLastMotionUpdateCount() const { return LastMotionUpdateCount; }

private:
	bool ApplyProxySlots(
		const AILOD::FVisualResidentPresentationFrame& Frame,
		const AILOD::FVisualProxySlotPlan& SlotPlan,
		const TMap<AILOD::FResidentID, AILOD::FVisualResidentMotionState>& CandidateMotionStates,
		FString& OutError);
	void UpdateProxyInstanceTransform(
		int32 SlotIndex,
		const AILOD::FVisualResidentPresentationEntry& Entry,
		const AILOD::FVisualResidentMotionState& MotionState,
		bool bMarkRenderStateDirty);
	void HideProxySlot(int32 SlotIndex, bool bMarkRenderStateDirty);
	void HideAllProxySlots();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> ProxyBodies;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> ProxyHeads;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AAILODVisualResidentActor>> ResidentActorPool;

	AILOD::FVisualActorPoolPlanner PoolPlanner;
	TUniquePtr<AILOD::FVisualProxySlotPlanner> ProxySlotPlanner;
	TArray<AILOD::FResidentID> ProxyResidentIDs;
	TArray<FVector2D> ProxyPositions;
	TArray<AILOD::FVisualResidentPresentationEntry> ProxyEntries;
	TMap<AILOD::FResidentID, AILOD::FVisualResidentMotionState> MotionStates;
	double GroundZ = 100.0;
	double WalkSpeed = 150.0;
	int32 VisibleProxyCount = 0;
	int32 BoundActorCount = 0;
	int32 LastReleasedCount = 0;
	int32 LastReboundCount = 0;
	int64 TotalReleasedCount = 0;
	int64 TotalReboundCount = 0;
	int32 LastMotionUpdateCount = 0;
	bool bInitialized = false;
};
