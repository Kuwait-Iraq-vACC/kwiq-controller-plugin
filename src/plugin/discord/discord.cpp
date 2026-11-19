// discord.cpp - DiscordEuroScope: Discord Rich Presence for EuroScope, built into the KWIQ plugin.
//
// Adapted from DiscordEuroScope by Kirollos Nashaat, https://github.com/Kirollos/DiscordEuroScope (1.3.0-rc1,
// commit 399a951), GPL-3.0: DiscordEuroscopeExt.cpp's presence update, the states and their order
// (supervisor, observer, controller), the placeholders, the radio callsign lookup and the aircraft counts.
// Changes, see also NOTICE.md:
//   * Not a CPlugIn of its own: CKWIQPlugin drives it from its timer.
//   * Everything in DiscordEuroscope_config.json comes from Core. Messages, images and buttons are lists with
//     filters (state, FIR, callsign pattern, event); one message is picked at random at each logon, and the
//     elapsed time starts again then. Radio callsigns come from Core instead of the ESE file.
//   * New placeholder {event}: the Core event on now.
//   * Discord_Initialize does not register a URL handler in the registry (autoRegister = 0).
//   * Nothing is shown or logged when Discord isn't running or installed.

#include "discord.hpp"
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <random>
#include <set>
#include "discord_rpc.h"

struct KwiqDiscord::Impl {
    EuroScopePlugIn::CPlugIn* es;
    explicit Impl(EuroScopePlugIn::CPlugIn* p) : es(p) {}

    std::mutex mu;                                  // guards cfg, coreOn (set from the worker thread)
    kwiq::DiscordConfig cfg;
    std::string body;                               // Core's last answer
    bool coreOn = false, cfgChanged = false;
    bool localOn = true;                            // main thread

    // main thread
    bool initialised = false;
    std::string appId, session;                     // session: state + callsign; a change picks again
    std::time_t startedAt = 0;
    unsigned roll = 0;
    std::set<std::string> tracked;                  // aircraft tracked this logon ({total_tracked})
    std::string lastSent;
    std::time_t lastSentAt = 0;
    std::mt19937 rng{ std::random_device{}() };

    void Stop() {
        if (!initialised) return;
        Discord_ClearPresence();
        Discord_RunCallbacks();
        Discord_Shutdown();
        initialised = false;
        lastSent.clear();
    }
};

KwiqDiscord::KwiqDiscord(EuroScopePlugIn::CPlugIn* es) : d_(new Impl(es)) {}
KwiqDiscord::~KwiqDiscord() { d_->Stop(); }

void KwiqDiscord::Configure(const std::string& body, bool coreEnabled) {
    std::lock_guard<std::mutex> g(d_->mu);
    d_->coreOn = coreEnabled;
    if (body == d_->body) return;
    d_->body = body;
    d_->cfg = kwiq::ParseDiscord(body);
    d_->cfgChanged = true;
}
void KwiqDiscord::SetLocal(bool on) { d_->localOn = on; }
bool KwiqDiscord::Local() const { return d_->localOn; }
bool KwiqDiscord::Active() const { return d_->initialised; }

void KwiqDiscord::Tick(std::time_t now, const std::string& eventName) {
    Impl& d = *d_;
    kwiq::DiscordConfig cfg;
    bool coreOn, changed;
    {
        std::lock_guard<std::mutex> g(d.mu);
        cfg = d.cfg; coreOn = d.coreOn; changed = d.cfgChanged; d.cfgChanged = false;
    }
    if (!coreOn || !d.localOn || cfg.appId.empty()) { d.Stop(); return; }
    if (d.initialised && cfg.appId != d.appId) d.Stop();
    if (!d.initialised) {
        DiscordEventHandlers handlers = {};
        Discord_Initialize(cfg.appId.c_str(), &handlers, 0, nullptr);
        d.initialised = true;
        d.appId = cfg.appId;
        d.session.clear();
    }

    // The state, as DiscordEuroScope works it out
    using namespace EuroScopePlugIn;
    int ct = d.es->GetConnectionType();
    std::string state;
    if (ct == CONNECTION_TYPE_NO) state = "idle";
    else if (ct == CONNECTION_TYPE_PLAYBACK) state = "playback";
    else if (ct == CONNECTION_TYPE_SWEATBOX && !cfg.sweatboxBypass) state = "sweatbox";
    else if (ct == CONNECTION_TYPE_DIRECT || ct == CONNECTION_TYPE_VIA_PROXY || ct == CONNECTION_TYPE_SWEATBOX) {
        CController me = d.es->ControllerMyself();
        state = me.IsValid() && me.GetRating() >= 11 ? "supervisor" : me.IsValid() && !me.IsController() ? "observer" : "controller";
    } else { d.Stop(); return; }

    CController me = d.es->ControllerMyself();
    std::string callsign = state == "idle" || !me.IsValid() || !me.GetCallsign() ? "" : me.GetCallsign();
    char freq[16] = {};
    if (me.IsValid() && me.GetPrimaryFrequency() > 100.0) std::snprintf(freq, sizeof freq, "%.3f", me.GetPrimaryFrequency());

    // A new logon, position or state: pick again, and start the elapsed time again
    std::string session = state + "|" + callsign;
    if (session != d.session || changed) {
        if (session != d.session) { d.startedAt = now; d.tracked.clear(); }
        d.session = session;
        d.roll = d.rng();
    }

    // Aircraft in range and tracked (DiscordEuroScope's CountACinRange / CountTrackedAC)
    int inRange = 0, trackedNow = 0;
    for (CRadarTarget ac = d.es->RadarTargetSelectFirst(); ac.IsValid(); ac = d.es->RadarTargetSelectNext(ac)) {
        inRange++;
        if (ac.GetCorrelatedFlightPlan().IsValid() && ac.GetCorrelatedFlightPlan().GetTrackingControllerIsMe()) {
            trackedNow++;
            if (ac.GetCallsign()) d.tracked.insert(ac.GetCallsign());
        }
    }

    std::map<std::string, std::string> values = {
        { "callsign", callsign },
        { "rcallsign", kwiq::DiscordRadioCallsign(cfg, callsign, freq) },
        { "frequency", freq },
        { "current_tracked", std::to_string(trackedNow) },
        { "total_tracked", std::to_string(d.tracked.size()) },
        { "total_inrange", std::to_string(inRange) },
        { "event", eventName },
    };
    kwiq::DiscordContext x;
    x.state = state; x.callsign = callsign; x.event = !eventName.empty();
    x.fir = kwiq::SectorPackage("", callsign);

    auto text = [&](const std::vector<kwiq::DiscordEntry>& list, int pick, size_t i) {
        std::string out;
        if (pick >= 0 && i < list[pick].v.size()) kwiq::DiscordFormat(list[pick].v[i], values, out);
        return kwiq::Utf8Truncate(out, 127);            // Discord's limit is 128 bytes; never half a character (#50)
    };
    int m = kwiq::DiscordPick(cfg.messages, x, values, d.roll);
    int img = kwiq::DiscordPick(cfg.images, x, values, d.roll);
    std::string details = text(cfg.messages, m, 0), stateText = text(cfg.messages, m, 1);
    std::string largeKey = text(cfg.images, img, 0), largeText = text(cfg.images, img, 1);
    std::string smallKey = text(cfg.images, img, 2), smallText = text(cfg.images, img, 3);

    // Buttons: the matching ones with the most filters set first, in Core's order; at most two
    std::vector<std::pair<int, size_t>> order;
    for (size_t i = 0; i < cfg.buttons.size(); i++) {
        int s = kwiq::DiscordMatch(cfg.buttons[i].f, x);
        if (s >= 0) order.push_back({ -s, i });
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string labels[2], urls[2];
    DiscordButton buttons[2] = { { nullptr, nullptr }, { nullptr, nullptr } };
    int nb = 0;
    for (auto& o : order) {
        if (nb == 2) break;
        std::string l, u;
        if (!kwiq::DiscordFormat(cfg.buttons[o.second].v[0], values, l) || !kwiq::DiscordFormat(cfg.buttons[o.second].v[1], values, u)) continue;
        if (u.size() > kwiq::kDiscordUrlMax || !kwiq::IsWebLink(u)) continue;   // Discord would refuse the presence (#50)
        labels[nb] = kwiq::Utf8Truncate(l, 31); urls[nb] = u;
        nb++;
    }
    for (int i = 0; i < nb; i++) buttons[i] = { labels[i].c_str(), urls[i].c_str() };
    if (nb == 1) buttons[1] = { "", "" };            // as DiscordEuroScope: never a single button in slot 2

    // Send only when something changed (or every 15 s), and let discord-rpc do its work every second
    std::string key = details + "\x1f" + stateText + "\x1f" + largeKey + "\x1f" + largeText + "\x1f" + smallKey + "\x1f" +
                      smallText + "\x1f" + labels[0] + urls[0] + labels[1] + urls[1] + "\x1f" + std::to_string(d.startedAt);
    if (key != d.lastSent || now - d.lastSentAt >= 15) {
        DiscordRichPresence p = {};
        p.details = details.c_str();
        p.state = stateText.c_str();
        p.largeImageKey = largeKey.c_str();
        p.largeImageText = largeText.c_str();
        p.smallImageKey = smallKey.c_str();
        p.smallImageText = smallText.c_str();
        p.startTimestamp = d.startedAt;
        if (nb) p.buttons = buttons;
        Discord_UpdatePresence(&p);
        d.lastSent = key;
        d.lastSentAt = now;
    }
    Discord_RunCallbacks();
}
