// pegasus.hpp - the CPegasusES (P21) tag items, built into the KWIQ plugin, and the HSR item.
//
// Rebuilt from CPegasusES 1.2 Public (P21Euroscope.dll, Hubert SO & Matt Z), see pegasus.cpp and NOTICE.md.
// EuroScope takes one plugin per DLL, so this is not a CPlugIn: CKWIQPlugin registers its items and passes
// their tag item and function calls on, the way RDF is built in. The texts are in pegasus_logic.hpp.
#pragma once
#include <windows.h>
#include "EuroScopePlugIn.h"

// Tag item IDs: 1100 + the original CPegasusES ID, so a tag can be moved over item by item. HSR is new.
// Function IDs: 2200 + the original. Clear of KWIQ (1001-1007, 2001-2115) and VCH (100, 340-650).
enum {
    PEG_ITEM_HEADING      = 1101,   // Heading in h
    PEG_ITEM_SPEED        = 1102,   // Speed in s
    PEG_ITEM_ROC          = 1103,   // Rate of Climb in r
    PEG_ITEM_RFL          = 1104,   // RFL 2 digit
    PEG_ITEM_DEST_COPX    = 1105,   // Dest / FIR COPX Point (autoselected)
    PEG_ITEM_SQUAWK_ERROR = 1106,   // Squawk Error (if different from assigned)
    PEG_ITEM_TYPE_SQUAWK  = 1108,   // A/C Type / Squawk Code (Cycle)
    PEG_ITEM_GS_ROC       = 1109,   // G/S / ROC (Cycle)
    PEG_ITEM_FQ_WARNING   = 1112,   // FQ Warning
    PEG_ITEM_RAM_WARNING  = 1113,   // Custom RAM Warning
    PEG_ITEM_CLAM_WARNING = 1114,   // Custom CLAM Warning
    PEG_ITEM_COORD        = 1115,   // Ongoing COORD indicator
    PEG_ITEM_CFL          = 1118,   // Custom CFL
    PEG_ITEM_XFL          = 1119,   // Custom XFL
    PEG_ITEM_CALLSIGN     = 1120,   // Custom Callsign
    PEG_ITEM_AFL          = 1121,   // Custom AFL
    PEG_ITEM_CFL_STATIC   = 1122,   // Custom CFL (Static)
    PEG_ITEM_COMM_TYPE    = 1123,   // Custom Comm Type (always "#", as in the original)
    PEG_ITEM_ACTYPE       = 1124,   // Custom A/C Type
    PEG_ITEM_GS           = 1125,   // Custom G/S
    PEG_ITEM_HSR          = 1127,   // HSR (Heading / Speed / Rate), KWIQ's own (CPegasusES's TEST, 26, is left out)
    PEG_ITEM_FIRST        = PEG_ITEM_HEADING,
    PEG_ITEM_LAST         = PEG_ITEM_HSR,
    PEG_FUNC_TYPE_CYCLE   = 2210,   // ACTYPE/ASSR Cycle
    PEG_FUNC_GS_CYCLE     = 2211,   // GS/ROC Cycle
    PEG_FUNC_HSR_LEFT     = 2212,   // HSR: open TopSky's AHDG/ASP/ARC menu, or its Waypoint menu for a coordination
    PEG_FUNC_HSR_RIGHT    = 2213,   // HSR: TopSky's ack RJC/FAIL coordination / clear value
    PEG_FUNC_FIRST        = PEG_FUNC_TYPE_CYCLE,
    PEG_FUNC_LAST         = PEG_FUNC_HSR_RIGHT
};

class KwiqPegasus {
public:
    explicit KwiqPegasus(EuroScopePlugIn::CPlugIn* es);
    // Constructor of CKWIQPlugin: registers the tag items and functions
    void Register();
    static bool IsItem(int code) { return code >= PEG_ITEM_FIRST && code <= PEG_ITEM_LAST; }
    static bool IsFunction(int id) { return id >= PEG_FUNC_FIRST && id <= PEG_FUNC_LAST; }
    void GetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode,
                    char sItemString[16],
                    int* pColorCode, COLORREF* pRGB);
    // radar: any of the plugin's radar screens, for StartTagFunction (TopSky's menus); may be null
    void FunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area,
                      EuroScopePlugIn::CRadarScreen* radar);

private:
    EuroScopePlugIn::CPlugIn* es_;
    // Flipped by the two cycle functions; each flips it for every tag, as in the original
    bool showType_ = true;   // A/C Type / Squawk Code: type and WTC, or "A" + squawk
    bool showGs_ = true;     // G/S / ROC: ground speed, or rate of climb
    void Hsr(EuroScopePlugIn::CFlightPlan& fp, bool left, const char* sItemString, POINT Pt, RECT Area,
             EuroScopePlugIn::CRadarScreen* radar);
};
