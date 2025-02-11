// Copyright (c) 2025 Jared Taylor

#pragma once

#include "Modules/ModuleManager.h"

class FActorTurnInPlaceModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
