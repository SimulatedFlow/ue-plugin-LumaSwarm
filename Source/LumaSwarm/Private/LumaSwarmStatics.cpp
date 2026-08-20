// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LumaSwarmStatics.h"

#include "Components/LightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "LumaSwarmSubsystem.h"

ULumaSwarmSubsystem* ULumaSwarmStatics::GetLumaSwarm(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<ULumaSwarmSubsystem>() : nullptr;
}

bool ULumaSwarmStatics::RegisterLightWithProfile(
	const UObject* WorldContextObject,
	ULightComponent* Light,
	ULumaSwarmProfile* Profile,
	float PhaseOffset,
	float ImportanceBias)
{
	ULumaSwarmSubsystem* Subsystem = GetLumaSwarm(WorldContextObject);
	if (!Subsystem || !IsValid(Light))
	{
		return false;
	}

	const float Phase = (PhaseOffset < 0.0f)
		? ULumaSwarmSubsystem::DerivePhase(Light, Light->GetComponentLocation())
		: PhaseOffset;

	Subsystem->RegisterLight(Light, Profile, Phase, ImportanceBias);
	return true;
}

bool ULumaSwarmStatics::UnregisterLightFromSwarm(const UObject* WorldContextObject, ULightComponent* Light)
{
	ULumaSwarmSubsystem* Subsystem = GetLumaSwarm(WorldContextObject);
	if (!Subsystem || !Light)
	{
		return false;
	}

	Subsystem->UnregisterLight(Light, true);
	return true;
}

FLumaSwarmStats ULumaSwarmStatics::GetLumaSwarmStats(const UObject* WorldContextObject)
{
	if (const ULumaSwarmSubsystem* Subsystem = GetLumaSwarm(WorldContextObject))
	{
		return Subsystem->GetStats();
	}

	return FLumaSwarmStats();
}
