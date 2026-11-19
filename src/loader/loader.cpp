// loader.cpp - KWIQPlugin.dll, the one file in the sector file packages that EuroScope loads.
//
// Laid out like the UK Controller Plugin's loader. The plugin, KWIQPluginCore.dll, and its updater,
// KWIQPluginUpdater.dll, live in %LOCALAPPDATA%\KWIQ\bin:
//   1. The first time, the controller is told (OK only) and the updater is downloaded from Core.
//   2. The updater runs: it installs or updates the plugin (asking first) and itself, and says how the
//      plugin is to start (in full, reduced after a declined update, or not at all: Core's kill switch).
//   3. The plugin is loaded from bin.
// This file never changes itself: a new loader comes with the next sector file packages.
// Its message boxes are UKCP's (kwiq_win::Box): shown while EuroScope loads the plugin, as UKCP's loader does.

#include "kwiq_win.hpp"
#include "EuroScopePlugIn.h"
#include "version.h"

using namespace kwiq_win;

typedef void (*CoreInitFn)(EuroScopePlugIn::CPlugIn**, int);
typedef void (*CoreExitFn)();
typedef int (*UpdaterRunFn)();

static HMODULE gCore = nullptr;
static CoreExitFn gCoreExit = nullptr;

// Stands in when the plugin is not loaded, so EuroScope still gets a plugin, and says why in the chat
class CKWIQUnavailable : public EuroScopePlugIn::CPlugIn {
public:
    explicit CKWIQUnavailable(const std::string& why)
        : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, "KWIQ Controller Plugin (inactive)", KWIQ_PLUGIN_VERSION,
                  "Kuwait & Iraq vACC", "GPL-3.0-or-later"), why_(why) {}
    void OnTimer(int) override {
        if (told_ || why_.empty()) return;
        told_ = true;
        DisplayUserMessage("KWIQ Controller Plugin", "Error", why_.c_str(), true, true, false, false, false);
    }
private:
    std::string why_;
    bool told_ = false;
};
static CKWIQUnavailable* gFallback = nullptr;

static void Inactive(EuroScopePlugIn::CPlugIn** pp, const std::string& why) {
    *pp = gFallback = new CKWIQUnavailable(why);
}

// As UKCP's DisplayLoaderError
static void LoaderError(const std::string& message, UINT icon = MB_ICONSTOP) {
    Box("KWIQ Loader Error", message, MB_OK | icon);
}

// The first time: the controller is told (OK only, there is nothing to decline) and the updater is downloaded.
// Returns why it failed, or "".
static std::string FirstTimeDownload(const std::string& updater) {
    Box("KWIQ First Time Download",
        "The KWIQ Controller Plugin will now download its components from Core: the KWIQ Plugin Updater, which then "
        "installs the plugin.\r\n\r\nSelect OK to continue.", MB_OK | MB_ICONINFORMATION);
    std::string body;
    int status = CoreGet(L"/latest", "0", body, 64 * 1024, 10000);
    if (status != 200) return "Core could not be reached.";
    kwiq::PluginConfig cfg = kwiq::ParseConfig(body);
    std::string dll, err = DownloadDll(L"updater", kwiq::Lower(kwiq::Trim(cfg.Get("plugin.updater_sha256"))), "0", dll);
    if (!err.empty()) return "The KWIQ Plugin Updater could not be downloaded. " + err;
    if (!WriteWhole(updater, dll.data(), dll.size())) return "The KWIQ Plugin Updater could not be saved in " + updater + ".";
    return "";
}

static void LoaderInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    std::string core = CorePath(), updater = UpdaterPath();
    if (core.empty() || !MakeDirs()) {
        LoaderError("The KWIQ Controller Plugin cannot be installed, because %LOCALAPPDATA%\\KWIQ cannot be created.");
        Inactive(ppPlugInInstance, "The plugin could not be installed in %LOCALAPPDATA%\\KWIQ.");
        return;
    }
    // Loaded twice (e.g. from both OKAC and ORBB in Other Set > Plug-ins): both share one plugin, so the second stays idle
    if (GetModuleHandleA(core.c_str())) {
        LoaderError("The KWIQ Controller Plugin is loaded twice.\r\n\r\nPlease remove one of them in Other Set > Plug-ins.",
                    MB_ICONWARNING);
        Inactive(ppPlugInInstance, "");
        return;
    }
    // Left by the updater replacing files that were in use, and by v0.5 and older
    DeleteFileA((core + ".old").c_str());
    DeleteFileA((updater + ".old").c_str());
    MigrateOldInstall();

    if (!Exists(updater)) {
        // Failed (Core unreachable, a bad checksum, not a 32-bit DLL): said once, the inactive stand-in is
        // loaded, and the next start tries again. No retry or second prompt in this session.
        std::string err = FirstTimeDownload(updater);
        if (!err.empty()) {
            Box("KWIQ Automatic Update Failed", "The KWIQ Controller Plugin could not be installed.\r\n\r\n" + err +
                "\r\n\r\nIt will try again the next time EuroScope starts.", MB_OK | MB_ICONSTOP);
            Inactive(ppPlugInInstance, "The KWIQ Controller Plugin could not be installed. It will try again the next time EuroScope starts.");
            return;
        }
    }

    int mode = (int)kwiq::StartMode::DontLoad;
    HMODULE upd = LoadLibraryA(updater.c_str());
    UpdaterRunFn run = upd ? (UpdaterRunFn)GetProcAddress(upd, "KWIQUpdaterRun") : nullptr;
    if (run) mode = run();
    else if (Exists(core)) mode = (int)kwiq::StartMode::Full;       // a broken updater must not stop a working plugin
    if (upd) FreeLibrary(upd);
    if (!run) DeleteFileA(updater.c_str());                          // downloaded again at the next start

    if (mode == (int)kwiq::StartMode::DontLoad) {
        Inactive(ppPlugInInstance, Exists(core) ? "The KWIQ Controller Plugin has been switched off by the vACC for now."
                                                : "The KWIQ Controller Plugin is not installed. It will be offered again the next time EuroScope starts.");
        return;
    }
    gCore = LoadLibraryA(core.c_str());
    CoreInitFn init = gCore ? (CoreInitFn)GetProcAddress(gCore, "KWIQCoreStart") : nullptr;
    gCoreExit = gCore ? (CoreExitFn)GetProcAddress(gCore, "KWIQCoreExit") : nullptr;
    if (init && gCoreExit) {
        init(ppPlugInInstance, mode);
        return;
    }
    if (gCore) { FreeLibrary(gCore); gCore = nullptr; }
    gCoreExit = nullptr;
    DeleteFileA(core.c_str());                                       // installed again at the next start
    LoaderError("The plugin could not be loaded from " + core + ".\r\n\r\nIt will be installed again the next time EuroScope starts.");
    Inactive(ppPlugInInstance, "The plugin could not be loaded. It will be installed again the next time EuroScope starts.");
}

// EuroScope's entry point: nothing may escape into EuroScope. If anything throws before the plugin is loaded,
// the inactive stand-in says so.
void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    *ppPlugInInstance = nullptr;
    try {
        LoaderInit(ppPlugInInstance);
    } catch (...) {
        if (!*ppPlugInInstance) {
            try { Inactive(ppPlugInInstance, "The KWIQ Controller Plugin could not start. It will try again the next time EuroScope starts."); }
            catch (...) {}
        }
    }
}

void __declspec(dllexport) EuroScopePlugInExit(void) {
    if (gCoreExit) gCoreExit();
    gCoreExit = nullptr;
    if (gCore) { FreeLibrary(gCore); gCore = nullptr; }
    delete gFallback;
    gFallback = nullptr;
}
