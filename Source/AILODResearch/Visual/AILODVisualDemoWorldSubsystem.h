// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Presentation/AILODVisualDemoRuntime.h"
#include "AILODVisualDemoWorldSubsystem.generated.h"

UCLASS(Config=Game)
class AILODRESEARCH_API UAILODVisualDemoWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	bool CopyDemoSnapshot(AILOD::FUnifiedDemoSnapshot& OutSnapshot) const;
	const AILOD::FVisualWorldLayout* GetReadOnlyLayout() const;
	bool RequestPaused(bool bPaused, FString& OutError);
	bool RequestTimeScale(int32 TimeScale, FString& OutError);
	bool RequestRestart(FString& OutError);

private:
	void UpdateCameraObservation();
	void DrawFunctionalUI();

	AILOD::FVisualDemoRuntime Runtime;
	FString LastUIMessage;
	bool bDemoActivated = false;
};
