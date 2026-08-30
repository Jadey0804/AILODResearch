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
		double GroundZCentimeters);
	void ApplyMotionState(const AILOD::FVisualResidentMotionState& MotionState);
	void ReleaseToPool();

	AILOD::FResidentID GetBoundResidentID() const { return BoundResidentID; }
	const AILOD::FVisualResidentPresentationEntry& GetPresentationEntry() const { return PresentationEntry; }
	bool IsBound() const { return BoundResidentID > 0; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Head;

	AILOD::FVisualResidentPresentationEntry PresentationEntry;
	AILOD::FResidentID BoundResidentID = 0;
	double GroundZ = 0.0;
};
