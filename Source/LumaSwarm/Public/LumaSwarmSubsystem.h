// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "LumaSwarmTypes.h"
#include "LumaSwarmSubsystem.generated.h"

class AActor;
class APlayerController;
class UCanvas;
class ULightComponent;
class ULumaSwarmProfile;

/**
 * One registered light. Plain struct on purpose: the scheduler walks this array every frame and must not
 * touch a single UObject pointer while doing so — weak pointers are only resolved for entries that are
 * actually due, and a light deleted mid-play simply resolves to null and gets pruned.
 */
struct FLumaSwarmEntry
{
	/** The light being animated. Weak, so deleting the actor during play is a non-event. */
	TWeakObjectPtr<ULightComponent> Light;

	/** The recipe. Weak as well — unloading a profile stops the animation instead of crashing it. */
	TWeakObjectPtr<const ULumaSwarmProfile> Profile;

	/** World position cached at registration. Swarm lights are set dressing; they do not move. */
	FVector Location = FVector::ZeroVector;

	/** Values the light had when it registered. Everything is a factor on these, and EndPlay restores them. */
	float BaseIntensity = 0.0f;
	FLinearColor BaseColor = FLinearColor::White;
	bool bBaseVisible = true;

	/** Offset into the animation period, in periods. */
	float Phase = 0.0f;

	/** Added to the tier decision so hero lamps keep their frame rate even far away. Higher pulls closer. */
	float ImportanceBias = 0.0f;

	/** Last values actually written, used by the epsilon test. */
	float LastAppliedIntensity = 0.0f;
	FLinearColor LastAppliedColor = FLinearColor::White;
	bool bLastAppliedVisible = true;

	/** Current distance bucket. */
	ELumaSwarmTier Tier = ELumaSwarmTier::Near;

	/** Scheduler frame this entry was last serviced on. */
	uint32 LastUpdateFrame = 0;

	/** True once the dormant reset has been written, so it happens exactly once per dormancy. */
	bool bDormantRestored = false;
};

/**
 * The scheduler. One per world, ticks itself, owns every animated light in that world.
 *
 * The problem it solves is not the GPU. UE 5.8's MegaLights made hundreds of shadowed dynamic lights
 * affordable to render; what it did not make cheaper is the CPU cost of *changing* them. Every call to
 * ULightComponent::SetIntensity or SetLightColor marks the component's render state dirty and enqueues a
 * render-thread command. Eight hundred flickering lamps each animating themselves means eight hundred of
 * those every frame, on the game thread, forever.
 *
 * LumaSwarm inverts that. Lights do not tick. This subsystem holds the list, classifies it by distance to
 * the camera, and each frame walks a round-robin cursor through it writing at most MaxUpdatesPerFrame
 * entries. Whatever does not fit is deferred to the next frame, never dropped — the cursor stays where it
 * stopped, so every light is guaranteed to come round, and no frame ever pays for all of them at once.
 *
 * The animation survives the skipping because ULumaSwarmProfile::Evaluate is a pure function of
 * (profile, clock, phase): a light updated on frame 400 lands on exactly the value it would have had if it
 * had been updated on frames 391 through 399 as well.
 */
UCLASS()
class LUMASWARM_API ULumaSwarmSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override;

	/**
	 * Add a light to the swarm. Its current intensity, colour and visibility become the base values that
	 * every profile factor is applied to, and that EndPlay restores.
	 * Registering the same component twice updates the existing entry instead of duplicating it.
	 */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm", meta = (AdvancedDisplay = "2"))
	void RegisterLight(ULightComponent* Light, ULumaSwarmProfile* Profile, float Phase = 0.0f, float ImportanceBias = 0.0f);

	/** Remove a light from the swarm, restoring the values it registered with unless told otherwise. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm", meta = (AdvancedDisplay = "1"))
	void UnregisterLight(ULightComponent* Light, bool bRestoreBaseValues = true);

	/**
	 * Register every light component found on an actor. Returns how many were added.
	 * A phase offset of -1 derives a stable phase per light from the actor's name and position, so the same
	 * level flickers identically on every machine and in every screenshot.
	 */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm", meta = (AdvancedDisplay = "2"))
	int32 RegisterActorLights(AActor* Actor, ULumaSwarmProfile* Profile, float PhaseOffset = -1.0f, float ImportanceBias = 0.0f);

	/** Remove every light of an actor from the swarm. Returns how many were removed. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm", meta = (AdvancedDisplay = "1"))
	int32 UnregisterActorLights(AActor* Actor, bool bRestoreBaseValues = true);

	/** Change the hard per-frame write cap. Takes effect on the next tick. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void SetBudget(int32 InMaxUpdatesPerFrame);

	/** Current per-frame write cap. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	int32 GetBudget() const { return MaxUpdatesPerFrame; }

	/** Distance beyond which lights go dormant. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void SetMaxDistance(float InMaxDistance);

	/** Speed of the shared swarm clock. 0 holds the animation still without stopping the scheduler. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void SetGlobalTimeDilation(float InTimeDilation);

	/** Current swarm clock speed. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	float GetGlobalTimeDilation() const { return GlobalTimeDilation; }

	/** Freeze the swarm. Paused lights keep the last value they were written with; nothing is reset. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void PauseSwarm(bool bInPaused);

	/** True while the swarm is frozen. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	bool IsSwarmPaused() const { return bPaused; }

	/** What the scheduler did on the most recent frame. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	const FLumaSwarmStats& GetStats() const { return Stats; }

	/** Show or hide the on-screen statistics box. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void SetShowStats(bool bInShowStats) { bShowStats = bInShowStats; }

	/** True while the statistics box is being drawn. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	bool IsShowingStats() const { return bShowStats; }

	/** Restore every registered light to the values it registered with, without unregistering anything. */
	UFUNCTION(BlueprintCallable, Category = "LumaSwarm")
	void RestoreAllLights();

	/** Number of lights currently in the swarm. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	int32 GetNumRegisteredLights() const { return Entries.Num(); }

	/**
	 * Derive a stable phase in [0,1) from an object's name and world position.
	 * Same level, same flicker — every run, every client, every screenshot.
	 */
	static float DerivePhase(const UObject* Object, const FVector& Location);

private:
	/** Prune dead entries and, when a camera position is available, re-bucket one slice of the list. */
	void ReclassifySlice(const FVector& ViewLocation, bool bHasViewLocation);

	/** Walk the round-robin cursor and service everything that is due, up to the budget. */
	void UpdateDueLights();

	/** Evaluate one entry and write only the channels that moved further than their epsilon. */
	void ApplyEntry(FLumaSwarmEntry& Entry);

	/** Write an entry's base values back onto its light. */
	void RestoreEntry(FLumaSwarmEntry& Entry);

	/** Drop an entry, keeping the index map consistent after the swap-remove. */
	void RemoveEntryAt(int32 Index, bool bRestoreBaseValues);

	/** Frames between updates for a tier. */
	int32 GetTierInterval(ELumaSwarmTier Tier) const;

	/** Bucket a squared distance, applying hysteresis relative to the tier the entry is already in. */
	ELumaSwarmTier ClassifyDistance(float DistanceSq, ELumaSwarmTier CurrentTier) const;

	/** Best available camera position: player camera first, then the last view the debug canvas reported. */
	bool ResolveViewLocation(FVector& OutLocation) const;

	/** Canvas callback: caches the view location and, when enabled, draws the statistics box. */
	void OnDebugDraw(UCanvas* Canvas, APlayerController* PlayerController);

	/** Pull the runtime knobs out of the project settings. Called once on Initialize. */
	void ApplySettings();

	/** Every registered light. Order is not stable — entries are swap-removed. */
	TArray<FLumaSwarmEntry> Entries;

	/** Component to index in Entries, so registration and removal stay O(1) at any swarm size. */
	TMap<TObjectKey<ULightComponent>, int32> LightToEntry;

	/** Round-robin cursor. Deliberately survives across frames; that is what makes the budget fair. */
	int32 UpdateCursor = 0;

	/** Cursor for the staggered re-classification pass. */
	int32 ReclassifyCursor = 0;

	/** Scheduler frame counter. Wraps harmlessly — only differences are ever compared. */
	uint32 FrameCounter = 0;

	/** Shared clock every profile is evaluated against. */
	float SwarmTime = 0.0f;

	/** Runtime copies of the project settings, adjustable through the setters and console commands. */
	int32 MaxUpdatesPerFrame = 128;
	float NearDistanceSq = 0.0f;
	float MidDistanceSq = 0.0f;
	float MaxDistanceSq = 0.0f;
	float NearDistance = 1500.0f;
	float MidDistance = 4000.0f;
	float MaxDistance = 12000.0f;
	int32 MidUpdateInterval = 3;
	int32 FarUpdateInterval = 8;
	float TierHysteresis = 0.1f;
	int32 ReclassifySlices = 8;
	float IntensityEpsilon = 0.01f;
	float ColorEpsilon = 0.004f;
	bool bRestoreOnDormant = true;

	float GlobalTimeDilation = 1.0f;
	bool bPaused = false;
	bool bShowStats = false;

	/** Last view position reported by the debug canvas, used when there is no player camera (editor viewport). */
	FVector CachedViewLocation = FVector::ZeroVector;
	bool bHasCachedViewLocation = false;

	/** Warn once per world instead of once per light about static-mobility lights that cannot be animated. */
	bool bLoggedStaticMobility = false;

	/** Handle for the canvas callback. */
	FDelegateHandle DebugDrawHandle;

	/** Published through GetStats(). */
	FLumaSwarmStats Stats;
};
