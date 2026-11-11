// discord.hpp - DiscordEuroScope: Discord Rich Presence for EuroScope, built into the KWIQ plugin.
//
// Adapted from DiscordEuroScope by Kirollos Nashaat (https://github.com/Kirollos/DiscordEuroScope, 1.3.0-rc1,
// GPL-3.0), see discord.cpp and NOTICE.md. Uses discord-rpc (Multi Theft Auto's fork, MIT) in libs/discord-rpc.
// Everything DiscordEuroscope_config.json held comes from Core (kwiq::DiscordConfig).
#pragma once
#include <windows.h>
#include <ctime>
#include <memory>
#include <string>
#include "EuroScopePlugIn.h"
#include "kwiq_logic.hpp"

class KwiqDiscord {
public:
    explicit KwiqDiscord(EuroScopePlugIn::CPlugIn* es);
    ~KwiqDiscord();
    // Any thread: Core's /discord answer ("" = none yet), and discord.enabled from /config. The message is only
    // picked again when the answer changes, not on every refresh.
    void Configure(const std::string& body, bool coreEnabled);
    // The controller's own switch (the DiscordEuroScope setting, also set by the Configurator)
    void SetLocal(bool on);
    bool Local() const;
    // Shown on Discord now: Core and the controller both have it on, and Core gave an application ID
    bool Active() const;
    // Main thread, each second. eventName: the Core event on now, or "".
    void Tick(std::time_t now, const std::string& eventName);

    struct Impl;
private:
    std::unique_ptr<Impl> d_;
};
