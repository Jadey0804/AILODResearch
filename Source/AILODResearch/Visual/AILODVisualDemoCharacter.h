// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AILODVisualDemoCharacter.generated.h"

class UCameraComponent;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;

UCLASS()
class AILODRESEARCH_API AAILODVisualDemoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAILODVisualDemoCharacter();
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void SetCameraForward(float Value);
	void SetCameraRight(float Value);
	void SetCameraYaw(float Value);
	void HandlePrimaryClick();
	void EnableTelescope();
	void DisableTelescope();
	void ReturnCameraToPlayer();
	void BeginFreeCamera();
	bool UIWantsMouse() const;
	bool UIWantsKeyboard() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> CameraAnchor;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> TopDownCamera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PlayerMarker;

	float CameraForwardInput = 0.0f;
	float CameraRightInput = 0.0f;
	float CameraYawInput = 0.0f;
	float FreeCameraSpeed = 3000.0f;
	float CameraYawSpeedDegrees = 90.0f;
	float SavedCameraBoomLength = 0.0f;
	float SavedCameraFieldOfView = 0.0f;
	FVector SavedCameraBoomRelativeLocation = FVector::ZeroVector;
	FRotator SavedCameraBoomRelativeRotation = FRotator::ZeroRotator;
	bool bCameraFollowsPlayer = true;
	bool bCameraReturning = false;
	bool bTelescopeViewActive = false;
};
