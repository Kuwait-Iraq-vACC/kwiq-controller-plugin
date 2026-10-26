// loader.cpp - KWIQPlugin.dll, the one file controllers download and load in EuroScope.
//
// The plugin itself, KWIQPluginCore.dll, is built into this file. It is kept up to date in
// %APPDATA%\KWIQ\plugin and loaded from there: put there from this file the first time (or when
// this file carries a newer version), and updated from Core. As EuroScope starts, before the plugin
// is loaded, the loader asks Core for the latest version; if there is a newer one it offers it in a
// Windows message box, downloads it, checks its SHA-256 against the one Core publishes and that it
// is a 32-bit DLL, and replaces the file. The new version then runs straight away: nothing to
// restart. If Core cannot be reached the installed version is loaded.

#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <string>
#include "EuroScopePlugIn.h"
#include "kwiq_logic.hpp"
#include "version.h"

#define CORE_HOST L"core.kuwait-iraqvacc.net"
#define CORE_PATH L"/api/plugin"
static const char* const kTitle = "KWIQ Controller Plugin";

typedef void (*CoreInitFn)(EuroScopePlugIn::CPlugIn**);
typedef void (*CoreExitFn)();
typedef const char* (*CoreVersionFn)();

static HMODULE gCore = nullptr;
static CoreExitFn gCoreExit = nullptr;

// Stands in when the plugin cannot be loaded, so EuroScope still gets a plugin and says nothing odd
class CKWIQUnavailable : public EuroScopePlugIn::CPlugIn {
public:
    CKWIQUnavailable() : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, "KWIQ Controller Plugin (inactive)", "-", "Kuwait & Iraq vACC", "GPL-3.0-or-later") {}
};
static CKWIQUnavailable* gFallback = nullptr;

// This file (KWIQPlugin.dll) and its folder
static std::string SelfPath() {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&SelfPath, &m);
    char b[MAX_PATH] = {};
    return m && GetModuleFileNameA(m, b, MAX_PATH) ? std::string(b) : std::string();
}
static std::string Here() {
    std::string p = SelfPath();
    size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? std::string() : p.substr(0, i);
}

static std::string ReadSmallFile(const std::string& path, size_t max) {
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "";
    DWORD size = GetFileSize(f, nullptr);
    size_t want = size == INVALID_FILE_SIZE ? max : (std::min)((size_t)size, max);
    std::string b(want, '\0');
    DWORD n = 0;
    if (want) ReadFile(f, &b[0], (DWORD)want, &n, nullptr);
    CloseHandle(f);
    b.resize(n);
    return b;
}

// %APPDATA%\KWIQ, where the plugin keeps the key from myKWIQ and its own installed copy
static std::string KwiqDir() {
    char b[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("APPDATA", b, MAX_PATH);
    return n && n < MAX_PATH ? std::string(b) + "\\KWIQ" : std::string();
}

static std::string ApiKey() {
    std::string d = KwiqDir();
    std::string k = d.empty() ? "" : kwiq::Trim(ReadSmallFile(d + "\\plugin-key.txt", 128));
    return kwiq::IsApiKey(k) ? k : "";
}

static bool WriteWhole(const std::string& path, const void* data, size_t size) {
    std::string tmp = path + ".new";
    HANDLE f = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data, (DWORD)size, &written, nullptr) && written == size;
    CloseHandle(f);
    if (!ok || !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        return false;
    }
    return true;
}

// GET from Core, with the key and version as the plugin sends them. Returns the HTTP status (0 = no answer).
static int CoreGet(const std::wstring& path, const std::string& version, std::string& body, size_t maxBytes, DWORD timeoutMs) {
    body.clear();
    int status = 0;
    HINTERNET s = WinHttpOpen(L"KWIQPluginLoader", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET c = WinHttpConnect(s, CORE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (r) {
        std::string h = "X-KWIQ-Version: " + version + "\r\n";
        std::string key = ApiKey();
        if (!key.empty()) h += "X-KWIQ-Key: " + key + "\r\n";
        std::wstring wh(h.begin(), h.end());
        if (WinHttpSendRequest(r, wh.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(r, &avail) && avail > 0) {
                if (body.size() + avail > maxBytes) { status = 0; break; }
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(r, &chunk[0], avail, &read) || read == 0) break;
                body.append(chunk.data(), read);
            }
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return status;
}

// The installed plugin's version, read from the file itself (it is not started)
static std::string InstalledVersion(const std::string& core) {
    if (GetFileAttributesA(core.c_str()) == INVALID_FILE_ATTRIBUTES) return "";
    HMODULE m = LoadLibraryA(core.c_str());
    if (!m) return "0";
    CoreVersionFn v = (CoreVersionFn)GetProcAddress(m, "KWIQCoreVersion");
    std::string out = v ? v() : "0";
    FreeLibrary(m);
    return out;
}

// Download, check and put in place. Returns why it failed, or "" when done.
static std::string Install(const std::string& core, const std::string& installed, const std::string& sha) {
    std::string dll;
    int status = CoreGet(CORE_PATH L"/download", installed.empty() ? "0" : installed, dll, 40u * 1024 * 1024, 60000);
    if (status == 401) return "Core did not accept your API key. Connect it again from myKWIQ.";
    if (status != 200) return status ? "Core answered with HTTP " + std::to_string(status) + "." : "Core could not be reached.";
    if (kwiq::Sha256Hex(dll) != kwiq::Lower(sha)) return "The download did not match the checksum Core published.";
    if (!kwiq::LooksLikeX86Dll(dll)) return "The download is not a 32-bit plugin DLL.";
    if (!WriteWhole(core, dll.data(), dll.size()))
        return "The plugin file could not be replaced (" + core + "). Is EuroScope open twice?";
    return "";
}

// Puts the plugin built into this file in place, the first time or when this file's is newer
static void ExtractBuiltIn(const std::string& core) {
    std::string installed = InstalledVersion(core);
    if (!installed.empty() && installed != "0" && kwiq::CompareVersions(installed, KWIQ_PLUGIN_VERSION) >= 0) return;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&ExtractBuiltIn, &self);
    HRSRC res = FindResourceA(self, "KWIQCORE", MAKEINTRESOURCEA(10));     // 10 = RT_RCDATA
    HGLOBAL data = res ? LoadResource(self, res) : nullptr;
    const void* bytes = data ? LockResource(data) : nullptr;
    DWORD size = res ? SizeofResource(self, res) : 0;
    if (!bytes || !size) return;
    CreateDirectoryA(KwiqDir().c_str(), nullptr);
    CreateDirectoryA((KwiqDir() + "\\plugin").c_str(), nullptr);
    if (!WriteWhole(core, bytes, size) && installed.empty())
        MessageBoxA(nullptr, ("The plugin could not be installed in " + core + ".").c_str(), kTitle, MB_OK | MB_ICONWARNING | MB_TOPMOST);
}

// This file replaces itself with the latest KWIQPlugin.dll, quietly, for the next start: Windows lets
// a loaded DLL be renamed but not overwritten, so it moves aside to KWIQPlugin.dll.old (removed then)
static void UpdateSelf(const kwiq::PluginConfig& cfg) {
    std::string self = SelfPath(), latest = kwiq::Trim(cfg.Get("plugin.latest_version"));
    std::string sha = kwiq::Lower(kwiq::Trim(cfg.Get("plugin.loader_sha256")));
    if (self.empty() || sha.size() != 64 || kwiq::CompareVersions(KWIQ_PLUGIN_VERSION, latest) >= 0) return;
    std::string mine = ReadSmallFile(self, 64u * 1024 * 1024);
    if (mine.empty() || kwiq::Sha256Hex(mine) == sha) return;     // unreadable: leave it rather than fetch every start

    std::string dll;
    if (CoreGet(CORE_PATH L"/download/loader", KWIQ_PLUGIN_VERSION, dll, 64u * 1024 * 1024, 60000) != 200) return;
    if (kwiq::Sha256Hex(dll) != sha || !kwiq::LooksLikeX86Dll(dll)) return;
    std::string old = self + ".old";
    DeleteFileA(old.c_str());
    if (!MoveFileExA(self.c_str(), old.c_str(), 0)) return;
    if (!WriteWhole(self, dll.data(), dll.size())) MoveFileExA(old.c_str(), self.c_str(), 0);   // put it back
}

static void CheckForUpdate(const std::string& core) {
    std::string installed = InstalledVersion(core), body;
    if (CoreGet(CORE_PATH L"/config", installed.empty() ? "0" : installed, body, 1024 * 1024, 5000) != 200) return;
    kwiq::PluginConfig cfg = kwiq::ParseConfig(body);
    UpdateSelf(cfg);
    std::string latest = kwiq::Trim(cfg.Get("plugin.latest_version")), sha = kwiq::Trim(cfg.Get("plugin.sha256"));

    if (installed.empty()) {                             // the main file is missing: put it back without asking
        if (latest.empty() || sha.size() != 64) return;
        std::string err = Install(core, installed, sha);
        if (!err.empty()) MessageBoxA(nullptr, ("The plugin could not be installed. " + err).c_str(), kTitle, MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return;
    }
    if (!kwiq::UpdateAvailable(installed, latest, sha)) return;

    std::string ask = "Version " + latest + " of the KWIQ Controller Plugin is available. You have version " + installed +
                      ".\n\nWould you like to update now? It only takes a moment, and the new version starts straight away.";
    if (MessageBoxA(nullptr, ask.c_str(), kTitle, MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES) return;

    std::string err = Install(core, installed, sha);
    if (err.empty())
        MessageBoxA(nullptr, ("The KWIQ Controller Plugin has been updated to version " + latest + ".").c_str(), kTitle, MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    else
        MessageBoxA(nullptr, ("The update could not be installed, so version " + installed + " will be used. " + err).c_str(), kTitle, MB_OK | MB_ICONWARNING | MB_TOPMOST);
}

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    std::string dir = KwiqDir();
    if (dir.empty()) dir = Here();                        // no %APPDATA%: keep it next to this file
    else dir += "\\plugin";
    std::string core = dir + "\\KWIQPluginCore.dll";
    // Loaded twice (e.g. from both OKAC and ORBB in Other Set > Plug-ins): both copies would share one
    // installed plugin, so the second stays idle
    if (GetModuleHandleA(core.c_str())) {
        MessageBoxA(nullptr, "The KWIQ Controller Plugin is loaded twice. Please remove one of them in Other Set > Plug-ins.",
                    kTitle, MB_OK | MB_ICONWARNING | MB_TOPMOST);
        *ppPlugInInstance = gFallback = new CKWIQUnavailable();
        return;
    }
    if (dir != Here()) DeleteFileA((Here() + "\\KWIQPluginCore.dll").c_str());   // left next to this file by v0.5.0.11, which came as two files
    DeleteFileA((SelfPath() + ".old").c_str());          // left by updating this file last time
    ExtractBuiltIn(core);
    CheckForUpdate(core);

    gCore = LoadLibraryA(core.c_str());
    CoreInitFn init = gCore ? (CoreInitFn)GetProcAddress(gCore, "KWIQCoreInit") : nullptr;
    gCoreExit = gCore ? (CoreExitFn)GetProcAddress(gCore, "KWIQCoreExit") : nullptr;
    if (init && gCoreExit) {
        init(ppPlugInInstance);
        return;
    }
    if (gCore) { FreeLibrary(gCore); gCore = nullptr; }
    MessageBoxA(nullptr, ("The plugin could not be loaded from " + core + ". Please download it again from myKWIQ.").c_str(),
                kTitle, MB_OK | MB_ICONWARNING | MB_TOPMOST);
    *ppPlugInInstance = gFallback = new CKWIQUnavailable();
}

void __declspec(dllexport) EuroScopePlugInExit(void) {
    if (gCoreExit) gCoreExit();
    gCoreExit = nullptr;
    if (gCore) { FreeLibrary(gCore); gCore = nullptr; }
    delete gFallback;
    gFallback = nullptr;
}
