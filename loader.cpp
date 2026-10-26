// loader.cpp - KWIQPlugin.dll, the file controllers load in EuroScope.
//
// It keeps the real plugin, KWIQPluginCore.dll (next to it), up to date and then loads it. As
// EuroScope starts, before the plugin is loaded, it asks Core for the latest version; if there is
// a newer one it offers it in a Windows message box, downloads it, checks its SHA-256 against the
// one Core publishes and that it is a 32-bit DLL, and replaces the file. The new version then runs
// straight away: nothing to restart. If Core cannot be reached the installed version is loaded.

#include <windows.h>
#include <winhttp.h>
#include <string>
#include "EuroScopePlugIn.h"
#include "kwiq_logic.hpp"

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
    CKWIQUnavailable() : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, kTitle, "unavailable", "Kuwait & Iraq vACC", "GPL-3.0-or-later") {}
};
static CKWIQUnavailable* gFallback = nullptr;

static std::string Here() {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Here, &m);
    char b[MAX_PATH] = {};
    if (!m || !GetModuleFileNameA(m, b, MAX_PATH)) return "";
    std::string p = b;
    size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? std::string() : p.substr(0, i);
}

static std::string ReadSmallFile(const std::string& path, size_t max) {
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "";
    std::string b(max, '\0');
    DWORD n = 0;
    ReadFile(f, &b[0], (DWORD)max, &n, nullptr);
    CloseHandle(f);
    b.resize(n);
    return b;
}

// The personal key from myKWIQ, kept by the plugin in %APPDATA%\KWIQ\plugin-key.txt
static std::string ApiKey() {
    char b[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("APPDATA", b, MAX_PATH);
    if (!n || n >= MAX_PATH) return "";
    std::string k = kwiq::Trim(ReadSmallFile(std::string(b) + "\\KWIQ\\plugin-key.txt", 128));
    return kwiq::IsApiKey(k) ? k : "";
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
    std::string tmp = core + ".new";
    HANDLE f = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "The plugin's folder cannot be written to (" + core + ").";
    DWORD written = 0;
    bool ok = WriteFile(f, dll.data(), (DWORD)dll.size(), &written, nullptr) && written == dll.size();
    CloseHandle(f);
    if (!ok || !MoveFileExA(tmp.c_str(), core.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        return "The plugin file could not be replaced (" + core + ").";
    }
    return "";
}

static void CheckForUpdate(const std::string& core) {
    std::string installed = InstalledVersion(core), body;
    if (CoreGet(CORE_PATH L"/config", installed.empty() ? "0" : installed, body, 1024 * 1024, 5000) != 200) return;
    kwiq::PluginConfig cfg = kwiq::ParseConfig(body);
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
    std::string core = Here() + "\\KWIQPluginCore.dll";
    CheckForUpdate(core);

    gCore = LoadLibraryA(core.c_str());
    CoreInitFn init = gCore ? (CoreInitFn)GetProcAddress(gCore, "KWIQCoreInit") : nullptr;
    gCoreExit = gCore ? (CoreExitFn)GetProcAddress(gCore, "KWIQCoreExit") : nullptr;
    if (init && gCoreExit) {
        init(ppPlugInInstance);
        return;
    }
    if (gCore) { FreeLibrary(gCore); gCore = nullptr; }
    MessageBoxA(nullptr, ("KWIQPluginCore.dll could not be loaded from " + Here() +
                          ". Please download the plugin again from myKWIQ and put both files in the same folder.").c_str(),
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
