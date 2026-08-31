// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualDemoWorldSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "ImGuiConfig.h"
#include "AILODVisualDemoGameMode.h"
#include "AILODVisualDemoSettings.h"
#include "AILODVisualDemoCharacter.h"
#include "AILODVisualPopulationPresenter.h"
#include "AILODVisualResidentActor.h"
#include "Components/SceneComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "imgui.h"

CSV_DEFINE_CATEGORY(AILODVisual, true);

namespace
{
	bool DeprojectScreenPointToGround(
		APlayerController& PlayerController,
		const float ScreenX,
		const float ScreenY,
		const double GroundZ,
		FVector2D& OutGroundPoint)
	{
		FVector RayOrigin;
		FVector RayDirection;
		if (!PlayerController.DeprojectScreenPositionToWorld(
			ScreenX,
			ScreenY,
			RayOrigin,
			RayDirection)
			|| RayDirection.Z >= -UE_SMALL_NUMBER)
		{
			return false;
		}
		const double Distance = (GroundZ - RayOrigin.Z) / RayDirection.Z;
		if (Distance < 0.0)
		{
			return false;
		}
		const FVector GroundPoint = RayOrigin + RayDirection * Distance;
		OutGroundPoint = FVector2D(GroundPoint.X, GroundPoint.Y);
		return true;
	}

	bool BuildNormalViewGroundFootprint(
		APlayerController& PlayerController,
		const double GroundZ,
		FVector2D& OutFocus,
		TArray<FVector2D>& OutPolygon)
	{
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		PlayerController.GetViewportSize(ViewportWidth, ViewportHeight);
		if (ViewportWidth <= 0 || ViewportHeight <= 0
			|| !DeprojectScreenPointToGround(
				PlayerController,
				ViewportWidth * 0.5f,
				ViewportHeight * 0.5f,
				GroundZ,
				OutFocus))
		{
			OutPolygon.Reset();
			return false;
		}

		const FVector2D ScreenCorners[] =
		{
			FVector2D(0.0, 0.0),
			FVector2D(ViewportWidth, 0.0),
			FVector2D(ViewportWidth, ViewportHeight),
			FVector2D(0.0, ViewportHeight)
		};
		OutPolygon.Reset(UE_ARRAY_COUNT(ScreenCorners));
		for (const FVector2D& ScreenCorner : ScreenCorners)
		{
			FVector2D GroundCorner;
			if (!DeprojectScreenPointToGround(
				PlayerController,
				ScreenCorner.X,
				ScreenCorner.Y,
				GroundZ,
				GroundCorner))
			{
				OutPolygon.Reset();
				break;
			}
			OutPolygon.Add(GroundCorner);
		}
		return true;
	}
}

bool UAILODVisualDemoWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAILODVisualDemoWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAILODVisualDemoWorldSubsystem::Deinitialize()
{
	if (IsValid(PopulationPresenter))
	{
		PopulationPresenter->Destroy();
	}
	if (IsValid(TelescopeStreamingSourceActor))
	{
		TelescopeStreamingSourceActor->Destroy();
	}
	PopulationPresenter = nullptr;
	TelescopeStreamingSourceActor = nullptr;
	TelescopeStreamingSource = nullptr;
	TelescopeFocusGate.Reset();
	Runtime = AILOD::FVisualDemoRuntime();
	LastUIMessage.Reset();
	bDemoActivated = false;
	bShowResidentDebugLabels = true;
	bTelescopeEnabled = false;
	bClearTrackedResidentRequested = false;
	PerformanceScenario.Reset();
	PerformanceCaptureName.Reset();
	PerformanceWarmupSeconds = 15.0;
	PerformanceCaptureSeconds = 30.0;
	PerformanceWarmupElapsedSeconds = 0.0;
	PerformanceCaptureElapsedSeconds = 0.0;
	PerformanceCameraStart = FVector::ZeroVector;
	PerformanceCameraTravelEnd = FVector::ZeroVector;
	PerformanceTimeScale = 1;
	bPerformanceCaptureEnabled = false;
	bPerformanceCaptureStarted = false;
	bPerformanceCaptureFinished = false;
	bPerformanceTimeScaleApplied = false;
	bPerformanceViewPositionApplied = false;
	bPerformanceScenarioTelescopeEnabled = false;
	Super::Deinitialize();
}

void UAILODVisualDemoWorldSubsystem::Tick(const float DeltaTime)
{
	if (!bDemoActivated)
	{
		const UWorld* World = GetWorld();
		if (!World || !World->GetAuthGameMode<AAILODVisualDemoGameMode>())
		{
			return;
		}
		bDemoActivated = true;
		InitializePerformanceCapture();
		const UAILODVisualDemoSettings* Settings = GetDefault<UAILODVisualDemoSettings>();
		bShowResidentDebugLabels = Settings->bShowResidentDebugLabels;
		FString InitializationError;
		if (!Runtime.Initialize(Settings->MakeRuntimeConfig(), InitializationError))
		{
			LastUIMessage = InitializationError;
			UE_LOG(LogTemp, Error, TEXT("AILOD Phase 7C Demo initialization failed: %s"), *InitializationError);
		}
		else if (!EnsurePopulationPresenter(InitializationError))
		{
			LastUIMessage = InitializationError;
			UE_LOG(LogTemp, Error, TEXT("AILOD Phase 7D resident presentation initialization failed: %s"), *InitializationError);
		}
	}

	FString Error;
	if (!Runtime.Tick(DeltaTime, Error))
	{
		LastUIMessage = Error;
	}
	if (Runtime.GetState() == AILOD::EVisualDemoRuntimeState::Running)
	{
		UpdateCameraObservation(DeltaTime);
	}
	UpdateResidentPresentation();
	if (IsValid(PopulationPresenter))
	{
		PopulationPresenter->AdvancePresentation(
			DeltaTime,
			Runtime.GetPresentationPlaybackRate());
	}
	DrawFunctionalUI();
	RecordPerformanceCsvStats();
	UpdatePerformanceCapture(DeltaTime);
}

TStatId UAILODVisualDemoWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAILODVisualDemoWorldSubsystem, STATGROUP_Tickables);
}

bool UAILODVisualDemoWorldSubsystem::CopyDemoSnapshot(AILOD::FUnifiedDemoSnapshot& OutSnapshot) const
{
	return bDemoActivated && Runtime.CopySnapshot(OutSnapshot);
}

bool UAILODVisualDemoWorldSubsystem::CopyPresentationFrame(
	AILOD::FVisualResidentPresentationFrame& OutFrame) const
{
	return bDemoActivated && Runtime.CopyPresentationFrame(OutFrame);
}

const AILOD::FVisualWorldLayout* UAILODVisualDemoWorldSubsystem::GetReadOnlyLayout() const
{
	return bDemoActivated && Runtime.GetLayout().IsBuilt() ? &Runtime.GetLayout() : nullptr;
}

bool UAILODVisualDemoWorldSubsystem::HandleResidentClick(const FHitResult& Hit)
{
	AILOD::FResidentID ResidentID = 0;
	if (const AAILODVisualResidentActor* ResidentActor = Cast<AAILODVisualResidentActor>(Hit.GetActor()))
	{
		ResidentID = ResidentActor->GetBoundResidentID();
	}
	else if (IsValid(PopulationPresenter))
	{
		ResidentID = PopulationPresenter->ResolveProxyResidentID(Hit);
	}
	if (ResidentID <= 0)
	{
		return false;
	}
	FString Error;
	if (!Runtime.RequestSelectedResident(ResidentID, Error))
	{
		LastUIMessage = Error;
	}
	else
	{
		LastUIMessage.Reset();
	}
	return true;
}

bool UAILODVisualDemoWorldSubsystem::RequestPaused(const bool bPaused, FString& OutError)
{
	return Runtime.RequestPaused(bPaused, OutError);
}

bool UAILODVisualDemoWorldSubsystem::RequestTimeScale(const int32 TimeScale, FString& OutError)
{
	return Runtime.RequestTimeScale(TimeScale, OutError);
}

bool UAILODVisualDemoWorldSubsystem::RequestRestart(FString& OutError)
{
	const bool bRestarted = Runtime.Restart(OutError);
	if (bRestarted && IsValid(PopulationPresenter))
	{
		PopulationPresenter->ResetPresentation();
	}
	if (bRestarted)
	{
		TelescopeFocusGate.Reset();
		bClearTrackedResidentRequested = false;
		DisableTelescopeStreamingSource();
	}
	return bRestarted;
}

void UAILODVisualDemoWorldSubsystem::SetTelescopeEnabled(const bool bEnabled)
{
	bTelescopeEnabled = bEnabled;
	if (!bTelescopeEnabled)
	{
		TelescopeFocusGate.Reset();
		DisableTelescopeStreamingSource();
	}
}

void UAILODVisualDemoWorldSubsystem::UpdateCameraObservation(const float DeltaTime)
{
	const UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
	const FVector CameraForward3D = PlayerController->PlayerCameraManager->GetCameraRotation().Vector();
	FVector2D CameraForward(CameraForward3D.X, CameraForward3D.Y);
	CameraForward.Normalize();

	AILOD::FVisualObservationFrameInput Input;
	const UAILODVisualDemoSettings* Settings = GetDefault<UAILODVisualDemoSettings>();
	const FVector2D CameraPosition(CameraLocation.X, CameraLocation.Y);
	FVector2D NormalFocus = CameraPosition;
	BuildNormalViewGroundFootprint(
		*PlayerController,
		Settings->NPCGroundZCentimeters,
		NormalFocus,
		Input.NormalVisibleGroundPolygon);
	Input.RealDeltaSeconds = DeltaTime;
	Input.bNormalViewUsesRadius = Settings->bUseRadialNormalObservation;
	Input.NormalView.Origin = NormalFocus;
	Input.NormalView.Forward = CameraForward;
	Input.NormalView.EnterDistance = Settings->NormalObservationDistanceMeters * 100.0;
	Input.NormalView.HalfAngleDegrees = Settings->NormalObservationHalfAngleDegrees;
	if (const APawn* PlayerPawn = PlayerController->GetPawn())
	{
		const FVector PawnLocation = PlayerPawn->GetActorLocation();
		Input.bHasNormalImmediateOrigin = true;
		Input.NormalImmediateOrigin = FVector2D(PawnLocation.X, PawnLocation.Y);
	}
	Input.bTelescopeEnabled = bTelescopeEnabled;
	Input.TelescopeView.Origin = CameraPosition;
	Input.TelescopeView.Forward = CameraForward;
	Input.TelescopeView.MinimumDistance = Settings->TelescopeMinimumDistanceMeters * 100.0;
	Input.TelescopeView.EnterDistance = Settings->TelescopeObservationDistanceMeters * 100.0;
	Input.TelescopeView.HalfAngleDegrees = Settings->TelescopeObservationHalfAngleDegrees;
	Input.bClearTrackedResident = bClearTrackedResidentRequested;
	FString Error;
	if (!Runtime.SubmitObservationFrame(Input, Error))
	{
		LastUIMessage = Error;
		return;
	}

	if (bClearTrackedResidentRequested)
	{
		AILOD::FUnifiedDemoSnapshot Snapshot;
		if (Runtime.CopySnapshot(Snapshot) && Snapshot.TrackedResidentID == 0)
		{
			bClearTrackedResidentRequested = false;
			TelescopeFocusGate.Reset();
			return;
		}
	}

	if (!bTelescopeEnabled)
	{
		return;
	}

	const AILOD::FResidentID CenterResidentID =
		Runtime.GetCurrentPresentationObservationPlan().TelescopeCenterResidentID;
	bool bStreamingReady = false;
	if (CenterResidentID > 0)
	{
		if (!UpdateTelescopeStreamingSource(CenterResidentID, Error))
		{
			LastUIMessage = Error;
		}
		else
		{
			bStreamingReady = TelescopeStreamingSource->IsStreamingCompleted();
		}
	}
	else
	{
		DisableTelescopeStreamingSource();
	}

	const AILOD::FResidentID PromotionResidentID = TelescopeFocusGate.Update(
		true,
		CenterResidentID,
		DeltaTime,
		bStreamingReady,
		Settings->TelescopeFocusSeconds);
	AILOD::FUnifiedDemoSnapshot Snapshot;
	if (PromotionResidentID <= 0
		|| !Runtime.CopySnapshot(Snapshot)
		|| Snapshot.TrackedResidentID == PromotionResidentID)
	{
		return;
	}

	Input.TelescopePromotionResidentID = PromotionResidentID;
	if (!Runtime.SubmitObservationFrame(Input, Error))
	{
		LastUIMessage = Error;
		return;
	}
	if (Runtime.CopySnapshot(Snapshot) && Snapshot.TrackedResidentID == PromotionResidentID)
	{
		Runtime.RequestSelectedResident(PromotionResidentID, Error);
		LastUIMessage = FString::Printf(
			TEXT("Telescope Lift committed: ResidentID %lld is now tracked."),
			PromotionResidentID);
	}
}

bool UAILODVisualDemoWorldSubsystem::EnsurePopulationPresenter(FString& OutError)
{
	if (IsValid(PopulationPresenter))
	{
		OutError.Reset();
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutError = TEXT("The Phase 7D population presenter requires a game World.");
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PopulationPresenter = World->SpawnActor<AAILODVisualPopulationPresenter>(
		AAILODVisualPopulationPresenter::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!IsValid(PopulationPresenter))
	{
		OutError = TEXT("The Phase 7D population presenter Actor could not be spawned.");
		return false;
	}

	const UAILODVisualDemoSettings* Settings = GetDefault<UAILODVisualDemoSettings>();
	const AILOD::FVisualDemoRuntimeConfig RuntimeConfig = Settings->MakeRuntimeConfig();
	return PopulationPresenter->InitializePresentation(
		Settings->FullActorBodyMesh.LoadSynchronous(),
		Settings->FullActorHeadMesh.LoadSynchronous(),
		RuntimeConfig.Presentation.LowLevelProxyCapacity,
		Settings->NPCGroundZCentimeters,
		Settings->PlaceholderWalkSpeedMetersPerSecond * 100.0,
		OutError);
}

bool UAILODVisualDemoWorldSubsystem::EnsureTelescopeStreamingSource(FString& OutError)
{
	if (IsValid(TelescopeStreamingSourceActor) && IsValid(TelescopeStreamingSource))
	{
		OutError.Reset();
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutError = TEXT("The telescope Streaming Source requires a game World.");
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TelescopeStreamingSourceActor = World->SpawnActor<AActor>(
		AActor::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!IsValid(TelescopeStreamingSourceActor))
	{
		OutError = TEXT("The telescope Streaming Source Actor could not be spawned.");
		return false;
	}
	TelescopeStreamingSourceActor->SetActorHiddenInGame(true);
	USceneComponent* StreamingRoot = NewObject<USceneComponent>(
		TelescopeStreamingSourceActor,
		TEXT("AILODTelescopeStreamingRoot"));
	TelescopeStreamingSourceActor->AddInstanceComponent(StreamingRoot);
	TelescopeStreamingSourceActor->SetRootComponent(StreamingRoot);
	StreamingRoot->RegisterComponent();
	TelescopeStreamingSource = NewObject<UWorldPartitionStreamingSourceComponent>(
		TelescopeStreamingSourceActor,
		TEXT("AILODTelescopeStreamingSource"));
	if (!IsValid(TelescopeStreamingSource))
	{
		OutError = TEXT("The telescope Streaming Source Component could not be created.");
		return false;
	}
	FStreamingSourceShape& Shape = TelescopeStreamingSource->Shapes.AddDefaulted_GetRef();
	Shape.bUseGridLoadingRange = false;
	Shape.Radius = static_cast<float>(
		GetDefault<UAILODVisualDemoSettings>()->TelescopeStreamingRadiusMeters * 100.0);
	TelescopeStreamingSource->Priority = EStreamingSourcePriority::High;
	TelescopeStreamingSource->DebugColor = FColor::Cyan;
	TelescopeStreamingSource->DisableStreamingSource();
	TelescopeStreamingSourceActor->AddInstanceComponent(TelescopeStreamingSource);
	TelescopeStreamingSource->RegisterComponent();
	OutError.Reset();
	return true;
}

bool UAILODVisualDemoWorldSubsystem::UpdateTelescopeStreamingSource(
	const AILOD::FResidentID ResidentID,
	FString& OutError)
{
	if (!EnsureTelescopeStreamingSource(OutError))
	{
		return false;
	}
	const AILOD::FVisualResidentPlacement* Placement = Runtime.GetLayout().FindResident(ResidentID);
	if (Placement == nullptr)
	{
		OutError = TEXT("The telescope center ResidentID has no fixed Visual World Layout placement.");
		return false;
	}
	const double GroundZ = GetDefault<UAILODVisualDemoSettings>()->NPCGroundZCentimeters;
	TelescopeStreamingSourceActor->SetActorLocation(FVector(
		Placement->ProxyPosition.X,
		Placement->ProxyPosition.Y,
		GroundZ));
	TelescopeStreamingSource->EnableStreamingSource();
	OutError.Reset();
	return true;
}

void UAILODVisualDemoWorldSubsystem::DisableTelescopeStreamingSource()
{
	if (IsValid(TelescopeStreamingSource))
	{
		TelescopeStreamingSource->DisableStreamingSource();
	}
}

void UAILODVisualDemoWorldSubsystem::UpdateResidentPresentation()
{
	if (!IsValid(PopulationPresenter))
	{
		return;
	}
	AILOD::FVisualResidentPresentationFrame Frame;
	if (!Runtime.CopyPresentationFrame(Frame))
	{
		return;
	}
	FString Error;
	if (!PopulationPresenter->ApplyPresentationFrame(Frame, Error))
	{
		LastUIMessage = Error;
	}
}

void UAILODVisualDemoWorldSubsystem::InitializePerformanceCapture()
{
	bPerformanceCaptureEnabled = FParse::Value(
		FCommandLine::Get(),
		TEXT("AILODPerfScenario="),
		PerformanceScenario);
	if (!bPerformanceCaptureEnabled)
	{
		return;
	}
#if CSV_PROFILER
	FParse::Value(
		FCommandLine::Get(),
		TEXT("AILODPerfCaptureName="),
		PerformanceCaptureName);
	FParse::Value(
		FCommandLine::Get(),
		TEXT("AILODPerfWarmupSeconds="),
		PerformanceWarmupSeconds);
	FParse::Value(
		FCommandLine::Get(),
		TEXT("AILODPerfCaptureSeconds="),
		PerformanceCaptureSeconds);
	FParse::Value(
		FCommandLine::Get(),
		TEXT("AILODPerfTimeScale="),
		PerformanceTimeScale);

	PerformanceWarmupSeconds = FMath::Max(0.0, PerformanceWarmupSeconds);
	PerformanceCaptureSeconds = FMath::Max(1.0, PerformanceCaptureSeconds);
	if (PerformanceTimeScale != 1 && PerformanceTimeScale != 4)
	{
		UE_LOG(LogTemp, Error,
			TEXT("AILOD Phase 7F-E capture rejected: time scale must be 1 or 4, got %d."),
			PerformanceTimeScale);
		bPerformanceCaptureEnabled = false;
		return;
	}
	if (PerformanceCaptureName.IsEmpty())
	{
		PerformanceCaptureName = FString::Printf(
			TEXT("Phase7F_E_%s_%dx.csv"),
			*PerformanceScenario,
			PerformanceTimeScale);
	}

	UE_LOG(LogTemp, Display,
		TEXT("AILOD Phase 7F-E capture armed: scenario=%s, scale=%dx, warmup=%.1fs, capture=%.1fs, file=%s"),
		*PerformanceScenario,
		PerformanceTimeScale,
		PerformanceWarmupSeconds,
		PerformanceCaptureSeconds,
		*PerformanceCaptureName);
#else
	UE_LOG(LogTemp, Error,
		TEXT("AILOD Phase 7F-E capture requires a build with CSV profiler support."));
	bPerformanceCaptureEnabled = false;
#endif
}

void UAILODVisualDemoWorldSubsystem::UpdatePerformanceCapture(const float DeltaTime)
{
#if CSV_PROFILER
	if (!bPerformanceCaptureEnabled || bPerformanceCaptureFinished
		|| Runtime.GetState() != AILOD::EVisualDemoRuntimeState::Running)
	{
		return;
	}

	if (!bPerformanceTimeScaleApplied)
	{
		FString Error;
		if (!RequestTimeScale(PerformanceTimeScale, Error))
		{
			LastUIMessage = Error;
			bPerformanceCaptureFinished = true;
			return;
		}
		bPerformanceTimeScaleApplied = true;
	}
	if (!bPerformanceViewPositionApplied)
	{
		UWorld* World = GetWorld();
		APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		const TArray<AILOD::FVisualDistrictRecord>& Districts = Runtime.GetLayout().GetDistricts();
		if (!PlayerPawn || Districts.IsEmpty())
		{
			return;
		}
		const FVector2D DistrictRoadJunction = Districts[0].Bounds.Min
			+ (Districts[0].Bounds.Max - Districts[0].Bounds.Min) / 3.0;
		FVector TestViewLocation = PlayerPawn->GetActorLocation();
		TestViewLocation.X = DistrictRoadJunction.X;
		TestViewLocation.Y = DistrictRoadJunction.Y;
		PlayerPawn->SetActorLocation(
			TestViewLocation,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		PerformanceCameraStart = TestViewLocation;
		const AILOD::FVisualDistrictRecord& TravelDistrict = Districts.Last();
		const FVector2D TravelRoadJunction = TravelDistrict.Bounds.Min
			+ (TravelDistrict.Bounds.Max - TravelDistrict.Bounds.Min) / 3.0;
		PerformanceCameraTravelEnd = FVector(
			TravelRoadJunction.X,
			TravelRoadJunction.Y,
			TestViewLocation.Z);
		if (AAILODVisualDemoCharacter* DemoCharacter = Cast<AAILODVisualDemoCharacter>(PlayerPawn))
		{
			const float BoomLength = PerformanceScenario == TEXT("DenseProxies")
				|| PerformanceScenario == TEXT("ActorCap50") ? 5000.0f : 2500.0f;
			const float AnchorYaw = PerformanceScenario == TEXT("TelescopeLift") ? 45.0f : 0.0f;
			DemoCharacter->ApplyPerformanceCameraPose(
				PerformanceCameraStart,
				AnchorYaw,
				BoomLength);
		}
		bPerformanceViewPositionApplied = true;
		UE_LOG(LogTemp, Display,
			TEXT("AILOD Phase 7F-E view positioned at first district road junction: X=%.1f Y=%.1f"),
			DistrictRoadJunction.X,
			DistrictRoadJunction.Y);
	}

	FCsvProfiler* CsvProfiler = FCsvProfiler::Get();
	if (!bPerformanceCaptureStarted)
	{
		PerformanceWarmupElapsedSeconds += DeltaTime;
		if (PerformanceWarmupElapsedSeconds < PerformanceWarmupSeconds)
		{
			return;
		}

		AILOD::FUnifiedDemoSnapshot Snapshot;
		Runtime.CopySnapshot(Snapshot);
		CSV_NON_PERSISTENT_METADATA(TEXT("AILODPhase"), TEXT("7F-E"));
		CSV_NON_PERSISTENT_METADATA(TEXT("AILODScenario"), *PerformanceScenario);
		CSV_NON_PERSISTENT_METADATA(
			TEXT("AILODPopulation"),
			*FString::FromInt(Snapshot.PopulationPerKingdom * 2));
		CSV_NON_PERSISTENT_METADATA(
			TEXT("AILODTimeScale"),
			*FString::FromInt(PerformanceTimeScale));
		CSV_NON_PERSISTENT_METADATA(
			TEXT("AILODDebugLabels"),
			bShowResidentDebugLabels ? TEXT("1") : TEXT("0"));
		CSV_NON_PERSISTENT_METADATA(
			TEXT("AILODNormalActiveBudget"),
			*FString::FromInt(
				GetDefault<UAILODVisualDemoSettings>()->NormalActiveActorBudget));
		CsvProfiler->BeginCapture(-1, FString(), PerformanceCaptureName);
		bPerformanceCaptureStarted = true;
		UE_LOG(LogTemp, Display,
			TEXT("AILOD Phase 7F-E capture started: %s"),
			*PerformanceCaptureName);
		return;
	}

	if (!CsvProfiler->IsCapturing())
	{
		return;
	}
	UpdatePerformanceScenario();
	PerformanceCaptureElapsedSeconds += DeltaTime;
	if (PerformanceCaptureElapsedSeconds >= PerformanceCaptureSeconds)
	{
		if (UWorld* World = GetWorld())
		{
			APlayerController* PlayerController = World->GetFirstPlayerController();
			if (AAILODVisualDemoCharacter* DemoCharacter = PlayerController
				? Cast<AAILODVisualDemoCharacter>(PlayerController->GetPawn()) : nullptr)
			{
				DemoCharacter->SetTelescopeViewEnabled(false);
			}
		}
		CsvProfiler->EndCapture();
		bPerformanceCaptureFinished = true;
		UE_LOG(LogTemp, Display,
			TEXT("AILOD Phase 7F-E capture ended after %.2f seconds."),
			PerformanceCaptureElapsedSeconds);
	}
#endif
}

void UAILODVisualDemoWorldSubsystem::UpdatePerformanceScenario()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	AAILODVisualDemoCharacter* DemoCharacter = PlayerController
		? Cast<AAILODVisualDemoCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!DemoCharacter)
	{
		return;
	}

	if (PerformanceScenario == TEXT("FastTraversal"))
	{
		const double AngleRadians = FMath::DegreesToRadians(
			PerformanceCaptureElapsedSeconds * 90.0);
		const double Radius = 3000.0;
		const FVector CameraLocation = PerformanceCameraStart + FVector(
			Radius * FMath::Sin(AngleRadians),
			Radius * (1.0 - FMath::Cos(AngleRadians)),
			0.0);
		DemoCharacter->ApplyPerformanceCameraPose(
			CameraLocation,
			static_cast<float>(PerformanceCaptureElapsedSeconds * 90.0),
			2500.0f);
		return;
	}

	if (PerformanceScenario == TEXT("WorldPartitionTravel"))
	{
		const double Phase = PerformanceCaptureElapsedSeconds / 10.0 * UE_DOUBLE_PI;
		const double Alpha = 0.5 - 0.5 * FMath::Cos(Phase);
		const FVector CameraLocation = FMath::Lerp(
			PerformanceCameraStart,
			PerformanceCameraTravelEnd,
			Alpha);
		const FVector2D Direction(
			PerformanceCameraTravelEnd.X - PerformanceCameraStart.X,
			PerformanceCameraTravelEnd.Y - PerformanceCameraStart.Y);
		float Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(
			Direction.Y,
			Direction.X)));
		if (FMath::Sin(Phase) < 0.0)
		{
			Yaw += 180.0f;
		}
		DemoCharacter->SetActorLocation(
			CameraLocation,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		DemoCharacter->ApplyPerformanceCameraPose(CameraLocation, Yaw, 2500.0f);
		return;
	}

	if (PerformanceScenario == TEXT("TelescopeLift"))
	{
		const int32 CycleIndex = FMath::FloorToInt(PerformanceCaptureElapsedSeconds / 8.0);
		const double CycleSeconds = FMath::Fmod(PerformanceCaptureElapsedSeconds, 8.0);
		const bool bShouldEnableTelescope = CycleSeconds < 6.0;
		const float Yaw = 45.0f + static_cast<float>((CycleIndex % 3) - 1) * 5.0f;
		DemoCharacter->ApplyPerformanceCameraPose(PerformanceCameraStart, Yaw, 2500.0f);
		if (bShouldEnableTelescope != bPerformanceScenarioTelescopeEnabled)
		{
			DemoCharacter->SetTelescopeViewEnabled(bShouldEnableTelescope);
			bPerformanceScenarioTelescopeEnabled = bShouldEnableTelescope;
			if (!bShouldEnableTelescope)
			{
				bClearTrackedResidentRequested = true;
			}
		}
	}
}

void UAILODVisualDemoWorldSubsystem::RecordPerformanceCsvStats() const
{
#if CSV_PROFILER
	if (!bPerformanceCaptureEnabled || !FCsvProfiler::Get()->IsCapturing())
	{
		return;
	}

	CSV_CUSTOM_STAT(AILODVisual, RuntimeState,
		static_cast<int32>(Runtime.GetState()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, TimeScale, Runtime.GetTimeScale(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, Paused, Runtime.IsPaused() ? 1 : 0, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, PendingHourSteps,
		Runtime.GetPendingHourSteps(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, LastTickStepCount,
		Runtime.GetLastTickStepCount(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, DebugLabelsEnabled,
		bShowResidentDebugLabels ? 1 : 0, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, TelescopeEnabled,
		bTelescopeEnabled ? 1 : 0, ECsvCustomStatOp::Set);

	AILOD::FUnifiedDemoSnapshot Snapshot;
	if (Runtime.CopySnapshot(Snapshot))
	{
		CSV_CUSTOM_STAT(AILODVisual, Population,
			Snapshot.PopulationPerKingdom * 2, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, GameMinutes,
			static_cast<double>(Snapshot.GameTime.Minutes), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, ActiveCount,
			Snapshot.ActiveCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, TrackedResident,
			static_cast<double>(Snapshot.TrackedResidentID), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, StepProductionMs,
			Snapshot.LastStepMeasurement.ProductionCpuMs, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, StepMacroMs,
			Snapshot.LastStepMeasurement.MacroCpuMs, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, StepMicroMs,
			Snapshot.LastStepMeasurement.MicroCpuMs, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, StepTransitionMs,
			Snapshot.LastStepMeasurement.TransitionCpuMs, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, StepQueueLength,
			Snapshot.LastStepMeasurement.QueueLength, ECsvCustomStatOp::Set);
	}

	const AILOD::FVisualObservationDiagnostics& Observation =
		Runtime.GetCurrentPresentationObservationPlan().Diagnostics;
	CSV_CUSTOM_STAT(AILODVisual, NormalVisitedCells,
		Observation.NormalQuery.VisitedCellCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, NormalVisitedResidents,
		Observation.NormalQuery.VisitedResidentEntryCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, NormalMatches,
		Observation.NormalQuery.MatchingResidentCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, NormalReturned,
		Observation.NormalQuery.ReturnedCandidateCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, TelescopeVisitedCells,
		Observation.TelescopeQuery.VisitedCellCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, TelescopeVisitedResidents,
		Observation.TelescopeQuery.VisitedResidentEntryCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, TelescopeMatches,
		Observation.TelescopeQuery.MatchingResidentCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, TelescopeReturned,
		Observation.TelescopeQuery.ReturnedCandidateCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, NormalVisibleCandidates,
		Observation.NormalVisibleCandidateCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, NormalEligibleCandidates,
		Observation.NormalEligibleActiveCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, NormalImmediateCandidates,
		Observation.NormalImmediateCandidateCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, NormalDwellStates,
		Observation.NormalObservationStateCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, DesiredActiveCount,
		Observation.DesiredActiveCount, ECsvCustomStatOp::Set);

	AILOD::FVisualResidentPresentationFrame PresentationFrame;
	const bool bHasPresentationFrame = Runtime.CopyPresentationFrame(PresentationFrame);
	if (bHasPresentationFrame)
	{
		CSV_CUSTOM_STAT(AILODVisual, ProxyCount,
			PresentationFrame.Diagnostics.LowLevelProxyCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, ActiveActorCount,
			PresentationFrame.Diagnostics.ActiveActorCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, ActorPoolCapacity,
			PresentationFrame.Diagnostics.ActorPoolCapacity, ECsvCustomStatOp::Set);
	}
	if (IsValid(PopulationPresenter))
	{
		CSV_CUSTOM_STAT(AILODVisual, BoundActorCount,
			PopulationPresenter->GetBoundActorCount(), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, MotionStateCount,
			PopulationPresenter->GetMotionStateCount(), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AILODVisual, MotionUpdates,
			PopulationPresenter->GetLastMotionUpdateCount(), ECsvCustomStatOp::Set);
	}

	const AILOD::FVisualTelescopeFocusStatus& Focus = TelescopeFocusGate.GetStatus();
	CSV_CUSTOM_STAT(AILODVisual, TelescopeCenterResident,
		static_cast<double>(Focus.CenterResidentID), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, TelescopeFocusSeconds,
		Focus.FocusedRealSeconds, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(AILODVisual, TelescopeStreamingReady,
		Focus.bStreamingReady ? 1 : 0, ECsvCustomStatOp::Set);
	const UWorld* World = GetWorld();
	CSV_CUSTOM_STAT(AILODVisual, LoadedLevelCount,
		World ? World->GetLevels().Num() : 0, ECsvCustomStatOp::Set);
	const UWorldPartitionSubsystem* WorldPartitionSubsystem =
		World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
	CSV_CUSTOM_STAT(AILODVisual, WorldPartitionStreamingReady,
		!WorldPartitionSubsystem || WorldPartitionSubsystem->IsStreamingCompleted() ? 1 : 0,
		ECsvCustomStatOp::Set);
	const bool bFullPopulationScan = Observation.NormalQuery.bScannedResidentCatalog
		|| Observation.TelescopeQuery.bScannedResidentCatalog
		|| (bHasPresentationFrame && PresentationFrame.Diagnostics.bScannedResidentCatalog);
	CSV_CUSTOM_STAT(AILODVisual, FullPopulationScan,
		bFullPopulationScan ? 1 : 0, ECsvCustomStatOp::Set);
#endif
}

void UAILODVisualDemoWorldSubsystem::DrawFunctionalUI()
{
	const ImGui::FScopedContext ScopedContext;
	if (!ScopedContext)
	{
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
	IO.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
	const auto DrawTelescopeReticle = [this]()
	{
		if (!bTelescopeEnabled)
		{
			return;
		}
		ImDrawList* Reticle = ImGui::GetForegroundDrawList();
		const ImVec2 DisplaySize = ImGui::GetIO().DisplaySize;
		const ImVec2 Center(DisplaySize.x * 0.5f, DisplaySize.y * 0.5f);
		const ImU32 Color = IM_COL32(64, 220, 255, 230);
		Reticle->AddCircle(Center, 12.0f, Color, 24, 2.0f);
		Reticle->AddLine(ImVec2(Center.x - 20.0f, Center.y), ImVec2(Center.x - 5.0f, Center.y), Color, 2.0f);
		Reticle->AddLine(ImVec2(Center.x + 5.0f, Center.y), ImVec2(Center.x + 20.0f, Center.y), Color, 2.0f);
		Reticle->AddLine(ImVec2(Center.x, Center.y - 20.0f), ImVec2(Center.x, Center.y - 5.0f), Color, 2.0f);
		Reticle->AddLine(ImVec2(Center.x, Center.y + 5.0f), ImVec2(Center.x, Center.y + 20.0f), Color, 2.0f);
	};
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(460.0f, 650.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("AILOD Visual Demo", nullptr, ImGuiWindowFlags_NoDocking))
	{
		ImGui::End();
		DrawTelescopeReticle();
		return;
	}

	ImGui::Text("State: %s", TCHAR_TO_UTF8(Runtime.GetStateName()));
	ImGui::Text("Mode: Interactive Demo (not formal data)");
	if (bPerformanceCaptureEnabled)
	{
		ImGui::Text("7F-E capture: %s %dx - %s",
			TCHAR_TO_UTF8(*PerformanceScenario),
			PerformanceTimeScale,
			bPerformanceCaptureFinished ? "finished"
				: bPerformanceCaptureStarted ? "recording" : "warmup");
	}
	AILOD::FUnifiedDemoSnapshot Snapshot;
	if (Runtime.CopySnapshot(Snapshot))
	{
		ImGui::Text("Model: v%s", TCHAR_TO_UTF8(*Snapshot.ModelSpecVersion));
		ImGui::Text("Domain digest: %s", TCHAR_TO_UTF8(*Snapshot.DeterministicDigestVersion));
		ImGui::Text("Demo protocol: v%s", TCHAR_TO_UTF8(*Snapshot.DemoProtocolVersion));
		ImGui::Text("Population: %d", Snapshot.PopulationPerKingdom * 2);
		ImGui::Text("Game time: %s", TCHAR_TO_UTF8(*Snapshot.GameTime.ToString()));
	}

	if (Runtime.GetState() == AILOD::EVisualDemoRuntimeState::Prewarming)
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Loading: prewarming Day -7 to Day 0...");
	}
	else if (Runtime.GetState() == AILOD::EVisualDemoRuntimeState::Running)
	{
		ImGui::Separator();
		if (ImGui::Button(Runtime.IsPaused() ? "Resume" : "Pause"))
		{
			FString Error;
			if (!RequestPaused(!Runtime.IsPaused(), Error)) LastUIMessage = Error;
		}
		ImGui::SameLine();
		for (const int32 Scale : { 1, 2, 4 })
		{
			if (Scale != 1) ImGui::SameLine();
			const FString Label = FString::Printf(TEXT("%dx"), Scale);
			if (ImGui::Button(TCHAR_TO_UTF8(*Label)))
			{
				FString Error;
				if (!RequestTimeScale(Scale, Error)) LastUIMessage = Error;
			}
		}
		ImGui::Text("Selected speed: %dx", Runtime.GetTimeScale());
		ImGui::Text("Queued hours: %.2f", Runtime.GetPendingHourSteps());
	}
	else if (Runtime.GetState() == AILOD::EVisualDemoRuntimeState::Complete)
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Day 60 reached. The Demo is stopped.");
		if (ImGui::Button("Restart Demo"))
		{
			FString Error;
			if (!RequestRestart(Error)) LastUIMessage = Error;
		}
	}

	if (Runtime.CopySnapshot(Snapshot))
	{
		ImGui::Separator();
		ImGui::Text("Active residents: %d / 50", Snapshot.ActiveCount);
		ImGui::Text("Kingdom A homes: healthy %d, waiting %d, repair %d, repaired %d",
			Snapshot.KingdomA.Healthy,
			Snapshot.KingdomA.DamagedWaiting,
			Snapshot.KingdomA.UnderRepair,
			Snapshot.KingdomA.Repaired);
		ImGui::Text("Kingdom B homes: healthy %d, waiting %d, repair %d, repaired %d",
			Snapshot.KingdomB.Healthy,
			Snapshot.KingdomB.DamagedWaiting,
			Snapshot.KingdomB.UnderRepair,
			Snapshot.KingdomB.Repaired);
		const int32 VisibleRows = FMath::Min(10, Snapshot.ActiveResidents.Num());
		for (int32 Index = 0; Index < VisibleRows; ++Index)
		{
			const AILOD::FUnifiedDemoResidentSnapshot& Resident = Snapshot.ActiveResidents[Index];
			ImGui::BulletText("ID %lld  %s  Home %lld  Cash %d",
				Resident.ResidentID,
				TCHAR_TO_UTF8(*Resident.Name),
				Resident.HomeID,
				Resident.Cash);
		}

		const AILOD::FVisualObservationDiagnostics& Diagnostics =
			Runtime.GetCurrentPresentationObservationPlan().Diagnostics;
		ImGui::Separator();
		ImGui::Text("Normal query: %s",
			GetDefault<UAILODVisualDemoSettings>()->bUseRadialNormalObservation
				? "screen-ground focus + bounded radius"
				: "forward cone");
		ImGui::Text("Spatial query: %d cells, %d resident entries",
			Diagnostics.NormalQuery.VisitedCellCount,
			Diagnostics.NormalQuery.VisitedResidentEntryCount);
		ImGui::Text("Observed on-screen: %d | eligible: %d | dwell states: %d",
			Diagnostics.NormalVisibleCandidateCount,
			Diagnostics.NormalEligibleActiveCount,
			Diagnostics.NormalObservationStateCount);
		if (Diagnostics.bNormalImmediateBudgetOverflow)
		{
			ImGui::TextColored(
				ImVec4(1.0f, 0.35f, 0.2f, 1.0f),
				"Close/selected residents exceed the normal Active budget (%d > %d)",
				Diagnostics.NormalImmediateCandidateCount,
				GetDefault<UAILODVisualDemoSettings>()->NormalActiveActorBudget);
		}
		else if (Diagnostics.bNormalActiveBudgetSaturated)
		{
			ImGui::TextColored(
				ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
				"Observation budget saturated; lower-priority visible residents remain proxies.");
		}
		ImGui::Text("Full population scan: %s", Diagnostics.NormalQuery.bScannedResidentCatalog ? "YES (ERROR)" : "No");
		ImGui::Separator();
		ImGui::TextUnformatted("Telescope: hold Right Mouse Button for ground view; aim with screen center");
		if (bTelescopeEnabled)
		{
			const AILOD::FVisualTelescopeFocusStatus& Focus = TelescopeFocusGate.GetStatus();
			ImGui::Text("Telescope candidates: %d", Diagnostics.TelescopeProxyCount);
			ImGui::Text("Telescope query: %d cells, %d resident entries",
				Diagnostics.TelescopeQuery.VisitedCellCount,
				Diagnostics.TelescopeQuery.VisitedResidentEntryCount);
			ImGui::Text("Center ResidentID: %lld", Focus.CenterResidentID);
			ImGui::Text("Focus: %.2f / %.2f s", Focus.FocusedRealSeconds,
				GetDefault<UAILODVisualDemoSettings>()->TelescopeFocusSeconds);
			ImGui::Text("Distant cell: %s", Focus.bStreamingReady ? "Ready" : "Loading...");
		}
		else
		{
			ImGui::TextUnformatted("Telescope state: Off");
		}
		if (Snapshot.TrackedResidentID > 0)
		{
			ImGui::Text("Tracked ResidentID: %lld", Snapshot.TrackedResidentID);
			if (ImGui::Button("Clear tracked resident"))
			{
				bClearTrackedResidentRequested = true;
			}
		}
	}

	AILOD::FVisualResidentPresentationFrame PresentationFrame;
	const bool bHasPresentationFrame = Runtime.CopyPresentationFrame(PresentationFrame);
	if (bHasPresentationFrame)
	{
		ImGui::Separator();
		ImGui::Checkbox("ResidentID debug labels", &bShowResidentDebugLabels);
		ImGui::Text("Low-level proxies: %d", PresentationFrame.Diagnostics.LowLevelProxyCount);
		ImGui::Text("Full NPC Actors: %d / %d",
			PresentationFrame.Diagnostics.ActiveActorCount,
			PresentationFrame.Diagnostics.ActorPoolCapacity);
		if (IsValid(PopulationPresenter))
		{
			ImGui::Text("Actor pool: bound %d, total rebinds %lld, total releases %lld",
				PopulationPresenter->GetBoundActorCount(),
				PopulationPresenter->GetTotalReboundCount(),
				PopulationPresenter->GetTotalReleasedCount());
			ImGui::Text("Visible motion: %d states, %d updates this frame",
				PopulationPresenter->GetMotionStateCount(),
				PopulationPresenter->GetLastMotionUpdateCount());
		}
		if (PresentationFrame.bHasSelectedResident)
		{
			const AILOD::FVisualResidentPresentationEntry& Selected = PresentationFrame.SelectedResident;
			ImGui::Separator();
			ImGui::Text("Selected ResidentID: %lld", Selected.ResidentID);
			const AILOD::FVisualObservationPlan& CurrentPlan =
				Runtime.GetCurrentPresentationObservationPlan();
			const bool bCandidate = CurrentPlan.NormalProxyCandidates.ContainsByPredicate(
				[&Selected](const AILOD::FVisualProxyCandidate& Candidate)
				{
					return Candidate.ResidentID == Selected.ResidentID;
				}) || CurrentPlan.TelescopeProxyCandidates.ContainsByPredicate(
				[&Selected](const AILOD::FVisualProxyCandidate& Candidate)
				{
					return Candidate.ResidentID == Selected.ResidentID;
				});
			const bool bDesired = CurrentPlan.ActiveRequest.DesiredActiveResidentIDs.Contains(
				Selected.ResidentID);
			const int32 ProxySlot = IsValid(PopulationPresenter)
				? PopulationPresenter->FindProxySlot(Selected.ResidentID)
				: INDEX_NONE;
			const int32 ActorSlot = IsValid(PopulationPresenter)
				? PopulationPresenter->FindActorSlot(Selected.ResidentID)
				: INDEX_NONE;
			ImGui::Text("Chain: Candidate %s | Desired %s | Committed %s",
				bCandidate ? "Yes" : "No",
				bDesired ? "Yes" : "No",
				Selected.bHasActiveState ? "Yes" : "No");
			ImGui::Text("Slots: Proxy %d | Actor %d", ProxySlot, ActorSlot);
			if (Selected.bHasActiveState)
			{
				ImGui::Text("Name: %s", TCHAR_TO_UTF8(*Selected.ActiveState.Name));
				ImGui::Text("HomeID: %lld  Cash: %d", Selected.HomeID, Selected.ActiveState.Cash);
				ImGui::Text("Action: %s  Remaining: %lld min",
					TCHAR_TO_UTF8(AILOD::ToString(Selected.ActiveState.CurrentAction)),
					Selected.ActiveState.RemainingWorkMinutes);
			}
			else
			{
				ImGui::TextUnformatted("Low-level proxy: real resident; exact action unavailable.");
				ImGui::Text("HomeID: %lld  Visual home slot: %lld",
					Selected.HomeID,
					Selected.VisualHomeSlotID);
			}
			if (ImGui::Button("Clear selection"))
			{
				Runtime.ClearSelectedResident();
			}
		}
	}

	if (!LastUIMessage.IsEmpty())
	{
		ImGui::Separator();
		ImGui::TextWrapped("Message: %s", TCHAR_TO_UTF8(*LastUIMessage));
	}
	if (!Runtime.GetLastObservationWarning().IsEmpty())
	{
		ImGui::Separator();
		ImGui::TextWrapped(
			"Observation: %s",
			TCHAR_TO_UTF8(*Runtime.GetLastObservationWarning()));
	}
	ImGui::End();
	DrawTelescopeReticle();
	if (bHasPresentationFrame && bShowResidentDebugLabels)
	{
		DrawResidentDebugLabels(PresentationFrame);
	}
}

void UAILODVisualDemoWorldSubsystem::DrawResidentDebugLabels(
	const AILOD::FVisualResidentPresentationFrame& Frame) const
{
	const UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !IsValid(PopulationPresenter))
	{
		return;
	}

	ImDrawList* DrawList = ImGui::GetBackgroundDrawList();
	const ImVec2 DisplaySize = ImGui::GetIO().DisplaySize;
	const AILOD::FVisualObservationPlan& CurrentPlan =
		Runtime.GetCurrentPresentationObservationPlan();
	auto DrawEntry = [&](const AILOD::FVisualResidentPresentationEntry& Entry)
	{
		FVector WorldLocation;
		if (!PopulationPresenter->FindResidentLabelLocation(Entry.ResidentID, WorldLocation))
		{
			return;
		}
		FVector2D ScreenPosition;
		if (!PlayerController->ProjectWorldLocationToScreen(WorldLocation, ScreenPosition, true)
			|| ScreenPosition.X < 0.0 || ScreenPosition.Y < 0.0
			|| ScreenPosition.X > DisplaySize.x || ScreenPosition.Y > DisplaySize.y)
		{
			return;
		}

		const bool bSelected = Frame.SelectedResidentID == Entry.ResidentID;
		const bool bDesired = CurrentPlan.ActiveRequest.DesiredActiveResidentIDs.Contains(
			Entry.ResidentID);
		const bool bDeferred = !Entry.bActiveActor
			&& bDesired
			&& !Runtime.GetLastObservationWarning().IsEmpty();
		const TCHAR State = Entry.bActiveActor ? TCHAR('A') : bDeferred ? TCHAR('D') : TCHAR('P');
		const FString Label = Entry.bHasActiveState
			? FString::Printf(
				TEXT("%c %lld %s"),
				State,
				Entry.ResidentID,
				AILOD::ToString(Entry.ActiveState.CurrentAction))
			: FString::Printf(TEXT("%c %lld"), State, Entry.ResidentID);
		const ImU32 Color = bSelected
			? IM_COL32(64, 220, 255, 255)
			: Entry.bActiveActor
				? IM_COL32(80, 255, 120, 255)
				: bDeferred
					? IM_COL32(255, 210, 40, 255)
					: IM_COL32(205, 205, 205, 230);
		const FTCHARToUTF8 LabelUtf8(*Label);
		const ImVec2 TextSize = ImGui::CalcTextSize(LabelUtf8.Get());
		const ImVec2 Position(
			static_cast<float>(ScreenPosition.X) - TextSize.x * 0.5f,
			static_cast<float>(ScreenPosition.Y) - TextSize.y);
		DrawList->AddText(ImVec2(Position.x + 1.0f, Position.y + 1.0f), IM_COL32(0, 0, 0, 220), LabelUtf8.Get());
		DrawList->AddText(Position, Color, LabelUtf8.Get());
	};

	for (const AILOD::FVisualResidentPresentationEntry& Entry : Frame.LowLevelProxies)
	{
		DrawEntry(Entry);
	}
	for (const AILOD::FVisualResidentPresentationEntry& Entry : Frame.ActiveActors)
	{
		DrawEntry(Entry);
	}
}
