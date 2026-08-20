// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LumaSwarmProfile.h"

namespace LumaSwarmNoise
{
	/** Cheap integer avalanche (Lowbias32). Same input, same output, on every platform and every client. */
	FORCEINLINE uint32 Hash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352du;
		X ^= X >> 15;
		X *= 0x846ca68bu;
		X ^= X >> 16;
		return X;
	}

	/** Hash to the unit interval. */
	FORCEINLINE float HashToUnit(uint32 X)
	{
		return static_cast<float>(Hash(X) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
	}

	/**
	 * One-dimensional smoothed value noise in [0,1].
	 * Deterministic by construction — no engine RNG, no per-light state, safe to call from any thread.
	 */
	FORCEINLINE float Value(float X, uint32 Seed)
	{
		const float Floored = FMath::FloorToFloat(X);
		const uint32 Cell = static_cast<uint32>(static_cast<int32>(Floored));
		const float Frac = X - Floored;

		const float A = HashToUnit(Cell ^ Seed);
		const float B = HashToUnit((Cell + 1u) ^ Seed);

		// Smoothstep the interpolant so the noise has no visible corners at the cell boundaries.
		const float Smooth = Frac * Frac * (3.0f - 2.0f * Frac);
		return FMath::Lerp(A, B, Smooth);
	}
}

bool ULumaSwarmProfile::HasIntensityShape() const
{
	if (IntensityShape.ExternalCurve != nullptr)
	{
		return true;
	}

	const FRichCurve* Curve = IntensityShape.GetRichCurveConst();
	return Curve != nullptr && Curve->GetNumKeys() > 0;
}

bool ULumaSwarmProfile::HasColorCurve() const
{
	if (ColorOverTime.ExternalCurve != nullptr)
	{
		return true;
	}

	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		if (ColorOverTime.ColorCurves[Channel].GetNumKeys() > 0)
		{
			return true;
		}
	}

	return false;
}

FLumaSwarmLightState ULumaSwarmProfile::Evaluate(float TimeSeconds, float Phase) const
{
	FLumaSwarmLightState State;

	// Position within the animation. The integer part counts completed periods, the fraction is where we are
	// inside the current one. Both are derived from the clock alone, never accumulated.
	const float Cycle = TimeSeconds * RateHz + Phase;
	const float T = FMath::Frac(Cycle);

	// Every animation boils down to an alpha in [0,1] that is then mapped into [IntensityMin, IntensityMax].
	float Alpha = 1.0f;

	switch (Anim)
	{
	case ELumaSwarmAnim::Steady:
		Alpha = 1.0f;
		break;

	case ELumaSwarmAnim::Pulse:
		Alpha = 0.5f + 0.5f * FMath::Sin(Cycle * UE_TWO_PI);
		break;

	case ELumaSwarmAnim::Strobe:
		Alpha = (T < DutyCycle) ? 1.0f : 0.0f;
		break;

	case ELumaSwarmAnim::ColorCycle:
		// Colour does the talking; intensity stays where it is unless a shape curve overrides it below.
		Alpha = 1.0f;
		break;

	case ELumaSwarmAnim::Flicker:
	{
		// Two octaves: a slow sag plus a fast rattle. Biased upwards so the lamp is mostly on and
		// occasionally stumbles, which is what a failing tube actually looks like.
		const uint32 Seed = static_cast<uint32>(static_cast<int32>(Phase * 65536.0f));
		const float Slow = LumaSwarmNoise::Value(Cycle * 2.0f, Seed);
		const float Fast = LumaSwarmNoise::Value(Cycle * 11.0f, Seed ^ 0x9e3779b9u);
		Alpha = FMath::Clamp(0.65f * Slow + 0.35f * Fast + 0.25f, 0.0f, 1.0f);
		break;
	}
	}

	// An authored shape multiplies onto the animation. With a Steady animation (alpha 1) that means the
	// curve alone defines the light; with Flicker it acts as an envelope on top of the noise.
	if (HasIntensityShape())
	{
		Alpha *= IntensityShape.GetRichCurveConst()->Eval(T, 1.0f);
	}

	if (NoiseAmount > 0.0f)
	{
		const uint32 NoiseSeed = static_cast<uint32>(static_cast<int32>(Phase * 4096.0f)) ^ 0x5bf03635u;
		const float N = LumaSwarmNoise::Value(Cycle * FMath::Max(NoiseFrequency, 0.1f), NoiseSeed);
		Alpha = FMath::Lerp(Alpha, Alpha * N, NoiseAmount);
	}

	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

	State.IntensityFactor = FMath::Lerp(IntensityMin, IntensityMax, Alpha);
	State.bVisible = Alpha > 0.5f;

	if (bAffectColor && HasColorCurve())
	{
		State.Color = ColorOverTime.GetLinearColorValue(T);
	}

	return State;
}

FPrimaryAssetId ULumaSwarmProfile::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("LumaSwarmProfile"), GetFName());
}
