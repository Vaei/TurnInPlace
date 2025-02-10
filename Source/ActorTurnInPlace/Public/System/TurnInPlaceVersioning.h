// Copyright (c) 2023 Drowning Dragons. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Misc/EngineVersionComparison.h"

#ifndef UE_5_03_OR_LATER
#if !UE_VERSION_OLDER_THAN(5, 3, 0)
#define UE_5_03_OR_LATER 1
#else
#define UE_5_03_OR_LATER 0
#endif
#endif
