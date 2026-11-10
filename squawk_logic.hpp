// squawk_logic.hpp - when to give an aircraft a squawk, adapted from the UK Controller Plugin
// (https://github.com/VATSIM-UK/uk-controller-plugin, src/plugin/squawk: SquawkAssignment, SquawkEventHandler,
// SquawkGenerator, SquawkValidator; GPL-3.0). Core allocates the codes (domestic, VFR or international, from the
// route and flight rules); this only decides when to ask.
// Pure logic (no Windows / EuroScope dependencies), unit-tested in tests/test_logic.cpp.
// License: GPL-3.0-or-later
#pragma once

#include "kwiq_logic.hpp"
#include <utility>

namespace kwiq::squawk {

// ---- codes (UKCP SquawkValidator, EuroScopeCFlightPlanWrapper::HasAssignedSquawk) ---------------
inline bool ValidSquawk(const std::string& s) {
    if (s.size() != 4) return false;
    for (char c : s) if (c < '0' || c > '7') return false;
    return true;
}
// Never assigned from Core: emergency, radio failure, hijack and the "no code" ones
inline bool AllowedSquawk(const std::string& s) {
    return ValidSquawk(s) && s != "7500" && s != "7600" && s != "7700" && s != "1200" && s != "2200" &&
           s != "0200" && s != "0000" && s != "1234";
}
// A controller has given the aircraft a code (EuroScope's default and "none" codes, 2000 among them, do not count)
inline bool HasAssignedSquawk(const std::string& s) {
    return ValidSquawk(s) && s != "0200" && s != "2200" && s != "1200" && s != "2000" && s != "0000" && s != "1234";
}

// ---- callsigns ---------------------------------------------------------------------------------
// OKBK_N_TWR -> OKBK and TWR
inline std::string CallsignPrefix(const std::string& cs) { return Upper(cs.substr(0, cs.find('_'))); }
inline std::string CallsignSuffix(const std::string& cs) {
    size_t i = cs.rfind('_');
    return i == std::string::npos ? std::string() : Upper(cs.substr(i + 1));
}
// A top-down position (OKBK_TWR) is worked by OKBK_TWR, OKBK_1_TWR, OKBK_N_TWR: same prefix and suffix
inline bool PositionMatches(const std::string& position, const std::string& callsign) {
    if (callsign.find('_') == std::string::npos) return false;
    return CallsignPrefix(position) == CallsignPrefix(callsign) && CallsignSuffix(position) == CallsignSuffix(callsign);
}

// ---- top-down: who gives departures their squawk (UKCP AirfieldServiceProviderCollection) ---------------
// Core /api/plugin/topdown (plain text, shared with the QNH monitor and notifications):  TOPDOWN|<airport>|<position>,<position>,...  lowest first,
// e.g. TOPDOWN|OKBK|OKBK_DEL,OKBK_GND,OKBK_TWR,OKBK_APP,OKAC_CTR
using TopDown = std::unordered_map<std::string, std::vector<std::string>>;

inline TopDown ParseTopDown(const std::string& body) {
    TopDown td;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() < 3 || f[0] != "TOPDOWN") continue;
        std::string apt = Upper(Trim(f[1]));
        if (!IsAirportCode(apt)) continue;
        std::vector<std::string> order;
        for (const auto& p : SplitChar(f[2], ',')) {
            std::string pos = Upper(Trim(p));
            if (pos.find('_') != std::string::npos) order.push_back(pos);
        }
        if (!order.empty()) td[apt] = order;
    }
    return td;
}
// An airport Core has no order for is covered by its own positions only
inline std::vector<std::string> TopDownOrder(const TopDown& td, const std::string& airport) {
    auto it = td.find(airport);
    if (it != td.end()) return it->second;
    return { airport + "_DEL", airport + "_GND", airport + "_TWR", airport + "_APP" };
}
// The user gives departures from the airport their squawk: the lowest position in its top-down order that
// anyone is on is one the user is on. online = the other controllers' callsigns.
inline bool DeliveryByMe(const TopDown& td, const std::string& airport, const std::string& me,
                         const std::vector<std::string>& online) {
    if (airport.empty() || me.empty()) return false;
    for (const auto& pos : TopDownOrder(td, airport)) {
        if (PositionMatches(pos, me)) return true;
        for (const auto& cs : online)
            if (PositionMatches(pos, cs)) return false;
    }
    return false;
}

// ---- the rules (UKCP SquawkAssignment) ---------------------------------------------------------
struct User {
    bool controller = false;          // logged on as a controller (not an observer), live or sweatbox
    std::string callsign;             // e.g. OKBK_TWR
};
struct Aircraft {
    std::string callsign, origin, destination, squawk;          // squawk: the one controllers assigned
    bool vfr = false, tracked = false, trackedByMe = false;
    bool deliveryByMe = false;        // DeliveryByMe(origin)
    double fromOrigin = 0;            // nm from the departure airport
    int groundSpeed = 0, altitude = 0;                          // kt, ft
};
// What the plugin remembers about a flight plan, so a pilot who reconnects gets the same code back
struct Stored {
    std::string origin, destination, squawk;
    bool byHand = false;              // a controller set the code by hand: leave it alone
    std::time_t expires = 0;          // 0 while connected; disconnected plans are forgotten after STORED_SECONDS
};

constexpr int UNTRACKED_MAX_SPEED = 30;               // kt: untracked aircraft only get a code on the ground
constexpr double UNTRACKED_MAX_FROM_ORIGIN = 7.0;     // nm
constexpr int LOGIN_DELAY_SECONDS = 15;               // nothing automatic straight after logging on
constexpr int STORED_SECONDS = 600;

inline bool Possible(const User& u, const Aircraft& a) {
    // All zeros: EuroScope sends flight plan events for aircraft it has no position for yet
    return u.controller && a.fromOrigin != 0.0 && a.altitude != 0;
}
// UKCP GeneralAssignmentNeeded: no code yet, and either tracked by the user or waiting on the ground at an
// airport the user gives departures their squawk
inline bool AssignmentNeeded(const User& u, const Aircraft& a) {
    if (!Possible(u, a) || HasAssignedSquawk(a.squawk)) return false;
    if (a.trackedByMe) return true;
    return a.groundSpeed <= UNTRACKED_MAX_SPEED && a.fromOrigin <= UNTRACKED_MAX_FROM_ORIGIN && a.deliveryByMe;
}
// The departure or destination changed since the plan was stored: a code Core holds for it may be for the old one
inline bool ForceNeeded(const Stored* s, const Aircraft& a) {
    return s && (s->origin != a.origin || s->destination != a.destination);
}
// Reconnected with the same plan: give it back the code it had (UKCP also needs no change of plan here)
inline bool PreviousNeeded(const User& u, const Aircraft& a, const Stored* s) {
    return AssignmentNeeded(u, a) && s && HasAssignedSquawk(s->squawk) && !ForceNeeded(s, a);
}
// Forcing a new code, or setting one by hand: a controller, on an aircraft nobody else tracks
inline bool ChangeAllowed(const User& u, const Aircraft& a) {
    return u.controller && (a.trackedByMe || !a.tracked);
}

enum class Action { None, Previous, Assign };
// UKCP SquawkEventHandler::AttemptAssignment, without the UK's circuit and local codes
inline Action Decide(const User& u, const Aircraft& a, const Stored* s) {
    if (PreviousNeeded(u, a, s)) return Action::Previous;
    if (s && s->byHand && !ForceNeeded(s, a)) return Action::None;
    if (AssignmentNeeded(u, a)) return Action::Assign;
    return Action::None;
}

// ---- Core ------------------------------------------------------------------------------------------
// /api/plugin/squawk-assignment/<callsign> answers SQUAWK|<code>; anything else, or a code Core must
// never hand out, is no code
inline std::string ParseSquawk(const std::string& body) {
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() >= 2 && f[0] == "SQUAWK" && AllowedSquawk(Trim(f[1]))) return Trim(f[1]);
    }
    return "";
}
// ERROR|<message> in an error response, else empty
inline std::string ParseError(const std::string& body) {
    for (const auto& line : SplitChar(body, '\n')) {
        std::string t = Trim(line);
        if (t.compare(0, 6, "ERROR|") == 0) return Trim(t.substr(6));
    }
    return "";
}
// Callsigns go into the URL as they are: only letters, digits, - and _
inline bool UrlSafeCallsign(const std::string& cs) {
    if (cs.empty() || cs.size() > 12) return false;
    for (char c : cs) if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') return false;
    return true;
}
inline std::string FormEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const auto& [k, v] : fields) {
        if (!out.empty()) out += '&';
        out += k + '=';
        for (unsigned char c : v) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.') out += (char)c;
            else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
        }
    }
    return out;
}

} // namespace kwiq::squawk
