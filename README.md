# Half Sword Performance Mod

Raises frame rates in Half Sword when many armoured fighters are on screen. Nothing about gameplay, physics or
combat changes.

## What it does

- **Armour follows the body directly.** In the stock game, each worn armour piece runs its own animation to copy
  the body's pose every frame. A fully armoured fighter can have twenty of them. The mod makes each piece use the
  body's bones directly, so that work disappears.
- **Off-screen armour is skipped.** The stock game makes each armour piece's bounds ten times too big, so armour
  behind the camera still draws and casts shadows. The mod gives armour the body's bounds.
- **Optional: no shadows from distant armour.** Off by default.
- **Optional: no blood or dent painting.** Off by default; this is the game's own switch.
- **Frame time counters** on a key, and a performance line in `HSPerf.log`.

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
| F10 | Frame time counters on or off |
| Ctrl + F10 | Armour fixes off or on, to compare with the stock game |

## Settings (`HSPerf.ini`)

Restart the game after you change a setting.

| Setting | Default | Meaning |
|---|---|---|
| `enabled` | 1 | 0 turns the whole mod off |
| `armor_leader_pose` | 1 | Armour follows the body's bones directly |
| `armor_bounds` | 1 | Armour uses the body's bounds, so off-screen armour is skipped |
| `shadow_trim` | 0 | 1: armour on far fighters casts no shadow |
| `shadow_trim_meters` | 15 | How far "far" is |
| `vertex_paint` | 1 | 0: no blood or dents painted on bodies and armour |
| `overlay_key` | F10 | The counters key (`none` for no key) |
| `log_seconds` | 10 | A performance line in `HSPerf.log` every N seconds (0 = never) |

## Building

The mod is built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and needs no Visual Studio. Run
`./build.sh` to build it into `build/winhttp.dll`, and `tests/run_tests.sh` to run the tests.

## License

MIT. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
