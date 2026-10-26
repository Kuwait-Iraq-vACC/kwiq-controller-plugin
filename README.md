# KWIQ Controller Plugin

Kuwait & Iraq vACC EuroScope plugin: VCH, event routes and slots, FPC and SRD search. Route data comes from Core (Laravel).

## Build the DLL (GitHub Actions)
1. Set the three Core URLs near the top of `KWIQPlugin.cpp`.
2. Push this folder to your private repo (branch `main`).
3. Open **Actions > build**. It runs the logic tests, then builds a 32-bit `KWIQPlugin.dll` on a Windows runner (about 5-10 minutes the first time, faster once vcpkg is cached).
4. Open the finished run and download the **KWIQPlugin** artifact. Pushing a tag such as `v0.3.0` also creates a draft release with the DLL attached.

## Load it in EuroScope
Plugins > Load > pick `KWIQPlugin.dll`. VCH is built in: do not load VCH separately.

## Tag items and functions
| Tag item type | Shows |
|---|---|
| KWIQ Controller Plugin / Event Route | green `OK` or red `CHK` on event days for event ADEP/ADES pairs |
| ... / Event Flag | `EV` if the pilot has a VATCAN slot |
| ... / Event Slot | the slot time |
| ... / FPC | `OK`, `RTE`, `MIN` or `MAX` against the SRD |
| ... / VCH items | Pending request, Hold Short, CTL, Reminder, and so on |

Functions: **Open event routes window** (left button on Event Route), **Explain FPC result** (left button on FPC), **Open SRD search window** (any item).
VCH items and functions keep their original names; see https://github.com/DrFreas/VCH for how to set them up.

## Commands
`.kwiq status`, `.kwiq refresh`, `.kwiq sector`, `.kwiq autorefresh`, `.kwiq update` (offer a pending plugin update), `.kwiq alert` (preview the sector file alert), `.kwiq version` (re-check the sector file), `.kwiq sectorfile <folder or .ese>` (tell the plugin where your sector file is; empty = automatic), plus VCH's own `.vch ...` commands.

## What Core controls
Only the Core address is compiled in. `/api/plugin/config` supplies: kill switch, minimum plugin version, message of the day, refresh intervals, tag texts and colours, sector indicator colour/default, and the current sector file AIRAC. Sector aliases, the SRD and events have their own endpoints. All of it is edited in Filament (Plugin settings, Sector aliases, SRD routes, Plugin events).

## Plugin updates
Core mirrors the DLL of the newest *published* GitHub release (`php artisan plugin:sync-release`, scheduled every 5 minutes; the build workflow creates the release as a draft, so review and publish it). Set `plugin.github_repo` in Plugin settings, plus `PLUGIN_GITHUB_TOKEN` if the repo is private. When Core advertises a newer version, the plugin shows a "PLUGIN UPDATE" popup (Update now / Later). Update now downloads in the background, checks the SHA-256 Core published and that the file is a 32-bit DLL, then swaps the file (the running DLL is renamed to `.old`, removed at next start). EuroScope is not interrupted. The new version loads the next time EuroScope starts: EuroScope cannot safely reload a plugin inside a running session. `.kwiq update` asks again.

## Outdated sector file alert
When a controller logs on (direct, proxy or sweatbox) and the local sector file is older than Core's version, a flashing red "OUTDATED SCT FILE" popup appears and `conflict_alert.wav` plays. It flashes between two colours for `alert.flash_seconds`, then stays solid; it never takes keyboard focus. The sound is searched for in the sector file's package (`Sounds` folder, also one folder down), near the plugin, then in every package under `%APPDATA%\EuroScope` (newest wins); a system beep is the fallback. `.kwiq alert` previews it. All alert settings are in Core (`alert.*`).

## Sector file version check
Core holds the current AIRAC (`sectorfile.airac`, e.g. `2610/02`), filled by `php artisan sectorfile:sync` from `AIRAC_VERSION.txt` in the sector file repository (see `docs/sector-file-release.yml`). The plugin finds the local version from `AIRAC_VERSION.txt` or the `AIRAC 2610/1` line in the `.ese` header, looking in the folder given with `.kwiq sectorfile`, otherwise in the plugin DLL's folder and its two parents. Ship the plugin and `AIRAC_VERSION.txt` in the same install package and it works with no setup. "2610/2" and "2610/02" are treated as equal.

## Tests
`g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic`

## Licence
GPL-3.0-or-later. Read `NOTICE.md` before distributing the DLL.
