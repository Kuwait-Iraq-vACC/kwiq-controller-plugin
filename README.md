# KWIQ Controller Plugin

Kuwait & Iraq vACC EuroScope plugin: VCH, event routes and slots, FPC and SRD search. Route data comes from Core (Laravel).

## Build the DLL (GitHub Actions)
1. Set the three Core URLs near the top of `KWIQPlugin.cpp`.
2. Push this folder to your private repo (branch `main`).
3. Open **Actions > build**. It runs the logic tests, then builds the 32-bit `KWIQPlugin.dll` (loader) and `KWIQPluginCore.dll` (the plugin) on a Windows runner (about 5-10 minutes the first time, faster once vcpkg is cached).
4. Open the finished run and download the **KWIQPlugin** artifact. Every build on `main` is also published as a release, versioned `KWIQ_PLUGIN_VERSION_BASE` plus the run number (e.g. `v0.5.0.42`), which Core then offers to controllers. Each pull request gets a test pre-release (`pr-<number>`), replaced on every push and removed when the pull request is closed; running the workflow by hand on another branch makes one named after the branch. Core never offers pre-releases. Change `KWIQ_PLUGIN_VERSION_BASE` in `KWIQPlugin.cpp` for a new minor or major version.

## Load it in EuroScope
Put `KWIQPlugin.dll` and `KWIQPluginCore.dll` in the same folder (myKWIQ gives both in `KWIQPlugin.zip`), then Plugins > Load > pick `KWIQPlugin.dll` only. VCH is built in: do not load VCH separately.

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
`.kwiq connect` (get your API key from myKWIQ), `.kwiq key <key>` (enter it by hand; `.kwiq key` shows which is in use, `.kwiq key clear` removes it), `.kwiq button` (hide or show the KWIQ button), `.kwiq status`, `.kwiq refresh`, `.kwiq sector`, `.kwiq autorefresh`, `.kwiq update` (offer a pending plugin update), `.kwiq alert` (preview the sector file alert), `.kwiq version` (re-check the sector file), `.kwiq sectorfile <folder or .ese>` (tell the plugin where your sector file is; empty = automatic), plus VCH's own `.vch ...` commands.

## What Core controls
Only the Core address is compiled in. `/api/plugin/config` supplies: kill switch, minimum plugin version, message of the day, refresh intervals, tag texts and colours, sector indicator colour/default, and the current sector file AIRAC. Sector aliases, the SRD and events have their own endpoints. All of it is edited in Filament (Plugin settings, Sector aliases, SRD routes, Plugin events).

## Plugin updates
Each release carries `KWIQPluginCore.dll` and `KWIQPlugin.zip`. Core mirrors both from the newest *published* release (`php artisan plugin:sync-release`, every 5 minutes; set `plugin.github_repo`, plus `PLUGIN_GITHUB_TOKEN` if the repository is private). Updates happen only as EuroScope starts: `KWIQPlugin.dll` (loader.cpp) asks Core for the latest version before the plugin is loaded and, if there is a newer one, asks in a Windows message box. On Yes it downloads `KWIQPluginCore.dll` from Core, checks the SHA-256 Core published and that it is a 32-bit DLL, replaces the file and loads the new version straight away: nothing to restart. If Core cannot be reached the installed version is loaded. Nothing pops up during a session; `.kwiq update` says whether a newer version is waiting.

## Outdated sector file alert
When EuroScope starts and the local sector file is older than Core's version, an "Outdated Sector File" warning appears (a Windows-style box that does not take focus or stop EuroScope), its title bar flashes for `alert.flash_seconds` and `conflict_alert.wav` plays. It is only shown at start-up, never during a session. The sound is searched for in the sector file's package (`Sounds` folder, also one folder down), near the plugin, then in every package under `%APPDATA%\EuroScope` (newest wins); a system sound is the fallback. `.kwiq alert` previews it. `alert.enabled`, `alert.sound_file` and `alert.flash_seconds` are set in Core.

## Sector file version check
OKAC and ORBB are released separately, so Core holds a current AIRAC for each (`sectorfile.airac.okac` and `sectorfile.airac.orbb`, e.g. `2610/02`), filled by `php artisan sectorfile:sync` from `AIRAC_VERSION.txt` in each package's repository, falling back to the tag of its latest release. The plugin compares the loaded sector file with its own package's version: the package is taken from the sector file's path (the OKAC or ORBB folder or file nearest it), otherwise from the controller's callsign (OR... is ORBB, OK... is OKAC). The plugin finds the local version from `AIRAC_VERSION.txt` or the `AIRAC 2610/1` line in the `.ese` header, looking in the folder given with `.kwiq sectorfile`, otherwise in the plugin DLL's folder and its three parents, in each case also in its `Plugins` folder. The packages keep it in `OKAC\Plugins\AIRAC_VERSION.txt` and `ORBB\Plugins\AIRAC_VERSION.txt`, which is also where Core reads the current version from, so it works with no setup. "2610/2" and "2610/02" are treated as equal.

## Tests
`g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic`

## Licence
GPL-3.0-or-later. Read `NOTICE.md` before distributing the DLL.

## Personal API key and the KWIQ button
Home and visiting controllers get a personal API key from myKWIQ (EuroScope Plugin), where the plugin can also be downloaded. When there is no key (or Core refuses it), the plugin opens myKWIQ's Connect EuroScope page once per session; after signing in and clicking Connect EuroScope, the key comes straight back to the plugin through a listener on 127.0.0.1 that only accepts it with a one-off code, and the API KEY SETUP window closes. The same is opened by API Key in the KWIQ button's menu or `.kwiq connect`; `.kwiq key <key>` still works for pasting a key by hand. It is kept in `%APPDATA%\KWIQ\plugin-key.txt`, outside the EuroScope profile, and sent to Core as `X-KWIQ-Key` with the plugin version as `X-KWIQ-Version`. Core only refuses requests without a valid key once `api.require_key` is set to 1, so it can be switched on after everyone has a version that sends one. Staff can see and invalidate keys in Core under EuroScope Plugin > API Keys.

The KWIQ button is a small red button at the top centre of each radar screen. Click it for the plugin's menu: API key, sector indicators, slot auto refresh, event routes, SRD search, sector file and update checks, refresh and status. Drag it to move it; `.kwiq button` or Hide Button in the menu hides it, and `.kwiq button` brings it back.
