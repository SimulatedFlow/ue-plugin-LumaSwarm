// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LumaSwarmComponent.h"

#include "Components/LightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LumaSwarmProfile.h"
#include "LumaSwarmSubsystem.h"

ULumaSwarmComponent::ULumaSwarmComponent()
{
	// Never. The subsystem owns the update loop; this component is a registration slip, not a worker.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

void ULumaSwarmComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterLights();
}

void ULumaSwarmComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Always restore. A lamp that is left frozen halfway through a flicker is a bug that only shows up
	// after a level transition, when nobody remembers what turned it down.
	UnregisterLights(true);
	Super::EndPlay(EndPlayReason);
}

void ULumaSwarmComponent::CollectLights(TArray<ULightComponent*>& OutLights) const
{
	OutLights.Reset();

	if (ExplicitLights.Num() > 0)
	{
		for (const TObjectPtr<ULightComponent>& Light : ExplicitLights)
		{
			if (IsValid(Light))
			{
				OutLights.Add(Light);
			}
		}
		return;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<ULightComponent*> Found;
	Owner->GetComponents<ULightComponent>(Found);

	if (Found.Num() == 0)
	{
		return;
	}

	if (bAffectAllLightsOnActor)
	{
		OutLights = MoveTemp(Found);
	}
	else
	{
		OutLights.Add(Found[0]);
	}
}

void ULumaSwarmComponent::RegisterLights()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ULumaSwarmSubsystem* Subsystem = World->GetSubsystem<ULumaSwarmSubsystem>();
	if (!Subsystem)
	{
		return;
	}

	TArray<ULightComponent*> Lights;
	CollectLights(Lights);

	ManagedLights.Reset(Lights.Num());

	for (ULightComponent* Light : Lights)
	{
		const float Phase = (PhaseOffset < 0.0f)
			? ULumaSwarmSubsystem::DerivePhase(Light, Light->GetComponentLocation())
			: PhaseOffset;

		Subsystem->RegisterLight(Light, Profile, Phase, ImportanceBias);
		ManagedLights.Add(Light);
	}
}

void ULumaSwarmComponent::UnregisterLights(bool bRestoreBaseValues)
{
	if (UWorld* World = GetWorld())
	{
		if (ULumaSwarmSubsystem* Subsystem = World->GetSubsystem<ULumaSwarmSubsystem>())
		{
			for (const TObjectPtr<ULightComponent>& Light : ManagedLights)
			{
				if (IsValid(Light))
				{
					Subsystem->UnregisterLight(Light, bRestoreBaseValues);
				}
			}
		}
	}

	ManagedLights.Reset();
}

void ULumaSwarmComponent::SetProfile(ULumaSwarmProfile* NewProfile)
{
	Profile = NewProfile;

	if (ManagedLights.Num() > 0)
	{
		// Re-registering keeps each light's base values and phase, it only retargets the recipe.
		RegisterLights();
	}
}
