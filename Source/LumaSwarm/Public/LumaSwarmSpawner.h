// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LumaSwarmTypes.h"
#include "LumaSwarmSpawner.generated.h"

class UInstancedStaticMeshComponent;
class ULightComponent;
class ULumaSwarmProfile;
class USplineComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * Places a whole swarm from one actor: hundreds of lights in a grid, a scattered box or along a spline,
 * each with a colour from the palette and a profile from the rotation, all registered with the scheduler.
 *
 * The lights are components of this single actor, not one actor each. Four hundred separate light actors
 * would drag four hundred sets of replication state, tick registrations, outliner entries and world
 * bookkeeping behind them — none of which a piece of animated set dressing has any use for. Four hundred
 * components on one actor cost the components and nothing else.
 *
 * Everything is derived from Seed, so the same settings produce the same swarm every time the actor is
 * rebuilt, on every machine.
 */
UCLASS(Blueprintable, meta = (DisplayName = "LumaSwarm Spawner"))
class LUMASWARM_API ALumaSwarmSpawner : public AActor
{
	GENERATED_BODY()

public:
	ALumaSwarmSpawner();

	/** How the lights are laid out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Layout")
	ELumaSwarmLayout Layout = ELumaSwarmLayout::Grid;

	/** How many lights to place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Layout", meta = (ClampMin = "0", UIMax = "4000"))
	int32 Count = 400;

	/** Half-size of the box used by the Random In Box layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Layout")
	FVector Extent = FVector(2000.0f, 2000.0f, 300.0f);

	/** Distance between neighbouring lights in the Grid layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Layout", meta = (ClampMin = "1.0"))
	float Spacing = 200.0f;

	/** Sideways offset applied to every other light along a spline, for a two-sided street. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Layout")
	float SplineSideOffset = 0.0f;

	/** Vertical offset applied to every placed light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Layout")
	float HeightOffset = 0.0f;

	/** Seeds every random decision: positions, palette jitter, phases. Same seed, same swarm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Layout")
	int32 Seed = 1337;

	/** Which light class to create. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights")
	ELumaSwarmLightType LightType = ELumaSwarmLightType::Point;

	/** Attenuation radius of each light. Keep it tight; overlapping radii are what makes light counts hurt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights", meta = (ClampMin = "1.0"))
	float Radius = 600.0f;

	/** Base intensity every profile factor is applied to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights", meta = (ClampMin = "0.0"))
	float Intensity = 5000.0f;

	/** Physical size of the emitter. Small values keep highlights crisp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights", meta = (ClampMin = "0.0"))
	float SourceRadius = 8.0f;

	/** Outer cone angle for the Spot light type, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights", meta = (ClampMin = "1.0", ClampMax = "80.0"))
	float SpotOuterConeAngle = 40.0f;

	/**
	 * Let the swarm cast shadows. Off by default: a few hundred shadow casters is exactly the case
	 * MegaLights exists for, and turning it on without MegaLights enabled will not end well.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights")
	bool bCastShadows = false;

	/** Colours handed out in rotation. Leave it empty for plain white. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights")
	TArray<FLinearColor> Palette;

	/** Profiles handed out in rotation, so one spawner can mix flickering tubes with pulsing signs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights")
	TArray<TObjectPtr<ULumaSwarmProfile>> Profiles;

	/** Scheduling bias passed to every light this spawner creates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Lights", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float ImportanceBias = 0.0f;

	/**
	 * Give every light a visible fixture. All of them share one instanced component, so the meshes cost a
	 * single draw call no matter how many lights the spawner places.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Meshes")
	bool bSpawnMeshes = false;

	/** Mesh used for the fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Meshes", meta = (EditCondition = "bSpawnMeshes"))
	TObjectPtr<UStaticMesh> LightMesh;

	/** Optional material override for the fixtures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Meshes", meta = (EditCondition = "bSpawnMeshes"))
	TObjectPtr<UMaterialInterface> MeshMaterial;

	/** Uniform scale of each fixture instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Meshes", meta = (EditCondition = "bSpawnMeshes", ClampMin = "0.001"))
	float MeshScale = 0.15f;

	/** Rebuild the swarm whenever the actor is constructed, so edits show up in the viewport immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LumaSwarm|Editor")
	bool bAutoRebuildOnConstruction = true;

	/** Throw the current swarm away and place a fresh one from the current settings. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "LumaSwarm")
	void Rebuild();

	/** Unregister and destroy every light this spawner created. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "LumaSwarm")
	void ClearLights();

	/** Number of lights currently placed by this spawner. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	int32 GetNumSpawnedLights() const { return SpawnedLights.Num(); }

	/** The spline used by the Along Spline layout. Shape it in the viewport like any other spline. */
	UFUNCTION(BlueprintPure, Category = "LumaSwarm")
	USplineComponent* GetSpline() const { return Spline; }

protected:
	// AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

private:
	/** Work out where light number Index goes. Returns false when the layout cannot place it. */
	bool ComputeTransform(int32 Index, FRandomStream& Stream, FTransform& OutTransform) const;

	/** Build one light of the configured type, already set up but not yet registered. */
	ULightComponent* CreateLight(int32 Index, const FTransform& LightTransform, const FLinearColor& Color);

	/** Hand every placed light to the scheduler. Safe to call more than once. */
	void RegisterWithSwarm();

	/** Take every placed light back off the scheduler. */
	void UnregisterFromSwarm();

	UPROPERTY(VisibleAnywhere, Category = "LumaSwarm")
	TObjectPtr<USceneComponent> SwarmRoot;

	UPROPERTY(VisibleAnywhere, Category = "LumaSwarm")
	TObjectPtr<USplineComponent> Spline;

	/** One instanced component holds every fixture mesh, which is why the meshes cost one draw call. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> MeshInstances;

	/** Lights created by the last rebuild. Transient — they are regenerated from Seed, never saved. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULightComponent>> SpawnedLights;
};
