# KWIQ Controller Plugin

Kuwait & Iraq vACC EuroScope plugin: VCH, event routes and slots, FPC and SRD search. Route data comes from Core (Laravel).

## Build the DLL (GitHub Actions)
1. Set the three Core URLs near the top of `KWIQPlugin.cpp`.
2. Push this folder to your private repo (branch `main`).
3. Open **Actions > build**. It runs the logic tests, then builds the 32-bit `KWIQPlugin.dll` (loader) and `KWIQPluginCore.dll` (the plugin) on a Windows runner (about 5-10 minutes the first time, faster once vcpkg is cached).
4. Open the finished run and download the **KWIQPlugin** artifact. Every build on `main` is also published as a release, versioned `KWIQ_PLUGIN_VERSION_BASE` plus the run number (e.g. `v0.5.0.42`), which Core then offers to controllers. Each pull request gets a test pre-release (`pr-<number>`), replaced on every push and removed when the pull request is closed; running the workflow by hand on another branch makes one named after the branch. Core never offers pre-releases. Change `KWIQ_PLUGIN_VERSION_BASE` in `KWIQPlugin.cpp` for a new minor or major version.

## Changelog
Every release from `main` rebuilds https://docs.kuwait-iraqvacc.net/Controller%20Guidance/KWIQ%20Controller%20Plugin/Changelog/ from the release notes (the PR titles, so write them for controllers) and commits it to kwiq-documentation-site (the `changelog` job, which needs the `DOCS_REPO_TOKEN` secret: Contents read and write on that repository only). Pre-releases are never listed.

## Load it in EuroScope
Plugins > Load > pick `KWIQPlugin.dll` (from myKWIQ, or the build). That is the only file: the plugin itself, `KWIQPluginCore.dll`, is built into it and kept in `%APPDATA%\KWIQ\plugin`. VCH is built in: do not load VCH separately.

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
`.kwiq connect` (get your API key from myKWIQ), `.kwiq key <key>` (enter it by hand; `.kwiq key` shows which is in use, `.kwiq key clear` removes it), `.kwiq button` (hide or show the KWIQ button), `.kwiq status`, `.kwiq refresh`, `.kwiq sector`, `.kwiq autorefresh`, `.kwiq update` (offer a pending plugin update), `.kwiq alert` (preview the sector file alert), `.kwiq version` (re-check the sector file), `.kwiq sectorfile <folder or AIRAC_VERSION.txt>` (tell the plugin where your package is; empty = automatic), plus VCH's own `.vch ...` commands.

## What Core controls
Only the Core address is compiled in. `/api/plugin/config` supplies: kill switch, minimum plugin version, message of the day, refresh intervals, tag texts and colours, sector indicator colours/default, and the current sector file AIRAC. Sector aliases, the SRD and events have their own endpoints. All of it is edited in Filament (Plugin settings, Sector aliases, SRD routes, Plugin events).

## Plugin updates
Each release carries one file, `KWIQPlugin.dll`, which controllers download from myKWIQ and installed copies update from. Core mirrors both from the newest *published* release (`php artisan plugin:sync-release`, every 5 minutes; set `plugin.github_repo`, plus `PLUGIN_GITHUB_TOKEN` if the repository is private). Updates happen only as EuroScope starts: `KWIQPlugin.dll` (loader.cpp) puts the plugin built into it in `%APPDATA%\KWIQ\plugin` if that copy is missing or older, asks Core for the latest version before the plugin is loaded and, if there is a newer one, asks in a Windows message box. On Yes it downloads the latest `KWIQPlugin.dll` from Core, checks the SHA-256 Core published and that it is a 32-bit DLL, takes the plugin out of it into `%APPDATA%\KWIQ\plugin`, replaces itself for the next start, and loads the new version straight away: nothing to restart. Core publishes it at `/api/plugin/download/loader` with its SHA-256 as `plugin.loader_sha256`. If Core cannot be reached the installed version is loaded. Nothing pops up during a session; `.kwiq update` says whether a newer version is waiting.

## Outdated sector file alert
Once logged on (direct, proxy or sweatbox) with a sector file older than Core's version, a red OUTDATED SCT FILE box is drawn in the middle of every radar screen until the sector file is updated or the controller logs off. It flashes between `alert.colour_a` and `alert.colour_b` for `alert.flash_seconds`, then stays solid, and `conflict_alert.wav` plays on a loop for as long as it is up. The sound is searched for in the sector file's package (`Sounds` folder, also one folder down), near the plugin, then in every package under `%APPDATA%\EuroScope` (newest wins); a system sound is the fallback. `.kwiq alert` shows the box for 15 seconds. `alert.enabled`, `alert.sound_file`, `alert.flash_seconds` and the colours are set in Core.

## EuroScope version check
As EuroScope starts, the plugin reads the version of the running EuroScope from its .exe and compares it with `euroscope.versions` in Core (comma separated; currently `3.2.3.2`, also known as 3.2c (r2), and never 3.2.9 or higher; `3.2.3` and `3.2.3.0` are the same). On any other version an Incorrect EuroScope Version popup says so, and OK opens `euroscope.setup_url` (by default the EuroScope setup guide on docs.kuwait-iraqvacc.net). It is checked once per session, as soon as Core's config arrives; if that is later than the first three minutes it is only said in the chat. Leave `euroscope.versions` empty to turn the check off. `.kwiq status` shows the EuroScope version.

## Sector file version check
OKAC and ORBB are released separately, so Core holds a current AIRAC for each (`sectorfile.airac.okac` and `sectorfile.airac.orbb`, e.g. `2610/02`), filled by `php artisan sectorfile:sync` from `AIRAC_VERSION.txt` in each package's repository, falling back to the tag of its latest release. The plugin compares the loaded sector file with its own package's version: the package is taken from the sector file's path (the OKAC or ORBB folder or file nearest it), otherwise from the controller's callsign (OR... is ORBB, OK... is OKAC). The plugin reads the local version only from `AIRAC_VERSION.txt`, which the packages keep in `OKAC\Plugins` and `ORBB\Plugins`: it looks in the folder given with `.kwiq sectorfile` (or that file's folder), otherwise in the plugin's folder and its three parents, each with its `Plugins` folder. Without that file the version is not checked and no alert is shown. "2610/2" and "2610/02" are treated as equal.

## Tests
`g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic`

## Licence
GPL-3.0-or-later. Read `NOTICE.md` before distributing the DLL.

## Personal API key and the KWIQ button
Home and visiting controllers, and anyone in training with the vACC, get a personal API key from myKWIQ (EuroScope Plugin), where the plugin can also be downloaded. Whenever there is no key, or Core refuses it as missing, invalid, revoked or expired, the plugin opens myKWIQ's Connect EuroScope page and an API Key Setup window: as EuroScope starts, whenever the controller connects to the network, or once they log off if Core refuses the key while they are logged on (they are told in the chat). The setup window cannot be dismissed or moved: it closes only once a key is connected. The key's ID is never shown in the plugin, only in Core. A key refused because the controller left the roster or is suspended is only reported. Only a 401 from Core counts as a refused key, so an outage never sends anyone to myKWIQ, and the last data downloaded stays in use. After the controller clicks Connect EuroScope, myKWIQ sends the browser to a listener on 127.0.0.1 with a one-off code (and the state the plugin opened the page with); the plugin swaps the code for the key at `/api/plugin/claim`, so the key never goes through the browser. The Connect EuroScope link is also put on the clipboard (never in the chat), in case no browser opens (EuroScope through Wine or CrossOver on a Mac or Linux hands it to the system browser; `.kwiq status` shows the Wine version). Help: discord.kuwait-iraqvacc.net. `.kwiq key <key>` still works for pasting a key by hand. It is kept in `%APPDATA%\KWIQ\plugin-key.txt`, outside the EuroScope profile, and sent to Core as `X-KWIQ-Key` with the plugin version as `X-KWIQ-Version`. Core only refuses requests without a valid key once `api.require_key` is set to 1; updates (`/api/plugin/latest` and the download) never need a key. Staff can see and invalidate keys in Core under EuroScope Plugin > API Keys.

The KWIQ button is a small red button at the top centre of each radar screen. Click it for the plugin's menu: API key, sector indicators, slot auto refresh, event routes, SRD search, sector file and update checks, refresh and status. Drag it to move it; `.kwiq button` or Hide Button in the menu hides it, and `.kwiq button` brings it back.

The sector indicator above each tracked aircraft is shown only for the positions in Core's Sector aliases. Each one is an ESE position ID, as in the ESE's `[POSITIONS]` (e.g. `BS` for `ORBB_S_CTR`), and the alias shown on the radar, at most 4 characters (e.g. `SR`); to show the ID itself, enter it as its own alias. Core sends them from `/api/plugin/sectors` as `SECTOR|<ID>|<alias>`. The plugin looks up the position EuroScope identifies the tracking controller as right now (by prefix, suffix and frequency), so a relief callsign or a controller covering a listed position shows that position's alias. A controller EuroScope does not identify gets no indicator. The indicator is drawn in the colour of the aircraft's tag, which Core sets per flight plan state, named as in EuroScope's Symbology: `ui.colour_sector_non_concerned`, `_notified`, `_information` (coordinated), `_transfer_to_me_initiated`, `_assumed` (also transfer from me initiated) and `_redundant`, each `#RRGGBB`. A state Core sets no colour for uses `ui.colour_sector`.