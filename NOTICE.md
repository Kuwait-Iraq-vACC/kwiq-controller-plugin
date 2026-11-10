# Notice and attribution

This plugin is licensed **GPL-3.0-or-later** (see `LICENSE`). It combines these sources:

| Part | Files | Origin | License |
|---|---|---|---|
| VCH, built in | `requestMenu.*`, `internet.*`, `usefulStuff.*`, `Constant.hpp`, `stdafx.h`, `targetver.h`, `Config.h` | https://github.com/DrFreas/VCH (Jan Fries), v0.8.6 | GPL-3.0 |
| Slot detection | `VatsimAPI.h`, `VatsimAPI.cpp` | Adapted from the VATCAN Slots-Plugin, via https://github.com/frazerxyz/Slots-Plugin | See below |
| Squawk assignment rules | `squawk_logic.hpp`, and the squawk section of `KWIQPlugin.cpp` | Adapted from the UK Controller Plugin, https://github.com/VATSIM-UK/uk-controller-plugin (`src/plugin/squawk`) | GPL-3.0 |
| KWIQ code | `KWIQPlugin.cpp`, `kwiq_logic.hpp`, `tests/` | Kuwait & Iraq vACC | GPL-3.0-or-later |
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
- Who gives departures their squawk comes from Core's top-down order (`/api/plugin/squawk/topdown`) instead of UKCP's airfield ownership.
- A reconnecting aircraft only gets its old code back if its departure and destination are unchanged; a code set by hand is left alone until they change.
- Requests say whether they are from the live network or sweatbox.

## Slot detection: permission
The plugin information string in the VATCAN source (`SituPlugin.cpp`) declares CC BY-NC 4.0, and the repository has no licence file.
The Kuwait & Iraq vACC has stated that it has permission from the authors (frazerxyz / VATCAN) to use this code, and permission from the VCH author for VCH. Keep the written confirmation (email or message) in the vACC's records and add a copy to this repository, e.g. `docs/permissions/`, so the basis for redistributing the DLL under GPL-3.0-or-later is on file.

Attribution kept in every case: VATCAN Slots-Plugin (https://github.com/VATSIMCanada/Slots-Plugin) and the frazerxyz fork (https://github.com/frazerxyz/Slots-Plugin).
