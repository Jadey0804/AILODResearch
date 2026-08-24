// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualLayoutPCGSettings.h"

#include "Data/PCGPointData.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "../AILODVisualDemoSettings.h"
#include "../../Simulation/AILODPhase0Manifest.h"

const FName UPCGAILODVisualLayoutSettings::RoadSegmentsPin(TEXT("Road Segments"));
const FName UPCGAILODVisualLayoutSettings::HomeSlotsPin(TEXT("Home Slots"));
const FName UPCGAILODVisualLayoutSettings::WorkAnchorsPin(TEXT("Work Anchors"));
const FName UPCGAILODVisualLayoutSettings::TreePointsPin(TEXT("Tree Points"));

namespace
{
	UPCGPointData* AddPointOutput(FPCGContext* Context, const FName Pin)
	{
		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Pin = Pin;
		UPCGPointData* PointData = NewObject<UPCGPointData>();
		TaggedData.Data = PointData;
		return PointData;
	}

	int32 StablePointSeed(const int32 LayoutSeed, const int64 StableID)
	{
		const uint32 Hash = HashCombine(GetTypeHash(LayoutSeed), GetTypeHash(StableID));
		return static_cast<int32>(Hash == 0 ? 1 : Hash);
	}

	bool IsInsideGenerationCell(const FBox& GenerationBounds, const FVector& Position)
	{
		return !GenerationBounds.IsValid || GenerationBounds.IsInsideOrOn(Position);
	}
}

TArray<FPCGPinProperties> UPCGAILODVisualLayoutSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UPCGAILODVisualLayoutSettings::OutputPinProperties() const
{
	return {
		FPCGPinProperties(RoadSegmentsPin, EPCGDataType::Point, false, false),
		FPCGPinProperties(HomeSlotsPin, EPCGDataType::Point, false, false),
		FPCGPinProperties(WorkAnchorsPin, EPCGDataType::Point, false, false),
		FPCGPinProperties(TreePointsPin, EPCGDataType::Point, false, false)
	};
}

FPCGElementPtr UPCGAILODVisualLayoutSettings::CreateElement() const
{
	return MakeShared<FAILODVisualLayoutPCGElement>();
}

bool FAILODVisualLayoutPCGElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UAILODVisualDemoSettings* Settings = GetDefault<UAILODVisualDemoSettings>();
	const AILOD::FVisualDemoRuntimeConfig RuntimeConfig = Settings->MakeRuntimeConfig();
	AILOD::FPhase0Config SimulationConfig;
	SimulationConfig.Seed = RuntimeConfig.SimulationSeed;
	SimulationConfig.PopulationPerKingdom = RuntimeConfig.PopulationPerKingdom;

	AILOD::FInitialPopulationManifest Population;
	AILOD::FEarthquakeDamageList Damage;
	AILOD::FPersistentTestPool PersistentPool;
	FString Error;
	if (!AILOD::FPhase0ManifestGenerator::Generate(
		SimulationConfig, Population, Damage, PersistentPool, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("AILOD PCG population generation failed: %s"), *Error);
		return true;
	}
	AILOD::FVisualWorldLayout Layout;
	if (!Layout.Build(Population, RuntimeConfig.Layout, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("AILOD PCG visual layout generation failed: %s"), *Error);
		return true;
	}
	const FBox GenerationBounds = Context->SourceComponent.IsValid()
		? Context->SourceComponent->GetGridBounds()
		: FBox(EForceInit::ForceInit);

	TArray<FPCGPoint>& RoadPoints = AddPointOutput(Context, UPCGAILODVisualLayoutSettings::RoadSegmentsPin)->GetMutablePoints();
	RoadPoints.Reserve(Layout.GetRoads().Num());
	for (const AILOD::FVisualRoadRecord& Road : Layout.GetRoads())
	{
		const FVector2D Direction = Road.End - Road.Start;
		const double Length = Direction.Size();
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		const FVector Position((Road.Start.X + Road.End.X) * 0.5, (Road.Start.Y + Road.End.Y) * 0.5, -10.0);
		if (!IsInsideGenerationCell(GenerationBounds, Position))
		{
			continue;
		}
		FPCGPoint& Point = RoadPoints.Emplace_GetRef();
		Point.Transform = FTransform(
			FRotator(0.0, Yaw, 0.0),
			Position,
			FVector(Length / 100.0, Settings->RoadWidthMeters, 0.2));
		Point.Seed = StablePointSeed(RuntimeConfig.Layout.LayoutSeed, Road.RoadID);
	}

	TArray<FPCGPoint>& HomePoints = AddPointOutput(Context, UPCGAILODVisualLayoutSettings::HomeSlotsPin)->GetMutablePoints();
	HomePoints.Reserve(Layout.GetHomeSlots().Num());
	for (const AILOD::FVisualHomeSlotRecord& Home : Layout.GetHomeSlots())
	{
		const FVector Position(Home.Position.X, Home.Position.Y, 0.0);
		if (!IsInsideGenerationCell(GenerationBounds, Position))
		{
			continue;
		}
		FPCGPoint& Point = HomePoints.Emplace_GetRef();
		Point.Transform = FTransform(
			FRotator(0.0, Home.FacingDegrees, 0.0),
			Position);
		Point.Seed = StablePointSeed(RuntimeConfig.Layout.LayoutSeed, Home.VisualHomeSlotID);
	}

	TArray<FPCGPoint>& WorkPoints = AddPointOutput(Context, UPCGAILODVisualLayoutSettings::WorkAnchorsPin)->GetMutablePoints();
	WorkPoints.Reserve(Layout.GetWorkAnchors().Num());
	for (const AILOD::FVisualWorkAnchorRecord& Work : Layout.GetWorkAnchors())
	{
		const FVector Position(Work.Position.X, Work.Position.Y, 0.0);
		if (!IsInsideGenerationCell(GenerationBounds, Position))
		{
			continue;
		}
		FPCGPoint& Point = WorkPoints.Emplace_GetRef();
		Point.Transform = FTransform(Position);
		Point.Seed = StablePointSeed(RuntimeConfig.Layout.LayoutSeed, Work.WorkAnchorID);
	}

	TArray<FPCGPoint>& TreePoints = AddPointOutput(Context, UPCGAILODVisualLayoutSettings::TreePointsPin)->GetMutablePoints();
	TreePoints.Reserve(Layout.GetDistricts().Num() * Settings->TreesPerDistrict);
	const int32 TreeColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<double>(Settings->TreesPerDistrict))));
	for (const AILOD::FVisualDistrictRecord& District : Layout.GetDistricts())
	{
		FRandomStream Stream(RuntimeConfig.Layout.LayoutSeed + District.DistrictID * 7919);
		const FVector2D ForestMin = FMath::Lerp(District.Bounds.Min, District.Bounds.Max, 0.05);
		const FVector2D ForestMax = FMath::Lerp(District.Bounds.Min, District.Bounds.Max, 0.28);
		const int32 TreeRows = FMath::Max(1, FMath::DivideAndRoundUp(Settings->TreesPerDistrict, TreeColumns));
		for (int32 TreeIndex = 0; TreeIndex < Settings->TreesPerDistrict; ++TreeIndex)
		{
			const int32 Column = TreeIndex % TreeColumns;
			const int32 Row = TreeIndex / TreeColumns;
			const double AlphaX = (Column + 0.5 + Stream.FRandRange(-0.25f, 0.25f)) / TreeColumns;
			const double AlphaY = (Row + 0.5 + Stream.FRandRange(-0.25f, 0.25f)) / TreeRows;
			const FVector2D Position(
				FMath::Lerp(ForestMin.X, ForestMax.X, AlphaX),
				FMath::Lerp(ForestMin.Y, ForestMax.Y, AlphaY));
			const FVector WorldPosition(Position.X, Position.Y, 0.0);
			if (!IsInsideGenerationCell(GenerationBounds, WorldPosition))
			{
				continue;
			}
			const float UniformScale = Stream.FRandRange(0.8f, 1.2f);
			FPCGPoint& Point = TreePoints.Emplace_GetRef();
			Point.Transform = FTransform(
				FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f),
				WorldPosition,
				FVector(UniformScale));
			Point.Seed = StablePointSeed(
				RuntimeConfig.Layout.LayoutSeed,
				static_cast<int64>(District.DistrictID) * 100000 + TreeIndex + 1);
		}
	}
	return true;
}
