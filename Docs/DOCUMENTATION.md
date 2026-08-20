# LumaSwarm — Documentation

*Animated light swarms on a budget. Unreal Engine 5.8, runtime C++ plugin, no editor dependencies.*

---

## 1. Installation

1. Copy the `LumaSwarm` folder into your project's `Plugins/` directory (a Fab install does this for you).
2. Open the project. If prompted to rebuild the module, accept.
3. **Edit > Plugins > Rendering > LumaSwarm** — make sure it is enabled, then restart the editor.

The plugin ships exactly one runtime module. There is no editor module, no third-party dependency, and no
editor-only code path, so everything in here works in a cooked, shipping build.

Supported platforms: Win64, Mac, Linux.

---

## 2. What the plugin actually does

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
it needs no replication at all. See §8.

---

## 3. Quick start

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
2. **Add Component > LumaSwarm Component**.
3. Assign a `Profile`.
4. Leave `Phase Offset` at `-1` to derive a stable phase from each light's name and position, or set an
   explicit value in the range 0–1.

At BeginPlay the component hands its owner's lights to the subsystem and gets out of the way. At EndPlay it
takes them back and restores the intensity, colour and visibility they had on registration.

---

## 4. Profile assets

Create one with **Right-click in the Content Browser > Miscellaneous > Data Asset > LumaSwarm Profile**.

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

Three profiles that cover most of what a night street needs:

| Look | Anim | Rate | Min / Max | Noise |
| --- | --- | --- | --- | --- |
| Broken fluorescent tube | `Flicker` | 6–12 Hz | 0.05 / 1.0 | 0.5–0.8 |
| Advertising sign | `Pulse` | 0.4–1.0 Hz | 0.55 / 1.0 | 0 |
| Colour billboard | `ColorCycle` | 0.15 Hz | 0.9 / 1.0 | 0 |

---

## 5. Budget tuning

Everything below lives in **Project Settings > Plugins > LumaSwarm** and can be overridden at runtime.

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

## 6. API overview

### `ULumaSwarmSubsystem` (`UTickableWorldSubsystem`)

```cpp
void  RegisterLight(ULightComponent* Light, ULumaSwarmProfile* Profile, float Phase, float ImportanceBias);
void  UnregisterLight(ULightComponent* Light, bool bRestoreBaseValues = true);
int32 RegisterActorLights(AActor* Actor, ULumaSwarmProfile* Profile, float PhaseOffset = -1.f, float ImportanceBias = 0.f);
int32 UnregisterActorLights(AActor* Actor, bool bRestoreBaseValues = true);

void  SetBudget(int32 MaxUpdatesPerFrame);
void  SetMaxDistance(float MaxDistance);
void  SetGlobalTimeDilation(float TimeDilation);
void  PauseSwarm(bool bPaused);
void  SetShowStats(bool bShowStats);
void  RestoreAllLights();

const FLumaSwarmStats& GetStats() const;
static float DerivePhase(const UObject* Object, const FVector& Location);
```

All of them are `BlueprintCallable`. Get the subsystem with
`ULumaSwarmStatics::GetLumaSwarm(WorldContext)` or the standard **Get World Subsystem** node.

### `FLumaSwarmStats`

`NumRegistered`, `NumNear`, `NumMid`, `NumFar`, `NumDormant`, `NumUpdatedThisFrame`,
`NumSkippedByEpsilon`, `LastUpdateMs`, `BudgetPerFrame`.

`NumSkippedByEpsilon` counts lights that were serviced, evaluated, and found to be exactly where they
already were — every one of those is a render-state update that did not happen. How large that number gets
depends entirely on your content: a scene full of slow pulses skips a lot, a scene full of 12 Hz strobes
skips almost nothing. Measure it in your own level rather than trusting a number from a store page.

### `ULumaSwarmStatics`

`GetLumaSwarm`, `RegisterLightWithProfile`, `UnregisterLightFromSwarm`, `GetLumaSwarmStats`.

### Stat group

`stat LumaSwarm` shows the scheduler's cycle counter plus counters for lights updated, writes skipped by
the epsilon, and the total number of registered lights.

---

## 7. Console commands

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

`LumaSwarm.Pause 1` freezes the clock; lights hold the last value they were written with, nothing is reset,
and `LumaSwarm.Pause 0` picks the animation back up from where the clock left off.

---

## 8. Networking

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

## 9. Limits — read this before buying

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

---

## 10. Troubleshooting

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

---

Copyright 2026 Silvan Teufel. All Rights Reserved.
