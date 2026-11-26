# Half Sword Performance Mod

Raises frame rates in Half Sword when many armoured fighters are on screen. Nothing about gameplay, physics or
combat changes. Every performance fix is on by default; the F9 menu turns each one off.

## What it does

- **Armour follows the body directly.** In the stock game, each worn armour piece runs its own animation to copy
  the body's pose every frame. A fully armoured fighter can have twenty of them. The mod makes each piece use the
  body's bones directly, so that work disappears.
- **Tighter armour bounds.** The stock game makes each armour piece's bounds ten times too big. The mod drops that,
  so armour behind the camera can be skipped.
- **Weapons lying still stop ticking.** Every weapon in the level runs code each frame for its swing and slide
  sounds, even one asleep on the floor. The mod pauses that on weapons at rest with nothing pending, and resumes it
  the moment one moves or is picked up.
- **Dead fighters lying still stop ticking.** A corpse runs the same per-frame code as a living fighter (about a
  millisecond each). The mod pauses it once the body has settled, and resumes it the moment the body is moved.
- **Tighter body bounds.** The stock game makes each fighter's body bounds a thousand times too big, so a body is
  never skipped, even behind the camera. The mod drops that.
- **Head and skeleton follow the body.** Like armour, they use the body's bones directly.
- **Armour out of ray tracing.** Armour is left out of ray traced reflections and lighting. This saves GPU time;
  armour then shows in no reflection. `armor_ray_tracing=1` puts it back.
- **No shadows from distant armour.** Armour on fighters farther than 15 m casts no shadow.
- **Skips the game mode's empty per-frame Blueprint.**
- **Cheaper blood, same look.** The blood snapshot each fighter keeps for saves is copied 4 times a second instead of
  every frame, and footsteps stop at the first floor vertex within 5 cm instead of searching the whole floor mesh.
- **Physics on more CPU threads, same result.** Physics solves each group of touching bodies on its own. The stock
  game only adds another thread for every 50 bodies, about two fighters' worth; the mod lowers that to 16, so a
  crowded fight is shared out among more threads. Each group is solved the same way on any thread.
- **Damage marker spheres skip overlap checks.** Each fighter carries about 230 small spheres the game uses to place
  wounds by distance. Spheres set up to check what they overlap as they move stop
  doing so (nothing in the game reads those checks) while the fighter's own body still makes them.
- **Optional: no blood or dent painting.** Off by default (blood stays); this is the game's own switch.
- **Frame time counters** on a key, and a performance line in `HSPerf.log` that also splits the CPU time between the
  game, render and RHI threads, and says when the frame is waiting on the GPU (or a frame cap) instead.
- **Blueprint profiler** (ported from HSMMod's): records which Blueprints and engine calls take the frame time.

It works together with Half Sword Multiplayer (HSMMod), HS Enhancer, UE4SS and ReShade.

## Install

1. In Steam, right-click Half Sword, choose **Manage → Browse local files**, and open `HalfswordUE5\Binaries\Win64`.
2. Copy `winhttp.dll` there, next to `HalfSwordUE5-Win64-Shipping.exe`.
3. Start the game. `HSPerf.ini` and `HSPerf.log` appear in the same folder.

On Linux or Steam Deck (Proton), also set the launch options to `WINEDLLOVERRIDES="winhttp=n,b" %command%`.
If you already have a `WINEDLLOVERRIDES` entry for another mod, put both in one, for example
`WINEDLLOVERRIDES="dwmapi,winhttp=n,b" %command%`.

To uninstall, delete `winhttp.dll`. You can also delete `HSPerf.ini` and `HSPerf.log`.

## Keys

| Key | Does |
|---|---|
| F9 | Settings menu (during a fight) |
| F10 | Frame time counters on or off |
| Ctrl + F10 | Armour, weapon, corpse, blood and physics fixes off or on, to compare with the stock game |
| Ctrl + Shift + F10 | Blueprint profiler: records up to 30 s (press again to stop early) |

## Settings menu

F9 opens the menu over the game during a fight: every setting below, the counters, the compare switch and the
profiler, with a live line of frame times and counts. Click a row, or use Up / Down and Enter / Left / Right. F9 or
Esc closes it. Changes apply at once and are saved to `HSPerf.ini`. The game keeps running behind the menu, but your
fighter gets no keys or mouse while it is open.

## Profiler

Ctrl + Shift + F10 starts a capture; `HSPerf.log` says when it starts and stops. It writes `HSPerfProfile-<date>-<time>.txt`
(the Blueprints and engine calls by time per frame, and the frame times) and a `.json` timeline next to the game exe.
Open the `.json` in [ui.perfetto.dev](https://ui.perfetto.dev) or `chrome://tracing`.

- `bp` rows are Blueprints the engine runs (ticks, hits, animation inputs). A Blueprint calling another one is counted
  inside the caller.
- `native` rows are engine functions a Blueprint calls (traces, component and math calls).
- It does not see animation, physics or rendering on other threads.
- Frames run slower while it records (it times every call); compare rows with each other, not with an uncaptured frame.
- A level change ends the capture.
- With HSMMod installed it does nothing: use HSMMod's own profiler (Ctrl+Shift+P), which also sees Blueprint-to-Blueprint calls.

## Settings (`HSPerf.ini`)

The F9 menu changes these in game. If you edit the file by hand, restart the game. `enabled`, the keys and
`log_seconds` only change on a restart. The menu writes the whole file again, so comments you add are not kept.
A file from 0.2.0 or older is updated on the first start: the settings whose default changed take the new default.

| Setting | Default | Meaning |
|---|---|---|
| `enabled` | 1 | 0 turns the whole mod off |
| `armor_leader_pose` | 1 | Armour follows the body's bones directly |
| `armor_bounds` | 1 | Armour drops the game's 10x bounds, so off-screen armour can be skipped |
| `shadow_trim` | 1 | Armour on far fighters casts no shadow |
| `shadow_trim_meters` | 15 | How far "far" is |
| `vertex_paint` | 1 | 0: no blood or dents painted on bodies and armour |
| `cheaper_blood` | 1 | Blood snapshot copied 4 times a second, footsteps' floor lookup stops within 5 cm (blood looks the same) |
| `idle_weapons` | 1 | Weapons lying still stop ticking until they move or are picked up |
| `corpse_sleep` | 1 | Dead fighters lying still stop ticking until something moves them |
| `armor_ray_tracing` | 0 | 0: armour is left out of ray traced reflections and lighting (saves GPU time); 1 as the game ships |
| `body_bounds` | 1 | The body drops the game's 1000x bounds, so off-screen fighters can be skipped |
| `head_leader_pose` | 1 | The head and skeleton meshes follow the body's bones directly |
| `physics_islands` | 1 | Physics shares groups of touching bodies out among more CPU threads (same result) |
| `island_min_per_worker` | 16 | Bodies or joints one thread takes before another joins in (the game uses 50) |
| `marker_spheres` | 1 | The damage marker spheres stop making overlap events nothing listens to |
| `skip_empty_gamemode_tick` | 1 | Skips the game mode's empty per-frame Blueprint (about a microsecond a frame) |
| `menu_key` | F9 | The settings menu key (`none` for no key) |
| `overlay_key` | F10 | The counters key (`none` for no key) |
| `log_seconds` | 10 | A performance line in `HSPerf.log` every N seconds (0 = never) |

## Building

The mod is built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and needs no Visual Studio. Run
`./build.sh` to build it into `build/winhttp.dll`, and `tests/run_tests.sh` to run the tests.

## License

MIT. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
