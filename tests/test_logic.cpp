// Logic tests: g++ -std=c++17 -Wall -Wextra -I. tests/test_logic.cpp -o test_logic && ./test_logic
#include "kwiq_logic.hpp"
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

    // sector indicators (ORBB only)
    SectorMap sm = ParseSectors("SECTOR|orbb_s_ctr|sr\nSECTOR|ORBB_N_CTR|NR\nSECTOR|OKAC_CTR|KR\nSECTOR|ORBB_X|\nSECTOR|ORBB_LONG|ABCDE\nbad\n");
    CHECK(sm.size() == 3 && sm["ORBB_S_CTR"] == "SR" && sm["ORBB_N_CTR"] == "NR" && sm["ORBB_LONG"] == "ABCD");
    CHECK(sm.find("OKAC_CTR") == sm.end() && sm.find("ORBB_X") == sm.end());
    CHECK(IsOrbbCallsign("ORBB_S_CTR") && IsOrbbCallsign("ORBB") && !IsOrbbCallsign("ORBBX") && !IsOrbbCallsign("OKAC_CTR") && !IsOrbbCallsign(""));
    CHECK(ParseSectors("NONE\n").empty());


    // Core config
    PluginConfig cfg = ParseConfig("plugin.enabled|1\r\nui.text_ok|GOOD\nrefresh.core_s|5\nbad line\nui.colour_ok|#00BE00\nplugin.message|\n");
    CHECK(cfg.Get("ui.text_ok", "OK") == "GOOD" && cfg.Get("missing", "dflt") == "dflt");
    CHECK(cfg.GetBool("plugin.enabled", false) && !cfg.GetBool("nothere", false) && cfg.GetBool("nothere", true));
    CHECK(cfg.GetInt("refresh.core_s", 60, 30, 3600) == 30 && cfg.GetInt("none", 60, 30, 3600) == 60);
    CHECK(ParseColour("#FF8000", 1) == 0x000080FFul);        // COLORREF is 0x00BBGGRR
    CHECK(ParseColour("bogus", 7) == 7 && ParseColour("#GG0000", 7) == 7);
    CHECK(CompareVersions("0.3.0", "0.3.1") < 0 && CompareVersions("1.0", "0.9.9") > 0 && CompareVersions("0.3", "0.3.0") == 0);

    // AIRAC
    Airac a1 = FindAirac("2610/02"), a2 = FindAirac(";  AIRAC 2610/1  ++  File-Publisher: Kuwait & Iraq vACC - ORBB", "AIRAC");
    CHECK(a1.valid() && a1.cycle == 2610 && a1.rev == 2 && a1.Str() == "2610/02");
    CHECK(a2.valid() && a2.cycle == 2610 && a2.rev == 1);
    CHECK(CompareAirac(a2, a1) < 0 && CompareAirac(a1, a1) == 0 && CompareAirac(FindAirac("2611/01"), a1) > 0);
    CHECK(CompareAirac(FindAirac("2610/2"), a1) == 0);                  // 2 == 02
    CHECK(!FindAirac("no version here").valid() && !FindAirac("AIRAC none", "AIRAC").valid());
    CHECK(!FindAirac("12610/02").valid() && FindAirac("\xEF\xBB\xBF" "2610/02\r\n").valid());

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
