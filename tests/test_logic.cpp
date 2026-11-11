// Logic tests: g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic
#include "kwiq_logic.hpp"
#include "fpc_logic.hpp"
#include "squawk_logic.hpp"
using namespace kwiq;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    // ---- edge cases (#52): every pure function, with empty input, CRLF, short and malformed lines ----
    CHECK(Upper("okbk_twr") == "OKBK_TWR" && Lower("OKBK") == "okbk" && Upper("").empty());
    CHECK(Trim(" \t a b \r\n") == "a b" && Trim("").empty() && Trim(" \r\n ").empty());
    CHECK(SplitWs("  a \t b\r\nc ") == std::vector<std::string>({ "a", "b", "c" }) && SplitWs(" \r\n").empty());
    CHECK(SplitChar("", '|').size() == 1 && SplitChar("a||b|", '|').size() == 4 && SplitChar("a||b|", '|')[1].empty());
    CHECK(SanitiseCode("ab-c_1 /?&") == "ab-c_1" && SanitiseCode("").empty());
    CHECK(AllDigits("ab123", 2, 3) && !AllDigits("ab123", 2, 4) && !AllDigits("12a", 0, 3) && AllDigits("", 0, 0));
    CHECK(ToInt("42") == 42 && ToInt(" -7x") == -7 && ToInt("") == 0 && ToInt("abc", 5) == 5);
    CHECK(ToInt("99999999999999999999") == INT_MAX && ToInt("-99999999999") == INT_MIN);     // clamped, not undefined
    // only web pages are ever opened (#44)
    CHECK(IsWebLink("https://docs.kuwait-iraqvacc.net/x?y=1") && IsWebLink("HTTP://example.net") && IsWebLink("http://127.0.0.1:8080/"));
    CHECK(!IsWebLink("") && !IsWebLink("https://") && !IsWebLink("https:///etc") && !IsWebLink("file:///C:/Windows/notepad.exe"));
    CHECK(!IsWebLink("C:\\Windows\\System32\\cmd.exe") && !IsWebLink("https://a b") && !IsWebLink("https://x\"y") && !IsWebLink("ms-settings:"));
    CHECK(!IsWebLink("https://x\r\n") && !IsWebLink("javascript:alert(1)"));
    CHECK(!IsSpeedLevel("") && !IsSpeedLevel("N0450") && !IsSpeedLevel("N0450F35") && !IsSpeedLevel("N0450X350") && IsSpeedLevel("N0450M0840"));
    CHECK(NormaliseRoute("").empty() && NormaliseRoute(" DCT \r\n N0450F350 ").empty() &&
          NormaliseRoute("abc/n0450f350\r\n") == std::vector<std::string>({ "ABC" }));
    CHECK(MatchTokens({}, {}) && MatchTokens({}, { "A" }) && !MatchTokens({ "A" }, {}) && MatchTokens({ "A", "B" }, { "A", "X", "B" }));
    CHECK(!IsDate("") && !IsDate("2026-11-0x") && !IsDate("2026/11/07"));
    CHECK(!ParseCoreEvent("").active && ParseCoreEvent("").routes.empty() && RoutesForDate(EventData{}, "2026-11-07").empty());
    {
        EventData e = ParseCoreEvent("ROUTE|2026-11-07|OKBK|ORBI|DCT\r\nEVENT\r\nEVENT|Short\r\nROUTE|2026-11-07|OKBK\r\n");
        CHECK(e.active && e.name == "Short" && e.vatcanCode.empty() && e.routes.size() == 1 && e.routes[0].ades == "ORBI");
        CHECK(ParseCoreEvent("ROUTE|2026-11-07|OKBK|ORBI|DCT\n").routes.empty());              // routes without an event
    }
    {
        SrdData s = ParseSrd("VERSION|v2\r\nSRD|okbk|orbi|99999999999|x|DCT ABC|\r\nSRD|OKBK|ORBI|1|2\r\nSRD| |ORBI|||ABC|\r\n");
        CHECK(s.version == "v2" && s.rows.size() == 1 && s.rows[0].minLvl == INT_MAX && s.rows[0].maxLvl == 0 && s.rows[0].notes.empty());
        CHECK(ParseSrd("").rows.empty() && ParseSrd("").version.empty() && ParseSrd("").pair.empty());
        SrdData d;
        d.rows = { SrdRoute{ "OKBK", "ORBI", "A", "", 0, 0, { "A" } }, SrdRoute{ "ABC", "ORBI", "B", "", 0, 0, { "B" } } };
        BuildSrd(d);
        CHECK(d.pair.size() == 1 && d.wp.size() == 1 && d.wp[0] == 1);
        std::vector<size_t> out;
        Candidates(d, "OKBK", "ORBI", { "A" }, out);
        CHECK(out.size() == 1 && out[0] == 0);                            // the airport pair wins
        Candidates(d, "OTHH", "ORBI", { "ABC", "B" }, out);
        CHECK(out.size() == 1 && out[0] == 1);                            // enroute: ABC filed, ORBI the destination
        Candidates(d, "OTHH", "ORBI", { "B" }, out);
        CHECK(out.empty());
        BuildSrd(d);
        CHECK(d.pair.size() == 1 && d.wp.size() == 1);                    // built again from scratch
    }
    CHECK(ParseLevelFt("1234567") == 0 && ParseLevelFt(" fl350\r\n") == 35000 && ParseLevelFt("999") == 99900);
    CHECK(ParseSectors("").empty() && ParseSectors("SECTOR|BS|SR\r\n").at("BS") == "SR" && ParseSectors("SECTOR|BS\r\nSECTOR||SR\r\n").empty());
    {
        PluginConfig c = ParseConfig("|nokey\r\na|1|2\r\nb| \r\nnobar\r\nn|99999999999\r\n");
        CHECK(c.kv.size() == 3 && c.Get("a") == "1|2" && c.Get("b", "x").empty() && c.GetBool("b", true) && c.GetBool("a", false));
        PluginConfig no;
        no.kv["f"] = "No";
        CHECK(c.GetInt("n", 5, 0, 100) == 100 && ParseConfig("").kv.empty() && !no.GetBool("f", true));
    }
    CHECK(ParseColour("#ff8000", 1) == 0x000080FFul && ParseColour("", 7) == 7 && ParseColour("#FF80001", 7) == 7 && ParseColour("FF8000", 7) == 7);
    CHECK(CompareVersions("", "") == 0 && CompareVersions("0.6.0.42", "0.6.0") > 0 && CompareVersions("1.x", "1.0") == 0 &&
          CompareVersions("99999999999.0", "1.0") > 0);
    CHECK(!FindAirac("").valid() && !FindAirac("2610/").valid() && FindAirac("x2610/7y").rev == 7 && !FindAirac("261/02").valid());
    CHECK(QueryValue("", "key").empty() && QueryValue("GET /?key HTTP/1.1\r\n", "key").empty() && QueryValue("GET /?key= HTTP/1.1", "key").empty());
    CHECK(QueryValue("GET /?a=1&key=kwiq_x", "key") == "kwiq_x");

    // route matching
    CHECK(RouteMatches("DCT ABC UL123 XYZ DCT", "N0450F350 ABC/N0450F350 UL123 XYZ"));
    CHECK(RouteMatches("ABC UL123 XYZ", "kuxx1a abc dct ul123 def xyz star1b"));
    CHECK(!RouteMatches("ABC UL123 XYZ", "ABC DCT XYZ"));
    CHECK(!RouteMatches("ABC UL123 XYZ", "XYZ UL123 ABC"));
    CHECK(!RouteMatches("ABC UL123 XYZ", ""));
    CHECK(RouteMatches("DCT", "ANYTHING HERE"));
    CHECK(RouteMatches("ABC DCT XYZ", "ABC XYZ"));
    CHECK(IsSpeedLevel("N0450F350") && IsSpeedLevel("K0830S1100") && IsSpeedLevel("M082F350"));
    CHECK(!IsSpeedLevel("KUXX1A") && !IsSpeedLevel("NOMAN") && !IsSpeedLevel("MIDAS"));

    // event routes, date-aware
    const std::string D = "2026-11-07", D2 = "2026-11-08";
    EventData ev = ParseCoreEvent("EVENT|Gulf Fly-in|ABC-123\r\nROUTE|2026-11-07|okbk|orbi|DCT ABC UL123 XYZ\nROUTE|2026-11-07|OKBK|ORBI|DCT QQQ\nROUTE|2026-11-08|OTHH|OKBK|DCT\nROUTE|bad-date|OKBK|OMDB|X\n");
    CHECK(ev.active && ev.name == "Gulf Fly-in" && ev.vatcanCode == "ABC-123" && ev.routes.size() == 3);
    CHECK(CheckEventRoute(ev, "OKBK", "ORBI", "ABC UL123 XYZ", D) == RouteResult::Ok);
    CHECK(CheckEventRoute(ev, "OKBK", "ORBI", "QQQ", D) == RouteResult::Ok);
    CHECK(CheckEventRoute(ev, "OKBK", "ORBI", "ABC XYZ", D) == RouteResult::Check);
    CHECK(CheckEventRoute(ev, "OKBK", "OMDB", "ABC XYZ", D) == RouteResult::None);
    CHECK(CheckEventRoute(ev, "OKBK", "ORBI", "ABC UL123 XYZ", D2) == RouteResult::None);
    CHECK(CheckEventRoute(ev, "OTHH", "OKBK", "DCT", D) == RouteResult::None);
    CHECK(CheckEventRoute(ev, "OTHH", "OKBK", "DCT", D2) == RouteResult::Ok);
    CHECK(RoutesForDate(ev, D).size() == 2 && RoutesForDate(ev, D2).size() == 1);
    CHECK(EventRouteAirports(RoutesForDate(ev, D)[0]) == RoutesForDate(ev, D)[0].adep + " -> " + RoutesForDate(ev, D)[0].ades);
    {
        EventRoute er;
        er.adep = "OKBK"; er.ades = "ORBI";
        CHECK(EventRouteAirports(er) == "OKBK -> ORBI");
    }
    CHECK(!ParseCoreEvent("NONE\n").active);
    CHECK(ParseCoreEvent("EVENT|X|a b/../c").vatcanCode == "abc");
    CHECK(IsDate("2026-11-07") && !IsDate("2026-1-07") && IsDate(TodayUtc()));
    CHECK(DateOf(0) == "1970-01-01");

    // SRD parse + SRD check
    SrdData srd = ParseSrd(
        "VERSION|abc123\n"
        "SRD|OKBK|ORBI|10000|30000|DCT ABC UL123 XYZ|Jets only\n"
        "SRD|OKBK|ORBI||45000|DCT QQQ|\n"
        "SRD|ABC|XYZ|20000||UL123|enroute segment\n"
        "SRD|OKBK||1|2|BAD|\n"
        "garbage\n");
    CHECK(srd.version == "abc123" && srd.rows.size() == 3 && srd.pair.size() == 1 && srd.wp.size() == 1);
    CHECK(srd.rows[0].minLvl == 10000 && srd.rows[0].maxLvl == 30000 && srd.rows[0].notes == "Jets only");
    CHECK(srd.rows[1].minLvl == 0 && srd.rows[1].maxLvl == 45000);
    auto F = [&](const char* r) { return NormaliseRoute(r); };
    CHECK(CheckSrd(srd, "OKBK", "ORBI", F("ABC UL123 XYZ"), 25000) == SrdResult::Ok);
    CHECK(CheckSrd(srd, "OKBK", "ORBI", F("ABC UL123 XYZ"), 35000) == SrdResult::Max);   // row0 max 30000
    CHECK(CheckSrd(srd, "OKBK", "ORBI", F("ABC UL123 XYZ"), 5000)  == SrdResult::Min);
    CHECK(CheckSrd(srd, "OKBK", "ORBI", F("QQQ"), 40000) == SrdResult::Ok);              // other row allows it
    CHECK(CheckSrd(srd, "OKBK", "ORBI", F("ABC XYZ"), 25000) == SrdResult::Route);
    CHECK(CheckSrd(srd, "OKBK", "ORBI", F("ABC UL123 XYZ"), 0) == SrdResult::Ok);        // unknown level: skip
    CHECK(CheckSrd(srd, "OKBK", "OMDB", F("DEF GHI"), 25000) == SrdResult::None);
    CHECK(CheckSrd(srd, "OKBK", "OMDB", F("ABC XYZ"), 25000) == SrdResult::Route);       // enroute row wants UL123
    // waypoint (enroute) row applies when both ends are on the filed route
    CHECK(CheckSrd(srd, "OTHH", "OIII", F("ABC UL123 XYZ"), 25000) == SrdResult::Ok);
    CHECK(CheckSrd(srd, "OTHH", "OIII", F("ABC UL123 XYZ"), 15000) == SrdResult::Min);
    CHECK(CheckSrd(srd, "OTHH", "OIII", F("DEF UL999"), 25000) == SrdResult::None);
    CHECK(ExplainSrd(srd, "OKBK", "ORBI", "ABC XYZ", 25000).find("does not match") != std::string::npos);
    CHECK(ExplainSrd(srd, "OKBK", "ORBI", "ABC UL123 XYZ", 35000).find("FL100") != std::string::npos);
    CHECK(ExplainSrd(srd, "OKBK", "ORBI", "ABC UL123 XYZ", 25000).find("conform") != std::string::npos);
    CHECK(ParseLevelFt("350") == 35000 && ParseLevelFt("FL350") == 35000 && ParseLevelFt("fl 350") == 35000 &&
          ParseLevelFt("35000") == 35000 && ParseLevelFt("") == 0 && ParseLevelFt("abc") == 0);
    CHECK(FlStr(35000) == "FL350" && FlStr(5000) == "FL050");

    // FIR exit points (UK Controller Plugin): the first listed point the route leaves the airspace at
    FirExits exits = ParseFirExits("EXIT|tasmi|090|180|0\r\nEXIT|ROTAP|300|060|0\nEXIT|SIDAD|315|045|1\n"
                                   "EXIT|BAD|400|10|0\nEXIT|ALSO|x|10|0\nnonsense\n");
    CHECK(exits.size() == 3 && exits["TASMI"].size() == 1 && exits["TASMI"][0].from == 90 && exits["SIDAD"][0].internal &&
          !exits["ROTAP"][0].internal);
    CHECK(InExitCone(exits["TASMI"][0], 135) && !InExitCone(exits["TASMI"][0], 200));
    CHECK(InExitCone(exits["ROTAP"][0], 350) && InExitCone(exits["ROTAP"][0], 30) && !InExitCone(exits["ROTAP"][0], 120));   // wraps north
    std::vector<RoutePoint> north = { { "OKBK", 0 }, { "SIDAD", 10 }, { "ABC", 5 }, { "ROTAP", 355 }, { "LTBA", -1 } };
    AircraftExit nx = FindFirExit(exits, north);
    CHECK(nx.internal == "SIDAD" && nx.external == "ROTAP");
    std::vector<RoutePoint> inbound = { { "ROTAP", 170 }, { "OKBK", -1 } };   // through ROTAP the wrong way
    CHECK(FindFirExit(exits, inbound).external.empty());
    std::vector<RoutePoint> last = { { "OKBK", 90 }, { "TASMI", -1 } };       // no next point: no track, no exit
    CHECK(FindFirExit(exits, last).external.empty());

    // several cones per point: one per direction, each with its own internal flag
    FirExits two = ParseFirExits("EXIT|BOUND|000|090|0\nEXIT|BOUND|180|270|1\n"     // NE out of the vACC, SW into ORBB
                                 "EXIT|NORTH|330|030|0\nEXIT|NORTH|150|210|0\n"     // both ways, one cone wrapping north
                                 "EXIT|DUP|090|180|0\nEXIT|DUP|090|180|0\nEXIT|dup|090|180|0\n"   // the same cone three times
                                 "EXIT|DUP|090|180|1\n");                         // same headings, other flag: another cone
    CHECK(two.size() == 3 && two["BOUND"].size() == 2 && two["NORTH"].size() == 2 && two["DUP"].size() == 2);
    // two cones on one point: both directions match, a third doesn't
    std::vector<RoutePoint> ne = { { "BOUND", 45 }, { "X", -1 } }, sw = { { "BOUND", 225 }, { "X", -1 } }, se = { { "BOUND", 135 }, { "X", -1 } };
    CHECK(FindFirExit(two, ne).external == "BOUND" && FindFirExit(two, ne).internal.empty());   // the external cone
    CHECK(FindFirExit(two, sw).internal == "BOUND" && FindFirExit(two, sw).external.empty());   // the internal cone
    CHECK(FindFirExit(two, se).internal.empty() && FindFirExit(two, se).external.empty());
    // a cone that wraps through north, next to an ordinary one on the same point
    std::vector<RoutePoint> n350 = { { "NORTH", 350 }, { "X", -1 } }, n10 = { { "NORTH", 10 }, { "X", -1 } },
                            s180 = { { "NORTH", 180 }, { "X", -1 } }, e90 = { { "NORTH", 90 }, { "X", -1 } };
    CHECK(FindFirExit(two, n350).external == "NORTH" && FindFirExit(two, n10).external == "NORTH" &&
          FindFirExit(two, s180).external == "NORTH" && FindFirExit(two, e90).external.empty());
    // the same point internal in one cone and external in another: a later external exit still counts
    std::vector<RoutePoint> via = { { "BOUND", 200 }, { "NORTH", 0 }, { "X", -1 } };
    AircraftExit vx = FindFirExit(two, via);
    CHECK(vx.internal == "BOUND" && vx.external == "NORTH");
    // overlapping cones on one point (DUP, external and internal at the same headings): external wins
    std::vector<RoutePoint> dup = { { "DUP", 135 }, { "X", -1 } };
    CHECK(FindFirExit(two, dup).external == "DUP" && FindFirExit(two, dup).internal.empty());
    CHECK(ExitConeFor(two["BOUND"], 135) == nullptr && ExitConeFor(two["BOUND"], 0) != nullptr && !ExitConeFor(two["BOUND"], 0)->internal);
    CHECK(Domestic("OKBK", "OK,OR") && Domestic("orbi", "OK, OR") && !Domestic("LTBA", "OK,OR"));
    CHECK(SrdEnd("ORBI", "OK,OR", nx) == "ORBI" && SrdEnd("LTBA", "OK,OR", nx) == "ROTAP");
    CHECK(SrdEnd("LTBA", "OK,OR", AircraftExit{ "SIDAD", "" }) == "SIDAD" && SrdEnd("LTBA", "OK,OR", {}) == "");

    // SRD check for a flight leaving the airspace: checked against ADEP to its FIR exit
    SrdData srdx = ParseSrd("SRD|OKBK|ROTAP|20000|41000|DCT ABC UL602 ROTAP|\n"
                            "SRD|OKBK|SIDAD||25000|DCT SIDAD|\n"
                            "SRD|OKBK|ORBI|||DCT QQQ|\n");
    CHECK(srdx.toPoint.size() == 2 && srdx.pair.size() == 1);
    auto toLtba = F("DCT SIDAD ABC UL602 ROTAP DCT LTBA");
    CHECK(CheckSrd(srdx, "OKBK", "LTBA", toLtba, 35000, nx) == SrdResult::Ok);
    CHECK(CheckSrd(srdx, "OKBK", "LTBA", toLtba, 45000, nx) == SrdResult::Max);
    CHECK(CheckSrd(srdx, "OKBK", "LTBA", F("DCT SIDAD XYZ ROTAP"), 35000, nx) == SrdResult::Route);
    CHECK(CheckSrd(srdx, "OKBK", "LTBA", F("DCT SIDAD"), 20000, AircraftExit{ "SIDAD", "" }) == SrdResult::Ok);   // only the internal exit known
    CHECK(CheckSrd(srdx, "OKBK", "ORBI", F("QQQ"), 30000, nx) == SrdResult::Ok);                  // a pair row still wins
    CHECK(ExplainSrd(srdx, "OKBK", "LTBA", "DCT SIDAD ABC UL602 ROTAP", 35000, nx).find("FIR exit ROTAP") != std::string::npos);
    CHECK(ExplainSrd(srdx, "OKBK", "LTBA", "DCT SIDAD XYZ ROTAP", 35000, nx).find("does not match") != std::string::npos);

    // flight plan check (VFPC rules)
    {
        using namespace kwiq::fpc;
        Rules rules = ParseRules(
            "FPC|OKKK|SESRU||ODD|||| OMDB, OI |M677,G669||\r\n"
            "FPC|OKKK|SESRU||ODD|||||M677,G669||\n"
            "FPC|OKKK|BOXIK||ODD||270||OBBI|B416|J|\n"
            "FPC|ORBI|BGD|1A|EVEN|80|600|250,260||DELMI DCT RUKAM B411 MURIB||RG\n"
            "FPC|ORBI|BGD||ANY|240||||NOLDO P975 SIDAD||\n"
            "FPC|XX|ABC||ODD|||||||\nFPC|ORBI|BGD|short\njunk\n");
        CHECK(rules.size() == 2 && rules["OKKK"]["SESRU"].size() == 2 && rules["ORBI"]["BGD"].size() == 2);
        CHECK(rules["OKKK"]["SESRU"][0].destinations.size() == 2 && rules["ORBI"]["BGD"][0].forbidden.size() == 2);
        CHECK(rules["ORBI"]["BGD"][0].airways[0].size() == 4);    // DCT dropped

        Flight f; f.origin = "OKKK"; f.destination = "OMDB"; f.sid = "SESRU1A"; f.route = "SESRU1A SESRU M677 XYZ";
        f.rflFt = 35000; f.engine = 'J'; f.capability = 'L';
        Result r = Check(rules, f);
        CHECK(r.Passed() && r.sidPoint == "SESRU" && r.suffix == "1A");
        CHECK(Explain(r, f).find("passed (destination OMDB, via M677, odd)") != std::string::npos);
        Flight g = f; g.rflFt = 36000;
        r = Check(rules, g);
        CHECK(!r.Passed() && r.fails.size() == 1 && r.fails[0] == Fail::Direction && std::string(Code(r.fails[0])) == "E/O");
        CHECK(Explain(r, g).find("FL360 is even, the SID needs odd") != std::string::npos);
        g = f; g.sid = "";
        CHECK(Check(rules, g).fails[0] == Fail::Sid);
        g = f; g.sid = "SESRU";                                     // no number: not a SID
        CHECK(Check(rules, g).fails[0] == Fail::Sid);
        g = f; g.sid = "ABCDE1A";
        CHECK(Check(rules, g).fails[0] == Fail::Sid);
        g = f; g.destination = "EGLL"; g.route = "SESRU G669 XYZ";  // not a listed destination: the next rule
        CHECK(Check(rules, g).Passed());
        g = f; g.route = "SESRU UL999 XYZ";                         // no rule fits
        r = Check(rules, g);
        CHECK(r.fails.size() == 1 && r.fails[0] == Fail::Sid && Explain(r, g).find("fits") != std::string::npos);
        g = f; g.sid = "BOXIK1B"; g.destination = "OBBI"; g.route = "BOXIK1B BOXIK B416 OBBI"; g.rflFt = 29000;
        r = Check(rules, g);
        CHECK(r.fails.size() == 1 && r.fails[0] == Fail::Max);
        g.engine = 'P';
        r = Check(rules, g);
        CHECK(r.fails.size() == 2 && r.fails[0] == Fail::Engine && r.fails[1] == Fail::Max);

        Flight b; b.origin = "ORBI"; b.destination = "LTBA"; b.sid = "BGD1A"; b.route = "BGD DELMI DCT RUKAM B411 MURIB";
        b.rflFt = 26000; b.engine = 'J'; b.capability = 'R';
        r = Check(rules, b);
        CHECK(r.fails.size() == 1 && r.fails[0] == Fail::Forbidden && r.WarningOnly());
        b.capability = 'X';
        r = Check(rules, b);
        CHECK(r.fails.size() == 2 && r.fails[1] == Fail::Navigation && !r.WarningOnly());
        b.sid = "BGD1B"; b.route = "BGD NOLDO P975 SIDAD"; b.rflFt = 23000; b.capability = 'R';   // 1A rule skipped
        r = Check(rules, b);
        CHECK(r.fails.size() == 1 && r.fails[0] == Fail::Min);

        g = f; g.vfr = true; g.sid = "";
        r = Check(rules, g);
        CHECK(r.Passed() && r.vfr && Explain(r, g).find("VFR") != std::string::npos);
        g = f; g.origin = "OMDB";                                   // no rules for the airport: nothing shown
        CHECK(!Check(rules, g).checked);
        CHECK(ContainsRun({ "A", "B", "C" }, { "B", "C" }) && !ContainsRun({ "A", "B", "C" }, { "A", "C" }));
    }

    // SRD search
    CHECK(SrdSearch(srd, "okbk", "orbi", 0).size() == 2);
    CHECK(SrdSearch(srd, "OKBK", "ORBI", 35000).size() == 1);   // only the 45000-max row
    CHECK(SrdSearch(srd, "", "ORBI", 0).size() == 2);
    CHECK(SrdSearch(srd, "", "", 0).size() == 3);
    CHECK(SrdSearch(srd, "ABC", "XYZ", 0).size() == 1);
    CHECK(SrdSearch(srd, "NOPE", "", 0).empty());
    // SRD search window columns and notes
    CHECK(SrdMinText(srd.rows[0]) == "100" && SrdMaxText(srd.rows[0]) == "300");
    CHECK(SrdMinText(srd.rows[1]) == "MC" && SrdMaxText(srd.rows[2]) == "-");
    CHECK(SrdNotesText(srd.rows[0]) == "Jets only" && SrdNotesText(srd.rows[1]) == "No notes.");
    SrdRoute fra; fra.rte = "ABC <FRA> XYZ";
    CHECK(HasFreeRoute(fra.rte) && !HasFreeRoute(srd.rows[0].rte));
    CHECK(SrdNotesText(fra).find("Free Route Airspace") != std::string::npos);

    // sector indicators, only for the ESE position IDs Core lists, shown as their alias
    SectorMap sm = ParseSectors("SECTOR|bs|sr\nSECTOR|BN|NR\nSECTOR|BGA|BGD\nSECTOR|BX|\nSECTOR|BL|ABCDE\nbad\n");
    CHECK(sm.size() == 4 && sm["BS"] == "SR" && sm["BN"] == "NR" && sm["BGA"] == "BGD" && sm["BL"] == "ABCD");
    CHECK(sm.find("BX") == sm.end());
    CHECK(ParseSectors("NONE\n").empty());

    // sector indicator colour by flight plan state, named as in EuroScope's Symbology
    CHECK(std::string(SectorColourName(0)) == "non_concerned" && std::string(SectorColourName(1)) == "notified" &&
          std::string(SectorColourName(2)) == "information" && std::string(SectorColourName(3)) == "transfer_to_me_initiated" &&
          std::string(SectorColourName(4)) == "assumed" && std::string(SectorColourName(5)) == "assumed" &&
          std::string(SectorColourName(7)) == "redundant" && std::string(SectorColourName(42)) == "non_concerned");


    // Core config
    PluginConfig cfg = ParseConfig("plugin.enabled|1\r\nui.text_ok|GOOD\nrefresh.core_s|5\nbad line\nui.colour_ok|#00BE00\nplugin.message|\n");
    CHECK(cfg.Get("ui.text_ok", "OK") == "GOOD" && cfg.Get("missing", "dflt") == "dflt");
    CHECK(cfg.GetBool("plugin.enabled", false) && !cfg.GetBool("nothere", false) && cfg.GetBool("nothere", true));
    CHECK(cfg.GetInt("refresh.core_s", 60, 30, 3600) == 30 && cfg.GetInt("none", 60, 30, 3600) == 60);
    CHECK(ParseColour("#FF8000", 1) == 0x000080FFul);        // COLORREF is 0x00BBGGRR
    CHECK(ParseColour("bogus", 7) == 7 && ParseColour("#GG0000", 7) == 7);
    CHECK(CompareVersions("0.3.0", "0.3.1") < 0 && CompareVersions("1.0", "0.9.9") > 0 && CompareVersions("0.3", "0.3.0") == 0);

    // EuroScope version
    CHECK(EuroScopeVersionAccepted("3.2.3.2", "3.2.3.2") && EuroScopeVersionAccepted("3.2.3.0", "3.2.3") && EuroScopeVersionAccepted("3.2.3.2", " 3.2.1 , 3.2.3.2 "));
    CHECK(!EuroScopeVersionAccepted("3.2.9.0", "3.2.3.2") && !EuroScopeVersionAccepted("3.2.3.1", "3.2.3.2") && !EuroScopeVersionAccepted("3.2.10.0", "3.2.1"));
    CHECK(EuroScopeVersionAccepted("3.2.3.2", "") && EuroScopeVersionAccepted("3.2.3.2", " , "));
    CHECK(EuroScopeVersions("3.2.1, ,3.2.3.2").size() == 2);

    // AIRAC
    Airac a1 = FindAirac("2610/02"), a2 = FindAirac(";  AIRAC 2610/1  ++  File-Publisher: Kuwait & Iraq vACC - ORBB", "AIRAC");
    CHECK(a1.valid() && a1.cycle == 2610 && a1.rev == 2 && a1.Str() == "2610/02");
    CHECK(a2.valid() && a2.cycle == 2610 && a2.rev == 1);
    CHECK(CompareAirac(a2, a1) < 0 && CompareAirac(a1, a1) == 0 && CompareAirac(FindAirac("2611/01"), a1) > 0);
    CHECK(CompareAirac(FindAirac("2610/2"), a1) == 0);                  // 2 == 02
    CHECK(!FindAirac("no version here").valid() && !FindAirac("AIRAC none", "AIRAC").valid());
    CHECK(!FindAirac("12610/02").valid() && FindAirac("\xEF\xBB\xBF" "2610/02\r\n").valid());

    // self-update helpers
    CHECK(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(Sha256Hex(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    {
        std::string dll(0x400, '\0');
        dll[0] = 'M'; dll[1] = 'Z'; dll[0x3C] = (char)0x80;
        dll.replace(0x80, 4, std::string("PE\0\0", 4));
        dll[0x84] = 0x4c; dll[0x85] = 0x01; dll[0x80 + 22] = 0x00; dll[0x80 + 23] = 0x20;
        CHECK(LooksLikeX86Dll(dll));
        std::string x64 = dll; x64[0x84] = 0x64; x64[0x85] = (char)0x86;
        std::string exe = dll; exe[0x80 + 23] = 0x00;
        CHECK(!LooksLikeX86Dll(x64) && !LooksLikeX86Dll(exe) && !LooksLikeX86Dll("<html>404</html>") && !LooksLikeX86Dll(""));
    }
    std::string sha64(64, 'a');
    CHECK(UpdateAvailable("0.4.0", "0.5.0", sha64) && !UpdateAvailable("0.5.0", "0.5.0", sha64) && !UpdateAvailable("0.6.0", "0.5.0", sha64));
    CHECK(!UpdateAvailable("0.4.0", "0.5.0", "") && !UpdateAvailable("0.4.0", "", sha64));

    // which sector file package is loaded
    CHECK(SectorPackage("C:\\Users\\x\\AppData\\Roaming\\EuroScope\\ORBB-2610_02 - Copy\\ORBB\\ORBB.ese", "") == "ORBB");
    CHECK(SectorPackage("C:\\EuroScope\\OKAC-2610_02\\OKAC\\AIRAC_VERSION.txt", "ORBI_TWR") == "OKAC");
    CHECK(SectorPackage("C:\\EuroScope\\okac\\files\\orbb.ese", "") == "ORBB");    // the name nearest the file
    CHECK(SectorPackage("C:\\EuroScope\\Sector\\Iraq.ese", "ORBI_TWR") == "ORBB");
    CHECK(SectorPackage("", "OKBK_GND") == "OKAC");
    CHECK(SectorPackage("C:\\EuroScope\\Sector\\x.ese", "LON_CTR") == "");

    CHECK(AiracFromPath("C:\\Users\\x\\EuroScope\\ORBB-2610_02 - Copy\\ORBB\\Plugins").Str() == "2610/02");
    CHECK(AiracFromPath("C:\\EuroScope\\okac-2608_1\\OKAC").Str() == "2608/01");
    CHECK(!AiracFromPath("C:\\EuroScope\\ORBB\\Plugins").valid());

    // personal API key from myKWIQ
    CHECK(IsApiKey("kwiq_295d6ebdAbCdEfGhIjKlMnOpQrStUvWxYz012345"));
    CHECK(ApiKeyId("kwiq_295d6ebdAbCdEfGhIjKlMnOpQrStUvWxYz012345") == "295d6ebd");
    CHECK(!IsApiKey("kwiq_295d6ebd"));
    CHECK(!IsApiKey("kwiq_295d6ebdAbCdEfGhIjKlMnOpQrStUvWxYz01234 "));
    CHECK(!IsApiKey("KWIQ_295d6ebdAbCdEfGhIjKlMnOpQrStUvWxYz012345"));
    CHECK(ApiKeyId("nonsense") == "");
    CHECK(QueryValue("GET /?key=kwiq_abc123&state=Xy9 HTTP/1.1\r\nHost: 127.0.0.1\r\n", "key") == "kwiq_abc123");
    CHECK(QueryValue("GET /?key=kwiq_abc123&state=Xy9 HTTP/1.1\r\n", "state") == "Xy9");
    CHECK(QueryValue("GET /?key=a%20b HTTP/1.1\r\n", "key") == "");
    CHECK(QueryValue("POST /?key=abc HTTP/1.1\r\n", "key") == "");
    CHECK(QueryValue("GET /favicon.ico HTTP/1.1\r\n", "key") == "");

    // squawk assignment (rules from the UK Controller Plugin)
    {
        using namespace kwiq::squawk;
        CHECK(ValidSquawk("4521") && !ValidSquawk("4528") && !ValidSquawk("452") && !ValidSquawk("45a1"));
        // Core's non-assignable codes: never handed out; "counts as assigned" only for the emergency ones
        const CodeList codes = ParseNonAssignable(
            "NONASSIGNABLE|7500|7500|Emergency|1|Never to be used on the VATSIM network\r\n"
            "NONASSIGNABLE|7600|7600|emergency|1|Radio failure\n"
            "NONASSIGNABLE|7700|7700|emergency|1|Mayday\n"
            "NONASSIGNABLE|2000|2000|conspicuity|0|IFR conspicuity\n"
            "NONASSIGNABLE|7000|7000|conspicuity|0|VFR conspicuity\n"
            "NONASSIGNABLE|2200|2200|default|0|Often used VATSIM non-discrete code\n"
            "NONASSIGNABLE|1200|1200|default|0|Default code on pilot clients\n"
            "NONASSIGNABLE|0|0|default|0|Sometimes appears in the VATSIM data feed\n"
            "NONASSIGNABLE|9999|9999|default|0|Sometimes appears in the VATSIM data feed\n"
            "NONASSIGNABLE|0440|0447|test|0|A range | with a bar\n"
            "NONASSIGNABLE|7477|7400|other|0|first after last\n"                 // left out: malformed
            "NONASSIGNABLE|1280|1290|other|0|not octal\n"
            "NONASSIGNABLE|1300|1300|other|2|bad flag\n"
            "NONASSIGNABLE|12345|12345|other|0|too long\n"
            "NONASSIGNABLE|1400|1400|other\n"
            "SQUAWK|1234\n");
        CHECK(codes.size() == 10 && codes[0].category == "emergency" && codes[0].countsAsAssigned && !codes[3].countsAsAssigned);
        CHECK(codes[9].first == "0440" && codes[9].last == "0447" && codes[9].description == "A range | with a bar");
        CHECK(ParseNonAssignable("").empty() && ParseNonAssignable("junk\nSQUAWK|4521\n").empty());
        CHECK(FindCode(codes, "0") && FindCode(codes, "9999") && FindCode(codes, "0443") && !FindCode(codes, "0450") && !FindCode(codes, "4521"));
        // use 1: Core may hand out a code only if it isn't on the list
        CHECK(!AllowedSquawk(codes, "7700") && !AllowedSquawk(codes, "2200") && !AllowedSquawk(codes, "0444") && AllowedSquawk(codes, "4521"));
        CHECK(AllowedSquawk(codes, "1234") && !AllowedSquawk(codes, "1238") && !AllowedSquawk(CodeList(), "9999"));   // only what Core lists
        // use 2: an emergency code counts as a code; default and non-discrete ones don't
        CHECK(HasAssignedSquawk(codes, "7700") && HasAssignedSquawk(codes, "7500") && HasAssignedSquawk(codes, "4521"));
        CHECK(!HasAssignedSquawk(codes, "2200") && !HasAssignedSquawk(codes, "2000") && !HasAssignedSquawk(codes, "7000") &&
              !HasAssignedSquawk(codes, "1200") && !HasAssignedSquawk(codes, "0441") && !HasAssignedSquawk(codes, ""));
        CHECK(HasAssignedSquawk(CodeList(), "2000"));               // nothing built in
        CHECK(PositionMatches("OKBK_TWR", "OKBK_N_TWR") && PositionMatches("OKBK_TWR", "okbk_twr"));
        CHECK(!PositionMatches("OKBK_TWR", "OKBK_APP") && !PositionMatches("OKBK_TWR", "OKBK"));
        CHECK(CallsignPrefix("OKBK_N_TWR") == "OKBK" && CallsignSuffix("OKBK_N_TWR") == "TWR");

        // top-down: the lowest position anyone is on gives departures their squawk
        TopDown td = ParseTopDown("TOPDOWN|okbk|OKBK_DEL, OKBK_GND,OKBK_TWR,OKAC_CTR\r\nTOPDOWN|BAD|X_Y\nJUNK\n");
        CHECK(td.size() == 1 && td["OKBK"].size() == 4);
        CHECK(DeliveryByMe(td, "OKBK", "OKAC_CTR", {}));
        CHECK(!DeliveryByMe(td, "OKBK", "OKAC_CTR", { "OKBK_TWR" }));
        CHECK(DeliveryByMe(td, "OKBK", "OKBK_GND", { "OKBK_TWR", "OKAC_CTR" }));
        CHECK(!DeliveryByMe(td, "ORBI", "OKAC_CTR", {}));          // no order from Core: only its own positions
        CHECK(DeliveryByMe(td, "ORBI", "ORBI_TWR", { "ORBI_APP" }));

        User twr{ true, "OKBK_TWR" }, ctr{ true, "OKAC_CTR" }, obs{ false, "" };
        Aircraft gnd;                                               // on the ground at OKBK
        gnd.callsign = "KAC123"; gnd.origin = "OKBK"; gnd.destination = "OMDB";
        gnd.fromOrigin = 0.5; gnd.altitude = 200; gnd.groundSpeed = 0; gnd.deliveryByMe = true;
        CHECK(Decide(twr, gnd, nullptr, codes) == Action::Assign);
        Aircraft a = gnd; a.deliveryByMe = false;
        CHECK(Decide(twr, a, nullptr, codes) == Action::None);
        a = gnd; a.groundSpeed = 140;                               // untracked and rolling
        CHECK(Decide(twr, a, nullptr, codes) == Action::None);
        a = gnd; a.squawk = "4521";
        CHECK(Decide(twr, a, nullptr, codes) == Action::None);
        a = gnd; a.squawk = "2000";                                 // 2000 is no code
        CHECK(Decide(twr, a, nullptr, codes) == Action::Assign);
        a = gnd; a.squawk = "7000";                                 // VFR conspicuity doesn't count: replaced
        CHECK(Decide(twr, a, nullptr, codes) == Action::Assign);
        a = gnd; a.squawk = "7700";                                 // an emergency counts: never replaced
        CHECK(Decide(twr, a, nullptr, codes) == Action::None);
        a = gnd; a.squawk = "7600";
        CHECK(Decide(twr, a, nullptr, codes) == Action::None);
        a = gnd; a.altitude = 0;                                    // no position yet
        CHECK(Decide(twr, a, nullptr, codes) == Action::None);
        CHECK(Decide(obs, gnd, nullptr, codes) == Action::None);
        a = gnd; a.vfr = true; a.destination = "OKBK";              // VFR circuits: an ordinary code, Core picks the range
        CHECK(Decide(twr, a, nullptr, codes) == Action::Assign);
        Stored same{ "OKBK", "OMDB", "4521", false, 0 }, moved{ "OKBK", "OTHH", "4521", false, 0 };
        CHECK(Decide(twr, gnd, &same, codes) == Action::Previous);         // reconnected: same code back
        CHECK(ForceNeeded(&moved, gnd) && Decide(twr, gnd, &moved, codes) == Action::Assign);
        Stored byHand{ "OKBK", "OMDB", "2000", true, 0 };           // set to 2000 by hand: left alone
        a = gnd; a.squawk = "2000";
        CHECK(Decide(twr, a, &byHand, codes) == Action::None);
        byHand.destination = "OTHH";                                // until the plan changes
        CHECK(Decide(twr, a, &byHand, codes) == Action::Assign);

        Aircraft trk = gnd;                                         // tracked by the user, airborne, far away
        trk.tracked = trk.trackedByMe = true; trk.deliveryByMe = false;
        trk.groundSpeed = 450; trk.fromOrigin = 120; trk.altitude = 30000;
        CHECK(Decide(ctr, trk, nullptr, codes) == Action::Assign);

        Aircraft other = gnd; other.tracked = true;
        CHECK(ChangeAllowed(twr, gnd) && !ChangeAllowed(twr, other) && !ChangeAllowed(obs, gnd));
        CHECK(ParseSquawk("SQUAWK|4521\r\n", codes) == "4521" && ParseSquawk("SQUAWK|7700", codes) == "" && ParseSquawk("nope", codes) == "");
        CHECK(ParseError("ERROR|No codes left\n") == "No codes left" && ParseError("SQUAWK|4521") == "");
        CHECK(UrlSafeCallsign("KAC123") && !UrlSafeCallsign("KAC/12") && !UrlSafeCallsign(""));
        CHECK(FormEncode({ { "origin", "OKBK" }, { "rules", "V I" } }) == "origin=OKBK&rules=V%20I");
        // the request: the flight and the requester (the controller's own callsign)
        CHECK(AssignForm(gnd, "OKBK_TWR") == "origin=OKBK&destination=OMDB&rules=I&requester=OKBK_TWR");
        Aircraft v = gnd; v.vfr = true;
        CHECK(AssignForm(v, "ORBI_N_APP") == "origin=OKBK&destination=OMDB&rules=V&requester=ORBI_N_APP");
    }

    // api-settings.json, as UKCP keeps its key
    const std::string K = "kwiq_295d6ebdAbCdEfGhIjKlMnOpQrStUvWxYz012345";
    CHECK(ApiKeyFromSettingsJson(ApiSettingsJson(K)) == K);
    CHECK(ApiKeyFromSettingsJson("{\"api-url\":\"https://x\",\"api-key\" : \" " + K + " \"}") == K);
    CHECK(ApiKeyFromSettingsJson("{\"api-key\": \"not a key\"}") == "");
    CHECK(ApiKeyFromSettingsJson("{}") == "" && ApiKeyFromSettingsJson("") == "" && ApiKeyFromSettingsJson("{\"api-key\": \"kwiq_") == "");
    CHECK(ApiKeyFromSettingsJson("{\r\n    \"api-key\": \"" + K + "\"\r\n}\r\n") == K);           // CRLF, as Notepad saves it

    // starting the plugin
    CHECK(DecideStart(true, false, true, false) == StartMode::Full);
    CHECK(DecideStart(true, false, true, true) == StartMode::Reduced);
    CHECK(DecideStart(true, true, true, false) == StartMode::DontLoad);
    CHECK(DecideStart(false, true, true, false) == StartMode::Full);       // Core unreachable: the switch is unknown
    CHECK(DecideStart(false, false, false, false) == StartMode::DontLoad);

    // RDF settings from Core, by callsign pattern
    CHECK(WildcardMatch("ORBB_*_CTR", "orbb_s_ctr") && WildcardMatch("*", "") && WildcardMatch("OK??_TWR", "OKBK_TWR"));
    CHECK(!WildcardMatch("ORBB_*_CTR", "ORBB_S_APP") && !WildcardMatch("OK??_TWR", "OKBK_A_TWR") && WildcardMatch("*CTR", "ORBB_CTR"));
    auto rdf = ParseRdf("RDF|ORBB_*_CTR|draw=1|colour=#00FF00|radius=30|require_tx=on\nRDF|*|bridge=0|radius=-5\nJUNK\nRDF||draw=1\n");
    CHECK(rdf.size() == 2);
    RdfSettings c = RdfFor(rdf, "ORBB_S_CTR");
    CHECK(c.draw && c.bridge && c.requireTx && c.radius == 30 && c.colour == 0x00FF00 && c.colourConcurrent == 0x0000FF);
    RdfSettings o = RdfFor(rdf, "OKBK_TWR");
    CHECK(!o.draw && !o.bridge && o.radius == 20);                      // a bad radius keeps the default
    RdfSettings none = RdfFor({}, "OKBK_TWR");
    CHECK(!none.draw && none.bridge && none.threshold == -1);

    // TrackAudio 1.3.0 Beta 5 or later
    CHECK(TrackAudioVersionOk("TrackAudio v1.3.0-beta.5") && TrackAudioVersionOk("1.3.0") && TrackAudioVersionOk("v1.4.0-beta.1"));
    CHECK(TrackAudioVersionOk("1.3.0-rc.1") && !TrackAudioVersionOk("1.3.0-beta.4") && !TrackAudioVersionOk("1.2.9") && !TrackAudioVersionOk("TrackAudio"));
    // RDF edge cases (#52)
    CHECK(ParseRdf("").empty() && ParseRdf("RDF\r\nRDF| \r\n").empty());
    auto rcr = ParseRdf("RDF|okbk_twr|draw=1|radius=x|threshold=-5|colour=#00ff00|nokey\r\n");
    CHECK(rcr.size() == 1 && rcr[0].pattern == "OKBK_TWR" && rcr[0].s.draw && rcr[0].s.radius == 20 && rcr[0].s.threshold == -5 && rcr[0].s.colour == 0x00FF00);
    CHECK(WildcardMatch("", "") && !WildcardMatch("", "A") && WildcardMatch("**", "abc") && WildcardMatch("a*c", "abbbc") && !WildcardMatch("a*c", "abcd"));
    CHECK(!ParseSemVer("").valid && !ParseSemVer("1.2").valid && ParseSemVer("v1.3.0-beta.5").pre == 1 && ParseSemVer("v1.3.0-beta.5").preNum == 5);

    // the KWIQ menu's TrackAudio row
    CHECK(TrackAudioStatus(false, true, true, "TrackAudio v1.3.0", false) == TrackAudioState::Off);       // RDF didn't start
    CHECK(TrackAudioStatus(true, false, true, "TrackAudio v1.3.0", true) == TrackAudioState::Off);        // Core: bridge and drawing off
    CHECK(TrackAudioStatus(true, true, true, "TrackAudio v1.3.0", false) == TrackAudioState::Connected);
    CHECK(TrackAudioStatus(true, true, true, "", false) == TrackAudioState::Connected);                    // no version (yet)
    CHECK(TrackAudioStatus(true, true, true, "TrackAudio", false) == TrackAudioState::Connected);          // unreadable version
    CHECK(TrackAudioStatus(true, true, true, "TrackAudio v1.3.0-beta.4", false) == TrackAudioState::TooOld);
    CHECK(TrackAudioStatus(true, true, false, "TrackAudio v1.3.0-beta.4", false) == TrackAudioState::NotRunning);
    CHECK(TrackAudioStatus(true, true, false, "", true) == TrackAudioState::AfvClient);
    CHECK(std::string(TrackAudioStateText(TrackAudioState::Connected)) == "Connected" &&
          std::string(TrackAudioStateText(TrackAudioState::TooOld)) == "Version too old" &&
          std::string(TrackAudioStateText(TrackAudioState::AfvClient)) == "AFV client" &&
          std::string(TrackAudioStateText(TrackAudioState::NotRunning)) == "Not running" &&
          std::string(TrackAudioStateText(TrackAudioState::Off)) == "Off");
    CHECK(TrackAudioClickAction(TrackAudioState::Connected) == TrackAudioClick::Resync &&
          TrackAudioClickAction(TrackAudioState::TooOld) == TrackAudioClick::Resync &&
          TrackAudioClickAction(TrackAudioState::NotRunning) == TrackAudioClick::Reconnect &&
          TrackAudioClickAction(TrackAudioState::AfvClient) == TrackAudioClick::Reconnect &&
          TrackAudioClickAction(TrackAudioState::Off) == TrackAudioClick::None);

    // DiscordEuroScope rules from Core
    DiscordConfig dc = ParseDiscord(
        "APP|1529517649506074776\r\nSWEATBOX_BYPASS|0\n"
        "MESSAGE|controller||||Controlling {rcallsign} on {frequency}MHz|Tracking {current_tracked} aircraft\n"
        "MESSAGE|controller||ORBB_*_CTR|1|Controlling {rcallsign} for {event} on {frequency} MHz|\n"
        "MESSAGE|controller||||Controlling {callsign}|\n"
        "MESSAGE|idle||||Preparing the scopes!\n"
        "IMAGE|||||https://cdn.kuwait-iraqvacc.net/discord.png|{callsign}||\n"
        "BUTTON|||||Website|https://kuwait-iraqvacc.net\nBUTTON|||||Broken|\n"
        "RADIO|ORBB|124.400|Baghdad Control\n");
    CHECK(dc.appId == "1529517649506074776" && !dc.sweatboxBypass && dc.messages.size() == 4 && dc.images.size() == 1 && dc.buttons.size() == 1);
    CHECK(DiscordRadioCallsign(dc, "ORBB_S_CTR", "124.400") == "Baghdad Control" && DiscordRadioCallsign(dc, "OKAC_CTR", "124.400") == "");
    DiscordContext dx; dx.state = "controller"; dx.fir = "ORBB"; dx.callsign = "ORBB_S_CTR"; dx.event = true;
    std::map<std::string, std::string> dv = { {"callsign","ORBB_S_CTR"}, {"rcallsign","Baghdad Control"}, {"frequency","124.400"},
                                              {"current_tracked","3"}, {"total_tracked","5"}, {"total_inrange","40"}, {"event","Tuesdays Over Iraq"} };
    CHECK(DiscordPick(dc.messages, dx, dv, 7) == 1);                     // the event message is the most specific
    std::string dout;
    CHECK(DiscordFormat(dc.messages[1].v[0], dv, dout) && dout == "Controlling Baghdad Control for Tuesdays Over Iraq on 124.400 MHz");
    dx.event = false; dv["event"] = "";
    CHECK(DiscordPick(dc.messages, dx, dv, 0) == 0 || DiscordPick(dc.messages, dx, dv, 0) == 2);
    dv["rcallsign"] = "";                                                 // unknown radio callsign: the {callsign} one
    CHECK(DiscordPick(dc.messages, dx, dv, 0) == 2 && DiscordPick(dc.messages, dx, dv, 1) == 2);
    dx.state = "idle";
    CHECK(DiscordPick(dc.messages, dx, dv, 3) == 3 && DiscordPick(dc.images, dx, dv, 0) == 0);
    dx.state = "playback";
    CHECK(DiscordPick(dc.messages, dx, dv, 0) == -1);
    CHECK(DiscordFormat("{unknown} {callsign}", dv, dout) && dout == "{unknown} ORBB_S_CTR");
    // Discord's byte limits, never cutting a UTF-8 character in half (#50)
    CHECK(Utf8Truncate("abc", 5) == "abc" && Utf8Truncate("abcdef", 3) == "abc" && Utf8Truncate("", 0).empty() && Utf8Truncate("abc", 0).empty());
    CHECK(Utf8Truncate("a\xC3\xA9", 2) == "a" && Utf8Truncate("a\xC3\xA9", 3) == "a\xC3\xA9");                 // e acute: 2 bytes
    CHECK(Utf8Truncate("\xF0\x9F\x9B\xAB!", 3).empty() && Utf8Truncate("\xF0\x9F\x9B\xAB!", 4) == "\xF0\x9F\x9B\xAB");   // an emoji: 4
    CHECK(Utf8Truncate("ab\xD8\xA8\xD8\xBA", 5) == "ab\xD8\xA8");                                              // Arabic: 2 a letter
    // DiscordEuroScope edge cases (#52)
    {
        DiscordConfig de = ParseDiscord("");
        CHECK(de.appId.empty() && de.messages.empty() && de.images.empty() && de.buttons.empty() && !de.sweatboxBypass);
        de = ParseDiscord("APP\r\nMESSAGE|controller|||\r\nIMAGE|||||k\r\nBUTTON|||||L\r\nRADIO|OR|1\r\nSWEATBOX_BYPASS|1\r\n");
        CHECK(de.appId.empty() && de.messages.empty() && de.images.size() == 1 && de.images[0].v.size() == 4 && de.images[0].v[0] == "k");
        CHECK(de.buttons.empty() && de.radios.empty() && de.sweatboxBypass);          // a button needs a label and a URL
        std::map<std::string, std::string> none;
        none["event"] = "";
        CHECK(DiscordFormat("{", none, dout) && dout == "{" && !DiscordFormat("on {event}", none, dout) && dout == "on ");
        CHECK(DiscordMatch(DiscordFilter(), DiscordContext()) == 0 && DiscordPick(std::vector<DiscordEntry>(), dx, dv, 0) == -1);
        CHECK(DiscordRadioCallsign(dc, "", "").empty() && DiscordRadioCallsign(dc, "ORBB", "124.400") == "Baghdad Control");
    }

    // QNH monitor
    CHECK(QnhFromMetar("OKBK 101250Z 32012KT CAVOK 24/M02 Q1012 NOSIG") == 1012);
    CHECK(QnhFromMetar("ORBI 101250Z 29010KT 9999 FEW040 18/03 Q0979=") == 979);
    CHECK(QnhFromMetar("KJFK 101251Z 31015KT 10SM FEW250 08/M06 A2992 RMK AO2 SLP132") == 1013);
    CHECK(QnhFromMetar("OKBK 101250Z 32012KT CAVOK 24/M02 RMK Q1012") == -1 && QnhFromMetar("") == -1);
    auto metars = ParseMetars("METAR|okbk|OKBK 101250Z Q1012\nMETAR|XX|bad\nJUNK\n");
    CHECK(metars.size() == 1 && metars["OKBK"] == "OKBK 101250Z Q1012");
    auto trl = ParseTrl("TRL|OR|14000|160,980,170\nTRL|OK|13000|150\nTRL|ORMM|14000|170,1000,180\nTRL|OX|1|1,2\n");
    CHECK(trl.size() == 3);
    CHECK(TrlFor(trl, "ORBI", 981) == 160 && TrlFor(trl, "ORBI", 980) == 160 && TrlFor(trl, "ORBI", 979) == 170);
    CHECK(TrlFor(trl, "OKBK", 960) == 150 && TrlFor(trl, "ORMM", 1000) == 170 && TrlFor(trl, "ORMM", 999) == 180);
    CHECK(TrlFor(trl, "OEJN", 1013) == 0 && TrlFor(trl, "ORBI", -1) == 0);
    CHECK(QnhMessage("ORBI", 979, 981, 170, 160) == "ORBI QNH 979 (was 981) TRL FL170 - CHANGED from FL160");
    CHECK(QnhMessage("ORBI", 1004, 1006, 160, 160) == "ORBI QNH 1004 (was 1006) TRL FL160");
    CHECK(QnhMessage("OEJN", 1013, 0, 0, 0) == "OEJN QNH 1013");

    // top-down
    auto td = ParseTopDownOrders("TOPDOWN|OKBK|OKBK_DEL,OKBK_GND,OKBK_TWR,OKBK_APP,OKAC_CTR\nTOPDOWN|ORBI|ORBI_TWR,ORBI_APP,ORBB_S_CTR\n");
    CHECK(CoveredByMe(td, "OKBK", "OKAC_CTR", {}) && !CoveredByMe(td, "OKBK", "OKAC_CTR", { "OKBK_N_TWR" }));
    CHECK(CoveredByMe(td, "OKBK", "OKBK_1_TWR", { "OKAC_CTR" }) && !CoveredByMe(td, "ORBI", "OKAC_CTR", {}));
    CHECK(CoveredByMe(td, "ORMM", "ORMM_TWR", {}) && !CoveredByMe(td, "ORMM", "ORBB_S_CTR", {}));     // no order: own positions
    auto cov = AirportsCovered(td, "ORBB_S_CTR", { "ORBI_APP" });
    CHECK(cov.empty());
    cov = AirportsCovered(td, "ORBB_S_CTR", {});
    CHECK(cov.size() == 1 && cov[0] == "ORBI");

    // procedure notifications
    auto notices = ParseNotices("NOTICE|12|OKBK, okkk|2026-11-01|0|Standard taxi routes|https://docs.kuwait-iraqvacc.net/x|Line 1\\nLine | 2\nNOTICE||OKBK|x|0|t|l|b\n");
    CHECK(notices.size() == 1 && notices[0].id == "12" && notices[0].airports.size() == 2 && notices[0].airports[1] == "OKKK");
    CHECK(!notices[0].read && notices[0].text == "Line 1\r\nLine | 2" && notices[0].link == "https://docs.kuwait-iraqvacc.net/x");
    CHECK(NoticeRelevant(notices[0], { "OKBK" }) && !NoticeRelevant(notices[0], { "ORBI" }));
    // notification IDs in Core's URL (#49)
    CHECK(UrlEncode("12") == "12" && UrlEncode("a-b_c.d~e") == "a-b_c.d~e" && UrlEncode("").empty());
    CHECK(UrlEncode("1/../x?y#z") == "1%2F..%2Fx%3Fy%23z" && UrlEncode("a b") == "a%20b" && UrlEncode("\xC3\xA9") == "%C3%A9");

    // QNH monitor, top-down and notifications: empty, CRLF, short and malformed lines (#52)
    CHECK(QnhFromMetar("OKBK Q101") == -1 && QnhFromMetar("OKBK QABCD A29.9") == -1 && QnhFromMetar("METAR OKBK Q1012\r\n") == 1012);
    CHECK(ParseMetars("").empty() && ParseMetars("METAR|OKBK\r\nMETAR||x\r\n").empty() && ParseMetars("METAR|OKBK|OKBK Q1012\r\n").at("OKBK") == "OKBK Q1012");
    CHECK(ParseTrl("").empty() && ParseTrl("TRL|OR|14000\r\nTRL||1|150\r\nTRL|OR|x|150,980\r\n").empty());
    auto trl2 = ParseTrl("TRL|or|14000|160, 980 ,170\r\n");
    CHECK(trl2.size() == 1 && trl2[0].prefix == "OR" && trl2[0].ta == 14000 && TrlFor(trl2, "orbi", 980) == 160 && TrlFor({}, "ORBI", 1013) == 0);
    CHECK(QnhMessage("OKBK", 1012, 1012, 0, 150) == "OKBK QNH 1012");
    CHECK(ParseTopDownOrders("").empty() && ParseTopDownOrders("TOPDOWN|OKBK\r\nTOPDOWN|OKB|OKB_TWR\r\nTOPDOWN|OKBK|TWR, \r\n").empty());
    auto td2 = ParseTopDownOrders("TOPDOWN|okbk| okbk_twr ,OKAC_CTR\r\n");
    CHECK(td2.size() == 1 && td2["OKBK"].size() == 2 && td2["OKBK"][0] == "OKBK_TWR");
    CHECK(!TopDownPositionMatches("OKBK_TWR", "OKBK") && !TopDownPositionMatches("OKBK", "OKBK_TWR") && TopDownPositionMatches("okbk_twr", "OKBK_X_TWR"));
    CHECK(!CoveredByMe(td2, "OKBK", "", {}) && !CoveredByMe(td2, "", "OKBK_TWR", {}) && AirportsCovered(td2, "", {}).empty());
    CHECK(ParseNotices("").empty() && ParseNotices("NOTICE|1|OKBK|2026-11-01|0|t|l\r\n").empty());
    auto n2 = ParseNotices("NOTICE| 7 |OKBK,,|2026-11-01|1|Title|https://x.net|end\\n\r\n");
    CHECK(n2.size() == 1 && n2[0].id == "7" && n2[0].airports.size() == 1 && n2[0].read && n2[0].text == "end\r\n" && !NoticeRelevant(n2[0], {}));

    // the KWIQ button (#59): 28 px at 100%, 35 at 125%, 42 at 150%, 49 at 175%, 56 at 200%
    CHECK(ScaleForDpi(28, 96) == 28 && ScaleForDpi(28, 120) == 35 && ScaleForDpi(28, 144) == 42 && ScaleForDpi(28, 168) == 49 &&
          ScaleForDpi(28, 192) == 56);
    CHECK(ScaleForDpi(8, 120) == 10 && ScaleForDpi(28, 0) == 28 && ScaleForDpi(28, -5) == 28);
    {
        // its place: an offset from the bottom-right corner, so it follows the window
        ButtonXY off; off.x = 8; off.y = 8;
        ButtonXY tl = ButtonTopLeft(800, 600, 28, off);
        CHECK(tl.x == 764 && tl.y == 564);
        tl = ButtonTopLeft(1000, 700, 28, off);                         // the window grew: still 8 px from the corner
        CHECK(tl.x == 964 && tl.y == 664);
        off.x = 5000; off.y = -40;                                     // clamped fully on screen
        tl = ButtonTopLeft(800, 600, 28, off);
        CHECK(tl.x == 0 && tl.y == 572);
        tl = ButtonTopLeft(20, 10, 28, off);                           // a radar area smaller than the button
        CHECK(tl.x == 0 && tl.y == 0);
        // dragging: centred on the mouse, clamped, and back again
        ButtonXY d = ButtonOffsetAt(800, 600, 28, 400, 300);
        CHECK(d.x == 386 && d.y == 286);
        tl = ButtonTopLeft(800, 600, 28, d);
        CHECK(tl.x == 386 && tl.y == 286);
        d = ButtonOffsetAt(800, 600, 28, -50, 9999);
        CHECK(d.x == 772 && d.y == 0);
        d = ButtonOffsetAt(800, 600, 28, 9999, -50);
        CHECK(d.x == 0 && d.y == 572);
        // the older kwiq_button_at (top left from the radar's top left) keeps its place
        d = ButtonOffsetFromTopLeft(800, 600, 28, 378, 10);
        tl = ButtonTopLeft(800, 600, 28, d);
        CHECK(d.x == 394 && d.y == 562 && tl.x == 378 && tl.y == 10);
        // the profile's "x,y"
        ButtonXY p;
        CHECK(ParseButtonXY("12,34", p) && p.x == 12 && p.y == 34 && FormatButtonXY(p) == "12,34");
        CHECK(ParseButtonXY("0,0", p) && p.x == 0 && p.y == 0);
        p.x = 7; p.y = 7;
        CHECK(!ParseButtonXY("", p) && !ParseButtonXY("12", p) && !ParseButtonXY("-1,5", p) && !ParseButtonXY("1,2,3", p) &&
              !ParseButtonXY(" 1,2", p) && !ParseButtonXY("1,", p) && !ParseButtonXY("9999999,1", p) && p.x == 7 && p.y == 7);
        // which embedded logo to draw
        std::vector<int> sizes = { 28, 35, 42, 49, 56 };
        CHECK(BestLogoSize(sizes, 28) == 28 && BestLogoSize(sizes, 31) == 35 && BestLogoSize(sizes, 56) == 56);
        CHECK(BestLogoSize(sizes, 80) == 56 && BestLogoSize(sizes, 10) == 28 && BestLogoSize(std::vector<int>(), 28) == 0);
        // premultiplied alpha
        unsigned char px[12] = { 200, 100, 50, 255,  200, 100, 50, 0,  200, 100, 50, 128 };
        PremultiplyBgra(px, 3);
        CHECK(px[0] == 200 && px[1] == 100 && px[2] == 50 && px[3] == 255 && px[4] == 0 && px[5] == 0 && px[6] == 0 && px[7] == 0);
        CHECK(px[8] == 100 && px[9] == 50 && px[10] == 25 && px[11] == 128);
    }

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
