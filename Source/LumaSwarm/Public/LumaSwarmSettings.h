// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LumaSwarmSettings.generated.h"

/**
 * Project-wide defaults for the LumaSwarm scheduler.
 * Edit under Project Settings > Plugins > LumaSwarm; stored in DefaultGame.ini.
 *
 * Every value here is only the starting point — the subsystem's setters and the LumaSwarm.* console
 * commands override them at runtime without touching the config.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "LumaSwarm"))
class LUMASWARM_API ULumaSwarmSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULumaSwarmSettings();

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** Convenience accessor. Never returns null. */
	static const ULumaSwarmSettings& Get();

	/**
	 * Hard cap on how many lights the scheduler may write per frame. This is the whole point of the plugin:
	 * a swarm of 2,000 lamps costs the same per frame as a swarm of 200 once it exceeds the budget, the
	 * difference is only how often each individual lamp gets refreshed.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", UIMax = "1024"))
	int32 MaxUpdatesPerFrame = 128;

	/** Lights closer than this are updated every frame. */
	UPROPERTY(Config, EditAnywhere, Category = "Tiers", meta = (ClampMin = "0.0", UIMax = "20000.0"))
	float NearDistance = 1500.0f;

	/** Lights between Near Distance and this are updated every Mid Update Interval frames. */
	UPROPERTY(Config, EditAnywhere, Category = "Tiers", meta = (ClampMin = "0.0", UIMax = "40000.0"))
	float MidDistance = 4000.0f;

	/** Beyond this a light goes dormant: never written again until it comes back inside. */
	UPROPERTY(Config, EditAnywhere, Category = "Tiers", meta = (ClampMin = "0.0", UIMax = "200000.0"))
	float MaxDistance = 12000.0f;

	/** Frames between updates for the Mid tier. */
	UPROPERTY(Config, EditAnywhere, Category = "Tiers", meta = (ClampMin = "1", UIMax = "16"))
	int32 MidUpdateInterval = 3;

	/** Frames between updates for the Far tier. */
	UPROPERTY(Config, EditAnywhere, Category = "Tiers", meta = (ClampMin = "1", UIMax = "64"))
	int32 FarUpdateInterval = 8;

	/**
	 * Relative slack around each tier boundary, as a fraction. Without it a light parked exactly on a
	 * boundary flips tier every frame as the camera breathes, and its update rate pumps visibly.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tiers", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float TierHysteresis = 0.1f;

	/**
	 * The registration list is re-sorted into tiers one slice at a time, this many slices per full pass.
	 * 8 means an eighth of the lights are re-classified each frame — distance changes slowly enough that
	 * nobody ever notices, and no frame ever pays for the whole list.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tiers", meta = (ClampMin = "1", UIMax = "32"))
	int32 ReclassifySlices = 8;

	/** Reset a light to the intensity and colour it registered with when it goes dormant. */
	UPROPERTY(Config, EditAnywhere, Category = "Tiers")
	bool bRestoreOnDormant = true;

	/**
	 * Relative intensity change below which the write is skipped. SetIntensity marks the light's render
	 * state dirty and queues a render-thread command, so writing a value nobody can see costs real money.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Epsilon", meta = (ClampMin = "0.0", UIMax = "0.25"))
	float IntensityEpsilon = 0.01f;

	/** Per-channel colour change below which the write is skipped. */
	UPROPERTY(Config, EditAnywhere, Category = "Epsilon", meta = (ClampMin = "0.0", UIMax = "0.25"))
	float ColorEpsilon = 0.004f;

	/** Show the on-screen statistics box as soon as a world's subsystem comes up. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bDrawStatsByDefault = false;

	/** Let the scheduler run in an editor world, so a placed spawner animates in the viewport without PIE. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bAnimateInEditor = true;
};
