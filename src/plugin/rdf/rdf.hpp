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
    // Main thread: closes TrackAudio's WebSocket (joining its thread) and the AFV windows. CKWIQPlugin calls it
    // before it cleans up curl, which that thread uses (#47); the destructor calls it too.
    void Stop();
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
    // For the KWIQ menu's TrackAudio row and .kwiq status (kwiq::TrackAudioStateText)
    kwiq::TrackAudioState TrackAudio() const;
    // Main thread: try to connect to TrackAudio now, leaving EuroScope's voice channels as they are
    // (unlike .rdf reload, which switches them all off first). Nothing if RDF didn't start.
    void Reconnect();

    struct Impl;
private:
    std::unique_ptr<Impl> d_;
};
