// squawk_logic.hpp - when to give an aircraft a squawk, adapted from the UK Controller Plugin
// (https://github.com/VATSIM-UK/uk-controller-plugin, src/plugin/squawk: SquawkAssignment, SquawkEventHandler,
// SquawkGenerator, SquawkValidator; GPL-3.0). Core allocates the codes (from the requester's FIR, the route and
// the flight rules); this only decides when to ask. No code is built in: which codes Core never assigns, and which
// of those still count as a code the aircraft has, come from Core (GET /squawk/non-assignable).
// Pure logic (no Windows / EuroScope dependencies), unit-tested in test/test_logic.cpp.
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

// ---- Core's non-assignable codes (UKCP's NonAssignableSquawkCode, with ranges, categories and a flag) -------
// GET /api/plugin/squawk/non-assignable, one line per row:
//   NONASSIGNABLE|<first>|<last>|<category>|<counts as assigned 0/1>|<description>
// A single code has first = last, and may be any 1 to 4 digits (0 and 9999 turn up in the VATSIM data feed); a
// range is two squawks, first <= last. Core never hands any of them out. "Counts as assigned" says whether an
// aircraft showing the code has a code: yes for emergencies (7500, 7600, 7700), so the plugin never replaces
// them; no for default and non-discrete codes (1200, 2000, 2200, 7000 ...), which are replaced automatically.
struct NonAssignable {
    std::string first, last, category, description;
    bool countsAsAssigned = false;
};
using CodeList = std::vector<NonAssignable>;

inline bool CodeText(const std::string& s) {
    if (s.empty() || s.size() > 4) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}
// Malformed rows are left out. An empty result means there is no list: squawk assignment stays off.
inline CodeList ParseNonAssignable(const std::string& body) {
    CodeList out;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() < 6 || Trim(f[0]) != "NONASSIGNABLE") continue;
        NonAssignable r;
        r.first = Trim(f[1]);
        r.last = Trim(f[2]);
        r.category = Lower(Trim(f[3]));
        std::string counts = Trim(f[4]);
        if (counts != "0" && counts != "1") continue;
        r.countsAsAssigned = counts == "1";
        for (size_t i = 5; i < f.size(); i++) r.description += (i > 5 ? "|" : "") + f[i];
        r.description = Trim(r.description);
        if (r.first == r.last) { if (!CodeText(r.first)) continue; }
        else if (!ValidSquawk(r.first) || !ValidSquawk(r.last) || r.first > r.last) continue;   // same length: text order is octal order
        out.push_back(std::move(r));
    }
    return out;
}
// The row a code falls in, or nullptr
inline const NonAssignable* FindCode(const CodeList& list, const std::string& s) {
    for (const auto& r : list) {
        if (r.first == r.last) { if (s == r.first) return &r; }
        else if (ValidSquawk(s) && s >= r.first && s <= r.last) return &r;
    }
    return nullptr;
}
// A code Core may hand out: a squawk that isn't on the list
inline bool AllowedSquawk(const CodeList& list, const std::string& s) {
    return ValidSquawk(s) && !FindCode(list, s);
}
// The aircraft has a code: a squawk that is either not on the list or on it as "counts as assigned"
inline bool HasAssignedSquawk(const CodeList& list, const std::string& s) {
    if (!ValidSquawk(s)) return false;
    const NonAssignable* r = FindCode(list, s);
    return !r || r->countsAsAssigned;
}

// The conspicuity codes (Core's squawk.conspicuity and squawk.conspicuity_vfr) and the holding code are never a
// code an aircraft has, nor one Core may give, even if Core's list leaves them out: added as "doesn't count"
inline void AddNoCodes(CodeList& list, const std::vector<std::string>& codes) {
    for (const auto& c : codes)
        if (ValidSquawk(c) && !FindCode(list, c)) list.push_back({ c, c, "conspicuity", "not on Core's list", false });
}

// ---- the rules (UKCP SquawkAssignment) ---------------------------------------------------------
struct User {
    bool controller = false;          // logged on as a controller (not an observer), live or sweatbox
    std::string callsign;             // e.g. OKBK_TWR
    bool sweatbox = false;            // on a sweatbox server: aircraft spawn anywhere with 2000
};
struct Aircraft {
    std::string callsign, origin, destination, squawk;          // squawk: the one controllers assigned
    bool vfr = false, tracked = false, trackedByMe = false;
    bool deliveryByMe = false;        // the user provides Delivery at the origin (ownership::AirfieldOwnership)
    double fromOrigin = 0;            // nm from the departure airport
    int groundSpeed = 0, altitude = 0;                          // kt, ft
};
// What the plugin remembers about a flight plan, so a pilot who reconnects gets the same code back
struct Stored {
    std::string origin, destination, squawk;
    bool byHand = false;              // a controller set a code that counts by hand (menu or EuroScope): given back
                                      // on a reconnect if Core holds none
    std::time_t expires = 0;          // 0 while connected; disconnected plans are forgotten after STORED_SECONDS
    bool vfr = false;
    bool planChanged = false;         // departure, destination or rules changed since the code was set: a code
                                      // Core holds for the callsign may be for the old plan, so ask for a new one
    bool mine = false;                // tracked by the user at the last look
    bool assumed = false;             // the user took it on with no code: a new one from Core, whatever came before
};

constexpr int UNTRACKED_MAX_SPEED = 30;               // kt: untracked aircraft only get a code on the ground
constexpr double UNTRACKED_MAX_FROM_ORIGIN = 7.0;     // nm
constexpr int LOGIN_DELAY_SECONDS = 15;               // nothing automatic straight after logging on
constexpr int STORED_SECONDS = 600;
// Shown while Core is asked (UKCP StartSquawkUpdate). It is never a code: an aircraft showing it still needs one,
// and seeing it set on another scope only means that scope's plugin is asking.
constexpr const char* HOLDING_SQUAWK = "7000";

// The aircraft has a code the plugin should not replace: Core's rule, and not the holding code
inline bool HasCode(const CodeList& codes, const std::string& s) {
    return s != HOLDING_SQUAWK && HasAssignedSquawk(codes, s);
}

inline bool Possible(const User& u, const Aircraft& a) {
    // All zeros: EuroScope sends flight plan events for aircraft it has no position for yet
    return u.controller && a.fromOrigin != 0.0 && a.altitude != 0;
}
// UKCP GeneralAssignmentNeeded: no code yet, and either tracked by the user or waiting on the ground at an
// airport the user gives departures their squawk. On sweatbox, where aircraft spawn anywhere (often in the air)
// with 2000, also any aircraft nobody tracks.
inline bool AssignmentNeeded(const User& u, const Aircraft& a, const CodeList& codes) {
    if (!Possible(u, a) || HasCode(codes, a.squawk)) return false;
    if (a.trackedByMe) return true;
    if (u.sweatbox && !a.tracked) return true;
    return a.groundSpeed <= UNTRACKED_MAX_SPEED && a.fromOrigin <= UNTRACKED_MAX_FROM_ORIGIN && a.deliveryByMe;
}
// The plan differs from the one stored (a plan never stored with a departure or destination is new, not changed)
inline bool PlanDiffers(const Stored& s, const Aircraft& a) {
    if (s.origin.empty() && s.destination.empty()) return false;
    return s.origin != a.origin || s.destination != a.destination || s.vfr != a.vfr;
}
// The departure, destination or flight rules changed since the code was set: a code Core holds for the callsign
// may be for the old plan. Stays until a code is set, so a change seen while nothing could be done isn't lost.
inline bool ForceNeeded(const Stored* s, const Aircraft& a) {
    return s && (s->planChanged || PlanDiffers(*s, a));
}
// Reconnected with the same plan: ask Core for the code it holds, so a code it has since given to someone else is
// never handed back; if it holds none, a code set by hand comes back, else a new one (UKCP sets the old code back
// without asking)
inline bool PreviousNeeded(const User& u, const Aircraft& a, const Stored* s, const CodeList& codes) {
    return AssignmentNeeded(u, a, codes) && s && HasAssignedSquawk(codes, s->squawk) && !ForceNeeded(s, a);
}
// Forcing a new code, or setting one by hand: a controller, on an aircraft nobody else tracks
inline bool ChangeAllowed(const User& u, const Aircraft& a) {
    return u.controller && (a.trackedByMe || !a.tracked);
}

enum class Action { None, Previous, Assign, Assumed };
// UKCP SquawkEventHandler::AttemptAssignment, without the UK's circuit and local codes
// A code that doesn't count (blank, or on Core's list without "counts as assigned") is always replaced, whoever set
// it. Assumed with one: always a new code from Core, not one Core holds for the callsign or a reconnect's old code
inline Action Decide(const User& u, const Aircraft& a, const Stored* s, const CodeList& codes) {
    if (s && s->assumed && a.trackedByMe && AssignmentNeeded(u, a, codes)) return Action::Assumed;
    if (PreviousNeeded(u, a, s, codes)) return Action::Previous;
    if (AssignmentNeeded(u, a, codes)) return Action::Assign;   // a code that doesn't count is always replaced
    return Action::None;
}

// ---- what the plugin remembers (UKCP StoredFlightplan) -------------------------------------------
// Every look at a flight plan. A new plan makes the plugin automatic again for it (UKCP: until the plan changes).
// codes: Core's list, to see that an aircraft assumed with a code (or given one later) needs no new one
inline void Remember(Stored& st, const Aircraft& a, const CodeList* codes = nullptr) {
    if (PlanDiffers(st, a)) { st.byHand = false; st.planChanged = true; }
    if (a.trackedByMe && !st.mine) st.assumed = true;           // kept until a code is set, or it isn't the user's
    if (!a.trackedByMe || (codes && HasCode(*codes, a.squawk))) st.assumed = false;
    st.mine = a.trackedByMe;
    st.origin = a.origin;
    st.destination = a.destination;
    st.vfr = a.vfr;
    st.expires = 0;
    if (st.squawk.empty() && ValidSquawk(a.squawk)) st.squawk = a.squawk;   // a code it had before the plugin saw it
}
// The plugin sets a code (Core's, a previous one, or one the controller chose in the menu)
inline void CodeSet(Stored& st, const std::string& code, bool byHand) {
    st.squawk = code;
    st.byHand = byHand;
    st.planChanged = false;
    st.assumed = false;
}
// EuroScope reports a code. One the plugin didn't set was set by a controller, here (typed in EuroScope) or on
// another scope: by hand, like a code set from the menu. Ignored: a cleared or invalid code, the holding
// code, anything while the pilot is disconnected (so the code to give back survives a reconnect), and a code that
// is no code (on Core's list, not counting as assigned): it is always replaced, whoever set it.
inline void CodeSeen(Stored& st, const std::string& code, const CodeList& codes, bool sweatbox) {
    (void)sweatbox;
    if (st.expires || code == st.squawk || !ValidSquawk(code) || code == HOLDING_SQUAWK) return;
    if (!HasAssignedSquawk(codes, code)) return;
    st.squawk = code;
    st.byHand = true;
    st.planChanged = false;
    st.assumed = false;                                         // chosen after the assume: kept
}
// Core's answer arrived: set it only if it is still wanted. One the controller asked for: if they may still change
// the code. An automatic one: also only if the aircraft still has no code that counts (UKCP sets it regardless and
// can overwrite a code someone set while the request was out).
inline bool AnswerWanted(bool manual, const User& u, const Aircraft& a, const Stored* s, const CodeList& codes) {
    if (!ChangeAllowed(u, a)) return false;
    if (manual) return true;
    (void)s;
    return !HasCode(codes, a.squawk);                           // a code that doesn't count, by hand or not, is replaced
}

// ---- Core ------------------------------------------------------------------------------------------
// /api/plugin/squawk-assignment/<callsign> answers SQUAWK|<code>: the code as sent, or empty
inline std::string AnsweredSquawk(const std::string& body) {
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() >= 2 && f[0] == "SQUAWK") return Trim(f[1]);
    }
    return "";
}
// Core's answer, or empty if it is no code to set: on Core's own non-assignable list, or the holding code
inline std::string ParseSquawk(const std::string& body, const CodeList& codes) {
    std::string code = AnsweredSquawk(body);
    return AllowedSquawk(codes, code) && code != HOLDING_SQUAWK ? code : "";
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

// PUT /squawk-assignment/<callsign>: the flight, and who is asking (the controller's own callsign), as Core
// picks the range from the requester's FIR
inline std::string AssignForm(const Aircraft& a, const std::string& requester) {
    return FormEncode({ { "origin", a.origin }, { "destination", a.destination }, { "rules", a.vfr ? "V" : "I" },
                        { "requester", requester } });
}

} // namespace kwiq::squawk
