// Copyright (c) Victor Rivas Perez. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Module-wide log category.
 *
 * Grass generation walks every landscape component and writes assets to disk, so it is worth
 * being able to raise its verbosity on its own ("Log LogGrassPlugin Verbose") without turning
 * the rest of the engine's output into noise. That is not possible through LogTemp.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGrassPlugin, Log, All);
