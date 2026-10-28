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
#include <map>
#include <array>
#include <cstring>
#include <mmsystem.h>       // PlaySound (link winmm)
#include <shellapi.h>       // ShellExecute
#include <winver.h>         // GetFileVersionInfo (link version)
#include "requestMenu.hpp"      // CVCHPlugin (VCH)
#include "kwiq_logic.hpp"
#include "VatsimAPI.h"
#include "keylink.hpp"

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
    KWIQ_ITEM_FPC         = 1004,
    KWIQ_ITEM_SECTOR      = 1005,   // sector indicator of the position that tracks the aircraft
    KWIQ_FUNC_EVENT_ROUTES = 2001,   // "Open event routes window"
    KWIQ_FUNC_FPC_EXPLAIN  = 2002,   // "Explain FPC result"
    KWIQ_FUNC_SRD_SEARCH   = 2003,   // "Open SRD search window"
    // the KWIQ button on the radar screen and its menu
    KWIQ_OBJ_BUTTON        = 2100,
    KWIQ_MENU_KEY          = 2101,
    KWIQ_MENU_SECTORS      = 2102,
    KWIQ_MENU_AUTOREFRESH  = 2104,
    KWIQ_MENU_EVENT_ROUTES = 2105,
    KWIQ_MENU_SRD          = 2106,
    KWIQ_MENU_SECTORFILE   = 2107,
    KWIQ_MENU_UPDATE       = 2108,
    KWIQ_MENU_REFRESH      = 2109,
    KWIQ_MENU_STATUS       = 2110,
    KWIQ_MENU_HIDE         = 2111
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
static bool Make(Win& win, POINT, const std::string& title, int ch) {
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
    int x, y;
    Centre(ww, wh, x, y);                    // in the middle of EuroScope's screen

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

// ---- Notice popups -------------------------------------------------------------------------
// Windows-style message boxes (title bar, icon, message, buttons on a grey strip), but not modal:
// EuroScope keeps running behind them and they never take keyboard focus. They are only shown when
// EuroScope starts, never in the middle of a session.
enum { NOTICE_KEY = 2, NOTICE_INFO = 3, NOTICE_ES_VERSION = 4 };
static void (*gOnNotice)(int kind, int button) = nullptr;       // button 1 = first, 2 = second (or closed)
static const char* const kNoticeClass = "KWIQNoticeWnd";
static bool gNoticeReg = false;
enum { NW = 440, NPAD = 22, NICON = 32, NBTN_W = 112, NBTN_H = 28, NFOOT = 48 };
struct Notice {
    HWND hwnd = nullptr;
    int kind = 0, textH = 0;
    LPCSTR icon = nullptr;
    std::string body;
};
static Notice gKey, gInfo, gEsVersion;
static HFONT gFontBody = nullptr;

static void PaintNotice(HWND h, const Notice& n) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    RECT body = rc, foot = rc;
    body.bottom = rc.bottom - NFOOT; foot.top = body.bottom;
    FillRect(dc, &body, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HBRUSH f = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(dc, &foot, f); DeleteObject(f);
    if (n.icon) DrawIcon(dc, NPAD, NPAD, LoadIconA(nullptr, n.icon));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    HGDIOBJ old = SelectObject(dc, gFontBody);
    RECT tb = { NPAD + NICON + 14, NPAD + 6, rc.right - NPAD, body.bottom - NPAD };
    DrawTextA(dc, n.body.c_str(), -1, &tb, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
    SelectObject(dc, old);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK NoticeProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Notice* n = (Notice*)GetWindowLongPtrA(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE:
        SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTA*)l)->lpCreateParams);
        break;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: {                                 // the key setup cannot be dragged by its title bar
        LRESULT hit = DefWindowProcA(h, m, w, l);
        return (n && n->kind == NOTICE_KEY && hit == HTCAPTION) ? HTCLIENT : hit;
    }
    case WM_SYSCOMMAND:
        if (n && n->kind == NOTICE_KEY && (w & 0xFFF0) == SC_MOVE) return 0;
        break;
    case WM_PAINT:
        if (n) { PaintNotice(h, *n); return 0; }
        break;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (n && (id == 1 || id == 2)) {
            int kind = n->kind;
            DestroyWindow(h);
            if (gOnNotice) gOnNotice(kind, id);
        }
        return 0;
    }
    case WM_CLOSE: {                                     // the title bar's X counts as the last button
        int kind = n ? n->kind : 0;
        if (kind == NOTICE_KEY) return 0;                // the key setup only closes once a key is connected
        DestroyWindow(h);
        if (gOnNotice && kind) gOnNotice(kind, 2);
        return 0;
    }
    case WM_NCDESTROY:
        if (n) *n = Notice();
        SetWindowLongPtrA(h, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcA(h, m, w, l);
}

// btn1 is on the right, as the main choice; btn2 (optional) to its left
static bool ShowNotice(Notice& n, int kind, const std::string& title, const std::string& body, LPCSTR icon,
                       const char* btn1, const char* btn2) {
    if (!gMod)
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&NoticeProc, &gMod);
    if (!gNoticeReg) {
        WNDCLASSEXA wc; ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = NoticeProc;
        wc.hInstance = gMod;
        wc.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512));
        wc.lpszClassName = kNoticeClass;
        if (!RegisterClassExA(&wc)) return false;
        gNoticeReg = true;
    }
    if (!gFontBody) {                                    // the font Windows uses in its own message boxes
        NONCLIENTMETRICSA ncm; ZeroMemory(&ncm, sizeof(ncm));
        ncm.cbSize = sizeof(ncm);
        gFontBody = SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)
            ? CreateFontIndirectA(&ncm.lfMessageFont) : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    if (n.hwnd) DestroyWindow(n.hwnd);                  // WM_NCDESTROY resets n
    n = Notice();
    n.kind = kind; n.body = body; n.icon = icon;

    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, gFontBody);
    RECT tr = { 0, 0, NW - 2 * NPAD - NICON - 14, 0 };
    DrawTextA(dc, body.c_str(), -1, &tr, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    int bodyH = (std::max)((int)(tr.bottom - tr.top) + 12, (int)NICON) + 2 * NPAD;

    // The key setup has no X: it stays until a key is connected
    DWORD style = WS_POPUP | WS_CAPTION | (kind == NOTICE_KEY ? 0 : WS_SYSMENU), ex = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_NOACTIVATE;
    RECT rc = { 0, 0, NW, bodyH + NFOOT };
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int x, y;
    Centre(ww, wh, x, y);
    HWND h = CreateWindowExA(ex, kNoticeClass, title.c_str(), style, x, y, ww, wh, nullptr, nullptr, gMod, &n);
    if (!h) { n = Notice(); return false; }
    n.hwnd = h;
    int by = bodyH + (NFOOT - NBTN_H) / 2;
    HWND b1 = Child("BUTTON", btn1, BS_DEFPUSHBUTTON, NW - NPAD - NBTN_W, by, NBTN_W, NBTN_H, h, 1);
    SendMessageA(b1, WM_SETFONT, (WPARAM)gFontBody, TRUE);
    if (btn2 && *btn2) {
        HWND b2 = Child("BUTTON", btn2, BS_PUSHBUTTON, NW - NPAD - 2 * NBTN_W - 8, by, NBTN_W, NBTN_H, h, 2);
        SendMessageA(b2, WM_SETFONT, (WPARAM)gFontBody, TRUE);
    }
    ShowWindow(h, SW_SHOWNOACTIVATE);
    return true;
}

// API key setup: waits while the controller connects on myKWIQ, and closes only once the key arrives.
static void ShowKeyPrompt() {
    ShowNotice(gKey, NOTICE_KEY, "KWIQ API Key Setup",
               "Sign in to myKWIQ in your browser and click Connect EuroScope. This window closes once your API key "
               "is connected. The link is also on your clipboard. Help: discord.kuwait-iraqvacc.net",
               IDI_INFORMATION, "Reopen Browser", nullptr);
}
static bool KeyPromptOpen() { return gKey.hwnd != nullptr; }
static void CloseKeyPrompt() { if (gKey.hwnd) DestroyWindow(gKey.hwnd); }
// A plain confirmation, e.g. once the API key has been saved
static void ShowInfo(const std::string& title, const std::string& body) {
    ShowNotice(gInfo, NOTICE_INFO, title, body, IDI_INFORMATION, "OK", nullptr);
}
// Incorrect EuroScope version: OK opens the setup guide
static void ShowEsVersion(const std::string& body) {
    ShowNotice(gEsVersion, NOTICE_ES_VERSION, "Incorrect EuroScope Version", body, IDI_WARNING, "OK", nullptr);
}

static void Shutdown() {
    if (gKey.hwnd) DestroyWindow(gKey.hwnd);
    if (gInfo.hwnd) DestroyWindow(gInfo.hwnd);
    if (gEsVersion.hwnd) DestroyWindow(gEsVersion.hwnd);
    if (gFontBody) { DeleteObject(gFontBody); gFontBody = nullptr; }
    if (gNoticeReg && gMod) { UnregisterClassA(kNoticeClass, gMod); gNoticeReg = false; }
    if (gEvt.hwnd) DestroyWindow(gEvt.hwnd);
    if (gSrd.hwnd) DestroyWindow(gSrd.hwnd);
    if (gClassReg && gMod) { UnregisterClassA(kClass, gMod); gClassReg = false; }
}

} // namespace kwiq_ui

class CKWIQPlugin;
static CKWIQPlugin* gPlugin = nullptr;

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
// Where the plugin is installed: KWIQPlugin.dll, the loader EuroScope loads (this file is kept in
// %APPDATA%\KWIQ\plugin), or this file when it is loaded directly
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
// change per release and per vACC, so search instead of assuming: near the sector file first, then
// near the plugin, then every package under %APPDATA%\EuroScope (newest wins).
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
static bool NewerThan(const std::string& a, const std::string& b) {
    WIN32_FILE_ATTRIBUTE_DATA x, y;
    if (!GetFileAttributesExA(a.c_str(), GetFileExInfoStandard, &x)) return false;
    if (!GetFileAttributesExA(b.c_str(), GetFileExInfoStandard, &y)) return true;
    return CompareFileTime(&x.ftLastWriteTime, &y.ftLastWriteTime) > 0;
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
    char app[MAX_PATH] = {};
    if (GetEnvironmentVariableA("APPDATA", app, MAX_PATH)) {
        std::string best;
        for (const auto& pkg : SubDirs(std::string(app) + "\\EuroScope")) {
            std::string r = Probe(pkg, file);
            if (!r.empty() && (best.empty() || NewerThan(r, best))) best = r;
        }
        if (!best.empty()) return best;
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
// The ESE of the loaded sector file: the one given with ".kwiq sectorfile", else in that folder or the
// plugin's folder and their parents, else in any package under %APPDATA%\EuroScope. The one named like the
// loaded sector file wins, else (only near the plugin or the folder given) the newest.
static std::vector<std::string> EseIn(const std::string& dir) {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.ese").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(dir + "\\" + fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}
static bool IsEse(const std::string& p) {
    return p.size() > 4 && kwiq::Upper(p.substr(p.size() - 4)) == ".ESE";
}
static bool NamedLike(const std::string& ese, const std::string& sectorName) {
    std::string f = ese.substr(ese.find_last_of("\\/") + 1);
    f = kwiq::Upper(f.substr(0, f.size() - 4));                       // without .ese
    return !f.empty() && kwiq::Upper(sectorName).find(f) != std::string::npos;
}
static std::string FindEse(const std::string& userPath, const std::string& sectorName) {
    if (IsEse(userPath) && FileExists(userPath)) return userPath;
    std::vector<std::string> starts, found;
    if (!userPath.empty()) starts.push_back(IsDir(userPath) ? userPath : ParentOf(userPath));
    starts.push_back(DllDir());
    for (const auto& s : starts) {
        std::string d = s;
        for (int up = 0; up < 4 && !d.empty(); up++, d = ParentOf(d))
            for (const auto& e : EseIn(d)) found.push_back(e);
    }
    std::string best;
    for (const auto& e : found) if (NamedLike(e, sectorName)) return e;
    for (const auto& e : found) if (best.empty() || NewerThan(e, best)) best = e;
    if (!best.empty()) return best;
    char app[MAX_PATH] = {};
    if (GetEnvironmentVariableA("APPDATA", app, MAX_PATH))
        for (const auto& pkg : SubDirs(std::string(app) + "\\EuroScope"))
            for (const auto& e : EseIn(pkg))
                if (NamedLike(e, sectorName) && (best.empty() || NewerThan(e, best))) best = e;
    return best;
}
} // namespace sf

// ---- personal API key ------------------------------------------------------------------------
// Generated on myKWIQ and kept in %APPDATA%\KWIQ\plugin-key.txt, outside the EuroScope profile files,
// so it is never shared along with a sector file package or someone's settings.
namespace apikey {
static std::string Dir() {
    char b[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("APPDATA", b, MAX_PATH);
    return n && n < MAX_PATH ? std::string(b) + "\\KWIQ" : std::string();
}
static std::string Load() {
    std::string d = Dir();
    if (d.empty()) return "";
    std::string k = kwiq::Trim(sf::ReadHead(d + "\\plugin-key.txt", 128));
    return kwiq::IsApiKey(k) ? k : "";
}
static bool Save(const std::string& key) {
    std::string d = Dir();
    if (d.empty()) return false;
    CreateDirectoryA(d.c_str(), nullptr);
    std::string f = d + "\\plugin-key.txt";
    if (key.empty()) { DeleteFileA(f.c_str()); return true; }
    FILE* fp = nullptr;
    if (fopen_s(&fp, f.c_str(), "wb") != 0 || !fp) return false;
    bool ok = fwrite(key.data(), 1, key.size(), fp) == key.size();
    fclose(fp);
    return ok;
}
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
static void NoticeThunk(int kind, int button);

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
        RegisterTagItemType("Sector Indicator", KWIQ_ITEM_SECTOR);
        RegisterTagItemFunction("Open event routes window", KWIQ_FUNC_EVENT_ROUTES);
        RegisterTagItemFunction("Explain FPC result",       KWIQ_FUNC_FPC_EXPLAIN);
        RegisterTagItemFunction("Open SRD search window",   KWIQ_FUNC_SRD_SEARCH);
        kwiq_ui::gOnSearch = &SrdSearchThunk;
        kwiq_ui::gOnNotice = &NoticeThunk;
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
        // EuroScope should load KWIQPlugin.dll, which updates and then loads this file; loaded directly,
        // nothing updates it
        if (!GetModuleHandleA("KWIQPlugin.dll"))
            Msg("This copy of the plugin cannot update itself. Please download it again from myKWIQ (EuroScope Plugin) "
                "and load KWIQPlugin.dll.", true);
    }
    virtual ~CKWIQPlugin() {
        kwiq_ui::gOnSearch = nullptr;
        kwiq_ui::gOnNotice = nullptr;
        kwiq_ui::Shutdown();
        keyLink_.Stop();
        if (soundLooping_) PlaySoundA(nullptr, nullptr, 0);
        quit_ = true;                                        // aborts a running update download
        if (worker_.joinable()) worker_.join();
        CDataHandler::Shutdown();
        kwiq_net::GlobalCleanup();
        if (instanceLock_) CloseHandle(instanceLock_);
    }

    virtual void OnTimer(int Counter) override {
        if (duplicate_) return;
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
            const char* bp = GetDataFromSettings("kwiq_button_at");
            int bx, by;
            if (bp && sscanf_s(bp, "%d,%d", &bx, &by) == 2 && bx >= 0 && by >= 0) { buttonX_ = bx; buttonY_ = by; }
            DeleteFileA((sf::DllPath() + ".old").c_str());   // left over from a previous self-update (fails while still loaded)
        }
        if (now - lastNameRead_ >= 10) { lastNameRead_ = now; ReadSectorName(); }
        if (now - lastEseRead_ >= 60) { lastEseRead_ = now; ReadEse(); }
        TickConnection(now);
        TickKeySetup(now);
        TickEuroScopeVersion(now);
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
        if (FunctionId >= KWIQ_MENU_KEY && FunctionId <= KWIQ_MENU_HIDE) { MenuAction(FunctionId, sItemString, Pt, Area); return; }
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
        if (duplicate_) return false;                        // the copy that is in use answers
        std::string c = kwiq::Upper(kwiq::Trim(sCommandLine));
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
        if (c == ".KWIQ KEY" || c.compare(0, 10, ".KWIQ KEY ") == 0) {
            std::string key = kwiq::Trim(std::string(sCommandLine).substr(9));
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
        if (c == ".KWIQ AUTOREFRESH") {
            autoRefresh_ = !autoRefresh_;
            Msg(std::string("Slot auto refresh: ") + (autoRefresh_ ? "on" : "off"), false);
            return true;
        }
        if (c == ".KWIQ STATUS") {
            std::lock_guard<std::mutex> g(mu_);
            std::string s = "v" KWIQ_PLUGIN_VERSION " | " + std::string(enabled_ ? "enabled" : "DISABLED by Core") +
                " | EuroScope " + (esVersion_.empty() ? std::string("?") : esVersion_) +
                " | API key " + (apiKey_.empty() ? std::string("not set") : std::string("connected")) + WineNote() +
                " | sector files OKAC " + (upstreamAirac_["OKAC"].empty() ? "?" : upstreamAirac_["OKAC"]) +
                " ORBB " + (upstreamAirac_["ORBB"].empty() ? "?" : upstreamAirac_["ORBB"]) +
                " | SRD routes " + std::to_string(srd_.rows.size()) + " | sectors " +
                std::to_string(sectors_.size()) + " | ESE positions " + std::to_string(esePositions_.size()) + " | ";
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

    // main thread: sector indicator of the position tracking an aircraft, only for the positions Core lists
    // in its sector aliases. A controller EuroScope identifies as an ESE position (by prefix, suffix and
    // frequency, so a relief callsign or a controller covering another position counts as the position
    // actually worked) gets that position's ESE ID when Core lists the position or the callsign. Otherwise
    // Core's alias for the callsign. Empty if nobody tracks it or Core lists neither.
    std::string SectorFor(const char* trackerCallsign) {
        std::string cs = kwiq::Upper(trackerCallsign);
        EuroScopePlugIn::CController me = ControllerMyself();
        EuroScopePlugIn::CController c = me.IsValid() && kwiq::Upper(me.GetCallsign()) == cs ? me : ControllerSelect(trackerCallsign);
        std::lock_guard<std::mutex> g(mu_);
        auto it = sectors_.find(cs);
        if (c.IsValid() && c.GetPositionIdentified() && c.GetPositionId() && *c.GetPositionId()) {
            std::string id = kwiq::Upper(c.GetPositionId());
            auto pos = esePositions_.find(id);
            if (it != sectors_.end() || (pos != esePositions_.end() && sectors_.count(pos->second))) return id.substr(0, 4);
            return "";
        }
        return it == sectors_.end() ? std::string() : it->second;
    }
    bool SectorDrawEnabled() const { return sectorDraw_ && enabled_ && !duplicate_; }
    bool Duplicate() const { return duplicate_; }

    // ---- the KWIQ button and its menu (main thread) ------------------------------------------
    // A small red button at the top centre of each radar screen; it can be dragged anywhere, and
    // ".kwiq button" (or Hide Button in its menu) hides it and brings it back.
    static constexpr int BUTTON_W = 44, BUTTON_H = 18;
    bool ButtonShown() const { return buttonShown_ && !duplicate_; }
    RECT ButtonRect(const RECT& radar) const {
        // Top centre until it is dragged somewhere else; always kept inside the radar area
        int w = radar.right - radar.left, h = radar.bottom - radar.top;
        int x = buttonX_ < 0 ? (w - BUTTON_W) / 2 : buttonX_, y = buttonY_ < 0 ? 10 : buttonY_;
        x = (std::max)(0, (std::min)(x, w - BUTTON_W));
        y = (std::max)(0, (std::min)(y, h - BUTTON_H));
        RECT r = { radar.left + x, radar.top + y, radar.left + x + BUTTON_W, radar.top + y + BUTTON_H };
        return r;
    }
    void MoveButton(const RECT& radar, POINT pt, bool released) {
        buttonX_ = (std::max)(0, (std::min)((int)(pt.x - radar.left) - BUTTON_W / 2, (int)(radar.right - radar.left) - BUTTON_W));
        buttonY_ = (std::max)(0, (std::min)((int)(pt.y - radar.top) - BUTTON_H / 2, (int)(radar.bottom - radar.top) - BUTTON_H));
        if (released) SaveDataToSettings("kwiq_button_at", "KWIQ button position", (std::to_string(buttonX_) + "," + std::to_string(buttonY_)).c_str());
    }
    void ShowButton(bool shown) {
        buttonShown_ = shown;
        SaveDataToSettings("kwiq_button", "KWIQ button on the radar screen", shown ? "1" : "0");
        Msg(shown ? "The KWIQ button is shown." : "The KWIQ button is hidden. Type .kwiq button to show it again.", false);
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
            kwiq_ui::ShowInfo("KWIQ API Key", "Your API key has been received and saved. The plugin is now connected.");
            return;
        }
        // The listener gives up after 15 minutes; the setup stays, so start a new one for Reopen Browser
        if (kwiq_ui::KeyPromptOpen() && !keyLink_.Running()) keyLink_.Start();
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
        if (now - startedAt_ <= STARTUP_SECONDS)
            kwiq_ui::ShowEsVersion("You are on an incorrect EuroScope version: " + esVersion_ + ". The Kuwait & Iraq vACC uses " +
                                   list + ".\n\nPress OK to open the EuroScope setup guide.");
        Msg("Incorrect EuroScope version " + esVersion_ + " (the vACC uses " + list + "). Setup guide: " + url, true);
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
        if (!apikey::Save(key)) { Msg("The API key could not be saved in %APPDATA%\\KWIQ.", true); return; }
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

public:
    void OnNotice(int kind, int button) {
        if (kind == kwiq_ui::NOTICE_KEY && button == 1) { OpenConnectPage(); kwiq_ui::ShowKeyPrompt(); }
        if (kind == kwiq_ui::NOTICE_ES_VERSION && button == 1) {
            std::string url;
            { std::lock_guard<std::mutex> g(mu_); url = esUrl_; }
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
private:
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
            latest_ = Trim(c.Get("plugin.latest_version"));
            latestSha_ = Lower(Trim(c.Get("plugin.loader_sha256")));
            // OKAC and ORBB are released separately, each with its own version
            upstreamAirac_ = { { "OKAC", c.Get("sectorfile.airac.okac") }, { "ORBB", c.Get("sectorfile.airac.orbb") } };
            sectorUrl_ = { { "OKAC", c.Get("sectorfile.url.okac") }, { "ORBB", c.Get("sectorfile.url.orbb") } };
            if (!motd.empty() && motd != motd_) pending_.emplace_back(motd, false);
            motd_ = motd;
            esAccepted_ = c.Get("euroscope.versions");
            esUrl_ = Trim(c.Get("euroscope.setup_url", ES_SETUP_URL));
            if (esUrl_.empty()) esUrl_ = ES_SETUP_URL;
            esConfigured_ = true;
        }
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

    // main thread: (re)reads the ESE's positions when another ESE is found or it has changed
    void ReadEse() {
        std::string path, name;
        { std::lock_guard<std::mutex> g(mu_); path = sectorPath_; name = sectorName_; }
        std::string ese = sf::FindEse(path, name);
        WIN32_FILE_ATTRIBUTE_DATA a = {};
        if (ese.empty() || !GetFileAttributesExA(ese.c_str(), GetFileExInfoStandard, &a)) return;
        if (ese == esePath_ && CompareFileTime(&a.ftLastWriteTime, &eseTime_) == 0) return;
        esePath_ = ese;
        eseTime_ = a.ftLastWriteTime;
        esePositions_ = kwiq::ParseEsePositions(sf::ReadHead(ese, 8u << 20));
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
                if (kwiq_net::HttpGet(CORE_EVENT_URL, body, err)) {
                    ClearError("Core");
                    EventData ev = ParseCoreEvent(body);
                    std::lock_guard<std::mutex> g(mu_);
                    ev_ = std::move(ev); evGen_++;
                } else fail("Core", err);
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
            if (doSrd) {                               // sector aliases: tiny, refreshed with the SRD
                if (kwiq_net::HttpGet(CORE_SECTORS_URL, body, err)) {
                    ClearError("Sectors");
                    SectorMap m = ParseSectors(body);
                    std::lock_guard<std::mutex> g(mu_);
                    sectors_ = std::move(m);
                } else fail("Sectors", err);
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
    int buttonX_ = -1, buttonY_ = -1;                               // main thread only: from the radar's top left; -1 = top centre
    std::string apiKey_;                                            // main thread only (the worker uses kwiq_net's copy)
    KeyLink keyLink_;
    std::string keyPromptedFor_, keyToldFor_;                      // main thread only: the key problem last offered / told about
    bool justConnected_ = false;                                    // main thread only: logged on since the last key check
    std::string esVersion_;                                         // the running EuroScope's, read once at start
    bool esChecked_ = false;                                        // main thread only
    std::time_t startedAt_ = 0, lastNameRead_ = 0, lastEseRead_ = 0;
    kwiq::EsePositions esePositions_;                               // main thread only: of the loaded sector file
    std::string esePath_;                                           // main thread only
    FILETIME eseTime_ = {};                                         // main thread only
    HANDLE instanceLock_ = nullptr;
    bool duplicate_ = false;
    std::atomic<bool> keyRefused_{ false };
    std::string keyReason_;                                          // guarded by mu_: why Core last refused the key
    bool connected_ = false, soundPlayed_ = false, soundLooping_ = false, bannerWas_ = false;   // main thread only
    std::time_t connectedAt_ = 0, previewUntil_ = 0;                // main thread only
    static constexpr std::time_t PREVIEW_SECONDS = 15;
    static constexpr std::time_t STARTUP_SECONDS = 180;             // popups are only shown this soon after starting
    std::atomic<bool> quit_{ false }, tooOld_{ false };
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
        if (small_) DeleteObject(small_);
        if (banner_) DeleteObject(banner_);
    }

    virtual void OnAsrContentToBeClosed(void) override { delete this; }

    virtual void OnRefresh(HDC hDC, int Phase) override {
        if (!gPlugin) return;
        if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_LISTS) {
            std::time_t now = std::time(nullptr);
            if (gPlugin->SctBannerShown(now)) DrawSctBanner(hDC, gPlugin->SctBannerColour(now));
            if (gPlugin->ButtonShown()) DrawButton(hDC);
        }
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
    }

    virtual void OnClickScreenObject(int ObjectType, const char*, POINT, RECT Area, int) override {
        if (ObjectType == KWIQ_OBJ_BUTTON && gPlugin) gPlugin->OpenMenu(Area);
    }
    virtual void OnMoveScreenObject(int ObjectType, const char*, POINT Pt, RECT, bool Released) override {
        if (ObjectType != KWIQ_OBJ_BUTTON || !gPlugin) return;
        gPlugin->MoveButton(GetRadarArea(), Pt, Released);
        RequestRefresh();
    }
    // the menu and the key box opened from the button report back here
    virtual void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override {
        if (gPlugin && FunctionId >= KWIQ_MENU_KEY && FunctionId <= KWIQ_MENU_HIDE) gPlugin->MenuAction(FunctionId, sItemString, Pt, Area);
    }

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

    void DrawButton(HDC hDC) {
        RECT r = gPlugin->ButtonRect(GetRadarArea());
        int saved = SaveDC(hDC);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(127, 29, 29));
        HBRUSH fill = CreateSolidBrush(RGB(185, 28, 28));
        SelectObject(hDC, pen);
        SelectObject(hDC, fill);
        RoundRect(hDC, r.left, r.top, r.right, r.bottom, 6, 6);
        SetBkMode(hDC, TRANSPARENT);
        SetTextColor(hDC, RGB(255, 255, 255));
        if (small_) SelectObject(hDC, small_);
        DrawTextA(hDC, "KWIQ", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RestoreDC(hDC, saved);
        DeleteObject(pen);
        DeleteObject(fill);
        AddScreenObject(KWIQ_OBJ_BUTTON, "KWIQ", r, true, "KWIQ options");
    }

    HFONT font_ = nullptr;
    HFONT banner_ = CreateFontA(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
    HFONT small_ = CreateFontA(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
};

EuroScopePlugIn::CRadarScreen* CKWIQPlugin::OnRadarScreenCreated(const char*, bool, bool, bool, bool) {
    return new CKWIQRadar();
}

static void SrdSearchThunk(const std::string& adep, const std::string& ades, const std::string& lvl) {
    if (gPlugin) gPlugin->RunSrdSearch(adep, ades, lvl);
}
static void NoticeThunk(int kind, int button) {
    if (gPlugin) gPlugin->OnNotice(kind, button);
}

// Entry points: declared (with dllexport) in EuroScopePlugIn.h, so no extern "C" here. The loader
// (KWIQPlugin.dll, loader.cpp) uses the KWIQCore* ones below instead, after any update.
void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    *ppPlugInInstance = gPlugin = new CKWIQPlugin();
}
void __declspec(dllexport) EuroScopePlugInExit(void) {
    CKWIQPlugin* p = gPlugin;
    gPlugin = nullptr;
    delete p;
}

extern "C" __declspec(dllexport) void KWIQCoreInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    EuroScopePlugInInit(ppPlugInInstance);
}
extern "C" __declspec(dllexport) void KWIQCoreExit() {
    EuroScopePlugInExit();
}
extern "C" __declspec(dllexport) const char* KWIQCoreVersion() {
    return KWIQ_PLUGIN_VERSION;
}
