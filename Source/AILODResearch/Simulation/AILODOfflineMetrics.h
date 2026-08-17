// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace AILOD
{
	class FOfflineMetricsEvaluator
	{
	public:
		static bool BuildSummary(
			const FString& ExperimentRoot,
			const FString& OutputPath,
			FString& OutError);
	};
}
