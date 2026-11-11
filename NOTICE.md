# Notice and attribution

This plugin is licensed **GPL-3.0-or-later** (see `LICENSE`). It combines these sources:

| Part | Files | Origin | License |
|---|---|---|---|
| VCH, built in | `requestMenu.*`, `internet.*`, `usefulStuff.*`, `Constant.hpp`, `stdafx.h`, `targetver.h`, `Config.h` | https://github.com/DrFreas/VCH (Jan Fries), v0.8.6 | GPL-3.0 |
| Slot detection | `VatsimAPI.h`, `VatsimAPI.cpp` | Adapted from the VATCAN Slots-Plugin, via https://github.com/frazerxyz/Slots-Plugin | See below |
| KWIQ code | `KWIQPlugin.cpp`, `kwiq_logic.hpp`, `kwiq_win.hpp`, `loader.cpp`, `updater.cpp`, `keylink.*`, `version.h`, `tests/` | Kuwait & Iraq vACC | GPL-3.0-or-later |
| Loader / updater design | `loader.cpp`, `updater.cpp`, `kwiq_win.hpp` | Follows the UK Controller Plugin's loader, updater and core layout (https://github.com/VATSIM-UK/uk-controller-plugin); written for this plugin, no UKCP code copied | GPL-3.0 |
| libcurl (linked, from vcpkg) | - | https://curl.se (Daniel Stenberg and contributors) | curl licence (MIT style) |
| JSON for Modern C++ (header only, from vcpkg) | - | https://github.com/nlohmann/json (Niels Lohmann) | MIT |
| RDF and the TrackAudio bridge | `rdf.hpp`, `rdf.cpp` | Adapted from https://github.com/KingfuChan/RDF (Kingfu Chan), v1.4.3b, commit a4bd0ae | GPL-3.0 |
| IXWebSocket (linked, from vcpkg) | - | https://github.com/machinezone/IXWebSocket | BSD-3-Clause |
| zlib (linked, from vcpkg, for IXWebSocket) | - | https://zlib.net (Jean-loup Gailly and Mark Adler) | zlib |
| EuroScope SDK | `libs/` | EuroScope plugin SDK (copy shipped in the VCH repo) | EuroScope SDK terms |

## Changes made to VCH
- `CVCHPlugin` takes the plugin name, version, developer and copyright as constructor arguments, so it appears as part of "KWIQ Controller Plugin".
- The upstream version check (which compares against DrFreas/VCH releases) is disabled.

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

## Slot detection: permission
The plugin information string in the VATCAN source (`SituPlugin.cpp`) declares CC BY-NC 4.0, and the repository has no licence file.
The Kuwait & Iraq vACC has stated that it has permission from the authors (frazerxyz / VATCAN) to use this code, and permission from the VCH author for VCH. Keep the written confirmation (email or message) in the vACC's records and add a copy to this repository, e.g. `docs/permissions/`, so the basis for redistributing the DLL under GPL-3.0-or-later is on file.

Attribution kept in every case: VATCAN Slots-Plugin (https://github.com/VATSIMCanada/Slots-Plugin) and the frazerxyz fork (https://github.com/frazerxyz/Slots-Plugin).
