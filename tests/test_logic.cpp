// Logic tests: g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic
#include "kwiq_logic.hpp"
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

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
