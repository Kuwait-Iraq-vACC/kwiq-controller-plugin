# Notice and attribution

This plugin is licensed **GPL-3.0-or-later** (see `LICENSE`). It combines these sources:

| Part | Files | Origin | License |
|---|---|---|---|
| VCH, built in | `requestMenu.*`, `internet.*`, `usefulStuff.*`, `Constant.hpp`, `stdafx.h`, `targetver.h`, `Config.h` | https://github.com/DrFreas/VCH (Jan Fries), v0.8.6 | GPL-3.0 |
| Slot detection | `VatsimAPI.h`, `VatsimAPI.cpp` | Adapted from the VATCAN Slots-Plugin, via https://github.com/frazerxyz/Slots-Plugin | See below |
| FIR exit points | the FIR exit section of `kwiq_logic.hpp` | Adapted from the UK Controller Plugin, https://github.com/VATSIM-UK/uk-controller-plugin (`src/plugin/intention`: FirExitPoint, ExitCone, CachedAircraftFirExitGenerator; `src/plugin/srd`: SrdSearchDialog) | GPL-3.0 |
| SRD Search dialog | `kwiq.rc`, `kwiq_res.h` | The UK Controller Plugin's `IDD_SRD_SEARCH` dialog template (`resource/UKControllerPlugin.rc`), https://github.com/VATSIM-UK/uk-controller-plugin | GPL-3.0 |
| KWIQ code | `KWIQPlugin.cpp`, `kwiq_logic.hpp`, `tests/` | Kuwait & Iraq vACC | GPL-3.0-or-later |
| EuroScope SDK | `libs/` | EuroScope plugin SDK (copy shipped in the VCH repo) | EuroScope SDK terms |

## Changes made to VCH
- `CVCHPlugin` takes the plugin name, version, developer and copyright as constructor arguments, so it appears as part of "KWIQ Controller Plugin".
- The upstream version check (which compares against DrFreas/VCH releases) is disabled.

## Slot detection: permission
The plugin information string in the VATCAN source (`SituPlugin.cpp`) declares CC BY-NC 4.0, and the repository has no licence file.
The Kuwait & Iraq vACC has stated that it has permission from the authors (frazerxyz / VATCAN) to use this code, and permission from the VCH author for VCH. Keep the written confirmation (email or message) in the vACC's records and add a copy to this repository, e.g. `docs/permissions/`, so the basis for redistributing the DLL under GPL-3.0-or-later is on file.

Attribution kept in every case: VATCAN Slots-Plugin (https://github.com/VATSIMCanada/Slots-Plugin) and the frazerxyz fork (https://github.com/frazerxyz/Slots-Plugin).
