// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualResidentActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AAILODVisualResidentActor::AAILODVisualResidentActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(SceneRoot);
	Body->SetRelativeLocation(FVector(0.0, 0.0, AILOD::FVisualResidentFigureGeometry::BodyCenterZ));
	Body->SetRelativeScale3D(FVector(
		AILOD::FVisualResidentFigureGeometry::BodyRadiusScale,
		AILOD::FVisualResidentFigureGeometry::BodyRadiusScale,
		AILOD::FVisualResidentFigureGeometry::BodyHeightScale));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	Body->SetCastShadow(false);

	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(SceneRoot);
	Head->SetRelativeLocation(FVector(0.0, 0.0, AILOD::FVisualResidentFigureGeometry::HeadCenterZ));
	Head->SetRelativeScale3D(FVector(AILOD::FVisualResidentFigureGeometry::HeadScale));
	Head->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Head->SetCollisionResponseToAllChannels(ECR_Ignore);
	Head->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Head->SetGenerateOverlapEvents(false);
	Head->SetCanEverAffectNavigation(false);
	Head->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeadMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (BodyMesh.Succeeded()) Body->SetStaticMesh(BodyMesh.Object);
	if (HeadMesh.Succeeded()) Head->SetStaticMesh(HeadMesh.Object);
	ReleaseToPool();
}

void AAILODVisualResidentActor::ConfigureMeshes(UStaticMesh* BodyMesh, UStaticMesh* HeadMesh)
{
	if (BodyMesh != nullptr) Body->SetStaticMesh(BodyMesh);
	if (HeadMesh != nullptr) Head->SetStaticMesh(HeadMesh);
}

void AAILODVisualResidentActor::ApplyPresentationEntry(
	const AILOD::FVisualResidentPresentationEntry& Entry,
	const double GroundZCentimeters)
{
	PresentationEntry = Entry;
	BoundResidentID = Entry.ResidentID;
	GroundZ = GroundZCentimeters;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
}

void AAILODVisualResidentActor::ApplyMotionState(
	const AILOD::FVisualResidentMotionState& MotionState)
{
	if (!IsBound() || MotionState.ResidentID != BoundResidentID)
	{
		return;
	}
	const AILOD::FVisualResidentMotionPose Pose =
		AILOD::FVisualResidentPresentationPlanner::ResolveMotionPose(
			PresentationEntry,
			MotionState);
	SetActorScale3D(FVector(1.0, 1.0, Pose.HeightScale));
	SetActorLocation(FVector(Pose.Position.X, Pose.Position.Y, GroundZ + Pose.GroundOffset));
	SetActorRotation(FRotator(0.0, Pose.FacingDegrees, 0.0));
}

void AAILODVisualResidentActor::ReleaseToPool()
{
	PresentationEntry = {};
	BoundResidentID = 0;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}
