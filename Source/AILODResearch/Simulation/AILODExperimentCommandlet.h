// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AILODExperimentCommandlet.generated.h"

UCLASS()
class AILODRESEARCH_API UAILODExperimentCommandlet final : public UCommandlet
{
	GENERATED_BODY()

public:
	UAILODExperimentCommandlet();
	virtual int32 Main(const FString& Params) override;
};
