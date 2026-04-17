# BetterDrone — StarRupture Client Plugin

Extends the building drone limits in [StarRupture](https://store.steampowered.com/app/1631270/StarRupture/) beyond their default values, configurable via an INI file without touching game files.

**Target:** Game client only

---

## What It Does

Patches the game's drone developer settings CDO at runtime to apply your configured values each session:

| Setting | Description |
|---|---|
| Speed | Movement speed of the building drone (cm/s) |
| Max Radius | Maximum horizontal operating range (cm) |
| Max Height | Maximum vertical operating range (cm) |
| Max Rail Length | Maximum rail length (cm) |

The warning thresholds (the visual indicators before hitting the limit) are automatically adjusted to 95% of the configured max values.

---

## Configuration

Config is stored in `Plugins\config\BetterDrone.ini` and is generated on first launch with the game's own default values pre-filled.

| Key | Description |
|---|---|
| `SpeedPerSec` | Movement speed in cm/s (max `4000`) |
| `MaxRadius` | Horizontal range in cm (max `1000000`) |
| `MaxHeight` | Vertical range in cm (max `100000`) |
| `MaxRailLength` | Rail length in cm (max `100000`) |

Changes to the INI are applied live — no restart required.

---

## Installation

1. Download the latest release ZIP from the [Releases](../../releases) page:
   - `BetterDrone-Client-*.zip`

2. Extract into your game's `Binaries\Win64\` folder. The ZIP contains a `Plugins\` folder — it will sit alongside your existing `dwmapi.dll`.

3. After the first launch, edit `Plugins\config\BetterDrone.ini` and set your desired values.

> **Requires [StarRupture-ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader)** to be installed first.

---

## Troubleshooting

| Problem | Solution |
|---|---|
| Values not changing | Check `modloader.log` in `Binaries\Win64\` for errors. |
| Plugin not loading | Confirm ModLoader is installed and `BetterDrone.dll` is in `Plugins\`. |

---

## Building from Source

Requires Visual Studio 2022 and the [StarRupture-Plugin-SDK](https://github.com/AlienXAXS/StarRupture-Plugin-SDK).

Clone the repo, open `BetterDrone.sln`, and build the `Client Release|x64` configuration. The output DLL will be placed in `build\Client Release\Plugins\`.

---

## Disclaimer

Use at your own risk. The authors are not responsible for any damage caused by using this software.
