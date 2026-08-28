// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualWorldLayout.h"

namespace AILOD
{
	namespace
	{
		uint64 Mix64(uint64 Value)
		{
			Value += 0x9E3779B97F4A7C15ull;
			Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
			Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
			return Value ^ (Value >> 31);
		}

		uint64 ResidentHash(const int32 Seed, const FResidentID ResidentID, const uint64 Salt)
		{
			return Mix64(static_cast<uint64>(static_cast<uint32>(Seed))
				^ Mix64(static_cast<uint64>(ResidentID)) ^ Salt);
		}

		double UnitValue(const uint64 Value)
		{
			return static_cast<double>(Value & 0xFFFFFFull) / 16777216.0;
		}

		void HashBytes(uint64& Hash, const void* Data, const int32 NumBytes)
		{
			const uint8* Bytes = static_cast<const uint8*>(Data);
			for (int32 Index = 0; Index < NumBytes; ++Index)
			{
				Hash ^= Bytes[Index];
				Hash *= 1099511628211ull;
			}
		}

		template <typename T>
		void HashValue(uint64& Hash, const T& Value)
		{
			HashBytes(Hash, &Value, sizeof(T));
		}

		FVector2D RotateDirection(const FVector2D& Forward, const double Degrees)
		{
			const double Radians = FMath::DegreesToRadians(Degrees);
			const double Cos = FMath::Cos(Radians);
			const double Sin = FMath::Sin(Radians);
			return FVector2D(
				Forward.X * Cos - Forward.Y * Sin,
				Forward.X * Sin + Forward.Y * Cos);
		}
	}

	FIntPoint FVisualWorldLayout::ToCell(const FVector2D& Position) const
	{
		return FIntPoint(
			FMath::FloorToInt(Position.X / LayoutConfig.SpatialCellSize),
			FMath::FloorToInt(Position.Y / LayoutConfig.SpatialCellSize));
	}

	bool FVisualWorldLayout::Build(
		const FInitialPopulationManifest& Population,
		const FVisualWorldLayoutConfig& Config,
		FString& OutError)
	{
		LayoutConfig = {};
		Districts.Reset();
		Roads.Reset();
		HomeSlots.Reset();
		WorkAnchors.Reset();
		Residents.Reset();
		ResidentIndex.Reset();
		HomeToVisualSlot.Reset();
		HomeSlotIndex.Reset();
		WorkAnchorIndex.Reset();
		SpatialGrid.Reset();
		SimulationSeed = 0;
		PopulationPerKingdom = 0;
		PopulationConfigHash.Reset();
		bBuilt = false;

		if (Population.Residents.IsEmpty()
			|| Population.Seed == 0
			|| Population.PopulationPerKingdom <= 0
			|| Population.Residents.Num() != Population.PopulationPerKingdom * 2
			|| Population.ConfigHash.IsEmpty()
			|| Config.LayoutVersion.IsEmpty()
			|| Config.LayoutSeed == 0
			|| Config.ResidentsPerDistrict <= 0
			|| Config.HomeSlotsPerDistrict <= 0
			|| Config.SpatialCellSize <= 0.0
			|| Config.DistrictSize <= Config.SpatialCellSize * 2.0
			|| Config.KingdomGap < 0.0)
		{
			OutError = TEXT("Visual World Layout requires a population and positive versioned layout dimensions, seed, and budgets.");
			return false;
		}

		TArray<const FInitialResidentRecord*> SortedResidents;
		SortedResidents.Reserve(Population.Residents.Num());
		int32 KingdomCounts[2] = {};
		TSet<FResidentID> SeenResidentIDs;
		TSet<FHomeID> SeenHomeIDs;
		for (const FInitialResidentRecord& Resident : Population.Residents)
		{
			if (Resident.ResidentID <= 0
				|| Resident.HomeID <= 0
				|| SeenResidentIDs.Contains(Resident.ResidentID)
				|| SeenHomeIDs.Contains(Resident.HomeID))
			{
				OutError = TEXT("Visual World Layout requires unique positive ResidentIDs and HomeIDs.");
				return false;
			}
			SeenResidentIDs.Add(Resident.ResidentID);
			SeenHomeIDs.Add(Resident.HomeID);
			SortedResidents.Add(&Resident);
			++KingdomCounts[Resident.Kingdom == EKingdom::A ? 0 : 1];
		}
		if (KingdomCounts[0] == 0 || KingdomCounts[1] == 0)
		{
			OutError = TEXT("Visual World Layout requires residents in both kingdoms.");
			return false;
		}
		SortedResidents.Sort([](const FInitialResidentRecord& Left, const FInitialResidentRecord& Right)
		{
			return Left.ResidentID < Right.ResidentID;
		});

		LayoutConfig = Config;
		SimulationSeed = Population.Seed;
		PopulationPerKingdom = Population.PopulationPerKingdom;
		PopulationConfigHash = Population.ConfigHash;
		int32 DistrictCounts[2] = {};
		int32 DistrictColumns[2] = {};
		int32 DistrictRows[2] = {};
		TArray<int32> KingdomDistrictIndices[2];
		for (int32 KingdomIndex = 0; KingdomIndex < 2; ++KingdomIndex)
		{
			DistrictCounts[KingdomIndex] = FMath::DivideAndRoundUp(
				KingdomCounts[KingdomIndex], LayoutConfig.ResidentsPerDistrict);
			DistrictColumns[KingdomIndex] = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(DistrictCounts[KingdomIndex])));
			DistrictRows[KingdomIndex] = FMath::DivideAndRoundUp(
				DistrictCounts[KingdomIndex], DistrictColumns[KingdomIndex]);
		}

		for (int32 KingdomIndex = 0; KingdomIndex < 2; ++KingdomIndex)
		{
			const EKingdom Kingdom = KingdomIndex == 0 ? EKingdom::A : EKingdom::B;
			const double KingdomWidth = DistrictColumns[KingdomIndex] * LayoutConfig.DistrictSize;
			const double StartX = KingdomIndex == 0
				? -LayoutConfig.KingdomGap * 0.5 - KingdomWidth
				: LayoutConfig.KingdomGap * 0.5;
			const double StartY = -DistrictRows[KingdomIndex] * LayoutConfig.DistrictSize * 0.5;
			for (int32 LocalDistrict = 0; LocalDistrict < DistrictCounts[KingdomIndex]; ++LocalDistrict)
			{
				const int32 Column = LocalDistrict % DistrictColumns[KingdomIndex];
				const int32 Row = LocalDistrict / DistrictColumns[KingdomIndex];
				FVisualDistrictRecord& District = Districts.AddDefaulted_GetRef();
				District.DistrictID = Districts.Num();
				District.Kingdom = Kingdom;
				District.Bounds.Min = FVector2D(
					StartX + Column * LayoutConfig.DistrictSize,
					StartY + Row * LayoutConfig.DistrictSize);
				District.Bounds.Max = District.Bounds.Min + FVector2D(LayoutConfig.DistrictSize, LayoutConfig.DistrictSize);
				KingdomDistrictIndices[KingdomIndex].Add(Districts.Num() - 1);

				const double Third = LayoutConfig.DistrictSize / 3.0;
				for (int32 Lane = 1; Lane <= 2; ++Lane)
				{
					FVisualRoadRecord& Horizontal = Roads.AddDefaulted_GetRef();
					Horizontal.RoadID = Roads.Num();
					Horizontal.DistrictID = District.DistrictID;
					Horizontal.Start = FVector2D(District.Bounds.Min.X, District.Bounds.Min.Y + Third * Lane);
					Horizontal.End = FVector2D(District.Bounds.Max.X, Horizontal.Start.Y);
					FVisualRoadRecord& Vertical = Roads.AddDefaulted_GetRef();
					Vertical.RoadID = Roads.Num();
					Vertical.DistrictID = District.DistrictID;
					Vertical.Start = FVector2D(District.Bounds.Min.X + Third * Lane, District.Bounds.Min.Y);
					Vertical.End = FVector2D(Vertical.Start.X, District.Bounds.Max.Y);
				}

				const int32 FirstDistrictRoadIndex = Roads.Num() - 4;
				for (int32 LocalSlot = 0; LocalSlot < LayoutConfig.HomeSlotsPerDistrict; ++LocalSlot)
				{
					const int32 RoadIndex = LocalSlot % 4;
					const int32 SlotAlongRoad = LocalSlot / 4;
					const int32 SlotsOnRoad = FMath::DivideAndRoundUp(
						LayoutConfig.HomeSlotsPerDistrict - RoadIndex, 4);
					const FVisualRoadRecord& Road = Roads[FirstDistrictRoadIndex + RoadIndex];
					const double Alpha = static_cast<double>(SlotAlongRoad + 1) / (SlotsOnRoad + 1);
					const FVector2D RoadDirection = (Road.End - Road.Start).GetSafeNormal();
					const FVector2D SidewalkNormal(-RoadDirection.Y, RoadDirection.X);
					const double SideSign = LocalSlot % 8 < 4 ? -1.0 : 1.0;
					FVisualHomeSlotRecord& Slot = HomeSlots.AddDefaulted_GetRef();
					Slot.VisualHomeSlotID = HomeSlots.Num();
					Slot.DistrictID = District.DistrictID;
					Slot.RoadID = Road.RoadID;
					Slot.Position = FMath::Lerp(Road.Start, Road.End, Alpha)
						+ SidewalkNormal * SideSign * 1200.0;
					const FVector2D FacingToRoad = SidewalkNormal * -SideSign;
					Slot.FacingDegrees = FMath::RadiansToDegrees(FMath::Atan2(
						FacingToRoad.Y, FacingToRoad.X));
					HomeSlotIndex.Add(Slot.VisualHomeSlotID, HomeSlots.Num() - 1);
				}

				const FVector2D AnchorFractions[3] =
				{
					FVector2D(1.0 / 3.0, 2.0 / 3.0),
					FVector2D(2.0 / 3.0, 2.0 / 3.0),
					FVector2D(2.0 / 3.0, 1.0 / 3.0)
				};
				for (int32 AnchorIndex = 0; AnchorIndex < 3; ++AnchorIndex)
				{
					FVisualWorkAnchorRecord& Anchor = WorkAnchors.AddDefaulted_GetRef();
					Anchor.WorkAnchorID = WorkAnchors.Num();
					Anchor.DistrictID = District.DistrictID;
					Anchor.RoadID = Roads[FirstDistrictRoadIndex + (AnchorIndex < 2 ? 2 : 0)].RoadID;
					Anchor.Type = AnchorIndex == 0
						? EVisualWorkAnchorType::LumberCamp
						: AnchorIndex == 1 ? EVisualWorkAnchorType::TimberPurchase : EVisualWorkAnchorType::Market;
					Anchor.Position = District.Bounds.Min + FVector2D(
						AnchorFractions[AnchorIndex].X * LayoutConfig.DistrictSize,
						AnchorFractions[AnchorIndex].Y * LayoutConfig.DistrictSize);
					WorkAnchorIndex.Add(Anchor.WorkAnchorID, WorkAnchors.Num() - 1);
				}
			}
		}

		Residents.Reserve(SortedResidents.Num());
		int32 KingdomOrdinals[2] = {};
		for (const FInitialResidentRecord* Resident : SortedResidents)
		{
			const int32 KingdomIndex = Resident->Kingdom == EKingdom::A ? 0 : 1;
			const TArray<int32>& AvailableDistricts = KingdomDistrictIndices[KingdomIndex];
			const int32 DistrictOffset = static_cast<int32>(ResidentHash(
				LayoutConfig.LayoutSeed, KingdomIndex + 1, 0xD157A1C7ull) % AvailableDistricts.Num());
			const int32 DistrictListIndex = (KingdomOrdinals[KingdomIndex]++ + DistrictOffset) % AvailableDistricts.Num();
			const FVisualDistrictRecord& District = Districts[AvailableDistricts[DistrictListIndex]];
			const int32 DistrictZeroIndex = District.DistrictID - 1;
			const int32 LocalHomeSlot = static_cast<int32>(ResidentHash(
				LayoutConfig.LayoutSeed, Resident->ResidentID, 0x484F4D45ull) % LayoutConfig.HomeSlotsPerDistrict);
			const FVisualHomeSlotRecord& HomeSlot = HomeSlots[
				DistrictZeroIndex * LayoutConfig.HomeSlotsPerDistrict + LocalHomeSlot];
			const int32 LocalWorkAnchor = Resident->Profession == EProfession::Logger
				? 0
				: 1 + static_cast<int32>(ResidentHash(
					LayoutConfig.LayoutSeed, Resident->ResidentID, 0x574F524Bull) % 2ull);
			const FVisualWorkAnchorRecord& WorkAnchor = WorkAnchors[DistrictZeroIndex * 3 + LocalWorkAnchor];

			const uint64 AxisHash = ResidentHash(LayoutConfig.LayoutSeed, Resident->ResidentID, 0x41584953ull);
			const bool bHorizontal = (AxisHash & 1ull) == 0;
			const int32 Lane = static_cast<int32>((AxisHash >> 1) & 1ull) + 1;
			const int32 LocalProxyRoadIndex = bHorizontal ? (Lane - 1) * 2 : (Lane - 1) * 2 + 1;
			const double RoadCoordinate = LayoutConfig.DistrictSize * Lane / 3.0;
			const double Along = 0.05 + UnitValue(ResidentHash(
				LayoutConfig.LayoutSeed, Resident->ResidentID, 0x504F5349ull)) * 0.90;
			const double SidewalkOffset = ((AxisHash >> 2) & 1ull) == 0 ? -300.0 : 300.0;
			const FVector2D LocalPosition = bHorizontal
				? FVector2D(Along * LayoutConfig.DistrictSize, RoadCoordinate + SidewalkOffset)
				: FVector2D(RoadCoordinate + SidewalkOffset, Along * LayoutConfig.DistrictSize);

			FVisualResidentPlacement& Placement = Residents.AddDefaulted_GetRef();
			Placement.ResidentID = Resident->ResidentID;
			Placement.HomeID = Resident->HomeID;
			Placement.DistrictID = District.DistrictID;
			Placement.VisualHomeSlotID = HomeSlot.VisualHomeSlotID;
			Placement.WorkAnchorID = WorkAnchor.WorkAnchorID;
			Placement.ProxyRoadID = DistrictZeroIndex * 4 + LocalProxyRoadIndex + 1;
			Placement.ProxyPosition = District.Bounds.Min + LocalPosition;
			Placement.SpatialCell = ToCell(Placement.ProxyPosition);
			ResidentIndex.Add(Placement.ResidentID, Residents.Num() - 1);
			HomeToVisualSlot.Add(Placement.HomeID, Placement.VisualHomeSlotID);
			SpatialGrid.FindOrAdd(Placement.SpatialCell).Add(Residents.Num() - 1);
		}

		bBuilt = true;
		OutError.Reset();
		return true;
	}

	bool FVisualWorldLayout::QueryCone(
		const FVisualConeQuery& Query,
		TArray<FVisualSpatialCandidate>& OutCandidates,
		FVisualSpatialQueryDiagnostics& OutDiagnostics,
		FString& OutError) const
	{
		OutCandidates.Reset();
		OutDiagnostics = {};
		OutDiagnostics.CatalogResidentCount = Residents.Num();
		if (!bBuilt
			|| Query.Forward.SizeSquared() <= UE_SMALL_NUMBER
			|| Query.MaxDistance <= 0.0
			|| Query.HalfAngleDegrees <= 0.0
			|| Query.HalfAngleDegrees > 90.0
			|| Query.MaxResults <= 0)
		{
			OutError = TEXT("Visual cone queries require a built layout, a direction, and positive distance, angle, and result budget.");
			return false;
		}

		const FVector2D Forward = Query.Forward.GetSafeNormal();
		const FVector2D FarCenter = Query.Origin + Forward * Query.MaxDistance;
		const FVector2D FarLeft = Query.Origin + RotateDirection(Forward, -Query.HalfAngleDegrees) * Query.MaxDistance;
		const FVector2D FarRight = Query.Origin + RotateDirection(Forward, Query.HalfAngleDegrees) * Query.MaxDistance;
		const FVector2D Min(
			FMath::Min(FMath::Min(Query.Origin.X, FarCenter.X), FMath::Min(FarLeft.X, FarRight.X)),
			FMath::Min(FMath::Min(Query.Origin.Y, FarCenter.Y), FMath::Min(FarLeft.Y, FarRight.Y)));
		const FVector2D Max(
			FMath::Max(FMath::Max(Query.Origin.X, FarCenter.X), FMath::Max(FarLeft.X, FarRight.X)),
			FMath::Max(FMath::Max(Query.Origin.Y, FarCenter.Y), FMath::Max(FarLeft.Y, FarRight.Y)));
		const FIntPoint MinCell = ToCell(Min);
		const FIntPoint MaxCell = ToCell(Max);
		const double MaxDistanceSquared = Query.MaxDistance * Query.MaxDistance;
		const double MinimumAlignment = FMath::Cos(FMath::DegreesToRadians(Query.HalfAngleDegrees));

		for (int32 CellX = MinCell.X; CellX <= MaxCell.X; ++CellX)
		{
			for (int32 CellY = MinCell.Y; CellY <= MaxCell.Y; ++CellY)
			{
				++OutDiagnostics.VisitedCellCount;
				const TArray<int32>* CellResidents = SpatialGrid.Find(FIntPoint(CellX, CellY));
				if (CellResidents == nullptr) continue;
				OutDiagnostics.VisitedResidentEntryCount += CellResidents->Num();
				for (const int32 ResidentArrayIndex : *CellResidents)
				{
					const FVisualResidentPlacement& Resident = Residents[ResidentArrayIndex];
					const FVector2D Delta = Resident.ProxyPosition - Query.Origin;
					const double DistanceSquared = Delta.SizeSquared();
					if (DistanceSquared > MaxDistanceSquared) continue;
					const double Distance = FMath::Sqrt(DistanceSquared);
					const double Alignment = Distance <= UE_SMALL_NUMBER
						? 1.0
						: FVector2D::DotProduct(Delta / Distance, Forward);
					if (Alignment < MinimumAlignment) continue;
					FVisualSpatialCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
					Candidate.ResidentID = Resident.ResidentID;
					Candidate.Position = Resident.ProxyPosition;
					Candidate.Distance = Distance;
					Candidate.ForwardAlignment = Alignment;
				}
			}
		}

		OutDiagnostics.MatchingResidentCount = OutCandidates.Num();
		OutCandidates.Sort([&Query](const FVisualSpatialCandidate& Left, const FVisualSpatialCandidate& Right)
		{
			if (Query.ResultOrder == FVisualConeQuery::EResultOrder::CenterAlignment
				&& !FMath::IsNearlyEqual(Left.ForwardAlignment, Right.ForwardAlignment))
			{
				return Left.ForwardAlignment > Right.ForwardAlignment;
			}
			return Left.Distance == Right.Distance
				? Left.ResidentID < Right.ResidentID
				: Left.Distance < Right.Distance;
		});
		if (OutCandidates.Num() > Query.MaxResults)
		{
			OutCandidates.SetNum(Query.MaxResults, EAllowShrinking::No);
			OutDiagnostics.bResultTruncated = true;
		}
		OutDiagnostics.ReturnedCandidateCount = OutCandidates.Num();
		OutError.Reset();
		return true;
	}

	bool FVisualWorldLayout::QueryRadius(
		const FVisualRadiusQuery& Query,
		TArray<FVisualSpatialCandidate>& OutCandidates,
		FVisualSpatialQueryDiagnostics& OutDiagnostics,
		FString& OutError) const
	{
		OutCandidates.Reset();
		OutDiagnostics = {};
		OutDiagnostics.CatalogResidentCount = Residents.Num();
		if (!bBuilt || Query.MaxDistance <= 0.0 || Query.MaxResults <= 0)
		{
			OutError = TEXT("Visual radius queries require a built layout and positive distance and result budget.");
			return false;
		}

		const FVector2D Extent(Query.MaxDistance, Query.MaxDistance);
		const FIntPoint MinCell = ToCell(Query.Origin - Extent);
		const FIntPoint MaxCell = ToCell(Query.Origin + Extent);
		const double MaxDistanceSquared = Query.MaxDistance * Query.MaxDistance;
		for (int32 CellX = MinCell.X; CellX <= MaxCell.X; ++CellX)
		{
			for (int32 CellY = MinCell.Y; CellY <= MaxCell.Y; ++CellY)
			{
				++OutDiagnostics.VisitedCellCount;
				const TArray<int32>* CellResidents = SpatialGrid.Find(FIntPoint(CellX, CellY));
				if (CellResidents == nullptr) continue;
				OutDiagnostics.VisitedResidentEntryCount += CellResidents->Num();
				for (const int32 ResidentArrayIndex : *CellResidents)
				{
					const FVisualResidentPlacement& Resident = Residents[ResidentArrayIndex];
					const double DistanceSquared = FVector2D::DistSquared(
						Resident.ProxyPosition,
						Query.Origin);
					if (DistanceSquared > MaxDistanceSquared) continue;
					FVisualSpatialCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
					Candidate.ResidentID = Resident.ResidentID;
					Candidate.Position = Resident.ProxyPosition;
					Candidate.Distance = FMath::Sqrt(DistanceSquared);
					Candidate.ForwardAlignment = 1.0;
				}
			}
		}

		OutDiagnostics.MatchingResidentCount = OutCandidates.Num();
		OutCandidates.Sort([](
			const FVisualSpatialCandidate& Left,
			const FVisualSpatialCandidate& Right)
		{
			return Left.Distance == Right.Distance
				? Left.ResidentID < Right.ResidentID
				: Left.Distance < Right.Distance;
		});
		if (OutCandidates.Num() > Query.MaxResults)
		{
			OutCandidates.SetNum(Query.MaxResults, EAllowShrinking::No);
			OutDiagnostics.bResultTruncated = true;
		}
		OutDiagnostics.ReturnedCandidateCount = OutCandidates.Num();
		OutError.Reset();
		return true;
	}

	const FVisualResidentPlacement* FVisualWorldLayout::FindResident(const FResidentID ResidentID) const
	{
		const int32* Index = ResidentIndex.Find(ResidentID);
		return Index != nullptr ? &Residents[*Index] : nullptr;
	}

	bool FVisualWorldLayout::FindVisualHomeSlotForHome(
		const FHomeID HomeID,
		FVisualHomeSlotID& OutVisualHomeSlotID) const
	{
		const FVisualHomeSlotID* VisualHomeSlotID = HomeToVisualSlot.Find(HomeID);
		if (VisualHomeSlotID == nullptr) return false;
		OutVisualHomeSlotID = *VisualHomeSlotID;
		return true;
	}

	const FVisualRoadRecord* FVisualWorldLayout::FindRoad(const FVisualRoadID RoadID) const
	{
		const int32 Index = static_cast<int32>(RoadID - 1);
		return Roads.IsValidIndex(Index) && Roads[Index].RoadID == RoadID ? &Roads[Index] : nullptr;
	}

	const FVisualHomeSlotRecord* FVisualWorldLayout::FindHomeSlot(const FVisualHomeSlotID VisualHomeSlotID) const
	{
		const int32* Index = HomeSlotIndex.Find(VisualHomeSlotID);
		return Index != nullptr ? &HomeSlots[*Index] : nullptr;
	}

	const FVisualWorkAnchorRecord* FVisualWorldLayout::FindWorkAnchor(const FVisualWorkAnchorID WorkAnchorID) const
	{
		const int32* Index = WorkAnchorIndex.Find(WorkAnchorID);
		return Index != nullptr ? &WorkAnchors[*Index] : nullptr;
	}

	const FVisualWorkAnchorRecord* FVisualWorldLayout::FindWorkAnchor(
		const FVisualDistrictID DistrictID,
		const EVisualWorkAnchorType Type) const
	{
		const int32 TypeOffset = Type == EVisualWorkAnchorType::LumberCamp
			? 0
			: Type == EVisualWorkAnchorType::TimberPurchase ? 1 : 2;
		const int32 Index = (DistrictID - 1) * 3 + TypeOffset;
		return WorkAnchors.IsValidIndex(Index)
			&& WorkAnchors[Index].DistrictID == DistrictID
			&& WorkAnchors[Index].Type == Type
			? &WorkAnchors[Index]
			: nullptr;
	}

	FString FVisualWorldLayout::BuildDeterministicDigest() const
	{
		uint64 Hash = 1469598103934665603ull;
		FTCHARToUTF8 VersionUtf8(*LayoutConfig.LayoutVersion);
		HashBytes(Hash, VersionUtf8.Get(), VersionUtf8.Length());
		FTCHARToUTF8 PopulationHashUtf8(*PopulationConfigHash);
		HashBytes(Hash, PopulationHashUtf8.Get(), PopulationHashUtf8.Length());
		HashValue(Hash, SimulationSeed);
		HashValue(Hash, PopulationPerKingdom);
		HashValue(Hash, LayoutConfig.LayoutSeed);
		HashValue(Hash, LayoutConfig.ResidentsPerDistrict);
		HashValue(Hash, LayoutConfig.HomeSlotsPerDistrict);
		const int64 DistrictSize = FMath::RoundToInt64(LayoutConfig.DistrictSize * 10.0);
		const int64 CellSize = FMath::RoundToInt64(LayoutConfig.SpatialCellSize * 10.0);
		HashValue(Hash, DistrictSize);
		HashValue(Hash, CellSize);
		for (const FVisualDistrictRecord& District : Districts)
		{
			HashValue(Hash, District.DistrictID);
			HashValue(Hash, District.Kingdom);
			const int64 MinX = FMath::RoundToInt64(District.Bounds.Min.X * 10.0);
			const int64 MinY = FMath::RoundToInt64(District.Bounds.Min.Y * 10.0);
			const int64 MaxX = FMath::RoundToInt64(District.Bounds.Max.X * 10.0);
			const int64 MaxY = FMath::RoundToInt64(District.Bounds.Max.Y * 10.0);
			HashValue(Hash, MinX);
			HashValue(Hash, MinY);
			HashValue(Hash, MaxX);
			HashValue(Hash, MaxY);
		}
		for (const FVisualRoadRecord& Road : Roads)
		{
			HashValue(Hash, Road.RoadID);
			HashValue(Hash, Road.DistrictID);
			const int64 StartX = FMath::RoundToInt64(Road.Start.X * 10.0);
			const int64 StartY = FMath::RoundToInt64(Road.Start.Y * 10.0);
			const int64 EndX = FMath::RoundToInt64(Road.End.X * 10.0);
			const int64 EndY = FMath::RoundToInt64(Road.End.Y * 10.0);
			HashValue(Hash, StartX);
			HashValue(Hash, StartY);
			HashValue(Hash, EndX);
			HashValue(Hash, EndY);
		}
		for (const FVisualHomeSlotRecord& Home : HomeSlots)
		{
			HashValue(Hash, Home.VisualHomeSlotID);
			HashValue(Hash, Home.DistrictID);
			HashValue(Hash, Home.RoadID);
			const int64 X = FMath::RoundToInt64(Home.Position.X * 10.0);
			const int64 Y = FMath::RoundToInt64(Home.Position.Y * 10.0);
			const int64 Facing = FMath::RoundToInt64(Home.FacingDegrees * 10.0);
			HashValue(Hash, X);
			HashValue(Hash, Y);
			HashValue(Hash, Facing);
		}
		for (const FVisualWorkAnchorRecord& Work : WorkAnchors)
		{
			HashValue(Hash, Work.WorkAnchorID);
			HashValue(Hash, Work.DistrictID);
			HashValue(Hash, Work.RoadID);
			HashValue(Hash, Work.Type);
			const int64 X = FMath::RoundToInt64(Work.Position.X * 10.0);
			const int64 Y = FMath::RoundToInt64(Work.Position.Y * 10.0);
			HashValue(Hash, X);
			HashValue(Hash, Y);
		}
		for (const FVisualResidentPlacement& Resident : Residents)
		{
			HashValue(Hash, Resident.ResidentID);
			HashValue(Hash, Resident.HomeID);
			HashValue(Hash, Resident.DistrictID);
			HashValue(Hash, Resident.VisualHomeSlotID);
			HashValue(Hash, Resident.WorkAnchorID);
			HashValue(Hash, Resident.ProxyRoadID);
			const int64 X = FMath::RoundToInt64(Resident.ProxyPosition.X * 10.0);
			const int64 Y = FMath::RoundToInt64(Resident.ProxyPosition.Y * 10.0);
			HashValue(Hash, X);
			HashValue(Hash, Y);
		}
		return FString::Printf(TEXT("%016llX"), Hash);
	}
}
