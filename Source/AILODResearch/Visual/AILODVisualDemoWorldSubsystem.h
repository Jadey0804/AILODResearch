// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Presentation/AILODVisualDemoRuntime.h"
#include "AILODVisualDemoWorldSubsystem.generated.h"

class AAILODVisualPopulationPresenter;
class AActor;
class UWorldPartitionStreamingSourceComponent;

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
	bool CopyPresentationFrame(AILOD::FVisualResidentPresentationFrame& OutFrame) const;
	const AILOD::FVisualWorldLayout* GetReadOnlyLayout() const;
	bool HandleResidentClick(const FHitResult& Hit);
	bool RequestPaused(bool bPaused, FString& OutError);
	bool RequestTimeScale(int32 TimeScale, FString& OutError);
	bool RequestRestart(FString& OutError);
	void SetTelescopeEnabled(bool bEnabled);

private:
	void UpdateCameraObservation(float DeltaTime);
	bool EnsurePopulationPresenter(FString& OutError);
	bool EnsureTelescopeStreamingSource(FString& OutError);
	bool UpdateTelescopeStreamingSource(AILOD::FResidentID ResidentID, FString& OutError);
	void DisableTelescopeStreamingSource();
	void UpdateResidentPresentation();
	void InitializePerformanceCapture();
	void UpdatePerformanceCapture(float DeltaTime);
	void UpdatePerformanceScenario();
	void RecordPerformanceCsvStats() const;
	void DrawFunctionalUI();
	void DrawResidentDebugLabels(const AILOD::FVisualResidentPresentationFrame& Frame) const;

	AILOD::FVisualDemoRuntime Runtime;
	UPROPERTY(Transient)
	TObjectPtr<AAILODVisualPopulationPresenter> PopulationPresenter;
	UPROPERTY(Transient)
	TObjectPtr<AActor> TelescopeStreamingSourceActor;
	UPROPERTY(Transient)
	TObjectPtr<UWorldPartitionStreamingSourceComponent> TelescopeStreamingSource;
	AILOD::FVisualTelescopeFocusGate TelescopeFocusGate;
	FString LastUIMessage;
	FString PerformanceScenario;
	FString PerformanceCaptureName;
	double PerformanceWarmupSeconds = 15.0;
	double PerformanceCaptureSeconds = 30.0;
	double PerformanceWarmupElapsedSeconds = 0.0;
	double PerformanceCaptureElapsedSeconds = 0.0;
	FVector PerformanceCameraStart = FVector::ZeroVector;
	FVector PerformanceCameraTravelEnd = FVector::ZeroVector;
	int32 PerformanceTimeScale = 1;
	bool bDemoActivated = false;
	bool bShowResidentDebugLabels = true;
	bool bTelescopeEnabled = false;
	bool bClearTrackedResidentRequested = false;
	bool bPerformanceCaptureEnabled = false;
	bool bPerformanceCaptureStarted = false;
	bool bPerformanceCaptureFinished = false;
	bool bPerformanceTimeScaleApplied = false;
	bool bPerformanceViewPositionApplied = false;
	bool bPerformanceScenarioTelescopeEnabled = false;
};
