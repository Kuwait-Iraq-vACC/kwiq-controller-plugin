// pegasus_logic.hpp - the text of the Pegasus tag items, and the HSR item's click fields.
//
// Rebuilt from CPegasusES 1.2 Public (P21Euroscope.dll, Hubert SO & Matt Z), which shipped in the OKAC package;
// see pegasus.cpp and NOTICE.md. Pure functions, no EuroScope or Windows, so the logic tests cover them.
// The original's behaviour is kept, including its quirks (marked QUIRK), so the items look exactly as before.
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

// Assigned heading (item 1): "090°"
inline std::string Heading(int hdg) {
    return std::string(hdg / 100 == 0 ? "0" : "") + std::to_string(hdg) + kGlyphDegree;
}

// Assigned speed (item 2): "K25" (IAS / 10), "M78", or "s " with neither
inline std::string SpeedItem(int ias, int mach) {
    if (ias == 0 && mach == 0) return "s ";
    if (ias > 0) return "K" + std::to_string(ias >= 10 ? ias / 10 : ias);
    if (mach > 0) return "M" + std::to_string(mach);
    return "";
}

// Assigned rate (item 3): "R05", "R25", or "r"
inline std::string RateItem(int rate) {
    if (rate == 0) return "r";
    return "R" + std::string(rate < 1000 ? "0" : "") + std::to_string(rate / 100);
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

// ---- HSR (Heading / Speed / Rate), KWIQ's own item, in TopSky's field formats -------------------------------

// Heading field: "220°", the direct-to point, or "h"
inline std::string HsrHeading(int hdg, const std::string& directTo) {
    if (hdg != 0) return Zero(hdg, 3) + kGlyphDegree;
    if (!directTo.empty()) return directTo;
    return "h";
}

// Speed field, as TopSky's ASP: "N180" (knots), "M079" (1/100 Mach), or "s"
inline std::string HsrSpeed(int ias, int mach) {
    if (ias > 0) return "N" + Zero(ias, 3);
    if (mach > 0) return "M" + Zero(mach, 3);
    return "s";
}

// Rate field, as TopSky's ARC: "R20" (hundreds of ft/min), or "r"
inline std::string HsrRate(int rate) {
    return rate > 0 ? "R" + Zero(rate / 100, 2) : "r";
}

// The three fields, padded so each stays in its column: "220° N180 R20", "h    M079 r"
inline std::string HsrText(std::string hdg, std::string spd, const std::string& rate) {
    if (hdg.size() < 4) hdg.append(4 - hdg.size(), ' ');
    if (spd.size() < 4) spd.append(4 - spd.size(), ' ');
    return hdg + " " + spd + " " + rate;
}

// Which field a click on the item hit: 0 heading, 1 speed, 2 rate, -1 if it can't be told.
// x is the click from the item's left edge, width the item's width; tag fonts are monospaced, so the
// position maps straight to a character. A click between two fields goes to the nearer one.
// field/start/len (optional) get the field's text position.
inline int HsrField(const std::string& text, int x, int width, size_t* start = nullptr, size_t* len = nullptr) {
    size_t s[3] = {}, e[3] = {};
    int n = 0;
    for (size_t i = 0; i < text.size() && n < 3;) {
        while (i < text.size() && text[i] == ' ') i++;
        if (i >= text.size()) break;
        s[n] = i;
        while (i < text.size() && text[i] != ' ') i++;
        e[n++] = i;
    }
    if (n < 3 || width <= 0) return -1;
    double col = (double)x * (double)text.size() / (double)width;
    int f = 2;
    if (col < (double)(e[0] + s[1]) / 2.0) f = 0;
    else if (col < (double)(e[1] + s[2]) / 2.0) f = 1;
    if (start) *start = s[f];
    if (len) *len = e[f] - s[f];
    return f;
}

}  // namespace kwiq::pegasus
