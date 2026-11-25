// pegasus.cpp - the CPegasusES (P21) tag items, built into the KWIQ plugin, and the HSR item.
//
// Rebuilt from CPegasusES 1.2 Public (P21Euroscope.dll, Hubert SO & Matt Z), the plugin written for Poland's
// Pegasus_21 display that shipped in the OKAC package. No source was published, so this was reconstructed from
// the DLL; see NOTICE.md. The items show what the original showed, including its quirks (pegasus_logic.hpp).
// Changes: the IDs (pegasus.hpp), a flight plan track without a radar target is supported, the 16-character item
// is never overrun, and the TEST item is left out. HSR is KWIQ's own.
#include "pegasus/pegasus.hpp"
#include <cstring>
#include <string>
#include "pegasus/pegasus_logic.hpp"
#include "kwiq_log.hpp"

using namespace EuroScopePlugIn;
namespace peg = kwiq::pegasus;

namespace {
// Tag colours
const COLORREF kIdent      = RGB(0x00, 0x73, 0xE6);   // blue: squawking IDENT
const COLORREF kCoordOther = RGB(0xE3, 0x6A, 0x29);   // orange: a coordination requested by another controller
const COLORREF kCoordMe    = RGB(0x29, 0xCD, 0xE3);   // cyan: a coordination requested by you
const COLORREF kWarning    = RGB(0xED, 0xD0, 0x00);   // yellow: SQ, FQ, RO, AW

// TopSky 2.6 tag item and function IDs, from TopSky.dll's registrations (2.6b6), for StartTagFunction
const char* const kTopSky = "TopSky plugin";
const int kTsItemCopnCopx = 44, kTsItemAsp = 47, kTsItemAhdg = 54, kTsItemArc = 56;
const int kTsOpenAhdg = 14, kTsOpenAsp = 15, kTsOpenArc = 16, kTsOpenWaypoint = 45;
const int kTsAckAsp = 71, kTsAckAhdg = 133, kTsAckArc = 134;   // Ack RJC or FAIL ... coord / Clear value

void Set(char sItemString[16], const std::string& s) {
    strncpy_s(sItemString, 16, s.c_str(), _TRUNCATE);
}

void Colour(int* pColorCode, COLORREF* pRGB, COLORREF c) {
    *pColorCode = TAG_COLOR_RGB_DEFINED;
    *pRGB = c;
}

// Blue while the aircraft squawks IDENT
void Ident(CFlightPlan& fp, int* pColorCode, COLORREF* pRGB) {
    if (fp.GetFPTrackPosition().GetTransponderI()) Colour(pColorCode, pRGB, kIdent);
}

std::string Str(const char* s) { return s ? s : ""; }

// A COPN/COPX coordination in progress: the point to show in place of the heading, and its colour.
// Requested by another controller first, then by you; exit before entry. False when there is none.
bool PendingPoint(CFlightPlan& fp, std::string& point, COLORREF& colour) {
    const int states[2] = { COORDINATION_STATE_REQUESTED_BY_OTHER, COORDINATION_STATE_REQUESTED_BY_ME };
    for (int state : states) {
        colour = state == COORDINATION_STATE_REQUESTED_BY_OTHER ? kCoordOther : kCoordMe;
        if (fp.GetExitCoordinationNameState() == state) { point = Str(fp.GetExitCoordinationPointName()); return true; }
        if (fp.GetEntryCoordinationPointState() == state) { point = Str(fp.GetEntryCoordinationPointName()); return true; }
    }
    return false;
}

// Any coordination (point or level) in the given state
bool AnyCoordination(CFlightPlan& fp, int state) {
    return fp.GetExitCoordinationNameState() == state || fp.GetExitCoordinationAltitudeState() == state ||
           fp.GetEntryCoordinationAltitudeState() == state || fp.GetEntryCoordinationPointState() == state;
}
}  // namespace

KwiqPegasus::KwiqPegasus(CPlugIn* es) : es_(es) {}

void KwiqPegasus::Register() {
    es_->RegisterTagItemType("Heading in h", PEG_ITEM_HEADING);
    es_->RegisterTagItemType("Speed in s", PEG_ITEM_SPEED);
    es_->RegisterTagItemType("Rate of Climb in r", PEG_ITEM_ROC);
    es_->RegisterTagItemType("RFL 2 digit", PEG_ITEM_RFL);
    es_->RegisterTagItemType("Dest / FIR COPX Point (autoselected)", PEG_ITEM_DEST_COPX);
    es_->RegisterTagItemType("Squawk Error (if different from assigned)", PEG_ITEM_SQUAWK_ERROR);
    es_->RegisterTagItemType("A/C Type / Squawk Code (Cycle)", PEG_ITEM_TYPE_SQUAWK);
    es_->RegisterTagItemType("G/S / ROC (Cycle)", PEG_ITEM_GS_ROC);
    es_->RegisterTagItemType("FQ Warning", PEG_ITEM_FQ_WARNING);
    es_->RegisterTagItemType("Custom RAM Warning", PEG_ITEM_RAM_WARNING);
    es_->RegisterTagItemType("Custom CLAM Warning", PEG_ITEM_CLAM_WARNING);
    es_->RegisterTagItemType("Ongoing COORD indicator (experimental)", PEG_ITEM_COORD);
    es_->RegisterTagItemType("Custom XFL", PEG_ITEM_XFL);
    es_->RegisterTagItemType("Custom CFL", PEG_ITEM_CFL);
    es_->RegisterTagItemType("Custom Callsign", PEG_ITEM_CALLSIGN);
    es_->RegisterTagItemType("Custom AFL", PEG_ITEM_AFL);
    es_->RegisterTagItemType("Custom CFL (Static)", PEG_ITEM_CFL_STATIC);
    es_->RegisterTagItemType("Custom Comm Type (Not working)", PEG_ITEM_COMM_TYPE);
    es_->RegisterTagItemType("Custom G/S", PEG_ITEM_GS);
    es_->RegisterTagItemType("Custom A/C Type", PEG_ITEM_ACTYPE);
    es_->RegisterTagItemType("HSR (Heading / Speed / Rate)", PEG_ITEM_HSR);
    es_->RegisterTagItemFunction("ACTYPE/ASSR Cycle", PEG_FUNC_TYPE_CYCLE);
    es_->RegisterTagItemFunction("GS/ROC Cycle", PEG_FUNC_GS_CYCLE);
    es_->RegisterTagItemFunction("HSR - Open TopSky menu", PEG_FUNC_HSR_LEFT);
    es_->RegisterTagItemFunction("HSR - TopSky ack / clear value", PEG_FUNC_HSR_RIGHT);
}

void KwiqPegasus::GetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, char sItemString[16],
                             int* pColorCode, COLORREF* pRGB) {
    // As the original: the flight plan of the radar target, looked up by callsign
    CFlightPlan fp = RadarTarget.IsValid() ? es_->FlightPlanSelect(RadarTarget.GetCallsign()) : FlightPlan;
    if (!fp.IsValid()) return;
    CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
    CFlightPlanData fpd = fp.GetFlightPlanData();
    CRadarTargetPositionData track = fp.GetFPTrackPosition();
    const int transAlt = es_->GetTransitionAltitude();

    switch (ItemCode) {
    case PEG_ITEM_HEADING: {
        std::string point;
        COLORREF colour = 0;
        if (PendingPoint(fp, point, colour)) { Set(sItemString, point); Colour(pColorCode, pRGB, colour); }
        else if (cad.GetAssignedHeading() != 0) Set(sItemString, peg::Heading(cad.GetAssignedHeading()));
        else if (!Str(cad.GetDirectToPointName()).empty()) Set(sItemString, Str(cad.GetDirectToPointName()));
        else Set(sItemString, "h  ");
        Ident(fp, pColorCode, pRGB);
        return;
    }
    case PEG_ITEM_SPEED:
        Set(sItemString, peg::SpeedItem(cad.GetAssignedSpeed(), cad.GetAssignedMach()));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_ROC:
        Set(sItemString, peg::RateItem(cad.GetAssignedRate()));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_RFL:
        Set(sItemString, peg::Rfl2(fp.GetFinalAltitude()));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_DEST_COPX:
        Set(sItemString, peg::DestCopx(Str(fpd.GetDestination()), Str(fp.GetNextFirCopxPointName())));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_SQUAWK_ERROR:
        if (fp.GetTrackingControllerIsMe() || fp.GetSectorEntryMinutes() >= 0) {
            if (Str(cad.GetSquawk()) != Str(track.GetSquawk())) { Colour(pColorCode, pRGB, kWarning); Set(sItemString, "SQ"); }
            return;
        }
        // QUIRK: neither tracked by you nor inbound to your sector, the original goes on to the FQ warning
        [[fallthrough]];
    case PEG_ITEM_FQ_WARNING: {
        *pColorCode = TAG_COLOR_DEFAULT;
        int fq = peg::FqWarning(fp.GetSectorExitMinutes(), fp.GetTrackingControllerIsMe());
        if (fq == 2) Colour(pColorCode, pRGB, kWarning);
        Set(sItemString, fq ? "FQ" : "");
        return;
    }
    case PEG_ITEM_RAM_WARNING:
        if (fp.GetRAMFlag() && fp.GetTrackingControllerIsMe()) { Colour(pColorCode, pRGB, kWarning); Set(sItemString, "RO"); }
        return;
    case PEG_ITEM_CLAM_WARNING:
        if (fp.GetCLAMFlag() && fp.GetTrackingControllerIsMe()) { Colour(pColorCode, pRGB, kWarning); Set(sItemString, "AW"); }
        return;
    case PEG_ITEM_COORD:
        if (AnyCoordination(fp, COORDINATION_STATE_REQUESTED_BY_ME)) { Colour(pColorCode, pRGB, kCoordMe); Set(sItemString, peg::kGlyphCoord); }
        else if (AnyCoordination(fp, COORDINATION_STATE_REQUESTED_BY_OTHER)) { Colour(pColorCode, pRGB, kCoordOther); Set(sItemString, peg::kGlyphCoord); }
        else Set(sItemString, " ");
        return;
    case PEG_ITEM_TYPE_SQUAWK:
        Set(sItemString, showType_ ? Str(fpd.GetAircraftFPType()) + fpd.GetAircraftWtc() : "A" + Str(track.GetSquawk()));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_GS_ROC:
        if (!RadarTarget.IsValid()) return;
        Set(sItemString, showGs_ ? peg::GroundSpeed(RadarTarget.GetPosition().GetReportedGS())
                                 : peg::ClimbRate(RadarTarget.GetVerticalSpeed()));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_CFL:
        Set(sItemString, peg::Cfl(cad.GetClearedAltitude(), fp.GetClearedAltitude(), transAlt,
                                  track.GetPressureAltitude(), track.GetFlightLevel()));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_XFL: {
        int xfl = 0;
        if (fp.GetSectorExitMinutes() > -1) xfl = fp.GetExitCoordinationAltitude();
        if (fp.GetSectorEntryMinutes() > 0 && !fp.GetTrackingControllerIsMe()) xfl = fp.GetEntryCoordinationAltitude();
        std::string t = peg::Xfl(xfl, fp.GetClearedAltitude(), transAlt);
        if (!t.empty()) Set(sItemString, t);
        Ident(fp, pColorCode, pRGB);
        return;
    }
    case PEG_ITEM_CALLSIGN:
        Set(sItemString, Str(fp.GetCallsign()));
        Ident(fp, pColorCode, pRGB);
        if (fp.GetState() == FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED) *pColorCode = TAG_COLOR_TRANSFER_TO_ME_INITIATED;
        else if (fp.GetState() == FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED) *pColorCode = TAG_COLOR_TRANSFER_TO_ME_INITIATED + 1;
        return;
    case PEG_ITEM_AFL:
        Set(sItemString, peg::Afl(track.GetPressureAltitude(), track.GetFlightLevel(), transAlt));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_CFL_STATIC:
        Set(sItemString, peg::CflStatic(cad.GetClearedAltitude(), fp.GetClearedAltitude()));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_COMM_TYPE:
        Set(sItemString, "#");   // QUIRK: the original ends every path with "#" (it was labelled "Not working")
        return;
    case PEG_ITEM_ACTYPE:
        Set(sItemString, Str(fpd.GetAircraftFPType()) + fpd.GetAircraftWtc());
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_GS:
        if (!RadarTarget.IsValid()) return;
        Set(sItemString, peg::GroundSpeed(RadarTarget.GetPosition().GetReportedGS()));
        Ident(fp, pColorCode, pRGB);
        return;
    case PEG_ITEM_HSR: {
        std::string hdg;
        COLORREF colour = 0;
        if (PendingPoint(fp, hdg, colour)) Colour(pColorCode, pRGB, colour);
        else hdg = peg::HsrHeading(cad.GetAssignedHeading(), Str(cad.GetDirectToPointName()));
        Set(sItemString, peg::HsrText(hdg, peg::HsrSpeed(cad.GetAssignedSpeed(), cad.GetAssignedMach()),
                                      peg::HsrRate(cad.GetAssignedRate())));
        Ident(fp, pColorCode, pRGB);
        return;
    }
    default:
        return;
    }
}

void KwiqPegasus::FunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area, CRadarScreen* radar) {
    if (FunctionId == PEG_FUNC_TYPE_CYCLE) { showType_ = !showType_; return; }
    if (FunctionId == PEG_FUNC_GS_CYCLE) { showGs_ = !showGs_; return; }
    if (FunctionId != PEG_FUNC_HSR_LEFT && FunctionId != PEG_FUNC_HSR_RIGHT) return;
    CFlightPlan fp = es_->FlightPlanSelectASEL();   // EuroScope selects the clicked aircraft first
    if (!fp.IsValid()) return;
    Hsr(fp, FunctionId == PEG_FUNC_HSR_LEFT, sItemString, Pt, Area, radar);
}

// HSR click: the field under the mouse decides which of TopSky's functions runs. TopSky's own menus, so its
// tactical coordination (HOP, RTI/TIP, accept/refuse) works from them as from its AHDG/ASP/ARC items.
void KwiqPegasus::Hsr(CFlightPlan& fp, bool left, const char* sItemString, POINT Pt, RECT Area, CRadarScreen* radar) {
    std::string text = Str(sItemString), cs = Str(fp.GetCallsign());
    size_t start = 0, len = 0;
    int field = peg::HsrField(text, Pt.x - Area.left, Area.right - Area.left, &start, &len);
    if (field < 0) return;
    std::string point;
    COLORREF colour = 0;
    int item = 0, function = 0;
    if (field == 0 && PendingPoint(fp, point, colour)) {
        if (!left) return;
        item = kTsItemCopnCopx; function = kTsOpenWaypoint;   // answers the COPN/COPX coordination
    }
    else if (field == 0) { item = kTsItemAhdg; function = left ? kTsOpenAhdg : kTsAckAhdg; }
    else if (field == 1) { item = kTsItemAsp; function = left ? kTsOpenAsp : kTsAckAsp; }
    else { item = kTsItemArc; function = left ? kTsOpenArc : kTsAckArc; }
    kwiq::log::Write("HSR", { { "cs", cs }, { "field", std::to_string(field) }, { "button", left ? "left" : "right" },
                              { "topsky", std::to_string(function) }, { "radar", radar ? "1" : "0" } });
    if (!radar) return;   // StartTagFunction is a radar screen's: none open yet
    std::string value = text.substr(start, len);
    radar->StartTagFunction(cs.c_str(), kTopSky, item, value.c_str(), kTopSky, function, Pt, Area);
}
