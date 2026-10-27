// Logic tests: g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic
#include "kwiq_logic.hpp"
#include "fpc_logic.hpp"
using namespace kwiq;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
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

    // sector indicators, for any position Core lists
    SectorMap sm = ParseSectors("SECTOR|orbb_s_ctr|sr\nSECTOR|ORBB_N_CTR|NR\nSECTOR|BGD_APP|BGD\nSECTOR|ORBB_X|\nSECTOR|ORBB_LONG|ABCDE\nbad\n");
    CHECK(sm.size() == 4 && sm["ORBB_S_CTR"] == "SR" && sm["ORBB_N_CTR"] == "NR" && sm["BGD_APP"] == "BGD" && sm["ORBB_LONG"] == "ABCD");
    CHECK(sm.find("ORBB_X") == sm.end());
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

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
