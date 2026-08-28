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
	Body->SetRelativeLocation(FVector(0.0, 0.0, 60.0));
	Body->SetRelativeScale3D(FVector(0.45, 0.45, 1.2));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);

	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(SceneRoot);
	Head->SetRelativeLocation(FVector(0.0, 0.0, 145.0));
	Head->SetRelativeScale3D(FVector(0.35));
	Head->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Head->SetCollisionResponseToAllChannels(ECR_Ignore);
	Head->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Head->SetGenerateOverlapEvents(false);
	Head->SetCanEverAffectNavigation(false);

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
	const double GroundZCentimeters,
	const double WalkSpeedCentimetersPerSecond)
{
	const bool bNewResident = BoundResidentID != Entry.ResidentID;
	PresentationEntry = Entry;
	BoundResidentID = Entry.ResidentID;
	GroundZ = GroundZCentimeters;
	WalkSpeed = WalkSpeedCentimetersPerSecond;
	if (bNewResident)
	{
		const uint32 Seed = Entry.ActiveState.AppearanceSeed != 0
			? Entry.ActiveState.AppearanceSeed
			: static_cast<uint32>(Entry.ResidentID);
		RouteAlpha = static_cast<double>(Seed % 1000u) / 999.0;
		RouteDirection = (Seed & 1u) == 0 ? 1 : -1;
		AnimationSeconds = static_cast<double>(Seed % 628u) / 100.0;
		const double HeightScale = 0.9 + static_cast<double>((Seed >> 8) % 21u) / 100.0;
		SetActorScale3D(FVector(1.0, 1.0, HeightScale));
	}
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	ApplyPose();
}

void AAILODVisualResidentActor::AdvancePlaceholderAnimation(const float DeltaSeconds)
{
	if (!IsBound()) return;
	AnimationSeconds += FMath::Max(0.0f, DeltaSeconds);
	if (PresentationEntry.bPlaceholderMoves)
	{
		const double RouteLength = FVector2D::Distance(
			PresentationEntry.RouteStart,
			PresentationEntry.RouteEnd);
		if (RouteLength > UE_SMALL_NUMBER)
		{
			RouteAlpha += RouteDirection * WalkSpeed * DeltaSeconds / RouteLength;
			if (RouteAlpha >= 1.0)
			{
				RouteAlpha = 2.0 - RouteAlpha;
				RouteDirection = -1;
			}
			else if (RouteAlpha <= 0.0)
			{
				RouteAlpha = -RouteAlpha;
				RouteDirection = 1;
			}
		}
	}
	ApplyPose();
}

void AAILODVisualResidentActor::ReleaseToPool()
{
	PresentationEntry = {};
	BoundResidentID = 0;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AAILODVisualResidentActor::ApplyPose()
{
	const FVector2D Position = AILOD::FVisualResidentPresentationPlanner::ResolveLocalRoutePosition(
		PresentationEntry,
		RouteAlpha);
	double FacingDegrees = PresentationEntry.FacingDegrees;
	if (PresentationEntry.bPlaceholderMoves)
	{
		const FVector2D RouteDirection2D = (PresentationEntry.RouteEnd - PresentationEntry.RouteStart).GetSafeNormal()
			* static_cast<double>(RouteDirection);
		if (!RouteDirection2D.IsNearlyZero())
		{
			FacingDegrees = FMath::RadiansToDegrees(FMath::Atan2(
				RouteDirection2D.Y,
				RouteDirection2D.X));
		}
	}
	const double Bob = PresentationEntry.bPlaceholderMoves
		? FMath::Sin(AnimationSeconds * 6.0) * 3.0
		: 0.0;
	SetActorLocation(FVector(Position.X, Position.Y, GroundZ + Bob));
	SetActorRotation(FRotator(0.0, FacingDegrees, 0.0));
}
