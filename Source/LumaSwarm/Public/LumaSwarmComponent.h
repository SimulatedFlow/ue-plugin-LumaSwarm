// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LumaSwarmComponent.generated.h"

class ULightComponent;
class ULumaSwarmProfile;

/**
 * Drop this on any actor that owns lights and they join the swarm.
 *
 * Note what this component does *not* do: it does not tick. PrimaryComponentTick.bCanEverTick is false and
 * stays false. All it does is hand its owner's lights to the world subsystem at BeginPlay and take them
 * back at EndPlay; the animation itself is the scheduler's job. That is the entire value proposition of the
 * plugin, so it has to be literally true rather than merely nearly true — a hundred of these on a level
 * cost zero tick registrations.
 */
UCLASS(ClassGroup = (LumaSwarm), meta = (BlueprintSpawnableComponent, DisplayName = "LumaSwarm Component"))
class LUMASWARM_API ULumaSwarmComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULumaSwarmComponent();

	/** Animation recipe applied to every light this component manages. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm")
	TObjectPtr<ULumaSwarmProfile> Profile;

	/**
	 * Offset into the animation period, in periods (0 to 1).
	 * Leave it at -1 to derive a stable phase from the light's name and world position — the same level then
	 * flickers identically on every run and on every machine, which is what makes screenshots reproducible.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float PhaseOffset = -1.0f;

	/**
	 * Pulls this light towards the camera for scheduling purposes only. 0 is neutral, 1 makes it behave as
	 * if it were half as far away. Use it on the handful of lamps the player is meant to look at.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float ImportanceBias = 0.0f;

	/** Manage every light component on the owning actor. With this off, only the first one found is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm")
	bool bAffectAllLightsOnActor = true;

	/**
	 * Explicit list of lights to manage. When this is non-empty it wins over the automatic search, which is
	 * how you animate three of an actor's five lamps and leave the rest alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm")
	TArray<TObjectPtr<ULightComponent>> ExplicitLights;

	/** Hand the lights to the subsystem. Called automatically at BeginPlay. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void RegisterLights();

	/** Take the lights back and restore the values they had when they registered. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void UnregisterLights(bool bRestoreBaseValues = true);

	/** Swap the profile at runtime. Re-registers the managed lights so the change takes effect immediately. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void SetProfile(ULumaSwarmProfile* NewProfile);

	/** The lights this component handed to the subsystem. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	int32 GetNumManagedLights() const { return ManagedLights.Num(); }

protected:
	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Work out which lights this component is responsible for. */
	void CollectLights(TArray<ULightComponent*>& OutLights) const;

	/** Lights currently registered by this component, so EndPlay gives back exactly what it took. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULightComponent>> ManagedLights;
};
