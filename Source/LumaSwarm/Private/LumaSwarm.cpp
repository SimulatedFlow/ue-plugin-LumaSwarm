// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LumaSwarm.h"
#include "LumaSwarmLog.h"

DEFINE_LOG_CATEGORY(LogLumaSwarm);

#define LOCTEXT_NAMESPACE "FLumaSwarmModule"

void FLumaSwarmModule::StartupModule()
{
	UE_LOG(LogLumaSwarm, Log, TEXT("LumaSwarm started."));
}

void FLumaSwarmModule::ShutdownModule()
{
	UE_LOG(LogLumaSwarm, Log, TEXT("LumaSwarm shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLumaSwarmModule, LumaSwarm)
