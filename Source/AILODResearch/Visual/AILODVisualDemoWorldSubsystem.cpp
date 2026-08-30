// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualDemoWorldSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "ImGuiConfig.h"
#include "AILODVisualDemoGameMode.h"
#include "AILODVisualDemoSettings.h"
#include "AILODVisualPopulationPresenter.h"
#include "AILODVisualResidentActor.h"
#include "Components/SceneComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "imgui.h"

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
