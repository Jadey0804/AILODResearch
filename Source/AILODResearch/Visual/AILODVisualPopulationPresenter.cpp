// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualPopulationPresenter.h"

#include "AILODVisualResidentActor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

AAILODVisualPopulationPresenter::AAILODVisualPopulationPresenter()
	: PoolPlanner(50)
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ProxyInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LowLevelProxies"));
	ProxyInstances->SetupAttachment(SceneRoot);
	ProxyInstances->SetMobility(EComponentMobility::Movable);
	ProxyInstances->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProxyInstances->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProxyInstances->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ProxyInstances->SetGenerateOverlapEvents(false);
	ProxyInstances->SetCanEverAffectNavigation(false);
	ProxyInstances->SetCastShadow(false);
}

void AAILODVisualPopulationPresenter::AdvancePresentation(
	const float DeltaSeconds,
	const double PlaybackRate)
{
	const float EffectiveDeltaSeconds = FMath::Max(0.0f, DeltaSeconds)
		* static_cast<float>(FMath::Clamp(PlaybackRate, 0.0, 4.0));
	for (AAILODVisualResidentActor* ResidentActor : ResidentActorPool)
	{
		if (IsValid(ResidentActor) && ResidentActor->IsBound())
		{
			ResidentActor->AdvancePlaceholderAnimation(EffectiveDeltaSeconds);
		}
	}
}

void AAILODVisualPopulationPresenter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AAILODVisualResidentActor* ResidentActor : ResidentActorPool)
	{
		if (IsValid(ResidentActor)) ResidentActor->Destroy();
	}
	ResidentActorPool.Reset();
	Super::EndPlay(EndPlayReason);
}

bool AAILODVisualPopulationPresenter::InitializePresentation(
	UStaticMesh* ProxyMesh,
	UStaticMesh* FullActorBodyMesh,
	UStaticMesh* FullActorHeadMesh,
	const int32 LowLevelProxyCapacity,
	const double GroundZCentimeters,
	const double WalkSpeedCentimetersPerSecond,
	FString& OutError)
{
	if (ProxyMesh == nullptr
		|| FullActorBodyMesh == nullptr
		|| FullActorHeadMesh == nullptr
		|| LowLevelProxyCapacity <= 0
		|| WalkSpeedCentimetersPerSecond <= 0.0
		|| GetWorld() == nullptr)
	{
		OutError = TEXT("The resident presenter requires three valid meshes, a World, and a positive placeholder speed.");
		return false;
	}
	GroundZ = GroundZCentimeters;
	WalkSpeed = WalkSpeedCentimetersPerSecond;
	ProxyInstances->SetStaticMesh(ProxyMesh);
	ProxySlotPlanner = MakeUnique<AILOD::FVisualProxySlotPlanner>(LowLevelProxyCapacity);
	ProxyResidentIDs.Init(0, LowLevelProxyCapacity);
	ProxyPositions.Init(FVector2D::ZeroVector, LowLevelProxyCapacity);
	const FTransform HiddenTransform(
		FRotator::ZeroRotator,
		FVector(0.0, 0.0, GroundZ),
		FVector::ZeroVector);
	for (int32 SlotIndex = 0; SlotIndex < LowLevelProxyCapacity; ++SlotIndex)
	{
		ProxyInstances->AddInstance(HiddenTransform, true);
	}

	if (ResidentActorPool.IsEmpty())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ResidentActorPool.Reserve(50);
		for (int32 SlotIndex = 0; SlotIndex < 50; ++SlotIndex)
		{
			AAILODVisualResidentActor* ResidentActor = GetWorld()->SpawnActor<AAILODVisualResidentActor>(
				AAILODVisualResidentActor::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
			if (ResidentActor == nullptr)
			{
				OutError = TEXT("The resident presenter could not allocate all 50 full NPC Actor slots.");
				return false;
			}
			ResidentActor->ConfigureMeshes(FullActorBodyMesh, FullActorHeadMesh);
			ResidentActorPool.Add(ResidentActor);
		}
	}
	if (ResidentActorPool.Num() != 50)
	{
		OutError = TEXT("The full NPC Actor pool must contain exactly 50 reusable slots.");
		return false;
	}
	bInitialized = true;
	ResetPresentation();
	OutError.Reset();
	return true;
}

bool AAILODVisualPopulationPresenter::ApplyPresentationFrame(
	const AILOD::FVisualResidentPresentationFrame& Frame,
	FString& OutError)
{
	if (!bInitialized
		|| Frame.Diagnostics.ActorPoolCapacity != 50
		|| Frame.ActiveActors.Num() > 50
		|| !ProxySlotPlanner)
	{
		OutError = TEXT("The resident presenter requires an initialized fixed 50-slot Actor pool.");
		return false;
	}

	AILOD::FVisualProxySlotPlanner CandidateProxyPlanner = *ProxySlotPlanner;
	AILOD::FVisualProxySlotPlan ProxySlotPlan;
	if (!CandidateProxyPlanner.Reconcile(Frame.LowLevelProxies, ProxySlotPlan, OutError))
	{
		return false;
	}

	AILOD::FVisualActorPoolPlanner CandidatePoolPlanner = PoolPlanner;
	AILOD::FVisualActorPoolPlan PoolPlan;
	if (!CandidatePoolPlanner.Reconcile(Frame.ActiveActors, PoolPlan, OutError))
	{
		return false;
	}
	TMap<AILOD::FResidentID, const AILOD::FVisualResidentPresentationEntry*> EntriesByResidentID;
	for (const AILOD::FVisualResidentPresentationEntry& Entry : Frame.ActiveActors)
	{
		EntriesByResidentID.Add(Entry.ResidentID, &Entry);
	}
	for (const AILOD::FResidentID ResidentID : PoolPlan.SlotResidentIDs)
	{
		if (ResidentID != 0 && !EntriesByResidentID.Contains(ResidentID))
		{
			OutError = TEXT("An Actor-pool plan could not resolve its copied presentation entry.");
			return false;
		}
	}
	if (!ApplyProxySlots(Frame, ProxySlotPlan, OutError))
	{
		return false;
	}
	for (int32 SlotIndex = 0; SlotIndex < PoolPlan.SlotResidentIDs.Num(); ++SlotIndex)
	{
		AAILODVisualResidentActor* ResidentActor = ResidentActorPool[SlotIndex];
		const AILOD::FResidentID ResidentID = PoolPlan.SlotResidentIDs[SlotIndex];
		if (ResidentID == 0)
		{
			ResidentActor->ReleaseToPool();
			continue;
		}
		const AILOD::FVisualResidentPresentationEntry* const* Entry = EntriesByResidentID.Find(ResidentID);
		if (Entry == nullptr)
		{
			OutError = TEXT("An Actor-pool slot could not resolve its copied presentation entry.");
			return false;
		}
		ResidentActor->ApplyPresentationEntry(**Entry, GroundZ, WalkSpeed);
	}
	*ProxySlotPlanner = MoveTemp(CandidateProxyPlanner);
	PoolPlanner = MoveTemp(CandidatePoolPlanner);
	BoundActorCount = PoolPlan.BoundCount;
	LastReleasedCount = PoolPlan.ReleasedCount;
	LastReboundCount = PoolPlan.ReboundCount;
	TotalReleasedCount += PoolPlan.ReleasedCount;
	TotalReboundCount += PoolPlan.ReboundCount;
	OutError.Reset();
	return true;
}

void AAILODVisualPopulationPresenter::ResetPresentation()
{
	if (ProxySlotPlanner)
	{
		ProxySlotPlanner->Reset();
	}
	HideAllProxySlots();
	PoolPlanner.Reset();
	for (AAILODVisualResidentActor* ResidentActor : ResidentActorPool)
	{
		if (IsValid(ResidentActor)) ResidentActor->ReleaseToPool();
	}
	BoundActorCount = 0;
	VisibleProxyCount = 0;
	LastReleasedCount = 0;
	LastReboundCount = 0;
	TotalReleasedCount = 0;
	TotalReboundCount = 0;
}

AILOD::FResidentID AAILODVisualPopulationPresenter::ResolveProxyResidentID(const FHitResult& Hit) const
{
	return Hit.GetActor() == this
		&& Hit.GetComponent() == ProxyInstances
		? ResolveProxyResidentIDByInstanceIndex(Hit.Item)
		: 0;
}

AILOD::FResidentID AAILODVisualPopulationPresenter::ResolveProxyResidentIDByInstanceIndex(
	const int32 InstanceIndex) const
{
	return ProxyResidentIDs.IsValidIndex(InstanceIndex)
		? ProxyResidentIDs[InstanceIndex]
		: 0;
}

int32 AAILODVisualPopulationPresenter::FindProxySlot(
	const AILOD::FResidentID ResidentID) const
{
	return ResidentID > 0 ? ProxyResidentIDs.IndexOfByKey(ResidentID) : INDEX_NONE;
}

int32 AAILODVisualPopulationPresenter::FindActorSlot(
	const AILOD::FResidentID ResidentID) const
{
	for (int32 SlotIndex = 0; SlotIndex < ResidentActorPool.Num(); ++SlotIndex)
	{
		const AAILODVisualResidentActor* ResidentActor = ResidentActorPool[SlotIndex];
		if (IsValid(ResidentActor) && ResidentActor->GetBoundResidentID() == ResidentID)
		{
			return SlotIndex;
		}
	}
	return INDEX_NONE;
}

bool AAILODVisualPopulationPresenter::FindResidentLabelLocation(
	const AILOD::FResidentID ResidentID,
	FVector& OutLocation) const
{
	const int32 ActorSlot = FindActorSlot(ResidentID);
	if (ResidentActorPool.IsValidIndex(ActorSlot) && IsValid(ResidentActorPool[ActorSlot]))
	{
		OutLocation = ResidentActorPool[ActorSlot]->GetActorLocation() + FVector(0.0, 0.0, 210.0);
		return true;
	}
	const int32 ProxySlot = FindProxySlot(ResidentID);
	if (ProxyPositions.IsValidIndex(ProxySlot))
	{
		OutLocation = FVector(ProxyPositions[ProxySlot].X, ProxyPositions[ProxySlot].Y, GroundZ + 220.0);
		return true;
	}
	return false;
}

bool AAILODVisualPopulationPresenter::ApplyProxySlots(
	const AILOD::FVisualResidentPresentationFrame& Frame,
	const AILOD::FVisualProxySlotPlan& SlotPlan,
	FString& OutError)
{
	if (!ProxySlotPlanner
		|| ProxyResidentIDs.Num() != ProxySlotPlanner->GetCapacity()
		|| ProxyInstances->GetInstanceCount() != ProxySlotPlanner->GetCapacity())
	{
		OutError = TEXT("The low-level proxy presenter requires its fixed visual slot allocation.");
		return false;
	}

	TMap<AILOD::FResidentID, const AILOD::FVisualResidentPresentationEntry*> EntriesByResidentID;
	for (const AILOD::FVisualResidentPresentationEntry& Entry : Frame.LowLevelProxies)
	{
		EntriesByResidentID.Add(Entry.ResidentID, &Entry);
	}
	bool bAnyTransformChanged = false;
	for (int32 SlotIndex = 0; SlotIndex < SlotPlan.SlotResidentIDs.Num(); ++SlotIndex)
	{
		const AILOD::FResidentID ResidentID = SlotPlan.SlotResidentIDs[SlotIndex];
		FTransform Transform(
			FRotator::ZeroRotator,
			FVector(0.0, 0.0, GroundZ),
			FVector::ZeroVector);
		FVector2D Position = FVector2D::ZeroVector;
		if (ResidentID != 0)
		{
			const AILOD::FVisualResidentPresentationEntry* const* Entry = EntriesByResidentID.Find(ResidentID);
			if (Entry == nullptr)
			{
				OutError = TEXT("A low-level proxy slot could not resolve its copied presentation entry.");
				return false;
			}
			Position = (*Entry)->ProxyPosition;
			Transform = FTransform(
				FRotator(0.0, (*Entry)->FacingDegrees, 0.0),
				FVector(Position.X, Position.Y, GroundZ + 70.0),
				FVector(0.25, 0.25, 1.4));
		}
		if (ProxyResidentIDs[SlotIndex] != ResidentID
			|| ProxyPositions[SlotIndex] != Position)
		{
			ProxyInstances->UpdateInstanceTransform(
				SlotIndex,
				Transform,
				true,
				false,
				true);
			ProxyResidentIDs[SlotIndex] = ResidentID;
			ProxyPositions[SlotIndex] = Position;
			bAnyTransformChanged = true;
		}
	}
	if (bAnyTransformChanged)
	{
		ProxyInstances->MarkRenderStateDirty();
	}
	VisibleProxyCount = SlotPlan.VisibleCount;
	OutError.Reset();
	return true;
}

void AAILODVisualPopulationPresenter::HideAllProxySlots()
{
	const FTransform HiddenTransform(
		FRotator::ZeroRotator,
		FVector(0.0, 0.0, GroundZ),
		FVector::ZeroVector);
	for (int32 SlotIndex = 0; SlotIndex < ProxyResidentIDs.Num(); ++SlotIndex)
	{
		ProxyInstances->UpdateInstanceTransform(
			SlotIndex,
			HiddenTransform,
			true,
			false,
			true);
		ProxyResidentIDs[SlotIndex] = 0;
		ProxyPositions[SlotIndex] = FVector2D::ZeroVector;
	}
	ProxyInstances->MarkRenderStateDirty();
}
