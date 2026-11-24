# Plugin Debug Log Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Have the plugin write its own day-by-day log of its decisions, next to KWIQPlugin.dll, so it can be sent in and filtered.

**Architecture:** One header-only portable module, `src/utils/kwiq_log.hpp`, does two jobs: pure formatting and pruning (`kwiq::log`) and a queued writer with its own thread (`kwiq::log::Writer`), using only the standard library (`std::filesystem`, `std::thread`), so it is unit-tested on Linux in `test/test_logic.cpp`. Every other file only calls `kwiq::log::Write(area, fields, text)`. The plugin starts it with the DLL's folder and a fallback folder, and the network helper and squawk code log through it.

**Tech Stack:** C++17, MSVC (CI) / g++ (tests), EuroScope plugin SDK, libcurl.

**Spec:** `docs/superpowers/specs/2026-11-24-plugin-log-design.md`

## Global Constraints

- Folder: `sf::DllDir()` (the folder of the KWIQPlugin.dll EuroScope loads). If it can't be written to, use `%LOCALAPPDATA%\KWIQ\logs`, and say so once in the chat.
- File name: `KWIQPlugin-YYYYMMDD.log`, one per UTC day.
- Line: `HH:MM:SS.mmm AREA key=value ... | text`, in UTC. Areas: START CORE SQUAWK MSG MENU SRD SECTOR FPC DISCORD CMD.
- Values with a space, `"`, `=` or `|` are double-quoted, with `"` and `\` escaped. `\r` and `\n` are written as `\r` and `\n` in values and text, so one event is always one line.
- 20 MB per day's file. After that, one line `LOG cap=20MB | log full for today` is written, then nothing more until the next UTC day.
- Files `KWIQPlugin-*.log` older than 7 days are deleted at start.
- Never logged: the API key, myKWIQ claim codes (`/claim/<code>` becomes `/claim/***`), query strings, or response bodies.
- Logging never throws out of a caller, and never blocks on disk: `Write` only queues.
- Commit as the existing git identity, with no AI attribution. Branch `feat/plugin-log`.

## Review Focus

- **Disk can't be written to (read-only package folder, file locked by an editor):** the fallback folder is used, and if that also fails the plugin works on with no log. Task 2 tests an unwritable folder.
- **The UTC day changes mid-session:** lines go to the new day's file and the 20 MB cap starts again. Task 2 tests rollover with an injected clock.
- **Text that would split a line or forge a field (newline, `|`, `=` in a callsign or Core error):** escaped. Task 1 tests this.
- **A burst of lines, such as a verbose tick over 300 aircraft:** the queue is capped at 10 000 lines, and when it is full the line is dropped and counted (`LOG dropped=N`), never blocking. Task 2 tests it.
- **Shutdown while lines are queued:** `Stop()` flushes what's queued, then joins. Task 2 tests it.

---

### Task 1: Pure formatting and pruning (`kwiq::log`)

**Files:**
- Create: `src/utils/kwiq_log.hpp`
- Test: `test/test_logic.cpp` (add `#include "kwiq_log.hpp"` and a block)

**Interfaces:**
- Produces (namespace `kwiq::log`):
  - `using Fields = std::vector<std::pair<std::string, std::string>>;`
  - `std::string Escape(const std::string& s)`: replaces `\r`→`\r` and `\n`→`\n` (as two characters).
  - `std::string Value(const std::string& v)`: `Escape`, then quotes if it holds a space, `"`, `=` or `|`, or is empty (`""`).
  - `std::string FormatLine(const std::tm& utc, int ms, const std::string& area, const Fields& f, const std::string& text)`
  - `std::string FileName(const std::tm& utc)`, which gives `KWIQPlugin-20261124.log`
  - `bool IsLogFile(const std::string& name)`
  - `bool Expired(const std::string& name, const std::tm& todayUtc, int keepDays)`, true for a log file whose date is more than `keepDays` before today
  - `std::string SafeUrl(const std::string& url)`, which drops `?…` and `#…` and masks the segment after `/claim/`
  - `class Changes { public: bool Changed(const std::string& key, const std::string& value); void Forget(const std::string& key); };`, true the first time and whenever the value differs from the last one, so that per-tag results are logged only when they change

- [ ] **Step 1: Write the failing tests** in `test/test_logic.cpp`, before `return`:

```cpp
{
    using namespace kwiq::log;
    std::tm t{}; t.tm_year = 126; t.tm_mon = 10; t.tm_mday = 24; t.tm_hour = 20; t.tm_min = 14; t.tm_sec = 5;
    CHECK(FormatLine(t, 7, "SQUAWK", { { "cs", "RJA812" }, { "code", "0725" } }, "") == "20:14:05.007 SQUAWK cs=RJA812 code=0725");
    CHECK(FormatLine(t, 123, "MSG", { { "err", "1" } }, "Core said\r\nno") == "20:14:05.123 MSG err=1 | Core said\\r\\nno");
    CHECK(Value("a b") == "\"a b\"" && Value("x=y") == "\"x=y\"" && Value("a|b") == "\"a|b\"" && Value("") == "\"\"");
    CHECK(Value("say \"hi\"") == "\"say \\\"hi\\\"\"" && Value("OK") == "OK" && Value("a\nb") == "a\\nb");
    CHECK(FileName(t) == "KWIQPlugin-20261124.log" && IsLogFile("KWIQPlugin-20261124.log"));
    CHECK(!IsLogFile("KWIQPlugin.dll") && !IsLogFile("KWIQPlugin-2026112.log") && !IsLogFile("KWIQPlugin-2026112x.log"));
    CHECK(Expired("KWIQPlugin-20261116.log", t, 7) && !Expired("KWIQPlugin-20261117.log", t, 7));
    CHECK(Expired("KWIQPlugin-20251231.log", t, 7) && !Expired("KWIQPlugin.dll", t, 7) && !Expired("KWIQPlugin-20261124.log", t, 7));
    CHECK(SafeUrl("https://core/api/plugin/squawk/RJA812?network=vatsim") == "https://core/api/plugin/squawk/RJA812");
    CHECK(SafeUrl("https://core/api/plugin/claim/AB12CD") == "https://core/api/plugin/claim/***");
    CHECK(SafeUrl("https://core/api/plugin/claim/AB12/x#f") == "https://core/api/plugin/claim/***/x" && SafeUrl("") == "");
    Changes c;
    CHECK(c.Changed("FPC RJA812", "OK") && !c.Changed("FPC RJA812", "OK") && c.Changed("FPC RJA812", "CHK"));
    c.Forget("FPC RJA812");
    CHECK(c.Changed("FPC RJA812", "CHK"));
}
```

- [ ] **Step 2: Run the tests and make sure they fail**

Run: `g++ -std=c++17 -Wall -Wextra -Isrc/utils -Isrc/plugin test/test_logic.cpp -o test_logic && ./test_logic`
Expected: compile error, `kwiq_log.hpp: No such file`.

- [ ] **Step 3: Implement the `kwiq::log` functions above in `src/utils/kwiq_log.hpp`** (header-only, `#pragma once`, standard library only, GPL header like `kwiq_logic.hpp`). Compute day differences in `Expired` with `std::mktime`-free arithmetic (days from civil date), so the result doesn't depend on the time zone.

- [ ] **Step 4: Run the tests and make sure they pass**

Run: the same command. Expected: `all passed`, with no warnings.

- [ ] **Step 5: Commit**

```bash
git add src/utils/kwiq_log.hpp test/test_logic.cpp
git commit -m "Log: line format, file names, pruning and safe URLs"
```

### Task 2: The writer (`kwiq::log::Writer` and the global functions)

**Files:**
- Modify: `src/utils/kwiq_log.hpp`
- Test: `test/test_logic.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:
  - `class Writer { public: using Clock = std::function<std::chrono::system_clock::time_point()>; explicit Writer(Clock c = {}); bool Start(const std::vector<std::string>& dirs, int keepDays = 7, std::uintmax_t capBytes = 20u << 20, size_t maxQueue = 10000); void Write(const std::string& area, const Fields& f, const std::string& text = ""); void Stop(); std::string Dir() const; size_t Dropped() const; };`
    - `Start` takes the first folder in `dirs` where a test file can be created and appended to, prunes expired logs there, and starts the thread. It returns false if no folder works, and then `Write` does nothing.
    - The time is taken when `Write` is called, not when the line is written.
    - The thread waits on a condition variable, appends with `std::ofstream` (binary, app), and flushes after each batch. It opens the file for each batch, so editors can read it and a rollover just picks a new name. When a batch is written after dropped lines, it starts with `LOG dropped=N`.
    - `Stop` writes what's left and joins. It's safe to call twice, and the destructor calls it.
  - Globals: `Writer& Global();`, `inline void Write(const std::string& area, const Fields& f, const std::string& text = "") noexcept` (catches everything), `inline bool Verbose()` and `inline void SetVerbose(bool)` (an `std::atomic<bool>`).

- [ ] **Step 1: Write the failing tests** in a temp folder under `std::filesystem::temp_directory_path() / "kwiq_log_test"` (removed at the start and end of the block):
  - `writer_writes_line`: Start([dir]) with a fixed clock 2026-11-24 20:14:05.007 UTC, Write("START", {{"v","0.7.6"}}), Stop(). The file `KWIQPlugin-20261124.log` holds exactly `20:14:05.007 START v=0.7.6\n`.
  - `writer_falls_back`: Start([dir/"missing"/"deep" made unwritable by being a regular file path, dir/"fb"]) returns true and `Dir()` is `dir/"fb"`. Start([a regular file path]) alone returns false, and Write then does nothing and doesn't throw.
  - `writer_prunes`: create `KWIQPlugin-20261101.log` and `KWIQPlugin-20261120.log` plus `other.log`. After Start, only the 1101 one is gone.
  - `writer_rollover`: the clock moves from 23:59:59 to 00:00:01 between two writes. There are two files, one line each.
  - `writer_cap`: capBytes = 100. Write five 40-byte lines. The file ends with `LOG cap=20MB | log full for today` and holds only the lines that fitted before it. The cap text always says `20MB`, as fixed copy. Then the next UTC day writes again.
  - `writer_queue_full`: maxQueue = 2, and the clock blocks the thread (it waits on a flag the test sets) while 5 writes are made. `Dropped()` is 3 (or more if the thread already took some), and after release and Stop the file holds a `LOG dropped=` line.
  - `writer_stop_flushes`: 1000 writes, then Stop. The file has 1000 lines.

- [ ] **Step 2: Run the tests and make sure they fail**

Run: `g++ -std=c++17 -Wall -Wextra -pthread -Isrc/utils -Isrc/plugin test/test_logic.cpp -o test_logic && ./test_logic`
Expected: compile error, `Writer` not declared.

- [ ] **Step 3: Implement `Writer` and the globals in `src/utils/kwiq_log.hpp`.** Use `gmtime_s` on `_WIN32` and `gmtime_r` elsewhere.

- [ ] **Step 4: Run the tests and make sure they pass**, both the plain command and the sanitizer one from `.github/workflows/build.yml`:

Run: `g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Wall -Wextra -pthread -Isrc/utils -Isrc/plugin test/test_logic.cpp -o test_logic_san && ./test_logic_san`
Expected: `all passed`.

- [ ] **Step 5: Add `-pthread` to both g++ lines in `.github/workflows/build.yml` and to the comment on line 1 of `test/test_logic.cpp`.**

- [ ] **Step 6: Commit**

```bash
git add src/utils/kwiq_log.hpp test/test_logic.cpp .github/workflows/build.yml
git commit -m "Log: a queued writer with daily files, a 20 MB cap and a fallback folder"
```

### Task 3: Start, messages, Core requests, commands

**Files:**
- Modify: `src/plugin/KWIQPlugin.cpp` (constructor about line 266, destructor about 316, `Msg` about 1688, `SetError` about 1701, `OnCompileCommand` about 509)
- Modify: `src/plugin/slots/VatsimAPI.cpp` (`HttpGet` line 98, `HttpSend` line 107)
- Modify: `src/utils/kwiq_win.hpp`, adding `std::string LocalAppDataDir()` only if one doesn't already exist there (look for how `LoadApiKey` builds `%LOCALAPPDATA%\KWIQ`, and reuse it)

**Interfaces:**
- Consumes: `kwiq::log::Global().Start`, `kwiq::log::Write`, `kwiq::log::SetVerbose`, `kwiq::log::SafeUrl`.

- [ ] **Step 1: Start the log** in the constructor, right after `esVersion_` is set and before the duplicate check: `Start({ sf::DllDir(), <LOCALAPPDATA>\KWIQ\logs })`, creating the fallback folder with `std::filesystem::create_directories` inside a try. Then write `START v=<KWIQ_PLUGIN_BUILD> es=<esVersion_> mode=full|reduced|duplicate dll=<sf::DllPath()>`. If `Dir()` isn't `sf::DllDir()`, call `Msg("The plugin log is in " + dir + " (the plugin's folder can't be written to).", false)`. If Start fails, nothing more happens.
- [ ] **Step 2: Stop the log** as the last line of the destructor, after `GlobalCleanup`, writing `START event=exit` first.
- [ ] **Step 3: `Msg` writes** `MSG err=0|1 | <m>`. `SetError` writes `MSG src=<src> err=1 | <msg>` only when it's a new message, and `ClearError` writes `MSG src=<src> cleared=1` only when there was one.
- [ ] **Step 4: Log each Core and Discord request** in `HttpGet` and `HttpSend`, timing with `std::chrono::steady_clock`: `CORE method=GET url=<SafeUrl> status=<code> ms=<n>`, adding `err=<err>` when it's set. Include `<utils/kwiq_log.hpp>` from the include path `kwiq_log.hpp`.
- [ ] **Step 5: Add the commands** in `OnCompileCommand`, before the VCH fallback:
  - `.KWIQ LOG` opens `Dir()` with `ShellExecuteA(nullptr, "open", dir, …)` and says `Msg("Plugin log: " + dir, false)`, or `Msg("The plugin log could not be started.", true)`.
  - `.KWIQ LOG VERBOSE` toggles `SetVerbose` and says `Verbose plugin log on/off until EuroScope restarts.`
  - Every command line that starts `.KWIQ` is logged as `CMD | <line>`, except `.KWIQ KEY …` and any other command carrying a key. Check the command list for which ones those are, and log only `CMD | .KWIQ KEY ***` for them.
- [ ] **Step 6: Run the tests** (`./test_logic`, which should pass) and check that the file compiles on CI. There's no Windows compiler here, so read the changed calls against their declarations once more.
- [ ] **Step 7: Commit**

```bash
git add src/plugin/KWIQPlugin.cpp src/plugin/slots/VatsimAPI.cpp src/utils/kwiq_win.hpp
git commit -m "Log: start and exit, chat messages, every Core request, .kwiq log commands"
```

### Task 4: Squawks

**Files:**
- Modify: `src/plugin/KWIQPlugin.cpp`, at `SquawkAttempt` (about line 1360), `SquawkRequest` (1311), `SquawkApply` (1424), `SquawkPick` / `SquawkManual` / `SquawkForce` (1542 to 1575), `OnFlightPlanControllerAssignedDataUpdate` (1604), `SquawkTick`'s "no list" branch, and where the code list arrives (look for `sqHaveCodes_ = `)
- Modify: `src/plugin/squawk/squawk_logic.hpp`
- Test: `test/test_logic.cpp`

**Interfaces:**
- Produces: `const char* kwiq::squawk::ActionName(Action a)`, which gives `"none"`, `"previous"`, `"assign"` or `"assumed"`.

- [ ] **Step 1: Write the failing test:** `CHECK(std::string(squawk::ActionName(squawk::Action::Assumed)) == "assumed" && std::string(squawk::ActionName(squawk::Action::None)) == "none");`
- [ ] **Step 2: Run it** and make sure it fails because `ActionName` isn't declared. Then implement it in `squawk_logic.hpp`, run it again, and make sure it passes.
- [ ] **Step 3: Log the decision in `SquawkAttempt`.** The line is `SQUAWK cs= decide=<ActionName> code=<a.squawk> counts=<0|1 HasCode> tracked=<me|other|none> assumed=<s->assumed> byhand=<s->byHand> planchanged=<ForceNeeded> gs= alt= fromorigin= delivery=<deliveryByMe>`. Add `skip=others|waiting|login|nofacts` when the switch isn't reached. Write it when the action isn't None. When it is None, write it only if `Verbose()` is on, or if `kwiq::log::Changes` (a member `sqLogChanges_`) says the line, without gs/alt/fromorigin, changed for that callsign. That way, an aircraft that stays unassigned is visible once, without being repeated every 5 seconds.
- [ ] **Step 4: Log the request and the answer.**
  - `SquawkRequest` writes `SQUAWK cs= request=PUT lookup=<0|1> manual=<0|1> previous=<code>`, or `busy=1` when the callsign is already waiting.
  - In `SquawkApply`, one line per result: `SQUAWK cs= result=set code=` / `result=error | <error>` / `result=not-wanted code=` / `result=released` / `result=release-failed | <error>`.
- [ ] **Step 5: Log clicks and codes seen.**
  - `SquawkPick` writes `MENU squawk=<option> cs=<ASEL callsign>`, and `SquawkManual` writes `MENU squawk=manual cs= code=`.
  - `OnFlightPlanControllerAssignedDataUpdate` writes `SQUAWK cs= seen=<code> byhand=<1 if CodeSeen marked it>` when the stored code changed.
  - When Core's list arrives, write `SQUAWK list=<n rows>`, and in the "no list" branch write `SQUAWK list=none`.
- [ ] **Step 6: Run the tests** (`./test_logic`), which should print `all passed`.
- [ ] **Step 7: Commit**

```bash
git add src/plugin/KWIQPlugin.cpp src/plugin/squawk/squawk_logic.hpp test/test_logic.cpp
git commit -m "Log: every squawk decision, request, answer, click and code seen"
```

### Task 5: SRD, FPC, sector indicator, Discord, release

**Files:**
- Modify: `src/plugin/KWIQPlugin.cpp`, at `RunSrdSearch` (about line 920), where an SRD tag result or a fill-in is chosen (look for `KWIQ_ITEM_SRD` and the code that writes the route into the flight plan), the FPC check at about line 1078, `SectorFor` (about line 634), and the config apply that sets `sectors_`
- Modify: `src/plugin/discord/discord.cpp`, at `Configure`, `Discord_Initialize` (line 81) and `Discord_UpdatePresence` (line 180)
- Modify: the version and changelog, following how 0.7.5 was released (`git show ff6b03e --stat`)

**Interfaces:**
- Consumes: `kwiq::log::Write`, `kwiq::log::Changes`, `kwiq::log::Verbose`.

- [ ] **Step 1: Log SRD searches and fill-ins.**
  - `RunSrdSearch` writes `SRD search adep= ades= lvl= hits=<n> | <first route>`.
  - Each route the plugin writes into a flight plan writes `SRD cs= set=<route> exit=<first FIR exit>`. That exit is what [#102](https://github.com/Kuwait-Iraq-vACC/kwiq-controller-plugin/pull/102) and [#94](https://github.com/Kuwait-Iraq-vACC/kwiq-controller-plugin/pull/94) are about.
- [ ] **Step 2: Log FPC results** as `FPC cs= result=<OK|CHK|…> | <Explain text, first 200 chars>`, only when `Changes` says the result changed for that callsign.
- [ ] **Step 3: Log the sector indicator** as `SECTOR cs= tracker= position=<GetPositionId or "">  alias=<result> colour=<state name used>`, only when it changed for that callsign. Also log `SECTOR rows=<n> pkg=<sectorPkg_>` when Core's sector list arrives. The colour is what [#101](https://github.com/Kuwait-Iraq-vACC/kwiq-controller-plugin/pull/101) is about.
- [ ] **Step 4: Log Discord.**
  - `Configure` writes `DISCORD core=<on|off> app=<set|none> event=<name>`. The event is what [#104](https://github.com/Kuwait-Iraq-vACC/kwiq-controller-plugin/pull/104) is about.
  - Initialize writes `DISCORD start`.
  - Each `UpdatePresence` whose key changed writes `DISCORD presence details= state= large= small=`.
- [ ] **Step 5: Add `Verbose()` lines.** In `SquawkTick`, write one `SQUAWK tick aircraft=<n> codes=<list size>` line per pass.
- [ ] **Step 6: Release the change.**
  - Bump the version to the next patch after main's current version, the way commit ff6b03e did.
  - Add a changelog line: "The plugin keeps a log of what it does, next to KWIQPlugin.dll (KWIQPlugin-YYYYMMDD.log, 7 days). `.kwiq log` opens the folder."
  - Add the same to `README.md`'s commands list.
- [ ] **Step 7: Run all the tests** (plain and sanitizer), which should print `all passed`. Then commit and push `feat/plugin-log`.

```bash
git add -A
git commit -m "Log: SRD, FPC, sector indicator and Discord; release"
git push -u origin feat/plugin-log
```
