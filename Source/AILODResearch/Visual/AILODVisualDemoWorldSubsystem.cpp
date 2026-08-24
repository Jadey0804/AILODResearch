// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualDemoWorldSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "ImGuiConfig.h"
#include "AILODVisualDemoGameMode.h"
#include "AILODVisualDemoSettings.h"
#include "imgui.h"

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
	Runtime = AILOD::FVisualDemoRuntime();
	LastUIMessage.Reset();
	bDemoActivated = false;
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
		FString InitializationError;
		if (!Runtime.Initialize(Settings->MakeRuntimeConfig(), InitializationError))
		{
			LastUIMessage = InitializationError;
			UE_LOG(LogTemp, Error, TEXT("AILOD Phase 7C Demo initialization failed: %s"), *InitializationError);
		}
	}

	FString Error;
	if (!Runtime.Tick(DeltaTime, Error))
	{
		LastUIMessage = Error;
	}
	if (Runtime.GetState() == AILOD::EVisualDemoRuntimeState::Running)
	{
		UpdateCameraObservation();
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

const AILOD::FVisualWorldLayout* UAILODVisualDemoWorldSubsystem::GetReadOnlyLayout() const
{
	return bDemoActivated && Runtime.GetLayout().IsBuilt() ? &Runtime.GetLayout() : nullptr;
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
	return Runtime.Restart(OutError);
}

void UAILODVisualDemoWorldSubsystem::UpdateCameraObservation()
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
	const FVector CameraForward3D = PlayerController->PlayerCameraManager->GetCameraRotation().Vector();
	FVector2D CameraForward(CameraForward3D.X, CameraForward3D.Y);
	CameraForward.Normalize();

	AILOD::FVisualObservationFrameInput Input;
	Input.NormalView.Origin = FVector2D(CameraLocation.X, CameraLocation.Y);
	Input.NormalView.Forward = CameraForward;
	const UAILODVisualDemoSettings* Settings = GetDefault<UAILODVisualDemoSettings>();
	Input.NormalView.EnterDistance = Settings->NormalObservationDistanceMeters * 100.0;
	Input.NormalView.HalfAngleDegrees = Settings->NormalObservationHalfAngleDegrees;
	FString Error;
	if (!Runtime.SubmitObservationFrame(Input, Error))
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
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(430.0f, 560.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("AILOD Visual Demo", nullptr, ImGuiWindowFlags_NoDocking))
	{
		ImGui::End();
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

		const AILOD::FVisualObservationDiagnostics& Diagnostics = Runtime.GetLastObservationPlan().Diagnostics;
		ImGui::Separator();
		ImGui::Text("Spatial query: %d cells, %d resident entries",
			Diagnostics.NormalQuery.VisitedCellCount,
			Diagnostics.NormalQuery.VisitedResidentEntryCount);
		ImGui::Text("Full population scan: %s", Diagnostics.NormalQuery.bScannedResidentCatalog ? "YES (ERROR)" : "No");
	}

	if (!LastUIMessage.IsEmpty())
	{
		ImGui::Separator();
		ImGui::TextWrapped("Message: %s", TCHAR_TO_UTF8(*LastUIMessage));
	}
	ImGui::End();
}
