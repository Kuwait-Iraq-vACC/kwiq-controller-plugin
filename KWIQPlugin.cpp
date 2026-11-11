// KWIQPlugin.cpp - Kuwait & Iraq vACC Controller Plugin
//
//   * VCH (Virtual Controller Helper, GPL-3.0, Jan Fries) is built in: CKWIQPlugin extends CVCHPlugin
//   * Event routes (OK / CHK) + "Open event routes window"
//   * Event Flag (EV) / Event Slot from VATCAN bookings (see VatsimAPI.cpp)
//   * SRD check against the SRD, "Explain SRD result", "Open SRD search window"; flights leaving the vACC's
//     airspace are checked up to their FIR exit point (as in the UK Controller Plugin)
//   * FPC: VFPC's flight plan check (SID, direction, levels, ...), "Explain FPC result" (see fpc_logic.hpp)
//   * RDF (Radio Direction Finder, GPL-3.0, Kingfu Chan) and its TrackAudio bridge are built in (rdf.cpp)
//   * DiscordEuroScope (Discord Rich Presence, GPL-3.0, Kirollos Nashaat) is built in (discord.cpp)
//
// All route data comes from Core (Laravel). License: GPL-3.0-or-later, see LICENSE and NOTICE.md.

#include "stdafx.h"
#include <mutex>
#include <thread>
#include <atomic>
#include <map>
#include <set>
#include <array>
#include <chrono>
#include <cstring>
#include <mmsystem.h>       // PlaySound (link winmm)
#include <shellapi.h>       // ShellExecute
#include <winver.h>         // GetFileVersionInfo (link version)
#include <commctrl.h>       // the SRD search results list (link comctl32)
#include "requestMenu.hpp"      // CVCHPlugin (VCH)
#include "kwiq_logic.hpp"
#include "fpc_logic.hpp"
#include "VatsimAPI.h"
#include "keylink.hpp"
#include "rdf.hpp"                // RDF and the TrackAudio bridge (KingfuChan/RDF), built in
#include "discord.hpp"            // DiscordEuroScope (Kirollos/DiscordEuroScope), built in
#include "push.hpp"               // Core's push events (Laravel Reverb), as UKCP gets its own
#define KWIQ_NO_WINHTTP
#include "kwiq_win.hpp"           // api-settings.json and the folders the loader uses
#include "kwiq_res.h"             // kwiq.rc: the KWIQ button's logo

#define KWIQ_PLUGIN_NAME      "KWIQ Controller Plugin"
#include "version.h"                // KWIQ_PLUGIN_VERSION
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
static const char* CORE_RDF_URL         = CORE_BASE "/rdf";
static const char* CORE_DISCORD_URL     = CORE_BASE "/discord";
static const char* CORE_TOPDOWN_ORDERS_URL     = CORE_BASE "/topdown";
static const char* CORE_TRL_URL         = CORE_BASE "/trl";
static const char* CORE_METAR_URL       = CORE_BASE "/metar";
static const char* CORE_NOTICES_URL     = CORE_BASE "/notifications";
static const char* CORE_FIR_EXITS_URL   = CORE_BASE "/fir-exits";
static const char* CORE_FPC_URL         = CORE_BASE "/fpc";
// myKWIQ page that hands the plugin its personal API key (see keylink.hpp)
static const char* MYKWIQ_CONNECT_URL   = "https://my.kuwait-iraqvacc.net/plugin/connect";
// EuroScope setup guide, opened from the incorrect EuroScope version popup until Core sends euroscope.setup_url
static const char* ES_SETUP_URL         = "https://docs.kuwait-iraqvacc.net/Controller%20Guidance/Software/EuroScope%20Setup/";
// ---------------------------------------------------------------------------

// Tag item / function ids. VCH uses 100, 340-346, 440-447, 540-556, 640-650: keep clear of those.
enum {
    KWIQ_ITEM_EVENT_ROUTE = 1001,
    KWIQ_ITEM_EVENT_FLAG  = 1002,
    KWIQ_ITEM_EVENT_SLOT  = 1003,
    KWIQ_ITEM_FPC         = 1004,   // flight plan check (VFPC)
    KWIQ_ITEM_SECTOR      = 1005,   // sector indicator of the position that tracks the aircraft
    KWIQ_ITEM_RDF         = 1006,   // RDF state: "!" while the aircraft transmits
    KWIQ_ITEM_SRD         = 1007,   // route and level against the SRD (1006 is RDF state)
    KWIQ_ITEM_FPC_FAILED  = 1008,   // the flight plan check, only when it fails
    KWIQ_ITEM_LAST        = KWIQ_ITEM_FPC_FAILED,
    KWIQ_FUNC_EVENT_ROUTES = 2001,   // "Open event routes window"
    KWIQ_FUNC_FPC_EXPLAIN  = 2002,   // "Explain FPC result"
    KWIQ_FUNC_SRD_SEARCH   = 2003,   // "Open SRD search window"
    KWIQ_FUNC_SRD_EXPLAIN  = 2010,   // "Explain SRD result"
    // the KWIQ button on the radar screen and its menu
    KWIQ_OBJ_BUTTON        = 2100,
    KWIQ_MENU_KEY          = 2101,
    KWIQ_MENU_SECTORS      = 2102,
    KWIQ_MENU_RDF          = 2103,   // RDF drawing on or off
    KWIQ_MENU_AUTOREFRESH  = 2104,
    KWIQ_MENU_EVENT_ROUTES = 2105,
    KWIQ_MENU_SRD          = 2106,
    KWIQ_MENU_SECTORFILE   = 2107,
    KWIQ_MENU_UPDATE       = 2108,
    KWIQ_MENU_REFRESH      = 2109,
    KWIQ_MENU_STATUS       = 2110,
    KWIQ_MENU_HIDE         = 2111,
    KWIQ_MENU_BRIDGE       = 2112,   // TrackAudio's connection (a status row: a click resyncs or reconnects)
    KWIQ_MENU_DISCORD      = 2113,   // DiscordEuroScope on or off
    KWIQ_MENU_NOTICES      = 2114,   // procedure notifications window
    KWIQ_MENU_LAST         = 2114
};

// ---------------------------------------------------------------------------
// Windows (plain Win32, no MFC): event routes list (Copy button per row) and SRD search
// ---------------------------------------------------------------------------
namespace kwiq_ui {

static const char* const kClass = "KWIQPluginWnd";
enum { ID_SEARCH = 2900, ID_SRD_COPY = 2901, ID_SRD_CLOSE = 2902, ID_SRD_LIST = 2903, ID_COPY_BASE = 3000,
       MAX_ROWS = 24, SRD_MAX_RESULTS = 500 };
static const int M = 10, LBL = 170, EDT = 460, BTN = 70, GAP = 6, ROWH = 24, ROWGAP = 6;
static const int CW = M + LBL + GAP + EDT + GAP + BTN + M;   // client width

struct Win {
    HWND hwnd = nullptr;
    std::vector<std::string> copy;
    std::vector<HWND> btns, lbls, edits;
    HWND eAdep = nullptr, eAdes = nullptr, eLvl = nullptr;
    HWND list = nullptr, notes = nullptr, copyBtn = nullptr;       // SRD search
    std::vector<kwiq::SrdRoute> results;
    int selected = -1;
};
static Win gEvt, gSrd;
static HMODULE gMod = nullptr;
static bool gClassReg = false;
static void (*gOnSearch)(const std::string&, const std::string&, const std::string&) = nullptr;

static Win* WinOf(HWND h) { return (h && h == gEvt.hwnd) ? &gEvt : (h && h == gSrd.hwnd) ? &gSrd : nullptr; }
static std::string TextOf(HWND h) { char b[256]; GetWindowTextA(h, b, sizeof b); return b; }
static void SetSrdResults(const std::vector<kwiq::SrdRoute>& rows);

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
        if (id == ID_SEARCH && win == &gSrd) {
            std::string adep = kwiq::Trim(TextOf(win->eAdep)), ades = kwiq::Trim(TextOf(win->eAdes));
            if (adep.empty() || ades.empty()) {               // both are required, as in UKCP
                SetSrdResults({});
                SetWindowTextA(win->notes, "Enter both the origin and the destination (a fix or an airfield).");
            } else if (gOnSearch) gOnSearch(adep, ades, TextOf(win->eLvl));
        } else if (id == ID_SRD_COPY && win == &gSrd) {
            if (win->selected >= 0 && win->selected < (int)win->results.size()) {
                CopyToClipboard(h, win->results[win->selected].rte);
                SetWindowTextA(win->copyBtn, "Copied");
            }
        } else if (id == ID_SRD_CLOSE) {
            DestroyWindow(h);
        } else if (id >= ID_COPY_BASE && id - ID_COPY_BASE < (int)win->copy.size()) {
            CopyToClipboard(h, win->copy[id - ID_COPY_BASE]);
            for (HWND b : win->btns) SetWindowTextA(b, "Copy");
            SetWindowTextA((HWND)l, "Copied");
        }
        return 0;
    }
    case WM_NOTIFY: {
        // a route picked in the SRD search results: its notes, and it can be copied
        NMHDR* nh = (NMHDR*)l;
        if (h != gSrd.hwnd || nh->idFrom != ID_SRD_LIST || nh->code != LVN_ITEMCHANGED) return 0;
        NMLISTVIEW* nl = (NMLISTVIEW*)l;
        if (!(nl->uNewState & LVIS_SELECTED) || (nl->uOldState & LVIS_SELECTED)) return 0;
        if (nl->iItem < 0 || nl->iItem >= (int)gSrd.results.size()) return 0;
        gSrd.selected = nl->iItem;
        SetWindowTextA(gSrd.notes, kwiq::SrdNotesText(gSrd.results[nl->iItem]).c_str());
        SetWindowTextA(gSrd.copyBtn, "Copy Route String To Clipboard");
        EnableWindow(gSrd.copyBtn, TRUE);
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

// The work area of the screen EuroScope is on (its largest visible window), so windows open in the
// middle of that screen rather than the main one
static BOOL CALLBACK LargestOwnWindow(HWND h, LPARAM l) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    RECT r;
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || !GetWindowRect(h, &r)) return TRUE;
    auto* best = (std::pair<HWND, long long>*)l;
    long long area = (long long)(r.right - r.left) * (r.bottom - r.top);
    if (area > best->second) *best = { h, area };
    return TRUE;
}
static RECT EuroScopeScreen() {
    std::pair<HWND, long long> best = { nullptr, 0 };
    EnumWindows(LargestOwnWindow, (LPARAM)&best);
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    HMONITOR mon = best.first ? MonitorFromWindow(best.first, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoA(mon, &mi)) return mi.rcWork;
    RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    return wa;
}
static void Centre(int ww, int wh, int& x, int& y) {
    RECT wa = EuroScopeScreen();
    x = wa.left + ((wa.right - wa.left) - ww) / 2;
    y = wa.top + ((wa.bottom - wa.top) - wh) / 2;
}

static HWND Child(const char* cls, const char* text, DWORD style, int x, int y, int w, int h,
                  HWND parent, int id = 0) {
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                             (HMENU)(INT_PTR)id, gMod, nullptr);
    SendMessageA(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

// Creates the popup, stores it in `win`, destroying any previous one of the same kind.
static bool Make(Win& win, POINT, const std::string& title, int ch, int cw = CW) {
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
    RECT rc = { 0, 0, cw, ch };
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int x, y;
    Centre(ww, wh, x, y);                    // in the middle of EuroScope's screen

    HWND h = CreateWindowExA(ex, kClass, title.c_str(), style, x, y, ww, wh, nullptr, nullptr, gMod, nullptr);
    if (!h) return false;
    win = Win();
    win.hwnd = h;
    return true;
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

// SRD search, laid out as the UK Controller Plugin's SRD Search dialog (src/plugin/srd/SrdSearchDialog.cpp,
// IDD_SRD_SEARCH): origin, destination and level, Search, the results (Min, Max, FRA, Route String), the
// selected route's notes, Copy Route String To Clipboard and Close. Not modal, so EuroScope keeps running.
static const int SRD_W = 600, SRD_H = 528;
static WNDPROC gEditProc = nullptr;
// In the three boxes: Enter searches, Tab moves on, Esc closes (what the dialog manager does for UKCP's)
static LRESULT CALLBACK SrdEditProc(HWND e, UINT m, WPARAM w, LPARAM l) {
    HWND parent = GetParent(e);
    if (m == WM_KEYDOWN && w == VK_RETURN) { PostMessageA(parent, WM_COMMAND, MAKEWPARAM(ID_SEARCH, BN_CLICKED), 0); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { PostMessageA(parent, WM_CLOSE, 0, 0); return 0; }
    if (m == WM_KEYDOWN && w == VK_TAB) {
        HWND next = GetNextDlgTabItem(parent, e, GetKeyState(VK_SHIFT) < 0);
        if (next) SetFocus(next);
        return 0;
    }
    if (m == WM_CHAR && (w == '\r' || w == '\t' || w == 27)) return 0;   // no beep
    return CallWindowProcA(gEditProc, e, m, w, l);
}
static void AddColumn(HWND list, int i, const char* text, int width) {
    LVCOLUMNA c = {};
    c.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    c.fmt = LVCFMT_LEFT;
    c.cx = width;
    c.pszText = (LPSTR)text;
    c.iSubItem = i;
    SendMessageA(list, LVM_INSERTCOLUMNA, (WPARAM)i, (LPARAM)&c);
}
static void SetCell(HWND list, int row, int col, const std::string& text) {
    LVITEMA it = {};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = col;
    it.pszText = (LPSTR)text.c_str();
    SendMessageA(list, col == 0 ? LVM_INSERTITEMA : LVM_SETITEMA, 0, (LPARAM)&it);
}

static void ShowSrd(POINT pt, const std::string& adep, const std::string& ades, const std::string& lvl) {
    static bool commonControls = false;
    if (!commonControls) {
        INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_LISTVIEW_CLASSES };
        commonControls = InitCommonControlsEx(&ic) != FALSE;
    }
    if (!Make(gSrd, pt, "SRD Search", SRD_H, SRD_W)) return;
    HWND h = gSrd.hwnd;
    const int L = 180, E = 130, EH = 22, W = SRD_W - 2 * M;
    const DWORD edit = WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    Child("STATIC", "Origin Fix or Airfield*", SS_LEFT, M, 13, L, 16, h);
    gSrd.eAdep = Child("EDIT", adep.c_str(), edit | ES_UPPERCASE, M + L, 10, E, EH, h);
    Child("STATIC", "Destination Fix or Airfield*", SS_LEFT, M, 41, L, 16, h);
    gSrd.eAdes = Child("EDIT", ades.c_str(), edit | ES_UPPERCASE, M + L, 38, E, EH, h);
    Child("STATIC", "Cruising Altitude or Level", SS_LEFT, M, 69, L, 16, h);
    gSrd.eLvl = Child("EDIT", lvl.c_str(), edit | ES_NUMBER, M + L, 66, E, EH, h);
    Child("STATIC", "* Required Field", SS_LEFT, M + L + E + 60, 41, 140, 16, h);
    Child("BUTTON", "Search", BS_DEFPUSHBUTTON | WS_TABSTOP, M + L, 96, 100, 26, h, ID_SEARCH);

    Child("STATIC", "Search Results", SS_CENTER, M, 132, W, 16, h);
    gSrd.list = Child(WC_LISTVIEWA, "", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                      M, 150, W, 160, h, ID_SRD_LIST);
    SendMessageA(gSrd.list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    AddColumn(gSrd.list, 0, "Min", 50);
    AddColumn(gSrd.list, 1, "Max", 50);
    AddColumn(gSrd.list, 2, "FRA", 50);
    AddColumn(gSrd.list, 3, "Route String", 900);

    Child("STATIC", "Notes", SS_CENTER, M, 318, W, 16, h);
    gSrd.notes = Child("EDIT", "", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, M, 336, W, 146, h);
    gSrd.copyBtn = Child("BUTTON", "Copy Route String To Clipboard", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED,
                         M, 492, 230, 26, h, ID_SRD_COPY);
    Child("BUTTON", "Close", BS_PUSHBUTTON | WS_TABSTOP, SRD_W - M - 100, 492, 100, 26, h, ID_SRD_CLOSE);

    for (HWND e : { gSrd.eAdep, gSrd.eAdes, gSrd.eLvl })
        gEditProc = (WNDPROC)SetWindowLongPtrA(e, GWLP_WNDPROC, (LONG_PTR)&SrdEditProc);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(adep.empty() ? gSrd.eAdep : ades.empty() ? gSrd.eAdes : gSrd.eLvl);
}

// Every result; "No routes found" when there are none (as UKCP)
static void SetSrdResults(const std::vector<kwiq::SrdRoute>& rows) {
    if (!gSrd.hwnd) return;
    SendMessageA(gSrd.list, LVM_DELETEALLITEMS, 0, 0);
    gSrd.results = rows;
    gSrd.selected = -1;
    SetWindowTextA(gSrd.notes, "");
    SetWindowTextA(gSrd.copyBtn, "Copy Route String To Clipboard");
    EnableWindow(gSrd.copyBtn, FALSE);
    if (rows.empty()) {
        SetCell(gSrd.list, 0, 0, "0");
        SetCell(gSrd.list, 0, 1, "0");
        SetCell(gSrd.list, 0, 2, "0");
        SetCell(gSrd.list, 0, 3, "No routes found");
        return;
    }
    for (int i = 0; i < (int)rows.size(); i++) {
        SetCell(gSrd.list, i, 0, kwiq::SrdMinText(rows[i]));
        SetCell(gSrd.list, i, 1, kwiq::SrdMaxText(rows[i]));
        SetCell(gSrd.list, i, 2, kwiq::HasFreeRoute(rows[i].rte) ? "Yes" : "No");
        SetCell(gSrd.list, i, 3, rows[i].rte);
    }
}

// ---- Message boxes ---------------------------------------------------------------------------------
// Every popup is a plain Windows message box, shown the way the UK Controller Plugin shows its own
// (kwiq_win::Box: MessageBoxW, owned by EuroScope's active window, on EuroScope's thread) (#55).
// The one exception is the API key setup: it waits while the controller signs in to myKWIQ, up to the
// 15 minutes the listener waits, so it runs on a thread of its own and EuroScope keeps going. It is
// not owned by EuroScope, so MB_SETFOREGROUND | MB_TOPMOST keep it in front. OK opens the page again
// and the box comes back until the key arrives; the plugin then closes it, so it can't be dismissed (#18).
class KeySetupBox {
public:
    ~KeySetupBox() { Close(); }
    bool Open() const { return running_; }
    void Show() {
        if (running_) return;
        if (thread_.joinable()) thread_.join();
        closing_ = false;
        reopen_ = false;
        running_ = true;
        thread_ = std::thread(&KeySetupBox::Run, this);
    }
    // True once after each OK: the plugin opens myKWIQ's page again (main thread)
    bool TakeReopen() { return reopen_.exchange(false); }
    // Closes the box (OK, as if clicked) and waits for its thread, which takes a few milliseconds
    void Close() {
        closing_ = true;
        while (running_) {                       // the box may not be up yet: ask until the thread ends
            DWORD id = threadId_;
            if (id) EnumThreadWindows(id, CloseBox, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    static BOOL CALLBACK CloseBox(HWND h, LPARAM) { PostMessageW(h, WM_COMMAND, IDOK, 0); return TRUE; }
    void Run() {
        threadId_ = GetCurrentThreadId();
        try {
            while (!closing_) {
                MessageBoxW(nullptr,
                            L"Sign in to myKWIQ in your browser and click Connect EuroScope. The link is also on your "
                            L"clipboard.\r\n\r\nThis message closes once your API key is connected. Select OK to open the "
                            L"page again.\r\n\r\nNeed help? discord.kuwait-iraqvacc.net",
                            L"KWIQ API Key Setup", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
                if (!closing_) reopen_ = true;
            }
        } catch (...) {}
        running_ = false;
    }
    std::thread thread_;
    std::atomic<bool> running_{ false }, closing_{ false }, reopen_{ false };
    std::atomic<DWORD> threadId_{ 0 };
};
static KeySetupBox gKeyBox;

static void ShowKeyPrompt() { gKeyBox.Show(); }
static bool KeyPromptOpen() { return gKeyBox.Open(); }
static bool KeyPromptReopen() { return gKeyBox.TakeReopen(); }
static void CloseKeyPrompt() { gKeyBox.Close(); }

// ---- Procedure notifications window (KWIQ menu > Notifications) --------------------------------
// The list on the left (unread first marked *), the selected one's text on the right, and Mark read /
// Mark unread and Open link below. Read state is kept in Core per API key.
static const char* const kNoticesClass = "KWIQNoticesWnd";
static bool gNoticesReg = false;
enum { NT_W = 720, NT_H = 420, NT_LIST = 250, ID_NT_LIST = 3500, ID_NT_READ = 3501, ID_NT_LINK = 3502, ID_NT_CLOSE = 3503 };
struct NoticesWin {
    HWND hwnd = nullptr, list = nullptr, title = nullptr, text = nullptr, bRead = nullptr, bLink = nullptr;
    std::vector<kwiq::ProcedureNotice> items;
    std::string selId;
};
static NoticesWin gNotices;
static void (*gOnMarkRead)(const std::string& id, bool read) = nullptr;

static void NoticesShowSelected() {
    NoticesWin& w = gNotices;
    int i = (int)SendMessageA(w.list, LB_GETCURSEL, 0, 0);
    const kwiq::ProcedureNotice* n = i >= 0 && i < (int)w.items.size() ? &w.items[i] : nullptr;
    w.selId = n ? n->id : "";
    SetWindowTextA(w.title, n ? (n->title + (n->published.empty() ? "" : "   (" + n->published + ")")).c_str() : "No notification selected.");
    SetWindowTextA(w.text, n ? n->text.c_str() : "");
    SetWindowTextA(w.bRead, n && n->read ? "Mark unread" : "Mark read");
    EnableWindow(w.bRead, n != nullptr);
    EnableWindow(w.bLink, n && kwiq::IsWebLink(n->link));         // only a web page is ever opened (#44)
}
static void NoticesFill() {
    NoticesWin& w = gNotices;
    if (!w.hwnd) return;
    SendMessageA(w.list, LB_RESETCONTENT, 0, 0);
    int sel = w.items.empty() ? -1 : 0;
    for (size_t i = 0; i < w.items.size(); i++) {
        std::string row = std::string(w.items[i].read ? "   " : "*  ") + w.items[i].title;
        SendMessageA(w.list, LB_ADDSTRING, 0, (LPARAM)row.c_str());
        if (w.items[i].id == w.selId) sel = (int)i;
    }
    if (w.items.empty()) SendMessageA(w.list, LB_ADDSTRING, 0, (LPARAM)"   No notifications for your position.");
    SendMessageA(w.list, LB_SETCURSEL, w.items.empty() ? -1 : sel, 0);
    NoticesShowSelected();
}
static LRESULT CALLBACK NoticesProc(HWND h, UINT m, WPARAM wp, LPARAM l) {
    NoticesWin& w = gNotices;
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_NT_LIST && HIWORD(wp) == LBN_SELCHANGE) NoticesShowSelected();
        else if (id == ID_NT_CLOSE) DestroyWindow(h);
        else if (HIWORD(wp) == BN_CLICKED) {
            for (auto& n : w.items) {
                if (n.id != w.selId) continue;
                if (id == ID_NT_LINK && kwiq::IsWebLink(n.link)) ShellExecuteA(nullptr, "open", n.link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (id == ID_NT_READ) {
                    n.read = !n.read;                    // shown at once; Core is told in the background
                    if (gOnMarkRead) gOnMarkRead(n.id, n.read);
                    NoticesFill();
                }
                break;
            }
        }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: if (h == w.hwnd) { w.hwnd = nullptr; w.list = w.title = w.text = w.bRead = w.bLink = nullptr; } return 0;
    }
    return DefWindowProcA(h, m, wp, l);
}
static void ShowNotices(const std::vector<kwiq::ProcedureNotice>& items) {
    if (!gMod)
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&NoticesProc, &gMod);
    if (!gNoticesReg) {
        WNDCLASSEXA wc; ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = NoticesProc;
        wc.hInstance = gMod;
        wc.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512));
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kNoticesClass;
        if (!RegisterClassExA(&wc)) return;
        gNoticesReg = true;
    }
    gNotices.items = items;
    if (gNotices.hwnd) { NoticesFill(); ShowWindow(gNotices.hwnd, SW_SHOW); SetForegroundWindow(gNotices.hwnd); return; }
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    RECT rc = { 0, 0, NT_W, NT_H };
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top, x, y;
    Centre(ww, wh, x, y);
    HWND h = CreateWindowExA(ex, kNoticesClass, "KWIQ Notifications", style, x, y, ww, wh, nullptr, nullptr, gMod, nullptr);
    if (!h) return;
    NoticesWin& w = gNotices;
    w.hwnd = h;
    int bottom = NT_H - M - ROWH;
    w.list = Child("LISTBOX", "", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, M, M, NT_LIST, bottom - 2 * M, h, ID_NT_LIST);
    int tx = M + NT_LIST + M, tw = NT_W - tx - M;
    w.title = Child("STATIC", "", SS_LEFT | SS_ENDELLIPSIS, tx, M + 2, tw, ROWH - 4, h);
    w.text = Child("EDIT", "", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, tx, M + ROWH, tw, bottom - 2 * M - ROWH, h);
    w.bRead = Child("BUTTON", "Mark read", BS_PUSHBUTTON, tx, bottom, 110, ROWH, h, ID_NT_READ);
    w.bLink = Child("BUTTON", "Open link", BS_PUSHBUTTON, tx + 110 + GAP, bottom, 110, ROWH, h, ID_NT_LINK);
    Child("BUTTON", "Close", BS_PUSHBUTTON, NT_W - M - 90, bottom, 90, ROWH, h, ID_NT_CLOSE);
    NoticesFill();
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
}
// New data from Core while the window is open: keep the selection, update the rest
static void UpdateNotices(const std::vector<kwiq::ProcedureNotice>& items) {
    if (!gNotices.hwnd) return;
    gNotices.items = items;
    NoticesFill();
}

static void Shutdown() {
    if (gNotices.hwnd) DestroyWindow(gNotices.hwnd);
    if (gNoticesReg && gMod) { UnregisterClassA(kNoticesClass, gMod); gNoticesReg = false; }
    gKeyBox.Close();                                     // its thread must end before the plugin does
    if (gEvt.hwnd) DestroyWindow(gEvt.hwnd);
    if (gSrd.hwnd) DestroyWindow(gSrd.hwnd);
    if (gClassReg && gMod) { UnregisterClassA(kClass, gMod); gClassReg = false; }
}

} // namespace kwiq_ui

class CKWIQPlugin;
static CKWIQPlugin* gPlugin = nullptr;
// How the loader starts the plugin (kwiq::StartMode). Reduced, after the controller declined an update:
// only the outdated sector file alert, the EuroScope version check and a reminder in the chat run.
static int gStartMode = (int)kwiq::StartMode::Full;
static const char* const kReducedReminder =
    "This version of the KWIQ Controller Plugin is out of date, so only the outdated sector file alert and the EuroScope "
    "version check are working. Restart EuroScope and accept the update to use everything else.";

// ---- Local sector file version -------------------------------------------------
// EuroScope does not expose the sector file version to plugins, so it is read from disk, and only ever
// from AIRAC_VERSION.txt (e.g. "2610/02"), which each package keeps in its Plugins folder. It is looked
// for in the folder set with ".kwiq sectorfile <folder|file>", else the plugin's folder and its parents,
// each with its Plugins folder.
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
    // The packages keep it in <package>\Plugins\AIRAC_VERSION.txt
    for (const std::string& f : { dir + "\\AIRAC_VERSION.txt", dir + "\\Plugins\\AIRAC_VERSION.txt" }) {
        kwiq::Airac a = kwiq::FindAirac(ReadHead(f, 256));
        if (a.valid()) { src = f; return a; }
    }
    return {};
}
static std::string ParentOf(const std::string& p) {
    size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? std::string() : p.substr(0, i);
}
// Where the plugin is installed: KWIQPlugin.dll, the loader EuroScope loads from the sector file package
// (this file is kept in %LOCALAPPDATA%\KWIQ\bin), or this file when it is loaded directly
static std::string DllPath() {
    HMODULE m = GetModuleHandleA("KWIQPlugin.dll");
    if (!m)
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&DllPath, &m);
    char b[MAX_PATH] = {};
    if (!m || !GetModuleFileNameA(m, b, MAX_PATH)) return "";
    return b;
}
static std::string DllDir() { return ParentOf(DllPath()); }

// ---- alert sound lookup ----------------------------------------------------------------------
// Packages look like  %APPDATA%\EuroScope\ORBB-2610_02\ORBB\Sounds\conflict_alert.wav, but the folder names
// change per release and per vACC, so search instead of assuming, but only in the controller's own package:
// near the sector file first, then near KWIQPlugin.dll (which the package carries). Not found: a system sound.
static bool FileExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::vector<std::string> SubDirs(const std::string& dir) {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') out.push_back(dir + "\\" + fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}
static std::string Probe(const std::string& dir, const std::string& file) {   // dir\Sounds\f, dir\f, dir\*\Sounds\f
    if (FileExists(dir + "\\Sounds\\" + file)) return dir + "\\Sounds\\" + file;
    if (FileExists(dir + "\\" + file)) return dir + "\\" + file;
    for (const auto& sub : SubDirs(dir))
        if (FileExists(sub + "\\Sounds\\" + file)) return sub + "\\Sounds\\" + file;
    return "";
}
static std::string FindSound(const std::string& file, const std::vector<std::string>& starts) {
    if (file.empty()) return "";
    for (const auto& s : starts) {
        std::string d = s;
        for (int up = 0; up < 4 && !d.empty(); up++, d = ParentOf(d)) {
            std::string r = Probe(d, file);
            if (!r.empty()) return r;
        }
    }
    return "";
}
static kwiq::Airac Local(const std::string& userPath, std::string& src) {
    if (!userPath.empty()) {
        // A folder, AIRAC_VERSION.txt itself, or any file in the package's folder (the .ese, say)
        return FromDir(IsDir(userPath) ? userPath : ParentOf(userPath), src);
    }
    std::string d = DllDir();
    for (int i = 0; i < 4 && !d.empty(); i++, d = ParentOf(d)) {
        kwiq::Airac a = FromDir(d, src);
        if (a.valid()) return a;
    }
    return {};
}
} // namespace sf

// ---- personal API key ------------------------------------------------------------------------
// Generated on myKWIQ and kept in %LOCALAPPDATA%\KWIQ\settings\api-settings.json, as UKCP keeps its own,
// outside the EuroScope profile files, so it is never shared along with a sector file package or settings.
namespace apikey {
static std::string Load() { return kwiq_win::LoadApiKey(); }
static bool Save(const std::string& key) { return kwiq_win::SaveApiKey(key); }
} // namespace apikey

// The running EuroScope's version from its .exe, e.g. "3.2.3.2"; empty if it cannot be read
static std::string EuroScopeVersion() {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return "";
    DWORD unused = 0, size = GetFileVersionInfoSizeA(exe, &unused);
    if (!size) return "";
    std::vector<char> buf(size);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!GetFileVersionInfoA(exe, 0, size, buf.data()) || !VerQueryValueA(buf.data(), "\\", (void**)&fi, &len) ||
        !fi || len < sizeof(*fi)) return "";
    return std::to_string(HIWORD(fi->dwFileVersionMS)) + "." + std::to_string(LOWORD(fi->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(fi->dwFileVersionLS)) + "." + std::to_string(LOWORD(fi->dwFileVersionLS));
}

// Values Core controls (defaults apply until the first config arrives or if Core is unreachable).
struct UiCfg {
    std::string ok = "OK", chk = "CHK", ev = "EV";
    COLORREF cOk = RGB(0, 190, 0), cBad = RGB(230, 30, 30), cEv = RGB(0, 170, 255), cSector = RGB(255, 255, 255);
    COLORREF cWarn = RGB(220, 220, 0);                               // FPC: only a forbidden level (VFPC's yellow)
    std::map<std::string, COLORREF> cStates;                         // sector indicator per tag state (SectorColourName)
    // outdated sector file alert
    bool alertOn = true;
    int flashSec = 10;
    COLORREF alertA = RGB(211, 47, 47), alertB = RGB(74, 14, 14);   // the outdated sector file box flashes between these
    std::string sound = "conflict_alert.wav";
};
// Result of comparing the local sector file with Core's current version.
struct SfStatus {
    bool known = false, outdated = false;
    std::string package, local, upstream, url, sound;               // package = OKAC or ORBB; sound = resolved wav path or empty
};
static void SrdSearchThunk(const std::string& adep, const std::string& ades, const std::string& lvl);

class CKWIQPlugin : public CVCHPlugin {
public:
    CKWIQPlugin()
        : CVCHPlugin(KWIQ_PLUGIN_NAME, KWIQ_PLUGIN_VERSION, KWIQ_PLUGIN_AUTHOR, KWIQ_PLUGIN_COPYRIGHT) {
        // Loaded twice (two copies in Other Set > Plug-ins) every message, check and button would appear twice:
        // the second copy stays idle and says so
        std::string lock = "Local\\KWIQPlugin-" + std::to_string(GetCurrentProcessId());
        instanceLock_ = CreateMutexA(nullptr, FALSE, lock.c_str());
        duplicate_ = instanceLock_ && GetLastError() == ERROR_ALREADY_EXISTS;
        kwiq_net::GlobalInit();
        RegisterTagItemType("Event Route", KWIQ_ITEM_EVENT_ROUTE);
        RegisterTagItemType("Event Flag",  KWIQ_ITEM_EVENT_FLAG);
        RegisterTagItemType("Event Slot",  KWIQ_ITEM_EVENT_SLOT);
        RegisterTagItemType("FPC",         KWIQ_ITEM_FPC);
        RegisterTagItemType("FPC (if failed)", KWIQ_ITEM_FPC_FAILED);
        RegisterTagItemType("SRD",         KWIQ_ITEM_SRD);
        RegisterTagItemType("Sector Indicator", KWIQ_ITEM_SECTOR);
        RegisterTagItemType("RDF state",   KWIQ_ITEM_RDF);
        RegisterTagItemFunction("Open event routes window", KWIQ_FUNC_EVENT_ROUTES);
        RegisterTagItemFunction("Explain FPC result",       KWIQ_FUNC_FPC_EXPLAIN);
        RegisterTagItemFunction("Explain SRD result",       KWIQ_FUNC_SRD_EXPLAIN);
        RegisterTagItemFunction("Open SRD search window",   KWIQ_FUNC_SRD_SEARCH);
        kwiq_ui::gOnSearch = &SrdSearchThunk;
        apiKey_ = apikey::Load();
        kwiq_net::SetCoreAuth(CORE_BASE "/", apiKey_, KWIQ_PLUGIN_VERSION);
        // myKWIQ's one-off code, swapped for the key with Core on the listener's thread
        keyLink_.Claim = [](const std::string& code) {
            std::string body, err;
            if (!kwiq_net::HttpGet(std::string(CORE_BASE "/claim/") + code, body, err)) return std::string();
            std::string t = kwiq::Trim(body);
            return t.compare(0, 4, "KEY|") == 0 ? kwiq::Trim(t.substr(4)) : std::string();
        };
        startedAt_ = std::time(nullptr);
        esVersion_ = EuroScopeVersion();
        if (duplicate_) {
            Msg("The KWIQ plugin is loaded twice, so this copy (" + sf::DllPath() + ") will do nothing. "
                "Remove one of them in Other Set > Plug-ins.", true);
            return;
        }
        Msg(std::string("KWIQ plugin v") + KWIQ_PLUGIN_VERSION + " loaded", false);
        if (reduced_) { Msg(kReducedReminder, true); return; }
        std::string why;
        if (!rdf_.Start(why)) Msg(why, true);
        // EuroScope should load KWIQPlugin.dll, which updates and then loads this file; loaded directly,
        // nothing updates it
        if (!GetModuleHandleA("KWIQPlugin.dll"))
            Msg("This copy of the plugin cannot update itself. Please download it again from myKWIQ (EuroScope Plugin) "
                "and load KWIQPlugin.dll.", true);
    }
    virtual ~CKWIQPlugin() {
        kwiq_ui::gOnSearch = nullptr;
        kwiq_ui::Shutdown();
        keyLink_.Stop();
        rdf_.Stop();                                         // TrackAudio's thread uses curl and this plugin (#47)
        if (soundLooping_) PlaySoundA(nullptr, nullptr, 0);
        quit_ = true;                                        // aborts a running update download
        if (worker_.joinable()) worker_.join();
        if (auxWorker_.joinable()) auxWorker_.join();
        if (markWorker_.joinable()) markWorker_.join();
        kwiq_ui::gOnMarkRead = nullptr;
        CDataHandler::Shutdown();
        kwiq_net::GlobalCleanup();
        if (instanceLock_) CloseHandle(instanceLock_);
    }

    // Every EuroScope callback is a function-try-block: an exception must never reach EuroScope (#45)
    virtual void OnTimer(int Counter) override try {
        if (duplicate_) return;
        if (reduced_) { ReducedTimer(); return; }
        CVCHPlugin::OnTimer(Counter);                       // VCH housekeeping
        CDataHandler::ApplyPending(this);                   // slot/feed results, main thread
        FlushMessages();

        std::time_t now = std::time(nullptr);
        if (!settingsLoaded_) {                              // remembered local sector file location
            settingsLoaded_ = true;
            const char* sp = GetDataFromSettings("kwiq_sectorfile_path");
            if (sp && *sp) { std::lock_guard<std::mutex> g(mu_); sectorPath_ = sp; }
            const char* bs = GetDataFromSettings("kwiq_button");
            if (bs && *bs == '0') buttonShown_ = false;
            const char* bo = GetDataFromSettings("kwiq_button_offset");  // the KWIQ button's place (#59)
            const char* ba = GetDataFromSettings("kwiq_button_at");      // before #59: its top left
            kwiq::ButtonXY p;
            if (bo && kwiq::ParseButtonXY(bo, p)) { buttonOffset_ = p; buttonPlace_ = ButtonPlace::Offset; }
            else if (ba && kwiq::ParseButtonXY(ba, p)) { buttonOffset_ = p; buttonPlace_ = ButtonPlace::TopLeft; }
            const char* rd = GetDataFromSettings("kwiq_rdf_draw");      // the controller's own RDF switch
            if (rd && (*rd == '0' || *rd == '1')) rdf_.SetLocalDraw(*rd - '0');
            // DiscordEuroScope on or off for this controller; the Configurator writes the same setting
            const char* ds = GetDataFromSettings("DiscordEuroScope");
            if (ds && *ds == '0') discord_.SetLocal(false);
        }
        if (now - lastNameRead_ >= 10) { lastNameRead_ = now; ReadSectorName(); }
        TickConnection(now);
        TickKeySetup(now);
        TickEuroScopeVersion(now);
        {
            EuroScopePlugIn::CController me = ControllerMyself();
            std::string cs = connected_ && me.IsValid() && me.GetCallsign() ? me.GetCallsign() : "";
            rdf_.Tick(now, cs, connected_, [this](const std::string& m, bool err) { Msg(m, err); });
        }
        {
            std::string eventName;
            { std::lock_guard<std::mutex> g(mu_); if (ev_.active && !kwiq::RoutesForDate(ev_, TodayCached()).empty()) eventName = ev_.name; }
            discord_.Tick(now, eventName);
        }
        TickQnhAndNotices(now);
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
    } catch (...) {}

    // Reduced: Core's config (for the sector file version, the alert and the EuroScope versions), the outdated
    // sector file alert, the EuroScope version check, and the reminder at every logon. Nothing else.
    void ReducedTimer() {
        FlushMessages();
        std::time_t now = std::time(nullptr);
        if (!settingsLoaded_) {
            settingsLoaded_ = true;
            const char* sp = GetDataFromSettings("kwiq_sectorfile_path");
            if (sp && *sp) { std::lock_guard<std::mutex> g(mu_); sectorPath_ = sp; }
        }
        if (now - lastNameRead_ >= 10) { lastNameRead_ = now; ReadSectorName(); }
        TickConnection(now);
        if (justConnected_) { justConnected_ = false; Msg(kReducedReminder, true); }
        TickEuroScopeVersion(now);
        if (busy_ || now - lastCore_ < coreEvery_) return;
        if (worker_.joinable()) worker_.join();
        lastCore_ = now;
        busy_ = true;
        worker_ = std::thread(&CKWIQPlugin::Refresh, this, true, false);
    }

    virtual void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override try {
        if (reduced_) return;
        if (FunctionId >= KWIQ_MENU_KEY && FunctionId <= KWIQ_MENU_LAST) { MenuAction(FunctionId, sItemString, Pt, Area); return; }
        switch (FunctionId) {
        case KWIQ_FUNC_EVENT_ROUTES: OpenRoutesWindow(Pt); return;
        case KWIQ_FUNC_FPC_EXPLAIN:  ExplainFpcSelected(); return;
        case KWIQ_FUNC_SRD_EXPLAIN:  ExplainSrdSelected(); return;
        case KWIQ_FUNC_SRD_SEARCH:   OpenSrdWindow(Pt); return;
        }
        CVCHPlugin::OnFunctionCall(FunctionId, sItemString, Pt, Area);
    } catch (...) {}

    virtual void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget,
                              int ItemCode, int TagData, char sItemString[16],
                              int* pColorCode, COLORREF* pRGB, double* pFontSize) override try {
        using namespace kwiq;
        if (reduced_) return;                                // no tag items at all, VCH's included
        bool ours = ItemCode >= KWIQ_ITEM_EVENT_ROUTE && ItemCode <= KWIQ_ITEM_LAST;
        if (ours && !enabled_) return;                       // plugin too old
        UiCfg ui;
        if (ours) { std::lock_guard<std::mutex> g(mu_); ui = ui_; }
        if (ItemCode == KWIQ_ITEM_EVENT_ROUTE || ItemCode == KWIQ_ITEM_SRD || ItemCode == KWIQ_ITEM_FPC ||
            ItemCode == KWIQ_ITEM_FPC_FAILED) {
            if (!FlightPlan.IsValid()) return;
            const CacheEntry& e = Checks(FlightPlan);
            std::string txt;
            COLORREF colour = ui.cBad;
            if (ItemCode == KWIQ_ITEM_EVENT_ROUTE) {
                if (e.ev == RouteResult::Ok) { txt = ui.ok; colour = ui.cOk; }
                else if (e.ev == RouteResult::Check) txt = ui.chk;
            } else if (ItemCode == KWIQ_ITEM_SRD) {
                switch (e.srd) {
                case SrdResult::Ok:    txt = ui.ok; colour = ui.cOk; break;
                case SrdResult::Route: txt = "RTE"; break;
                case SrdResult::Min:   txt = "MIN"; break;
                case SrdResult::Max:   txt = "MAX"; break;
                default: break;
                }
            } else if (e.fpcChecked) {
                // VFPC: VFR and passed in green, the first failure in red, a forbidden level alone in yellow
                if (e.fpcCode.empty()) {
                    if (ItemCode == KWIQ_ITEM_FPC) { txt = e.fpcVfr ? "VFR" : ui.ok; colour = ui.cOk; }
                } else {
                    txt = e.fpcCode;
                    colour = e.fpcWarn ? ui.cWarn : ui.cBad;
                }
            }
            if (txt.empty()) return;
            strncpy_s(sItemString, 16, txt.c_str(), _TRUNCATE);
            *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
            *pRGB = colour;
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
        else if (ItemCode == KWIQ_ITEM_RDF) {
            if (FlightPlan.IsValid() && rdf_.Transmitting(FlightPlan.GetCallsign())) strcpy_s(sItemString, 16, "!");
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
    } catch (...) {}

    virtual bool OnCompileCommand(const char* sCommandLine) override try {
        // the copy that is in use answers; reduced: no commands
        if (duplicate_ || reduced_ || !sCommandLine) return false;
        // Arguments come from the trimmed line, so a leading space doesn't shift them (#46)
        const std::string line = kwiq::Trim(sCommandLine);
        std::string c = kwiq::Upper(line);
        if (c == ".KWIQ REFRESH") {
            lastCore_ = lastSrd_ = lastSlotFetch_ = 0;
            { std::lock_guard<std::mutex> g(mu_); srd_.version.clear(); }   // force SRD re-download
            Msg("Refresh requested", false);
            return true;
        }
        if (c == ".KWIQ ALERT") {                            // preview of the outdated sector file box
            previewUntil_ = std::time(nullptr) + PREVIEW_SECONDS;
            RefreshRadars();
            Msg("Showing the OUTDATED SCT FILE box for " + std::to_string(PREVIEW_SECONDS) + " seconds.", false);
            return true;
        }
        if (c == ".KWIQ UPDATE") {
            std::string latest, sha;
            { std::lock_guard<std::mutex> g(mu_); latest = latest_; sha = latestSha_; }
            if (kwiq::UpdateAvailable(KWIQ_PLUGIN_VERSION, latest, sha))
                Msg("Version " + latest + " is available. You will be asked to install it the next time EuroScope starts.", false);
            else Msg("Plugin v" KWIQ_PLUGIN_VERSION " is up to date.", false);
            return true;
        }
        if (c == ".KWIQ VERSION") {
            CheckSectorFile(true);
            return true;
        }
        if (c.compare(0, 16, ".KWIQ SECTORFILE") == 0) {
            std::string path = kwiq::Trim(line.substr(16));
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
        if (c == ".KWIQ KEY" || c.compare(0, 10, ".KWIQ KEY ") == 0) {
            std::string key = kwiq::Trim(line.substr(9));
            if (key.empty()) Msg(apiKey_.empty() ? "No API key has been entered. Generate one on myKWIQ, then use: .kwiq key <your key>"
                                                 : "An API key is connected.", false);
            else if (kwiq::Upper(key) == "CLEAR") SetApiKey("");
            else SetApiKey(key);
            return true;
        }
        if (c == ".KWIQ CONNECT") {
            StartKeySetup();
            return true;
        }
        if (c == ".KWIQ BUTTON") {
            ShowButton(!buttonShown_);
            return true;
        }
        if (c == ".RDF DRAW ON" || c == ".RDF DRAW OFF" || c == ".RDF DRAW CORE") {
            SetRdfDraw(c == ".RDF DRAW CORE" ? -1 : c == ".RDF DRAW ON" ? 1 : 0);
            return true;
        }
        if (c == ".RDF REFRESH" || c == ".RDF RELOAD") {
            if (!rdf_.Command(c)) Msg("RDF is not running.", true);
            else Msg(c == ".RDF RELOAD" ? "RDF: reconnecting to TrackAudio." : "RDF: channels synced again with TrackAudio.", false);
            return true;
        }
        if (c.rfind(".KWIQ QNH", 0) == 0) {                // .kwiq qnh <ICAO>
            std::string icao = kwiq::Trim(c.substr(9));
            std::string metar;
            int trl = 0, qnh = -1;
            { std::lock_guard<std::mutex> g(mu_); auto it = metars_.find(icao); if (it != metars_.end()) metar = it->second; }
            if (!metar.empty()) { qnh = kwiq::QnhFromMetar(metar); std::lock_guard<std::mutex> g(mu_); trl = kwiq::TrlFor(trl_, icao, qnh); }
            if (!kwiq::IsAirportCode(icao)) Msg("Usage: .kwiq qnh <ICAO>", true);
            else if (qnh < 0) Msg("No QNH for " + icao + (metar.empty() ? " from Core yet." : " in its METAR."), true);
            else DisplayUserMessage("PRESSURE_MONITOR", "KWIQ", kwiq::QnhMessage(icao, qnh, 0, trl, 0).c_str(), true, true, true, false, false);
            return true;
        }
        if (c == ".KWIQ NOTIFICATIONS") { OpenNotices(); return true; }
        if (c == ".KWIQ DISCORD") {
            SetDiscord(!discord_.Local());
            return true;
        }
        if (c == ".KWIQ AUTOREFRESH") {
            autoRefresh_ = !autoRefresh_;
            Msg(std::string("Slot auto refresh: ") + (autoRefresh_ ? "on" : "off"), false);
            return true;
        }
        if (c == ".KWIQ STATUS") {
            std::lock_guard<std::mutex> g(mu_);
            std::string s = "v" KWIQ_PLUGIN_VERSION " | " + std::string(enabled_ ? "enabled" : "DISABLED (too old)") +
                " | EuroScope " + (esVersion_.empty() ? std::string("?") : esVersion_) +
                " | API key " + (apiKey_.empty() ? std::string("not set") : std::string("connected")) + WineNote() +
                " | RDF " + (rdf_.DrawOn() ? "on" : "off") + ", TrackAudio " + kwiq::TrackAudioStateText(rdf_.TrackAudio()) +
                " | DiscordEuroScope " + (discord_.Active() ? "on" : discord_.Local() ? "off (Core)" : "off") +
                " | sector files OKAC " + (upstreamAirac_["OKAC"].empty() ? "?" : upstreamAirac_["OKAC"]) +
                " ORBB " + (upstreamAirac_["ORBB"].empty() ? "?" : upstreamAirac_["ORBB"]) +
                " | SRD routes " + std::to_string(srd_.rows.size()) + " | sectors " +
                std::to_string(sectors_.size()) + " | FIR exits " + std::to_string(firExits_.size()) +
                " | FPC airports " + std::to_string(fpcRules_.size()) + " | ";
            s += ev_.active
                ? ev_.name + " | event routes " + std::to_string(ev_.routes.size()) +
                  " | VATCAN code " + ev_.vatcanCode + " | slots " + std::to_string(CDataHandler::SlotCount()) +
                  " | pilots " + std::to_string(CDataHandler::PilotCount())
                : "no active event";
            Msg(s, false);
            return true;
        }
        return CVCHPlugin::OnCompileCommand(sCommandLine);
    } catch (...) { return false; }

    // main thread: sector indicator of the position tracking an aircraft: Core's alias for the ID of the ESE
    // position EuroScope identifies that controller as right now (by prefix, suffix and frequency, so a relief
    // callsign or a controller covering another position gets the position actually worked). Empty if nobody
    // tracks it, EuroScope does not identify the controller, or Core does not list that position.
    std::string SectorFor(const char* trackerCallsign) {
        std::string cs = kwiq::Upper(trackerCallsign);
        EuroScopePlugIn::CController me = ControllerMyself();
        EuroScopePlugIn::CController c = me.IsValid() && kwiq::Upper(me.GetCallsign()) == cs ? me : ControllerSelect(trackerCallsign);
        if (!c.IsValid() || !c.GetPositionIdentified() || !c.GetPositionId() || !*c.GetPositionId()) return "";
        std::lock_guard<std::mutex> g(mu_);
        auto it = sectors_.find(kwiq::Upper(c.GetPositionId()));
        return it == sectors_.end() ? std::string() : it->second;
    }
    bool SectorDrawEnabled() const { return sectorDraw_ && enabled_ && !duplicate_ && !reduced_; }
    bool Duplicate() const { return duplicate_; }
    // RDF on the radar screen (REFRESH_PHASE_AFTER_TAGS)
    void DrawRdf(EuroScopePlugIn::CRadarScreen* screen, HDC hDC) { if (!duplicate_ && !reduced_) rdf_.Draw(screen, hDC); }
    // ---- QNH monitor and procedure notifications (#4) -------------------------------------------------
    // The logged-on controller's callsign, and the other controllers online (for top-down)
    bool Myself(std::string& me, std::vector<std::string>& online) {
        EuroScopePlugIn::CController my = ControllerMyself();
        if (!connected_ || !my.IsValid() || !my.IsController() || !my.GetCallsign()) return false;
        me = my.GetCallsign();
        for (auto c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
            if (c.IsController() && c.GetCallsign() && me != c.GetCallsign()) online.push_back(c.GetCallsign());
        return true;
    }
    std::vector<std::string> Covered() {
        std::string me;
        std::vector<std::string> online;
        if (!Myself(me, online)) return {};
        std::lock_guard<std::mutex> g(mu_);
        return kwiq::AirportsCovered(topDown_, me, online);
    }
    // Logged on: the notifications for the airports the controller covers. Not logged on: all of them.
    std::vector<kwiq::ProcedureNotice> NoticesForMe() {
        std::vector<kwiq::ProcedureNotice> all;
        { std::lock_guard<std::mutex> g(mu_); all = notices_; }
        if (!connected_) return all;
        std::vector<std::string> cov = Covered();
        std::vector<kwiq::ProcedureNotice> out;
        for (const auto& n : all) if (kwiq::NoticeRelevant(n, cov)) out.push_back(n);
        return out;
    }
    void OpenNotices() {
        // Called from the window's WndProc, so nothing may escape into Windows (#45)
        kwiq_ui::gOnMarkRead = [](const std::string& id, bool read) { try { if (gPlugin) gPlugin->MarkRead(id, read); } catch (...) {} };
        kwiq_ui::ShowNotices(NoticesForMe());
        noticesDue_ = true;                                          // and fetch them again
    }
    // Mark read / Mark unread: shown at once, and Core is told (POST or DELETE /notifications/<id>/read)
    void MarkRead(const std::string& id, bool read) {
        {
            std::lock_guard<std::mutex> g(mu_);
            for (auto& n : notices_) if (n.id == id) n.read = read;
            pendingMarks_.emplace_back(id, read);
        }
        if (markBusy_) return;                                      // the running worker takes it
        if (markWorker_.joinable()) markWorker_.join();
        markBusy_ = true;
        markWorker_ = std::thread([this]() {
            try {                                                   // nothing may leave a thread's function (#48)
                for (;;) {
                    std::pair<std::string, bool> m;
                    {
                        std::lock_guard<std::mutex> g(mu_);
                        if (pendingMarks_.empty() || quit_) { markBusy_ = false; return; }
                        m = pendingMarks_.front();
                        pendingMarks_.erase(pendingMarks_.begin());
                    }
                    std::string body, err;
                    // The ID is percent-encoded, so whatever Core sends stays one path segment (#49)
                    kwiq_net::HttpSend(m.second ? "POST" : "DELETE", std::string(CORE_NOTICES_URL) + "/" + kwiq::UrlEncode(m.first) + "/read",
                                       "", body, err);
                    if (!err.empty()) SetError("Notifications", err); else ClearError("Notifications");
                }
            } catch (...) {}
            markBusy_ = false;                                      // the next Mark read starts a new worker
        });
    }
    // METARs and notifications: fetched when Core pushes "updated", and polled as a fallback.
    // A function-try-block: nothing may leave the thread's function, and auxBusy_ is reset either way (#48).
    void AuxRefresh(bool metar, bool notices) try {
        std::string body, err;
        if (metar) {
            if (kwiq_net::HttpGet(CORE_METAR_URL, body, err)) {
                ClearError("METAR");
                auto m = kwiq::ParseMetars(body);
                { std::lock_guard<std::mutex> g(mu_); metars_ = std::move(m); }
                metarsNew_ = true;
            } else if (err != "HTTP 404") SetError("METAR", err);
        }
        if (notices) {
            if (kwiq_net::HttpGet(CORE_NOTICES_URL, body, err)) {
                ClearError("Notifications");
                auto n = kwiq::ParseNotices(body);
                { std::lock_guard<std::mutex> g(mu_); if (pendingMarks_.empty()) notices_ = std::move(n); }
                noticesNew_ = true;
            } else if (err != "HTTP 404") SetError("Notifications", err);
        }
        auxBusy_ = false;
    } catch (...) {
        auxBusy_ = false;
    }
    void TickQnhAndNotices(std::time_t now) {
        for (const auto& e : push_.Take()) {
            if (e.first == "metars") metarDue_ = true;
            if (e.first == "notifications") noticesDue_ = true;
        }
        // While pushes come, a slow poll is only a safety net
        if (now - lastMetar_ >= (push_.Connected() ? 600 : metarPoll_.load())) metarDue_ = true;
        if (now - lastNotices_ >= 300) noticesDue_ = true;

        std::string me;
        std::vector<std::string> online;
        bool on = Myself(me, online);
        if (on && me != qnhCallsign_) { qnhSeen_.clear(); noticesTold_.clear(); qnhCallsign_ = me; noticesDue_ = true; metarDue_ = true; }
        if (!on && !qnhCallsign_.empty()) { qnhSeen_.clear(); noticesTold_.clear(); qnhCallsign_.clear(); }

        if ((metarDue_ || noticesDue_) && !auxBusy_) {
            if (auxWorker_.joinable()) auxWorker_.join();
            bool m = metarDue_, n = noticesDue_;
            metarDue_ = noticesDue_ = false;
            if (m) lastMetar_ = now;
            if (n) lastNotices_ = now;
            auxBusy_ = true;
            auxWorker_ = std::thread(&CKWIQPlugin::AuxRefresh, this, m, n);
        }

        if (metarsNew_.exchange(false) && on) {
            std::vector<std::string> cov;
            std::map<std::string, std::string> metars;
            std::vector<kwiq::TrlRule> trl;
            { std::lock_guard<std::mutex> g(mu_); cov = kwiq::AirportsCovered(topDown_, me, online); metars = metars_; trl = trl_; }
            for (const auto& icao : cov) {
                auto it = metars.find(icao);
                int q = it == metars.end() ? -1 : kwiq::QnhFromMetar(it->second);
                if (q < 0) continue;
                int t = kwiq::TrlFor(trl, icao, q);
                auto seen = qnhSeen_.find(icao);
                // Only changes during the session: the first QNH after logging on (or taking an airport) is noted
                if (seen != qnhSeen_.end() && seen->second.first != q)
                    DisplayUserMessage("PRESSURE_MONITOR", "KWIQ", kwiq::QnhMessage(icao, q, seen->second.first, t, seen->second.second).c_str(),
                                       true, true, true, true, false);
                qnhSeen_[icao] = { q, t };
            }
        }

        if (noticesNew_.exchange(false)) {
            std::vector<kwiq::ProcedureNotice> mine = NoticesForMe();
            kwiq_ui::UpdateNotices(mine);
            if (on) {
                int fresh = 0, unread = 0;
                for (const auto& n : mine) {
                    if (n.read) continue;
                    unread++;
                    if (noticesTold_.insert(n.id).second) fresh++;
                }
                if (fresh)
                    DisplayUserMessage("KWIQ_NOTIFICATIONS", "KWIQ", (std::to_string(unread) + " unread notification" + (unread == 1 ? "" : "s") +
                                       " for the position you are controlling. Open them from the KWIQ button: Notifications.").c_str(),
                                       true, true, true, true, false);
            }
        }
    }

    // DiscordEuroScope on or off for this controller, kept as the DiscordEuroScope setting (the Configurator
    // writes it too). Core can still turn it off for everyone.
    void SetDiscord(bool on) {
        discord_.SetLocal(on);
        SaveDataToSettings("DiscordEuroScope", "DiscordEuroScope (Discord Rich Presence) on or off", on ? "1" : "0");
        Msg(std::string("DiscordEuroScope: ") + (on ? "on" : "off"), false);
    }
    // The controller's own RDF switch: 1 on, 0 off, -1 as Core says. Kept in the profile.
    void SetRdfDraw(int v) {
        rdf_.SetLocalDraw(v);
        SaveDataToSettings("kwiq_rdf_draw", "KWIQ RDF drawing (empty = as Core says)", v < 0 ? "" : v ? "1" : "0");
        Msg(std::string("RDF drawing: ") + (rdf_.DrawOn() ? "on" : "off") + (v < 0 ? " (as the vACC sets it)" : ""), false);
    }

    // ---- the KWIQ button and its menu (main thread) (#59) ------------------------------------
    // The vACC's logo, as other plugins show theirs on the radar screen: no border, label or status colours.
    // By default in the bottom-right corner of each radar screen, inset by BUTTON_MARGIN; it can be dragged
    // anywhere, and its place is kept in the profile (kwiq_button_offset) as its offset from the bottom-right
    // corner, so it follows the window as it resizes and always stays fully on screen. A profile with the
    // older kwiq_button_at (its top left) keeps that place until it is dragged. ".kwiq button" (or Hide
    // Button in its menu) hides it and brings it back. In reduced mode it is shown, and a click repeats the
    // reminder instead.
    static constexpr int BUTTON_SIZE = 28;                           // px at 96 DPI, scaled for Windows DPI
    static constexpr int BUTTON_MARGIN = 8;                          // px at 96 DPI, from the corner by default
    bool ButtonShown() const { return buttonShown_ && !duplicate_; }
    RECT ButtonRect(const RECT& radar, int size, int dpi) const {
        int w = radar.right - radar.left, h = radar.bottom - radar.top;
        kwiq::ButtonXY off;
        if (buttonPlace_ == ButtonPlace::Offset) off = buttonOffset_;
        else if (buttonPlace_ == ButtonPlace::TopLeft) off = kwiq::ButtonOffsetFromTopLeft(w, h, size, buttonOffset_.x, buttonOffset_.y);
        else off.x = off.y = kwiq::ScaleForDpi(BUTTON_MARGIN, dpi);
        kwiq::ButtonXY p = kwiq::ButtonTopLeft(w, h, size, off);
        RECT r = { radar.left + p.x, radar.top + p.y, radar.left + p.x + size, radar.top + p.y + size };
        return r;
    }
    void MoveButton(const RECT& radar, POINT pt, int size, bool released) {
        buttonOffset_ = kwiq::ButtonOffsetAt(radar.right - radar.left, radar.bottom - radar.top, size,
                                             (int)(pt.x - radar.left), (int)(pt.y - radar.top));
        buttonPlace_ = ButtonPlace::Offset;
        if (released) SaveDataToSettings("kwiq_button_offset", "KWIQ button offset from the bottom-right corner",
                                         kwiq::FormatButtonXY(buttonOffset_).c_str());
    }
    void ButtonClicked(RECT area) {
        if (reduced_) { Msg(kReducedReminder, true); return; }      // no menu in reduced mode
        OpenMenu(area);
    }
    void ShowButton(bool shown) {
        buttonShown_ = shown;
        SaveDataToSettings("kwiq_button", "KWIQ button on the radar screen", shown ? "1" : "0");
        Msg(shown ? "The KWIQ button is shown." : "The KWIQ button is hidden. Type .kwiq button to show it again.", false);
    }
    // The TrackAudio row: TrackAudio connects on its own, so this is a status, not a switch. A click syncs the
    // channels again while connected, tries to connect now while not, and does nothing when RDF is off.
    void TrackAudioClicked() {
        switch (kwiq::TrackAudioClickAction(rdf_.TrackAudio())) {
        case kwiq::TrackAudioClick::Resync:    OnCompileCommand(".rdf refresh"); return;
        case kwiq::TrackAudioClick::Reconnect: rdf_.Reconnect(); Msg("RDF: trying to connect to TrackAudio again.", false); return;
        default: return;
        }
    }
    void OpenMenu(RECT area) {
        // One column, each line padded so the values line up on the right (the list uses a fixed-width font)
        auto line = [](const char* label, const char* value) {
            std::string l = label, v = value;
            return l + std::string(l.size() + v.size() < 28 ? 28 - l.size() - v.size() : 1, ' ') + v;
        };
        OpenPopupList(area, "KWIQ v" KWIQ_PLUGIN_VERSION, 1);
        AddPopupListElement(line("API Key", apiKey_.empty() ? "Not set" : "Connected").c_str(), "", KWIQ_MENU_KEY);
        AddPopupListElement(line("Sector Indicators", sectorDraw_ ? "On" : "Off").c_str(), "", KWIQ_MENU_SECTORS);
        AddPopupListElement(line("RDF Drawing", rdf_.DrawOn() ? "On" : "Off").c_str(), "", KWIQ_MENU_RDF);
        AddPopupListElement(line("TrackAudio", kwiq::TrackAudioStateText(rdf_.TrackAudio())).c_str(), "", KWIQ_MENU_BRIDGE);
        AddPopupListElement(line("DiscordEuroScope", discord_.Local() ? "On" : "Off").c_str(), "", KWIQ_MENU_DISCORD);
        {
            int unread = 0;
            for (const auto& n : NoticesForMe()) if (!n.read) unread++;
            AddPopupListElement(line("Notifications", unread ? (std::to_string(unread) + " unread").c_str() : "").c_str(), "", KWIQ_MENU_NOTICES);
        }
        AddPopupListElement(line("Slot Auto Refresh", autoRefresh_ ? "On" : "Off").c_str(), "", KWIQ_MENU_AUTOREFRESH);
        AddPopupListElement("Event Routes", "", KWIQ_MENU_EVENT_ROUTES);
        AddPopupListElement("SRD Search", "", KWIQ_MENU_SRD);
        AddPopupListElement(line("Sector File", "Check").c_str(), "", KWIQ_MENU_SECTORFILE);
        AddPopupListElement(line("Plugin Updates", "Check").c_str(), "", KWIQ_MENU_UPDATE);
        AddPopupListElement("Refresh Data", "", KWIQ_MENU_REFRESH);
        AddPopupListElement("Status", "", KWIQ_MENU_STATUS);
        AddPopupListElement("Hide Button", "", KWIQ_MENU_HIDE, false, EuroScopePlugIn::POPUP_ELEMENT_NO_CHECKBOX, false, true);
    }
    void MenuAction(int id, const char*, POINT pt, RECT) {
        // EuroScope can report one choice to both the radar screen and the plugin: act on it once,
        // or a switch would go off and straight back on
        static int lastId = 0;
        static DWORD lastAt = 0;
        DWORD tick = GetTickCount();
        if (id == lastId && tick - lastAt < 500) return;
        lastId = id; lastAt = tick;
        switch (id) {
        case KWIQ_MENU_KEY:          StartKeySetup(); return;
        case KWIQ_MENU_SECTORS:      OnCompileCommand(".kwiq sector"); return;
        case KWIQ_MENU_RDF:          SetRdfDraw(rdf_.DrawOn() ? 0 : 1); return;
        case KWIQ_MENU_BRIDGE:       TrackAudioClicked(); return;
        case KWIQ_MENU_DISCORD:      SetDiscord(!discord_.Local()); return;
        case KWIQ_MENU_NOTICES:      OpenNotices(); return;
        case KWIQ_MENU_AUTOREFRESH:  OnCompileCommand(".kwiq autorefresh"); return;
        case KWIQ_MENU_EVENT_ROUTES: OpenRoutesWindow(pt); return;
        case KWIQ_MENU_SRD:          OpenSrdWindow(pt); return;
        case KWIQ_MENU_SECTORFILE:   OnCompileCommand(".kwiq version"); return;
        case KWIQ_MENU_UPDATE:       OnCompileCommand(".kwiq update"); return;
        case KWIQ_MENU_REFRESH:      OnCompileCommand(".kwiq refresh"); return;
        case KWIQ_MENU_STATUS:       OnCompileCommand(".kwiq status"); return;
        case KWIQ_MENU_HIDE:         ShowButton(false); return;
        }
    }
    // Sector indicator colour per flight plan state (FLIGHT_PLAN_STATE_*): Core's colour for that state,
    // else Core's sector colour
    std::array<COLORREF, 8> SectorColours() {
        std::lock_guard<std::mutex> g(mu_);
        std::array<COLORREF, 8> a;
        for (int i = 0; i < 8; i++) {
            auto it = ui_.cStates.find(kwiq::SectorColourName(i));
            a[i] = it == ui_.cStates.end() ? ui_.cSector : it->second;
        }
        return a;
    }

    virtual EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent,
                                                                bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;

    // called from the SRD window's Search button (main thread)
    void RunSrdSearch(const std::string& adep, const std::string& ades, const std::string& lvl) {
        std::vector<kwiq::SrdRoute> rows;
        {
            std::lock_guard<std::mutex> g(mu_);
            auto hits = kwiq::SrdSearch(srd_, adep, ades, kwiq::ParseLevelFt(lvl));
            for (size_t i = 0; i < hits.size() && (int)i < kwiq_ui::SRD_MAX_RESULTS; i++) rows.push_back(srd_.rows[hits[i]]);
        }
        kwiq_ui::SetSrdResults(rows);
    }

private:
    // ---- API key setup through myKWIQ (main thread) -------------------------------------------
    // Whenever there is no key, or Core refuses it as missing, invalid, revoked or expired, the setup
    // opens: as EuroScope starts, whenever the controller connects to the network, or once they log off
    // if Core refuses the key while they are logged on (they are told in the chat). It cannot be
    // dismissed: it closes only once a key is connected. A key refused because they left the roster
    // or are suspended is only reported, as a new one wouldn't help.
    void StartKeySetup() {
        if (!keyLink_.Start()) {
            Msg("The API key setup could not start (a firewall may be blocking it). Use Show Key on myKWIQ, under EuroScope "
                "Plugin, then: .kwiq key <your key>. Need help? discord.kuwait-iraqvacc.net", true);
            return;
        }
        OpenConnectPage();
        kwiq_ui::ShowKeyPrompt();
    }
    void OpenConnectPage() {
        if (!keyLink_.Running()) return;
        // v=2: myKWIQ hands over a one-off code that is swapped for the key with Core (KeyLink::Claim)
        std::string url = std::string(MYKWIQ_CONNECT_URL) + "?port=" + std::to_string(keyLink_.Port()) + "&state=" + keyLink_.State() + "&v=2";
        // Wine (EuroScope on a Mac or Linux) hands the link to the system browser; if no browser opens,
        // there or on Windows, the link is on the clipboard to paste in by hand (never in the chat)
        bool opened = (INT_PTR)ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
        kwiq_ui::CopyToClipboard(nullptr, url);
        if (!opened) Msg("Your browser could not be opened. The link is on your clipboard: paste it into your browser.", true);
    }
    void TickKeySetup(std::time_t now) {
        std::string key = keyLink_.Take();
        if (!key.empty()) {
            SetApiKey(key);
            kwiq_win::Box("KWIQ API Key Connected", "Your API key has been received and saved. The plugin is now connected.",
                          MB_OK | MB_ICONINFORMATION);
            return;
        }
        // The listener gives up after 15 minutes; the setup stays, so start a new one for the next OK
        if (kwiq_ui::KeyPromptOpen() && !keyLink_.Running()) keyLink_.Start();
        if (kwiq_ui::KeyPromptReopen()) OpenConnectPage();   // OK in the setup box: the page again
        if (now - startedAt_ < 5) return;                    // let EuroScope settle first

        std::string reason;
        { std::lock_guard<std::mutex> g(mu_); reason = keyReason_; }
        bool fixable = reason == "missing" || reason == "invalid" || reason == "revoked" || reason == "expired";
        if (!(apiKey_.empty() || (keyRefused_ && fixable))) { justConnected_ = false; return; }

        // One setup per problem: a new key, or a different refusal, is a new problem. Connecting to the
        // network without a working key opens it again
        std::string problem = apiKey_.empty() ? std::string("none") : kwiq::ApiKeyId(apiKey_) + ":" + reason;
        bool onConnect = justConnected_;
        justConnected_ = false;
        if (keyLink_.Running() || (problem == keyPromptedFor_ && !onConnect)) return;
        if (connected_ && !onConnect) {
            if (problem != keyToldFor_) {
                keyToldFor_ = problem;
                Msg("Your API key needs connecting again. The setup will open when you log off, or use the KWIQ button and API Key.", true);
            }
            return;
        }
        keyPromptedFor_ = problem;
        StartKeySetup();
    }

    // ---- EuroScope version (main thread) ---------------------------------------------------------
    // Checked once, as soon as Core's config has arrived: a popup as EuroScope starts, the chat after that.
    void TickEuroScopeVersion(std::time_t now) {
        if (esChecked_ || esVersion_.empty()) return;
        std::string accepted, url;
        {
            std::lock_guard<std::mutex> g(mu_);
            if (!esConfigured_) return;
            accepted = esAccepted_; url = esUrl_;
        }
        esChecked_ = true;
        if (kwiq::EuroScopeVersionAccepted(esVersion_, accepted)) return;
        std::string list;
        for (const std::string& v : kwiq::EuroScopeVersions(accepted)) list += (list.empty() ? "" : " or ") + v;
        Msg("Incorrect EuroScope version " + esVersion_ + " (the vACC uses " + list + "). Setup guide: " + url, true);
        // As UKCP asks about its API key: OK opens the page, Cancel doesn't (EuroScope's thread, modal)
        if (now - startedAt_ <= STARTUP_SECONDS &&
            kwiq_win::Box("KWIQ Incorrect EuroScope Version",
                          "You are on an incorrect EuroScope version: " + esVersion_ + ". The Kuwait & Iraq vACC uses " + list +
                          ".\r\n\r\nSelect OK to open the EuroScope setup guide.", MB_OKCANCEL | MB_ICONWARNING) == IDOK &&
            kwiq::IsWebLink(url))
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // " | Wine 9.0" when EuroScope runs through Wine or CrossOver (a Mac or Linux), for support
    static std::string WineNote() {
        typedef const char* (*WineVersionFn)();
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        WineVersionFn v = ntdll ? (WineVersionFn)GetProcAddress(ntdll, "wine_get_version") : nullptr;
        return v ? std::string(" | Wine ") + v() : std::string();
    }

    static std::string KeyRefusedMessage(const std::string& reason) {
        if (reason == "missing") return "Core needs your personal API key. Connect it with the KWIQ button and API Key, or .kwiq connect";
        if (reason == "revoked") return "Your API key has been invalidated. Connect a new one with the KWIQ button and API Key, or .kwiq connect";
        if (reason == "expired") return "Your API key has expired. Connect a new one with the KWIQ button and API Key, or .kwiq connect";
        if (reason == "unlinked") return "Your API key no longer works, as you are no longer on the Kuwait & Iraq vACC roster.";
        if (reason == "suspended") return "Your API key is paused while your VATSIM account is suspended.";
        return "Core did not recognise your API key. Connect it again with the KWIQ button and API Key, or .kwiq connect";
    }

    void SetApiKey(const std::string& key) {
        if (!key.empty() && !kwiq::IsApiKey(key)) {
            Msg("That is not a KWIQ API key. Copy it from myKWIQ, under EuroScope Plugin; it starts with kwiq_", true);
            return;
        }
        if (!apikey::Save(key)) { Msg("The API key could not be saved in %LOCALAPPDATA%\\KWIQ\\settings.", true); return; }
        apiKey_ = key;
        kwiq_net::SetCoreAuth(CORE_BASE "/", apiKey_, KWIQ_PLUGIN_VERSION);
        { std::lock_guard<std::mutex> g(mu_); lastErr_.erase("API key"); srd_.version.clear(); }
        lastCore_ = lastSrd_ = 0;                            // try it straight away
        keyRefused_ = false;
        keyLink_.Stop();
        kwiq_ui::CloseKeyPrompt();
        Msg(key.empty() ? "API key removed." : "API key connected.", false);
    }

    struct CacheEntry {
        bool valid = false;
        unsigned evGen = 0, srdGen = 0, exitGen = 0, fpcGen = 0;
        int rfl = 0, filedRfl = 0;
        char engine = 0, capability = 0;
        std::string adep, ades, rte, sid, planType, today;
        kwiq::RouteResult ev = kwiq::RouteResult::None;
        kwiq::SrdResult srd = kwiq::SrdResult::None;
        bool fpcChecked = false, fpcVfr = false, fpcWarn = false;   // the flight plan check: empty code = passed
        std::string fpcCode;
    };

    // Draw-path checks, cached per callsign: only recomputed when the flight plan, the UTC day
    // or the Core data changed, so a redraw costs a few string compares and no allocation.
    // The entry is only read on the main thread, where it is also written.
    const CacheEntry& Checks(EuroScopePlugIn::CFlightPlan& fp) {
        auto d = fp.GetFlightPlanData();
        std::string o = kwiq::Upper(d.GetOrigin()), a = kwiq::Upper(d.GetDestination()), r = d.GetRoute();
        std::string sid = d.GetSidName(), planType = d.GetPlanType();
        int rfl = fp.GetFinalAltitude(), filedRfl = d.GetFinalAltitude();
        char engine = d.GetEngineType(), capability = d.GetCapibilities();
        const std::string& today = TodayCached();
        std::string cs = fp.GetCallsign();
        std::lock_guard<std::mutex> g(mu_);
        if (cache_.size() > 5000) cache_.clear();
        CacheEntry& e = cache_[cs];
        if (!(e.valid && e.evGen == evGen_ && e.srdGen == srdGen_ && e.exitGen == exitGen_ && e.fpcGen == fpcGen_ &&
              e.rfl == rfl && e.filedRfl == filedRfl && e.engine == engine && e.capability == capability &&
              e.today == today && e.adep == o && e.ades == a && e.rte == r && e.sid == sid && e.planType == planType)) {
            auto toks = kwiq::NormaliseRoute(r);
            e.ev  = kwiq::CheckEventRoute(ev_, o, a, toks, today);
            e.srd = kwiq::CheckSrd(srd_, o, a, toks, rfl, FirExitLocked(fp, a));
            kwiq::fpc::Result f = kwiq::fpc::Check(fpcRules_, FpcFlight(fp));
            e.fpcChecked = f.checked; e.fpcVfr = f.vfr; e.fpcWarn = f.WarningOnly();
            e.fpcCode = f.fails.empty() ? std::string() : kwiq::fpc::Code(f.fails[0]);
            e.evGen = evGen_; e.srdGen = srdGen_; e.exitGen = exitGen_; e.fpcGen = fpcGen_;
            e.rfl = rfl; e.filedRfl = filedRfl; e.engine = engine; e.capability = capability; e.today = today;
            e.adep = o; e.ades = a; e.rte = r; e.sid = sid; e.planType = planType; e.valid = true;
        }
        return e;
    }
    // What VFPC checks, from the flight plan as filed
    static kwiq::fpc::Flight FpcFlight(EuroScopePlugIn::CFlightPlan& fp) {
        auto d = fp.GetFlightPlanData();
        kwiq::fpc::Flight f;
        f.origin = d.GetOrigin();
        f.destination = d.GetDestination();
        f.route = d.GetRoute();
        f.sid = d.GetSidName();
        f.rflFt = d.GetFinalAltitude();
        f.engine = d.GetEngineType();
        f.capability = d.GetCapibilities();
        f.vfr = std::strcmp(d.GetPlanType(), "V") == 0;
        return f;
    }
    // ---- FIR exit (UK Controller Plugin: CachedAircraftFirExitGenerator) ----------------------
    // Where a flight leaves the vACC's airspace, from EuroScope's extracted route and Core's exit points.
    // Only looked for when the destination is outside it; callers hold mu_.
    kwiq::AircraftExit FirExitLocked(EuroScopePlugIn::CFlightPlan& fp, const std::string& ades) {
        if (firExits_.empty() || kwiq::Domestic(ades, domesticPrefixes_)) return {};
        std::vector<kwiq::RoutePoint> pts;
        EuroScopePlugIn::CFlightPlanExtractedRoute er = fp.GetExtractedRoute();
        int n = er.GetPointsNumber();
        for (int i = 0; i < n; i++) {
            kwiq::RoutePoint p;
            p.name = er.GetPointName(i);
            if (i + 1 < n) p.track = ((int)er.GetPointPosition(i).DirectionTo(er.GetPointPosition(i + 1)) % 360 + 360) % 360;
            pts.push_back(std::move(p));
        }
        return kwiq::FindFirExit(firExits_, pts);
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
            {   // leaving the vACC's airspace: search to the FIR exit point (UKCP SrdSearchDialog)
                std::lock_guard<std::mutex> g(mu_);
                std::string end = kwiq::SrdEnd(ades, domesticPrefixes_, FirExitLocked(fp, ades));
                if (!end.empty()) ades = end;
            }
            int rfl = fp.GetFinalAltitude();
            if (rfl > 0) lvl = std::to_string(rfl / 100);
        }
        kwiq_ui::ShowSrd(pt, adep, ades, lvl);
        if (!adep.empty() || !ades.empty()) RunSrdSearch(adep, ades, lvl);
    }
    void ExplainSrdSelected() {
        auto fp = FlightPlanSelectASEL();
        if (!fp.IsValid()) return;
        auto d = fp.GetFlightPlanData();
        std::string msg;
        {
            std::lock_guard<std::mutex> g(mu_);
            std::string ades = kwiq::Upper(d.GetDestination());
            msg = "SRD: " + kwiq::ExplainSrd(srd_, kwiq::Upper(d.GetOrigin()), ades, d.GetRoute(), fp.GetFinalAltitude(),
                                             FirExitLocked(fp, ades));
        }
        DisplayUserMessage("KWIQ Controller Plugin", fp.GetCallsign(), msg.c_str(), true, true, false, false, false);
    }
    // VFPC checkFPDetail
    void ExplainFpcSelected() {
        auto fp = FlightPlanSelectASEL();
        if (!fp.IsValid()) return;
        kwiq::fpc::Flight f = FpcFlight(fp);
        std::string msg;
        {
            std::lock_guard<std::mutex> g(mu_);
            msg = "FPC: " + kwiq::fpc::Explain(kwiq::fpc::Check(fpcRules_, f), f);
        }
        DisplayUserMessage("KWIQ Controller Plugin", fp.GetCallsign(), msg.c_str(), true, true, false, false, false);
    }

    // ---- outdated sector file (main thread) --------------------------------------------------
    // Once logged on with an outdated sector file, a red OUTDATED SCT FILE box is drawn in the middle of
    // every radar screen until the sector file is up to date or the controller logs off. It flashes
    // between the two alert colours for alert.flash_seconds, then stays solid, and the alert sound
    // plays on a loop for as long as it is up.
    void TickConnection(std::time_t now) {
        int ct = GetConnectionType();
        bool net = ct == EuroScopePlugIn::CONNECTION_TYPE_DIRECT || ct == EuroScopePlugIn::CONNECTION_TYPE_VIA_PROXY ||
                   ct == EuroScopePlugIn::CONNECTION_TYPE_SWEATBOX;
        if (net && !connected_) {
            connected_ = true;
            connectedAt_ = now;
            justConnected_ = true;                           // a missing key's setup opens as they connect
            // Our position can tell OKAC from ORBB; the check runs on the next refresh, brought forward
            { std::lock_guard<std::mutex> g(mu_); myCallsign_ = ControllerMyself().GetCallsign(); }
            lastCore_ = 0;
        }
        if (!net) connected_ = false;

        // The alert sound plays on a loop for as long as the box is up, and stops when it goes
        bool shown = SctBannerShown(now);
        if (shown && !soundPlayed_) {
            soundPlayed_ = true;
            SfStatus s;
            { std::lock_guard<std::mutex> g(mu_); s = sf_; }
            soundLooping_ = !s.sound.empty() && PlaySoundA(s.sound.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
        }
        if (shown && !soundLooping_ && now % 3 == 0) MessageBeep(MB_ICONWARNING);   // sound file not found: never alert silently
        if (!shown && soundPlayed_) {
            soundPlayed_ = false;
            if (soundLooping_) PlaySoundA(nullptr, nullptr, 0);
            soundLooping_ = false;
        }
        // Redraw while it flashes, and once whenever it appears or goes
        if (shown != bannerWas_ || (shown && SctBannerFlashing(now))) RefreshRadars();
        bannerWas_ = shown;
    }
public:
    bool SctBannerShown(std::time_t now) {
        if (now < previewUntil_) return true;
        if (!connected_ || !enabled_) return false;
        std::lock_guard<std::mutex> g(mu_);
        return sf_.known && sf_.outdated && ui_.alertOn;
    }
    bool SctBannerFlashing(std::time_t now) {
        int sec;
        { std::lock_guard<std::mutex> g(mu_); sec = ui_.flashSec; }
        std::time_t from = now < previewUntil_ ? previewUntil_ - PREVIEW_SECONDS : connectedAt_;
        return now - from < sec;
    }
    COLORREF SctBannerColour(std::time_t now) {
        UiCfg ui;
        { std::lock_guard<std::mutex> g(mu_); ui = ui_; }
        std::time_t from = now < previewUntil_ ? previewUntil_ - PREVIEW_SECONDS : connectedAt_;
        return SctBannerFlashing(now) && (now - from) % 2 == 1 ? ui.alertB : ui.alertA;
    }
    static void RegisterRadar(EuroScopePlugIn::CRadarScreen* r, bool add) {
        if (add) radars_.push_back(r);
        else radars_.erase(std::remove(radars_.begin(), radars_.end(), r), radars_.end());
    }
private:
    static inline std::vector<EuroScopePlugIn::CRadarScreen*> radars_;
    void RefreshRadars() { for (auto* r : radars_) r->RequestRefresh(); }

    void Msg(const std::string& m, bool err) {
        DisplayUserMessage("KWIQ Controller Plugin", err ? "Error" : "Info", m.c_str(), true, err, false, false, false);
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
        u.cWarn = (COLORREF)ParseColour(c.Get("ui.colour_warn"), u.cWarn);
        for (int i = 0; i < 8; i++) {
            std::string n = SectorColourName(i);
            unsigned long v = ParseColour(c.Get("ui.colour_sector_" + n), 0xFFFFFFFFul);
            if (v != 0xFFFFFFFFul) u.cStates[n] = (COLORREF)v;
        }
        coreEvery_  = c.GetInt("refresh.core_s", 60, 30, 3600);
        srdEvery_   = c.GetInt("refresh.srd_s", 300, 60, 86400);
        slotsEvery_ = c.GetInt("refresh.slots_s", 60, 15, 3600);
        if (!sectorUserSet_) sectorDraw_ = c.GetBool("ui.sector_enabled", true);

        std::string minV = c.Get("plugin.min_version", "0.0.0"), updUrl = c.Get("plugin.update_url");
        bool tooOld = CompareVersions(KWIQ_PLUGIN_VERSION, minV) < 0;
        tooOld_ = tooOld;
        u.alertOn = c.GetBool("alert.enabled", true);
        u.flashSec = c.GetInt("alert.flash_seconds", 10, 0, 60);
        u.alertA = (COLORREF)ParseColour(c.Get("alert.colour_a"), u.alertA);
        u.alertB = (COLORREF)ParseColour(c.Get("alert.colour_b"), u.alertB);
        { std::string f = c.Get("alert.sound_file", u.sound); size_t i = f.find_last_of("\\/"); u.sound = i == std::string::npos ? f : f.substr(i + 1); }
        // Core's kill switch (plugin.enabled = 0) only acts as EuroScope starts (the loader then does not load
        // the plugin): a plugin that is already running keeps working until EuroScope closes
        bool on = !tooOld;
        bool wasOn = enabled_.exchange(on);
        if (tooOld && wasOn) Notify("Plugin v" KWIQ_PLUGIN_VERSION " is too old (minimum " + minV + "), disabled." +
                                    (updUrl.empty() ? "" : " Update: " + updUrl), true);
        else if (on && !wasOn) Notify("Enabled again.", false);
        bool killed = !c.GetBool("plugin.enabled", true);
        if (killed && !killSwitchTold_.exchange(true))
            Notify("The vACC has switched the plugin off. It keeps working until EuroScope closes, and will not start next time.", false);
        else if (!killed) killSwitchTold_ = false;

        std::string motd = c.Get("plugin.message");
        std::string domestic = Trim(c.Get("srd.domestic_prefixes", "OK,OR"));
        if (domestic.empty()) domestic = "OK,OR";
        {
            std::lock_guard<std::mutex> g(mu_);
            ui_ = u;
            latest_ = Trim(c.Get("plugin.latest_version"));
            latestSha_ = Lower(Trim(c.Get("plugin.core_sha256")));
            // OKAC and ORBB are released separately, each with its own version
            upstreamAirac_ = { { "OKAC", c.Get("sectorfile.airac.okac") }, { "ORBB", c.Get("sectorfile.airac.orbb") } };
            sectorUrl_ = { { "OKAC", c.Get("sectorfile.url.okac") }, { "ORBB", c.Get("sectorfile.url.orbb") } };
            if (!motd.empty() && motd != motd_) pending_.emplace_back(motd, false);
            motd_ = motd;
            esAccepted_ = c.Get("euroscope.versions");
            esUrl_ = Trim(c.Get("euroscope.setup_url", ES_SETUP_URL));
            if (!IsWebLink(esUrl_)) esUrl_ = ES_SETUP_URL;   // only a web page is ever opened (#44)
            esConfigured_ = true;
            if (domestic != domesticPrefixes_) { domesticPrefixes_ = domestic; exitGen_++; }
        }
        rdf_.SetEndpoint(c.Get("rdf.endpoint", "127.0.0.1:49080"));
        discordCoreOn_ = c.GetBool("discord.enabled", true);
        // Core's push events (Laravel Reverb), and how often to poll METARs while they are not coming
        push_.Configure(Trim(c.Get("push.host")), c.GetInt("push.port", 443, 1, 65535), c.GetBool("push.tls", true),
                        Trim(c.Get("push.key")), { "metars", "notifications" });
        metarPoll_ = c.GetInt("metar.poll_seconds", 60, 30, 3600);
        CheckSectorFile(false);
    }

    // Compare the user's local sector file against Core's version of the same package (OKAC or ORBB).
    // Messages once per distinct (package, local, upstream); force = user asked (.kwiq version / .kwiq sectorfile).
    // main thread: the loaded sector file's name ([INFO] line and the name EuroScope reports), e.g.
    // "ORBB-2610_02", which names the package even when its folders don't
    void ReadSectorName() {
        std::string n;
        EuroScopePlugIn::CSectorElement info = SectorFileElementSelectFirst(EuroScopePlugIn::SECTOR_ELEMENT_INFO);
        if (info.IsValid() && info.GetName()) n = info.GetName();
        EuroScopePlugIn::CController me = ControllerMyself();
        if (me.IsValid() && me.GetSectorFileName()) n += std::string(" ") + me.GetSectorFileName();
        std::lock_guard<std::mutex> g(mu_);
        sectorName_ = kwiq::Trim(n);
    }

    void CheckSectorFile(bool force) {
        using namespace kwiq;
        std::map<std::string, std::string> ups, urls;
        std::string path, callsign, name;
        { std::lock_guard<std::mutex> g(mu_); ups = upstreamAirac_; urls = sectorUrl_; path = sectorPath_; callsign = myCallsign_; name = sectorName_; }
        std::string src;
        Airac l = sf::Local(path, src);
        // OKAC or ORBB: from where the version was found, the folder given, the sector file's name, where
        // the plugin is installed, then the callsign
        std::string pkg;
        for (const std::string& p : { src, path, name, sf::DllPath() })
            if (pkg.empty() && !p.empty()) pkg = SectorPackage(p, "");
        if (pkg.empty()) pkg = SectorPackage("", callsign);
        if (pkg.empty()) {
            { std::lock_guard<std::mutex> g(mu_); sf_ = SfStatus{}; }
            std::string key = "?pkg>" + (l.valid() ? l.Str() : std::string("?"));
            { std::lock_guard<std::mutex> g(mu_); if (!force && key == lastSfKey_) return; lastSfKey_ = key; }
            if (l.valid() || force)
                Notify("Cannot tell whether your sector file is OKAC or ORBB, so its version was not checked. "
                       "Use: .kwiq sectorfile <folder or AIRAC_VERSION.txt path>", false);
            return;
        }
        std::string up = ups[pkg], url = urls[pkg];
        Airac u = FindAirac(up);
        if (!u.valid()) {
            { std::lock_guard<std::mutex> g(mu_); sf_ = SfStatus{}; }
            if (force) Notify("Core has not published a version of the " + pkg + " sector file yet.", false);
            return;
        }
        // status for the logon alert (main thread reads it); resolve the sound only when it will be needed
        {
            SfStatus st;
            st.known = l.valid();
            st.outdated = l.valid() && CompareAirac(l, u) < 0;
            st.package = pkg; st.local = l.Str(); st.upstream = u.Str(); st.url = url;
            std::string soundFile, prev;
            { std::lock_guard<std::mutex> g(mu_); soundFile = ui_.sound; prev = sf_.sound; }
            st.sound = prev;
            if (st.outdated && (force || prev.empty())) {
                std::vector<std::string> starts;
                if (!src.empty()) starts.push_back(sf::ParentOf(src));
                if (!path.empty()) starts.push_back(sf::IsDir(path) ? path : sf::ParentOf(path));
                starts.push_back(sf::DllDir());
                st.sound = sf::FindSound(soundFile, starts);
            }
            std::lock_guard<std::mutex> g(mu_);
            sf_ = st;
        }
        std::string key = pkg + ":" + (l.valid() ? l.Str() : std::string("?")) + ">" + u.Str();
        { std::lock_guard<std::mutex> g(mu_); if (!force && key == lastSfKey_) return; lastSfKey_ = key; }
        std::string tail = url.empty() ? "" : " Download: " + url;
        if (!l.valid())
            Notify("Cannot find your " + pkg + " sector file version (current is " + u.Str() + "). Install the latest " + pkg +
                   " package, which has Plugins\\AIRAC_VERSION.txt, or use: .kwiq sectorfile <folder or AIRAC_VERSION.txt path>", false);
        else if (CompareAirac(l, u) < 0)
            Notify(pkg + " SECTOR FILE OUT OF DATE: you have " + l.Str() + ", current is " + u.Str() + "." + tail, true);
        else if (CompareAirac(l, u) > 0)
            Notify(pkg + " sector file " + l.Str() + " is newer than Core's " + u.Str() + " (Core not updated yet?).", false);
        else
            Notify(pkg + " sector file " + l.Str() + " is up to date.", false);
    }

    // worker thread: Core event + SRD (slots and the data feed run in CDataHandler)
    void Refresh(bool doCore, bool doSrd) {
        try {
            using namespace kwiq;
            std::string body, err;
            // Core refuses requests without a valid personal key once it requires them: say so once, not per request
            // Only a 401 means the key itself: anything else (Core down, a timeout) never sends anyone to myKWIQ
            auto fail = [this, &body](const std::string& src, const std::string& e) {
                if (e == "HTTP 401") {
                    std::string reason = kwiq::Trim(body).compare(0, 4, "KEY|") == 0 ? kwiq::Trim(kwiq::Trim(body).substr(4)) : "invalid";
                    { std::lock_guard<std::mutex> g(mu_); keyReason_ = reason; }
                    keyRefused_ = true;
                    SetError("API key", KeyRefusedMessage(reason));
                }
                else SetError(src, e);
            };

            if (doCore) {
                if (kwiq_net::HttpGet(CORE_CONFIG_URL, body, err)) {
                    ClearError("Config");
                    ClearError("API key");
                    keyRefused_ = false;
                    ApplyConfig(ParseConfig(body));
                } else fail("Config", err);
                if (!reduced_ && kwiq_net::HttpGet(CORE_EVENT_URL, body, err)) {
                    ClearError("Core");
                    EventData ev = ParseCoreEvent(body);
                    std::lock_guard<std::mutex> g(mu_);
                    ev_ = std::move(ev); evGen_++;
                } else if (!reduced_) fail("Core", err);
                if (!reduced_) {                       // DiscordEuroScope's messages, images and buttons
                    if (kwiq_net::HttpGet(CORE_DISCORD_URL, body, err)) { ClearError("Discord"); discord_.Configure(body, discordCoreOn_); }
                    else if (err == "HTTP 404") discord_.Configure("", discordCoreOn_);
                    else fail("Discord", err);
                }
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
                        } else fail("SRD", err);
                    } else ClearError("SRD");
                } else fail("SRD", err);
            }
            if (doSrd && !reduced_) {                  // top-down orders and transition levels; none yet is fine
                if (kwiq_net::HttpGet(CORE_TOPDOWN_ORDERS_URL, body, err)) { ClearError("Top-down"); auto td = ParseTopDownOrders(body); std::lock_guard<std::mutex> g(mu_); topDown_ = std::move(td); }
                else if (err != "HTTP 404") fail("Top-down", err);
                if (kwiq_net::HttpGet(CORE_TRL_URL, body, err)) { ClearError("TRL"); auto t = ParseTrl(body); std::lock_guard<std::mutex> g(mu_); trl_ = std::move(t); }
                else if (err != "HTTP 404") fail("TRL", err);
            }
            if (doSrd && !reduced_) {                  // RDF settings per callsign pattern; none yet is fine
                if (kwiq_net::HttpGet(CORE_RDF_URL, body, err)) { ClearError("RDF"); rdf_.SetRows(ParseRdf(body)); }
                else if (err == "HTTP 404") rdf_.SetRows({});
                else fail("RDF", err);
            }
            if (doSrd) {                               // sector aliases: tiny, refreshed with the SRD
                if (kwiq_net::HttpGet(CORE_SECTORS_URL, body, err)) {
                    ClearError("Sectors");
                    SectorMap m = ParseSectors(body);
                    std::lock_guard<std::mutex> g(mu_);
                    sectors_ = std::move(m);
                } else fail("Sectors", err);
            }
            if (doSrd) {                               // FIR exit points, refreshed with the SRD
                std::string exitErr;
                bool ok = kwiq_net::HttpGet(CORE_FIR_EXITS_URL, body, exitErr);
                if (ok || exitErr == "HTTP 404") {     // 404: Core has none yet, so no exits
                    ClearError("FIR exits");
                    FirExits m = ok ? ParseFirExits(body) : FirExits{};
                    std::lock_guard<std::mutex> g(mu_);
                    firExits_ = std::move(m); exitGen_++;
                } else fail("FIR exits", exitErr);
            }
            if (doSrd) {                               // flight plan check rules, refreshed with the SRD
                std::string fpcErr;
                bool ok = kwiq_net::HttpGet(CORE_FPC_URL, body, fpcErr);
                if (ok || fpcErr == "HTTP 404") {      // 404: Core has none yet, so no checks
                    ClearError("FPC");
                    fpc::Rules rules = ok ? fpc::ParseRules(body) : fpc::Rules{};
                    std::lock_guard<std::mutex> g(mu_);
                    fpcRules_ = std::move(rules); fpcGen_++;
                } else fail("FPC", fpcErr);
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
    bool buttonShown_ = true;                                       // main thread only
    enum class ButtonPlace { Default, Offset, TopLeft };            // main thread only: how buttonOffset_ is meant
    ButtonPlace buttonPlace_ = ButtonPlace::Default;
    kwiq::ButtonXY buttonOffset_;
    std::string apiKey_;                                            // main thread only (the worker uses kwiq_net's copy)
    KeyLink keyLink_;
    KwiqRdf rdf_{ this };                                           // RDF and the TrackAudio bridge
    KwiqDiscord discord_{ this };                                   // DiscordEuroScope
    std::atomic<bool> discordCoreOn_{ true };                       // discord.enabled in Core's config
    // QNH monitor and procedure notifications (#4)
    KwiqPush push_;
    kwiq::TopDownOrders topDown_;                                   // mu_
    std::vector<kwiq::TrlRule> trl_;                                // mu_
    std::map<std::string, std::string> metars_;                     // mu_
    std::vector<kwiq::ProcedureNotice> notices_;                    // mu_
    std::vector<std::pair<std::string, bool>> pendingMarks_;        // mu_: read / unread, for Core
    std::atomic<bool> metarsNew_{ false }, noticesNew_{ false }, auxBusy_{ false }, markBusy_{ false };
    bool metarDue_ = true, noticesDue_ = true;                      // main thread
    std::time_t lastMetar_ = 0, lastNotices_ = 0;
    std::atomic<int> metarPoll_{ 60 };
    std::map<std::string, std::pair<int, int>> qnhSeen_;            // main thread: ICAO -> QNH, TRL this session
    std::set<std::string> noticesTold_;                             // main thread: unread notifications already announced
    std::string qnhCallsign_;
    std::thread auxWorker_, markWorker_;
    std::string keyPromptedFor_, keyToldFor_;                      // main thread only: the key problem last offered / told about
    bool justConnected_ = false;                                    // main thread only: logged on since the last key check
    std::string esVersion_;                                         // the running EuroScope's, read once at start
    bool esChecked_ = false;                                        // main thread only
    std::time_t startedAt_ = 0, lastNameRead_ = 0;
    HANDLE instanceLock_ = nullptr;
    bool duplicate_ = false;
    const bool reduced_ = gStartMode == (int)kwiq::StartMode::Reduced;   // set by the loader before construction
    std::atomic<bool> keyRefused_{ false };
    std::string keyReason_;                                          // guarded by mu_: why Core last refused the key
    bool connected_ = false, soundPlayed_ = false, soundLooping_ = false, bannerWas_ = false;   // main thread only
    std::time_t connectedAt_ = 0, previewUntil_ = 0;                // main thread only
    static constexpr std::time_t PREVIEW_SECONDS = 15;
    static constexpr std::time_t STARTUP_SECONDS = 180;             // popups are only shown this soon after starting
    std::atomic<bool> quit_{ false }, tooOld_{ false }, killSwitchTold_{ false };
    std::time_t lastCore_ = 0, lastSrd_ = 0, lastSlotFetch_ = 0;   // main thread only
    long todayIdx_ = -1;                                            // main thread only
    std::string todayStr_;

    // guarded by mu_
    kwiq::EventData ev_;
    kwiq::SrdData srd_;
    kwiq::SectorMap sectors_;
    kwiq::FirExits firExits_;
    kwiq::fpc::Rules fpcRules_;
    std::string domesticPrefixes_ = "OK,OR";                        // Core's srd.domestic_prefixes
    unsigned evGen_ = 1, srdGen_ = 1, exitGen_ = 1, fpcGen_ = 1;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::vector<std::pair<std::string, bool>> pending_;
    UiCfg ui_;
    SfStatus sf_;
    std::string latest_, latestSha_;
    std::map<std::string, std::string> upstreamAirac_, sectorUrl_;   // per package: OKAC, ORBB
    std::string sectorPath_, motd_, lastSfKey_, myCallsign_, sectorName_;
    std::string esAccepted_, esUrl_ = ES_SETUP_URL;                 // Core's euroscope.versions and euroscope.setup_url
    bool esConfigured_ = false;                                     // Core's config has arrived at least once
    std::map<std::string, std::string> lastErr_;
};

// Radar screen overlay: draws the sector indicator above the symbol of every aircraft that is
// tracked by a position with an alias in Core (NR, SR, BGD ...). Core decides which positions and which text.
class CKWIQRadar : public EuroScopePlugIn::CRadarScreen {
public:
    CKWIQRadar() {
        font_ = CreateFontA(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
        CKWIQPlugin::RegisterRadar(this, true);
    }
    virtual ~CKWIQRadar() {
        CKWIQPlugin::RegisterRadar(this, false);
        if (font_) DeleteObject(font_);
        for (auto& l : logos_) DeleteObject(l.second);
        if (banner_) DeleteObject(banner_);
    }

    virtual void OnAsrContentToBeClosed(void) override { delete this; }

    // As in CKWIQPlugin, every callback catches everything (#45)
    virtual void OnRefresh(HDC hDC, int Phase) override try {
        if (!gPlugin) return;
        if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_LISTS) {
            std::time_t now = std::time(nullptr);
            if (gPlugin->SctBannerShown(now)) DrawSctBanner(hDC, gPlugin->SctBannerColour(now));
            if (gPlugin->ButtonShown()) DrawButton(hDC);
        }
        if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS) gPlugin->DrawRdf(this, hDC);
        if (Phase != EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS || !gPlugin->SectorDrawEnabled()) return;
        RECT area = GetRadarArea();
        int saved = SaveDC(hDC);
        SetBkMode(hDC, TRANSPARENT);
        std::array<COLORREF, 8> colours = gPlugin->SectorColours();
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
            if (alias.empty()) continue;                                    // no alias for that position
            POINT p = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());
            if (p.x < area.left || p.x > area.right || p.y < area.top || p.y > area.bottom) continue;
            int state = fp.GetState();
            SetTextColor(hDC, colours[state >= 0 && state < 8 ? state : 0]);   // the colour of its tag
            TextOutA(hDC, p.x, p.y - 9, alias.c_str(), (int)alias.size());
        }
        RestoreDC(hDC, saved);
    } catch (...) {}

    // Any button opens the KWIQ menu, a left or a right click
    virtual void OnClickScreenObject(int ObjectType, const char*, POINT, RECT Area, int) override try {
        if (ObjectType == KWIQ_OBJ_BUTTON && gPlugin) gPlugin->ButtonClicked(Area);
    } catch (...) {}
    virtual void OnMoveScreenObject(int ObjectType, const char*, POINT Pt, RECT, bool Released) override try {
        if (ObjectType != KWIQ_OBJ_BUTTON || !gPlugin || buttonSize_ <= 0) return;
        gPlugin->MoveButton(GetRadarArea(), Pt, buttonSize_, Released);
        RequestRefresh();
    } catch (...) {}
    // the menu and the key box opened from the button report back here
    virtual void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override try {
        if (gPlugin && FunctionId >= KWIQ_MENU_KEY && FunctionId <= KWIQ_MENU_LAST) gPlugin->MenuAction(FunctionId, sItemString, Pt, Area);
    } catch (...) {}

private:
    // Red box with white OUTDATED SCT FILE, in the middle of the radar screen
    void DrawSctBanner(HDC hDC, COLORREF fill) {
        static const char* text = "OUTDATED SCT FILE";
        RECT area = GetRadarArea();
        int saved = SaveDC(hDC);
        SelectObject(hDC, banner_);
        SIZE sz; GetTextExtentPoint32A(hDC, text, (int)strlen(text), &sz);
        int w = sz.cx + 56, h = sz.cy + 22;
        int x = area.left + ((area.right - area.left) - w) / 2, y = area.top + ((area.bottom - area.top) - h) / 2;
        RECT r = { x, y, x + w, y + h };
        HBRUSH b = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        SelectObject(hDC, b);
        SelectObject(hDC, pen);
        Rectangle(hDC, r.left, r.top, r.right, r.bottom);
        SetBkMode(hDC, TRANSPARENT);
        SetTextColor(hDC, RGB(255, 255, 255));
        DrawTextA(hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RestoreDC(hDC, saved);
        DeleteObject(b);
        DeleteObject(pen);
    }

    // The logo, alpha-blended at BUTTON_SIZE scaled for the screen's DPI, from the image embedded at that size
    // (or the next bigger one). The whole square is the click target; nothing outside it is covered.
    void DrawButton(HDC hDC) {
        int dpi = GetDeviceCaps(hDC, LOGPIXELSX);
        int size = kwiq::ScaleForDpi(CKWIQPlugin::BUTTON_SIZE, dpi);
        buttonSize_ = size;
        RECT r = gPlugin->ButtonRect(GetRadarArea(), size, dpi);
        if (!logosLoaded_) { logosLoaded_ = true; LoadLogos(); }
        std::vector<int> sizes;
        for (auto& l : logos_) sizes.push_back(l.first);
        int best = kwiq::BestLogoSize(sizes, size);
        auto it = logos_.find(best);
        if (it != logos_.end()) {
            HDC mem = CreateCompatibleDC(hDC);
            if (mem) {
                HGDIOBJ old = SelectObject(mem, it->second);
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                GdiAlphaBlend(hDC, r.left, r.top, size, size, mem, 0, 0, best, best, bf);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
        } else {                                                        // the images didn't load: a plain mark
            HBRUSH fill = CreateSolidBrush(RGB(11, 65, 54));
            FillRect(hDC, &r, fill);
            DeleteObject(fill);
        }
        AddScreenObject(KWIQ_OBJ_BUTTON, "KWIQ", r, true, "KWIQ options");    // movable
    }
    // The logo images in kwiq.rc: 32-bit BMPs with straight alpha, premultiplied here for AlphaBlend
    void LoadLogos() {
        HMODULE mod = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&LogoMarker), &mod)) return;
        for (int id : { IDR_KWIQ_LOGO_28, IDR_KWIQ_LOGO_35, IDR_KWIQ_LOGO_42, IDR_KWIQ_LOGO_49, IDR_KWIQ_LOGO_56 }) {
            HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));     // RT_RCDATA
            HGLOBAL h = res ? LoadResource(mod, res) : nullptr;
            const unsigned char* p = h ? static_cast<const unsigned char*>(LockResource(h)) : nullptr;
            DWORD n = res ? SizeofResource(mod, res) : 0;
            if (!p || n < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) continue;
            BITMAPFILEHEADER fh;
            BITMAPINFOHEADER ih;
            std::memcpy(&fh, p, sizeof fh);
            std::memcpy(&ih, p + sizeof fh, sizeof ih);
            if (fh.bfType != 0x4D42 || ih.biBitCount != 32 || ih.biCompression != BI_RGB || ih.biWidth <= 0 ||
                ih.biWidth > 256 || ih.biHeight != ih.biWidth) continue;
            size_t bytes = (size_t)ih.biWidth * (size_t)ih.biWidth * 4;
            if (fh.bfOffBits > n || n - fh.bfOffBits < bytes) continue;
            BITMAPINFO bi = {};
            bi.bmiHeader = ih;
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            void* bits = nullptr;
            HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bmp || !bits) { if (bmp) DeleteObject(bmp); continue; }
            std::memcpy(bits, p + fh.bfOffBits, bytes);
            kwiq::PremultiplyBgra(static_cast<unsigned char*>(bits), bytes / 4);
            GdiFlush();
            logos_[(int)ih.biWidth] = bmp;
        }
    }
    static void LogoMarker() {}                                         // an address in this DLL, for its resources

    HFONT font_ = nullptr;
    HFONT banner_ = CreateFontA(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
    std::map<int, HBITMAP> logos_;                                  // the KWIQ button's logo, by size in px
    bool logosLoaded_ = false;
    int buttonSize_ = 0;                                            // as last drawn, for dragging
};

EuroScopePlugIn::CRadarScreen* CKWIQPlugin::OnRadarScreenCreated(const char*, bool, bool, bool, bool) try {
    return new CKWIQRadar();
} catch (...) { return nullptr; }

// Called from the windows' WndProcs, so nothing may escape into Windows either
static void SrdSearchThunk(const std::string& adep, const std::string& ades, const std::string& lvl) {
    try { if (gPlugin) gPlugin->RunSrdSearch(adep, ades, lvl); } catch (...) {}
}

// Entry points: declared (with dllexport) in EuroScopePlugIn.h, so no extern "C" here. The loader
// (KWIQPlugin.dll, loader.cpp) uses the KWIQCore* ones below instead, after the updater has run.
void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    *ppPlugInInstance = gPlugin = new CKWIQPlugin();
}
void __declspec(dllexport) EuroScopePlugInExit(void) {
    CKWIQPlugin* p = gPlugin;
    gPlugin = nullptr;
    delete p;
}

// The loader (v0.6 and later): mode is a kwiq::StartMode from the updater (Full or Reduced)
extern "C" __declspec(dllexport) void KWIQCoreStart(EuroScopePlugIn::CPlugIn** ppPlugInInstance, int mode) {
    gStartMode = mode == (int)kwiq::StartMode::Reduced ? mode : (int)kwiq::StartMode::Full;
    EuroScopePlugInInit(ppPlugInInstance);
}
// v0.5 loaders, which start the plugin in full
extern "C" __declspec(dllexport) void KWIQCoreInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    KWIQCoreStart(ppPlugInInstance, (int)kwiq::StartMode::Full);
}
extern "C" __declspec(dllexport) void KWIQCoreExit() {
    EuroScopePlugInExit();
}
extern "C" __declspec(dllexport) const char* KWIQCoreVersion() {
    return KWIQ_PLUGIN_VERSION;
}
