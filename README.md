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
`.kwiq status`, `.kwiq refresh`, `.kwiq sector`, `.kwiq autorefresh`, `.kwiq version` (re-check the sector file), `.kwiq sectorfile <folder or .ese>` (tell the plugin where your sector file is; empty = automatic), plus VCH's own `.vch ...` commands.

## What Core controls
Only the Core address is compiled in. `/api/plugin/config` supplies: kill switch, minimum plugin version, message of the day, refresh intervals, tag texts and colours, sector indicator colour/default, and the current sector file AIRAC. Sector aliases, the SRD and events have their own endpoints. All of it is edited in Filament (Plugin settings, Sector aliases, SRD routes, Plugin events).

## Sector file version check
Core holds the current AIRAC (`sectorfile.airac`, e.g. `2610/02`), filled by `php artisan sectorfile:sync` from `AIRAC_VERSION.txt` in the sector file repository (see `docs/sector-file-release.yml`). The plugin finds the local version from `AIRAC_VERSION.txt` or the `AIRAC 2610/1` line in the `.ese` header, looking in the folder given with `.kwiq sectorfile`, otherwise in the plugin DLL's folder and its two parents. Ship the plugin and `AIRAC_VERSION.txt` in the same install package and it works with no setup. "2610/2" and "2610/02" are treated as equal.

## Tests
`g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic`

## Licence
GPL-3.0-or-later. Read `NOTICE.md` before distributing the DLL.
