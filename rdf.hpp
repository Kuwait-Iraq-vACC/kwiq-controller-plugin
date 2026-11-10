// rdf.hpp - RDF (Radio Direction Finder) and its EuroScope <-> TrackAudio bridge, built into the KWIQ plugin.
//
// Adapted from RDF by Kingfu Chan (https://github.com/KingfuChan/RDF, v1.4.3b, GPL-3.0), see rdf.cpp and NOTICE.md.
// EuroScope takes one plugin per DLL, so RDF is not a CPlugIn here: CKWIQPlugin owns it and calls it, the way
// VCH is built in. Its settings come from Core per callsign pattern (kwiq::RdfRow), not from RDF's own files.
#pragma once
#include <windows.h>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "EuroScopePlugIn.h"
#include "kwiq_logic.hpp"

class KwiqRdf {
public:
    explicit KwiqRdf(EuroScopePlugIn::CPlugIn* es);
    ~KwiqRdf();
    // Main thread. False (with why) if another RDF or afv-euroscope-bridge is loaded in this EuroScope.
    bool Start(std::string& why);
    // Core's settings and TrackAudio's address (rdf.endpoint); the WebSocket reconnects if the address changes
    void SetRows(std::vector<kwiq::RdfRow> rows);
    void SetEndpoint(const std::string& hostPort);
    // Main thread, each second: picks the settings for the logon callsign and passes on messages to show
    void Tick(std::time_t now, const std::string& callsign, bool loggedOn,
              const std::function<void(const std::string&, bool)>& msg);
    // The controller's own choice for drawing: -1 = as Core says, 0 = off, 1 = on
    void SetLocalDraw(int v);
    int LocalDraw() const;
    bool DrawOn() const;
    // Radar screen, REFRESH_PHASE_AFTER_TAGS
    void Draw(EuroScopePlugIn::CRadarScreen* screen, HDC hDC);
    // "!" in the RDF state tag item while the aircraft transmits (and after, while middle-click shows the last one)
    bool Transmitting(const std::string& callsign);
    // .RDF REFRESH and .RDF RELOAD (upper case); true if handled
    bool Command(const std::string& cmd);
    // For the KWIQ menu: "TrackAudio", "AFV" or "Not connected"
    std::string BridgeStatus() const;

    struct Impl;
private:
    std::unique_ptr<Impl> d_;
};
