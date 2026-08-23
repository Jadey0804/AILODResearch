// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../Simulation/AILODPhase0Types.h"

namespace AILOD
{
	using FVisualDistrictID = int32;
	using FVisualHomeSlotID = int64;
	using FVisualWorkAnchorID = int64;
	using FVisualRoadID = int64;

	enum class EVisualWorkAnchorType : uint8
	{
		LumberCamp,
		TimberPurchase,
		Market
	};

	struct FVisualWorldLayoutConfig
	{
		FString LayoutVersion = TEXT("phase7b-layout-v1");
		int32 LayoutSeed = 20260823;
		int32 ResidentsPerDistrict = 2500;
		int32 HomeSlotsPerDistrict = 64;
		double DistrictSize = 100000.0;
		double KingdomGap = 200000.0;
		double SpatialCellSize = 5000.0;
	};

	struct FVisualBounds2D
	{
		FVector2D Min = FVector2D::ZeroVector;
		FVector2D Max = FVector2D::ZeroVector;
	};

	struct FVisualDistrictRecord
	{
		FVisualDistrictID DistrictID = 0;
		EKingdom Kingdom = EKingdom::A;
		FVisualBounds2D Bounds;
	};

	struct FVisualRoadRecord
	{
		FVisualRoadID RoadID = 0;
		FVisualDistrictID DistrictID = 0;
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
	};

	struct FVisualHomeSlotRecord
	{
		FVisualHomeSlotID VisualHomeSlotID = 0;
		FVisualDistrictID DistrictID = 0;
		FVisualRoadID RoadID = 0;
		FVector2D Position = FVector2D::ZeroVector;
		double FacingDegrees = 0.0;
	};

	struct FVisualWorkAnchorRecord
	{
		FVisualWorkAnchorID WorkAnchorID = 0;
		FVisualDistrictID DistrictID = 0;
		FVisualRoadID RoadID = 0;
		EVisualWorkAnchorType Type = EVisualWorkAnchorType::Market;
		FVector2D Position = FVector2D::ZeroVector;
	};

	struct FVisualResidentPlacement
	{
		FResidentID ResidentID = 0;
		FHomeID HomeID = 0;
		FVisualDistrictID DistrictID = 0;
		FVisualHomeSlotID VisualHomeSlotID = 0;
		FVisualWorkAnchorID WorkAnchorID = 0;
		FVisualRoadID ProxyRoadID = 0;
		FVector2D ProxyPosition = FVector2D::ZeroVector;
		FIntPoint SpatialCell = FIntPoint::ZeroValue;
	};

	struct FVisualConeQuery
	{
		enum class EResultOrder : uint8
		{
			Distance,
			CenterAlignment
		};

		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D Forward = FVector2D(1.0, 0.0);
		double MaxDistance = 20000.0;
		double HalfAngleDegrees = 60.0;
		int32 MaxResults = 512;
		EResultOrder ResultOrder = EResultOrder::Distance;
	};

	struct FVisualSpatialCandidate
	{
		FResidentID ResidentID = 0;
		FVector2D Position = FVector2D::ZeroVector;
		double Distance = 0.0;
		double ForwardAlignment = -1.0;
	};

	struct FVisualSpatialQueryDiagnostics
	{
		int32 VisitedCellCount = 0;
		int32 VisitedResidentEntryCount = 0;
		int32 MatchingResidentCount = 0;
		int32 ReturnedCandidateCount = 0;
		int32 CatalogResidentCount = 0;
		bool bResultTruncated = false;
		bool bScannedResidentCatalog = false;
	};

	class FVisualWorldLayout
	{
	public:
		bool Build(
			const FInitialPopulationManifest& Population,
			const FVisualWorldLayoutConfig& Config,
			FString& OutError);

		bool QueryCone(
			const FVisualConeQuery& Query,
			TArray<FVisualSpatialCandidate>& OutCandidates,
			FVisualSpatialQueryDiagnostics& OutDiagnostics,
			FString& OutError) const;

		const FVisualResidentPlacement* FindResident(FResidentID ResidentID) const;
		bool FindVisualHomeSlotForHome(FHomeID HomeID, FVisualHomeSlotID& OutVisualHomeSlotID) const;
		const FVisualRoadRecord* FindRoad(FVisualRoadID RoadID) const;
		const FVisualHomeSlotRecord* FindHomeSlot(FVisualHomeSlotID VisualHomeSlotID) const;
		const FVisualWorkAnchorRecord* FindWorkAnchor(FVisualWorkAnchorID WorkAnchorID) const;

		bool IsBuilt() const { return bBuilt; }
		const FVisualWorldLayoutConfig& GetConfig() const { return LayoutConfig; }
		int32 GetSimulationSeed() const { return SimulationSeed; }
		int32 GetPopulationPerKingdom() const { return PopulationPerKingdom; }
		const FString& GetPopulationConfigHash() const { return PopulationConfigHash; }
		const TArray<FVisualDistrictRecord>& GetDistricts() const { return Districts; }
		const TArray<FVisualRoadRecord>& GetRoads() const { return Roads; }
		const TArray<FVisualHomeSlotRecord>& GetHomeSlots() const { return HomeSlots; }
		const TArray<FVisualWorkAnchorRecord>& GetWorkAnchors() const { return WorkAnchors; }
		const TArray<FVisualResidentPlacement>& GetResidents() const { return Residents; }
		FString BuildDeterministicDigest() const;

	private:
		FIntPoint ToCell(const FVector2D& Position) const;

		FVisualWorldLayoutConfig LayoutConfig;
		TArray<FVisualDistrictRecord> Districts;
		TArray<FVisualRoadRecord> Roads;
		TArray<FVisualHomeSlotRecord> HomeSlots;
		TArray<FVisualWorkAnchorRecord> WorkAnchors;
		TArray<FVisualResidentPlacement> Residents;
		TMap<FResidentID, int32> ResidentIndex;
		TMap<FHomeID, FVisualHomeSlotID> HomeToVisualSlot;
		TMap<FVisualHomeSlotID, int32> HomeSlotIndex;
		TMap<FVisualWorkAnchorID, int32> WorkAnchorIndex;
		TMap<FIntPoint, TArray<int32>> SpatialGrid;
		int32 SimulationSeed = 0;
		int32 PopulationPerKingdom = 0;
		FString PopulationConfigHash;
		bool bBuilt = false;
	};
}
