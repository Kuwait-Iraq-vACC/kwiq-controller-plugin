# KWIQ Controller Plugin

Kuwait & Iraq vACC EuroScope plugin: VCH, event routes and slots, FPC and SRD search. Route data comes from Core (Laravel).

## Build the DLL (GitHub Actions)
1. Core's address is the only server compiled in: `CORE_BASE` in `KWIQPlugin.cpp` and `kCoreHost` in `kwiq_win.hpp`.
2. Push to `main`, or open a pull request.
3. **Actions > build** runs the logic tests, then builds three 32-bit DLLs on a Windows runner (about 5-10 minutes the first time, faster once vcpkg is cached): `KWIQPlugin.dll` (the loader), `KWIQPluginUpdater.dll` and `KWIQPluginCore.dll` (the plugin). KWIQ's own files are built at /W4, and the build fails if they have any compiler warning (the **Compiler warnings** step lists them); each KWIQ header is also compiled on its own.
4. Download them from the run's **KWIQPlugin** artifact. Every build on `main` is also published as a release with all three, versioned `KWIQ_PLUGIN_VERSION_BASE` plus the run number (e.g. `v0.6.0.42`), which Core then offers to controllers. Each pull request gets a test pre-release (`pr-<number>`), replaced on every push and removed when the pull request is closed; running the workflow by hand on another branch makes one named after the branch. Core never offers pre-releases. Change `KWIQ_PLUGIN_VERSION_BASE` in `version.h` for a new minor or major version.

## Changelog
Every release from `main` rebuilds https://docs.kuwait-iraqvacc.net/Controller%20Guidance/KWIQ%20Controller%20Plugin/Changelog/ from the release notes (the PR titles, so write them for controllers) and commits it to kwiq-documentation-site (the `changelog` job, which needs the `DOCS_REPO_TOKEN` secret: Contents read and write on that repository only). Pre-releases are never listed.

## Load it in EuroScope
Plugins > Load > pick `KWIQPlugin.dll` (in the OKAC and ORBB packages, or from myKWIQ). That is the only file to load: as in the UK Controller Plugin, it installs `KWIQPluginUpdater.dll` and `KWIQPluginCore.dll` in `%LOCALAPPDATA%\KWIQ\bin` and loads the plugin from there. VCH is built in: do not load VCH separately.

## Tag items and functions
| Tag item type | Shows |
|---|---|
| KWIQ Controller Plugin / Event Route | green `OK` or red `CHK` on event days for event ADEP/ADES pairs |
| ... / Event Flag | `EV` if the pilot has a VATCAN slot |
| ... / Event Slot | the slot time |
| ... / FPC | `OK`, `RTE`, `MIN` or `MAX` against the SRD |
| ... / Sector Indicator | the alias of the position tracking the aircraft (also drawn above it) |
| ... / RDF state | `!` while the aircraft transmits |
| ... / VCH items | Pending request, Hold Short, CTL, Reminder, and so on |

Functions: **Open event routes window** (left button on Event Route), **Explain FPC result** (left button on FPC), **Open SRD search window** (any item).
VCH items and functions keep their original names; see https://github.com/DrFreas/VCH for how to set them up.

## Commands
`.kwiq connect` (get your API key from myKWIQ), `.kwiq key <key>` (enter it by hand; `.kwiq key` says whether one is connected, `.kwiq key clear` removes it), `.kwiq button` (hide or show the KWIQ button), `.kwiq status`, `.kwiq refresh`, `.kwiq sector` (sector indicators on the radar on or off), `.kwiq autorefresh` (slot auto refresh on or off), `.kwiq update` (says whether a newer version is waiting), `.kwiq alert` (preview the sector file alert), `.kwiq version` (re-check the sector file), `.kwiq sectorfile <folder or AIRAC_VERSION.txt>` (tell the plugin where your package is; empty = automatic), `.rdf draw on|off|core` (your own RDF switch; `core` = as Core says), `.rdf refresh` (switch the channels off and sync them again from TrackAudio), `.rdf reload` (reconnect to TrackAudio), plus VCH's own `.vch ...` commands.

## What Core controls
Besides Core's address, only myKWIQ's Connect EuroScope page and the default EuroScope setup guide and changelog links are compiled in. `/api/plugin/latest` supplies the latest version, the checksums of the plugin and the updater, the kill switch and the changelog link. `/api/plugin/config` supplies: minimum plugin version, message of the day, refresh intervals, tag texts and colours, sector indicator colours/default, and the current sector file AIRAC. Sector aliases, RDF settings, the SRD and events have their own endpoints. All of it is edited in Filament (Plugin settings, Sector aliases, SRD routes, Plugin events).

## RDF and the TrackAudio bridge
RDF (Radio Direction Finder, by Kingfu Chan) is built in, as VCH is: do not load `RDFPlugin.dll` or afv-euroscope-bridge as well (the plugin says so and leaves RDF off if they are).
- **Direction finding:** a circle (or a line from the middle of the screen when it is off screen) on aircraft transmitting on your frequencies; red when more than one transmits at once. Middle-click shows the last transmission again.
- **Bridge:** when a station is set to RX or TX in TrackAudio (WebSocket on `127.0.0.1:49080`, 1.3.0 Beta 5 or later) or the AFV standalone client, the matching EuroScope voice channel follows, so frequencies are only set in one place. Only on a direct connection.
- **Settings from Core** (`/api/plugin/rdf`, one line per logon callsign pattern, the first match wins): `RDF|ORBB_*_CTR|draw=1|bridge=1|colour=#FFFFFF|colour_concurrent=#FF0000|radius=20|threshold=-1|precision=0|low_altitude=0|high_altitude=0|low_precision=0|high_precision=0|controllers=0|require_tx=0`. Keys left out take RDF's defaults; with no matching line, drawing is off and the bridge on. `rdf.endpoint` in `/config` is TrackAudio's address.
- **Controller:** RDF Drawing in the KWIQ menu or `.rdf draw on|off` overrides Core for themselves (kept in the profile as `kwiq_rdf_draw`, empty = as Core says; `.rdf draw core` goes back to Core). The menu's TrackAudio row shows the connection, which the plugin makes on its own: Connected, Version too old (before 1.3.0 Beta 5), AFV client (TrackAudio isn't connected but the AFV standalone client is), Not running, or Off (RDF didn't start, or Core has the bridge and drawing off). It isn't a switch: a click syncs the channels again (`.rdf refresh`) while connected, tries to connect now while not (leaving the channels as they are), and does nothing when off.
- **Messages:** once a session, 15 seconds after logging on, if TrackAudio isn't running (and no AFV client is), or is older than 1.3.0 Beta 5.

## Plugin updates
Installed and updated the way the UK Controller Plugin is:
- **Files.** The packages carry only `KWIQPlugin.dll`, the loader. It never changes itself: a new one comes with the next sector file packages. `KWIQPluginUpdater.dll` and `KWIQPluginCore.dll` are kept in `%LOCALAPPDATA%\KWIQ\bin`.
- **First start.** "Performing first time download" (OK / Cancel), then the loader downloads the updater, which installs the plugin.
- **Every start.** The loader runs the updater, which asks Core for the latest version (`/api/plugin/latest`, which needs no key). If there is a newer one it asks; No asks again, as an update is required, and a second No starts the plugin reduced: only the outdated sector file alert, the EuroScope version check and a reminder in the chat (as the plugin loads and at every logon). Nothing else works in that session: no tag items (VCH's included), KWIQ button or commands. After an update the controller is offered the changelog (`plugin.changelog_url` if it is an http(s) link, otherwise the built-in changelog page). The updater also keeps itself in step with Core's copy.
- **Downloads** (`/api/plugin/download/core`, `/api/plugin/download/updater`) are checked against `plugin.core_sha256` and `plugin.updater_sha256` and must be 32-bit DLLs. A file in use is renamed to `.old` and removed at the next start. Core mirrors all three files from the newest *published* release (`php artisan plugin:sync-release`, every 5 minutes; set `plugin.github_repo`, plus `PLUGIN_GITHUB_TOKEN` if the repository is private).
- **Kill switch.** With `plugin.enabled = 0` as EuroScope starts, the plugin is not loaded (an inactive stand-in says so in the chat). Switched off during a session, a running plugin keeps working until EuroScope closes.
- If Core cannot be reached, the installed version is loaded. Nothing pops up during a session; `.kwiq update` says whether a newer version is waiting.
- v0.5 and older kept the plugin in `%APPDATA%\KWIQ\plugin` and the key in `%APPDATA%\KWIQ\plugin-key.txt`: the new loader moves the key and removes the old install.

## Outdated sector file alert
Once logged on (direct, proxy or sweatbox) with a sector file older than Core's version, a red OUTDATED SCT FILE box is drawn in the middle of every radar screen until the sector file is updated or the controller logs off. It flashes between `alert.colour_a` and `alert.colour_b` for `alert.flash_seconds`, then stays solid, and `conflict_alert.wav` plays on a loop for as long as it is up. The sound is searched for only in the controller's own package: near the sector file (`Sounds` folder, also one folder down), then near `KWIQPlugin.dll`; a system sound is the fallback. `.kwiq alert` shows the box for 15 seconds. `alert.enabled`, `alert.sound_file`, `alert.flash_seconds` and the colours are set in Core.

## EuroScope version check
As EuroScope starts, the plugin reads the version of the running EuroScope from its .exe and compares it with `euroscope.versions` in Core (comma separated; currently `3.2.3.2`, also known as 3.2c (r2), and never 3.2.9 or higher; `3.2.3` and `3.2.3.0` are the same). On any other version an Incorrect EuroScope Version popup says so, and OK opens `euroscope.setup_url` (by default, and whenever Core's value isn't an http(s) link, the EuroScope setup guide on docs.kuwait-iraqvacc.net). It is checked once per session, as soon as Core's config arrives; if that is later than the first three minutes it is only said in the chat. Leave `euroscope.versions` empty to turn the check off. `.kwiq status` shows the EuroScope version.

## Sector file version check
OKAC and ORBB are released separately, so Core holds a current AIRAC for each (`sectorfile.airac.okac` and `sectorfile.airac.orbb`, e.g. `2610/02`), filled by `php artisan sectorfile:sync` from `AIRAC_VERSION.txt` in each package's repository, falling back to the tag of its latest release. The plugin compares the loaded sector file with its own package's version: the package is taken from the sector file's path (the OKAC or ORBB folder or file nearest it), otherwise from the controller's callsign (OR... is ORBB, OK... is OKAC). The plugin reads the local version only from `AIRAC_VERSION.txt`, which the packages keep in `OKAC\Plugins` and `ORBB\Plugins`: it looks in the folder given with `.kwiq sectorfile` (or that file's folder), otherwise in the plugin's folder and its three parents, each with its `Plugins` folder. Without that file the version is not checked and no alert is shown. "2610/2" and "2610/02" are treated as equal.

## Tests
`g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic`

## Licence
GPL-3.0-or-later. Read `NOTICE.md` before distributing the DLL.

## Personal API key and the KWIQ button
Home and visiting controllers, and anyone in training with the vACC, get a personal API key from myKWIQ (EuroScope Plugin), where the plugin can also be downloaded. Whenever there is no key, or Core refuses it as missing, invalid, revoked or expired, the plugin opens myKWIQ's Connect EuroScope page and an API Key Setup window: as EuroScope starts, whenever the controller connects to the network, or once they log off if Core refuses the key while they are logged on (they are told in the chat). The setup window cannot be dismissed or moved: it closes only once a key is connected. The key's ID is never shown in the plugin, only in Core. A key refused because the controller left the roster or is suspended is only reported. Only a 401 from Core counts as a refused key, so an outage never sends anyone to myKWIQ, and the last data downloaded stays in use. After the controller clicks Connect EuroScope, myKWIQ sends the browser to a listener on 127.0.0.1 with a one-off code (and the state the plugin opened the page with); the plugin swaps the code for the key at `/api/plugin/claim`, so the key never goes through the browser. The Connect EuroScope link is also put on the clipboard (never in the chat), in case no browser opens (EuroScope through Wine or CrossOver on a Mac or Linux hands it to the system browser; `.kwiq status` shows the Wine version). Help: discord.kuwait-iraqvacc.net. `.kwiq key <key>` still works for pasting a key by hand. It is kept in `%LOCALAPPDATA%\KWIQ\settings\api-settings.json` (`{"api-key": "..."}`, as UKCP keeps its own), outside the EuroScope profile, and sent to Core as `X-KWIQ-Key` with the plugin version as `X-KWIQ-Version`. Core only refuses requests without a valid key once `api.require_key` is set to 1; updates (`/api/plugin/latest` and the download) never need a key. Staff can see and invalidate keys in Core under EuroScope Plugin > API Keys.

The KWIQ button is a small red button at the top centre of each radar screen. Click it for the plugin's menu: API key, sector indicators, RDF drawing, the TrackAudio bridge, slot auto refresh, event routes, SRD search, sector file and update checks, refresh and status. Drag it to move it; `.kwiq button` or Hide Button in the menu hides it, and `.kwiq button` brings it back.

The sector indicator above each tracked aircraft is shown only for the positions in Core's Sector aliases. Each one is an ESE position ID, as in the ESE's `[POSITIONS]` (e.g. `BS` for `ORBB_S_CTR`), and the alias shown on the radar, at most 4 characters (e.g. `SR`); to show the ID itself, enter it as its own alias. Core sends them from `/api/plugin/sectors` as `SECTOR|<ID>|<alias>`. The plugin looks up the position EuroScope identifies the tracking controller as right now (by prefix, suffix and frequency), so a relief callsign or a controller covering a listed position shows that position's alias. A controller EuroScope does not identify gets no indicator. The indicator is drawn in the colour of the aircraft's tag, which Core sets per flight plan state, named as in EuroScope's Symbology: `ui.colour_sector_non_concerned`, `_notified`, `_information` (coordinated), `_transfer_to_me_initiated`, `_assumed` (also transfer from me initiated) and `_redundant`, each `#RRGGBB`. A state Core sets no colour for uses `ui.colour_sector`.