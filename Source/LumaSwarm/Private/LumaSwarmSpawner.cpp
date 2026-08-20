// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "LumaSwarmSpawner.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "LumaSwarmLog.h"
#include "LumaSwarmProfile.h"
#include "LumaSwarmSubsystem.h"

ALumaSwarmSpawner::ALumaSwarmSpawner()
{
	// The spawner has no work of its own to do. The scheduler animates the lights it places.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SwarmRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SwarmRoot"));
	SetRootComponent(SwarmRoot);

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	Spline->SetupAttachment(SwarmRoot);

	Palette = {
		FLinearColor(1.0f, 0.10f, 0.55f),	// magenta
		FLinearColor(0.10f, 0.85f, 1.0f),	// cyan
		FLinearColor(1.0f, 0.65f, 0.20f),	// amber
		FLinearColor(0.25f, 1.0f, 0.40f)	// green
	};
}

void ALumaSwarmSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (bAutoRebuildOnConstruction)
	{
		Rebuild();
	}
}

void ALumaSwarmSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (SpawnedLights.Num() == 0 && Count > 0)
	{
		Rebuild();
	}
	else
	{
		// OnConstruction already placed them; make sure the play world's scheduler knows about them.
		RegisterWithSwarm();
	}
}

void ALumaSwarmSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSwarm();
	Super::EndPlay(EndPlayReason);
}

void ALumaSwarmSpawner::Destroyed()
{
	ClearLights();
	Super::Destroyed();
}

bool ALumaSwarmSpawner::ComputeTransform(int32 Index, FRandomStream& Stream, FTransform& OutTransform) const
{
	const FTransform& ActorTransform = GetActorTransform();

	switch (Layout)
	{
	case ELumaSwarmLayout::Grid:
	{
		// Square-ish grid centred on the actor.
		const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FMath::Max(Count, 1)))));
		const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(Count, Columns));

		const int32 Column = Index % Columns;
		const int32 Row = Index / Columns;

		const FVector Local(
			(Column - (Columns - 1) * 0.5f) * Spacing,
			(Row - (Rows - 1) * 0.5f) * Spacing,
			HeightOffset);

		OutTransform = FTransform(ActorTransform.TransformPosition(Local));
		return true;
	}

	case ELumaSwarmLayout::RandomInBox:
	{
		const FVector Local(
			Stream.FRandRange(-Extent.X, Extent.X),
			Stream.FRandRange(-Extent.Y, Extent.Y),
			Stream.FRandRange(-Extent.Z, Extent.Z) + HeightOffset);

		OutTransform = FTransform(ActorTransform.TransformPosition(Local));
		return true;
	}

	case ELumaSwarmLayout::AlongSpline:
	{
		if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
		{
			return false;
		}

		const float Length = Spline->GetSplineLength();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		// Every other light goes to the opposite side, which turns a single spline down the middle of an
		// alley into two rows of signs facing each other.
		const int32 Steps = FMath::Max(1, Count / 2);
		const int32 Step = Index / 2;
		const float Side = (Index % 2 == 0) ? 1.0f : -1.0f;

		const float Distance = (Steps > 1) ? (Length * Step) / (Steps - 1) : 0.0f;

		const FVector Location = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		const FRotator Rotation = Spline->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		const FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

		OutTransform = FTransform(
			FRotator(0.0f, Rotation.Yaw + (Side > 0.0f ? -90.0f : 90.0f), 0.0f),
			Location + Right * (SplineSideOffset * Side) + FVector(0.0f, 0.0f, HeightOffset));
		return true;
	}
	}

	return false;
}

ULightComponent* ALumaSwarmSpawner::CreateLight(int32 Index, const FTransform& LightTransform, const FLinearColor& Color)
{
	ULightComponent* Light = nullptr;

	// Transient: the whole swarm is a pure function of the actor's settings and Seed, so saving hundreds of
	// components into the level would only bloat the map with data the constructor can regenerate for free.
	const EObjectFlags Flags = RF_Transient;

	switch (LightType)
	{
	case ELumaSwarmLightType::Spot:
	{
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this, NAME_None, Flags);
		Spot->OuterConeAngle = SpotOuterConeAngle;
		Spot->InnerConeAngle = FMath::Max(0.0f, SpotOuterConeAngle - 12.0f);
		Spot->AttenuationRadius = Radius;
		Spot->SourceRadius = SourceRadius;
		Light = Spot;
		break;
	}

	case ELumaSwarmLightType::Rect:
	{
		URectLightComponent* Rect = NewObject<URectLightComponent>(this, NAME_None, Flags);
		Rect->AttenuationRadius = Radius;
		Rect->SourceWidth = FMath::Max(1.0f, SourceRadius * 8.0f);
		Rect->SourceHeight = FMath::Max(1.0f, SourceRadius * 3.0f);
		Light = Rect;
		break;
	}

	case ELumaSwarmLightType::Point:
	default:
	{
		UPointLightComponent* Point = NewObject<UPointLightComponent>(this, NAME_None, Flags);
		Point->AttenuationRadius = Radius;
		Point->SourceRadius = SourceRadius;
		Light = Point;
		break;
	}
	}

	if (!Light)
	{
		return nullptr;
	}

	// Movable is not optional: the engine refuses runtime intensity and colour changes on static and
	// stationary lights, so a swarm built from anything else would simply sit there.
	Light->SetMobility(EComponentMobility::Movable);
	Light->Intensity = Intensity;
	Light->LightColor = Color.ToFColor(true);
	Light->CastShadows = bCastShadows;
	Light->bAffectsWorld = true;

	Light->SetupAttachment(SwarmRoot);
	Light->RegisterComponent();

	// After registration, not before: SetupAttachment only queues the attachment, so a world transform set
	// too early would be interpreted relative to nothing and then re-applied against the root, which puts
	// the whole swarm at double the actor's offset from the origin.
	Light->SetWorldTransform(LightTransform);

	return Light;
}

void ALumaSwarmSpawner::Rebuild()
{
	ClearLights();

	if (Count <= 0)
	{
		return;
	}

	FRandomStream Stream(Seed);

	if (bSpawnMeshes && LightMesh)
	{
		MeshInstances = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		MeshInstances->SetStaticMesh(LightMesh);
		if (MeshMaterial)
		{
			MeshInstances->SetMaterial(0, MeshMaterial);
		}
		MeshInstances->SetMobility(EComponentMobility::Movable);
		MeshInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshInstances->SetCastShadow(false);
		MeshInstances->SetupAttachment(SwarmRoot);
		MeshInstances->RegisterComponent();
	}

	SpawnedLights.Reserve(Count);

	int32 Placed = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FTransform LightTransform;
		if (!ComputeTransform(Index, Stream, LightTransform))
		{
			continue;
		}

		const FLinearColor Color = (Palette.Num() > 0) ? Palette[Index % Palette.Num()] : FLinearColor::White;

		if (ULightComponent* Light = CreateLight(Index, LightTransform, Color))
		{
			SpawnedLights.Add(Light);
			++Placed;

			if (MeshInstances)
			{
				FTransform MeshTransform = LightTransform;
				MeshTransform.SetScale3D(FVector(MeshScale));
				MeshInstances->AddInstance(MeshTransform, /*bWorldSpace=*/true);
			}
		}
	}

	if (Placed == 0 && Layout == ELumaSwarmLayout::AlongSpline)
	{
		UE_LOG(LogLumaSwarm, Warning,
			TEXT("'%s' is set to Along Spline but its spline has fewer than two points, so nothing was placed."),
			*GetName());
	}

	RegisterWithSwarm();
}

void ALumaSwarmSpawner::ClearLights()
{
	UnregisterFromSwarm();

	for (const TObjectPtr<ULightComponent>& Light : SpawnedLights)
	{
		if (IsValid(Light))
		{
			Light->DestroyComponent();
		}
	}
	SpawnedLights.Reset();

	if (IsValid(MeshInstances))
	{
		MeshInstances->DestroyComponent();
	}
	MeshInstances = nullptr;
}

void ALumaSwarmSpawner::RegisterWithSwarm()
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

	for (int32 Index = 0; Index < SpawnedLights.Num(); ++Index)
	{
		ULightComponent* Light = SpawnedLights[Index];
		if (!IsValid(Light))
		{
			continue;
		}

		ULumaSwarmProfile* Profile = (Profiles.Num() > 0) ? Profiles[Index % Profiles.Num()].Get() : nullptr;

		// Derived from the light's position, so identical neighbours never beat in unison and the same map
		// always produces the same flicker.
		const float Phase = ULumaSwarmSubsystem::DerivePhase(Light, Light->GetComponentLocation());

		Subsystem->RegisterLight(Light, Profile, Phase, ImportanceBias);
	}
}

void ALumaSwarmSpawner::UnregisterFromSwarm()
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

	for (const TObjectPtr<ULightComponent>& Light : SpawnedLights)
	{
		if (IsValid(Light))
		{
			Subsystem->UnregisterLight(Light, true);
		}
	}
}
