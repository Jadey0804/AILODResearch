// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "AILODVisualLayoutPCGSettings.generated.h"

UCLASS(BlueprintType, ClassGroup=(Procedural))
class AILODRESEARCH_API UPCGAILODVisualLayoutSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("AILODVisualLayout"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("AILODVisualLayoutPCG", "NodeTitle", "AILOD Visual Layout"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("AILODVisualLayoutPCG", "NodeTooltip", "Outputs deterministic roads, homes, work anchors, and tree points from the authoritative AILOD visual layout configuration."); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

	static const FName RoadSegmentsPin;
	static const FName HomeSlotsPin;
	static const FName WorkAnchorsPin;
	static const FName TreePointsPin;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class FAILODVisualLayoutPCGElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
};
