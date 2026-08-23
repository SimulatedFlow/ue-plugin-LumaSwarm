# LumaSwarm — Documentation

*Animated light swarms on a budget. Unreal Engine 5.8, runtime C++ plugin, no editor dependencies.*

---

## Table of contents

1. [Supported engine and platforms](#1-supported-engine-and-platforms)
2. [Installation](#2-installation)
3. [What the plugin actually does](#3-what-the-plugin-actually-does)
4. [Quick start](#4-quick-start)
5. [Profile assets](#5-profile-assets)
6. [Budget tuning](#6-budget-tuning)
7. [API overview](#7-api-overview)
8. [Code examples](#8-code-examples)
9. [Console commands and statistics](#9-console-commands-and-statistics)
10. [Demo content](#10-demo-content)
11. [Networking](#11-networking)
12. [Limits — read this before buying](#12-limits--read-this-before-buying)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. Supported engine and platforms

| | |
| --- | --- |
| **Engine version** | Unreal Engine **5.8** (`"EngineVersion": "5.8.0"` in the `.uplugin`) |
| **Plugin type** | Code plugin, full C++ source included |
| **Modules** | Exactly one: `LumaSwarm`, `Type: Runtime`, `LoadingPhase: PreDefault` |
| **Platform allow list** | `Win64`, `Mac`, `Linux` |
| **Editor dependencies** | None. No `UnrealEd`, no `Slate`, no editor module — everything here survives a cooked shipping build |
| **Module dependencies** | `Core`, `CoreUObject`, `Engine`, `DeveloperSettings` (public); `RenderCore`, `RHI` (private) |
| **Third-party libraries** | None |
| **Content** | Demo map, four profile assets, materials, HUD and demo Blueprints under `Content/LumaSwarm/` |

The plugin uses no platform-specific code. The allow list is the set of platforms it is offered for on Fab;
compilation is verified on Win64. Consoles are not in the allow list — not because anything would break, but
because they cannot be verified here.

---

## 2. Installation

**From Fab**

1. Install the plugin from the Epic Games Launcher into your engine, or into the project's `Plugins/` folder.
2. Open the project. Accept the rebuild prompt if one appears.
3. **Edit ▸ Plugins ▸ Rendering ▸ LumaSwarm** — make sure it is enabled, then restart the editor.

**From source**

1. Copy the `LumaSwarm` folder into `<YourProject>/Plugins/LumaSwarm`.
2. Right-click the `.uproject` ▸ **Generate Visual Studio project files** (or run `GenerateProjectFiles` on
   Mac/Linux).
3. Build the project's editor target and start the editor.

A Blueprint-only project works too, as long as it has a C++ toolchain available for the initial compile — the
plugin has no C++ integration requirements beyond being enabled.

Nothing needs to be added to your `Build.cs` unless you want to call the API from your own C++ module. In that
case add `"LumaSwarm"` to `PublicDependencyModuleNames` — see [§8](#8-code-examples).

---

## 3. What the plugin actually does

UE 5.8 made MegaLights production-ready — hundreds of shadowed dynamic lights are now affordable to render.
That is a GPU improvement. The CPU side is unchanged: `ULightComponent::SetIntensity`,
`SetLightColor` and `SetVisibility` each mark the component's render state dirty and enqueue a render-thread
command. A level with 800 lamps, each running its own flicker Blueprint or Timeline, produces 800 of those
per frame, forever, whether the player can see the lamps or not.

LumaSwarm replaces that with one scheduler:

* **Lights do not tick.** `ULumaSwarmComponent` sets `bCanEverTick = false` and never turns it back on.
  `ALumaSwarmSpawner` does the same. Everything is driven from one `UTickableWorldSubsystem`.
* **Distance tiers.** Near lights are refreshed every frame, mid lights every *n*th frame, far lights rarely,
  and anything past `MaxDistance` goes dormant and is not written at all.
* **A hard per-frame budget.** At most `MaxUpdatesPerFrame` lights are written in any one frame. Whatever
  does not fit is deferred to the next frame — never dropped, never doubled up later.
* **A dirty epsilon.** A value that has not moved further than `IntensityEpsilon` (relative) or
  `ColorEpsilon` is not written at all, because writing it would cost a render-state update for a change
  nobody can see.
* **Staggered re-classification.** Only `1/ReclassifySlices` of the list is re-bucketed per frame. The plugin
  refuses to create the kind of frame spike it exists to prevent.

### Why the animation does not fall apart when frames are skipped

`ULumaSwarmProfile::Evaluate(Time, Phase)` is a **pure function**. The value at time *T* does not depend on
the value at time *T−1*; there is no per-light state machine anywhere in the plugin.

That is the single design decision everything else rests on. Because a light's correct value can be computed
from the clock alone, the scheduler is free to skip it for five frames, fifty frames, or a full minute while
it is off in the distance — and when it is finally served again it lands on exactly the value it would have
had if it had been updated every frame in between. A stateful implementation (accumulate, step, advance)
would drift apart the moment two lights were served at different rates, and the whole budgeting idea would
collapse with it.

A useful side effect: the animation is identical on every machine that shares the same clock and phase, so
it needs no replication at all. See [§11](#11-networking).

---

## 4. Quick start

### With the spawner

1. Drag a **LumaSwarm Spawner** into the level.
2. Set `Count` (default 400) and choose a `Layout`:
   * **Grid** — square-ish grid centred on the actor, `Spacing` apart.
   * **RandomInBox** — deterministic scatter inside `Extent`.
   * **AlongSpline** — evenly spaced along the actor's own spline component; every other light is pushed
     to the opposite side by `SplineSideOffset`, which turns one spline down the middle of an alley into
     two facing rows of signs.
3. Fill `Palette` with the colours you want handed out in rotation (it comes pre-filled with a neon set).
4. Fill `Profiles` with one or more profile assets. They are handed out in rotation too, so one spawner can
   mix flickering tubes with pulsing signs and a colour-cycling billboard.
5. Press **Rebuild** in the Details panel. The lights appear immediately — no PIE required.
6. Press Play, open the console, type `LumaSwarm.ShowStats 1`.

The spawner's lights are **components of that one actor**, not one actor each. Four hundred separate light
actors would drag four hundred sets of replication state, tick registrations, outliner entries and world
bookkeeping behind them; four hundred components cost the components and nothing else.

They are also created transient and regenerated from `Seed` in `OnConstruction`, so a spawner with 2,000
lights adds essentially nothing to the size of your saved map.

### With lights you placed yourself

1. Select the actor that owns the lights.
2. **Add Component ▸ LumaSwarm Component**.
3. Assign a `Profile`.
4. Leave `Phase Offset` at `-1` to derive a stable phase from each light's name and position, or set an
   explicit value in the range 0–1.

At BeginPlay the component hands its owner's lights to the subsystem and gets out of the way. At EndPlay it
takes them back and restores the intensity, colour and visibility they had on registration.

> **Set your lights to Movable.** The engine refuses runtime intensity and colour changes on Static and
> Stationary lights. LumaSwarm logs one warning per world when it is handed one and carries on.

---

## 5. Profile assets

Create one with **Right-click in the Content Browser ▸ Miscellaneous ▸ Data Asset ▸ LumaSwarm Profile**.

| Property | Meaning |
| --- | --- |
| `Anim` | `Steady`, `Flicker`, `Pulse`, `Strobe` or `ColorCycle` |
| `RateHz` | Periods per second. One period = one pulse, one strobe on/off pair, one colour-curve pass |
| `IntensityMin` / `IntensityMax` | **Factors** (0–2) on the intensity the light had when it registered |
| `IntensityShape` | Optional curve over one period (X 0→1). Multiplied onto whatever the animation produced |
| `ColorOverTime` | Colour curve over one period. Read by `ColorCycle` |
| `NoiseAmount` | 0–1, how much deterministic value noise is mixed into the intensity |
| `NoiseFrequency` | Noise frequency relative to the animation rate |
| `DutyCycle` | Fraction of each period `Strobe` stays on |
| `bAffectIntensity` | Write the animated intensity |
| `bAffectColor` | Write the animated colour (needs a `ColorOverTime` curve to do anything) |
| `bAffectVisibility` | Toggle component visibility instead of writing a value — cheaper for a hard strobe |

Notes:

* Because min and max are **factors**, the same profile works on a 50-lumen candle and a 50,000-lumen
  floodlight. Set the absolute brightness on the light itself.
* `IntensityShape` multiplies. On a `Steady` profile the built-in alpha is 1, so the curve alone defines the
  light. On `Flicker` the curve acts as an envelope on top of the noise.
* The noise is a hashed value noise seeded from the phase — never `FMath::Rand`, never engine RNG. Same
  level, same flicker, every run and every machine.

Three settings that cover most of what a night street needs:

| Look | Anim | Rate | Min / Max | Noise |
| --- | --- | --- | --- | --- |
| Broken fluorescent tube | `Flicker` | 6–12 Hz | 0.05 / 1.0 | 0.5–0.8 |
| Advertising sign | `Pulse` | 0.4–1.0 Hz | 0.55 / 1.0 | 0 |
| Colour billboard | `ColorCycle` | 0.15 Hz | 0.9 / 1.0 | 0 |

The four ready-made assets that ship with the plugin are listed in [§10](#10-demo-content).

---

## 6. Budget tuning

Everything below lives in **Project Settings ▸ Plugins ▸ LumaSwarm** (`ULumaSwarmSettings`, stored in
`DefaultGame.ini`) and can be overridden at runtime through the subsystem setters or the console.

| Setting | Default | What it does |
| --- | --- | --- |
| `MaxUpdatesPerFrame` | 128 | Hard cap on light writes per frame |
| `NearDistance` | 1500 | Below this, every frame |
| `MidDistance` | 4000 | Between Near and this, every `MidUpdateInterval` frames |
| `MaxDistance` | 12000 | Beyond this, dormant |
| `MidUpdateInterval` | 3 | Frames between mid-tier updates |
| `FarUpdateInterval` | 8 | Frames between far-tier updates |
| `TierHysteresis` | 0.1 | Slack around each tier boundary |
| `ReclassifySlices` | 8 | Re-bucket 1/8 of the list per frame instead of all of it |
| `bRestoreOnDormant` | true | Reset a light to its base values when it goes dormant |
| `IntensityEpsilon` | 0.01 | Relative intensity change below which the write is skipped |
| `ColorEpsilon` | 0.004 | Per-channel colour change below which the write is skipped |
| `bDrawStatsByDefault` | false | Show the statistics box as soon as a world comes up |
| `bAnimateInEditor` | true | Let the scheduler run in an editor world, without PIE |

**How to tune it.** Turn the stats box on and lower the budget until you can see the animation resolving too
coarsely, then go back up one step. What "too coarse" means depends entirely on the animation: a slow pulse
at 0.5 Hz survives being updated every twentieth frame, a 12 Hz flicker does not. A budget below the number
of *near* lights means even near lights start to share frames — that is legitimate, and it is what keeps the
worst case bounded, but it is where you will notice it first.

**Hysteresis** exists because without it a lamp parked exactly on a tier boundary flips bucket every frame as
the camera breathes, and its refresh rate pumps visibly. Ten percent of slack costs nothing and removes it.

**Importance bias** (on the component and the spawner) pulls a light towards the camera for classification
purposes only: a bias of 1 makes it behave as if it were half as far away. Use it on the handful of lamps the
player is actually meant to look at — a hero sign keeps its frame rate from across the street while the
background stays cheap.

---

## 7. API overview

### Classes at a glance

| Class | Base | Purpose |
| --- | --- | --- |
| `ULumaSwarmSubsystem` | `UTickableWorldSubsystem` | The scheduler. Tiers, round-robin cursor, budget, epsilon, statistics |
| `ULumaSwarmProfile` | `UPrimaryDataAsset` | The reusable animation recipe. Stateless `Evaluate(Time, Phase)` |
| `ULumaSwarmComponent` | `UActorComponent` | Registers an actor's lights with the swarm. **Does not tick** |
| `ALumaSwarmSpawner` | `AActor` | Places a whole swarm from one actor (Grid / RandomInBox / AlongSpline) |
| `ULumaSwarmSettings` | `UDeveloperSettings` | Project Settings ▸ Plugins ▸ LumaSwarm |
| `ULumaSwarmStatics` | `UBlueprintFunctionLibrary` | Blueprint shortcuts to the subsystem |
| `FLumaSwarmStats` | `USTRUCT` | What the scheduler did on the last frame |
| `FLumaSwarmLightState` | `USTRUCT` | Result of evaluating a profile at one instant |
| `ELumaSwarmAnim` / `ELumaSwarmTier` / `ELumaSwarmLayout` / `ELumaSwarmLightType` | `UENUM` | Animation, distance bucket, layout, light class |

### `ULumaSwarmSubsystem`

```cpp
// Registration
void  RegisterLight(ULightComponent* Light, ULumaSwarmProfile* Profile,
                    float Phase = 0.f, float ImportanceBias = 0.f);
void  UnregisterLight(ULightComponent* Light, bool bRestoreBaseValues = true);
int32 RegisterActorLights(AActor* Actor, ULumaSwarmProfile* Profile,
                          float PhaseOffset = -1.f, float ImportanceBias = 0.f);
int32 UnregisterActorLights(AActor* Actor, bool bRestoreBaseValues = true);

// Runtime control
void  SetBudget(int32 InMaxUpdatesPerFrame);
int32 GetBudget() const;
void  SetMaxDistance(float InMaxDistance);
void  SetGlobalTimeDilation(float InTimeDilation);
float GetGlobalTimeDilation() const;
void  PauseSwarm(bool bInPaused);
bool  IsSwarmPaused() const;
void  RestoreAllLights();

// Introspection
const FLumaSwarmStats& GetStats() const;
int32 GetNumRegisteredLights() const;
void  SetShowStats(bool bInShowStats);
bool  IsShowingStats() const;

// Utility
static float DerivePhase(const UObject* Object, const FVector& Location);
```

All of them are `BlueprintCallable` or `BlueprintPure`. Get the subsystem with
`ULumaSwarmStatics::GetLumaSwarm(WorldContext)` or the standard **Get World Subsystem** node.

Registering the same component twice updates the existing entry rather than duplicating it, so calling
`RegisterActorLights` again after a profile swap is safe.

The subsystem exists in `Game`, `PIE` and `Editor` worlds. In an editor world it ticks only while
`bAnimateInEditor` is on — that is what makes a placed spawner animate in the viewport without PIE.

### `ULumaSwarmProfile`

```cpp
UFUNCTION(BlueprintPure)
FLumaSwarmLightState Evaluate(float TimeSeconds, float Phase) const;

bool HasIntensityShape() const;
bool HasColorCurve() const;
```

`FLumaSwarmLightState` carries `IntensityFactor`, `Color` and `bVisible`. You can call `Evaluate` yourself —
to drive an emissive material parameter in lockstep with a lamp, for instance — without registering anything.

### `ULumaSwarmComponent`

Properties: `Profile`, `PhaseOffset` (−1 = derive), `ImportanceBias`, `bAffectAllLightsOnActor`,
`ExplicitLights`.
Functions: `RegisterLights()`, `UnregisterLights(bool bRestoreBaseValues = true)`,
`SetProfile(ULumaSwarmProfile*)`, `GetNumManagedLights()`.

`ExplicitLights` wins over the automatic search when it is non-empty — that is how you animate three of an
actor's five lamps and leave the rest alone.

### `ALumaSwarmSpawner`

| Group | Properties |
| --- | --- |
| Layout | `Layout`, `Count`, `Extent`, `Spacing`, `SplineSideOffset`, `HeightOffset`, `Seed` |
| Lights | `LightType` (Point/Spot/Rect), `Radius`, `Intensity`, `SourceRadius`, `SpotOuterConeAngle`, `bCastShadows`, `Palette`, `Profiles`, `ImportanceBias` |
| Meshes | `bSpawnMeshes`, `LightMesh`, `MeshMaterial`, `MeshScale` — all instances live in one `UInstancedStaticMeshComponent`, so the fixtures cost a single draw call |
| Editor | `bAutoRebuildOnConstruction` |

Functions: `Rebuild()` and `ClearLights()` (both `CallInEditor` **and** `BlueprintCallable`),
`GetNumSpawnedLights()`, `GetSpline()`.

`bCastShadows` is off by default on purpose: several hundred shadow-casting dynamic lights is exactly the
case MegaLights exists for, and turning it on without MegaLights enabled will not end well.

### `ULumaSwarmStatics`

`GetLumaSwarm`, `RegisterLightWithProfile`, `UnregisterLightFromSwarm`, `GetLumaSwarmStats`.

### `FLumaSwarmStats`

`NumRegistered`, `NumNear`, `NumMid`, `NumFar`, `NumDormant`, `NumUpdatedThisFrame`,
`NumSkippedByEpsilon`, `LastUpdateMs`, `BudgetPerFrame`.

`NumSkippedByEpsilon` counts lights that were serviced, evaluated, and found to be exactly where they
already were — every one of those is a render-state update that did not happen. How large that number gets
depends entirely on your content: a scene full of slow pulses skips a lot, a scene full of 12 Hz strobes
skips almost nothing. Measure it in your own level rather than trusting a number from a store page.

---

## 8. Code examples

### Using the API from your own C++ module

`YourModule.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "LumaSwarm",
});
```

### Registering a light by hand

```cpp
#include "LumaSwarmSubsystem.h"
#include "LumaSwarmProfile.h"
#include "Components/PointLightComponent.h"

void AMyStreetLamp::BeginPlay()
{
    Super::BeginPlay();

    if (ULumaSwarmSubsystem* Swarm = GetWorld()->GetSubsystem<ULumaSwarmSubsystem>())
    {
        // -1 as the phase means "derive a stable one from name + position", which is what
        // RegisterActorLights does. RegisterLight itself takes the phase literally, so hash it here.
        const float Phase = ULumaSwarmSubsystem::DerivePhase(LampLight, LampLight->GetComponentLocation());
        Swarm->RegisterLight(LampLight, FlickerProfile, Phase, /*ImportanceBias=*/1.0f);
    }
}

void AMyStreetLamp::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ULumaSwarmSubsystem* Swarm = GetWorld()->GetSubsystem<ULumaSwarmSubsystem>())
    {
        Swarm->UnregisterLight(LampLight, /*bRestoreBaseValues=*/true);
    }
    Super::EndPlay(Reason);
}
```

Use `ULumaSwarmComponent` instead if you just want an actor's lights animated — it does exactly the above,
including the restore on EndPlay, without any code.

### Registering every light on an actor

```cpp
if (ULumaSwarmSubsystem* Swarm = ULumaSwarmStatics::GetLumaSwarm(this))
{
    const int32 NumAdded = Swarm->RegisterActorLights(SignActor, PulseProfile);
    UE_LOG(LogTemp, Display, TEXT("%d lights joined the swarm"), NumAdded);
}
```

### Scaling the budget with the platform

```cpp
void UMyGameInstance::ApplyLightQuality(int32 QualityLevel)
{
    ULumaSwarmSubsystem* Swarm = ULumaSwarmStatics::GetLumaSwarm(this);
    if (!Swarm)
    {
        return;
    }

    switch (QualityLevel)
    {
        case 0:  Swarm->SetBudget(24);  Swarm->SetMaxDistance(6000.f);  break;   // handheld / low
        case 1:  Swarm->SetBudget(64);  Swarm->SetMaxDistance(12000.f); break;   // console / medium
        default: Swarm->SetBudget(256); Swarm->SetMaxDistance(30000.f); break;   // desktop / high
    }
}
```

The budget is a ceiling, not a target: lowering it never breaks the animation, it only lowers how often each
individual lamp is refreshed. Nothing is dropped — the round-robin cursor keeps its place between frames.

### Reading the statistics

```cpp
const FLumaSwarmStats& Stats = Swarm->GetStats();
UE_LOG(LogTemp, Display, TEXT("%d registered | %d/%d written | %d skipped | %.3f ms"),
       Stats.NumRegistered, Stats.NumUpdatedThisFrame, Stats.BudgetPerFrame,
       Stats.NumSkippedByEpsilon, Stats.LastUpdateMs);
```

In Blueprint: **Get LumaSwarm Stats** (world context) ▸ break the struct ▸ bind the members to a UMG text
block. `BP_LumaSwarmDemoDirector` in the demo content does exactly this.

### Evaluating a profile without registering anything

```cpp
// Drive an emissive material in lockstep with a lamp that the scheduler owns.
const float SwarmClock = GetWorld()->GetTimeSeconds();
const float Phase      = ULumaSwarmSubsystem::DerivePhase(this, GetActorLocation());

const FLumaSwarmLightState State = Profile->Evaluate(SwarmClock, Phase);
DynamicMaterial->SetScalarParameterValue(TEXT("EmissiveScale"), State.IntensityFactor);
```

Because `Evaluate` is pure, this is safe to call from anywhere, any number of times, in any order.

### Swapping a profile at runtime (Blueprint or C++)

```cpp
// On the component: re-registers its lights so the change is immediate.
LampSwarmComponent->SetProfile(AlarmStrobeProfile);

// Or globally: a district-wide power failure.
Swarm->SetGlobalTimeDilation(0.15f);   // everything slows to a crawl
Swarm->PauseSwarm(true);               // ... and then freezes where it is
```

`PauseSwarm(true)` freezes the clock. Lights hold the last value they were written with; nothing is reset,
and `PauseSwarm(false)` picks the animation back up from where the clock left off.

---

## 9. Console commands and statistics

```
LumaSwarm.ShowStats 0|1        show / hide the on-screen statistics box
LumaSwarm.Budget <n>           set the hard per-frame write cap (no argument logs the current value)
LumaSwarm.MaxDistance <cm>     set the dormancy distance
LumaSwarm.Pause 0|1            freeze / resume the swarm clock
LumaSwarm.TimeDilation <scale> speed of the swarm clock (0 holds it still, 2 runs it double)
```

They apply to every world that has a swarm, so they behave the same typed during PIE or typed in the editor
with nothing playing.

The statistics box is drawn through `UDebugDrawService` on the `Game` flag, which means it appears in editor
viewports as well as in PIE — that is deliberate, so it is present in a level screenshot taken without ever
pressing Play. Each world's subsystem only draws into the viewport that is actually showing that world.

`stat LumaSwarm` adds the profiler group: a cycle counter for the scheduler tick plus counters for lights
updated, writes skipped by the epsilon, and the total number of registered lights.

Verifying the component really does not tick: `stat game` ▸ Tick Time, or the Unreal Insights tick list.
`ULumaSwarmComponent` never appears there, at any swarm size.

---

## 10. Demo content

Everything is under `Content/LumaSwarm/` (Fab single-pack-folder layout), mounted as `/LumaSwarm/LumaSwarm/`.

| Asset | What it shows |
| --- | --- |
| `Maps/L_LumaSwarmDemo` | Night city block, **634 registered lights**: a 520-light `RandomInBox` avenue swarm with instanced fixtures, 104 rooftop strobe beacons in a `Grid`, and 10 hand-placed lanterns using `ULumaSwarmComponent` |
| `Profiles/DA_LumaSwarm_NeonPulse` | `Pulse` — advertising signs |
| `Profiles/DA_LumaSwarm_BrokenTube` | `Flicker` — failing fluorescent tubes |
| `Profiles/DA_LumaSwarm_Beacon` | `Strobe` — rooftop warning lights |
| `Profiles/DA_LumaSwarm_ColorCycle` | `ColorCycle` — colour billboards, with an authored colour curve |
| `Blueprints/BP_LumaSwarmDemoDirector` | Puts up the HUD and drives `SetShowStats` / `SetBudget` |
| `Blueprints/BP_LumaSwarmLantern` | A point light plus a `ULumaSwarmComponent` — the hand-placed path |
| `UI/WBP_LumaSwarmHUD` | Four buttons: Budget 8 / 64 / 512 / Pause |
| `Materials/M_LumaSwarm{Ground,Building,Fixture}` | Dark street materials and the emissive fixture |

Open the map, press Play, and use the buttons: at Budget 512 the box reports the scheduler servicing around a
hundred lights per frame (that is all that is *due*); at Budget 8 it reports 8/8 and the animation keeps
running, just resolved more coarsely; Pause freezes it without any light snapping to a wrong value.

The demo map ships with MegaLights **disabled** in its post-process volume, because it has to open on
machines without hardware ray tracing. LumaSwarm does not care either way — it schedules writes to lights and
never touches the render path. Turn MegaLights on in your own project if your hardware allows it; the two are
complementary, not alternatives.

---

## 11. Networking

There is no replication, and that is the design rather than an omission.

A light's value is a pure function of the swarm clock and the light's phase, and the phase is derived from
the light's name and world position. Every client running the same level therefore computes the same
animation from the same inputs without a single byte crossing the wire. If you need the server to change
something — swap a profile, pause the swarm, dim a district — replicate *that* decision, which is a handful
of bytes, and let each client's scheduler do the work.

What this does not give you is frame-exact agreement between clients whose swarm clocks have drifted apart
(the clock starts when the world does). For set dressing this does not matter. For anything a player could
be judged on, do not drive it from a light animation in the first place.

---

## 12. Limits — read this before buying

* **LumaSwarm renders nothing.** It schedules updates to lights the engine renders. It is not a renderer, not
  a replacement for MegaLights or Lumen, and it does not make any individual light cheaper to draw.
* **It does not replace light functions or material effects.** Animated gobos, projected patterns and
  emissive material tricks are a different tool; use them alongside this, not instead of it.
* **Lights must be Movable.** The engine refuses runtime intensity and colour changes on static and
  stationary lights. The plugin warns once per world when it is handed one.
* **No audio coupling.** Competing flicker kits tie their animation to MetaSounds. That is a different
  product and is deliberately left out here.
* **No editor utility widgets, no bulk light editor.** This is a runtime system.
* **Cached positions are refreshed lazily.** A light's position is re-read during the staggered
  re-classification pass, so a light that moves very fast may be bucketed by a slightly stale position for a
  few frames. Its tier is wrong by one bucket at worst; the animation is unaffected.
* **The budget is per world.** Two worlds ticking at once (editor plus PIE) each get their own.
* **No performance figures are quoted here.** How much the tiers and the epsilon save depends entirely on
  your content. The statistics box exists so you can measure it in your own level.

---

## 13. Troubleshooting

**Nothing animates.**
Check the lights are Movable, check the profile is assigned, check `LumaSwarm.ShowStats 1` reports a
non-zero `Registered`. If `Registered` is 0, nothing ever called `RegisterLight` — the component only
registers at BeginPlay, and the spawner only registers after a `Rebuild`.

**The stats box says everything is Dormant.**
Your camera is further from the lights than `MaxDistance`. Raise it with `LumaSwarm.MaxDistance 50000`.

**Lights stay at a strange brightness after stopping PIE.**
That should not happen — `EndPlay` and `Deinitialize` both restore base values. If you see it, something
destroyed the subsystem without a proper teardown; `RestoreAllLights()` fixes it by hand.

**The animation looks chunky on distant lights.**
That is the tier system working as intended. Lower `MidUpdateInterval` / `FarUpdateInterval`, push out
`NearDistance`, or raise the budget.

**Lights near a tier boundary pump.**
Raise `TierHysteresis`.

**The spawner places nothing in AlongSpline mode.**
Its spline needs at least two points. Select the spawner, select the spline component, and add some.

**Nothing animates in the editor viewport without PIE.**
Check `bAnimateInEditor` in Project Settings ▸ Plugins ▸ LumaSwarm, and make sure the spawner has been
rebuilt at least once (`Rebuild` in the Details panel, or `bAutoRebuildOnConstruction`).

**The spawner's lights vanished after reloading the map.**
That is expected: spawned lights are transient and regenerated from `Seed` in `OnConstruction`, which is why
a 2,000-light spawner costs nothing in your saved map. If they do not come back, `bAutoRebuildOnConstruction`
is off — press **Rebuild**.

---

Support: see `SupportURL` in `LumaSwarm.uplugin`.

Copyright 2026 Silvan Teufel. All Rights Reserved.
