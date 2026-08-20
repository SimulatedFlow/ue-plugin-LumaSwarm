// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LumaSwarmTypes.h"
#include "LumaSwarmStatics.generated.h"

class ULightComponent;
class ULumaSwarmProfile;
class ULumaSwarmSubsystem;

/** Blueprint shortcuts to the swarm, so a level Blueprint never has to spell out the subsystem lookup. */
UCLASS()
class LUMASWARM_API ULumaSwarmStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The swarm scheduler for the given context's world, or null if the world has none. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm", meta = (WorldContext = "WorldContextObject", DisplayName = "Get LumaSwarm"))
	static ULumaSwarmSubsystem* GetLumaSwarm(const UObject* WorldContextObject);

	/**
	 * Add a single light to the swarm.
	 * A phase offset of -1 derives a stable phase from the light's name and position.
	 * Returns false when there is no swarm in this world or the light is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "3"))
	static bool RegisterLightWithProfile(
		const UObject* WorldContextObject,
		ULightComponent* Light,
		ULumaSwarmProfile* Profile,
		float PhaseOffset = -1.0f,
		float ImportanceBias = 0.0f);

	/** Remove a single light from the swarm, restoring its base values. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm", meta = (WorldContext = "WorldContextObject"))
	static bool UnregisterLightFromSwarm(const UObject* WorldContextObject, ULightComponent* Light);

	/** What the scheduler did on the most recent frame. Returns a zeroed struct when there is no swarm. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm", meta = (WorldContext = "WorldContextObject", DisplayName = "Get LumaSwarm Stats"))
	static FLumaSwarmStats GetLumaSwarmStats(const UObject* WorldContextObject);
};
