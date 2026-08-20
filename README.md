# LumaSwarm — Animated Light Swarms on a Budget

Animate hundreds to thousands of dynamic lights with a hard CPU budget per frame.

UE 5.8's MegaLights made it affordable to *render* hundreds of shadowed dynamic lights. It did not make
it any cheaper to *change* them: every `SetIntensity` or `SetLightColor` marks a light's render state dirty
and enqueues a render-thread command. Eight hundred lamps animating themselves means eight hundred of those
every single frame, on the game thread.

LumaSwarm turns that around. Lights stop ticking. A single world subsystem holds the list, sorts it by
distance to the camera, and each frame writes **at most `MaxUpdatesPerFrame` lights** — round-robin, so
everything comes round eventually and no frame ever pays for all of them at once.

## Quick start

1. Enable the plugin and restart the editor.
2. Drop a **LumaSwarm Spawner** into your level.
3. Set `Count`, pick a `Layout`, assign one or more **LumaSwarm Profile** assets, press **Rebuild**.
4. Press Play. Type `LumaSwarm.ShowStats 1` in the console to watch the scheduler work.

Already have lights placed by hand? Add a **LumaSwarm Component** to the actor that owns them, give it a
profile, and they join the swarm at BeginPlay.

## What is in the box

| Class | Purpose |
| --- | --- |
| `ULumaSwarmProfile` | Data asset: the animation recipe (Steady / Flicker / Pulse / Strobe / ColorCycle) |
| `ULumaSwarmComponent` | Registers an actor's lights with the swarm. Does not tick. |
| `ULumaSwarmSubsystem` | The scheduler: tiers, round-robin cursor, budget, epsilon, statistics |
| `ULumaSwarmSettings` | Project Settings > Plugins > LumaSwarm |
| `ALumaSwarmSpawner` | Places a whole swarm from one actor (Grid / RandomInBox / AlongSpline) |
| `ULumaSwarmStatics` | Blueprint shortcuts |

## Console commands

```
LumaSwarm.ShowStats 0|1        on-screen statistics box
LumaSwarm.Budget <n>           hard per-frame write cap
LumaSwarm.MaxDistance <cm>     distance beyond which lights go dormant
LumaSwarm.Pause 0|1            freeze / resume the swarm clock
LumaSwarm.TimeDilation <s>     speed of the swarm clock
```

Full documentation: [`Docs/DOCUMENTATION.md`](Docs/DOCUMENTATION.md).

---

Copyright 2026 Silvan Teufel. All Rights Reserved.
