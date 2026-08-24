// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODVisualDemoGameMode.h"

#include "AILODVisualDemoCharacter.h"

AAILODVisualDemoGameMode::AAILODVisualDemoGameMode()
{
	DefaultPawnClass = AAILODVisualDemoCharacter::StaticClass();
}
