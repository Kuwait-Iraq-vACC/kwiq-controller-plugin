// kwiq_win.hpp - Windows helpers shared by the loader (loader.cpp), the updater (updater.cpp) and the plugin.
//
// Laid out like the UK Controller Plugin:
//   %LOCALAPPDATA%\KWIQ\bin        KWIQPluginUpdater.dll and KWIQPluginCore.dll
//   %LOCALAPPDATA%\KWIQ\settings   api-settings.json, the personal API key from myKWIQ
// The sector file packages carry only KWIQPlugin.dll, the loader EuroScope loads.
// The plugin defines KWIQ_NO_WINHTTP: it talks to Core with libcurl, and only uses the file helpers.
#pragma once
#include <windows.h>
#ifndef KWIQ_NO_WINHTTP          // the plugin uses WinInet (VCH), which cannot be included with WinHTTP
#include <winhttp.h>
#endif
#include <algorithm>
#include <string>
#include "kwiq_logic.hpp"

namespace kwiq_win {

static const wchar_t* const kCoreHost = L"core.kuwait-iraqvacc.net";
static const wchar_t* const kCorePath = L"/api/plugin";
static const char* const kTitle = "KWIQ Controller Plugin";
static const char* const kChangelogUrl = "https://docs.kuwait-iraqvacc.net/Controller%20Guidance/KWIQ%20Controller%20Plugin/Changelog/";

inline std::string Env(const char* name) {
    char b[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA(name, b, MAX_PATH);
    return n && n < MAX_PATH ? std::string(b) : std::string();
}
// %LOCALAPPDATA%\KWIQ (%APPDATA%\KWIQ if there is no LOCALAPPDATA, as on some Wine setups)
inline std::string KwiqDir() {
    std::string d = Env("LOCALAPPDATA");
    if (d.empty()) d = Env("APPDATA");
    return d.empty() ? std::string() : d + "\\KWIQ";
}
inline std::string BinDir() { std::string d = KwiqDir(); return d.empty() ? d : d + "\\bin"; }
inline std::string SettingsDir() { std::string d = KwiqDir(); return d.empty() ? d : d + "\\settings"; }
inline std::string UpdaterPath() { std::string d = BinDir(); return d.empty() ? d : d + "\\KWIQPluginUpdater.dll"; }
inline std::string CorePath() { std::string d = BinDir(); return d.empty() ? d : d + "\\KWIQPluginCore.dll"; }
inline bool MakeDirs() {
    std::string k = KwiqDir();
    if (k.empty()) return false;
    CreateDirectoryA(k.c_str(), nullptr);
    CreateDirectoryA(BinDir().c_str(), nullptr);
    CreateDirectoryA(SettingsDir().c_str(), nullptr);
    DWORD a = GetFileAttributesA(BinDir().c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
inline bool Exists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::string ReadSmallFile(const std::string& path, size_t max) {
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
// Written to path.new, then moved over path, so a failed write never leaves half a file
inline bool WriteWhole(const std::string& path, const void* data, size_t size) {
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
// Replaces a DLL that may be loaded: Windows lets a loaded DLL be renamed but not overwritten, so it
// moves aside to <file>.old, which the loader removes at the next start
inline bool ReplaceDll(const std::string& path, const std::string& bytes) {
    if (WriteWhole(path, bytes.data(), bytes.size())) return true;
    std::string old = path + ".old";
    DeleteFileA(old.c_str());
    if (!MoveFileExA(path.c_str(), old.c_str(), 0)) return false;
    if (WriteWhole(path, bytes.data(), bytes.size())) return true;
    MoveFileExA(old.c_str(), path.c_str(), 0);                   // put it back
    return false;
}

// ---- personal API key --------------------------------------------------------------------------
inline std::string LoadApiKey() {
    std::string d = SettingsDir();
    return d.empty() ? "" : kwiq::ApiKeyFromSettingsJson(ReadSmallFile(d + "\\api-settings.json", 4096));
}
inline bool SaveApiKey(const std::string& key) {
    if (!MakeDirs()) return false;
    std::string f = SettingsDir() + "\\api-settings.json";
    if (key.empty()) { DeleteFileA(f.c_str()); return !Exists(f); }
    std::string json = kwiq::ApiSettingsJson(key);
    return WriteWhole(f, json.data(), json.size());
}

// Up to v0.5 the key was %APPDATA%\KWIQ\plugin-key.txt and the plugin %APPDATA%\KWIQ\plugin\KWIQPluginCore.dll:
// the key moves to api-settings.json, and the old install is removed
inline void MigrateOldInstall() {
    std::string app = Env("APPDATA");
    if (app.empty()) return;
    std::string old = app + "\\KWIQ";
    std::string key = kwiq::Trim(ReadSmallFile(old + "\\plugin-key.txt", 128));
    if (kwiq::IsApiKey(key) && LoadApiKey().empty()) SaveApiKey(key);
    if (!LoadApiKey().empty() || !kwiq::IsApiKey(key)) DeleteFileA((old + "\\plugin-key.txt").c_str());
    for (const char* f : { "KWIQPluginCore.dll", "KWIQPluginCore.dll.new", "KWIQPluginCore.dll.download" })
        DeleteFileA((old + "\\plugin\\" + f).c_str());
    RemoveDirectoryA((old + "\\plugin").c_str());               // only once empty
    if (old != KwiqDir()) RemoveDirectoryA(old.c_str());
}

#ifndef KWIQ_NO_WINHTTP
// ---- Core ------------------------------------------------------------------------------------------
// GET from Core with the key and version as the plugin sends them. Returns the HTTP status (0 = no answer).
inline int CoreGet(const std::wstring& path, const std::string& version, std::string& body, size_t maxBytes, DWORD timeoutMs) {
    body.clear();
    int status = 0;
    HINTERNET s = WinHttpOpen(L"KWIQPluginUpdater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"KWIQPluginUpdater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET c = WinHttpConnect(s, kCoreHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    std::wstring full = std::wstring(kCorePath) + path;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", full.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (r) {
        std::string h = "X-KWIQ-Version: " + version + "\r\n";
        std::string key = LoadApiKey();
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

// One of the plugin's files from Core (/download/core, /download/updater), checked against the SHA-256
// Core publishes and that it is a 32-bit DLL. Returns why it failed, or "".
inline std::string DownloadDll(const wchar_t* what, const std::string& sha, const std::string& version, std::string& dll) {
    if (sha.size() != 64) return "Core has not published a checksum for it.";
    int status = CoreGet(std::wstring(L"/download/") + what, version, dll, 64u * 1024 * 1024, 60000);
    if (status != 200) return status ? "Core answered with HTTP " + std::to_string(status) + "." : "Core could not be reached.";
    if (kwiq::Sha256Hex(dll) != sha) return "The download did not match the checksum Core published.";
    if (!kwiq::LooksLikeX86Dll(dll)) return "The download is not a 32-bit plugin DLL.";
    return "";
}

#endif // KWIQ_NO_WINHTTP

inline int Box(const std::string& text, UINT flags) {
    return MessageBoxA(nullptr, text.c_str(), kTitle, flags | MB_TOPMOST | MB_SETFOREGROUND);
}

} // namespace kwiq_win
