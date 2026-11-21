# KWIQ Controller Plugin

Kuwait & Iraq vACC EuroScope plugin: VCH, event routes and slots, flight plan check (FPC), SRD check and SRD search. Route data comes from Core (Laravel).

## Build the DLL (GitHub Actions)
1. Core's address is the only server compiled in: `CORE_BASE` in `src/plugin/KWIQPlugin.cpp` and `kCoreHost` in `src/utils/kwiq_win.hpp`.
2. Push to `main`, or open a pull request.
3. **Actions > build** runs the logic tests, then builds three 32-bit DLLs on a Windows runner (about 5-10 minutes the first time, faster once vcpkg is cached): `KWIQPlugin.dll` (the loader), `KWIQPluginUpdater.dll` and `KWIQPluginCore.dll` (the plugin). KWIQ's own files are built at /W4, and the build fails if they have any compiler warning (the **Compiler warnings** step lists them); each KWIQ header is also compiled on its own.
4. Download them from the run's **KWIQPlugin** artifact. A push to `main` also publishes a release with all three, which Core then offers to controllers, but only once per version (see below). Each pull request gets a test pre-release (`pr-<number>`), replaced on every push and removed when the pull request is closed; running the workflow by hand on another branch makes one named after the branch. Core never offers pre-releases.

## Repository layout
Laid out like the [UK Controller Plugin](https://github.com/VATSIM-UK/uk-controller-plugin):

| Folder | What's in it |
| --- | --- |
| `src/plugin/` | `KWIQPluginCore.dll`: `KWIQPlugin.cpp`, plus one folder per feature (`ui/`, `fpc/`, `squawk/`, `rdf/`, `discord/`, `push/`, `keylink/`) and the vendored code (`vch/` for VCH, `slots/` for the Slots-Plugin) |
| `src/loader/` | `KWIQPlugin.dll`, the loader EuroScope loads |
| `src/updater/` | `KWIQPluginUpdater.dll` |
| `src/utils/` | Headers shared by all three DLLs and the tests: `kwiq_logic.hpp`, `kwiq_win.hpp`, `version.h` |
| `resource/` | `kwiq.rc`, its IDs (`kwiq_res.h`) and the logo images |
| `third_party/` | The EuroScope SDK (`euroscope/`) and `discord-rpc/` |
| `test/` | The logic tests |
| `docs/` | Files for other repositories, e.g. the sector file release workflow |

Includes are relative to `src/plugin/` (`"rdf/rdf.hpp"`) or `src/utils/` (`"kwiq_logic.hpp"`).

## Versions and releases
The plugin uses [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`, optionally with a pre-release (`0.7.0-rc.1`).
- **The version** is `KWIQ_VERSION` in `src/utils/version.h`, and nothing adds to it: there is no fourth number any more. 0.7.0 is the first version released this way.
- **Build metadata:** every CI build adds `+<run>.<commit>` (`0.7.0+115.a1b2c3d`), shown in `.kwiq status` and in the "loaded" message, so a build can be traced to its run and commit. It never counts when versions are compared, and isn't sent to Core.
- **Pull request builds** are versioned `<version>-pr.<number>.<run>` (`0.7.0-pr.66.115`), a pre-release, so they're always lower than the release they lead to. A manual run on another branch builds `<version>-dev.<run>`. For the update check and `plugin.min_version` only, a test build counts as the version it was built from, so a tester isn't asked to "update" to that release or disabled by it. Because of that, `src/utils/version.h` can't use `pr` or `dev` in its own pre-release; the build stops if it does.
- **Comparing:** versions are compared by SemVer precedence everywhere: updates, `.kwiq update`, and Core's `plugin.min_version`. Major, minor and patch compare as numbers, a pre-release is lower than its release (`1.0.0-alpha` < `1.0.0-alpha.1` < `1.0.0-beta` < `1.0.0-beta.11` < `1.0.0-rc.1` < `1.0.0`), and build metadata is ignored. Old four-part versions from 0.5 and 0.6 (`0.6.0.159`) keep their fourth number as a build number after PATCH (`0.6.0.100` < `0.6.0.159` < `0.6.1` < `0.7.0`), so a four-part `plugin.min_version` or `plugin.latest_version` in Core still works for them.
- **Cutting a release:** in the pull request that should be released, change `KWIQ_VERSION` in `src/utils/version.h`. Bump PATCH for fixes, MINOR for new features, and MAJOR for changes that need controllers or Core to do something differently. When it's merged, the push to `main` publishes `v<version>`. A push that leaves the version unchanged is still tested and built, but publishes nothing, as that version is already released. If publishing a version fails, the next push to `main` (or a manual run) tries again, whatever it changes. A version with a pre-release (`0.7.0-rc.1`) is published as a GitHub pre-release, for testers to download; Core doesn't offer it to controllers until the version without one (`0.7.0`) is released.

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
| ... / FPC | the flight plan check (VFPC's rules): `OK`, `VFR`, or what failed: `SID`, `RTE`, `ENG`, `E/O`, `MIN`, `MAX`, `FLR`, `NAV`; orange `EVT` for an event flight off its event route, green `EVT` on it; grey `-` when disabled with Open FPC menu |
| ... / SRD | the same as FPC: the SRD and the flight plan check are one route check now (kept so existing tag layouts still work) |
| ... / Sector Indicator | the alias of the position tracking the aircraft (also drawn above it) |
| ... / RDF state | `!` while the aircraft transmits |
| ... / VCH items | Pending request, Hold Short, CTL, Reminder, and so on |

Functions: **Open event routes window** (left button on Event Route), **Explain FPC result** or **Open FPC menu** (left button on FPC), **Explain SRD result** (left button on SRD; the same as Explain FPC result), **Open SRD search window** (any item), **Open squawk assignment menu** and **Force squawk recycle** (on the Assigned Squawk item, see Squawk assignment).
VCH items and functions keep their original names; see https://github.com/DrFreas/VCH for how to set them up.

## Commands
`.kwiq connect` (get your API key from myKWIQ), `.kwiq key <key>` (enter it by hand; `.kwiq key` says whether one is connected, `.kwiq key clear` removes it), `.kwiq button` (hide or show the KWIQ button), `.kwiq status`, `.kwiq refresh`, `.kwiq sector` (sector indicators on the radar on or off), `.kwiq autorefresh` (slot auto refresh on or off), `.kwiq squawk` (automatic squawk assignment on or off), `.kwiq update` (says whether a newer version is waiting), `.kwiq alert` (preview the sector file alert), `.kwiq discord` (DiscordEuroScope on or off for yourself), `.kwiq qnh <ICAO>`, `.kwiq notifications`, `.kwiq version` (re-check the sector file), `.kwiq sectorfile <folder or AIRAC_VERSION.txt>` (tell the plugin where your package is; empty = automatic), `.rdf draw on|off|core` (your own RDF switch; `core` = as Core says), `.rdf refresh` (switch the channels off and sync them again from TrackAudio), `.rdf reload` (reconnect to TrackAudio), plus VCH's own `.vch ...` commands.

## What Core controls
Besides Core's address, only myKWIQ's Connect EuroScope page and the default EuroScope setup guide and changelog links are compiled in. `/api/plugin/latest` supplies the latest version, the checksums of the plugin and the updater, the kill switch and the changelog link. `/api/plugin/config` supplies: minimum plugin version, message of the day, refresh intervals, tag texts and colours, sector indicator colours/default, and the current sector file AIRAC. Sector aliases, RDF settings, the SRD and events have their own endpoints. All of it is edited in Filament (Plugin settings, Sector aliases, SRD routes, Plugin events).

## RDF and the TrackAudio bridge
RDF (Radio Direction Finder, by Kingfu Chan) is built in, as VCH is: do not load `RDFPlugin.dll` or afv-euroscope-bridge as well (the plugin says so and leaves RDF off if they are).
- **Direction finding:** a circle (or a line from the middle of the screen when it is off screen) on aircraft transmitting on your frequencies; red when more than one transmits at once. Middle-click shows the last transmission again.
- **Bridge:** when a station is set to RX or TX in TrackAudio (WebSocket on `127.0.0.1:49080`, 1.3.0 Beta 5 or later) or the AFV standalone client, the matching EuroScope voice channel follows, so frequencies are only set in one place. Only on a direct connection.
- **Settings from Core** (`/api/plugin/rdf`, one line per logon callsign pattern, the first match wins): `RDF|ORBB_*_CTR|draw=1|bridge=1|colour=#FFFFFF|colour_concurrent=#FF0000|radius=20|threshold=-1|precision=0|low_altitude=0|high_altitude=0|low_precision=0|high_precision=0|controllers=0|require_tx=0`. Keys left out take RDF's defaults; with no matching line, drawing is off and the bridge on. `rdf.endpoint` in `/config` is TrackAudio's address.
- **Controller:** RDF Drawing in the KWIQ menu or `.rdf draw on|off` overrides Core for themselves (kept in the profile as `kwiq_rdf_draw`, empty = as Core says; `.rdf draw core` goes back to Core). The menu's TrackAudio row shows the connection, which the plugin makes on its own: Connected, AFV client (TrackAudio isn't connected but the AFV standalone client is), Not running, or Off (RDF didn't start, or Core has the bridge and drawing off). It isn't a switch: a click syncs the channels again (`.rdf refresh`) while connected, tries to connect now while not (leaving the channels as they are), and does nothing when off.
- **Messages:** once a session, 15 seconds after logging on, if TrackAudio isn't running (and no AFV client is). TrackAudio's version isn't checked: RDF and the bridge need 1.3.0 Beta 5 or later.

## DiscordEuroScope
DiscordEuroScope (Discord Rich Presence, by Kirollos Nashaat) is built in: do not load `DiscordEuroscope.dll` as well. It is on by default; Core can turn it off for everyone (`discord.enabled = 0`), and each controller can turn it off for themselves with DiscordEuroScope in the KWIQ menu or `.kwiq discord`. That is kept as the `DiscordEuroScope` setting (`KWIQ Controller Plugin:DiscordEuroScope:0` in the package's `Settings\Plugins.txt`), which the Configurator also sets.

Core's `/api/plugin/discord` holds everything `DiscordEuroscope_config.json` did:
```
APP|1529517649506074776
SWEATBOX_BYPASS|0
MESSAGE|<state>|<fir>|<callsign pattern>|<event>|<details>|<state text>
IMAGE|<state>|<fir>|<callsign pattern>|<event>|<large image URL>|<large text>|<small image URL>|<small text>
BUTTON|<state>|<fir>|<callsign pattern>|<event>|<label>|<url>
RADIO|<ICAO prefix>|<frequency>|<radio callsign>
```
The four filters may be empty (any): state is `idle`, `playback`, `sweatbox`, `observer`, `controller` or `supervisor`; FIR is `OKAC` or `ORBB`; the pattern uses `*` and `?`; event is `1` (only while a Core event is on) or `0` (only when none is). At each logon (or position or state change) the plugin takes the matching messages and images with the most filters set and picks one at random, skipping any whose placeholders have no value; the elapsed time starts again then. At most two buttons are shown. Placeholders: `{callsign}`, `{rcallsign}` (from the `RADIO` lines, by the callsign's ICAO prefix and the primary frequency), `{frequency}`, `{current_tracked}`, `{total_tracked}` (aircraft tracked this logon), `{total_inrange}` and `{event}`. Discord's limits apply: texts are cut to 127 bytes and button labels to 31, never in the middle of a character, and a button whose URL isn't an http(s) link or is over 512 bytes is left out. Nothing is shown or logged when Discord isn't running.

## QNH monitor
While a controller is logged on, the plugin watches the QNH of every airport whose top-down order has their position, whoever else is online, as UKCP's pressure monitor does (see Top-down and airfield ownership). When a QNH changes during the session, a flashing `PRESSURE_MONITOR` tab says so: `Pressure monitor: OKKK QNH now 1019 (was 1018)`, with the transition level only when it changed: `Pressure monitor: ORBI QNH now 979 (was 981) TRL FL170 (was FL160)`. A re-issued METAR with the same QNH says nothing. `.kwiq qnh <ICAO>` asks for one airport.
- **METARs**, as UKCP gets them: Core fetches them from VATSIM once for everyone (`/api/plugin/metar`: `METAR|OKBK|<METAR>`) and tells the plugins when they change over a WebSocket (Laravel Reverb, the Pusher protocol: `push.host`, `push.port`, `push.key`, `push.tls` in `/config`; public channel `metars`, any event). Without the socket, the plugin polls every `metar.poll_seconds` (60); with it, every 10 minutes as a safety net.
- **Transition levels** (`/api/plugin/trl`), as TopSky's QNHTL: `TRL|OR|14000|160,980,170` (TA 14,000 ft; FL160 at QNH 980 and above, FL170 below) and `TRL|OK|13000|150` (fixed). The longest matching ICAO prefix wins.

## Top-down and airfield ownership
Who provides which service at each airport is worked out as the UK Controller Plugin does it (`src/plugin/ownership/ownership_logic.hpp`, from UKCP's `src/plugin/ownership`). Core sends one top-down order per airport, lowest first (`/api/plugin/topdown`: `TOPDOWN|OKBK|OKBK_DEL,OKBK_GND,OKBK_TWR,OKBK_APP,OKAC_CTR`, refreshed with the SRD). It is fetched once and used by squawks, the QNH monitor and notifications.
- **Online:** a controller counts once they have a primary frequency. A position (`OKBK_TWR`) is worked by any callsign with the same prefix and suffix (`OKBK_TWR`, `OKBK_1_TWR`, `OKBK_N_TWR`); with several on one position, the first in callsign order leads it. An airport with no order in Core is covered only by its own DEL, GND, TWR and APP.
- **Services:** the lowest position online provides Delivery. Every GND provides Ground, or failing one the first TWR, APP or CTR. Every TWR provides Tower, or failing one the first APP or CTR. The first APP or CTR provides Final Approach, and every APP provides Approach, or failing one the first CTR.
- **Used by:** squawks for aircraft on the ground (the user provides Delivery), notifications (the user provides any service) and the QNH monitor (the user's position is in the airport's order).
- Who is online follows EuroScope's controller updates and disconnects, and only the airports a change can affect are worked out again. Logging off clears it.

## Procedure notifications
Staff post procedure updates in Core (Filament) for a group of airports. KWIQ menu > Notifications (or `.kwiq notifications`) lists those for the airports where you provide a service (see Top-down and airfield ownership; all of them when not logged on), with Mark read / Mark unread and Open link (only for an http(s) link); read state is kept in Core per API key. At logon, and whenever new ones arrive, a flashing `KWIQ_NOTIFICATIONS` tab says how many are unread for your position.
- `GET /api/plugin/notifications`: `NOTICE|<id>|<airports, comma separated>|<published>|<read 0/1>|<title>|<link>|<text, \n for new lines>`
- `POST` / `DELETE /api/plugin/notifications/<id>/read`: mark read / unread
- Core pushes any event on the public `notifications` channel when there are new ones; the plugin also asks every 5 minutes either way.

## Plugin updates
Installed and updated the way the UK Controller Plugin is, with the same plain Windows message boxes (KWIQ First Time Download, KWIQ Automatic Update, KWIQ Automatic Update Complete, KWIQ Automatic Update Failed, KWIQ Loader Error):
- **Files.** The packages carry only `KWIQPlugin.dll`, the loader. It never changes itself: a new one comes with the next sector file packages. `KWIQPluginUpdater.dll` and `KWIQPluginCore.dll` are kept in `%LOCALAPPDATA%\KWIQ\bin`.
- **First start.** KWIQ First Time Download says the plugin will now download its components from Core (OK only), then the loader downloads the updater, which installs the plugin. If Core can't be reached or a download fails its checksum or isn't a 32-bit DLL, KWIQ Automatic Update Failed says so, an inactive stand-in is loaded for the session, and the next start tries again.
- **Every start.** The loader runs the updater, which asks Core for the latest version (`/api/plugin/latest`, which needs no key). If there is a newer one it asks; No asks again, as an update is required, and a second No starts the plugin reduced: only the outdated sector file alert, the EuroScope version check and a reminder in the chat (as the plugin loads and at every logon). Nothing else works in that session: no tag items (VCH's included), KWIQ menu or commands (the KWIQ button is still shown; a click repeats the reminder). After an update the controller is offered the changelog (`plugin.changelog_url` if it is an http(s) link, otherwise the built-in changelog page). The updater also keeps itself in step with Core's copy.
- **Downloads** (`/api/plugin/download/core`, `/api/plugin/download/updater`) are checked against `plugin.core_sha256` and `plugin.updater_sha256` and must be 32-bit DLLs. A file in use is renamed to `.old` and removed at the next start. Core mirrors all three files from the newest *published* release (`php artisan plugin:sync-release`, every 5 minutes; set `plugin.github_repo`, plus `PLUGIN_GITHUB_TOKEN` if the repository is private).
- **Kill switch.** With `plugin.enabled = 0` as EuroScope starts, the plugin is not loaded (an inactive stand-in says so in the chat). Switched off during a session, a running plugin keeps working until EuroScope closes.
- If Core cannot be reached, the installed version is loaded. Nothing pops up during a session; `.kwiq update` says whether a newer version is waiting.
- v0.5 and older kept the plugin in `%APPDATA%\KWIQ\plugin` and the key in `%APPDATA%\KWIQ\plugin-key.txt`: the new loader moves the key and removes the old install.
- 0.5 copies (0.5.0.56 in the packages, and 0.5.0.115) update themselves to the current version: `KWIQPlugin.dll` also carries `KWIQPluginCore.dll` as the `KWIQCORE` resource, which is what a 0.5 loader installs from. It takes the plugin out of it, starts it straight away and replaces itself with the new loader, which takes over at the next start. Core offers 0.5 copies the current release from 0.7.4 (`SyncPluginRelease::BRIDGE_VERSION` in kwiq-core).

## Outdated sector file alert
Once logged on (direct, proxy or sweatbox) with a sector file older than Core's version, a red OUTDATED SCT FILE box is drawn in the middle of every radar screen until the sector file is updated or the controller logs off. It flashes between `alert.colour_a` and `alert.colour_b` for `alert.flash_seconds`, then stays solid, and `conflict_alert.wav` plays on a loop for as long as it is up. The sound is searched for only in the controller's own package: near the sector file (`Sounds` folder, also one folder down), then near `KWIQPlugin.dll`; a system sound is the fallback. `.kwiq alert` shows the box for 15 seconds. `alert.enabled`, `alert.sound_file`, `alert.flash_seconds` and the colours are set in Core.

## EuroScope version check
As EuroScope starts, the plugin reads the version of the running EuroScope from its .exe and compares it with `euroscope.versions` in Core (comma separated; currently `3.2.3.2`, also known as 3.2c (r2), and never 3.2.9 or higher; `3.2.3` and `3.2.3.0` are the same). On any other version an Incorrect EuroScope Version popup says so, and OK opens `euroscope.setup_url` (by default, and whenever Core's value isn't an http(s) link, the EuroScope setup guide on docs.kuwait-iraqvacc.net). It is checked once per session, as soon as Core's config arrives; if that is later than the first three minutes it is only said in the chat. Leave `euroscope.versions` empty to turn the check off. `.kwiq status` shows the EuroScope version.

## Sector file version check
OKAC and ORBB are released separately, so Core holds a current AIRAC for each (`sectorfile.airac.okac` and `sectorfile.airac.orbb`, e.g. `2610/02`), filled by `php artisan sectorfile:sync` from `AIRAC_VERSION.txt` in each package's repository, falling back to the tag of its latest release. The plugin compares the loaded sector file with its own package's version: the package is taken from the sector file's path (the OKAC or ORBB folder or file nearest it), otherwise from the controller's callsign (OR... is ORBB, OK... is OKAC). The plugin reads the local version only from `AIRAC_VERSION.txt`, which the packages keep in `OKAC\Plugins` and `ORBB\Plugins`: it looks in the folder given with `.kwiq sectorfile` (or that file's folder), otherwise in the plugin's folder and its three parents, each with its `Plugins` folder. Without that file the version is not checked and no alert is shown. "2610/2" and "2610/02" are treated as equal.

## SRD check and FIR exit points
**The SRD and the flight plan check are one now.** Core keeps one table, its SRD, with the FPC's fields on each route (SID point, suffix, destinations, even/odd, forbidden levels, engines, equipment), and serves both `/api/plugin/srd` (SRD Search) and `/api/plugin/fpc` (the route check) from it. The SRD and FPC items both show the FPC result, and Explain SRD result is Explain FPC result. Each SRD route that starts at an airport is checked by the FPC: keyed on its SID point, or on its first point when it has none, with its destination as the destination filter. SRD Search lists every route, the FPC's included (`ORBI` to `PAXAT`: `BGD NOLDO B411 PAXAT`), with its FPC fields in the notes. The rest of this section describes the plugin's own SRD matching, which SRD Search's results still follow.

The plugin's SRD matching checks the filed route and level against the SRD rows for the flight:
1. Rows for its departure and destination airports, if there are any.
2. For a flight whose destination is outside the vACC's airspace (not `srd.domestic_prefixes`, default `OK,OR`), rows from its departure airport to the point where it leaves the airspace, as the UK Controller Plugin does. If there are none for that point, rows to the point where it leaves OKAC for ORBB or the other way round.
3. Otherwise, en-route rows whose two ends are both on the filed route.

The Iraq AIP's routes start or end at the aerodrome's VOR (ORBI: `BGD DCT NOLDO B411 PAXAT`, ORMM: `TASMI G795 BSR`), which pilots who file a SID or STAR leave out. So a navaid (2 or 3 letters) at an end of a row that is an airport is optional there: `NOLDO1A NOLDO B411 PAXAT` matches the row above, and **Explain SRD result** says `(BGD not filed)`. A navaid anywhere else in the row is still required.

Where a flight leaves the airspace comes from Core's FIR exit points, `/api/plugin/fir-exits`, one line per exit cone: `EXIT|<point>|<from heading>|<to heading>|<internal 0 or 1>`. A point can have several cones, one per direction (a point on the OKAC/ORBB boundary is crossed both ways), so the same point on several lines is several cones, each with its own internal flag; identical lines count once. A route leaves at a point if its track from there to the next point is inside any of its cones, and if two of that point's cones overlap at that track, the external one is used. An example is `EXIT|ROTAP|300|060|0`. A flight leaves at the first listed point on its route (EuroScope's extracted route) whose track to the next point lies in one of that point's exit cones, each from one heading clockwise to the other. Internal points are on the OKAC and ORBB boundary, and count only before the flight leaves the vACC's airspace. With no exit points in Core (or a 404), only steps 1 and 3 apply.

**Explain SRD result** names the exit point it used, and **Open SRD search window** fills in the exit point as the destination for such a flight. `.kwiq status` shows how many exit points Core sent (distinct points, not cones).

## Event Routes
**Open event routes window** (left button on Event Route, or Event Routes in the KWIQ button's menu) opens the event's routes for today (UTC), as Core sends them. It's the same kind of dialog as SRD Search, with the same look: a standard Windows dialog, in front of EuroScope until it is closed (Close, Enter, Esc or the title bar's X). The title names the event and the date. The list shows each route's **Airports** (`OKBK -> ORBI`) and **Route String**; select one and use **Copy Route String To Clipboard**, which then says "Copied". With no event, or no routes today, the list says "No event routes for today (UTC)."

## SRD Search
**Open SRD search window** (on any tag item, or SRD Search in the KWIQ button's menu) opens the UK Controller Plugin's SRD Search dialog: a standard Windows dialog, opened as UKCP opens it, in front of EuroScope until it is closed (Close, Esc or the title bar's X), like EuroScope's own dialogues. Enter the **Origin Fix or Airfield** and **Destination Fix or Airfield** (both required) and, optionally, the **Cruising Altitude or Level** (`350` is FL350, `35000` is feet), then **Search** or press Enter. Opened from an aircraft, it is filled in with the aircraft's departure, its destination (or its FIR exit point when it leaves the vACC's airspace) and its requested level, and searched straight away. A destination in the vACC's airspace (`srd.domestic_prefixes`, `OK,OR`) is always shown as itself.

**Search Results** lists every matching route: **Min** (`MC` when there is no minimum), **Max** (`-` when there is no limit), **FRA** (whether the route has Free Route Airspace, `<FRA>`, in it) and the **Route String**. Click a route to see its **Notes**, then **Copy Route String To Clipboard**. Tab moves between the boxes and Esc or **Close** closes the window. EuroScope keeps running while it is open.

## Flight plan check (FPC)
The FPC item is VFPC (hpeter2/VFPC, from DrFreas/VFPC) built in, with the rules the vACC keeps in VFPC's `Sid.json` served by Core instead of a file. It is separate from the SRD check. Rules are per departure airport and per SID, by the SID's first waypoint (`SESRU` for SESRU1A), or by the aerodrome's VOR (ORBB's rules are all keyed on it: `BGD`, `BSR`, `RER`, `KIR`, `SUL`, `ALI`). They are tried from top to bottom, and the first one the flight fits decides, so the narrowest rules go first.

When the SID's first waypoint has no rules at the airport, or the flight plan has no SID yet (EuroScope only sets one once a runway is active), the rules are found by the route instead: first those of a point on the filed route, then any rule whose `airways` the route follows. So an ORBI departure on `NOLDO1A NOLDO B411 PAXAT` is checked against BGD's `NOLDO B411 PAXAT` rule, and **Explain FPC result** says `rules for BGD by the route`. A SID that has rules of its own is only checked against those.

The route is only checked up to the first FIR exit point on it (Core's FIR exits, internal or external, found as for the SRD), whatever the destination: what is filed after it isn't the vACC's to check. **Explain FPC result** then says `route checked up to FIR exit PAXAT`.

**Event flights:** when the flight has an event route today (Core's event routes for its ADEP/ADES on that UTC day), the FPC item shows orange `EVT` if the filed route isn't one of them exactly, and green `EVT` if it is (the colour of a wrong one is `ui.colour_event_wrong` in Core, `#FF8C00` by default). Only these may differ: `DCT`, speed/level changes (`N0450F350`, `NOLDO/N0450F350`), a SID before the route and a STAR after it, and the aerodrome VOR at either end (as for the SRD). An extra point, a missing one or another order is `EVT`. This compares the whole route, not only up to the FIR exit. A flight on its event route shows green `EVT` instead of the usual FPC result; **Explain FPC result** still lists the FPC's checks. The Event Route item keeps its looser check (`OK` while every event-route point is filed, in order). **Explain FPC result** says which event route was expected.

| Field | Kind | What it does | Fails as |
|---|---|---|---|
| `suffix` | filter | the rule is only for that SID suffix, e.g. `1A` | |
| `destinations` | filter | only for these destinations; a prefix covers all of them (`OM`) | |
| `airways` | filter | only if the route has one of these, an airway or a run of route elements (`NAMDI M860 NINVA`, DCT inside a run ignored); `DCT` on its own means the route has a DCT | |
| `engine` | check | engine type one of `P`, `T`, `J`, `E` | `ENG` |
| `direction` | check | `EVEN`, `ODD` or `ANY` cruising level; above FL410, FL410/450/490 are odd and FL430/470/510 even | `E/O` |
| `min_fl`, `max_fl` | check | lowest and highest flight level | `MIN`, `MAX` |
| `forbidden_fls` | check | flight levels not allowed | `FLR`, yellow (`ui.colour_warn`) when it is the only failure |
| `navigation` | check | equipment codes allowed, e.g. `RG` | `NAV` |

A SID that has rules of its own, none of which fits the flight, is `SID`. When the rules are found by the route (no SID filed, or a SID with no rules, as at every ORBB airport) and none fits, the route is wrong: `RTE`, and **Explain FPC result** lists the routes the airport's rules allow, e.g. `route not valid out of ORBI, it must follow one of: NAMDI M860 NINVA / TAGRU L718 KABAN / NOLDO B411 PAXAT`. VFR flights show `VFR`, and airports with no rules show nothing. With more than one failure the item cycles through them, two seconds each (an even level above the highest allowed one shows `E/O`, then `MAX`); a level below the lowest is `MIN` and above the highest `MAX`; **Explain FPC result** lists everything that was checked, in its own **KWIQ FPC** chat tab (opened if it is not open yet). **Open FPC menu** is VFPC's **Check FP** menu: **Show Checks** is the same as Explain FPC result, and **Disable** turns the check off for that flight, so its FPC item shows a grey `-` (a VFR flight still shows `VFR`). On a disabled flight the menu has **Enable** instead, which turns it back on. As in VFPC, a flight stays disabled until its flight plan disconnects. `.kwiq status` shows how many airports have rules.

Core serves the rules at `/api/plugin/fpc`, one line per rule in order (a 404 means none):
`FPC|<airport>|<sid>|<suffix>|<direction>|<min_fl>|<max_fl>|<forbidden_fls>|<destinations>|<airways>|<engine>|<navigation>`. Lists are comma separated and an empty field is no restriction, e.g. `FPC|ORBI|BGD||EVEN|80|600|250,260||DELMI DCT RUKAM B411 MURIB||`. The existing `Sid.json` files (OKAC `OKAC/Plugins/VFPC/Sid.json`, ORBB `.data/VFPC/*.json`) map onto it field for field.

## Squawk assignment
Squawks are allocated by Core, so two controllers never hand out the same code. Core picks the range from the requesting controller's FIR (the plugin sends the controller's own callsign), the departure, the destination and the flight rules; when to ask for a code follows the UK Controller Plugin (`src/plugin/squawk/squawk_logic.hpp`, see `NOTICE.md`). It is off until `squawk.enabled` is set to 1 in Core, and while Core has not sent its list of codes never to assign (the plugin has no list of its own, and says so once in the chat). As in UKCP it works on the live network and on sweatbox (Core keeps the two apart); through a proxy only what the controller asks for is done, nothing automatic.

**Automatic**: checked on every flight plan update and every 5 seconds, not in the first 15 seconds after logging on, for aircraft with no code that nobody else tracks. Which codes count as none comes from Core: a code on its non-assignable list with "counts as assigned" off (default and non-discrete codes such as 1200, 2000, 2200 and 7000) is replaced; an emergency code (7500, 7600, 7700) counts as a code and is never replaced:
- **Untracked, on the ground** (30 kt or less, within 7 nm of the departure airport): only from the controller providing Delivery at that airport (see Top-down and airfield ownership), as in UKCP.
- **Tracked by the user**: any aircraft with no code.
- **Assumed with no code**: an aircraft the user takes on while its code is blank or non-assignable (2000, 1200, 7000 ...) always gets a new code from Core: not one Core already holds for the callsign, not a reconnect's old code, and even if someone set the 2000 by hand. A code the user sets after assuming is kept.
- **Sweatbox**: aircraft spawn anywhere, often in the air, with 2000, so there any aircraft nobody tracks gets a code (the 2000 they spawn with is never taken as set by hand).
- **Reconnects**: a pilot who reconnects within 10 minutes with the same departure, destination and flight rules: Core is asked first, and the code it still holds for them comes back. If it holds none, a code a controller set by hand comes back, otherwise they get a new one (so a code Core has since given to someone else is never handed back).

While Core is asked, the aircraft shows the holding code 7000, as in UKCP. It is never taken as a code: an aircraft showing it still needs one, and 7000 seen from another scope only means that scope is asking (so 7000 typed in EuroScope is not by hand; use **Conspicuity** in the menu). The conspicuity codes (`squawk.conspicuity`, `squawk.conspicuity_vfr`) and 7000 never count as a code and are never set from Core's answer, even if Core's non-assignable list leaves them out; Core answering with one is shown as an error. If Core gives nothing, an Assign from the menu goes back to the code the aircraft had; otherwise 7000 stays and the aircraft is asked for again a minute later.

When the departure, destination or flight rules change, the next code is a new one from Core (not one it holds for the old plan), even if the change was seen while nothing could be done. A code Core sends is only set if it is still wanted: not if someone else has started tracking the aircraft, or set a code, while Core was answering. If Core cannot give a code, the error is shown once and that aircraft is not asked for again for a minute.

**By hand**: **Open squawk assignment menu** offers Assign (a new code from Core, also **Force squawk recycle**), Conspicuity (by flight rules: `squawk.conspicuity`, default 2000, for IFR, `squawk.conspicuity_vfr`, default 7000, for VFR) and Manual (type a code). Setting a code from the menu releases the one Core held. A code set from the menu, typed in EuroScope, or set by a controller on another scope counts as by hand: the plugin leaves that aircraft alone (a 2000 typed in EuroScope is not replaced; on sweatbox it is) until its departure, destination or flight rules change. Only for aircraft the user tracks or nobody tracks.

**Core API**, all under `/api/plugin`, with the same `X-KWIQ-Key` and `X-KWIQ-Version` headers as the rest. Every request carries `?network=live` or `?network=sweatbox`, and Core keeps the two apart.

| Request | Answer |
|---|---|
| `GET /squawk-assignment/<callsign>` | `SQUAWK\|<code>` if Core holds a code for it, else 404 |
| `PUT /squawk-assignment/<callsign>`, form `origin=OKBK&destination=OMDB&rules=I` (or `V`)`&requester=OKBK_TWR` (the controller's own callsign) | `SQUAWK\|<code>`: a new code from the requester's FIR's range for that flight (replaces any it held), or `ERROR\|<message>` (requester in no FIR, every range full) |
| `DELETE /squawk-assignment/<callsign>` | 2xx, or 404 if it held none |
| `GET /squawk/non-assignable` | the codes Core never assigns, fetched with the config: `NONASSIGNABLE\|<first>\|<last>\|<category>\|<counts as assigned 0/1>\|<description>`, a single code with first = last. Kept in memory only; no list (unreachable on the first start, 404, or no valid rows) keeps squawks off |
| `GET /topdown` | one `TOPDOWN\|<airport>\|<position>,<position>,...` line per airport, lowest position first (refreshed with the SRD) |

Errors may carry `ERROR|<message>` in the body, which the plugin shows. A code Core answers with that is on its own non-assignable list is ignored. Config keys: `squawk.enabled` (default 0), `squawk.conspicuity` (default 2000), `squawk.conspicuity_vfr` (default 7000); the old names `squawk.non_discrete` and `squawk.non_discrete_vfr` are still read when the new ones are not set.

## Tests
`g++ -std=c++17 -Wall -Wextra -Isrc/utils -Isrc/plugin test/test_logic.cpp -o test_logic && ./test_logic`

## Licence
GPL-3.0-or-later. Read `NOTICE.md` before distributing the DLL.

## Personal API key and the KWIQ button
Home and visiting controllers, and anyone in training with the vACC, get a personal API key from myKWIQ (EuroScope Plugin), where the plugin can also be downloaded. Whenever there is no key, or Core refuses it as missing, invalid, revoked or expired, the plugin opens myKWIQ's Connect EuroScope page and an API Key Setup window: as EuroScope starts, whenever the controller connects to the network, or once they log off if Core refuses the key while they are logged on (they are told in the chat). The setup is a standard Windows message box, shown on its own thread so EuroScope keeps running: OK opens the page again, and the box comes back until a key is connected, when the plugin closes it. The key's ID is never shown in the plugin, only in Core. A key refused because the controller left the roster or is suspended is only reported. Only a 401 from Core counts as a refused key, so an outage never sends anyone to myKWIQ, and the last data downloaded stays in use. After the controller clicks Connect EuroScope, myKWIQ sends the browser to a listener on 127.0.0.1 with a one-off code (and the state the plugin opened the page with); the plugin swaps the code for the key at `/api/plugin/claim`, so the key never goes through the browser. The Connect EuroScope link is also put on the clipboard (never in the chat), in case no browser opens (EuroScope through Wine or CrossOver on a Mac or Linux hands it to the system browser; `.kwiq status` shows the Wine version). Help: discord.kuwait-iraqvacc.net. `.kwiq key <key>` still works for pasting a key by hand. It is kept in `%LOCALAPPDATA%\KWIQ\settings\api-settings.json` (`{"api-key": "..."}`, as UKCP keeps its own), outside the EuroScope profile, and sent to Core as `X-KWIQ-Key` with the plugin version as `X-KWIQ-Version`. Core only refuses requests without a valid key once `api.require_key` is set to 1; updates (`/api/plugin/latest` and the download) never need a key. Staff can see and invalidate keys in Core under EuroScope Plugin > API Keys.

The KWIQ button is the vACC's logo on each radar screen, as other plugins show theirs: no border, label or status colours. It is 28 pixels at 100% display scaling (35 at 125%, 42 at 150%, 56 at 200%) and starts in the bottom-right corner, 8 pixels in from the edges. Drag it anywhere on the radar screen: its place is kept in the profile as its distance from the bottom-right corner, so it follows the window as it is resized, and it always stays fully on screen. In reduced mode a click repeats the reminder in the chat instead. Click it (left or right) for the plugin's menu: API key, sector indicators, RDF drawing, the TrackAudio bridge, DiscordEuroScope, notifications, slot auto refresh, event routes, SRD search, sector file and update checks, refresh and status. `.kwiq button` or Hide Button in the menu hides it, and `.kwiq button` brings it back. The logo is `resource/kwiq_logo.png`; `resource/kwiq_logo.ps1` makes the small copies the plugin embeds (`resource/kwiq_logo_28.bmp` to `resource/kwiq_logo_56.bmp`).

The sector indicator above each tracked aircraft is shown only for the positions in Core's Sector aliases. Each one is an ESE position ID, as in the ESE's `[POSITIONS]` (e.g. `BS` for `ORBB_S_CTR`), and the alias shown on the radar, at most 4 characters (e.g. `SR`); to show the ID itself, enter it as its own alias. Position IDs are per sector file (the same ID can be a different position in the OKAC and ORBB ESEs), so each alias names its sector file, OKAC or ORBB. Core sends them from `/api/plugin/sectors` as `SECTOR|<ID>|<alias>|<sector file>`, and the plugin uses the loaded sector file's aliases (the package found for the sector file version check). Aliases with no sector file, from before it was asked for, apply to both. The plugin looks up the position EuroScope identifies the tracking controller as right now (by prefix, suffix and frequency), so a relief callsign or a controller covering a listed position shows that position's alias. A controller EuroScope does not identify gets no indicator. The indicator is drawn in the colour of the aircraft's tag, which Core sets per flight plan state, named as in EuroScope's Symbology: `ui.colour_sector_non_concerned`, `_notified`, `_information` (coordinated), `_transfer_to_me_initiated`, `_assumed` (also transfer from me initiated) and `_redundant`, each `#RRGGBB`. A state Core sets no colour for uses `ui.colour_sector`.