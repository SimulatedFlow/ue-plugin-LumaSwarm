// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LumaSwarmTypes.generated.h"

/** Animation recipes a LumaSwarm profile can play back. */
UENUM(BlueprintType)
enum class ELumaSwarmAnim : uint8
{
	/** No built-in movement. The intensity shape curve, if present, defines everything. */
	Steady,

	/** Deterministic value noise — broken fluorescent tubes, failing neon, campfires. */
	Flicker,

	/** Smooth sine between the min and max intensity factor — advertising signs, machine status lamps. */
	Pulse,

	/** Hard on/off square wave with an adjustable duty cycle — beacons, police lights, club strobes. */
	Strobe,

	/** Walks the colour curve over one period, intensity stays put unless a shape curve says otherwise. */
	ColorCycle
};

/** Distance bucket a registered light currently sits in. Decides how often it is allowed to be written. */
UENUM(BlueprintType)
enum class ELumaSwarmTier : uint8
{
	/** Updated every frame. */
	Near,

	/** Updated every Nth frame (see project settings). */
	Mid,

	/** Updated rarely. */
	Far,

	/** Beyond the max distance: frozen, never written, optionally reset to its base values once. */
	Dormant
};

/** Placement patterns offered by ALumaSwarmSpawner. */
UENUM(BlueprintType)
enum class ELumaSwarmLayout : uint8
{
	/** Even XY grid centred on the spawner. */
	Grid,

	/** Deterministic scatter inside the spawner's box extent. */
	RandomInBox,

	/** Evenly spaced along the spawner's spline component. */
	AlongSpline
};

/** Light class the spawner creates. */
UENUM(BlueprintType)
enum class ELumaSwarmLightType : uint8
{
	Point,
	Spot,
	Rect
};

/**
 * Result of evaluating a profile at one point in time.
 * A pure function of (profile, time, phase) — no per-light state is involved, which is what lets a light
 * skip frames without its animation drifting out of sync with the rest of the swarm.
 */
USTRUCT(BlueprintType)
struct FLumaSwarmLightState
{
	GENERATED_BODY()

	/** Multiplier applied to the light's intensity as it was when it registered. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	float IntensityFactor = 1.0f;

	/** Colour for this instant. Only meaningful when the profile has Affect Color set. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	FLinearColor Color = FLinearColor::White;

	/** Visibility for this instant. Only meaningful when the profile has Affect Visibility set. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	bool bVisible = true;
};

/**
 * Snapshot of what the scheduler did on the most recent frame.
 * Read it from Blueprint through ULumaSwarmStatics::GetLumaSwarmStats, or show it on screen with
 * the console command LumaSwarm.ShowStats 1.
 */
USTRUCT(BlueprintType)
struct FLumaSwarmStats
{
	GENERATED_BODY()

	/** Lights currently registered with the subsystem. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	int32 NumRegistered = 0;

	/** Registered lights per distance tier. Adds up to NumRegistered. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	int32 NumNear = 0;

	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	int32 NumMid = 0;

	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	int32 NumFar = 0;

	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	int32 NumDormant = 0;

	/** Lights the scheduler serviced on the last frame. Never exceeds BudgetPerFrame. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	int32 NumUpdatedThisFrame = 0;

	/** Of those, how many turned out to be within the epsilon and cost no render-state update at all. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	int32 NumSkippedByEpsilon = 0;

	/** Wall-clock cost of the last scheduler tick in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	float LastUpdateMs = 0.0f;

	/** The hard per-frame cap currently in force. */
	UPROPERTY(BlueprintReadOnly, Category = "LumaSwarm")
	int32 BudgetPerFrame = 0;
};
