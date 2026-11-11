// kwiq_logic.hpp - pure logic for the KWIQ plugin (no Windows / EuroScope dependencies).
// Route matching, event routes, SRD (shared by the SRD check and SRD search). Unit-tested in tests/test_logic.cpp.
// License: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <tuple>
#include <cmath>
#include <sstream>
#include <array>
#include <map>
#include <functional>

namespace kwiq {

inline std::string Upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
inline std::vector<std::string> SplitWs(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
inline std::vector<std::string> SplitChar(const std::string& s, char d) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == d) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
inline std::string SanitiseCode(const std::string& s) {
    std::string o;
    for (char c : s) if (std::isalnum((unsigned char)c) || c == '_' || c == '-') o += c;
    return o;
}
inline bool AllDigits(const std::string& s, size_t from, size_t n) {
    if (from + n > s.size()) return false;
    for (size_t i = 0; i < n; i++) if (!std::isdigit((unsigned char)s[from + i])) return false;
    return true;
}
// A whole number from Core's text, as atoi reads it (leading spaces, a sign, digits, anything after is
// ignored), but clamped to the int range instead of undefined when it doesn't fit. No digits: def.
inline int ToInt(const std::string& s, int def = 0) {
    const char* p = s.c_str();
    char* end = nullptr;
    long long v = std::strtoll(p, &end, 10);                 // saturates at LLONG_MIN / LLONG_MAX
    if (end == p) return def;
    return v < INT_MIN ? INT_MIN : v > INT_MAX ? INT_MAX : (int)v;
}
// A web page to open in the browser: http:// or https://, a host, and no spaces, quotes or control
// characters. Anything else from Core (a local path, another scheme) is never given to ShellExecute.
inline bool IsWebLink(const std::string& url) {
    auto starts = [&](const char* scheme) {
        size_t n = std::char_traits<char>::length(scheme);
        if (url.size() < n) return false;
        for (size_t i = 0; i < n; i++) if (std::tolower((unsigned char)url[i]) != scheme[i]) return false;
        return true;
    };
    size_t host = starts("https://") ? 8 : starts("http://") ? 7 : 0;
    if (!host || url.size() == host || url[host] == '/') return false;
    for (char c : url) if ((unsigned char)c <= ' ' || c == 0x7F || c == '"') return false;
    return true;
}

// ---- Route handling --------------------------------------------------------

// ICAO speed/level groups: N0450F350, K0830S1100, M082F350, N0450A100, N0450M0840
inline bool IsSpeedLevel(const std::string& t) {
    if (t.size() < 7) return false;
    if (t[0] != 'N' && t[0] != 'K' && t[0] != 'M') return false;
    size_t sp = (t[0] == 'M') ? 3 : 4;
    if (!AllDigits(t, 1, sp)) return false;
    size_t i = 1 + sp;
    if (i >= t.size()) return false;
    char l = t[i++];
    size_t ln;
    if (l == 'F' || l == 'A') ln = 3; else if (l == 'S' || l == 'M') ln = 4; else return false;
    return t.size() == i + ln && AllDigits(t, i, ln);
}
// Upper-case, drop DCT, drop speed/level groups, strip "/..." suffixes (WPT/N0450F350, SID/27L)
inline std::vector<std::string> NormaliseRoute(const std::string& route) {
    std::vector<std::string> out;
    for (const auto& raw : SplitWs(route)) {
        std::string t = Upper(raw);
        size_t slash = t.find('/');
        if (slash != std::string::npos) t = t.substr(0, slash);
        if (t.empty() || t == "DCT" || IsSpeedLevel(t)) continue;
        out.push_back(t);
    }
    return out;
}
// Every element of `want` must appear, in order, in `filed`. Extra filed elements
// (SIDs, STARs, extra waypoints) are allowed.
inline bool MatchTokens(const std::vector<std::string>& want, const std::vector<std::string>& filed) {
    size_t j = 0;
    for (size_t i = 0; i < filed.size() && j < want.size(); i++) if (filed[i] == want[j]) j++;
    return j == want.size();
}
inline bool RouteMatches(const std::string& wantRte, const std::string& filedRte) {
    return MatchTokens(NormaliseRoute(wantRte), NormaliseRoute(filedRte));
}

// ---- Event routes ----------------------------------------------------------

struct EventRoute { std::string date, adep, ades, rte; std::vector<std::string> tokens; }; // date = YYYY-MM-DD UTC
struct EventData {
    bool active = false;
    std::string name, vatcanCode;
    std::vector<EventRoute> routes;
};
enum class RouteResult { None, Ok, Check };

inline bool IsDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    return AllDigits(s, 0, 4) && AllDigits(s, 5, 2) && AllDigits(s, 8, 2);
}
inline std::string DateOf(std::time_t t) {
    std::tm g;
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &g);
    return b;
}
inline std::string TodayUtc() { return DateOf(std::time(nullptr)); }

// Routes valid on the given UTC day (a route without a date applies every day)
inline std::vector<EventRoute> RoutesForDate(const EventData& ev, const std::string& today) {
    std::vector<EventRoute> out;
    if (!ev.active) return out;
    for (const auto& r : ev.routes) if (r.date.empty() || r.date == today) out.push_back(r);
    return out;
}
// None = no event / no route for this ADEP-ADES pair today (show nothing). Allocation-free.
inline RouteResult CheckEventRoute(const EventData& ev, const std::string& adep, const std::string& ades,
                                   const std::vector<std::string>& filed, const std::string& today) {
    if (!ev.active) return RouteResult::None;
    bool pair = false;
    for (const auto& r : ev.routes) {
        if ((!r.date.empty() && r.date != today) || r.adep != adep || r.ades != ades) continue;
        pair = true;
        if (MatchTokens(r.tokens, filed)) return RouteResult::Ok;
    }
    return pair ? RouteResult::Check : RouteResult::None;
}
inline RouteResult CheckEventRoute(const EventData& ev, const std::string& adep, const std::string& ades,
                                   const std::string& filedRte, const std::string& today) {
    return CheckEventRoute(ev, adep, ades, NormaliseRoute(filedRte), today);
}

// Core /api/plugin/event (plain text):
//   NONE
//   EVENT|<name>|<vatcan code>
//   ROUTE|<YYYY-MM-DD UTC>|<ADEP>|<ADES>|<route>
inline EventData ParseCoreEvent(const std::string& body) {
    EventData ev;
    for (const auto& rawLine : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(rawLine), '|');
        if (f[0] == "EVENT" && f.size() >= 2) {
            ev.active = true;
            ev.name = Trim(f[1]);
            ev.vatcanCode = f.size() > 2 ? SanitiseCode(Trim(f[2])) : "";
        } else if (f[0] == "ROUTE" && f.size() >= 5 && IsDate(Trim(f[1]))) {
            EventRoute r{ Trim(f[1]), Upper(Trim(f[2])), Upper(Trim(f[3])), Trim(f[4]), {} };
            r.tokens = NormaliseRoute(r.rte);
            ev.routes.push_back(std::move(r));
        }
    }
    if (!ev.active) ev.routes.clear();
    return ev;
}

// ---- SRD (shared by the SRD check and SRD search) ------------------------------------
// Core /api/plugin/srd (plain text):
//   VERSION|<hash>
//   SRD|<ADEP>|<ADES>|<min ft or empty>|<max ft or empty>|<route>|<notes>
// ADEP/ADES are airports (4 letters) or waypoints (anything else).

struct SrdRoute {
    std::string adep, ades, rte, notes;
    int minLvl = 0, maxLvl = 0;            // feet, 0 = no limit
    std::vector<std::string> tokens;
};
struct SrdData {
    std::vector<SrdRoute> rows;
    std::unordered_map<std::string, std::vector<size_t>> pair; // "ADEP|ADES" for airport-airport rows
    std::unordered_map<std::string, std::vector<size_t>> toPoint; // "ADEP|POINT" for airport-to-waypoint rows (FIR exits)
    std::vector<size_t> wp;                                    // rows with a waypoint end
    std::string version;
};
inline bool IsAirportCode(const std::string& s) {
    if (s.size() != 4) return false;
    for (char c : s) if (!std::isalpha((unsigned char)c)) return false;
    return true;
}
inline void BuildSrd(SrdData& d) {
    d.pair.clear(); d.toPoint.clear(); d.wp.clear();
    for (size_t i = 0; i < d.rows.size(); i++) {
        const auto& r = d.rows[i];
        if (IsAirportCode(r.adep) && IsAirportCode(r.ades)) { d.pair[r.adep + "|" + r.ades].push_back(i); continue; }
        if (IsAirportCode(r.adep)) d.toPoint[r.adep + "|" + r.ades].push_back(i);
        d.wp.push_back(i);
    }
}
inline SrdData ParseSrd(const std::string& body) {
    SrdData d;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f[0] == "VERSION" && f.size() >= 2) d.version = Trim(f[1]);
        else if (f[0] == "SRD" && f.size() >= 6) {
            SrdRoute r;
            r.adep = Upper(Trim(f[1])); r.ades = Upper(Trim(f[2]));
            if (r.adep.empty() || r.ades.empty()) continue;
            r.minLvl = ToInt(f[3]); r.maxLvl = ToInt(f[4]);
            r.rte = Trim(f[5]);
            if (f.size() > 6) r.notes = Trim(f[6]);
            r.tokens = NormaliseRoute(r.rte);
            d.rows.push_back(std::move(r));
        }
    }
    BuildSrd(d);
    return d;
}
inline std::string FlStr(int ft) {
    char b[16];
    std::snprintf(b, sizeof b, "FL%03d", ft / 100);
    return b;
}
// "350", "FL350", "fl 350" -> 35000 ; "35000" -> 35000 ; "" / junk -> 0
inline int ParseLevelFt(const std::string& in) {
    std::string s = Upper(Trim(in)), d;
    for (char c : s) if (std::isdigit((unsigned char)c)) d += c;
    if (d.empty() || d.size() > 6) return 0;
    int v = ToInt(d);
    return v < 1000 ? v * 100 : v;
}

// ---- FIR exit points (UK Controller Plugin: intention/FirExitPoint, ExitCone, CachedAircraftFirExitGenerator) ----
// Core /api/plugin/fir-exits (plain text), one line per exit cone:  EXIT|<point>|<from heading>|<to heading>|<internal 0/1>
// An aircraft leaves the airspace at a listed point when its track from that point to the next one on its
// route lies in one of the point's exit cones (from..to, clockwise, degrees). A point can have several cones,
// one per direction: a point on the OKAC/ORBB boundary is crossed both ways, so the same name on several lines
// is several cones, each with its own internal flag. Internal cones are on the OKAC/ORBB boundary; the others
// are where the vACC's airspace ends. Identical lines count once.
struct FirExit { std::string point; int from = 0, to = 360; bool internal = false; };
using FirExits = std::unordered_map<std::string, std::vector<FirExit>>;   // point -> its cones; size() = points

inline FirExits ParseFirExits(const std::string& body) {
    FirExits m;
    auto heading = [](const std::string& s, int& out) {
        std::string t = Trim(s);
        if (t.empty() || t.size() > 3 || !AllDigits(t, 0, t.size())) return false;
        out = std::atoi(t.c_str());
        return out <= 360;
    };
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() < 4 || f[0] != "EXIT") continue;
        FirExit e;
        e.point = Upper(Trim(f[1]));
        if (e.point.empty() || !heading(f[2], e.from) || !heading(f[3], e.to)) continue;
        e.internal = f.size() > 4 && Trim(f[4]) == "1";
        auto& cones = m[e.point];
        bool same = false;
        for (const auto& c : cones) same = same || (c.from == e.from && c.to == e.to && c.internal == e.internal);
        if (!same) cones.push_back(e);
    }
    return m;
}
// UKCP ExitCone::AircraftIsExiting; a cone that wraps through north has to <= from
inline bool InExitCone(const FirExit& e, int heading) {
    return e.to <= e.from ? heading >= e.from || heading <= e.to : heading >= e.from && heading <= e.to;
}
// A point on the route, with the track to the next one (-1 for the last point)
struct RoutePoint { std::string name; int track = -1; };
struct AircraftExit { std::string internal, external; };     // empty when it does not leave there
// The cone of a point that a track leaves through, or nullptr. If two of its cones overlap at that track, an
// external one wins: the flight leaves the vACC's airspace there.
inline const FirExit* ExitConeFor(const std::vector<FirExit>& cones, int track) {
    const FirExit* hit = nullptr;
    for (const auto& c : cones) {
        if (!InExitCone(c, track)) continue;
        if (!c.internal) return &c;
        if (!hit) hit = &c;
    }
    return hit;
}
// UKCP CachedAircraftFirExitGenerator::Generate: the first exit along the route; an internal one only counts
// before the aircraft leaves the vACC's airspace. Each point is taken if the track is inside any of its cones,
// with that cone's own internal flag.
inline AircraftExit FindFirExit(const FirExits& exits, const std::vector<RoutePoint>& route) {
    AircraftExit out;
    for (const auto& p : route) {
        auto it = exits.find(Upper(p.name));
        if (it == exits.end() || p.track < 0) continue;
        const FirExit* cone = ExitConeFor(it->second, p.track);
        if (!cone) continue;
        if (cone->internal) { if (out.internal.empty()) out.internal = it->first; continue; }
        out.external = it->first;
        break;
    }
    return out;
}
// Airports in the vACC's airspace: ICAO prefixes, "OK,OR" by default (srd.domestic_prefixes in Core)
inline bool Domestic(const std::string& airport, const std::string& prefixes) {
    for (const auto& p : SplitChar(prefixes, ',')) {
        std::string t = Upper(Trim(p));
        if (!t.empty() && Upper(airport).compare(0, t.size(), t) == 0) return true;
    }
    return false;
}
// What SRD destination to use for a flight: its destination when it stays in the vACC's airspace, else where
// it leaves it (UKCP SrdSearchDialog::PrepopulateSearch); empty when that is not known
inline std::string SrdEnd(const std::string& ades, const std::string& prefixes, const AircraftExit& exit) {
    if (Domestic(ades, prefixes)) return ades;
    return !exit.external.empty() ? exit.external : exit.internal;
}

// SRD rows that apply to a flight. Airport-pair rows (ADEP|ADES) take precedence. Then, for a flight
// leaving the vACC's airspace, rows from ADEP to its FIR exit point (failing that, its exit between OKAC
// and ORBB). Only when there are none do enroute rows apply (waypoint ends that are both on the filed route).
inline void Candidates(const SrdData& d, const std::string& adep, const std::string& ades,
                       const std::vector<std::string>& filed, std::vector<size_t>& out,
                       const AircraftExit& exit = {}) {
    out.clear();
    auto it = d.pair.find(adep + "|" + ades);
    if (it != d.pair.end()) { out = it->second; return; }
    for (const std::string* point : { &exit.external, &exit.internal }) {
        if (point->empty()) continue;
        auto e = d.toPoint.find(adep + "|" + *point);
        if (e != d.toPoint.end()) { out = e->second; return; }
    }
    auto on = [&](const std::string& end, const std::string& airport) {
        return IsAirportCode(end) ? end == airport : std::find(filed.begin(), filed.end(), end) != filed.end();
    };
    for (size_t i : d.wp) if (on(d.rows[i].adep, adep) && on(d.rows[i].ades, ades)) out.push_back(i);
}

enum class SrdResult { None, Ok, Route, Min, Max };

inline SrdResult CheckSrd(const SrdData& d, const std::string& adep, const std::string& ades,
                          const std::vector<std::string>& filed, int rflFt, const AircraftExit& exit = {}) {
    std::vector<size_t> c;
    Candidates(d, adep, ades, filed, c, exit);
    if (c.empty()) return SrdResult::None;
    bool routeOk = false;
    SrdResult lvl = SrdResult::None;
    for (size_t i : c) {
        const auto& r = d.rows[i];
        if (!MatchTokens(r.tokens, filed)) continue;
        routeOk = true;
        if (rflFt > 0 && r.minLvl && rflFt < r.minLvl) { if (lvl == SrdResult::None) lvl = SrdResult::Min; }
        else if (rflFt > 0 && r.maxLvl && rflFt > r.maxLvl) { if (lvl == SrdResult::None) lvl = SrdResult::Max; }
        else return SrdResult::Ok;
    }
    return routeOk ? lvl : SrdResult::Route;
}
inline std::string ExplainSrd(const SrdData& d, const std::string& adep, const std::string& ades,
                              const std::string& filedRte, int rflFt, const AircraftExit& exit = {}) {
    auto filed = NormaliseRoute(filedRte);
    std::vector<size_t> c;
    Candidates(d, adep, ades, filed, c, exit);
    // Say which exit the rows are for, when it was one
    std::string via;
    if (!c.empty() && IsAirportCode(d.rows[c[0]].adep) && !IsAirportCode(d.rows[c[0]].ades)) via = " (FIR exit " + d.rows[c[0]].ades + ")";
    else if (c.empty() && !exit.external.empty()) via = " (FIR exit " + exit.external + ")";
    std::string head = adep + " -> " + ades + via + " " + (rflFt > 0 ? FlStr(rflFt) : "FL---") + ": ";
    switch (CheckSrd(d, adep, ades, filed, rflFt, exit)) {
    case SrdResult::None:  return head + (exit.external.empty() && exit.internal.empty()
                                              ? "no SRD route defined for this pair."
                                              : "no SRD route defined for this pair or its FIR exit.");
    case SrdResult::Ok:    return head + "route and level conform to the SRD.";
    case SrdResult::Min:
    case SrdResult::Max: {
        std::string s = head + "route is valid but the level is outside the SRD limits. Allowed:";
        for (size_t i : c) {
            const auto& r = d.rows[i];
            if (!MatchTokens(r.tokens, filed)) continue;
            s += " [" + (r.minLvl ? FlStr(r.minLvl) : std::string("any")) + " to " +
                 (r.maxLvl ? FlStr(r.maxLvl) : std::string("any")) + "]";
        }
        return s;
    }
    case SrdResult::Route: break;
    }
    std::string s = head + "filed route does not match the SRD. Expected one of:";
    size_t shown = 0;
    for (size_t i : c) {
        if (shown++ == 3) { s += " ... (+" + std::to_string(c.size() - 3) + " more)"; break; }
        s += " | " + d.rows[i].rte;
    }
    return s;
}
// SRD search window, as the UK Controller Plugin's SRD Search dialog: Min (MC = no minimum), Max (- = no limit),
// FRA (the route has Free Route Airspace in it) and the notes for the selected route
inline std::string SrdMinText(const SrdRoute& r) { return r.minLvl ? std::to_string(r.minLvl / 100) : "MC"; }
inline std::string SrdMaxText(const SrdRoute& r) { return r.maxLvl ? std::to_string(r.maxLvl / 100) : "-"; }
inline bool HasFreeRoute(const std::string& rte) { return rte.find("<FRA>") != std::string::npos; }
inline std::string SrdNotesText(const SrdRoute& r) {
    std::string s = r.notes.empty() ? "No notes." : r.notes;
    if (HasFreeRoute(r.rte)) s += "\r\n\r\nThis route contains segments of Free Route Airspace.";
    return s;
}
// Empty field = wildcard. lvlFt 0 = no level filter. Exact (case-insensitive) match on ends.
inline std::vector<size_t> SrdSearch(const SrdData& d, const std::string& adep, const std::string& ades, int lvlFt) {
    std::vector<size_t> out;
    std::string a = Upper(Trim(adep)), b = Upper(Trim(ades));
    for (size_t i = 0; i < d.rows.size(); i++) {
        const auto& r = d.rows[i];
        if (!a.empty() && r.adep != a) continue;
        if (!b.empty() && r.ades != b) continue;
        if (lvlFt > 0 && ((r.minLvl && lvlFt < r.minLvl) || (r.maxLvl && lvlFt > r.maxLvl))) continue;
        out.push_back(i);
    }
    return out;
}


// ---- Sector indicators -------------------------------------------------------
// Core /api/plugin/sectors (plain text):  SECTOR|<ESE position ID>|<alias, max 4 chars>
// Only the positions Core lists get an indicator, and the alias is what controllers see on the radar
// (e.g. BS -> SR for ORBB_S_CTR). The ID is the one in the ESE's [POSITIONS], as EuroScope gives it.
using SectorMap = std::unordered_map<std::string, std::string>;   // ESE position ID -> alias

inline SectorMap ParseSectors(const std::string& body) {
    SectorMap m;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f[0] != "SECTOR" || f.size() < 3) continue;
        std::string id = Upper(Trim(f[1])), alias = Upper(Trim(f[2]));
        if (alias.size() > 4) alias.resize(4);
        if (id.empty() || alias.empty()) continue;
        m[id] = alias;
    }
    return m;
}

// The indicator is drawn in the colour of the aircraft's tag, which Core sets per EuroScope flight plan
// state as ui.colour_sector_<name>, named as in EuroScope's Symbology (Datablock: ...).
// FLIGHT_PLAN_STATE_*: 0 non concerned, 1 notified, 2 coordinated, 3 transfer to me initiated,
// 4 transfer from me initiated, 5 assumed, 7 redundant.
inline const char* SectorColourName(int state) {
    switch (state) {
    case 1:         return "notified";
    case 2:         return "information";
    case 3:         return "transfer_to_me_initiated";
    case 4: case 5: return "assumed";
    case 7:         return "redundant";
    default:        return "non_concerned";
    }
}

// ---- Core-managed configuration ---------------------------------------------
// Core /api/plugin/config (plain text):  KEY|value   one per line.
// Anything missing falls back to the built-in default, so the plugin still works if Core is down.
struct PluginConfig {
    std::unordered_map<std::string, std::string> kv;
    std::string Get(const std::string& k, const std::string& def = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
    bool GetBool(const std::string& k, bool def) const {
        auto it = kv.find(k);
        if (it == kv.end() || it->second.empty()) return def;
        return it->second != "0" && Upper(it->second) != "FALSE" && Upper(it->second) != "NO";
    }
    int GetInt(const std::string& k, int def, int lo, int hi) const {
        auto it = kv.find(k);
        if (it == kv.end() || it->second.empty()) return def;
        int v = ToInt(it->second);
        return v < lo ? lo : (v > hi ? hi : v);
    }
};
inline PluginConfig ParseConfig(const std::string& body) {
    PluginConfig c;
    for (const auto& line : SplitChar(body, '\n')) {
        std::string t = Trim(line);
        size_t bar = t.find('|');
        if (bar == std::string::npos || bar == 0) continue;
        c.kv[Trim(t.substr(0, bar))] = Trim(t.substr(bar + 1));
    }
    return c;
}
// "#RRGGBB" -> 0x00BBGGRR (Windows COLORREF layout); returns def when malformed.
inline unsigned long ParseColour(const std::string& s, unsigned long def) {
    if (s.size() != 7 || s[0] != '#') return def;
    unsigned long v = 0;
    for (size_t i = 1; i < 7; i++) {
        int d = std::isxdigit((unsigned char)s[i]) ? (std::isdigit((unsigned char)s[i]) ? s[i] - '0' : (std::tolower(s[i]) - 'a' + 10)) : -1;
        if (d < 0) return def;
        v = v * 16 + (unsigned long)d;
    }
    return ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
}
// "0.3.0" style compare: <0, 0, >0
inline int CompareVersions(const std::string& a, const std::string& b) {
    auto pa = SplitChar(a, '.'), pb = SplitChar(b, '.');
    for (size_t i = 0; i < (std::max)(pa.size(), pb.size()); i++) {
        int x = i < pa.size() ? ToInt(pa[i]) : 0, y = i < pb.size() ? ToInt(pb[i]) : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// ---- EuroScope version ------------------------------------------------------
// Core's euroscope.versions, e.g. "3.2.3.2" (3.2c r2): the EuroScope versions the vACC accepts
inline std::vector<std::string> EuroScopeVersions(const std::string& accepted) {
    std::vector<std::string> out;
    for (const std::string& v : SplitChar(accepted, ',')) { std::string t = Trim(v); if (!t.empty()) out.push_back(t); }
    return out;
}
// One of them ("3.2.9" and "3.2.9.0" are the same), or anything when Core lists none
inline bool EuroScopeVersionAccepted(const std::string& running, const std::string& accepted) {
    std::vector<std::string> vs = EuroScopeVersions(accepted);
    for (const std::string& v : vs) if (CompareVersions(running, v) == 0) return true;
    return vs.empty();
}

// ---- Sector file version (AIRAC) --------------------------------------------
// "2610/02", "2610/2" and the ESE header text "AIRAC 2610/1  ++ ..." all mean cycle 2610, revision 2 / 1.
struct Airac {
    int cycle = 0, rev = 0;
    bool valid() const { return cycle > 0; }
    std::string Str() const { char b[24]; std::snprintf(b, sizeof b, "%04d/%02d", cycle, rev); return b; }
};
// Finds the first "dddd/d{1,2}" in text; if marker is non-empty the search starts after its first occurrence.
inline Airac FindAirac(const std::string& text, const std::string& marker = "") {
    size_t pos = 0;
    if (!marker.empty()) {
        pos = Upper(text).find(marker);
        if (pos == std::string::npos) return {};
        pos += marker.size();
    }
    for (size_t i = pos; i + 6 < text.size() + 1 && i + 5 <= text.size(); i++) {
        if (!(std::isdigit((unsigned char)text[i]) && std::isdigit((unsigned char)text[i + 1]) &&
              std::isdigit((unsigned char)text[i + 2]) && std::isdigit((unsigned char)text[i + 3]) && text[i + 4] == '/'))
            continue;
        if (i > 0 && std::isdigit((unsigned char)text[i - 1])) continue;
        size_t j = i + 5, n = 0;
        while (j + n < text.size() && std::isdigit((unsigned char)text[j + n]) && n < 2) n++;
        if (n == 0) continue;
        Airac a; a.cycle = std::atoi(text.substr(i, 4).c_str()); a.rev = std::atoi(text.substr(j, n).c_str());
        return a;
    }
    return {};
}
// <0 = local older than upstream, 0 = same, >0 = local newer
// The version in a sector file package's folder name, e.g. ...\ORBB-2610_02 - Copy\ORBB\ -> 2610/02.
// Used when the package has no AIRAC_VERSION.txt and its .ese header gives no version.
inline Airac AiracFromPath(const std::string& path) {
    std::string p = Upper(path);
    for (const char* pkg : { "OKAC-", "ORBB-" }) {
        size_t at = p.rfind(pkg);
        if (at == std::string::npos) continue;
        std::string rest = p.substr(at + 5, 8);
        std::replace(rest.begin(), rest.end(), '_', '/');
        Airac a = FindAirac(rest);
        if (a.valid()) return a;
    }
    return {};
}

// Which sector file package is loaded, "OKAC" or "ORBB" ("" if it can't be told). The package's name
// in the sector file's path wins (the folder nearest the file, e.g. ...\ORBB-2610_02\ORBB\ORBB.ese);
// otherwise the controller's own callsign: OR... is Iraq (ORBB), OK... is Kuwait (OKAC).
inline std::string SectorPackage(const std::string& sectorPath, const std::string& callsign) {
    std::string p = Upper(sectorPath);
    size_t okac = p.rfind("OKAC"), orbb = p.rfind("ORBB");
    if (okac != std::string::npos || orbb != std::string::npos) {
        if (okac == std::string::npos) return "ORBB";
        if (orbb == std::string::npos) return "OKAC";
        return okac > orbb ? "OKAC" : "ORBB";
    }
    std::string c = Upper(Trim(callsign));
    if (c.rfind("OR", 0) == 0) return "ORBB";
    if (c.rfind("OK", 0) == 0) return "OKAC";
    return "";
}

inline int CompareAirac(const Airac& local, const Airac& upstream) {
    if (local.cycle != upstream.cycle) return local.cycle < upstream.cycle ? -1 : 1;
    if (local.rev != upstream.rev) return local.rev < upstream.rev ? -1 : 1;
    return 0;
}

// ---- Self-update helpers ----------------------------------------------------
// SHA-256 (FIPS 180-4), used to verify a downloaded plugin DLL against the hash Core publishes.
inline std::string Sha256Hex(const std::string& data) {
    static const unsigned K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    unsigned h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    auto rotr = [](unsigned x, int n) { return (x >> n) | (x << (32 - n)); };
    std::string msg = data;
    unsigned long long bits = (unsigned long long)data.size() * 8;
    msg.push_back('\x80');                                   // a char literal, so no truncating cast (C4310)
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; i--) msg.push_back((char)((bits >> (i * 8)) & 0xFF));
    for (size_t off = 0; off < msg.size(); off += 64) {
        unsigned w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((unsigned)(unsigned char)msg[off + i * 4] << 24) | ((unsigned)(unsigned char)msg[off + i * 4 + 1] << 16) |
                   ((unsigned)(unsigned char)msg[off + i * 4 + 2] << 8) | (unsigned)(unsigned char)msg[off + i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            unsigned s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            unsigned s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            unsigned S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g);
            unsigned t1 = hh + S1 + ch + K[i] + w[i];
            unsigned S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c);
            unsigned t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; i++) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return out;
}
inline std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
// A downloaded plugin must at least be a 32-bit (x86) Windows DLL before it replaces the current file.
inline bool LooksLikeX86Dll(const std::string& d) {
    auto u8 = [&](size_t i) { return (unsigned)(unsigned char)d[i]; };
    if (d.size() < 0x200 || d[0] != 'M' || d[1] != 'Z') return false;
    size_t pe = u8(0x3C) | (u8(0x3D) << 8) | (u8(0x3E) << 16) | ((size_t)u8(0x3F) << 24);
    if (pe + 24 > d.size() || d.compare(pe, 4, std::string("PE\0\0", 4)) != 0) return false;
    unsigned machine = u8(pe + 4) | (u8(pe + 5) << 8), chr = u8(pe + 22) | (u8(pe + 23) << 8);
    return machine == 0x014c && (chr & 0x2000) != 0;          // I386 + IMAGE_FILE_DLL
}
// True when Core advertises a newer plugin than the running one and gave a checksum to verify it with.
inline bool UpdateAvailable(const std::string& running, const std::string& latest, const std::string& sha) {
    return !latest.empty() && sha.size() == 64 && CompareVersions(running, latest) < 0;
}

// Personal API key from myKWIQ: "kwiq_" then 40 letters and digits. The first 8 after "kwiq_" are its ID.
inline bool IsApiKey(const std::string& k) {
    if (k.size() != 45 || k.compare(0, 5, "kwiq_") != 0) return false;
    for (size_t i = 5; i < k.size(); i++) if (!std::isalnum((unsigned char)k[i])) return false;
    return true;
}
inline std::string ApiKeyId(const std::string& k) { return IsApiKey(k) ? k.substr(5, 8) : std::string(); }

// %LOCALAPPDATA%\KWIQ\settings\api-settings.json, as UKCP keeps its key: {"api-key": "kwiq_..."}.
// A key is only letters, digits and _, so no escaping is needed either way.
inline std::string ApiSettingsJson(const std::string& key) {
    return "{\n    \"api-key\": \"" + key + "\"\n}\n";
}
inline std::string ApiKeyFromSettingsJson(const std::string& json) {
    size_t k = json.find("\"api-key\"");
    if (k == std::string::npos) return "";
    size_t colon = json.find(':', k + 9);
    size_t open = colon == std::string::npos ? colon : json.find('"', colon + 1);
    size_t close = open == std::string::npos ? open : json.find('"', open + 1);
    if (close == std::string::npos) return "";
    std::string v = Trim(json.substr(open + 1, close - open - 1));
    return IsApiKey(v) ? v : "";
}

// What the updater tells the loader to do with the plugin (KWIQUpdaterRun)
enum class StartMode { Full = 0, Reduced = 1, DontLoad = 2 };
// As EuroScope starts: Core's kill switch stops the plugin loading; an update the controller declined
// starts it reduced; no plugin installed and none to install means nothing to load.
inline StartMode DecideStart(bool coreAnswered, bool killSwitch, bool installed, bool updateDeclined) {
    if (coreAnswered && killSwitch) return StartMode::DontLoad;
    if (!installed) return StartMode::DontLoad;
    return updateDeclined ? StartMode::Reduced : StartMode::Full;
}

// One query value from the first line of an HTTP request ("GET /?key=...&state=... HTTP/1.1").
// Only letters, digits and _ are expected, so no decoding is done; anything else gives "".
inline std::string QueryValue(const std::string& request, const std::string& name) {
    size_t end = request.find_first_of("\r\n");
    std::string line = request.substr(0, end);
    size_t q = line.find('?');
    if (line.compare(0, 4, "GET ") != 0 || q == std::string::npos) return "";
    size_t stop = line.find(' ', q);
    std::string query = line.substr(q + 1, stop == std::string::npos ? std::string::npos : stop - q - 1);
    for (const auto& part : SplitChar(query, '&')) {
        size_t eq = part.find('=');
        if (eq == std::string::npos || part.substr(0, eq) != name) continue;
        std::string v = part.substr(eq + 1);
        for (char c : v) if (!std::isalnum((unsigned char)c) && c != '_') return "";
        return v;
    }
    return "";
}

// ---- RDF and the TrackAudio bridge (built in from KingfuChan/RDF) --------------------------------
// Core /api/plugin/rdf (plain text), one line per callsign pattern, in Core's order (the first match wins):
//   RDF|<pattern>|draw=1|bridge=1|colour=#FFFFFF|colour_concurrent=#FF0000|radius=20|threshold=-1|precision=0|
//       low_altitude=0|high_altitude=0|low_precision=0|high_precision=0|controllers=0|require_tx=0
// The pattern is a logon callsign with * (any characters) and ? (one character), e.g. ORBB_*_CTR or *.
// A key left out takes RDF's default. With no matching line: no drawing, bridge on.
struct RdfSettings {
    bool draw = false, bridge = true, controllers = false, requireTx = false;
    unsigned long colour = 0xFFFFFF, colourConcurrent = 0x0000FF;   // COLORREF (0x00BBGGRR): white, red
    int radius = 20, threshold = -1, precision = 0, lowAltitude = 0, highAltitude = 0, lowPrecision = 0, highPrecision = 0;
};
struct RdfRow { std::string pattern; RdfSettings s; };

inline bool WildcardMatch(const std::string& pattern, const std::string& text) {
    size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || std::toupper((unsigned char)pattern[p]) == std::toupper((unsigned char)text[t]))) { p++; t++; }
        else if (p < pattern.size() && pattern[p] == '*') { star = p++; mark = t; }
        else if (star != std::string::npos) { p = star + 1; t = ++mark; }
        else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') p++;
    return p == pattern.size();
}

inline std::vector<RdfRow> ParseRdf(const std::string& body) {
    std::vector<RdfRow> rows;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() < 2 || f[0] != "RDF" || Trim(f[1]).empty()) continue;
        RdfRow r;
        r.pattern = Upper(Trim(f[1]));
        for (size_t i = 2; i < f.size(); i++) {
            size_t eq = f[i].find('=');
            if (eq == std::string::npos) continue;
            std::string k = Lower(Trim(f[i].substr(0, eq))), v = Trim(f[i].substr(eq + 1));
            auto num = [&](int& out, int lo) { try { int n = std::stoi(v); if (n >= lo) out = n; } catch (...) {} };
            auto flag = [&](bool& out) { if (v == "1" || Upper(v) == "ON") out = true; else if (v == "0" || Upper(v) == "OFF") out = false; };
            if (k == "draw") flag(r.s.draw);
            else if (k == "bridge") flag(r.s.bridge);
            else if (k == "controllers") flag(r.s.controllers);
            else if (k == "require_tx") flag(r.s.requireTx);
            else if (k == "colour") r.s.colour = ParseColour(v, r.s.colour);
            else if (k == "colour_concurrent") r.s.colourConcurrent = ParseColour(v, r.s.colourConcurrent);
            else if (k == "radius") num(r.s.radius, 1);
            else if (k == "threshold") num(r.s.threshold, -1000000);
            else if (k == "precision") num(r.s.precision, 0);
            else if (k == "low_altitude") num(r.s.lowAltitude, -100000);
            else if (k == "high_altitude") num(r.s.highAltitude, 1);
            else if (k == "low_precision") num(r.s.lowPrecision, 0);
            else if (k == "high_precision") num(r.s.highPrecision, 0);
        }
        rows.push_back(r);
    }
    return rows;
}
inline RdfSettings RdfFor(const std::vector<RdfRow>& rows, const std::string& callsign) {
    for (const auto& r : rows) if (WildcardMatch(r.pattern, Upper(callsign))) return r.s;
    return RdfSettings();
}

// TrackAudio's version, from the text its HTTP endpoint answers with (e.g. "TrackAudio v1.3.0-beta.5").
// RDF needs 1.3.0 Beta 5 or later. Prerelease order: alpha < beta < rc < the release. False if unreadable.
struct SemVer { int major = 0, minor = 0, patch = 0, pre = 3, preNum = 0; bool valid = false; };
inline SemVer ParseSemVer(const std::string& text) {
    SemVer v;
    for (size_t i = 0; i < text.size(); i++) {
        if (!std::isdigit((unsigned char)text[i]) || (i && std::isdigit((unsigned char)text[i - 1]))) continue;
        int a, b, c, n = 0;
        if (std::sscanf(text.c_str() + i, "%d.%d.%d%n", &a, &b, &c, &n) != 3) continue;
        v.major = a; v.minor = b; v.patch = c; v.valid = true;
        std::string rest = Lower(text.substr(i + n));
        if (!rest.empty() && rest[0] == '-') {
            v.pre = rest.compare(1, 5, "alpha") == 0 ? 0 : rest.compare(1, 4, "beta") == 0 ? 1 : rest.compare(1, 2, "rc") == 0 ? 2 : 0;
            size_t d = rest.find_first_of("0123456789");
            v.preNum = d == std::string::npos ? 0 : std::atoi(rest.c_str() + d);
        }
        return v;
    }
    return v;
}
inline bool TrackAudioVersionOk(const std::string& text) {
    SemVer v = ParseSemVer(text);
    if (!v.valid) return false;
    auto key = [](const SemVer& s) { return std::make_tuple(s.major, s.minor, s.patch, s.pre, s.preNum); };
    SemVer min; min.major = 1; min.minor = 3; min.patch = 0; min.pre = 1; min.preNum = 5;
    return key(v) >= key(min);
}

// The KWIQ menu's TrackAudio row: the state of the connection to TrackAudio, which the plugin makes on its own.
//   running:   RDF started (not when another RDF or afv-euroscope-bridge is loaded)
//   inUse:     Core's settings (or the controller's RDF Drawing) use TrackAudio: the bridge or drawing is on
//   wsOpen:    TrackAudio's WebSocket is open
//   version:   what TrackAudio said its version is (empty if it didn't); an unreadable one isn't "too old"
//   afvRecent: the AFV standalone client was heard from in the last 10 minutes
enum class TrackAudioState { Off, Connected, TooOld, AfvClient, NotRunning };
inline TrackAudioState TrackAudioStatus(bool running, bool inUse, bool wsOpen, const std::string& version, bool afvRecent) {
    if (!running || !inUse) return TrackAudioState::Off;
    if (wsOpen) return ParseSemVer(version).valid && !TrackAudioVersionOk(version) ? TrackAudioState::TooOld : TrackAudioState::Connected;
    return afvRecent ? TrackAudioState::AfvClient : TrackAudioState::NotRunning;
}
inline const char* TrackAudioStateText(TrackAudioState s) {
    switch (s) {
    case TrackAudioState::Connected:  return "Connected";
    case TrackAudioState::TooOld:     return "Version too old";
    case TrackAudioState::AfvClient:  return "AFV client";
    case TrackAudioState::NotRunning: return "Not running";
    default:                          return "Off";
    }
}
// What a click on the row does: with TrackAudio connected, sync the channels again (.rdf refresh); without it,
// try to connect now (leaving EuroScope's channels as they are); when off, nothing
enum class TrackAudioClick { None, Resync, Reconnect };
inline TrackAudioClick TrackAudioClickAction(TrackAudioState s) {
    switch (s) {
    case TrackAudioState::Connected:
    case TrackAudioState::TooOld:     return TrackAudioClick::Resync;
    case TrackAudioState::AfvClient:
    case TrackAudioState::NotRunning: return TrackAudioClick::Reconnect;
    default:                          return TrackAudioClick::None;
    }
}

// ---- DiscordEuroScope (Discord Rich Presence, built in from Kirollos/DiscordEuroScope) ----------------
// Core /api/plugin/discord (plain text). Every MESSAGE, IMAGE and BUTTON line has four filters, empty = any:
// state (idle, playback, sweatbox, observer, controller, supervisor), FIR (OKAC, ORBB), logon callsign
// pattern (* and ?) and event (1 = only while a Core event is on, 0 = only when none is).
//   APP|<Discord application ID>
//   SWEATBOX_BYPASS|1                 (sweatbox shows as a normal logon)
//   MESSAGE|<state>|<fir>|<pattern>|<event>|<details>|<state text>
//   IMAGE|<state>|<fir>|<pattern>|<event>|<large image URL or key>|<large text>|<small image URL or key>|<small text>
//   BUTTON|<state>|<fir>|<pattern>|<event>|<label>|<url>          (at most two are shown, in Core's order)
//   RADIO|<ICAO prefix>|<frequency>|<radio callsign>              e.g. RADIO|ORBB|124.400|Baghdad Control
// At logon (and whenever the state changes) the plugin takes the matching entries with the most filters set
// and picks one at random, skipping a message whose placeholders have no value. Texts may use {callsign},
// {rcallsign}, {frequency}, {current_tracked}, {total_tracked}, {total_inrange} and {event}.
struct DiscordFilter { std::string state, fir, pattern; int event = -1; };
struct DiscordEntry { DiscordFilter f; std::vector<std::string> v; };
struct DiscordConfig {
    std::string appId;
    bool sweatboxBypass = false;
    std::vector<DiscordEntry> messages, images, buttons;
    std::vector<std::array<std::string, 3>> radios;          // ICAO prefix, frequency, radio callsign
};
struct DiscordContext { std::string state, fir, callsign; bool event = false; };

inline DiscordConfig ParseDiscord(const std::string& body) {
    DiscordConfig c;
    for (const auto& raw : SplitChar(body, '\n')) {
        std::string line = raw;
        while (!line.empty() && (line.back() == '\r')) line.pop_back();
        auto f = SplitChar(line, '|');
        std::string kind = Upper(Trim(f[0]));
        if (kind == "APP" && f.size() >= 2) c.appId = Trim(f[1]);
        else if (kind == "SWEATBOX_BYPASS" && f.size() >= 2) c.sweatboxBypass = Trim(f[1]) == "1";
        else if (kind == "RADIO" && f.size() >= 4) c.radios.push_back({ Upper(Trim(f[1])), Trim(f[2]), Trim(f[3]) });
        else if ((kind == "MESSAGE" || kind == "IMAGE" || kind == "BUTTON") && f.size() >= 6) {
            DiscordEntry e;
            e.f.state = Lower(Trim(f[1])); e.f.fir = Upper(Trim(f[2])); e.f.pattern = Upper(Trim(f[3]));
            std::string ev = Trim(f[4]);
            e.f.event = ev == "1" ? 1 : ev == "0" ? 0 : -1;
            for (size_t i = 5; i < f.size(); i++) e.v.push_back(f[i]);
            size_t want = kind == "MESSAGE" ? 2 : kind == "IMAGE" ? 4 : 2;
            e.v.resize(want);
            if (kind == "MESSAGE") c.messages.push_back(e);
            else if (kind == "IMAGE") c.images.push_back(e);
            else if (!Trim(e.v[0]).empty() && !Trim(e.v[1]).empty()) c.buttons.push_back(e);
        }
    }
    return c;
}
// -1 if the entry does not apply, else how many of its filters are set
inline int DiscordMatch(const DiscordFilter& f, const DiscordContext& x) {
    if (!f.state.empty() && f.state != x.state) return -1;
    if (!f.fir.empty() && f.fir != Upper(x.fir)) return -1;
    if (!f.pattern.empty() && !WildcardMatch(f.pattern, x.callsign)) return -1;
    if (f.event >= 0 && (f.event == 1) != x.event) return -1;
    return (int)!f.state.empty() + (int)!f.fir.empty() + (int)!f.pattern.empty() + (int)(f.event >= 0);
}
// {name} placeholders. A placeholder with no value (missing or empty) makes the text unusable.
inline bool DiscordFormat(const std::string& text, const std::map<std::string, std::string>& values, std::string& out) {
    out.clear();
    bool ok = true;
    for (size_t i = 0; i < text.size(); i++) {
        size_t close = text[i] == '{' ? text.find('}', i) : std::string::npos;
        if (close == std::string::npos) { out += text[i]; continue; }
        auto it = values.find(text.substr(i + 1, close - i - 1));
        if (it == values.end()) { out += text.substr(i, close - i + 1); i = close; continue; }   // not a placeholder
        if (it->second.empty()) ok = false;
        out += it->second;
        i = close;
    }
    return ok;
}
// The entry to use: among the matching entries with the most filters set, one picked by `roll` (random),
// preferring those whose texts all have values. -1 if none matches.
inline int DiscordPick(const std::vector<DiscordEntry>& entries, const DiscordContext& x,
                       const std::map<std::string, std::string>& values, unsigned roll) {
    std::map<int, std::vector<int>, std::greater<int>> tiers;
    for (size_t i = 0; i < entries.size(); i++) {
        int m = DiscordMatch(entries[i].f, x);
        if (m >= 0) tiers[m].push_back((int)i);
    }
    int fallback = -1;
    for (auto& t : tiers) {
        std::vector<int> usable;
        for (int i : t.second) {
            bool ok = true;
            std::string tmp;
            for (const auto& s : entries[i].v) ok = DiscordFormat(s, values, tmp) && ok;
            if (ok) usable.push_back(i);
        }
        if (!usable.empty()) return usable[roll % usable.size()];
        if (fallback < 0) fallback = t.second[roll % t.second.size()];
    }
    return fallback;
}
// The longest start of text that fits in maxBytes without ending inside a UTF-8 character (#50). Discord's
// limits are in bytes (128 for texts, 32 for button labels), and half a character is invalid text to it.
inline std::string Utf8Truncate(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t n = maxBytes;                                      // text[n] is the first byte left out
    while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;   // inside a character: cut before it
    return text.substr(0, n);
}
// Discord refuses a button whose URL is longer than this
constexpr size_t kDiscordUrlMax = 512;
// DiscordEuroScope's lookup: the ICAO prefix of the callsign and the primary frequency
inline std::string DiscordRadioCallsign(const DiscordConfig& c, const std::string& callsign, const std::string& freq) {
    std::string icao = Upper(callsign.substr(0, callsign.find('_')));
    for (const auto& r : c.radios) if (r[0] == icao && r[1] == freq) return r[2];
    return "";
}

// ---- QNH monitor -----------------------------------------------------------------------------------
// The QNH in a METAR, in hPa: Q1013, or A2992 (inches of mercury) converted. -1 if there is none.
inline int QnhFromMetar(const std::string& metar) {
    std::istringstream in(metar);
    std::string tok;
    while (in >> tok) {
        if (tok == "RMK") break;
        while (!tok.empty() && tok.back() == '=') tok.pop_back();
        if (tok.size() == 5 && (tok[0] == 'Q' || tok[0] == 'A') && std::all_of(tok.begin() + 1, tok.end(), ::isdigit)) {
            int v = std::atoi(tok.c_str() + 1);
            return tok[0] == 'Q' ? v : (int)std::floor(v / 100.0 * 33.8639);
        }
    }
    return -1;
}
// Core /api/plugin/metar: METAR|<ICAO>|<METAR>
inline std::map<std::string, std::string> ParseMetars(const std::string& body) {
    std::map<std::string, std::string> out;
    for (const auto& line : SplitChar(body, '\n')) {
        size_t a = line.find('|'), b = a == std::string::npos ? a : line.find('|', a + 1);
        if (b == std::string::npos || Trim(line.substr(0, a)) != "METAR") continue;
        std::string icao = Upper(Trim(line.substr(a + 1, b - a - 1)));
        if (IsAirportCode(icao)) out[icao] = Trim(line.substr(b + 1));
    }
    return out;
}

// Transition levels, from Core /api/plugin/trl, the way TopSky's QNHTL lines give them:
//   TRL|<ICAO or ICAO prefix>|<TA in ft>|<TRL>,<QNH>,<TRL>,<QNH>,<TRL>...
// e.g. TRL|OR|14000|160,980,170 (FL160 at QNH 980 and above, FL170 below) and TRL|OK|13000|150 (fixed).
// The longest matching prefix wins.
struct TrlRule { std::string prefix; int ta = 0; std::vector<int> v; };
inline std::vector<TrlRule> ParseTrl(const std::string& body) {
    std::vector<TrlRule> rules;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() < 4 || f[0] != "TRL") continue;
        TrlRule r;
        r.prefix = Upper(Trim(f[1]));
        r.ta = ToInt(f[2]);
        for (const auto& t : SplitChar(f[3], ',')) { std::string x = Trim(t); if (!x.empty()) r.v.push_back(ToInt(x)); }
        if (!r.prefix.empty() && r.v.size() % 2 == 1) rules.push_back(r);
    }
    return rules;
}
// The TRL (flight level, e.g. 160) at that QNH, or 0 if Core has none for the airport
inline int TrlFor(const std::vector<TrlRule>& rules, const std::string& icao, int qnh) {
    const TrlRule* best = nullptr;
    for (const auto& r : rules)
        if (Upper(icao).compare(0, r.prefix.size(), r.prefix) == 0 && (!best || r.prefix.size() > best->prefix.size())) best = &r;
    if (!best || qnh <= 0) return 0;
    for (size_t i = 0; i + 1 < best->v.size(); i += 2) if (qnh >= best->v[i + 1]) return best->v[i];
    return best->v.back();
}
// As in #4: "ORBI QNH 979 (was 981) TRL FL170 - CHANGED from FL160"
inline std::string QnhMessage(const std::string& icao, int qnh, int prevQnh, int trl, int prevTrl) {
    std::string m = icao + " QNH " + std::to_string(qnh);
    if (prevQnh > 0 && prevQnh != qnh) m += " (was " + std::to_string(prevQnh) + ")";
    if (trl > 0) {
        m += " TRL FL" + std::to_string(trl);
        if (prevTrl > 0 && prevTrl != trl) m += " - CHANGED from FL" + std::to_string(prevTrl);
    }
    return m;
}

// ---- top-down (shared by the QNH monitor and procedure notifications) --------------------------------
// Core /api/plugin/topdown: TOPDOWN|<airport>|<position>,<position>,...  lowest first,
// e.g. TOPDOWN|OKBK|OKBK_DEL,OKBK_GND,OKBK_TWR,OKBK_APP,OKAC_CTR
// A position (OKBK_TWR) is worked by OKBK_TWR, OKBK_1_TWR, OKBK_N_TWR: same prefix and suffix.
using TopDownOrders = std::map<std::string, std::vector<std::string>>;
inline bool TopDownPositionMatches(const std::string& position, const std::string& callsign) {
    size_t a = position.find('_'), b = callsign.find('_');
    if (a == std::string::npos || b == std::string::npos) return false;
    return Upper(position.substr(0, a)) == Upper(callsign.substr(0, b)) &&
           Upper(position.substr(position.rfind('_') + 1)) == Upper(callsign.substr(callsign.rfind('_') + 1));
}
inline TopDownOrders ParseTopDownOrders(const std::string& body) {
    TopDownOrders td;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() < 3 || f[0] != "TOPDOWN" || !IsAirportCode(Upper(Trim(f[1])))) continue;
        std::vector<std::string> order;
        for (const auto& p : SplitChar(f[2], ',')) { std::string x = Upper(Trim(p)); if (x.find('_') != std::string::npos) order.push_back(x); }
        if (!order.empty()) td[Upper(Trim(f[1]))] = order;
    }
    return td;
}
// The airport is the user's: the lowest position in its top-down order that anyone is on is one the user
// is on (online = the other controllers' callsigns). An airport with no order: its own positions only.
inline bool CoveredByMe(const TopDownOrders& td, const std::string& airport, const std::string& me,
                        const std::vector<std::string>& online) {
    if (airport.empty() || me.empty()) return false;
    auto it = td.find(airport);
    std::vector<std::string> order = it != td.end() ? it->second
        : std::vector<std::string>{ airport + "_DEL", airport + "_GND", airport + "_TWR", airport + "_APP" };
    for (const auto& pos : order) {
        if (TopDownPositionMatches(pos, me)) return true;
        for (const auto& cs : online) if (TopDownPositionMatches(pos, cs)) return false;
    }
    return false;
}
// Every airport Core has an order for (and the user's own) that the user covers now
inline std::vector<std::string> AirportsCovered(const TopDownOrders& td, const std::string& me, const std::vector<std::string>& online) {
    std::vector<std::string> out;
    for (const auto& kv : td) if (CoveredByMe(td, kv.first, me, online)) out.push_back(kv.first);
    std::string own = Upper(me.substr(0, me.find('_')));
    if (IsAirportCode(own) && !td.count(own) && CoveredByMe(td, own, me, online)) out.push_back(own);
    return out;
}

// ---- procedure notifications ---------------------------------------------------------------------
// Core /api/plugin/notifications (for the controller's API key):
//   NOTICE|<id>|<airports, comma separated>|<published, YYYY-MM-DD>|<read 0/1>|<title>|<link>|<text, \n for new lines>
// Staff post them in Filament for a group of airports (an FIR, a TMA, the military airports...).
struct ProcedureNotice { std::string id, published, title, link, text; std::vector<std::string> airports; bool read = false; };
inline std::vector<ProcedureNotice> ParseNotices(const std::string& body) {
    std::vector<ProcedureNotice> out;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(line, '|');
        if (f.size() < 8 || Trim(f[0]) != "NOTICE" || Trim(f[1]).empty()) continue;
        ProcedureNotice n;
        n.id = Trim(f[1]);
        for (const auto& a : SplitChar(f[2], ',')) { std::string x = Upper(Trim(a)); if (!x.empty()) n.airports.push_back(x); }
        n.published = Trim(f[3]);
        n.read = Trim(f[4]) == "1";
        n.title = Trim(f[5]);
        n.link = Trim(f[6]);
        std::string t;
        for (size_t i = 7; i < f.size(); i++) t += (i > 7 ? "|" : "") + f[i];
        for (size_t i = 0; i < t.size(); i++) {
            if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == 'n') { n.text += "\r\n"; i++; }
            else if (t[i] != '\r') n.text += t[i];
        }
        out.push_back(n);
    }
    return out;
}
inline bool NoticeRelevant(const ProcedureNotice& n, const std::vector<std::string>& covered) {
    for (const auto& a : n.airports) if (std::find(covered.begin(), covered.end(), a) != covered.end()) return true;
    return false;
}
// A value for one segment of a URL path (a notification's ID in /notifications/<id>/read): everything but
// RFC 3986's unreserved characters is percent-encoded (#49), so "/", "?", "#" or a space can't change the URL
inline std::string UrlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

// ---- the KWIQ button (#59) ------------------------------------------------------------------------
// A size in pixels at 96 DPI, for a screen at dpi (rounded; 0 or less: unscaled)
inline int ScaleForDpi(int px, int dpi) { return dpi > 0 ? (px * dpi + 48) / 96 : px; }

// The button's place is kept as its offset from the radar area's bottom-right corner (from the button's own
// bottom-right corner), so it follows the window as it resizes. Positions are in pixels from the radar
// area's top left; the button is a square of `size` px in a radar area w x h px.
struct ButtonXY { int x = 0, y = 0; };
inline int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
// Where its top-left corner goes for an offset, clamped so the whole button stays inside the radar area
// (at the top left when the area is smaller than the button)
inline ButtonXY ButtonTopLeft(int w, int h, int size, ButtonXY offset) {
    int maxX = w - size > 0 ? w - size : 0, maxY = h - size > 0 ? h - size : 0;
    ButtonXY p;
    p.x = ClampInt(maxX - offset.x, 0, maxX);
    p.y = ClampInt(maxY - offset.y, 0, maxY);
    return p;
}
// The offset for a button whose top-left corner is at (x, y), clamped as above
inline ButtonXY ButtonOffsetFromTopLeft(int w, int h, int size, int x, int y) {
    int maxX = w - size > 0 ? w - size : 0, maxY = h - size > 0 ? h - size : 0;
    ButtonXY o;
    o.x = maxX - ClampInt(x, 0, maxX);
    o.y = maxY - ClampInt(y, 0, maxY);
    return o;
}
// The offset while it is dragged: centred on the mouse at (px, py), clamped as above
inline ButtonXY ButtonOffsetAt(int w, int h, int size, int px, int py) {
    return ButtonOffsetFromTopLeft(w, h, size, px - size / 2, py - size / 2);
}
// "x,y" as saved in the profile (kwiq_button_offset, and the older kwiq_button_at): two whole numbers, 0 or more
inline bool ParseButtonXY(const std::string& s, ButtonXY& out) {
    size_t comma = s.find(',');
    if (comma == std::string::npos) return false;
    auto num = [](const std::string& t, int& v) {
        if (t.empty() || t.size() > 6) return false;
        v = 0;
        for (char c : t) { if (c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
        return true;
    };
    ButtonXY p;
    if (!num(s.substr(0, comma), p.x) || !num(s.substr(comma + 1), p.y)) return false;
    out = p;
    return true;
}
inline std::string FormatButtonXY(ButtonXY p) { return std::to_string(p.x) + "," + std::to_string(p.y); }

// The logo is embedded at a few sizes: the one to draw at `want` px is the smallest that is at least that big
// (scaled down a little), or the biggest there is. 0 when there are none.
inline int BestLogoSize(const std::vector<int>& sizes, int want) {
    int best = 0, biggest = 0;
    for (int s : sizes) {
        if (s > biggest) biggest = s;
        if (s >= want && (best == 0 || s < best)) best = s;
    }
    return best ? best : biggest;
}
// 32-bit BGRA pixels with straight alpha to premultiplied, as AlphaBlend needs (rounded)
inline void PremultiplyBgra(unsigned char* px, size_t count) {
    for (size_t i = 0; i < count; i++) {
        unsigned a = px[i * 4 + 3];
        for (int c = 0; c < 3; c++) px[i * 4 + c] = (unsigned char)((px[i * 4 + c] * a + 127) / 255);
    }
}

} // namespace kwiq
