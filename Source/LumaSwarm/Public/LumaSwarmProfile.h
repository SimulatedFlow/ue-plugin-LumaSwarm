// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/DataAsset.h"
#include "LumaSwarmTypes.h"
#include "LumaSwarmProfile.generated.h"

/**
 * A reusable animation recipe. One asset can drive a thousand lights; nothing about a profile is per-light.
 *
 * Profiles are deliberately stateless. Everything a light needs is derived from the profile, the swarm
 * clock and the light's own phase offset — see Evaluate() for why that matters.
 */
UCLASS(BlueprintType, meta = (DisplayName = "LumaSwarm Profile"))
class LUMASWARM_API ULumaSwarmProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Which built-in animation to play. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm")
	ELumaSwarmAnim Anim = ELumaSwarmAnim::Pulse;

	/** Periods per second. One period is one full pulse, one strobe on/off pair, one colour-curve pass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm", meta = (ClampMin = "0.0", UIMax = "30.0"))
	float RateHz = 2.0f;

	/** Lower end of the intensity range, as a factor on the intensity the light had when it registered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Intensity", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float IntensityMin = 0.0f;

	/** Upper end of the intensity range, as a factor on the intensity the light had when it registered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Intensity", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float IntensityMax = 1.0f;

	/**
	 * Optional shape over one period (X from 0 to 1). When it has keys or an external curve it is
	 * multiplied onto whatever the selected animation produced, so a Steady profile with a curve becomes
	 * a pure curve playback while a Flicker profile with a curve keeps its noise and gains an envelope.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Intensity")
	FRuntimeFloatCurve IntensityShape;

	/** Colour over one period (X from 0 to 1). Only read by the ColorCycle animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Color")
	FRuntimeCurveLinearColor ColorOverTime;

	/**
	 * How much deterministic value noise is mixed into the intensity. 0 is a clean animation, 1 is pure noise.
	 * The noise is hashed from the phase and the swarm clock, never from FMath::Rand — two clients running
	 * the same level see exactly the same flicker without exchanging a single byte.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoiseAmount = 0.0f;

	/** Frequency multiplier for the noise, relative to the animation rate. Higher is more nervous. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Noise", meta = (ClampMin = "0.1", UIMax = "20.0"))
	float NoiseFrequency = 4.0f;

	/** Fraction of each period the Strobe animation stays on. 0.5 is a symmetric square wave. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Strobe", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DutyCycle = 0.5f;

	/** Write the animated intensity to the light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Channels")
	bool bAffectIntensity = true;

	/** Write the animated colour to the light. Requires a ColorCycle animation to produce anything. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Channels")
	bool bAffectColor = false;

	/**
	 * Toggle component visibility instead of writing a value. For a hard strobe this is cheaper than an
	 * intensity write and reads identically on screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LumaSwarm|Channels")
	bool bAffectVisibility = false;

	/**
	 * Evaluate the profile for one instant.
	 *
	 * This is a pure function of (profile, time, phase) and holds no state whatsoever. That is the single
	 * design decision the rest of the plugin rests on: because the result at time T does not depend on the
	 * result at time T-1, the scheduler is free to skip a light for five frames, twenty frames, or an entire
	 * minute while it is far away, and the light still snaps back onto exactly the value it would have had
	 * if it had been updated every frame. A per-light state machine would drift apart the moment two lights
	 * were served at different rates, and the whole budgeting idea would collapse with it.
	 *
	 * @param TimeSeconds  The swarm clock (shared by every light in the world, not the world's own time).
	 * @param Phase        This light's offset within the period, in periods. Keeps identical lamps from beating in unison.
	 */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	FLumaSwarmLightState Evaluate(float TimeSeconds, float Phase) const;

	/** True when IntensityShape carries usable data and should be multiplied onto the animation. */
	bool HasIntensityShape() const;

	/** True when ColorOverTime carries usable data. */
	bool HasColorCurve() const;

	// UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
