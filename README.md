# Half Sword Performance Mod

Raises frame rates in Half Sword when many armoured fighters are on screen. Nothing about gameplay, physics or
combat changes.

## What it does

- **Armour follows the body directly.** In the stock game, each worn armour piece runs its own animation to copy
  the body's pose every frame. A fully armoured fighter can have twenty of them. The mod makes each piece use the
  body's bones directly, so that work disappears.
- **Optional: tighter armour bounds.** The stock game makes each armour piece's bounds ten times too big. With
  `armor_bounds=1` the mod drops that, so armour behind the camera can be skipped. Off by default while it is checked.
- **Weapons lying still stop ticking.** Every weapon in the level runs code each frame for its swing and slide
  sounds, even one asleep on the floor. The mod pauses that on weapons at rest with nothing pending, and resumes it
  the moment one moves or is picked up.
- **Optional: no shadows from distant armour.** Off by default.
- **Optional: no blood or dent painting.** Off by default; this is the game's own switch.
- **Frame time counters** on a key, and a performance line in `HSPerf.log`.
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
| Ctrl + F10 | Armour and weapon fixes off or on, to compare with the stock game |
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

| Setting | Default | Meaning |
|---|---|---|
| `enabled` | 1 | 0 turns the whole mod off |
| `armor_leader_pose` | 1 | Armour follows the body's bones directly |
| `armor_bounds` | 0 | Armour drops the game's 10x bounds, so off-screen armour can be skipped (being checked) |
| `shadow_trim` | 0 | 1: armour on far fighters casts no shadow |
| `shadow_trim_meters` | 15 | How far "far" is |
| `vertex_paint` | 1 | 0: no blood or dents painted on bodies and armour |
| `idle_weapons` | 1 | Weapons lying still stop ticking until they move or are picked up |
| `skip_empty_gamemode_tick` | 0 | 1: skips the game mode's empty per-frame Blueprint (about a microsecond a frame) |
| `menu_key` | F9 | The settings menu key (`none` for no key) |
| `overlay_key` | F10 | The counters key (`none` for no key) |
| `log_seconds` | 10 | A performance line in `HSPerf.log` every N seconds (0 = never) |

## Building

The mod is built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and needs no Visual Studio. Run
`./build.sh` to build it into `build/winhttp.dll`, and `tests/run_tests.sh` to run the tests.

## License

MIT. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
