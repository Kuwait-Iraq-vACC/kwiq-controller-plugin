# Notice and attribution

This plugin is licensed **GPL-3.0-or-later** (see `LICENSE`). It combines these sources:

| Part | Files | Origin | License |
|---|---|---|---|
| VCH, built in | `requestMenu.*`, `internet.*`, `usefulStuff.*`, `Constant.hpp`, `stdafx.h`, `targetver.h`, `Config.h` | https://github.com/DrFreas/VCH (Jan Fries), v0.8.6 | GPL-3.0 |
| Slot detection | `VatsimAPI.h`, `VatsimAPI.cpp` | Adapted from the VATCAN Slots-Plugin, via https://github.com/frazerxyz/Slots-Plugin | See below |
| FIR exit points | the FIR exit section of `kwiq_logic.hpp` | Adapted from the UK Controller Plugin, https://github.com/VATSIM-UK/uk-controller-plugin (`src/plugin/intention`: FirExitPoint, ExitCone, CachedAircraftFirExitGenerator; `src/plugin/srd`: SrdSearchDialog) | GPL-3.0 |
| SRD Search dialog | `kwiq.rc`, `kwiq_res.h` | The UK Controller Plugin's `IDD_SRD_SEARCH` dialog template (`resource/UKControllerPlugin.rc`), https://github.com/VATSIM-UK/uk-controller-plugin; the Event Routes dialog (`IDD_EVENT_ROUTES`) is laid out from it | GPL-3.0 |
| Flight plan check | `fpc_logic.hpp` | Adapted from VFPC, https://github.com/hpeter2/VFPC (from https://github.com/DrFreas/VFPC, Jan Fries): `analyzeFP.cpp` | GPL-3.0 |
| Squawk assignment rules | `squawk_logic.hpp`, and the squawk section of `KWIQPlugin.cpp` | Adapted from the UK Controller Plugin, https://github.com/VATSIM-UK/uk-controller-plugin (`src/plugin/squawk`) | GPL-3.0 |
| KWIQ code | `KWIQPlugin.cpp`, `kwiq_ui.*`, `kwiq_logic.hpp`, `kwiq_win.hpp`, `loader.cpp`, `updater.cpp`, `keylink.*`, `push.*`, `version.h`, `tests/` | Kuwait & Iraq vACC | GPL-3.0-or-later |
| Loader / updater design | `loader.cpp`, `updater.cpp`, `kwiq_win.hpp` | Follows the UK Controller Plugin's loader, updater and core layout (https://github.com/VATSIM-UK/uk-controller-plugin); written for this plugin, no UKCP code copied | GPL-3.0 |
| libcurl (linked, from vcpkg) | - | https://curl.se (Daniel Stenberg and contributors) | curl licence (MIT style) |
| JSON for Modern C++ (header only, from vcpkg) | - | https://github.com/nlohmann/json (Niels Lohmann) | MIT |
| RDF and the TrackAudio bridge | `rdf.hpp`, `rdf.cpp` | Adapted from https://github.com/KingfuChan/RDF (Kingfu Chan), v1.4.3b, commit a4bd0ae | GPL-3.0 |
| DiscordEuroScope | `discord.hpp`, `discord.cpp` | Adapted from https://github.com/Kirollos/DiscordEuroScope (Kirollos Nashaat), 1.3.0-rc1, commit 399a951 | GPL-3.0 |
| discord-rpc | `libs/discord-rpc/` | https://github.com/multitheftauto/discord-rpc (Discord, Inc.; Multi Theft Auto's fork, commit b9b5699), Windows files only, unchanged | MIT (`libs/discord-rpc/LICENSE`) |
| RapidJSON (header only, from vcpkg) | - | https://github.com/Tencent/rapidjson, used by discord-rpc | MIT |
| Mbed TLS (linked, from vcpkg, for IXWebSocket) | - | https://github.com/Mbed-TLS/mbedtls | Apache-2.0 |
| IXWebSocket (linked, from vcpkg) | - | https://github.com/machinezone/IXWebSocket | BSD-3-Clause |
| zlib (linked, from vcpkg, for IXWebSocket) | - | https://zlib.net (Jean-loup Gailly and Mark Adler) | zlib |
| EuroScope SDK | `libs/` | EuroScope plugin SDK (copy shipped in the VCH repo) | EuroScope SDK terms |

## Changes made to VCH
- `CVCHPlugin` takes the plugin name, version, developer and copyright as constructor arguments, so it appears as part of "KWIQ Controller Plugin".
- The upstream version check (which compares against DrFreas/VCH releases) is disabled.

## Changes made to the UK Controller Plugin's squawk code
- Ported from UKCP's classes (SquawkAssignment, SquawkEventHandler, SquawkGenerator, SquawkAssignmentMenu, SquawkValidator, Login) to plain functions in `squawk_logic.hpp` and the EuroScope events in `KWIQPlugin.cpp`.
- Codes come from Core (`/api/plugin/squawk-assignment/<callsign>`) instead of the UKCP API. Requests are form fields and answers are plain text (`SQUAWK|<code>`), like the rest of Core's plugin API.
- UKCP's general and local code types are one request: Core picks the range from the requesting controller's FIR, the departure, the destination and the flight rules. The UK's circuit code and local codes are gone.
- No code is built in: the codes never assigned, and which of them count as a code the aircraft has (UKCP's `HasAssignedSquawk`), come from Core's `/squawk/non-assignable`.
- UKCP's conspicuity option is Non-discrete, with the codes (2000, 7000 for VFR) set in Core. No code is held while Core is asked; an aircraft Core could not give a code is not asked for again for a minute.
- Who gives departures their squawk comes from Core's top-down order (`/api/plugin/topdown`) instead of UKCP's airfield ownership.
- A reconnecting aircraft only gets its old code back if its departure and destination are unchanged; a code set by hand is left alone until they change.
- Requests say whether they are from the live network or sweatbox.

## Changes made to the Slots-Plugin code
- As `VatsimAPI.h` describes: the event code comes from Core, the NAT track and flight plan amend parts are removed, and the data is kept in a store of its own instead of the radar screen class. `kwiq_net` (the plugin's HTTP helper) lives in the same files.
- Every request stops at 32 MiB (#43).
- `CDataHandler::FetchWorker` catches every exception and always clears `fetchInProgress`, so a failed fetch can neither end EuroScope nor stop later fetches (#45).
## Changes made to RDF
- It is driven by `CKWIQPlugin` and drawn by `CKWIQRadar`, instead of being a plugin of its own.
- Its settings come from Core per callsign pattern. RDF's settings file, `RDFStyle.json`, per-ASR settings and the `.RDF` commands that set them are removed; `.RDF REFRESH` and `.RDF RELOAD` are kept, and `.RDF DRAW ON|OFF|CORE` is the controller's own switch.
- plog, MFC and cpp-httplib are removed: TrackAudio's version is read with the plugin's libcurl, and messages go through the plugin's chat output on the main thread.
- The "RDF state" tag item uses ID 1006, clear of the KWIQ and VCH IDs.
- It does not start if another RDF or afv-euroscope-bridge is loaded in the same EuroScope.

## Changes made to DiscordEuroScope
- It is driven by `CKWIQPlugin`'s timer, instead of being a plugin of its own.
- Everything in `DiscordEuroscope_config.json` comes from Core. Messages, images and buttons are lists with filters (state, FIR, callsign pattern, event); one message is picked at random at each logon, and the elapsed time starts again then. Radio callsigns come from Core instead of the ESE file.
- New placeholder `{event}`.
- `Discord_Initialize` does not register a URL handler in the registry.
- `.discord hotreload` is gone: Core's settings arrive on their own.
- Texts are cut at a UTF-8 character boundary (127 bytes, 31 for button labels), and a button whose URL Discord would refuse (not http(s), or over 512 bytes) is left out.

## Slot detection: permission
The plugin information string in the VATCAN source (`SituPlugin.cpp`) declares CC BY-NC 4.0, and the repository has no licence file.
The Kuwait & Iraq vACC has stated that it has permission from the authors (frazerxyz / VATCAN) to use this code, and permission from the VCH author for VCH. Keep the written confirmation (email or message) in the vACC's records and add a copy to this repository, e.g. `docs/permissions/`, so the basis for redistributing the DLL under GPL-3.0-or-later is on file.

Attribution kept in every case: VATCAN Slots-Plugin (https://github.com/VATSIMCanada/Slots-Plugin) and the frazerxyz fork (https://github.com/frazerxyz/Slots-Plugin).
