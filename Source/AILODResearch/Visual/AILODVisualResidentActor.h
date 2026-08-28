// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Presentation/AILODVisualResidentPresentation.h"
#include "AILODVisualResidentActor.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class AILODRESEARCH_API AAILODVisualResidentActor : public AActor
{
	GENERATED_BODY()

public:
	AAILODVisualResidentActor();

	void ConfigureMeshes(UStaticMesh* BodyMesh, UStaticMesh* HeadMesh);
	void ApplyPresentationEntry(
		const AILOD::FVisualResidentPresentationEntry& Entry,
		double GroundZCentimeters,
		double WalkSpeedCentimetersPerSecond);
	void AdvancePlaceholderAnimation(float DeltaSeconds);
	void ReleaseToPool();

	AILOD::FResidentID GetBoundResidentID() const { return BoundResidentID; }
	bool IsBound() const { return BoundResidentID > 0; }

private:
	void ApplyPose();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Head;

	AILOD::FVisualResidentPresentationEntry PresentationEntry;
	AILOD::FResidentID BoundResidentID = 0;
	double GroundZ = 0.0;
	double WalkSpeed = 150.0;
	double RouteAlpha = 0.0;
	double AnimationSeconds = 0.0;
	int32 RouteDirection = 1;
};
