// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualPopulationPresenter.h"

#include "AILODVisualResidentActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

AAILODVisualPopulationPresenter::AAILODVisualPopulationPresenter()
	: PoolPlanner(50)
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ProxyBodies = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LowLevelProxyBodies"));
	ProxyBodies->SetupAttachment(SceneRoot);
	ProxyBodies->SetMobility(EComponentMobility::Movable);
	ProxyBodies->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProxyBodies->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProxyBodies->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ProxyBodies->SetGenerateOverlapEvents(false);
	ProxyBodies->SetCanEverAffectNavigation(false);
	ProxyBodies->SetCastShadow(false);

	ProxyHeads = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LowLevelProxyHeads"));
	ProxyHeads->SetupAttachment(SceneRoot);
	ProxyHeads->SetMobility(EComponentMobility::Movable);
	ProxyHeads->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProxyHeads->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProxyHeads->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ProxyHeads->SetGenerateOverlapEvents(false);
	ProxyHeads->SetCanEverAffectNavigation(false);
	ProxyHeads->SetCastShadow(false);
}

void AAILODVisualPopulationPresenter::AdvancePresentation(
	const float DeltaSeconds,
	const double PlaybackRate)
{
	const float EffectiveDeltaSeconds = FMath::Max(0.0f, DeltaSeconds)
		* static_cast<float>(FMath::Clamp(PlaybackRate, 0.0, 4.0));
	LastMotionUpdateCount = 0;
	if (EffectiveDeltaSeconds <= 0.0f)
	{
		return;
	}

	bool bUpdatedProxy = false;
	for (int32 SlotIndex = 0; SlotIndex < ProxyResidentIDs.Num(); ++SlotIndex)
	{
		const AILOD::FResidentID ResidentID = ProxyResidentIDs[SlotIndex];
		AILOD::FVisualResidentMotionState* MotionState = MotionStates.Find(ResidentID);
		if (ResidentID <= 0 || MotionState == nullptr || !ProxyEntries.IsValidIndex(SlotIndex))
		{
			continue;
		}
		AILOD::FVisualResidentPresentationPlanner::AdvanceMotionState(
			ProxyEntries[SlotIndex],
			EffectiveDeltaSeconds,
			WalkSpeed,
			*MotionState);
		UpdateProxyInstanceTransform(
			SlotIndex,
			ProxyEntries[SlotIndex],
			*MotionState,
			false);
		bUpdatedProxy = true;
		++LastMotionUpdateCount;
	}
	if (bUpdatedProxy)
	{
		ProxyBodies->MarkRenderStateDirty();
		ProxyHeads->MarkRenderStateDirty();
	}

	for (AAILODVisualResidentActor* ResidentActor : ResidentActorPool)
	{
		if (IsValid(ResidentActor) && ResidentActor->IsBound())
		{
			AILOD::FVisualResidentMotionState* MotionState = MotionStates.Find(
				ResidentActor->GetBoundResidentID());
			if (MotionState == nullptr)
			{
				continue;
			}
			AILOD::FVisualResidentPresentationPlanner::AdvanceMotionState(
				ResidentActor->GetPresentationEntry(),
				EffectiveDeltaSeconds,
				WalkSpeed,
				*MotionState);
			ResidentActor->ApplyMotionState(*MotionState);
			++LastMotionUpdateCount;
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
	UStaticMesh* ResidentBodyMesh,
	UStaticMesh* ResidentHeadMesh,
	const int32 LowLevelProxyCapacity,
	const double GroundZCentimeters,
	const double WalkSpeedCentimetersPerSecond,
	FString& OutError)
{
	if (ResidentBodyMesh == nullptr
		|| ResidentHeadMesh == nullptr
		|| LowLevelProxyCapacity <= 0
		|| WalkSpeedCentimetersPerSecond <= 0.0
		|| GetWorld() == nullptr)
	{
		OutError = TEXT("The resident presenter requires shared body/head meshes, a World, and a positive placeholder speed.");
		return false;
	}
	GroundZ = GroundZCentimeters;
	WalkSpeed = WalkSpeedCentimetersPerSecond;
	ProxyBodies->SetStaticMesh(ResidentBodyMesh);
	ProxyHeads->SetStaticMesh(ResidentHeadMesh);
	ProxySlotPlanner = MakeUnique<AILOD::FVisualProxySlotPlanner>(LowLevelProxyCapacity);
	ProxyResidentIDs.Init(0, LowLevelProxyCapacity);
	ProxyPositions.Init(FVector2D::ZeroVector, LowLevelProxyCapacity);
	ProxyEntries.SetNum(LowLevelProxyCapacity);
	const FTransform HiddenTransform(
		FRotator::ZeroRotator,
		FVector(0.0, 0.0, GroundZ),
		FVector::ZeroVector);
	for (int32 SlotIndex = 0; SlotIndex < LowLevelProxyCapacity; ++SlotIndex)
	{
		ProxyBodies->AddInstance(HiddenTransform, true);
		ProxyHeads->AddInstance(HiddenTransform, true);
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
			ResidentActor->ConfigureMeshes(ResidentBodyMesh, ResidentHeadMesh);
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

	TMap<AILOD::FResidentID, AILOD::FVisualResidentMotionState> CandidateMotionStates = MotionStates;
	TSet<AILOD::FResidentID> DesiredResidentIDs;
	for (const AILOD::FVisualResidentPresentationEntry& Entry : Frame.LowLevelProxies)
	{
		DesiredResidentIDs.Add(Entry.ResidentID);
		if (!CandidateMotionStates.Contains(Entry.ResidentID))
		{
			CandidateMotionStates.Add(
				Entry.ResidentID,
				AILOD::FVisualResidentPresentationPlanner::MakeInitialMotionState(Entry));
		}
	}
	for (const AILOD::FVisualResidentPresentationEntry& Entry : Frame.ActiveActors)
	{
		DesiredResidentIDs.Add(Entry.ResidentID);
		if (!CandidateMotionStates.Contains(Entry.ResidentID))
		{
			CandidateMotionStates.Add(
				Entry.ResidentID,
				AILOD::FVisualResidentPresentationPlanner::MakeInitialMotionState(Entry));
		}
	}
	for (auto Iterator = CandidateMotionStates.CreateIterator(); Iterator; ++Iterator)
	{
		if (!DesiredResidentIDs.Contains(Iterator.Key()))
		{
			Iterator.RemoveCurrent();
		}
	}

	if (!ApplyProxySlots(Frame, ProxySlotPlan, CandidateMotionStates, OutError))
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
		const AILOD::FVisualResidentMotionState* MotionState = CandidateMotionStates.Find(ResidentID);
		if (MotionState == nullptr)
		{
			OutError = TEXT("An Actor-pool slot could not resolve its bounded presentation motion state.");
			return false;
		}
		ResidentActor->ApplyPresentationEntry(**Entry, GroundZ);
		ResidentActor->ApplyMotionState(*MotionState);
	}
	MotionStates = MoveTemp(CandidateMotionStates);
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
	LastMotionUpdateCount = 0;
	MotionStates.Reset();
}

AILOD::FResidentID AAILODVisualPopulationPresenter::ResolveProxyResidentID(const FHitResult& Hit) const
{
	return Hit.GetActor() == this
		&& (Hit.GetComponent() == ProxyBodies || Hit.GetComponent() == ProxyHeads)
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
	const TMap<AILOD::FResidentID, AILOD::FVisualResidentMotionState>& CandidateMotionStates,
	FString& OutError)
{
	if (!ProxySlotPlanner
		|| ProxyResidentIDs.Num() != ProxySlotPlanner->GetCapacity()
		|| ProxyEntries.Num() != ProxySlotPlanner->GetCapacity()
		|| ProxyBodies->GetInstanceCount() != ProxySlotPlanner->GetCapacity()
		|| ProxyHeads->GetInstanceCount() != ProxySlotPlanner->GetCapacity())
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
		if (ResidentID == 0)
		{
			if (ProxyResidentIDs[SlotIndex] != 0)
			{
				HideProxySlot(SlotIndex, false);
				bAnyTransformChanged = true;
			}
			continue;
		}

		const AILOD::FVisualResidentPresentationEntry* const* Entry = EntriesByResidentID.Find(ResidentID);
		const AILOD::FVisualResidentMotionState* MotionState = CandidateMotionStates.Find(ResidentID);
		if (Entry == nullptr || MotionState == nullptr)
		{
			OutError = TEXT("A low-level proxy slot could not resolve its presentation entry and motion state.");
			return false;
		}
		ProxyEntries[SlotIndex] = **Entry;
		if (ProxyResidentIDs[SlotIndex] != ResidentID)
		{
			ProxyResidentIDs[SlotIndex] = ResidentID;
			UpdateProxyInstanceTransform(SlotIndex, **Entry, *MotionState, false);
			bAnyTransformChanged = true;
		}
	}
	if (bAnyTransformChanged)
	{
		ProxyBodies->MarkRenderStateDirty();
		ProxyHeads->MarkRenderStateDirty();
	}
	VisibleProxyCount = SlotPlan.VisibleCount;
	OutError.Reset();
	return true;
}

void AAILODVisualPopulationPresenter::UpdateProxyInstanceTransform(
	const int32 SlotIndex,
	const AILOD::FVisualResidentPresentationEntry& Entry,
	const AILOD::FVisualResidentMotionState& MotionState,
	const bool bMarkRenderStateDirty)
{
	const AILOD::FVisualResidentMotionPose Pose =
		AILOD::FVisualResidentPresentationPlanner::ResolveMotionPose(Entry, MotionState);
	const double RootZ = GroundZ + Pose.GroundOffset;
	const FTransform BodyTransform(
		FRotator(0.0, Pose.FacingDegrees, 0.0),
		FVector(
			Pose.Position.X,
			Pose.Position.Y,
			RootZ + AILOD::FVisualResidentFigureGeometry::BodyCenterZ * Pose.HeightScale),
		FVector(
			AILOD::FVisualResidentFigureGeometry::BodyRadiusScale,
			AILOD::FVisualResidentFigureGeometry::BodyRadiusScale,
			AILOD::FVisualResidentFigureGeometry::BodyHeightScale * Pose.HeightScale));
	const FTransform HeadTransform(
		FRotator(0.0, Pose.FacingDegrees, 0.0),
		FVector(
			Pose.Position.X,
			Pose.Position.Y,
			RootZ + AILOD::FVisualResidentFigureGeometry::HeadCenterZ * Pose.HeightScale),
		FVector(
			AILOD::FVisualResidentFigureGeometry::HeadScale,
			AILOD::FVisualResidentFigureGeometry::HeadScale,
			AILOD::FVisualResidentFigureGeometry::HeadScale * Pose.HeightScale));
	ProxyBodies->UpdateInstanceTransform(SlotIndex, BodyTransform, true, bMarkRenderStateDirty, true);
	ProxyHeads->UpdateInstanceTransform(SlotIndex, HeadTransform, true, bMarkRenderStateDirty, true);
	ProxyPositions[SlotIndex] = Pose.Position;
}

void AAILODVisualPopulationPresenter::HideProxySlot(
	const int32 SlotIndex,
	const bool bMarkRenderStateDirty)
{
	const FTransform HiddenTransform(
		FRotator::ZeroRotator,
		FVector(0.0, 0.0, GroundZ),
		FVector::ZeroVector);
	ProxyBodies->UpdateInstanceTransform(SlotIndex, HiddenTransform, true, bMarkRenderStateDirty, true);
	ProxyHeads->UpdateInstanceTransform(SlotIndex, HiddenTransform, true, bMarkRenderStateDirty, true);
	ProxyResidentIDs[SlotIndex] = 0;
	ProxyPositions[SlotIndex] = FVector2D::ZeroVector;
	ProxyEntries[SlotIndex] = {};
}

void AAILODVisualPopulationPresenter::HideAllProxySlots()
{
	for (int32 SlotIndex = 0; SlotIndex < ProxyResidentIDs.Num(); ++SlotIndex)
	{
		HideProxySlot(SlotIndex, false);
	}
	ProxyBodies->MarkRenderStateDirty();
	ProxyHeads->MarkRenderStateDirty();
}
