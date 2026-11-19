// updater.cpp - KWIQPluginUpdater.dll, kept in %LOCALAPPDATA%\KWIQ\bin.
//
// As in the UK Controller Plugin, the loader (KWIQPlugin.dll) runs this before it loads the plugin. It asks
// Core for the latest version, and keeps KWIQPluginCore.dll and itself up to date:
//   * No plugin installed yet: it is installed without asking (the loader asked before the first download).
//   * A newer version: the controller is asked. No asks again, as an update is required; a second No starts
//     the plugin reduced (only the outdated sector file alert, the EuroScope version check and a reminder).
//   * After an update, the controller is offered the changelog.
//   * Core's kill switch (plugin.enabled = 0) as EuroScope starts: the plugin is not loaded.
// Every file is downloaded from Core and checked against the SHA-256 Core publishes.
// The message boxes are UKCP's (PerformUpdates.cpp): same titles, buttons, icons and wording, shown while
// EuroScope loads the plugin (kwiq_win::Box).

#include "kwiq_win.hpp"
#include <shellapi.h>
#include "version.h"

using namespace kwiq_win;

typedef const char* (*CoreVersionFn)();

static std::string SelfPath() {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&SelfPath, &m);
    char b[MAX_PATH] = {};
    return m && GetModuleFileNameA(m, b, MAX_PATH) ? std::string(b) : std::string();
}

// The installed plugin's version, from the file itself; "" if there is none, "0" if it cannot be read
static std::string InstalledVersion(const std::string& core) {
    if (!Exists(core)) return "";
    HMODULE m = LoadLibraryA(core.c_str());
    if (!m) return "0";
    CoreVersionFn v = (CoreVersionFn)GetProcAddress(m, "KWIQCoreVersion");
    std::string out = v ? v() : "0";
    FreeLibrary(m);
    return out;
}

// This file, when Core publishes a different one. Quiet: if it fails, the next start tries again.
static void UpdateSelf(const std::string& updaterSha, const std::string& version) {
    std::string self = SelfPath(), upd;
    if (self.empty() || updaterSha.size() != 64 || kwiq::Sha256Hex(ReadSmallFile(self, 64u * 1024 * 1024)) == updaterSha) return;
    if (DownloadDll(L"updater", updaterSha, version, upd).empty()) ReplaceDll(self, upd);
}

// The plugin, then this file. Returns why it failed, or "".
static std::string Install(const std::string& core, const std::string& coreSha, const std::string& updaterSha, const std::string& version) {
    std::string dll, err = DownloadDll(L"core", coreSha, version, dll);
    if (!err.empty()) return err;
    if (!ReplaceDll(core, dll)) return "The plugin file could not be replaced (" + core + "). Is EuroScope open twice?";
    UpdateSelf(updaterSha, version);
    return "";
}

// UKCP's DisplayPreUpdateConsentNotification: asked twice, as an update is required
static bool AskToUpdate(const std::string& latest, const std::string& installed) {
    if (Box("KWIQ Automatic Update",
            "A new version of the KWIQ Controller Plugin, version " + latest + ", is available. You have version " +
            installed + ".\r\n\r\nWould you like to update now?", MB_YESNO | MB_ICONINFORMATION) == IDYES)
        return true;
    return Box("KWIQ Automatic Update",
               "In order to use the KWIQ Controller Plugin, you must update to the latest version.\r\n\r\n"
               "This is because old or faulty versions of the plugin can affect other controllers and pilots on the "
               "network.\r\n\r\nSelect Yes to update now, or No to start with only the outdated sector file alert and "
               "the EuroScope version check.", MB_YESNO | MB_ICONEXCLAMATION) == IDYES;
}

// As UKCP reports a failed update
static void UpdateFailed(const std::string& message) {
    Box("KWIQ Automatic Update Failed", message, MB_OK | MB_ICONSTOP);
}

// Returns a kwiq::StartMode for the loader
static int UpdaterRun() {
    using kwiq::StartMode;
    std::string core = CorePath();
    if (core.empty()) return (int)StartMode::DontLoad;
    std::string installed = InstalledVersion(core), body;
    int status = CoreGet(L"/latest", installed.empty() ? "0" : installed, body, 64 * 1024, 5000);
    bool answered = status == 200;
    kwiq::PluginConfig cfg = answered ? kwiq::ParseConfig(body) : kwiq::PluginConfig();
    bool killSwitch = answered && !cfg.GetBool("plugin.enabled", true);
    if (killSwitch) return (int)StartMode::DontLoad;

    std::string latest = kwiq::Trim(cfg.Get("plugin.latest_version"));
    std::string coreSha = kwiq::Lower(kwiq::Trim(cfg.Get("plugin.core_sha256")));
    std::string updaterSha = kwiq::Lower(kwiq::Trim(cfg.Get("plugin.updater_sha256")));
    std::string changelog = kwiq::Trim(cfg.Get("plugin.changelog_url", kChangelogUrl));
    if (!kwiq::IsWebLink(changelog)) changelog = kChangelogUrl;     // only a web page is ever opened (#44)

    bool declined = false, updated = false;
    if (answered && kwiq::UpdateAvailable(installed.empty() ? "0" : installed, latest, coreSha)) {
        if (installed.empty() || AskToUpdate(latest, installed)) {
            std::string err = Install(core, coreSha, updaterSha, installed.empty() ? "0" : installed);
            if (err.empty()) updated = true;
            else if (installed.empty()) UpdateFailed("The KWIQ Controller Plugin could not be installed.\r\n\r\n" + err);
            else UpdateFailed(err + "\r\n\r\nThe plugin will load with the version already installed, " + installed + ".");
        } else declined = true;
    } else if (answered && !installed.empty()) {
        UpdateSelf(updaterSha, installed);                       // the plugin is current; keep this file in step
    } else if (installed.empty()) {
        UpdateFailed(answered ? "Core has no version of the KWIQ Controller Plugin to install yet."
                              : "The KWIQ Controller Plugin could not be installed, because Core could not be reached."
                                "\r\n\r\nIt will try again the next time EuroScope starts.");
    }

    // UKCP's DisplayPostUpdateNotification
    if (updated && !installed.empty() &&
        Box("KWIQ Automatic Update Complete",
            "The KWIQ Controller Plugin has been updated to version " + latest + ".\r\n\r\n"
            "Would you like to view the changelog now?", MB_YESNO | MB_ICONINFORMATION) == IDYES)
        ShellExecuteA(nullptr, "open", changelog.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    return (int)kwiq::DecideStart(answered, killSwitch, Exists(core), declined);
}
// Called by the loader while EuroScope loads it, so nothing may escape: if anything throws, the plugin that is
// installed starts as it is (the next start tries the update again)
extern "C" __declspec(dllexport) int KWIQUpdaterRun() {
    try {
        return UpdaterRun();
    } catch (...) {
        try { std::string core = CorePath(); return (int)(!core.empty() && Exists(core) ? kwiq::StartMode::Full : kwiq::StartMode::DontLoad); }
        catch (...) { return (int)kwiq::StartMode::DontLoad; }
    }
}

// The updater's own version, for support
extern "C" __declspec(dllexport) const char* KWIQUpdaterVersion() { return KWIQ_PLUGIN_VERSION; }
