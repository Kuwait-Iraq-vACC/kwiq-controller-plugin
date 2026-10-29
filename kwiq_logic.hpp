// kwiq_logic.hpp - pure logic for the KWIQ plugin (no Windows / EuroScope dependencies).
// Route matching, event routes, SRD (shared by FPC and SRD search). Unit-tested in tests/test_logic.cpp.
// License: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

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

// ---- SRD (shared by FPC and SRD search) ------------------------------------
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
    std::vector<size_t> wp;                                    // rows with a waypoint end
    std::string version;
};
inline bool IsAirportCode(const std::string& s) {
    if (s.size() != 4) return false;
    for (char c : s) if (!std::isalpha((unsigned char)c)) return false;
    return true;
}
inline void BuildSrd(SrdData& d) {
    d.pair.clear(); d.wp.clear();
    for (size_t i = 0; i < d.rows.size(); i++) {
        const auto& r = d.rows[i];
        if (IsAirportCode(r.adep) && IsAirportCode(r.ades)) d.pair[r.adep + "|" + r.ades].push_back(i);
        else d.wp.push_back(i);
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
            r.minLvl = std::atoi(f[3].c_str()); r.maxLvl = std::atoi(f[4].c_str());
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
    int v = std::atoi(d.c_str());
    return v < 1000 ? v * 100 : v;
}

// SRD rows that apply to a flight. Airport-pair rows (ADEP|ADES) take precedence; only when
// there are none do enroute rows apply (waypoint ends that are both on the filed route).
inline void Candidates(const SrdData& d, const std::string& adep, const std::string& ades,
                       const std::vector<std::string>& filed, std::vector<size_t>& out) {
    out.clear();
    auto it = d.pair.find(adep + "|" + ades);
    if (it != d.pair.end()) { out = it->second; return; }
    auto on = [&](const std::string& end, const std::string& airport) {
        return IsAirportCode(end) ? end == airport : std::find(filed.begin(), filed.end(), end) != filed.end();
    };
    for (size_t i : d.wp) if (on(d.rows[i].adep, adep) && on(d.rows[i].ades, ades)) out.push_back(i);
}

enum class FpcResult { None, Ok, Route, Min, Max };

inline FpcResult CheckFpc(const SrdData& d, const std::string& adep, const std::string& ades,
                          const std::vector<std::string>& filed, int rflFt) {
    std::vector<size_t> c;
    Candidates(d, adep, ades, filed, c);
    if (c.empty()) return FpcResult::None;
    bool routeOk = false;
    FpcResult lvl = FpcResult::None;
    for (size_t i : c) {
        const auto& r = d.rows[i];
        if (!MatchTokens(r.tokens, filed)) continue;
        routeOk = true;
        if (rflFt > 0 && r.minLvl && rflFt < r.minLvl) { if (lvl == FpcResult::None) lvl = FpcResult::Min; }
        else if (rflFt > 0 && r.maxLvl && rflFt > r.maxLvl) { if (lvl == FpcResult::None) lvl = FpcResult::Max; }
        else return FpcResult::Ok;
    }
    return routeOk ? lvl : FpcResult::Route;
}
inline std::string ExplainFpc(const SrdData& d, const std::string& adep, const std::string& ades,
                              const std::string& filedRte, int rflFt) {
    auto filed = NormaliseRoute(filedRte);
    std::vector<size_t> c;
    Candidates(d, adep, ades, filed, c);
    std::string head = adep + " -> " + ades + " " + (rflFt > 0 ? FlStr(rflFt) : "FL---") + ": ";
    switch (CheckFpc(d, adep, ades, filed, rflFt)) {
    case FpcResult::None:  return head + "no SRD route defined for this pair.";
    case FpcResult::Ok:    return head + "route and level conform to the SRD.";
    case FpcResult::Min:
    case FpcResult::Max: {
        std::string s = head + "route is valid but the level is outside the SRD limits. Allowed:";
        for (size_t i : c) {
            const auto& r = d.rows[i];
            if (!MatchTokens(r.tokens, filed)) continue;
            s += " [" + (r.minLvl ? FlStr(r.minLvl) : std::string("any")) + " to " +
                 (r.maxLvl ? FlStr(r.maxLvl) : std::string("any")) + "]";
        }
        return s;
    }
    case FpcResult::Route: break;
    }
    std::string s = head + "filed route does not match the SRD. Expected one of:";
    size_t shown = 0;
    for (size_t i : c) {
        if (shown++ == 3) { s += " ... (+" + std::to_string(c.size() - 3) + " more)"; break; }
        s += " | " + d.rows[i].rte;
    }
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
        int v = std::atoi(it->second.c_str());
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
        int x = i < pa.size() ? std::atoi(pa[i].c_str()) : 0, y = i < pb.size() ? std::atoi(pb[i].c_str()) : 0;
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
    msg.push_back((char)0x80);
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

} // namespace kwiq
