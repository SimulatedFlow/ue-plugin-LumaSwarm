// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/** LumaSwarm — budgeted runtime scheduler for large numbers of animated dynamic lights (runtime module). */
class FLumaSwarmModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
