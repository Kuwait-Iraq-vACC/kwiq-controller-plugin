// pegasus_logic.hpp - the text of the Pegasus tag items.
//
// Rebuilt from CPegasusES 1.2 Public (P21Euroscope.dll, Hubert SO & Matt Z), which shipped in the OKAC package;
// see pegasus.cpp and NOTICE.md. Pure functions, no EuroScope or Windows, so the logic tests cover them.
// The original's behaviour is kept, including its quirks (marked QUIRK), except Heading in h, Speed in s and
// Rate in r, which show TopSky's formats (220°, N180 / M079, R20).
#pragma once
#include <cmath>
#include <cstring>
#include <string>

namespace kwiq::pegasus {

// EuroScope font glyphs
inline constexpr const char* kGlyphVisual = "\xA9";   // cleared visual approach (cleared level 2)
inline constexpr const char* kGlyphIls    = "\xAE";   // cleared ILS approach (cleared level 1)
inline constexpr const char* kGlyphCoord  = "\xA4";   // ongoing coordination
inline constexpr const char* kGlyphDegree = "\xB0";   // after a heading

// value, left-padded with zeros to at least `digits` digits (negative values unpadded)
inline std::string Zero(int value, int digits) {
    std::string s = std::to_string(value);
    if (value >= 0 && (int)s.size() < digits) s.insert(0, (size_t)(digits - (int)s.size()), '0');
    return s;
}

// The original streamed rounded doubles: 350.0 prints as "350"
inline std::string Rounded(double v) {
    return std::to_string((long long)std::llround(v));
}

// Ground speed (items 9 and 25). QUIRK: only the "below 100" branch is ever reached, so 5 kt is "05", 0 kt "00".
inline std::string GroundSpeed(int gs) {
    if (gs < 100) return "0" + std::to_string(gs);
    return std::to_string(gs);
}

// Vertical speed in the G/S / ROC cycle (item 9). QUIRK: ft/min divided by 200, and level flight is "-00".
inline std::string ClimbRate(int vs) {
    int roc = (vs >= -499 && vs <= 499) ? 0 : vs / 200;
    if (roc > 0) return "+" + std::string(roc < 10 ? "0" : "") + std::to_string(roc);
    return std::string(roc > -10 ? "-0" : "") + std::to_string(-roc);
}

// Heading in h (item 1): "220°", else the direct-to point, else "h  " (the original's placeholder).
// A COPN/COPX coordination in progress is shown instead by pegasus.cpp.
inline std::string Heading(int hdg, const std::string& directTo) {
    if (hdg != 0) return Zero(hdg, 3) + kGlyphDegree;
    if (!directTo.empty()) return directTo;
    return "h  ";
}

// Speed in s (item 2), as TopSky's ASP: "N180" (knots), "M079" (1/100 Mach), else "s " (the original's placeholder)
inline std::string Speed(int ias, int mach) {
    if (ias > 0) return "N" + Zero(ias, 3);
    if (mach > 0) return "M" + Zero(mach, 3);
    return "s ";
}

// Rate in r (item 3), as TopSky's ARC: "R20" (hundreds of ft/min), else "r"
inline std::string Rate(int rate) {
    return rate > 0 ? "R" + Zero(rate / 100, 2) : "r";
}

// RFL (item 4): thousands at 10000 ft and above (FL350 is "35"), hundreds below, "##" with none
inline std::string Rfl2(int rfl) {
    if (rfl == 0) return "##";
    return std::to_string(rfl >= 10000 ? rfl / 1000 : rfl / 100);
}

// Destination or FIR COPX (item 5). QUIRK: Polish destinations (EP**) always show the destination, as the
// plugin was written for Poland.
inline std::string DestCopx(const std::string& dest, const std::string& copx) {
    if (dest.compare(0, 2, "EP") == 0) return dest;
    if (copx.size() >= 3) return copx;
    return dest;
}

// FQ warning (item 12): 0 nothing, 1 "FQ" in the default colour (2 minutes to exit), 2 "FQ" in yellow (0-1)
inline int FqWarning(int exitMinutes, bool trackedByMe) {
    if (!trackedByMe) return 0;
    if (exitMinutes == 2) return 1;
    if (exitMinutes >= 0 && exitMinutes <= 1) return 2;
    return 0;
}

// AFL (item 21): "A045" at or below the transition altitude (pressure altitude), " 350" above it (flight level)
inline std::string Afl(int pressureAlt, int flightLevel, int transAlt) {
    if (pressureAlt <= transAlt) {
        std::string v = Rounded(pressureAlt / 100.0);
        if (pressureAlt >= 9950) return "A" + v;
        if (pressureAlt < 950) return "A00" + v;
        return "A0" + v;
    }
    return std::string(flightLevel < 9950 ? " 0" : " ") + Rounded(flightLevel / 100.0);
}

// Cleared level, in hundreds with a leading zero below 100 ("080")
inline std::string Level(int cfl) {
    return std::string(cfl / 100 < 100 ? "0" : "") + std::to_string(cfl / 100);
}

// Custom CFL (item 18): the ILS/visual glyphs, otherwise the CFL only when the aircraft is more than 200 ft from it
inline std::string Cfl(int cadCfl, int cfl, int transAlt, int pressureAlt, int flightLevel) {
    if (cadCfl == 2) return kGlyphVisual;
    if (cadCfl == 1) return kGlyphIls;
    if (cadCfl >= 3 || cadCfl == 0) {
        int diff = cfl - (cfl <= transAlt ? pressureAlt : flightLevel);
        if (diff > 200 || diff < -200) return Level(cfl);
    }
    return "";
}

// Custom CFL (Static) (item 22): the glyphs, otherwise always the CFL
inline std::string CflStatic(int cadCfl, int cfl) {
    if (cadCfl == 2) return kGlyphVisual;
    if (cadCfl == 1) return kGlyphIls;
    if (cadCfl >= 3 || cadCfl == 0) return Level(cfl);
    return "";
}

// Custom XFL (item 19): "A" + hundreds at or below the transition altitude, empty when none or equal to the CFL
inline std::string Xfl(int xfl, int cfl, int transAlt) {
    if (xfl == 0 || xfl == cfl) return "";
    int h = xfl / 100;
    if (xfl > transAlt) return std::string(h < 100 ? "0" : "") + std::to_string(h);
    return "A" + std::to_string(h);
}

}  // namespace kwiq::pegasus
