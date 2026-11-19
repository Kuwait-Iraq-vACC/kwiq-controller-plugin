// fpc_logic.hpp - flight plan check (FPC), adapted from VFPC (https://github.com/hpeter2/VFPC, from
// https://github.com/DrFreas/VFPC; analyzeFP.cpp validizeSid and getFails; GPL-3.0): the rules the vACC
// keeps in VFPC's Sid.json, per departure airport and SID. Separate from the SRD check in kwiq_logic.hpp.
// Pure logic (no Windows / EuroScope dependencies), unit-tested in tests/test_logic.cpp.
// License: GPL-3.0-or-later
#pragma once

#include "kwiq_logic.hpp"

namespace kwiq::fpc {

// ---- rules -----------------------------------------------------------------------------------
// Core /api/plugin/fpc (plain text), one line per Sid.json condition, in Sid.json's order:
//   FPC|<airport>|<sid>|<suffix>|<direction>|<min_fl>|<max_fl>|<forbidden_fls>|<destinations>|<airways>|<engine>|<navigation>
// <sid> is the SID's first waypoint (SESRU for SESRU1A), or the aerodrome's VOR (ORBB keys every airport's rules on
// it: BGD, BSR, ...); lists are comma separated; empty = no restriction.
// Filters (the condition is only for flights that fit): suffix, destinations (prefixes, OM = any OM..), airways
// (an airway or a run of route elements, e.g. "NAMDI M860 NINVA"; "DCT" = the route has a DCT in it). Checks:
// engine, direction, min_fl, max_fl, forbidden_fls, navigation.
struct Condition {
    std::string suffix;                                 // e.g. 1A
    std::string direction = "ANY";                      // EVEN, ODD or ANY
    int minFl = 0, maxFl = 0;                           // flight levels, 0 = none
    std::vector<int> forbidden;                         // flight levels
    std::vector<std::string> destinations, engines;     // engines: P, T, J, E
    std::vector<std::vector<std::string>> airways;      // each one normalised, as route elements
    std::string navigation;                             // allowed equipment codes, e.g. "LGZ"; empty = any
};
struct AirportRules {
    std::unordered_map<std::string, std::vector<Condition>> sids;           // SID first waypoint (or VOR) -> conditions
    std::vector<std::string> order;                                         // the keys of `sids`, in Core's order
};
using Rules = std::unordered_map<std::string, AirportRules>;                // departure airport -> its rules

inline std::vector<std::string> SplitList(const std::string& s) {
    std::vector<std::string> out;
    for (const auto& p : SplitChar(s, ',')) {
        std::string t = Upper(Trim(p));
        if (!t.empty()) out.push_back(t);
    }
    return out;
}
inline Rules ParseRules(const std::string& body) {
    Rules r;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() < 12 || f[0] != "FPC") continue;
        std::string apt = Upper(Trim(f[1])), sid = Upper(Trim(f[2]));
        if (!IsAirportCode(apt) || sid.empty()) continue;
        Condition c;
        c.suffix = Upper(Trim(f[3]));
        std::string dir = Upper(Trim(f[4]));
        c.direction = dir.empty() ? "ANY" : dir;
        c.minFl = std::atoi(Trim(f[5]).c_str());
        c.maxFl = std::atoi(Trim(f[6]).c_str());
        for (const auto& fl : SplitList(f[7])) c.forbidden.push_back(std::atoi(fl.c_str()));
        c.destinations = SplitList(f[8]);
        for (const auto& a : SplitList(f[9])) {
            auto toks = a == "DCT" ? std::vector<std::string>{ "DCT" } : NormaliseRoute(a);   // DCT alone is an option
            if (!toks.empty()) c.airways.push_back(toks);
        }
        c.engines = SplitList(f[10]);
        c.navigation = Upper(Trim(f[11]));
        auto& ar = r[apt];
        auto& conds = ar.sids[sid];
        if (conds.empty()) ar.order.push_back(sid);
        conds.push_back(std::move(c));
    }
    return r;
}

// ---- the check (VFPC validizeSid) -------------------------------------------------------------
struct Flight {
    std::string origin, destination, route, sid;        // sid: EuroScope's SID name, e.g. SESRU1A
    int rflFt = 0;
    char engine = 0;                                    // EuroScope's engine type: P, T, J, E
    char capability = 0;                                // EuroScope's equipment code
    bool vfr = false;
};
enum class Fail { Sid, Engine, Direction, Min, Max, Forbidden, Navigation };
// VFPC's tag codes
inline const char* Code(Fail f) {
    switch (f) {
    case Fail::Sid:        return "SID";
    case Fail::Engine:     return "ENG";
    case Fail::Direction:  return "E/O";
    case Fail::Min:        return "MIN";
    case Fail::Max:        return "MAX";
    case Fail::Forbidden:  return "FLR";
    case Fail::Navigation: return "NAV";
    }
    return "FPL";
}
struct Result {
    bool checked = false;                               // the departure airport has rules (VFR: always)
    bool vfr = false;
    std::string sidPoint, suffix;                       // sidPoint: whose rules were used
    bool byRoute = false;                               // the SID had no rules (or there was none): found by the route
    bool noSid = false;                                 // the flight plan has no SID set yet
    std::vector<Fail> fails;
    std::vector<std::string> notes;                     // what was checked, for Explain FPC result
    bool Passed() const { return checked && fails.empty(); }
    // VFPC shows the forbidden-level failure in yellow when it is the only one
    bool WarningOnly() const { return fails.size() == 1 && fails[0] == Fail::Forbidden; }
};

// Every element of `want` in `filed`, one after the other (VFPC: the route string contains it)
inline bool ContainsRun(const std::vector<std::string>& filed, const std::vector<std::string>& want) {
    if (want.empty() || want.size() > filed.size()) return false;
    for (size_t i = 0; i + want.size() <= filed.size(); i++)
        if (std::equal(want.begin(), want.end(), filed.begin() + (std::ptrdiff_t)i)) return true;
    return false;
}

// The cruising level's direction. Up to FL410 every 2,000 ft alternates (FL350 odd, FL360 even); above FL410 the
// levels are 4,000 ft apart: FL410, 450, 490 odd, FL430, 470, 510 even. (VFPC's (RFL / 1000) % 2 calls FL430 odd.)
inline bool EvenLevel(int rflFt) {
    int fl = rflFt / 100;
    if (fl > 410) return ((fl - 410) / 20) % 2 == 1;
    return (rflFt / 1000) % 2 == 0;
}

// Does the condition's filters fit the flight? Fills viaDest and viaAirway for Explain.
inline bool Fits(const Condition& c, const std::string& suffix, const std::string& dest, const std::vector<std::string>& filed,
                 bool hasDct, std::string& viaDest, std::string& viaAirway) {
    viaDest.clear(); viaAirway.clear();
    if (!c.suffix.empty() && c.suffix != suffix) return false;
    if (!c.destinations.empty()) {
        for (const auto& d : c.destinations) if (dest.compare(0, d.size(), d) == 0) { viaDest = d; break; }
        if (viaDest.empty()) return false;
    }
    if (!c.airways.empty()) {
        for (const auto& a : c.airways) {
            bool hit = (a.size() == 1 && a[0] == "DCT") ? hasDct : ContainsRun(filed, a);
            if (hit) { viaAirway = a[0] + (a.size() > 1 ? " ... " + a.back() : ""); break; }
        }
        if (viaAirway.empty()) return false;
    }
    return true;
}

// The checks of the condition that decides (VFPC: the first one whose filters fit)
inline void Apply(const Condition& c, const Flight& f, Result& r) {
    int fl = f.rflFt / 100;
    if (!c.engines.empty()) {
        std::string list;
        bool ok = false;
        for (const auto& e : c.engines) { list += (list.empty() ? "" : ",") + e; if (e[0] == f.engine) ok = true; }
        if (!ok) { r.fails.push_back(Fail::Engine); r.notes.push_back("engine type " + std::string(1, f.engine ? f.engine : '?') + " is not " + list); }
    }
    bool even = EvenLevel(f.rflFt);
    if (c.direction == "EVEN" || c.direction == "ODD") {
        if (even != (c.direction == "EVEN")) {
            r.fails.push_back(Fail::Direction);
            r.notes.push_back(FlStr(f.rflFt) + " is " + (even ? "even" : "odd") + ", the SID needs " + Lower(c.direction));
        } else r.notes.push_back(Lower(c.direction));
    } else if (c.direction != "ANY") {
        r.fails.push_back(Fail::Direction);
        r.notes.push_back("the rule's direction " + c.direction + " is not EVEN, ODD or ANY");
    }
    if (c.minFl > 0 && fl < c.minFl) { r.fails.push_back(Fail::Min); r.notes.push_back(FlStr(f.rflFt) + " is below FL" + std::to_string(c.minFl)); }
    if (c.maxFl > 0 && fl > c.maxFl) { r.fails.push_back(Fail::Max); r.notes.push_back(FlStr(f.rflFt) + " is above FL" + std::to_string(c.maxFl)); }
    if (std::find(c.forbidden.begin(), c.forbidden.end(), fl) != c.forbidden.end()) {
        r.fails.push_back(Fail::Forbidden);
        r.notes.push_back(FlStr(f.rflFt) + " is not allowed");
    }
    if (!c.navigation.empty() && (!f.capability || c.navigation.find(f.capability) == std::string::npos)) {
        r.fails.push_back(Fail::Navigation);
        r.notes.push_back("equipment " + std::string(1, f.capability ? f.capability : '?') + " is not one of " + c.navigation);
    }
}

// VFPC validizeSid, with two differences so that valid flight plans pass:
// - When the SID's first waypoint has no rules at the airport, or no SID is set yet (EuroScope only sets one once a
//   runway is active), the rules are found by the route: first the rules of a point on the filed route, then any
//   rule whose airways the route follows. This is how ORBB's rules, keyed on the aerodrome VOR (BGD, BSR, ...), are
//   found for a flight on NOLDO1A. A SID that has rules of its own is still only checked against those.
// - The direction above FL410 (EvenLevel).
inline Result Check(const Rules& rules, const Flight& f) {
    Result r;
    if (f.vfr) { r.checked = r.vfr = true; return r; }
    auto apt = rules.find(Upper(f.origin));
    if (apt == rules.end()) return r;                   // no rules for the airport: nothing to show
    r.checked = true;
    const AirportRules& ar = apt->second;
    auto fail = [&r](Fail x, const std::string& why) { r.fails.push_back(x); r.notes.push_back(why); return r; };

    // SESRU1A -> SESRU and 1A; a SID with no number is no SID, except VCT (vectors)
    std::string sid = Upper(Trim(f.sid)), point, suffix;
    size_t digit = sid.find_first_of("0123456789");
    point = sid.substr(0, digit);
    if (digit != std::string::npos) suffix = sid.substr(digit);
    r.noSid = point.empty() || (suffix.empty() && point != "VCT");

    std::string dest = Upper(f.destination), viaDest, viaAirway;
    auto filed = NormaliseRoute(f.route);
    bool hasDct = false;
    for (const auto& t : SplitWs(f.route)) if (Upper(t) == "DCT") { hasDct = true; break; }
    auto decide = [&](const std::string& key, const Condition& c) {
        r.sidPoint = key;
        if (!viaDest.empty()) r.notes.push_back("destination " + viaDest);
        if (!viaAirway.empty()) r.notes.push_back("via " + viaAirway);
        Apply(c, f, r);
        return r;
    };

    if (!r.noSid) {
        r.suffix = suffix;
        auto own = ar.sids.find(point);
        if (own != ar.sids.end()) {
            for (const auto& c : own->second)
                if (Fits(c, suffix, dest, filed, hasDct, viaDest, viaAirway)) return decide(point, c);
            r.sidPoint = point;
            return fail(Fail::Sid, "no rule for SID " + point + " fits this destination and route");
        }
    }

    // Found by the route: the rules of points on the filed route, in route order, then the others in Core's order,
    // where only a condition the route follows by airway counts (so a rule with no filters doesn't catch everything)
    r.byRoute = true;
    std::vector<const std::string*> onRoute;
    for (const auto& t : filed) {
        auto it = ar.sids.find(t);
        if (it != ar.sids.end() && std::find(onRoute.begin(), onRoute.end(), &it->first) == onRoute.end()) onRoute.push_back(&it->first);
    }
    std::string sfx = r.noSid ? std::string() : suffix;
    for (const std::string* key : onRoute)
        for (const auto& c : ar.sids.at(*key))
            if (Fits(c, sfx, dest, filed, hasDct, viaDest, viaAirway)) return decide(*key, c);
    for (const auto& key : ar.order) {
        bool seen = false;
        for (const std::string* k : onRoute) if (*k == key) seen = true;
        if (seen) continue;
        for (const auto& c : ar.sids.at(key))
            if (!c.airways.empty() && Fits(c, sfx, dest, filed, hasDct, viaDest, viaAirway)) return decide(key, c);
    }
    r.byRoute = false;
    if (r.noSid) return fail(Fail::Sid, "no SID in the flight plan, and no rule at " + apt->first + " fits the route");
    r.sidPoint = point;
    return fail(Fail::Sid, "no rules for SID " + point + " at " + apt->first + ", and none fits the route");
}

// Explain FPC result (VFPC checkFPDetail)
inline std::string Explain(const Result& r, const Flight& f) {
    std::string head = Upper(f.origin) + " -> " + Upper(f.destination) + " " + (f.rflFt > 0 ? FlStr(f.rflFt) : "FL---");
    if (r.vfr) return head + ": VFR, not checked.";
    if (!r.checked) return head + ": no flight plan check rules for " + Upper(f.origin) + ".";
    std::string sid = Upper(Trim(f.sid));
    std::string s = head + (r.noSid ? "" : " SID " + sid) + (r.Passed() ? ": passed" : ": failed");
    std::vector<std::string> notes;
    if (r.byRoute) notes.push_back(std::string(r.noSid ? "no SID set, " : "") + "rules for " + r.sidPoint + " by the route");
    notes.insert(notes.end(), r.notes.begin(), r.notes.end());
    for (size_t i = 0; i < notes.size(); i++) s += (i ? ", " : " (") + notes[i];
    return s + (notes.empty() ? "." : ").");
}

} // namespace kwiq::fpc
