// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LumaSwarmSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/LightComponent.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "LumaSwarmLog.h"
#include "LumaSwarmProfile.h"
#include "LumaSwarmSettings.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("LumaSwarm"), STATGROUP_LumaSwarm, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("LumaSwarm Update"), STAT_LumaSwarmUpdate, STATGROUP_LumaSwarm);
DECLARE_DWORD_COUNTER_STAT(TEXT("Lights Updated"), STAT_LumaSwarmUpdated, STATGROUP_LumaSwarm);
DECLARE_DWORD_COUNTER_STAT(TEXT("Writes Skipped By Epsilon"), STAT_LumaSwarmSkipped, STATGROUP_LumaSwarm);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Registered Lights"), STAT_LumaSwarmRegistered, STATGROUP_LumaSwarm);

namespace LumaSwarmConsole
{
	/**
	 * Debug commands apply to every world that has a swarm, not just the one the console happens to be
	 * bound to. That way LumaSwarm.ShowStats 1 works the same whether it is typed during PIE or in the
	 * editor with nothing playing.
	 */
	static void ForEachSubsystem(UWorld* World, TFunctionRef<void(ULumaSwarmSubsystem&)> Func)
	{
		TArray<ULumaSwarmSubsystem*, TInlineAllocator<4>> Found;

		auto Collect = [&Found](UWorld* Candidate)
		{
			if (Candidate)
			{
				if (ULumaSwarmSubsystem* Subsystem = Candidate->GetSubsystem<ULumaSwarmSubsystem>())
				{
					Found.AddUnique(Subsystem);
				}
			}
		};

		Collect(World);

		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				Collect(Context.World());
			}
		}

		for (ULumaSwarmSubsystem* Subsystem : Found)
		{
			Func(*Subsystem);
		}
	}

	static bool ParseBool(const TArray<FString>& Args, bool bDefault)
	{
		if (Args.Num() == 0)
		{
			return bDefault;
		}
		return Args[0].ToBool() || Args[0] == TEXT("1");
	}

	static FAutoConsoleCommandWithWorldAndArgs GShowStats(
		TEXT("LumaSwarm.ShowStats"),
		TEXT("LumaSwarm.ShowStats 0|1 - show the on-screen scheduler statistics box."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bShow = ParseBool(Args, true);
			ForEachSubsystem(World, [bShow](ULumaSwarmSubsystem& Subsystem) { Subsystem.SetShowStats(bShow); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GBudget(
		TEXT("LumaSwarm.Budget"),
		TEXT("LumaSwarm.Budget <n> - hard cap on light writes per frame."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 0)
			{
				ForEachSubsystem(World, [](ULumaSwarmSubsystem& Subsystem)
				{
					UE_LOG(LogLumaSwarm, Display, TEXT("LumaSwarm.Budget = %d"), Subsystem.GetBudget());
				});
				return;
			}
			const int32 Budget = FCString::Atoi(*Args[0]);
			ForEachSubsystem(World, [Budget](ULumaSwarmSubsystem& Subsystem) { Subsystem.SetBudget(Budget); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GMaxDistance(
		TEXT("LumaSwarm.MaxDistance"),
		TEXT("LumaSwarm.MaxDistance <cm> - distance beyond which lights go dormant."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 0)
			{
				return;
			}
			const float Distance = FCString::Atof(*Args[0]);
			ForEachSubsystem(World, [Distance](ULumaSwarmSubsystem& Subsystem) { Subsystem.SetMaxDistance(Distance); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GPause(
		TEXT("LumaSwarm.Pause"),
		TEXT("LumaSwarm.Pause 0|1 - freeze or resume the swarm clock."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bPause = ParseBool(Args, true);
			ForEachSubsystem(World, [bPause](ULumaSwarmSubsystem& Subsystem) { Subsystem.PauseSwarm(bPause); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GTimeDilation(
		TEXT("LumaSwarm.TimeDilation"),
		TEXT("LumaSwarm.TimeDilation <scale> - speed of the shared swarm clock."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 0)
			{
				return;
			}
			const float Scale = FCString::Atof(*Args[0]);
			ForEachSubsystem(World, [Scale](ULumaSwarmSubsystem& Subsystem) { Subsystem.SetGlobalTimeDilation(Scale); });
		}));
}

void ULumaSwarmSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();

	// "Game" is drawn for PIE viewports and for editor viewports alike, which is what makes the statistics
	// box show up in a level screenshot taken without ever pressing Play.
	DebugDrawHandle = UDebugDrawService::Register(
		TEXT("Game"),
		FDebugDrawDelegate::CreateUObject(this, &ULumaSwarmSubsystem::OnDebugDraw));
}

void ULumaSwarmSubsystem::Deinitialize()
{
	if (DebugDrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DebugDrawHandle);
		DebugDrawHandle.Reset();
	}

	// Leave the level the way we found it: a half-faded lamp left behind by a torn-down world is a bug
	// that only shows up after the fact, in a screenshot nobody can reproduce.
	RestoreAllLights();

	Entries.Reset();
	LightToEntry.Reset();

	Super::Deinitialize();
}

bool ULumaSwarmSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::Editor;
}

bool ULumaSwarmSubsystem::IsTickableInEditor() const
{
	return ULumaSwarmSettings::Get().bAnimateInEditor;
}

TStatId ULumaSwarmSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULumaSwarmSubsystem, STATGROUP_Tickables);
}

void ULumaSwarmSubsystem::ApplySettings()
{
	const ULumaSwarmSettings& Settings = ULumaSwarmSettings::Get();

	MaxUpdatesPerFrame = Settings.MaxUpdatesPerFrame;
	NearDistance = Settings.NearDistance;
	MidDistance = Settings.MidDistance;
	MaxDistance = Settings.MaxDistance;
	MidUpdateInterval = FMath::Max(1, Settings.MidUpdateInterval);
	FarUpdateInterval = FMath::Max(1, Settings.FarUpdateInterval);
	TierHysteresis = Settings.TierHysteresis;
	ReclassifySlices = FMath::Max(1, Settings.ReclassifySlices);
	IntensityEpsilon = Settings.IntensityEpsilon;
	ColorEpsilon = Settings.ColorEpsilon;
	bRestoreOnDormant = Settings.bRestoreOnDormant;
	bShowStats = Settings.bDrawStatsByDefault;

	NearDistanceSq = FMath::Square(NearDistance);
	MidDistanceSq = FMath::Square(MidDistance);
	MaxDistanceSq = FMath::Square(MaxDistance);
}

// ---------------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------------

float ULumaSwarmSubsystem::DerivePhase(const UObject* Object, const FVector& Location)
{
	uint32 Hash = 0x811c9dc5u;

	if (Object)
	{
		Hash = HashCombine(Hash, GetTypeHash(Object->GetFName()));
	}

	// Quantised to a centimetre so a nudge in the editor does not reshuffle the whole swarm's phases.
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Location.X)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Location.Y)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Location.Z)));

	return static_cast<float>(Hash & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void ULumaSwarmSubsystem::RegisterLight(ULightComponent* Light, ULumaSwarmProfile* Profile, float Phase, float ImportanceBias)
{
	if (!IsValid(Light))
	{
		return;
	}

	if (Light->Mobility != EComponentMobility::Movable && !bLoggedStaticMobility)
	{
		bLoggedStaticMobility = true;
		UE_LOG(LogLumaSwarm, Warning,
			TEXT("'%s' is not set to Movable mobility. The engine refuses runtime intensity and colour changes on "
			     "static and stationary lights, so LumaSwarm cannot animate it."),
			*Light->GetPathName());
	}

	const TObjectKey<ULightComponent> Key(Light);

	if (int32* ExistingIndex = LightToEntry.Find(Key))
	{
		// Re-registering an already known light retargets it instead of animating it twice.
		FLumaSwarmEntry& Existing = Entries[*ExistingIndex];
		Existing.Profile = Profile;
		Existing.Phase = FMath::Frac(FMath::Abs(Phase));
		Existing.ImportanceBias = ImportanceBias;
		return;
	}

	FLumaSwarmEntry Entry;
	Entry.Light = Light;
	Entry.Profile = Profile;
	Entry.Location = Light->GetComponentLocation();
	Entry.BaseIntensity = Light->Intensity;
	Entry.BaseColor = Light->GetLightColor();
	Entry.bBaseVisible = Light->IsVisible();
	Entry.Phase = FMath::Frac(FMath::Abs(Phase));
	Entry.ImportanceBias = ImportanceBias;
	Entry.LastAppliedIntensity = Entry.BaseIntensity;
	Entry.LastAppliedColor = Entry.BaseColor;
	Entry.bLastAppliedVisible = Entry.bBaseVisible;
	Entry.Tier = ELumaSwarmTier::Near;
	Entry.LastUpdateFrame = FrameCounter;

	const int32 NewIndex = Entries.Add(MoveTemp(Entry));
	LightToEntry.Add(Key, NewIndex);
}

void ULumaSwarmSubsystem::UnregisterLight(ULightComponent* Light, bool bRestoreBaseValues)
{
	if (!Light)
	{
		return;
	}

	if (const int32* Index = LightToEntry.Find(TObjectKey<ULightComponent>(Light)))
	{
		RemoveEntryAt(*Index, bRestoreBaseValues);
	}
}

int32 ULumaSwarmSubsystem::RegisterActorLights(AActor* Actor, ULumaSwarmProfile* Profile, float PhaseOffset, float ImportanceBias)
{
	if (!IsValid(Actor))
	{
		return 0;
	}

	TArray<ULightComponent*> Lights;
	Actor->GetComponents<ULightComponent>(Lights);

	for (ULightComponent* Light : Lights)
	{
		const float Phase = (PhaseOffset < 0.0f)
			? DerivePhase(Light, Light->GetComponentLocation())
			: PhaseOffset;

		RegisterLight(Light, Profile, Phase, ImportanceBias);
	}

	return Lights.Num();
}

int32 ULumaSwarmSubsystem::UnregisterActorLights(AActor* Actor, bool bRestoreBaseValues)
{
	if (!Actor)
	{
		return 0;
	}

	TArray<ULightComponent*> Lights;
	Actor->GetComponents<ULightComponent>(Lights);

	int32 Removed = 0;
	for (ULightComponent* Light : Lights)
	{
		if (const int32* Index = LightToEntry.Find(TObjectKey<ULightComponent>(Light)))
		{
			RemoveEntryAt(*Index, bRestoreBaseValues);
			++Removed;
		}
	}

	return Removed;
}

void ULumaSwarmSubsystem::RemoveEntryAt(int32 Index, bool bRestoreBaseValues)
{
	if (!Entries.IsValidIndex(Index))
	{
		return;
	}

	if (bRestoreBaseValues)
	{
		RestoreEntry(Entries[Index]);
	}

	if (ULightComponent* Light = Entries[Index].Light.Get())
	{
		LightToEntry.Remove(TObjectKey<ULightComponent>(Light));
	}
	else
	{
		// The component is already gone, so we cannot rebuild its key. Find the stale mapping the slow way.
		for (auto It = LightToEntry.CreateIterator(); It; ++It)
		{
			if (It.Value() == Index)
			{
				It.RemoveCurrent();
				break;
			}
		}
	}

	Entries.RemoveAtSwap(Index, EAllowShrinking::No);

	// RemoveAtSwap moved the last entry into this slot, so its index mapping has to follow it.
	if (Entries.IsValidIndex(Index))
	{
		if (ULightComponent* Moved = Entries[Index].Light.Get())
		{
			LightToEntry.Add(TObjectKey<ULightComponent>(Moved), Index);
		}
	}
}

void ULumaSwarmSubsystem::RestoreAllLights()
{
	for (FLumaSwarmEntry& Entry : Entries)
	{
		RestoreEntry(Entry);
	}
}

void ULumaSwarmSubsystem::RestoreEntry(FLumaSwarmEntry& Entry)
{
	ULightComponent* Light = Entry.Light.Get();
	if (!IsValid(Light))
	{
		return;
	}

	Light->SetIntensity(Entry.BaseIntensity);
	Light->SetLightColor(Entry.BaseColor);

	if (Light->IsVisible() != Entry.bBaseVisible)
	{
		Light->SetVisibility(Entry.bBaseVisible);
	}

	Entry.LastAppliedIntensity = Entry.BaseIntensity;
	Entry.LastAppliedColor = Entry.BaseColor;
	Entry.bLastAppliedVisible = Entry.bBaseVisible;
}

// ---------------------------------------------------------------------------------------------------------
// Runtime knobs
// ---------------------------------------------------------------------------------------------------------

void ULumaSwarmSubsystem::SetBudget(int32 InMaxUpdatesPerFrame)
{
	MaxUpdatesPerFrame = FMath::Max(0, InMaxUpdatesPerFrame);
}

void ULumaSwarmSubsystem::SetMaxDistance(float InMaxDistance)
{
	MaxDistance = FMath::Max(0.0f, InMaxDistance);
	MaxDistanceSq = FMath::Square(MaxDistance);
}

void ULumaSwarmSubsystem::SetGlobalTimeDilation(float InTimeDilation)
{
	GlobalTimeDilation = FMath::Max(0.0f, InTimeDilation);
}

void ULumaSwarmSubsystem::PauseSwarm(bool bInPaused)
{
	bPaused = bInPaused;
}

// ---------------------------------------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------------------------------------

int32 ULumaSwarmSubsystem::GetTierInterval(ELumaSwarmTier Tier) const
{
	switch (Tier)
	{
	case ELumaSwarmTier::Near:	return 1;
	case ELumaSwarmTier::Mid:	return MidUpdateInterval;
	case ELumaSwarmTier::Far:	return FarUpdateInterval;
	default:					return MAX_int32;
	}
}

ELumaSwarmTier ULumaSwarmSubsystem::ClassifyDistance(float DistanceSq, ELumaSwarmTier CurrentTier) const
{
	// Hysteresis: a boundary an entry has already crossed is easier to stay on the far side of than it was
	// to cross in the first place. Without this, a lamp sitting on a boundary changes tier every frame and
	// its refresh rate visibly pumps.
	const float Outward = FMath::Square(1.0f + TierHysteresis);
	const float Inward = FMath::Square(1.0f - TierHysteresis);

	auto Threshold = [Outward, Inward](float BoundarySq, bool bAlreadyBeyond)
	{
		return BoundarySq * (bAlreadyBeyond ? Inward : Outward);
	};

	const uint8 Current = static_cast<uint8>(CurrentTier);

	ELumaSwarmTier Tier = ELumaSwarmTier::Near;
	if (DistanceSq > Threshold(NearDistanceSq, Current >= 1))
	{
		Tier = ELumaSwarmTier::Mid;
	}
	if (DistanceSq > Threshold(MidDistanceSq, Current >= 2))
	{
		Tier = ELumaSwarmTier::Far;
	}
	if (DistanceSq > Threshold(MaxDistanceSq, Current >= 3))
	{
		Tier = ELumaSwarmTier::Dormant;
	}

	return Tier;
}

bool ULumaSwarmSubsystem::ResolveViewLocation(FVector& OutLocation) const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
			{
				OutLocation = CameraManager->GetCameraLocation();
				return true;
			}

			FVector Location;
			FRotator Rotation;
			PC->GetPlayerViewPoint(Location, Rotation);
			OutLocation = Location;
			return true;
		}
	}

	// No player controller: this is an editor viewport with nothing playing. The debug canvas hands us the
	// scene view every time it draws, so use the position it last reported rather than dragging an editor
	// module into a runtime plugin.
	if (bHasCachedViewLocation)
	{
		OutLocation = CachedViewLocation;
		return true;
	}

	return false;
}

void ULumaSwarmSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SCOPE_CYCLE_COUNTER(STAT_LumaSwarmUpdate);
	const double TickStartSeconds = FPlatformTime::Seconds();

	++FrameCounter;

	if (!bPaused)
	{
		SwarmTime += DeltaTime * GlobalTimeDilation;
	}

	Stats.NumUpdatedThisFrame = 0;
	Stats.NumSkippedByEpsilon = 0;

	FVector ViewLocation = FVector::ZeroVector;
	const bool bHasView = ResolveViewLocation(ViewLocation);

	// Runs either way: the same pass that re-buckets a slice of the list is also the one that prunes lights
	// that have been deleted, and that still has to happen when there is no camera to measure against.
	ReclassifySlice(ViewLocation, bHasView);

	if (!bPaused)
	{
		UpdateDueLights();
	}

	Stats.NumRegistered = Entries.Num();
	Stats.BudgetPerFrame = MaxUpdatesPerFrame;
	Stats.NumNear = 0;
	Stats.NumMid = 0;
	Stats.NumFar = 0;
	Stats.NumDormant = 0;

	for (const FLumaSwarmEntry& Entry : Entries)
	{
		switch (Entry.Tier)
		{
		case ELumaSwarmTier::Near:		++Stats.NumNear; break;
		case ELumaSwarmTier::Mid:		++Stats.NumMid; break;
		case ELumaSwarmTier::Far:		++Stats.NumFar; break;
		default:						++Stats.NumDormant; break;
		}
	}

	Stats.LastUpdateMs = static_cast<float>((FPlatformTime::Seconds() - TickStartSeconds) * 1000.0);

	SET_DWORD_STAT(STAT_LumaSwarmRegistered, Stats.NumRegistered);
	INC_DWORD_STAT_BY(STAT_LumaSwarmUpdated, Stats.NumUpdatedThisFrame);
	INC_DWORD_STAT_BY(STAT_LumaSwarmSkipped, Stats.NumSkippedByEpsilon);
}

void ULumaSwarmSubsystem::ReclassifySlice(const FVector& ViewLocation, bool bHasViewLocation)
{
	const int32 Num = Entries.Num();
	if (Num == 0)
	{
		ReclassifyCursor = 0;
		return;
	}

	// One slice per frame. Distance to the camera changes far too slowly to justify re-sorting the whole
	// list every frame, and a full pass over ten thousand entries would be exactly the per-frame spike this
	// plugin exists to avoid.
	const int32 SliceSize = FMath::Max(1, FMath::DivideAndRoundUp(Num, ReclassifySlices));

	int32 Index = (ReclassifyCursor < Num) ? ReclassifyCursor : 0;
	int32 Processed = 0;

	while (Processed < SliceSize && Entries.Num() > 0)
	{
		if (Index >= Entries.Num())
		{
			Index = 0;
		}

		FLumaSwarmEntry& Entry = Entries[Index];

		ULightComponent* Light = Entry.Light.Get();
		if (!IsValid(Light))
		{
			// The light was deleted out from under us. Nothing to restore, nothing to crash on.
			RemoveEntryAt(Index, false);
			++Processed;
			continue;
		}

		// Refresh the cached position while we have the component resolved anyway. Static set dressing never
		// moves, but a lamp carried by a character should not stay bucketed where it was spawned.
		Entry.Location = Light->GetComponentLocation();

		if (!bHasViewLocation)
		{
			// No camera to measure against. Leave the tier alone — entries start out Near, so the swarm still
			// animates and the budget alone bounds the cost. Bucketing against the world origin instead would
			// send half the level dormant for no reason.
			++Index;
			++Processed;
			continue;
		}

		const float DistanceSq = FVector::DistSquared(Entry.Location, ViewLocation);

		// Importance pulls a light towards the camera for classification purposes only: a bias of 1 halves
		// its effective distance, so a hero sign keeps its frame rate from across the street.
		const float Effective = (Entry.ImportanceBias > 0.0f)
			? DistanceSq / FMath::Square(1.0f + Entry.ImportanceBias)
			: DistanceSq;

		const ELumaSwarmTier NewTier = ClassifyDistance(Effective, Entry.Tier);

		if (NewTier != Entry.Tier)
		{
			Entry.Tier = NewTier;
			Entry.bDormantRestored = false;
		}

		if (Entry.Tier == ELumaSwarmTier::Dormant && bRestoreOnDormant && !Entry.bDormantRestored)
		{
			// Done once on the way out, and bounded by the slice size, so it never becomes a spike of its own.
			RestoreEntry(Entry);
			Entry.bDormantRestored = true;
		}

		++Index;
		++Processed;
	}

	ReclassifyCursor = Index;
}

void ULumaSwarmSubsystem::UpdateDueLights()
{
	const int32 Num = Entries.Num();
	if (Num == 0 || MaxUpdatesPerFrame <= 0)
	{
		UpdateCursor = 0;
		return;
	}

	int32 Remaining = MaxUpdatesPerFrame;
	int32 Examined = 0;
	int32 Index = (UpdateCursor < Num) ? UpdateCursor : 0;

	while (Examined < Num && Remaining > 0)
	{
		FLumaSwarmEntry& Entry = Entries[Index];

		const int32 Interval = GetTierInterval(Entry.Tier);
		const uint32 Elapsed = FrameCounter - Entry.LastUpdateFrame;

		if (Interval != MAX_int32 && static_cast<int32>(FMath::Min<uint32>(Elapsed, MAX_int32)) >= Interval)
		{
			ApplyEntry(Entry);
			Entry.LastUpdateFrame = FrameCounter;
			--Remaining;
			++Stats.NumUpdatedThisFrame;
		}

		Index = (Index + 1) % Num;
		++Examined;
	}

	// The cursor deliberately survives the frame. Everything that did not fit into this frame's budget is
	// simply the first thing the next frame looks at, which is what makes the round-robin fair instead of
	// starving the tail of the list forever.
	UpdateCursor = Index;
}

void ULumaSwarmSubsystem::ApplyEntry(FLumaSwarmEntry& Entry)
{
	const ULumaSwarmProfile* Profile = Entry.Profile.Get();
	ULightComponent* Light = Entry.Light.Get();

	if (!Profile || !IsValid(Light))
	{
		return;
	}

	const FLumaSwarmLightState State = Profile->Evaluate(SwarmTime, Entry.Phase);

	bool bWroteAnything = false;

	if (Profile->bAffectIntensity)
	{
		const float NewIntensity = Entry.BaseIntensity * State.IntensityFactor;

		// Relative epsilon against the base value, so the same setting behaves the same on a 50 lumen candle
		// and a 50,000 lumen floodlight.
		const float Tolerance = IntensityEpsilon * FMath::Max(Entry.BaseIntensity, UE_KINDA_SMALL_NUMBER);

		if (FMath::Abs(NewIntensity - Entry.LastAppliedIntensity) > Tolerance)
		{
			Light->SetIntensity(NewIntensity);
			Entry.LastAppliedIntensity = NewIntensity;
			bWroteAnything = true;
		}
	}

	if (Profile->bAffectColor && Profile->HasColorCurve())
	{
		if (!State.Color.Equals(Entry.LastAppliedColor, ColorEpsilon))
		{
			Light->SetLightColor(State.Color);
			Entry.LastAppliedColor = State.Color;
			bWroteAnything = true;
		}
	}

	if (Profile->bAffectVisibility)
	{
		if (State.bVisible != Entry.bLastAppliedVisible)
		{
			Light->SetVisibility(State.bVisible);
			Entry.bLastAppliedVisible = State.bVisible;
			bWroteAnything = true;
		}
	}

	if (!bWroteAnything)
	{
		// Serviced, evaluated, and found to be exactly where it already was. SetIntensity would have marked
		// the render state dirty and queued a render-thread command for no visible change at all.
		++Stats.NumSkippedByEpsilon;
	}
}

// ---------------------------------------------------------------------------------------------------------
// Debug canvas
// ---------------------------------------------------------------------------------------------------------

void ULumaSwarmSubsystem::OnDebugDraw(UCanvas* Canvas, APlayerController* PlayerController)
{
	if (!Canvas)
	{
		return;
	}

	// The debug draw service is engine-wide: every world's subsystem is called for every viewport that
	// draws. Work out which world this viewport is showing and ignore the ones that are not ours.
	UWorld* ViewWorld = nullptr;
	if (Canvas->SceneView && Canvas->SceneView->Family && Canvas->SceneView->Family->Scene)
	{
		ViewWorld = Canvas->SceneView->Family->Scene->GetWorld();
	}
	if (!ViewWorld && PlayerController)
	{
		ViewWorld = PlayerController->GetWorld();
	}
	if (ViewWorld && ViewWorld != GetWorld())
	{
		return;
	}

	if (Canvas->SceneView)
	{
		CachedViewLocation = Canvas->SceneView->ViewLocation;
		bHasCachedViewLocation = true;
	}

	if (!bShowStats)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const float LineHeight = Font->GetMaxCharHeight() + 2.0f;
	const float PanelX = 24.0f;
	const float PanelY = 24.0f;
	const float PanelWidth = 300.0f;

	TArray<FString, TInlineAllocator<8>> Lines;
	Lines.Add(FString::Printf(TEXT("Registered            %d"), Stats.NumRegistered));
	Lines.Add(FString::Printf(TEXT("Updated this frame    %d / %d"), Stats.NumUpdatedThisFrame, Stats.BudgetPerFrame));
	Lines.Add(FString::Printf(TEXT("Skipped (epsilon)     %d"), Stats.NumSkippedByEpsilon));
	Lines.Add(FString::Printf(TEXT("Near / Mid / Far      %d / %d / %d"), Stats.NumNear, Stats.NumMid, Stats.NumFar));
	Lines.Add(FString::Printf(TEXT("Dormant               %d"), Stats.NumDormant));
	Lines.Add(FString::Printf(TEXT("Update                %.3f ms"), Stats.LastUpdateMs));
	if (bPaused)
	{
		Lines.Add(TEXT("PAUSED"));
	}

	const float PanelHeight = LineHeight * (Lines.Num() + 1) + 12.0f;

	FCanvasTileItem Background(
		FVector2D(PanelX - 8.0f, PanelY - 8.0f),
		GWhiteTexture,
		FVector2D(PanelWidth, PanelHeight),
		FLinearColor(0.0f, 0.0f, 0.0f, 0.62f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	FCanvasTextItem Title(FVector2D(PanelX, PanelY), FText::FromString(TEXT("LumaSwarm")), Font, FLinearColor(1.0f, 0.85f, 0.35f));
	Title.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Title);

	float Y = PanelY + LineHeight;
	for (const FString& Line : Lines)
	{
		FCanvasTextItem Text(FVector2D(PanelX, Y), FText::FromString(Line), Font, FLinearColor::White);
		Text.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Text);
		Y += LineHeight;
	}
}
