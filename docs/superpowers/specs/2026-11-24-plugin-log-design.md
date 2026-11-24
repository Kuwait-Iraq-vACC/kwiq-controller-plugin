# Plugin debug log: design

## Why
EuroScope's own log is a network trace. It shows the codes that were set, but not why the plugin set them, what
Core answered, or whether the plugin tried at all. A controller should be able to send one file that answers
those questions, and the file should be easy to filter by aircraft or area.

## Goals
- Always on: the plugin writes its own log, with nothing to switch on first.
- One line per event, with fixed fields, so a log can be filtered with grep or a short script.
- Records the plugin's decisions and the facts behind them: squawk decisions, Core and Discord requests (status
  and time taken), everything shown in the chat, menu clicks, SRD searches, sector indicator and FPC results.
- Never slows the radar screen: lines are queued, and a background thread writes them.

## Non-goals and anti-goals
- Not a replacement for EuroScope's network log.
- Never writes the API key, a claim code or any other credential, and never writes Core's response bodies.
- Must never stop the plugin from working. If the log can't be written, the plugin carries on.

## Where it goes (decided by the controller)
In the same folder as `KWIQPlugin.dll`, the DLL EuroScope loads from the sector file package (`sf::DllDir()`).
Files are named `KWIQPlugin-YYYYMMDD.log`, one per UTC day. If that folder can't be written to, the log goes to
`%LOCALAPPDATA%\KWIQ\logs` instead, and the chat says where once.

## Line format
`HH:MM:SS.mmm AREA key=value key=value ... | free text`

All times are UTC. AREA is one of START, CORE, SQUAWK, MSG, MENU, SRD, SECTOR, FPC, DISCORD and CMD. Values
containing spaces are quoted. Newlines are written as `\n`, so one event is always one line.

## Approaches
1. **A log module with a queue and a writer thread (picked).** A small `kwiq::log` with pure formatting and
   pruning, which can be unit-tested, plus a Windows writer. Logging is added where the plugin already reports
   things (Msg, SetError, the HTTP helper) and in the squawk decisions.
2. Writing to the file directly on each call. This is simpler, but it blocks the radar thread while the disk is
   busy.
3. Sending lines to EuroScope's own log. EuroScope gives plugins no way to do this, and the result would be
   mixed in with 1.5 million network lines.

## Limits
- Each day's file stops at 20 MB, with a final line saying so.
- Files older than 7 days are deleted when EuroScope starts.
- `.kwiq log` opens the folder.
- `.kwiq log verbose` adds per-tick detail until EuroScope restarts.

## Left to the builder
The exact fields in each area, beyond the squawk decision inputs (callsign, decision, assigned code, tracked
by, whether it was assumed, whether the plan changed, waiting on a retry or busy) and the Core request fields
(method, path without query, status, ms, error).
