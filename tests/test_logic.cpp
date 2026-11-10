// Logic tests: g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic
#include "kwiq_logic.hpp"
#include "squawk_logic.hpp"
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

    // SRD parse + FPC
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
    CHECK(CheckFpc(srd, "OKBK", "ORBI", F("ABC UL123 XYZ"), 25000) == FpcResult::Ok);
    CHECK(CheckFpc(srd, "OKBK", "ORBI", F("ABC UL123 XYZ"), 35000) == FpcResult::Max);   // row0 max 30000
    CHECK(CheckFpc(srd, "OKBK", "ORBI", F("ABC UL123 XYZ"), 5000)  == FpcResult::Min);
    CHECK(CheckFpc(srd, "OKBK", "ORBI", F("QQQ"), 40000) == FpcResult::Ok);              // other row allows it
    CHECK(CheckFpc(srd, "OKBK", "ORBI", F("ABC XYZ"), 25000) == FpcResult::Route);
    CHECK(CheckFpc(srd, "OKBK", "ORBI", F("ABC UL123 XYZ"), 0) == FpcResult::Ok);        // unknown level: skip
    CHECK(CheckFpc(srd, "OKBK", "OMDB", F("DEF GHI"), 25000) == FpcResult::None);
    CHECK(CheckFpc(srd, "OKBK", "OMDB", F("ABC XYZ"), 25000) == FpcResult::Route);       // enroute row wants UL123
    // waypoint (enroute) row applies when both ends are on the filed route
    CHECK(CheckFpc(srd, "OTHH", "OIII", F("ABC UL123 XYZ"), 25000) == FpcResult::Ok);
    CHECK(CheckFpc(srd, "OTHH", "OIII", F("ABC UL123 XYZ"), 15000) == FpcResult::Min);
    CHECK(CheckFpc(srd, "OTHH", "OIII", F("DEF UL999"), 25000) == FpcResult::None);
    CHECK(ExplainFpc(srd, "OKBK", "ORBI", "ABC XYZ", 25000).find("does not match") != std::string::npos);
    CHECK(ExplainFpc(srd, "OKBK", "ORBI", "ABC UL123 XYZ", 35000).find("FL100") != std::string::npos);
    CHECK(ExplainFpc(srd, "OKBK", "ORBI", "ABC UL123 XYZ", 25000).find("conform") != std::string::npos);
    CHECK(ParseLevelFt("350") == 35000 && ParseLevelFt("FL350") == 35000 && ParseLevelFt("fl 350") == 35000 &&
          ParseLevelFt("35000") == 35000 && ParseLevelFt("") == 0 && ParseLevelFt("abc") == 0);
    CHECK(FlStr(35000) == "FL350" && FlStr(5000) == "FL050");

    // SRD search
    CHECK(SrdSearch(srd, "okbk", "orbi", 0).size() == 2);
    CHECK(SrdSearch(srd, "OKBK", "ORBI", 35000).size() == 1);   // only the 45000-max row
    CHECK(SrdSearch(srd, "", "ORBI", 0).size() == 2);
    CHECK(SrdSearch(srd, "", "", 0).size() == 3);
    CHECK(SrdSearch(srd, "ABC", "XYZ", 0).size() == 1);
    CHECK(SrdSearch(srd, "NOPE", "", 0).empty());

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

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
