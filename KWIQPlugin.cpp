// KWIQPlugin.cpp - Kuwait & Iraq vACC Controller Plugin
//
//   * VCH (Virtual Controller Helper, GPL-3.0, Jan Fries) is built in: CKWIQPlugin extends CVCHPlugin
//   * Event routes (OK / CHK) + "Open event routes window"
//   * Event Flag (EV) / Event Slot from VATCAN bookings (see VatsimAPI.cpp)
//   * FPC against the SRD, "Explain FPC result", "Open SRD search window"
//
// All route data comes from Core (Laravel). License: GPL-3.0-or-later, see LICENSE and NOTICE.md.

#include "stdafx.h"
#include <mutex>
#include <thread>
#include <atomic>
#include <cstring>
#include "requestMenu.hpp"      // CVCHPlugin (VCH)
#include "kwiq_logic.hpp"
#include "VatsimAPI.h"

#define KWIQ_PLUGIN_NAME      "KWIQ Controller Plugin"
#define KWIQ_PLUGIN_VERSION   "0.4.0"
#define KWIQ_PLUGIN_AUTHOR    "Kuwait & Iraq vACC"
#define KWIQ_PLUGIN_COPYRIGHT "GPL-3.0-or-later"

// ---- CHANGE ME: the ONLY thing compiled in is where Core lives. -------------
// Everything else (colours, texts, intervals, sector aliases, kill switch, minimum version,
// sector file AIRAC ...) is served by Core at /api/plugin/config.
#define CORE_BASE "https://core.kuwait-iraqvacc.net/api/plugin"
static const char* CORE_CONFIG_URL      = CORE_BASE "/config";
static const char* CORE_EVENT_URL       = CORE_BASE "/event";
static const char* CORE_SRD_URL         = CORE_BASE "/srd";
static const char* CORE_SRD_VERSION_URL = CORE_BASE "/srd/version";
static const char* CORE_SECTORS_URL     = CORE_BASE "/sectors";
// ---------------------------------------------------------------------------

// Tag item / function ids. VCH uses 100, 340-346, 440-447, 540-556, 640-650: keep clear of those.
enum {
    KWIQ_ITEM_EVENT_ROUTE = 1001,
    KWIQ_ITEM_EVENT_FLAG  = 1002,
    KWIQ_ITEM_EVENT_SLOT  = 1003,
    KWIQ_ITEM_FPC         = 1004,
    KWIQ_ITEM_SECTOR      = 1005,   // sector indicator of the ORBB station that tracks the aircraft
    KWIQ_FUNC_EVENT_ROUTES = 2001,   // "Open event routes window"
    KWIQ_FUNC_FPC_EXPLAIN  = 2002,   // "Explain FPC result"
    KWIQ_FUNC_SRD_SEARCH   = 2003    // "Open SRD search window"
};

// ---------------------------------------------------------------------------
// Windows (plain Win32, no MFC): event routes list and SRD search, Copy button per row
// ---------------------------------------------------------------------------
namespace kwiq_ui {

static const char* const kClass = "KWIQPluginWnd";
enum { ID_SEARCH = 2900, ID_COPY_BASE = 3000, MAX_ROWS = 24, SRD_ROWS = 12 };
static const int M = 10, LBL = 170, EDT = 460, BTN = 70, GAP = 6, ROWH = 24, ROWGAP = 6;
static const int CW = M + LBL + GAP + EDT + GAP + BTN + M;   // client width

struct Win {
    HWND hwnd = nullptr;
    std::vector<std::string> copy;
    std::vector<HWND> btns, lbls, edits;
    HWND eAdep = nullptr, eAdes = nullptr, eLvl = nullptr, sInfo = nullptr;
};
static Win gEvt, gSrd;
static HMODULE gMod = nullptr;
static bool gClassReg = false;
static void (*gOnSearch)(const std::string&, const std::string&, const std::string&) = nullptr;

static Win* WinOf(HWND h) { return (h && h == gEvt.hwnd) ? &gEvt : (h && h == gSrd.hwnd) ? &gSrd : nullptr; }
static std::string TextOf(HWND h) { char b[256]; GetWindowTextA(h, b, sizeof b); return b; }

static void CopyToClipboard(HWND owner, const std::string& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (h) {
        void* p = GlobalLock(h);
        if (p) {
            std::memcpy(p, text.c_str(), text.size() + 1);
            GlobalUnlock(h);
            if (!SetClipboardData(CF_TEXT, h)) GlobalFree(h);
        } else GlobalFree(h);
    }
    CloseClipboard();
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        Win* win = WinOf(h);
        if (!win || HIWORD(w) != BN_CLICKED) return 0;
        int id = (int)LOWORD(w);
        if (id == ID_SEARCH && gOnSearch && win == &gSrd) {
            gOnSearch(TextOf(win->eAdep), TextOf(win->eAdes), TextOf(win->eLvl));
        } else if (id >= ID_COPY_BASE && id - ID_COPY_BASE < (int)win->copy.size()) {
            CopyToClipboard(h, win->copy[id - ID_COPY_BASE]);
            for (HWND b : win->btns) SetWindowTextA(b, "Copy");
            SetWindowTextA((HWND)l, "Copied");
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (Win* win = WinOf(h)) *win = Win();
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static HWND Child(const char* cls, const char* text, DWORD style, int x, int y, int w, int h,
                  HWND parent, int id = 0) {
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                             (HMENU)(INT_PTR)id, gMod, nullptr);
    SendMessageA(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

// Creates the popup, stores it in `win`, destroying any previous one of the same kind.
static bool Make(Win& win, POINT pt, const std::string& title, int ch) {
    if (!gMod)
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&WndProc, &gMod);
    if (!gClassReg) {
        WNDCLASSEXA wc; ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = gMod;
        wc.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)); // IDC_ARROW
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClass;
        if (!RegisterClassExA(&wc)) return false;
        gClassReg = true;
    }
    if (win.hwnd) DestroyWindow(win.hwnd);   // WM_DESTROY resets `win`

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    RECT rc = { 0, 0, CW, ch };
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int x = (int)pt.x, y = (int)pt.y;
    if (x + ww > wa.right)  x = wa.right - ww;
    if (y + wh > wa.bottom) y = wa.bottom - wh;
    if (x < wa.left) x = wa.left;
    if (y < wa.top)  y = wa.top;

    HWND h = CreateWindowExA(ex, kClass, title.c_str(), style, x, y, ww, wh, nullptr, nullptr, gMod, nullptr);
    if (!h) return false;
    win = Win();
    win.hwnd = h;
    return true;
}

static std::string LevelsText(const kwiq::SrdRoute& r) {
    if (r.minLvl && r.maxLvl) return kwiq::FlStr(r.minLvl) + "-" + kwiq::FlStr(r.maxLvl);
    if (r.minLvl) return kwiq::FlStr(r.minLvl) + "+";
    if (r.maxLvl) return "<=" + kwiq::FlStr(r.maxLvl);
    return "";
}

static void ShowEvent(POINT pt, const std::string& title, const std::vector<kwiq::EventRoute>& rows) {
    int n = (int)(rows.size() < (size_t)MAX_ROWS ? rows.size() : (size_t)MAX_ROWS);
    int ch = M + (n ? n * (ROWH + ROWGAP) - ROWGAP : ROWH) + M;
    if (!Make(gEvt, pt, title, ch)) return;
    HWND h = gEvt.hwnd;
    if (n == 0) Child("STATIC", "No event routes for today (UTC).", SS_LEFT, M, M + 4, CW - 2 * M, ROWH, h);
    for (int i = 0; i < n; i++) {
        int ry = M + i * (ROWH + ROWGAP);
        std::string lbl = rows[i].adep + " -> " + rows[i].ades;
        Child("STATIC", lbl.c_str(), SS_LEFT, M, ry + 4, LBL, ROWH - 4, h);
        Child("EDIT", rows[i].rte.c_str(), WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, M + LBL + GAP, ry, EDT, ROWH, h);
        gEvt.copy.push_back(rows[i].rte);
        gEvt.btns.push_back(Child("BUTTON", "Copy", BS_PUSHBUTTON, M + LBL + GAP + EDT + GAP, ry, BTN, ROWH, h,
                                  ID_COPY_BASE + i));
    }
    ShowWindow(h, SW_SHOW);
}

static void ShowSrd(POINT pt, const std::string& adep, const std::string& ades, const std::string& lvl) {
    int rowsTop = M + ROWH + 8 + ROWH + 6;
    int ch = rowsTop + SRD_ROWS * (ROWH + ROWGAP) - ROWGAP + M;
    if (!Make(gSrd, pt, "KWIQ SRD Search", ch)) return;
    HWND h = gSrd.hwnd;
    int x = M, y = M;
    Child("STATIC", "ADEP", SS_LEFT, x, y + 4, 36, ROWH - 4, h);
    gSrd.eAdep = Child("EDIT", adep.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_UPPERCASE, x + 40, y, 70, ROWH, h);
    x += 40 + 70 + 14;
    Child("STATIC", "ADES", SS_LEFT, x, y + 4, 36, ROWH - 4, h);
    gSrd.eAdes = Child("EDIT", ades.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_UPPERCASE, x + 40, y, 70, ROWH, h);
    x += 40 + 70 + 14;
    Child("STATIC", "Level", SS_LEFT, x, y + 4, 36, ROWH - 4, h);
    gSrd.eLvl = Child("EDIT", lvl.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_UPPERCASE, x + 40, y, 70, ROWH, h);
    x += 40 + 70 + 14;
    Child("BUTTON", "Search", BS_PUSHBUTTON, x, y, 80, ROWH, h, ID_SEARCH);
    gSrd.sInfo = Child("STATIC", "Enter ADEP and/or ADES (airport or waypoint) and optionally a level, then Search.",
                       SS_LEFT, M, M + ROWH + 8 + 4, CW - 2 * M, ROWH - 4, h);
    gSrd.copy.assign(SRD_ROWS, "");
    for (int i = 0; i < SRD_ROWS; i++) {
        int ry = rowsTop + i * (ROWH + ROWGAP);
        gSrd.lbls.push_back(Child("STATIC", "", SS_LEFT, M, ry + 4, LBL, ROWH - 4, h));
        gSrd.edits.push_back(Child("EDIT", "", WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, M + LBL + GAP, ry, EDT, ROWH, h));
        gSrd.btns.push_back(Child("BUTTON", "Copy", BS_PUSHBUTTON, M + LBL + GAP + EDT + GAP, ry, BTN, ROWH, h,
                                  ID_COPY_BASE + i));
        ShowWindow(gSrd.lbls.back(), SW_HIDE); ShowWindow(gSrd.edits.back(), SW_HIDE); ShowWindow(gSrd.btns.back(), SW_HIDE);
    }
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(gSrd.eAdep);
}

static void SetSrdResults(const std::vector<kwiq::SrdRoute>& rows, size_t total) {
    if (!gSrd.hwnd) return;
    for (int i = 0; i < SRD_ROWS; i++) {
        int show = i < (int)rows.size() ? SW_SHOW : SW_HIDE;
        if (show == SW_SHOW) {
            const auto& r = rows[i];
            std::string lbl = r.adep + " -> " + r.ades + "  " + LevelsText(r);
            SetWindowTextA(gSrd.lbls[i], lbl.c_str());
            std::string rte = r.notes.empty() ? r.rte : r.rte + "   [" + r.notes + "]";
            SetWindowTextA(gSrd.edits[i], rte.c_str());
            SetWindowTextA(gSrd.btns[i], "Copy");
            gSrd.copy[i] = r.rte;
        }
        ShowWindow(gSrd.lbls[i], show); ShowWindow(gSrd.edits[i], show); ShowWindow(gSrd.btns[i], show);
    }
    std::string info = total == 0 ? "No matching routes."
        : std::to_string(total) + " matching route(s)" + (total > rows.size() ? ", showing the first " + std::to_string(rows.size()) + " - narrow the search" : "");
    SetWindowTextA(gSrd.sInfo, info.c_str());
}

static void Shutdown() {
    if (gEvt.hwnd) DestroyWindow(gEvt.hwnd);
    if (gSrd.hwnd) DestroyWindow(gSrd.hwnd);
    if (gClassReg && gMod) { UnregisterClassA(kClass, gMod); gClassReg = false; }
}

} // namespace kwiq_ui

class CKWIQPlugin;
static CKWIQPlugin* gPlugin = nullptr;

// ---- Local sector file version -------------------------------------------------
// EuroScope does not expose the sector file version to plugins, so it is read from disk:
//   1. AIRAC_VERSION.txt (e.g. "2610/02") next to the sector file / install package, or
//   2. the "AIRAC 2610/1" line in the header of the .ese in that folder.
// Folder = the path set with ".kwiq sectorfile <folder|file>", else the plugin DLL's folder and its parents.
namespace sf {
static std::string ReadHead(const std::string& path, size_t max = 16384) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return "";
    std::string b(max, '\0');
    b.resize(fread(&b[0], 1, max, f));
    fclose(f);
    return b;
}
static bool IsDir(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static kwiq::Airac FromDir(const std::string& dir, std::string& src) {
    kwiq::Airac a = kwiq::FindAirac(ReadHead(dir + "\\AIRAC_VERSION.txt", 256));
    if (a.valid()) { src = dir + "\\AIRAC_VERSION.txt"; return a; }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.ese").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string last;                                   // names are timestamped: the last one is the newest
    do { if (last.empty() || last < fd.cFileName) last = fd.cFileName; } while (FindNextFileA(h, &fd));
    FindClose(h);
    a = kwiq::FindAirac(ReadHead(dir + "\\" + last), "AIRAC");
    if (a.valid()) src = dir + "\\" + last;
    return a;
}
static std::string ParentOf(const std::string& p) {
    size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? std::string() : p.substr(0, i);
}
static std::string DllDir() {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&DllDir, &m);
    char b[MAX_PATH] = {};
    if (!m || !GetModuleFileNameA(m, b, MAX_PATH)) return "";
    return ParentOf(b);
}
static kwiq::Airac Local(const std::string& userPath, std::string& src) {
    if (!userPath.empty()) {
        if (IsDir(userPath)) return FromDir(userPath, src);
        kwiq::Airac a = kwiq::FindAirac(ReadHead(userPath), "AIRAC");
        if (!a.valid()) a = kwiq::FindAirac(ReadHead(userPath, 256));
        if (a.valid()) src = userPath;
        return a;
    }
    std::string d = DllDir();
    for (int i = 0; i < 3 && !d.empty(); i++, d = ParentOf(d)) {
        kwiq::Airac a = FromDir(d, src);
        if (a.valid()) return a;
    }
    return {};
}
} // namespace sf

// Values Core controls (defaults apply until the first config arrives or if Core is unreachable).
struct UiCfg {
    std::string ok = "OK", chk = "CHK", ev = "EV";
    COLORREF cOk = RGB(0, 190, 0), cBad = RGB(230, 30, 30), cEv = RGB(0, 170, 255), cSector = RGB(255, 255, 255);
};
static void SrdSearchThunk(const std::string& adep, const std::string& ades, const std::string& lvl);

class CKWIQPlugin : public CVCHPlugin {
public:
    CKWIQPlugin()
        : CVCHPlugin(KWIQ_PLUGIN_NAME, KWIQ_PLUGIN_VERSION, KWIQ_PLUGIN_AUTHOR, KWIQ_PLUGIN_COPYRIGHT) {
        kwiq_net::GlobalInit();
        RegisterTagItemType("Event Route", KWIQ_ITEM_EVENT_ROUTE);
        RegisterTagItemType("Event Flag",  KWIQ_ITEM_EVENT_FLAG);
        RegisterTagItemType("Event Slot",  KWIQ_ITEM_EVENT_SLOT);
        RegisterTagItemType("FPC",         KWIQ_ITEM_FPC);
        RegisterTagItemType("Sector Indicator", KWIQ_ITEM_SECTOR);
        RegisterTagItemFunction("Open event routes window", KWIQ_FUNC_EVENT_ROUTES);
        RegisterTagItemFunction("Explain FPC result",       KWIQ_FUNC_FPC_EXPLAIN);
        RegisterTagItemFunction("Open SRD search window",   KWIQ_FUNC_SRD_SEARCH);
        kwiq_ui::gOnSearch = &SrdSearchThunk;
        Msg(std::string("KWIQ plugin v") + KWIQ_PLUGIN_VERSION + " loaded", false);
    }
    virtual ~CKWIQPlugin() {
        kwiq_ui::gOnSearch = nullptr;
        kwiq_ui::Shutdown();
        if (worker_.joinable()) worker_.join();
        CDataHandler::Shutdown();
        kwiq_net::GlobalCleanup();
    }

    virtual void OnTimer(int Counter) override {
        CVCHPlugin::OnTimer(Counter);                       // VCH housekeeping
        CDataHandler::ApplyPending(this);                   // slot/feed results, main thread
        FlushMessages();

        std::time_t now = std::time(nullptr);
        if (!settingsLoaded_) {                              // remembered local sector file location
            settingsLoaded_ = true;
            const char* sp = GetDataFromSettings("kwiq_sectorfile_path");
            if (sp && *sp) { std::lock_guard<std::mutex> g(mu_); sectorPath_ = sp; }
        }
        CDataHandler::refreshInterval = slotsEvery_;
        std::string code;
        { std::lock_guard<std::mutex> g(mu_); code = ev_.active ? ev_.vatcanCode : ""; }
        if (CDataHandler::SetEventCode(code)) lastSlotFetch_ = 0;
        // Same trigger as the Slots-Plugin: fetch when more than refreshInterval seconds have passed.
        if (autoRefresh_ && !code.empty() && (now - lastSlotFetch_) > CDataHandler::refreshInterval) {
            lastSlotFetch_ = now;
            CDataHandler::StartFetch();
        }

        if (busy_) return;
        bool core = now - lastCore_ >= coreEvery_;
        bool srd  = now - lastSrd_  >= srdEvery_;
        if (!core && !srd) return;
        if (worker_.joinable()) worker_.join();
        if (core) lastCore_ = now;
        if (srd)  lastSrd_  = now;
        busy_ = true;
        worker_ = std::thread(&CKWIQPlugin::Refresh, this, core, srd);
    }

    virtual void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override {
        switch (FunctionId) {
        case KWIQ_FUNC_EVENT_ROUTES: OpenRoutesWindow(Pt); return;
        case KWIQ_FUNC_FPC_EXPLAIN:  ExplainSelected(); return;
        case KWIQ_FUNC_SRD_SEARCH:   OpenSrdWindow(Pt); return;
        }
        CVCHPlugin::OnFunctionCall(FunctionId, sItemString, Pt, Area);
    }

    virtual void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget,
                              int ItemCode, int TagData, char sItemString[16],
                              int* pColorCode, COLORREF* pRGB, double* pFontSize) override {
        using namespace kwiq;
        bool ours = ItemCode >= KWIQ_ITEM_EVENT_ROUTE && ItemCode <= KWIQ_ITEM_SECTOR;
        if (ours && !enabled_) return;                       // Core kill switch / plugin too old
        UiCfg ui;
        if (ours) { std::lock_guard<std::mutex> g(mu_); ui = ui_; }
        if (ItemCode == KWIQ_ITEM_EVENT_ROUTE || ItemCode == KWIQ_ITEM_FPC) {
            if (!FlightPlan.IsValid()) return;
            RouteResult ev; FpcResult fpc;
            Checks(FlightPlan, ev, fpc);
            const char* txt = nullptr; bool green = false;
            if (ItemCode == KWIQ_ITEM_EVENT_ROUTE) {
                if (ev == RouteResult::Ok) { txt = ui.ok.c_str(); green = true; }
                else if (ev == RouteResult::Check) txt = ui.chk.c_str();
            } else {
                switch (fpc) {
                case FpcResult::Ok:    txt = ui.ok.c_str(); green = true; break;
                case FpcResult::Route: txt = "RTE"; break;
                case FpcResult::Min:   txt = "MIN"; break;
                case FpcResult::Max:   txt = "MAX"; break;
                default: break;
                }
            }
            if (!txt) return;
            strcpy_s(sItemString, 16, txt);
            *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
            *pRGB = green ? ui.cOk : ui.cBad;
        }
        else if (ItemCode == KWIQ_ITEM_EVENT_FLAG || ItemCode == KWIQ_ITEM_EVENT_SLOT) {
            if (!FlightPlan.IsValid()) return;
            const ACData* ac = CDataHandler::Get(FlightPlan.GetCallsign());
            if (!ac || !ac->hasCTP) return;
            if (ItemCode == KWIQ_ITEM_EVENT_FLAG) {
                strncpy_s(sItemString, 16, ui.ev.c_str(), _TRUNCATE);
                *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
                *pRGB = ui.cEv;
            } else {
                strncpy_s(sItemString, 16, ac->slotTime.c_str(), _TRUNCATE);
            }
        }
        else if (ItemCode == KWIQ_ITEM_SECTOR) {
            if (!FlightPlan.IsValid()) return;
            const char* tracker = FlightPlan.GetTrackingControllerCallsign();
            if (!tracker || !*tracker) return;
            std::string alias = SectorFor(tracker);
            if (alias.empty()) return;
            strncpy_s(sItemString, 16, alias.c_str(), _TRUNCATE);
        }
        else {
            CVCHPlugin::OnGetTagItem(FlightPlan, RadarTarget, ItemCode, TagData, sItemString, pColorCode, pRGB, pFontSize);
        }
    }

    virtual bool OnCompileCommand(const char* sCommandLine) override {
        std::string c = kwiq::Upper(kwiq::Trim(sCommandLine));
        if (c == ".KWIQ REFRESH") {
            lastCore_ = lastSrd_ = lastSlotFetch_ = 0;
            { std::lock_guard<std::mutex> g(mu_); srd_.version.clear(); }   // force SRD re-download
            Msg("Refresh requested", false);
            return true;
        }
        if (c == ".KWIQ VERSION") {
            CheckSectorFile(true);
            return true;
        }
        if (c.compare(0, 16, ".KWIQ SECTORFILE") == 0) {
            std::string path = kwiq::Trim(std::string(sCommandLine).substr(16));
            { std::lock_guard<std::mutex> g(mu_); sectorPath_ = path; }
            SaveDataToSettings("kwiq_sectorfile_path", "KWIQ sector file folder or file", path.c_str());
            Msg(path.empty() ? "Sector file location: automatic" : "Sector file location: " + path, false);
            CheckSectorFile(true);
            return true;
        }
        if (c == ".KWIQ SECTOR") {
            sectorUserSet_ = true;
            sectorDraw_ = !sectorDraw_;
            Msg(std::string("Sector indicators on the radar: ") + (sectorDraw_ ? "on" : "off"), false);
            return true;
        }
        if (c == ".KWIQ AUTOREFRESH") {
            autoRefresh_ = !autoRefresh_;
            Msg(std::string("Slot auto refresh: ") + (autoRefresh_ ? "on" : "off"), false);
            return true;
        }
        if (c == ".KWIQ STATUS") {
            std::lock_guard<std::mutex> g(mu_);
            std::string s = "v" KWIQ_PLUGIN_VERSION " | " + std::string(enabled_ ? "enabled" : "DISABLED by Core") +
                " | sector file " + (upstreamAirac_.empty() ? "?" : upstreamAirac_) + " | SRD routes " + std::to_string(srd_.rows.size()) + " | sectors " +
                std::to_string(sectors_.size()) + " | ";
            s += ev_.active
                ? ev_.name + " | event routes " + std::to_string(ev_.routes.size()) +
                  " | VATCAN code " + ev_.vatcanCode + " | slots " + std::to_string(CDataHandler::SlotCount()) +
                  " | pilots " + std::to_string(CDataHandler::PilotCount())
                : "no active event";
            Msg(s, false);
            return true;
        }
        return CVCHPlugin::OnCompileCommand(sCommandLine);
    }

    // Sector indicator of the ORBB station tracking an aircraft; empty if none / not an ORBB station.
    std::string SectorFor(const char* trackerCallsign) {
        std::lock_guard<std::mutex> g(mu_);
        auto it = sectors_.find(trackerCallsign);
        return it == sectors_.end() ? std::string() : it->second;
    }
    bool SectorDrawEnabled() const { return sectorDraw_ && enabled_; }
    COLORREF SectorColour() { std::lock_guard<std::mutex> g(mu_); return ui_.cSector; }

    virtual EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent,
                                                                bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;

    // called from the SRD window's Search button (main thread)
    void RunSrdSearch(const std::string& adep, const std::string& ades, const std::string& lvl) {
        std::vector<kwiq::SrdRoute> rows;
        size_t total;
        {
            std::lock_guard<std::mutex> g(mu_);
            auto hits = kwiq::SrdSearch(srd_, adep, ades, kwiq::ParseLevelFt(lvl));
            total = hits.size();
            for (size_t i = 0; i < hits.size() && (int)i < kwiq_ui::SRD_ROWS; i++) rows.push_back(srd_.rows[hits[i]]);
        }
        kwiq_ui::SetSrdResults(rows, total);
    }

private:
    struct CacheEntry {
        bool valid = false;
        unsigned evGen = 0, srdGen = 0;
        int rfl = 0;
        std::string adep, ades, rte, today;
        kwiq::RouteResult ev = kwiq::RouteResult::None;
        kwiq::FpcResult fpc = kwiq::FpcResult::None;
    };

    // Draw-path checks, cached per callsign: only recomputed when the flight plan, the UTC day
    // or the Core data changed, so a redraw costs a few string compares and no allocation.
    void Checks(EuroScopePlugIn::CFlightPlan& fp, kwiq::RouteResult& ev, kwiq::FpcResult& fpc) {
        auto d = fp.GetFlightPlanData();
        std::string o = kwiq::Upper(d.GetOrigin()), a = kwiq::Upper(d.GetDestination()), r = d.GetRoute();
        int rfl = fp.GetFinalAltitude();
        const std::string& today = TodayCached();
        std::string cs = fp.GetCallsign();
        std::lock_guard<std::mutex> g(mu_);
        if (cache_.size() > 5000) cache_.clear();
        CacheEntry& e = cache_[cs];
        if (!(e.valid && e.evGen == evGen_ && e.srdGen == srdGen_ && e.rfl == rfl && e.today == today &&
              e.adep == o && e.ades == a && e.rte == r)) {
            auto toks = kwiq::NormaliseRoute(r);
            e.ev  = kwiq::CheckEventRoute(ev_, o, a, toks, today);
            e.fpc = kwiq::CheckFpc(srd_, o, a, toks, rfl);
            e.evGen = evGen_; e.srdGen = srdGen_; e.rfl = rfl; e.today = today;
            e.adep = o; e.ades = a; e.rte = r; e.valid = true;
        }
        ev = e.ev; fpc = e.fpc;
    }
    const std::string& TodayCached() {
        long day = (long)(std::time(nullptr) / 86400);
        if (day != todayIdx_) { todayIdx_ = day; todayStr_ = kwiq::TodayUtc(); }
        return todayStr_;
    }

    void OpenRoutesWindow(POINT pt) {
        std::string today = kwiq::TodayUtc(), name;
        std::vector<kwiq::EventRoute> rows;
        {
            std::lock_guard<std::mutex> g(mu_);
            name = ev_.active ? ev_.name : "No active event";
            rows = kwiq::RoutesForDate(ev_, today);
        }
        kwiq_ui::ShowEvent(pt, "KWIQ Event Routes - " + name + " - " + today + "Z", rows);
    }
    void OpenSrdWindow(POINT pt) {
        std::string adep, ades, lvl;
        auto fp = FlightPlanSelectASEL();
        if (fp.IsValid()) {
            adep = kwiq::Upper(fp.GetFlightPlanData().GetOrigin());
            ades = kwiq::Upper(fp.GetFlightPlanData().GetDestination());
            int rfl = fp.GetFinalAltitude();
            if (rfl > 0) lvl = std::to_string(rfl / 100);
        }
        kwiq_ui::ShowSrd(pt, adep, ades, lvl);
        if (!adep.empty() || !ades.empty()) RunSrdSearch(adep, ades, lvl);
    }
    void ExplainSelected() {
        auto fp = FlightPlanSelectASEL();
        if (!fp.IsValid()) return;
        auto d = fp.GetFlightPlanData();
        std::string msg;
        {
            std::lock_guard<std::mutex> g(mu_);
            msg = kwiq::ExplainFpc(srd_, kwiq::Upper(d.GetOrigin()), kwiq::Upper(d.GetDestination()),
                                   d.GetRoute(), fp.GetFinalAltitude());
        }
        DisplayUserMessage("KWIQ", fp.GetCallsign(), msg.c_str(), true, true, false, false, false);
    }

    void Msg(const std::string& m, bool err) {
        DisplayUserMessage("KWIQ", err ? "Error" : "Info", m.c_str(), true, err, false, false, false);
    }
    void FlushMessages() {
        std::vector<std::pair<std::string, bool>> out;
        { std::lock_guard<std::mutex> g(mu_); out.swap(pending_); }
        for (auto& m : out) Msg(m.first, m.second);
    }
    void Notify(const std::string& m, bool err) {          // any thread; shown on the next timer tick
        std::lock_guard<std::mutex> g(mu_);
        pending_.emplace_back(m, err);
    }
    // worker thread: report each distinct error once until the source recovers
    void SetError(const std::string& src, const std::string& msg) {
        std::lock_guard<std::mutex> g(mu_);
        if (lastErr_[src] == msg) return;
        lastErr_[src] = msg;
        pending_.emplace_back(src + ": " + msg, true);
    }
    void ClearError(const std::string& src) {
        std::lock_guard<std::mutex> g(mu_);
        lastErr_.erase(src);
    }

    // worker thread: apply what Core says (colours, texts, intervals, kill switch, notices, sector file)
    void ApplyConfig(const kwiq::PluginConfig& c) {
        using namespace kwiq;
        UiCfg u;
        u.ok  = c.Get("ui.text_ok", u.ok).substr(0, 15);
        u.chk = c.Get("ui.text_chk", u.chk).substr(0, 15);
        u.ev  = c.Get("ui.text_event_flag", u.ev).substr(0, 15);
        u.cOk = (COLORREF)ParseColour(c.Get("ui.colour_ok"), u.cOk);
        u.cBad = (COLORREF)ParseColour(c.Get("ui.colour_bad"), u.cBad);
        u.cEv = (COLORREF)ParseColour(c.Get("ui.colour_event"), u.cEv);
        u.cSector = (COLORREF)ParseColour(c.Get("ui.colour_sector"), u.cSector);
        coreEvery_  = c.GetInt("refresh.core_s", 60, 30, 3600);
        srdEvery_   = c.GetInt("refresh.srd_s", 300, 60, 86400);
        slotsEvery_ = c.GetInt("refresh.slots_s", 60, 15, 3600);
        if (!sectorUserSet_) sectorDraw_ = c.GetBool("ui.sector_enabled", true);

        std::string minV = c.Get("plugin.min_version", "0.0.0"), updUrl = c.Get("plugin.update_url");
        bool tooOld = CompareVersions(KWIQ_PLUGIN_VERSION, minV) < 0;
        bool on = c.GetBool("plugin.enabled", true) && !tooOld;
        bool wasOn = enabled_.exchange(on);
        if (tooOld && wasOn) Notify("Plugin v" KWIQ_PLUGIN_VERSION " is too old (minimum " + minV + "), disabled." +
                                    (updUrl.empty() ? "" : " Update: " + updUrl), true);
        else if (!on && wasOn) Notify("Disabled by the vACC (plugin.enabled = 0 in Core).", true);
        else if (on && !wasOn) Notify("Enabled again.", false);

        std::string motd = c.Get("plugin.message");
        {
            std::lock_guard<std::mutex> g(mu_);
            ui_ = u;
            upstreamAirac_ = c.Get("sectorfile.airac");
            sectorUrl_ = c.Get("sectorfile.url");
            if (!motd.empty() && motd != motd_) pending_.emplace_back(motd, false);
            motd_ = motd;
        }
        CheckSectorFile(false);
    }

    // Compare the user's local sector file against Core's upstream value. Messages once per distinct
    // (local, upstream) pair; force = user asked (.kwiq version / .kwiq sectorfile).
    void CheckSectorFile(bool force) {
        using namespace kwiq;
        std::string up, url, path;
        { std::lock_guard<std::mutex> g(mu_); up = upstreamAirac_; url = sectorUrl_; path = sectorPath_; }
        Airac u = FindAirac(up);
        if (!u.valid()) { if (force) Notify("Core has not published a sector file version yet.", false); return; }
        std::string src;
        Airac l = sf::Local(path, src);
        std::string key = (l.valid() ? l.Str() : std::string("?")) + ">" + u.Str();
        { std::lock_guard<std::mutex> g(mu_); if (!force && key == lastSfKey_) return; lastSfKey_ = key; }
        std::string tail = url.empty() ? "" : " Download: " + url;
        if (!l.valid())
            Notify("Cannot find your sector file version (current is " + u.Str() + "). Put AIRAC_VERSION.txt next to your "
                   ".ese, or use: .kwiq sectorfile <folder or .ese path>", false);
        else if (CompareAirac(l, u) < 0)
            Notify("SECTOR FILE OUT OF DATE: you have " + l.Str() + ", current is " + u.Str() + "." + tail, true);
        else if (CompareAirac(l, u) > 0)
            Notify("Sector file " + l.Str() + " is newer than Core's " + u.Str() + " (Core not updated yet?).", false);
        else
            Notify("Sector file " + l.Str() + " is up to date.", false);
    }

    // worker thread: Core event + SRD (slots and the data feed run in CDataHandler)
    void Refresh(bool doCore, bool doSrd) {
        try {
            using namespace kwiq;
            std::string body, err;

            if (doCore) {
                if (kwiq_net::HttpGet(CORE_CONFIG_URL, body, err)) {
                    ClearError("Config");
                    ApplyConfig(ParseConfig(body));
                } else SetError("Config", err);
                if (kwiq_net::HttpGet(CORE_EVENT_URL, body, err)) {
                    ClearError("Core");
                    EventData ev = ParseCoreEvent(body);
                    std::lock_guard<std::mutex> g(mu_);
                    ev_ = std::move(ev); evGen_++;
                } else SetError("Core", err);
            }

            if (doSrd) {                               // cheap version check, full download only on change
                if (kwiq_net::HttpGet(CORE_SRD_VERSION_URL, body, err)) {
                    std::string ver = Trim(body);
                    bool changed;
                    { std::lock_guard<std::mutex> g(mu_); changed = ver != srd_.version || srd_.rows.empty(); }
                    if (changed) {
                        if (kwiq_net::HttpGet(CORE_SRD_URL, body, err)) {
                            SrdData d = ParseSrd(body);
                            if (d.version.empty()) d.version = ver;
                            ClearError("SRD");
                            std::lock_guard<std::mutex> g(mu_);
                            srd_ = std::move(d); srdGen_++;
                        } else SetError("SRD", err);
                    } else ClearError("SRD");
                } else SetError("SRD", err);
            }
            if (doSrd) {                               // sector aliases: tiny, refreshed with the SRD
                if (kwiq_net::HttpGet(CORE_SECTORS_URL, body, err)) {
                    ClearError("Sectors");
                    SectorMap m = ParseSectors(body);
                    std::lock_guard<std::mutex> g(mu_);
                    sectors_ = std::move(m);
                } else SetError("Sectors", err);
            }
        } catch (...) {}
        busy_ = false;
    }

    std::mutex mu_;
    std::thread worker_;
    std::atomic<bool> busy_{ false };
    std::atomic<bool> sectorDraw_{ true }, autoRefresh_{ true }, sectorUserSet_{ false };
    std::atomic<bool> enabled_{ true };
    std::atomic<int> coreEvery_{ 60 }, srdEvery_{ 300 }, slotsEvery_{ 60 };
    bool settingsLoaded_ = false;                                   // main thread only
    std::time_t lastCore_ = 0, lastSrd_ = 0, lastSlotFetch_ = 0;   // main thread only
    long todayIdx_ = -1;                                            // main thread only
    std::string todayStr_;

    // guarded by mu_
    kwiq::EventData ev_;
    kwiq::SrdData srd_;
    kwiq::SectorMap sectors_;
    unsigned evGen_ = 1, srdGen_ = 1;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::vector<std::pair<std::string, bool>> pending_;
    UiCfg ui_;
    std::string upstreamAirac_, sectorUrl_, sectorPath_, motd_, lastSfKey_;
    std::map<std::string, std::string> lastErr_;
};

// Radar screen overlay: draws the sector indicator above the symbol of every aircraft that is
// tracked by an ORBB FIR station (NR / SR ...). Core decides which stations and which text.
class CKWIQRadar : public EuroScopePlugIn::CRadarScreen {
public:
    CKWIQRadar() {
        font_ = CreateFontA(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
    }
    virtual ~CKWIQRadar() { if (font_) DeleteObject(font_); }

    virtual void OnAsrContentToBeClosed(void) override { delete this; }

    virtual void OnRefresh(HDC hDC, int Phase) override {
        if (Phase != EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS || !gPlugin || !gPlugin->SectorDrawEnabled()) return;
        RECT area = GetRadarArea();
        int saved = SaveDC(hDC);
        SetBkMode(hDC, TRANSPARENT);
        SetTextColor(hDC, gPlugin->SectorColour());
        SetTextAlign(hDC, TA_CENTER | TA_BOTTOM);
        if (font_) SelectObject(hDC, font_);
        EuroScopePlugIn::CPlugIn* plugin = GetPlugIn();
        for (EuroScopePlugIn::CRadarTarget rt = plugin->RadarTargetSelectFirst(); rt.IsValid();
             rt = plugin->RadarTargetSelectNext(rt)) {
            if (rt.GetPosition().GetRadarFlags() == 0) continue;            // no radar contact
            EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
            if (!fp.IsValid()) continue;
            const char* tracker = fp.GetTrackingControllerCallsign();       // empty when nobody tracks it
            if (!tracker || !*tracker) continue;
            std::string alias = gPlugin->SectorFor(tracker);
            if (alias.empty()) continue;                                    // not an ORBB station
            POINT p = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());
            if (p.x < area.left || p.x > area.right || p.y < area.top || p.y > area.bottom) continue;
            TextOutA(hDC, p.x, p.y - 9, alias.c_str(), (int)alias.size());
        }
        RestoreDC(hDC, saved);
    }

private:
    HFONT font_ = nullptr;
};

EuroScopePlugIn::CRadarScreen* CKWIQPlugin::OnRadarScreenCreated(const char*, bool, bool, bool, bool) {
    return new CKWIQRadar();
}

static void SrdSearchThunk(const std::string& adep, const std::string& ades, const std::string& lvl) {
    if (gPlugin) gPlugin->RunSrdSearch(adep, ades, lvl);
}

// Entry points: declared (with dllexport) in EuroScopePlugIn.h, so no extern "C" here.
void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    *ppPlugInInstance = gPlugin = new CKWIQPlugin();
}
void __declspec(dllexport) EuroScopePlugInExit(void) {
    CKWIQPlugin* p = gPlugin;
    gPlugin = nullptr;
    delete p;
}
