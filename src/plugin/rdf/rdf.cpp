// rdf.cpp - RDF (Radio Direction Finder) and the EuroScope <-> TrackAudio bridge, built into the KWIQ plugin.
//
// Adapted from RDF by Kingfu Chan, https://github.com/KingfuChan/RDF (v1.4.3b, commit a4bd0ae), GPL-3.0:
// CRDFPlugin.cpp, CRDFScreen.cpp, RDFCommon.cpp and HiddenWindow.cpp. Changes, see also NOTICE.md:
//   * Not a CPlugIn of its own: CKWIQPlugin drives it, and CKWIQRadar draws it.
//   * Settings come from Core per callsign pattern (kwiq::RdfRow). RDF's settings file, RDFStyle.json, per-ASR
//     settings and their .RDF commands are gone; .RDF REFRESH and .RDF RELOAD stay, and .RDF DRAW ON|OFF is
//     the controller's own switch (handled in KWIQPlugin.cpp).
//   * No plog, MFC or cpp-httplib. TrackAudio's version isn't read or checked: the WebSocket is all it uses.
//   * Messages are passed to the main thread instead of being shown from the WebSocket's thread.
//   * Refuses to start next to another RDF or afv-euroscope-bridge in the same EuroScope.
// The drawing, the random offsets, the channel matching and the AFV hidden windows are RDF's, unchanged.

#include "rdf.hpp"
#include <deque>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace {

constexpr auto TRACKAUDIO_PARAM_WS = "/ws";
constexpr int TRACKAUDIO_TIMEOUT_SEC = 1;
constexpr int TRACKAUDIO_HEARTBEAT_SEC = 30;
constexpr int FREQUENCY_REDUNDANT = 199999;     // kHz
constexpr double kPi = 3.141592653589793;
constexpr double EarthRadius = 3438.0;          // nautical miles, as EuroScope's internal CEuroScopeCoord
constexpr const char* kRdfClass = "RDFHiddenWindowClass";
constexpr const char* kAfvClass = "AfvBridgeHiddenWindowClass";

inline double Rad(double deg) { return deg * kPi / 180.0; }
inline double Deg(double rad) { return rad / kPi * 180.0; }
inline int FrequencyFromMHz(double f) { return (int)std::round(f * 1000.0); }
inline int FrequencyFromHz(double f) { return (int)std::round(f / 1000.0); }
inline bool FrequencyIsSame(int a, int b) { return std::abs(a - b) <= 2; }     // kHz

// From EuroScope's internal void CEuroScopeCoord::Move(double heading, double distance)
void AddOffset(EuroScopePlugIn::CPosition& position, double heading, double distance) {
    if (distance < 0.000001) return;
    double lat = position.m_Latitude, lon = position.m_Longitude;
    double dr = distance / EarthRadius, c = std::cos(dr), s = std::sin(dr);
    double fi2 = std::asin(std::sin(Rad(lat)) * c + std::cos(Rad(lat)) * s * std::cos(Rad(heading)));
    double lambda2 = Rad(lon) + std::atan2(std::sin(Rad(heading)) * s * std::cos(Rad(lat)), c - std::sin(Rad(lat)) * std::sin(fi2));
    position.m_Latitude = Deg(fi2);
    position.m_Longitude = Deg(lambda2);
}

struct DrawPosition {
    EuroScopePlugIn::CPosition position;
    double radius = 0;                          // 0 = don't draw
};
using CallsignPosition = std::map<std::string, DrawPosition>;

struct ChannelState {
    bool isPrim = false, isAtis = false, rx = false, tx = false;
    int frequency = FREQUENCY_REDUNDANT;
    ChannelState() = default;
    explicit ChannelState(EuroScopePlugIn::CGrountToAirChannel ch)
        : isPrim(ch.GetIsPrimary()), isAtis(ch.GetIsAtis()), rx(ch.GetIsTextReceiveOn()), tx(ch.GetIsTextTransmitOn()),
          frequency(FrequencyFromMHz(ch.GetFrequency())) {}
};

KwiqRdf::Impl* gRdf = nullptr;                  // for the hidden windows

LRESULT CALLBACK HiddenWindowRDF(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK HiddenWindowAFV(HWND, UINT, WPARAM, LPARAM);

// Another RDF or afv-euroscope-bridge in this EuroScope already has these windows
BOOL CALLBACK FindOwnHidden(HWND h, LPARAM l) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    char cls[64] = {};
    GetClassNameA(h, cls, sizeof cls);
    if (pid == GetCurrentProcessId() && (strcmp(cls, kRdfClass) == 0 || strcmp(cls, kAfvClass) == 0)) { *(bool*)l = true; return FALSE; }
    return TRUE;
}

} // namespace

struct KwiqRdf::Impl {
    EuroScopePlugIn::CPlugIn* es;
    explicit Impl(EuroScopePlugIn::CPlugIn* p) : es(p) {}

    // settings in effect (Core's for the logon callsign) and the controller's own drawing switch
    mutable std::shared_mutex mtxSettings;
    kwiq::RdfSettings cur;
    std::vector<kwiq::RdfRow> rows;
    std::string forCallsign = "\x01";           // the callsign cur was picked for
    std::atomic<int> localDraw{ -1 };

    // transmissions being drawn, and the frequencies the controller transmits on
    std::shared_mutex mtxTransmission;
    CallsignPosition curTransmission, preTransmission;
    std::shared_mutex mtxTxFrequencies;
    std::set<int> curTxFrequencies;             // kHz

    // TrackAudio WebSocket and the AFV standalone client's hidden windows
    std::mutex mtxEndpoint;
    std::string addressTrackAudio = "127.0.0.1:49080";
    ix::WebSocket socketTrackAudio;
    bool netInit = false, started = false;
    HWND hiddenWindowRDF = nullptr, hiddenWindowAFV = nullptr;
    bool classRDF = false, classAFV = false;
    std::atomic<bool> wsOpen{ false }, endpointChanged{ false };
    std::atomic<std::time_t> afvSeenAt{ 0 };
    std::mutex mtxInfo;
    std::vector<std::pair<std::string, bool>> pending;   // messages for the main thread
    // TrackAudio's messages, parsed on the WebSocket's thread and handled on EuroScope's (Pump): the EuroScope
    // API isn't thread-safe (#80). At most QUEUE_MAX waiting; the oldest go first.
    static constexpr size_t QUEUE_MAX = 1000;
    std::mutex mtxQueue;
    std::deque<std::pair<std::string, nlohmann::json>> queue;

    // main thread only
    bool wasLoggedOn = false, toldMissing = false;
    std::time_t loggedOnAt = 0;

    void Say(const std::string& m, bool err) { std::lock_guard<std::mutex> g(mtxInfo); pending.emplace_back(m, err); }
    bool DrawEnabled() const {
        int l = localDraw;
        if (l >= 0) return l == 1;
        std::shared_lock<std::shared_mutex> g(mtxSettings);
        return cur.draw;
    }
    bool BridgeEnabled() const { std::shared_lock<std::shared_mutex> g(mtxSettings); return cur.bridge; }
    kwiq::RdfSettings Settings() const { std::shared_lock<std::shared_mutex> g(mtxSettings); return cur; }

    // ---- TrackAudio connection (RDF's LoadTrackAudioSettings) ----------------------------------
    void Connect() {
        socketTrackAudio.stop();
        wsOpen = false;
        { std::unique_lock<std::shared_mutex> t(mtxTransmission); curTransmission.clear(); preTransmission.clear(); }
        std::string addr;
        { std::lock_guard<std::mutex> g(mtxEndpoint); addr = addressTrackAudio; }
        socketTrackAudio.setUrl("ws://" + addr + TRACKAUDIO_PARAM_WS);
        UpdateChannel(nullptr, nullptr);
        socketTrackAudio.start();
    }

    // ---- AFV standalone client (RDF's HiddenWndProcess*Message) ----------------------------------
    void ProcessRDFMessage(const std::string& message) {
        afvSeenAt = std::time(nullptr);
        std::unique_lock<std::shared_mutex> t(mtxTransmission);
        if (message.empty()) { curTransmission.clear(); return; }
        std::vector<std::string> callsigns;
        std::istringstream f(message);
        std::string s;
        while (std::getline(f, s, ':')) callsigns.push_back(s);
        // skip existing callsigns and clear redundant
        for (auto it = curTransmission.begin(); it != curTransmission.end();) {
            auto found = std::find(callsigns.begin(), callsigns.end(), it->first);
            if (found == callsigns.end()) it = curTransmission.erase(it);
            else { callsigns.erase(found); ++it; }
        }
        for (const auto& cs : callsigns) {
            DrawPosition dp = GenerateDrawPosition(cs);
            if (dp.radius > 0) curTransmission[cs] = dp;
        }
        preTransmission = curTransmission;
    }
    void ProcessAFVMessage(const std::string& message) {
        afvSeenAt = std::time(nullptr);
        if (!BridgeEnabled() || message.empty()) return;
        // format: xxx.xxx:True:False
        std::vector<std::string> parts;
        std::istringstream f(message);
        std::string s;
        while (std::getline(f, s, ':')) parts.push_back(s);
        if (parts.size() != 3) return;              // incomplete message
        ChannelState state;
        try { state.frequency = FrequencyFromMHz(std::stod(parts[0])); } catch (...) { return; }
        state.rx = parts[1] == "True";
        state.tx = parts[2] == "True";
        UpdateChannel(nullptr, &state);
    }

    // ---- drawing positions (RDF's GenerateDrawPosition) -------------------------------------------
    DrawPosition GenerateDrawPosition(const std::string& callsign) {
        try {
            static std::random_device randomDevice;
            static std::mt19937 rdGenerator(randomDevice());
            static std::uniform_real_distribution<> disBearing(0.0, 360.0);
            static std::normal_distribution<> disDistance(0, 1.0);

            auto radarTarget = es->RadarTargetSelect(callsign.c_str());
            auto controller = es->ControllerSelect(callsign.c_str());
            if (!radarTarget.IsValid() && controller.IsValid() && !callsign.empty() && callsign.back() >= 'A' && callsign.back() <= 'Z')
                radarTarget = es->RadarTargetSelect(callsign.substr(0, callsign.size() - 1).c_str());   // dump last character
            kwiq::RdfSettings p = Settings();
            if (!DrawEnabled()) return {};
            if (radarTarget.IsValid()) {
                int alt = radarTarget.GetPosition().GetPressureAltitude();
                if (alt >= p.lowAltitude) {
                    EuroScopePlugIn::CPosition pos = radarTarget.GetPosition().GetPosition();
                    double radius = p.radius, offset = p.precision;
                    if (p.threshold >= 0 && (p.lowPrecision > 0 || p.precision > 0)) {
                        if (p.highPrecision > 0 && p.highAltitude > p.lowAltitude)
                            offset = (double)p.lowPrecision + (double)(alt - p.lowAltitude) * (double)(p.highPrecision - p.lowPrecision) /
                                     (double)(p.highAltitude - p.lowAltitude);
                        else offset = p.lowPrecision > 0 ? p.lowPrecision : p.precision;
                        radius = offset;
                    }
                    if (offset > 0) AddOffset(pos, disBearing(rdGenerator), std::abs(disDistance(rdGenerator)) / 3.0 * offset);
                    DrawPosition dp; dp.position = pos; dp.radius = radius;
                    return dp;
                }
            } else if (p.controllers && controller.IsValid()) {
                DrawPosition dp; dp.position = controller.GetPosition(); dp.radius = p.radius;
                return dp;
            }
        } catch (...) {}
        return {};
    }

    // ---- TrackAudio messages (RDF's TrackAudio*Handler) ---------------------------------------
    // These call the EuroScope API (ControllerMyself, RadarTargetSelect, the voice channels), so they run on
    // EuroScope's thread, from Pump (#80); upstream RDF runs them on the WebSocket's thread. The WebSocket's thread
    // only parses and queues (MessageHandler). Locks are always taken in the order mtxTransmission,
    // mtxTxFrequencies, mtxSettings.
    void TransmissionHandler(const nlohmann::json& data, bool rxEnd) {
        std::unique_lock<std::shared_mutex> t(mtxTransmission);
        std::string callsign = data.at("callsign");
        int frequency = FrequencyFromHz(data.at("pFrequencyHz"));
        auto it = curTransmission.find(callsign);
        if (it != curTransmission.end()) {
            if (rxEnd) curTransmission.erase(it);
        } else if (!rxEnd) {
            std::shared_lock<std::shared_mutex> f(mtxTxFrequencies);
            if (!es->ControllerMyself().IsController() || !Settings().requireTx || curTxFrequencies.count(frequency)) {
                DrawPosition dp = GenerateDrawPosition(callsign);
                if (dp.radius > 0) curTransmission[callsign] = dp;
            }
        }
        if (!curTransmission.empty()) preTransmission = curTransmission;
    }
    void StationStatesHandler(const nlohmann::json& data) {
        if (!BridgeEnabled()) return;
        for (auto& station : data.at("stations"))
            if (station.at("type") == "kStationStateUpdate") StationStateUpdateHandler(station.at("value"));
    }
    void StationStateUpdateHandler(const nlohmann::json& data) {
        // only on a direct connection: several EuroScopes would fight over the same channels
        if (!BridgeEnabled() || es->GetConnectionType() != EuroScopePlugIn::CONNECTION_TYPE_DIRECT) return;
        std::string callsign = data.value("callsign", "");
        ChannelState state;
        state.frequency = FrequencyFromHz(data.value("frequency", (double)FREQUENCY_REDUNDANT * 1000.0));
        state.rx = data.value("rx", false);
        state.tx = data.value("tx", false);
        UpdateChannel(callsign.empty() ? nullptr : &callsign, &state);
    }
    void MessageHandler(const ix::WebSocketMessagePtr& msg) {
        try {
            if (msg->type == ix::WebSocketMessageType::Message) {
                auto data = nlohmann::json::parse(msg->str);
                std::string type = data["type"];
                if (type != "kRxBegin" && type != "kRxEnd" && type != "kStationStateUpdate" && type != "kStationStates") return;
                std::lock_guard<std::mutex> g(mtxQueue);
                if (queue.size() >= QUEUE_MAX) queue.pop_front();
                queue.emplace_back(std::move(type), std::move(data["value"]));
            } else if (msg->type == ix::WebSocketMessageType::Open) {
                wsOpen = true;
                std::string addr;
                { std::lock_guard<std::mutex> g(mtxEndpoint); addr = addressTrackAudio; }
                Say("RDF connected to TrackAudio on " + addr + ".", false);
            } else if (msg->type == ix::WebSocketMessageType::Close) {
                bool was = wsOpen.exchange(false);
                if (was) Say("RDF: TrackAudio disconnected.", true);
            }
        } catch (...) {}
    }

    // Main thread: handle what TrackAudio sent since the last time
    void Pump() {
        std::deque<std::pair<std::string, nlohmann::json>> todo;
        { std::lock_guard<std::mutex> g(mtxQueue); todo.swap(queue); }
        for (const auto& m : todo) {
            try {
                if (m.first == "kRxBegin") TransmissionHandler(m.second, false);
                else if (m.first == "kRxEnd") TransmissionHandler(m.second, true);
                else if (m.first == "kStationStateUpdate") StationStateUpdateHandler(m.second);
                else if (m.first == "kStationStates") StationStatesHandler(m.second);
            } catch (...) {}                            // one bad message never stops the rest
        }
    }

    // ---- EuroScope voice channels (RDF's SelectGroundToAirChannel / UpdateChannel / ToggleChannel) ----
    EuroScopePlugIn::CGrountToAirChannel SelectChannel(const std::string* callsign, const int* frequency) {
        auto nameHas = [](const char* name, const std::string& cs) { return name && std::string(name).find(cs) != std::string::npos; };
        if (callsign && frequency) {                // precise match
            for (auto ch = es->GroundToArChannelSelectFirst(); ch.IsValid(); ch = es->GroundToArChannelSelectNext(ch))
                if (nameHas(ch.GetName(), *callsign) && FrequencyIsSame(FrequencyFromMHz(ch.GetFrequency()), *frequency)) return ch;
        } else if (callsign) {                      // callsign only
            for (auto ch = es->GroundToArChannelSelectFirst(); ch.IsValid(); ch = es->GroundToArChannelSelectNext(ch))
                if (nameHas(ch.GetName(), *callsign)) return ch;
        }
        if (frequency) {                            // the matching frequency nearest the primary
            std::map<std::string, ChannelState> all;
            for (auto ch = es->GroundToArChannelSelectFirst(); ch.IsValid(); ch = es->GroundToArChannelSelectNext(ch))
                all[ch.GetName() ? ch.GetName() : ""] = ChannelState(ch);
            auto prim = std::find_if(all.begin(), all.end(), [](const auto& c) { return c.second.isPrim; });
            if (prim != all.end()) {
                std::map<std::string, long> dist;
                long primAt = (long)std::distance(all.begin(), prim);
                for (auto it = all.begin(); it != all.end(); ++it)
                    if (FrequencyIsSame(it->second.frequency, *frequency)) dist[it->first] = std::labs(primAt - (long)std::distance(all.begin(), it));
                auto best = std::min_element(dist.begin(), dist.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
                if (best != dist.end())
                    for (auto ch = es->GroundToArChannelSelectFirst(); ch.IsValid(); ch = es->GroundToArChannelSelectNext(ch))
                        if (ch.GetName() && best->first == ch.GetName() && FrequencyIsSame(FrequencyFromMHz(ch.GetFrequency()), *frequency)) return ch;
            } else {                                // no primary: the first frequency match
                for (auto ch = es->GroundToArChannelSelectFirst(); ch.IsValid(); ch = es->GroundToArChannelSelectNext(ch))
                    if (FrequencyIsSame(FrequencyFromMHz(ch.GetFrequency()), *frequency)) return ch;
            }
        }
        return EuroScopePlugIn::CGrountToAirChannel();
    }
    void UpdateChannel(const std::string* callsign, const ChannelState* state) {
        // EuroScope allows duplicate channel names, but each name and frequency pair is unique
        if (state) {
            auto ch = SelectChannel(callsign, &state->frequency);
            if (ch.IsValid()) Toggle(ch, state->rx, state->tx);
            std::unique_lock<std::shared_mutex> f(mtxTxFrequencies);
            if (state->tx) curTxFrequencies.insert(state->frequency);
            else curTxFrequencies.erase(state->frequency);
        } else {                                    // no channel given: switch them all off
            for (auto ch = es->GroundToArChannelSelectFirst(); ch.IsValid(); ch = es->GroundToArChannelSelectNext(ch)) Toggle(ch, false, false);
            std::unique_lock<std::shared_mutex> f(mtxTxFrequencies);
            curTxFrequencies.clear();
        }
    }
    static void Toggle(EuroScopePlugIn::CGrountToAirChannel ch, bool rx, bool tx) {
        if (!ch.IsValid() || ch.GetIsAtis() || ch.GetIsPrimary()) return;
        if (rx != ch.GetIsTextReceiveOn()) ch.ToggleTextReceive();
        if (tx != ch.GetIsTextTransmitOn()) ch.ToggleTextTransmit();
    }

    CallsignPosition DrawStations() {
        std::shared_lock<std::shared_mutex> t(mtxTransmission);
        return curTransmission.empty() && GetAsyncKeyState(VK_MBUTTON) ? preTransmission : curTransmission;
    }
};

namespace {
LRESULT CALLBACK HiddenWindowRDF(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COPYDATA) {
        auto* data = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (data && data->dwData == 666 && data->lpData && gRdf)
            gRdf->ProcessRDFMessage(std::string(reinterpret_cast<const char*>(data->lpData), strnlen(reinterpret_cast<const char*>(data->lpData), data->cbData)));
        return TRUE;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
LRESULT CALLBACK HiddenWindowAFV(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COPYDATA) {
        auto* data = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (data && data->dwData == 666 && data->lpData && gRdf)
            gRdf->ProcessAFVMessage(std::string(reinterpret_cast<const char*>(data->lpData), strnlen(reinterpret_cast<const char*>(data->lpData), data->cbData)));
        return TRUE;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
} // namespace

KwiqRdf::KwiqRdf(EuroScopePlugIn::CPlugIn* es) : d_(new Impl(es)) {}

KwiqRdf::~KwiqRdf() {
    Stop();
    if (d_->netInit) ix::uninitNetSystem();
}

void KwiqRdf::Stop() {
    if (gRdf == d_.get()) gRdf = nullptr;            // the AFV windows no longer reach it
    if (!d_->started) return;
    d_->started = false;
    d_->socketTrackAudio.stop();                     // returns once the WebSocket's thread has ended
    { std::lock_guard<std::mutex> g(d_->mtxQueue); d_->queue.clear(); }
    if (d_->hiddenWindowRDF) { DestroyWindow(d_->hiddenWindowRDF); d_->hiddenWindowRDF = nullptr; }
    if (d_->hiddenWindowAFV) { DestroyWindow(d_->hiddenWindowAFV); d_->hiddenWindowAFV = nullptr; }
    HINSTANCE exe = GetModuleHandleA(nullptr);
    if (d_->classRDF) { UnregisterClassA(kRdfClass, exe); d_->classRDF = false; }
    if (d_->classAFV) { UnregisterClassA(kAfvClass, exe); d_->classAFV = false; }
}

bool KwiqRdf::Start(std::string& why) {
    bool other = false;
    EnumWindows(FindOwnHidden, (LPARAM)&other);
    if (other) {
        why = "RDF is off, because another RDF plugin or afv-euroscope-bridge is loaded. Remove it in Other Set > Plug-ins: "
              "RDF is part of the KWIQ Controller Plugin.";
        return false;
    }
    gRdf = d_.get();
    HINSTANCE exe = GetModuleHandleA(nullptr);       // as RDF registers them, so the AFV client finds the windows
    WNDCLASSA rdf = {}, afv = {};
    rdf.lpfnWndProc = HiddenWindowRDF; rdf.hInstance = exe; rdf.lpszClassName = kRdfClass;
    afv.lpfnWndProc = HiddenWindowAFV; afv.hInstance = exe; afv.lpszClassName = kAfvClass;
    d_->classRDF = RegisterClassA(&rdf) != 0;
    d_->classAFV = RegisterClassA(&afv) != 0;
    d_->hiddenWindowRDF = d_->classRDF ? CreateWindowA(kRdfClass, "RDFHiddenWindow", 0, 0, 0, 0, 0, nullptr, nullptr, exe, nullptr) : nullptr;
    d_->hiddenWindowAFV = d_->classAFV ? CreateWindowA(kAfvClass, "AfvBridgeHiddenWindow", 0, 0, 0, 0, 0, nullptr, nullptr, exe, nullptr) : nullptr;
    if (!d_->hiddenWindowRDF || !d_->hiddenWindowAFV) d_->Say("RDF could not open its windows for the AFV client; TrackAudio still works.", true);

    ix::initNetSystem();
    d_->netInit = true;
    d_->socketTrackAudio.setHandshakeTimeout(TRACKAUDIO_TIMEOUT_SEC);
    d_->socketTrackAudio.setMaxWaitBetweenReconnectionRetries(TRACKAUDIO_HEARTBEAT_SEC * 2000);   // ms
    d_->socketTrackAudio.setPingInterval(TRACKAUDIO_HEARTBEAT_SEC);
    Impl* impl = d_.get();
    d_->socketTrackAudio.setOnMessageCallback([impl](const ix::WebSocketMessagePtr& m) { impl->MessageHandler(m); });
    d_->started = true;
    d_->Connect();
    return true;
}

void KwiqRdf::SetRows(std::vector<kwiq::RdfRow> rows) {
    std::unique_lock<std::shared_mutex> g(d_->mtxSettings);
    d_->rows = std::move(rows);
    d_->forCallsign = "\x01";                         // picked again on the next tick
}

void KwiqRdf::SetEndpoint(const std::string& hostPort) {
    std::string ep = kwiq::Trim(hostPort);
    if (ep.empty()) ep = "127.0.0.1:49080";
    {
        std::lock_guard<std::mutex> g(d_->mtxEndpoint);
        if (ep == d_->addressTrackAudio) return;
        d_->addressTrackAudio = ep;
    }
    d_->endpointChanged = true;                      // reconnected on the main thread (Tick)
}

void KwiqRdf::Pump() {
    if (d_->started) d_->Pump();
}

void KwiqRdf::Tick(std::time_t now, const std::string& callsign, bool loggedOn,
                   const std::function<void(const std::string&, bool)>& msg) {
    Pump();
    {
        std::unique_lock<std::shared_mutex> g(d_->mtxSettings);
        if (callsign != d_->forCallsign) { d_->forCallsign = callsign; d_->cur = kwiq::RdfFor(d_->rows, callsign); }
    }
    std::vector<std::pair<std::string, bool>> out;
    { std::lock_guard<std::mutex> g(d_->mtxInfo); out.swap(d_->pending); }
    for (auto& m : out) msg(m.first, m.second);
    if (!d_->started) return;
    if (d_->endpointChanged.exchange(false)) d_->Connect();

    // Once a session, 15 seconds after logging on: TrackAudio missing (and no AFV client)
    if (loggedOn && !d_->wasLoggedOn) d_->loggedOnAt = now;
    d_->wasLoggedOn = loggedOn;
    if (!loggedOn || now - d_->loggedOnAt < 15) return;
    bool afv = now - d_->afvSeenAt < 600;
    if (!d_->wsOpen && !afv && !d_->toldMissing) {
        d_->toldMissing = true;
        msg("TrackAudio is not running, so RDF and the TrackAudio bridge are not working. Start TrackAudio (1.3.0 Beta 5 or later).", true);
    }
}

void KwiqRdf::SetLocalDraw(int v) { d_->localDraw = v < 0 ? -1 : (v ? 1 : 0); }
int KwiqRdf::LocalDraw() const { return d_->localDraw; }
bool KwiqRdf::DrawOn() const { return d_->DrawEnabled(); }

void KwiqRdf::Draw(EuroScopePlugIn::CRadarScreen* screen, HDC hDC) {
    if (!d_->started || !d_->DrawEnabled()) return;
    CallsignPosition drawPosition = d_->DrawStations();
    if (drawPosition.empty()) return;
    const kwiq::RdfSettings params = d_->Settings();
    RECT area = screen->GetRadarArea();
    HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    HPEN hPen = CreatePen(PS_SOLID, 1, (COLORREF)(drawPosition.size() > 1 ? params.colourConcurrent : params.colour));
    HGDIOBJ oldPen = SelectObject(hDC, hPen);
    for (auto& cp : drawPosition) {
        POINT p = screen->ConvertCoordFromPositionToPixel(cp.second.position);
        if (p.x >= area.left && p.x <= area.right && p.y >= area.top && p.y <= area.bottom) {
            double drawR = cp.second.radius;
            if (params.threshold >= 0) {            // radius in nautical miles: to pixels at this zoom
                EuroScopePlugIn::CPosition ld, ru;
                screen->GetDisplayArea(&ld, &ru);
                POINT pLD = screen->ConvertCoordFromPositionToPixel(ld), pRU = screen->ConvertCoordFromPositionToPixel(ru);
                double dst = std::sqrt(std::pow(pRU.x - pLD.x, 2) + std::pow(pRU.y - pLD.y, 2));
                drawR = drawR * dst / ld.DistanceTo(ru);
            }
            if (drawR >= (double)params.threshold) {
                if (params.threshold >= 0) {        // a circle of radius nautical miles
                    EuroScopePlugIn::CPosition pl = cp.second.position, pt = pl, pr = pl, pb = pl;
                    AddOffset(pl, 270, cp.second.radius); AddOffset(pt, 0, cp.second.radius);
                    AddOffset(pr, 90, cp.second.radius); AddOffset(pb, 180, cp.second.radius);
                    Ellipse(hDC, screen->ConvertCoordFromPositionToPixel(pl).x, screen->ConvertCoordFromPositionToPixel(pt).y,
                            screen->ConvertCoordFromPositionToPixel(pr).x, screen->ConvertCoordFromPositionToPixel(pb).y);
                } else {                            // a circle of radius pixels
                    int r = (int)std::round(drawR);
                    Ellipse(hDC, p.x - r, p.y - r, p.x + r, p.y + r);
                }
                continue;
            }
        }
        // off screen or too small: a line from the middle of the radar area (upstream RDF leaves out its left and top)
        POINT oldPoint;
        MoveToEx(hDC, area.left + (area.right - area.left) / 2, area.top + (area.bottom - area.top) / 2, &oldPoint);
        LineTo(hDC, p.x, p.y);
        MoveToEx(hDC, oldPoint.x, oldPoint.y, nullptr);
    }
    SelectObject(hDC, oldBrush);
    SelectObject(hDC, oldPen);
    DeleteObject(hPen);
}

bool KwiqRdf::Transmitting(const std::string& callsign) {
    std::shared_lock<std::shared_mutex> t(d_->mtxTransmission);
    return d_->preTransmission.count(callsign) > 0;
}

bool KwiqRdf::Command(const std::string& cmd) {
    if (!d_->started) return false;
    if (cmd == ".RDF RELOAD") { d_->Connect(); return true; }
    if (cmd == ".RDF REFRESH") {
        { std::unique_lock<std::shared_mutex> t(d_->mtxTransmission); d_->curTransmission.clear(); d_->preTransmission.clear(); }
        d_->UpdateChannel(nullptr, nullptr);         // switch all channels off, then ask TrackAudio for its stations
        nlohmann::json j;
        j["type"] = "kGetStationStates";
        d_->socketTrackAudio.send(j.dump());
        return true;
    }
    return false;
}

kwiq::TrackAudioState KwiqRdf::TrackAudio() const {
    bool inUse = d_->BridgeEnabled() || d_->DrawEnabled();
    return kwiq::TrackAudioStatus(d_->started, inUse, d_->wsOpen, std::time(nullptr) - d_->afvSeenAt < 600);
}

void KwiqRdf::Reconnect() {
    if (!d_->started) return;
    d_->socketTrackAudio.stop();
    d_->wsOpen = false;
    std::string addr;
    { std::lock_guard<std::mutex> g(d_->mtxEndpoint); addr = d_->addressTrackAudio; }
    d_->socketTrackAudio.setUrl("ws://" + addr + TRACKAUDIO_PARAM_WS);
    d_->socketTrackAudio.start();
}
