// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Presentation/AILODVisualResidentPresentation.h"
#include "AILODVisualPopulationPresenter.generated.h"

class AAILODVisualResidentActor;
class UHierarchicalInstancedStaticMeshComponent;
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
		UStaticMesh* ProxyMesh,
		UStaticMesh* FullActorBodyMesh,
		UStaticMesh* FullActorHeadMesh,
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

private:
	bool ApplyProxySlots(
		const AILOD::FVisualResidentPresentationFrame& Frame,
		const AILOD::FVisualProxySlotPlan& SlotPlan,
		FString& OutError);
	void HideAllProxySlots();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ProxyInstances;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AAILODVisualResidentActor>> ResidentActorPool;

	AILOD::FVisualActorPoolPlanner PoolPlanner;
	TUniquePtr<AILOD::FVisualProxySlotPlanner> ProxySlotPlanner;
	TArray<AILOD::FResidentID> ProxyResidentIDs;
	TArray<FVector2D> ProxyPositions;
	double GroundZ = 100.0;
	double WalkSpeed = 150.0;
	int32 VisibleProxyCount = 0;
	int32 BoundActorCount = 0;
	int32 LastReleasedCount = 0;
	int32 LastReboundCount = 0;
	int64 TotalReleasedCount = 0;
	int64 TotalReboundCount = 0;
	bool bInitialized = false;
};
