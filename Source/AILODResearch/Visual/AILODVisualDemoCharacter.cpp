// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualDemoCharacter.h"

#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "ImGuiConfig.h"
#include "UObject/ConstructorHelpers.h"
#include "imgui.h"

AAILODVisualDemoCharacter::AAILODVisualDemoCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	CameraAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("CameraAnchor"));
	CameraAnchor->SetupAttachment(RootComponent);
	CameraAnchor->SetAbsolute(false, true, false);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CameraAnchor);
	CameraBoom->TargetArmLength = 2500.0f;
	CameraBoom->SetRelativeRotation(FRotator(-55.0f, -45.0f, 0.0f));
	CameraBoom->bDoCollisionTest = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;

	PlayerMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlayerMarker"));
	PlayerMarker->SetupAttachment(RootComponent);
	PlayerMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlayerMarker->SetRelativeLocation(FVector(0.0f, 0.0f, -70.0f));
	PlayerMarker->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.2f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MarkerMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (MarkerMesh.Succeeded())
	{
		PlayerMarker->SetStaticMesh(MarkerMesh.Object);
	}
}

void AAILODVisualDemoCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bCameraReturning)
	{
		const FVector Target = GetActorLocation();
		const FVector Next = FMath::VInterpTo(CameraAnchor->GetComponentLocation(), Target, DeltaSeconds, 5.0f);
		CameraAnchor->SetWorldLocation(Next);
		if (FVector::DistSquared(Next, Target) < 25.0f)
		{
			CameraAnchor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
			CameraAnchor->SetRelativeLocation(FVector::ZeroVector);
			bCameraReturning = false;
			bCameraFollowsPlayer = true;
		}
		return;
	}

	if (UIWantsKeyboard())
	{
		return;
	}
	if (!FMath::IsNearlyZero(CameraYawInput))
	{
		CameraAnchor->AddWorldRotation(FRotator(
			0.0f,
			CameraYawInput * CameraYawSpeedDegrees * DeltaSeconds,
			0.0f));
	}
	const FVector2D Input(CameraForwardInput, CameraRightInput);
	if (Input.IsNearlyZero())
	{
		return;
	}
	if (bCameraFollowsPlayer)
	{
		BeginFreeCamera();
	}
	FVector Forward = TopDownCamera->GetForwardVector();
	Forward.Z = 0.0f;
	Forward.Normalize();
	FVector Right = TopDownCamera->GetRightVector();
	Right.Z = 0.0f;
	Right.Normalize();
	CameraAnchor->AddWorldOffset((Forward * CameraForwardInput + Right * CameraRightInput) * FreeCameraSpeed * DeltaSeconds);
}

void AAILODVisualDemoCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAxis(TEXT("VisualCameraForward"), this, &AAILODVisualDemoCharacter::SetCameraForward);
	PlayerInputComponent->BindAxis(TEXT("VisualCameraRight"), this, &AAILODVisualDemoCharacter::SetCameraRight);
	PlayerInputComponent->BindAxis(TEXT("VisualCameraYaw"), this, &AAILODVisualDemoCharacter::SetCameraYaw);
	PlayerInputComponent->BindAction(TEXT("VisualPrimaryClick"), IE_Pressed, this, &AAILODVisualDemoCharacter::HandlePrimaryClick);
	PlayerInputComponent->BindAction(TEXT("VisualCameraReturn"), IE_Pressed, this, &AAILODVisualDemoCharacter::ReturnCameraToPlayer);

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->bShowMouseCursor = true;
		PlayerController->bEnableClickEvents = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
	}
}

void AAILODVisualDemoCharacter::SetCameraForward(const float Value)
{
	CameraForwardInput = Value;
}

void AAILODVisualDemoCharacter::SetCameraRight(const float Value)
{
	CameraRightInput = Value;
}

void AAILODVisualDemoCharacter::SetCameraYaw(const float Value)
{
	CameraYawInput = Value;
}

void AAILODVisualDemoCharacter::HandlePrimaryClick()
{
	if (UIWantsMouse())
	{
		return;
	}
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	FHitResult Hit;
	if (PlayerController && PlayerController->GetHitResultUnderCursor(ECC_Visibility, true, Hit))
	{
		UAIBlueprintHelperLibrary::SimpleMoveToLocation(PlayerController, Hit.Location);
	}
}

void AAILODVisualDemoCharacter::ReturnCameraToPlayer()
{
	if (bCameraFollowsPlayer)
	{
		CameraAnchor->SetRelativeLocation(FVector::ZeroVector);
		return;
	}
	bCameraReturning = true;
}

void AAILODVisualDemoCharacter::BeginFreeCamera()
{
	CameraAnchor->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	bCameraFollowsPlayer = false;
	bCameraReturning = false;
}

bool AAILODVisualDemoCharacter::UIWantsMouse() const
{
	const ImGui::FScopedContext ScopedContext;
	return ScopedContext && ImGui::GetIO().WantCaptureMouse;
}

bool AAILODVisualDemoCharacter::UIWantsKeyboard() const
{
	const ImGui::FScopedContext ScopedContext;
	return ScopedContext && ImGui::GetIO().WantCaptureKeyboard;
}
