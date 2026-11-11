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
// <sid> is the SID's first waypoint (SESRU for SESRU1A); lists are comma separated; empty = no restriction.
// Filters (the condition is only for flights that fit): suffix, destinations (prefixes, OM = any OM..), airways
// (an airway or a run of route elements, e.g. "NAMDI M860 NINVA"). Checks: engine, direction, min_fl, max_fl,
// forbidden_fls, navigation.
struct Condition {
    std::string suffix;                                 // e.g. 1A
    std::string direction = "ANY";                      // EVEN, ODD or ANY
    int minFl = 0, maxFl = 0;                           // flight levels, 0 = none
    std::vector<int> forbidden;                         // flight levels
    std::vector<std::string> destinations, engines;     // engines: P, T, J, E
    std::vector<std::vector<std::string>> airways;      // each one normalised, as route elements
    std::string navigation;                             // allowed equipment codes, e.g. "LGZ"; empty = any
};
using SidRules = std::unordered_map<std::string, std::vector<Condition>>;   // SID first waypoint -> conditions
using Rules = std::unordered_map<std::string, SidRules>;                    // departure airport -> SIDs

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
            auto toks = NormaliseRoute(a);
            if (!toks.empty()) c.airways.push_back(toks);
        }
        c.engines = SplitList(f[10]);
        c.navigation = Upper(Trim(f[11]));
        r[apt][sid].push_back(std::move(c));
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
    std::string sidPoint, suffix;
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

inline Result Check(const Rules& rules, const Flight& f) {
    Result r;
    if (f.vfr) { r.checked = r.vfr = true; return r; }
    auto apt = rules.find(Upper(f.origin));
    if (apt == rules.end()) return r;                   // no rules for the airport: nothing to show
    r.checked = true;
    auto fail = [&r](Fail x, const std::string& why) { r.fails.push_back(x); r.notes.push_back(why); return r; };

    // SESRU1A -> SESRU and 1A; a SID with no number is no SID, except VCT (vectors)
    std::string sid = Upper(Trim(f.sid));
    if (sid.empty()) return fail(Fail::Sid, "no SID in the flight plan");
    size_t digit = sid.find_first_of("0123456789");
    r.sidPoint = sid.substr(0, digit);
    if (digit != std::string::npos) r.suffix = sid.substr(digit);
    if (r.suffix.empty() && r.sidPoint != "VCT") return fail(Fail::Sid, "no SID in the flight plan");
    auto conds = apt->second.find(r.sidPoint);
    if (conds == apt->second.end()) return fail(Fail::Sid, "no rules for SID " + r.sidPoint + " at " + apt->first);

    std::string dest = Upper(f.destination);
    auto filed = NormaliseRoute(f.route);
    int fl = f.rflFt / 100;
    for (const auto& c : conds->second) {
        // Filters: the first condition the flight fits decides
        if (!c.suffix.empty() && c.suffix != r.suffix) continue;
        std::string viaDest, viaAirway;
        if (!c.destinations.empty()) {
            for (const auto& d : c.destinations) if (dest.compare(0, d.size(), d) == 0) { viaDest = d; break; }
            if (viaDest.empty()) continue;
        }
        if (!c.airways.empty()) {
            for (const auto& a : c.airways) if (ContainsRun(filed, a)) { viaAirway = a[0] + (a.size() > 1 ? " ... " + a.back() : ""); break; }
            if (viaAirway.empty()) continue;
        }
        if (!viaDest.empty()) r.notes.push_back("destination " + viaDest);
        if (!viaAirway.empty()) r.notes.push_back("via " + viaAirway);

        // Checks
        if (!c.engines.empty()) {
            std::string list;
            bool ok = false;
            for (const auto& e : c.engines) { list += (list.empty() ? "" : ",") + e; if (e[0] == f.engine) ok = true; }
            if (!ok) { r.fails.push_back(Fail::Engine); r.notes.push_back("engine type " + std::string(1, f.engine ? f.engine : '?') + " is not " + list); }
        }
        bool even = (f.rflFt / 1000) % 2 == 0;
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
        return r;
    }
    return fail(Fail::Sid, "no rule for SID " + r.sidPoint + " fits this destination and route");
}

// Explain FPC result (VFPC checkFPDetail)
inline std::string Explain(const Result& r, const Flight& f) {
    std::string head = Upper(f.origin) + " -> " + Upper(f.destination) + " " + (f.rflFt > 0 ? FlStr(f.rflFt) : "FL---");
    if (r.vfr) return head + ": VFR, not checked.";
    if (!r.checked) return head + ": no flight plan check rules for " + Upper(f.origin) + ".";
    std::string s = head + (r.sidPoint.empty() ? "" : " SID " + r.sidPoint + r.suffix) + (r.Passed() ? ": passed" : ": failed");
    for (size_t i = 0; i < r.notes.size(); i++) s += (i ? ", " : " (") + r.notes[i];
    return s + (r.notes.empty() ? "." : ").");
}

} // namespace kwiq::fpc
