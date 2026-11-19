// kwiq_ui.cpp - the plugin's windows (plain Win32, no MFC): the event routes list (Copy button per row), the SRD
// Search dialog (kwiq.rc), the API key setup box and the procedure notifications window. See kwiq_ui.hpp.
// Moved out of KWIQPlugin.cpp unchanged (#51). License: GPL-3.0-or-later, see LICENSE and NOTICE.md.

#include <windows.h>
#include <shellapi.h>       // ShellExecute
#include <commctrl.h>       // the SRD search and event routes lists (link comctl32)
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "kwiq_res.h"       // kwiq.rc: the SRD Search and Event Routes dialogs
#include "kwiq_logic.hpp"
#include "kwiq_ui.hpp"

namespace kwiq_ui {

static const int M = 10, GAP = 6, ROWH = 24;                    // the Notifications window's margin, gap and row height

static HMODULE gMod = nullptr;
// SRD Search: the routes for an origin, a destination and a level (the plugin's data from Core)
std::vector<kwiq::SrdRoute> (*gOnSearch)(const std::string&, const std::string&, const std::string&) = nullptr;

void CopyToClipboard(HWND owner, const std::string& text) {
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

// SRD Search: the UK Controller Plugin's dialog (IDD_SRD_SEARCH in kwiq.rc, SrdSearchDialog.cpp), opened
// the way UKCP opens it: DialogBoxParam, modal, owned by EuroScope's active window (#56). Origin and
// destination are required; the results (Min, Max, FRA, Route String), the chosen route's notes, and Copy
// Route String To Clipboard. Enter searches (Search is the default button), Esc or Close closes it.
struct SrdDialog {
    std::string adep, ades, lvl;                 // filled in from the flight plan, or empty
    std::vector<kwiq::SrdRoute> results;
};
static std::string DlgText(HWND d, int id) { char b[256] = {}; GetDlgItemTextA(d, id, b, sizeof b); return kwiq::Trim(b); }
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
// Every result; "No routes found" when there are none (as UKCP)
static void SrdShowResults(HWND d, SrdDialog& s) {
    HWND list = GetDlgItem(d, IDC_SRD_RESULTS);
    SendMessageA(list, LVM_DELETEALLITEMS, 0, 0);
    SetDlgItemTextA(d, IDC_SRD_NOTES, "");
    SetDlgItemTextA(d, IDC_SRD_ROUTE_COPY, "Copy Route String To Clipboard");
    EnableWindow(GetDlgItem(d, IDC_SRD_ROUTE_COPY), FALSE);
    if (s.results.empty()) {
        SetCell(list, 0, 0, "0");
        SetCell(list, 0, 1, "0");
        SetCell(list, 0, 2, "0");
        SetCell(list, 0, 3, "No routes found");
        return;
    }
    for (int i = 0; i < (int)s.results.size(); i++) {
        SetCell(list, i, 0, kwiq::SrdMinText(s.results[i]));
        SetCell(list, i, 1, kwiq::SrdMaxText(s.results[i]));
        SetCell(list, i, 2, kwiq::HasFreeRoute(s.results[i].rte) ? "Yes" : "No");
        SetCell(list, i, 3, s.results[i].rte);
    }
}
static void SrdRunSearch(HWND d, SrdDialog& s) {
    std::string adep = DlgText(d, IDC_SRD_ORIGIN), ades = DlgText(d, IDC_SRD_DESTINATION);
    if (adep.empty() || ades.empty()) {                      // both are required, as in UKCP
        s.results.clear();
        SrdShowResults(d, s);
        SetDlgItemTextA(d, IDC_SRD_NOTES, "Enter both the origin and the destination (a fix or an airfield).");
        return;
    }
    s.results = gOnSearch ? gOnSearch(adep, ades, DlgText(d, IDC_SRD_CRUISE)) : std::vector<kwiq::SrdRoute>();
    SrdShowResults(d, s);
}
// Called by Windows inside DialogBoxParam, so nothing may escape from it
static INT_PTR CALLBACK SrdDialogProc(HWND d, UINT m, WPARAM w, LPARAM l) {
    try {
        SrdDialog* s = (SrdDialog*)GetWindowLongPtrW(d, DWLP_USER);
        switch (m) {
        case WM_INITDIALOG: {
            s = (SrdDialog*)l;
            SetWindowLongPtrW(d, DWLP_USER, (LONG_PTR)s);
            HWND list = GetDlgItem(d, IDC_SRD_RESULTS);
            SendMessageA(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
            RECT cols = { 0, 0, 25, 0 };                         // UKCP's columns, in dialog units
            MapDialogRect(d, &cols);
            AddColumn(list, 0, "Min", cols.right);
            AddColumn(list, 1, "Max", cols.right);
            AddColumn(list, 2, "FRA", cols.right);
            AddColumn(list, 3, "Route String", 900);
            SetDlgItemTextA(d, IDC_SRD_ORIGIN, s->adep.c_str());
            SetDlgItemTextA(d, IDC_SRD_DESTINATION, s->ades.c_str());
            SetDlgItemTextA(d, IDC_SRD_CRUISE, s->lvl.c_str());
            if (!s->adep.empty() && !s->ades.empty()) SrdRunSearch(d, *s);   // opened from a flight plan: search at once
            SetFocus(GetDlgItem(d, s->adep.empty() ? IDC_SRD_ORIGIN : s->ades.empty() ? IDC_SRD_DESTINATION : IDC_SRD_CRUISE));
            return FALSE;                                        // the focus is set
        }
        case WM_COMMAND:
            if (!s) return FALSE;
            switch (LOWORD(w)) {
            case IDC_SRD_SEARCH: SrdRunSearch(d, *s); return TRUE;
            case IDC_SRD_ROUTE_COPY: {
                int i = (int)SendDlgItemMessageA(d, IDC_SRD_RESULTS, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
                if (i >= 0 && i < (int)s->results.size()) {
                    CopyToClipboard(d, s->results[i].rte);
                    SetDlgItemTextA(d, IDC_SRD_ROUTE_COPY, "Copied");
                }
                return TRUE;
            }
            case IDOK: case IDCANCEL: EndDialog(d, 0); return TRUE;   // Close, Esc and the title bar's X
            }
            return FALSE;
        case WM_NOTIFY: {
            // a route picked in the results: its notes, and it can be copied
            NMHDR* nh = (NMHDR*)l;
            if (!s || nh->idFrom != IDC_SRD_RESULTS || nh->code != LVN_ITEMCHANGED) return FALSE;
            NMLISTVIEW* nv = (NMLISTVIEW*)l;
            if (!(nv->uNewState & LVIS_SELECTED) || (nv->uOldState & LVIS_SELECTED)) return FALSE;
            if (nv->iItem < 0 || nv->iItem >= (int)s->results.size()) return FALSE;
            SetDlgItemTextA(d, IDC_SRD_NOTES, kwiq::SrdNotesText(s->results[nv->iItem]).c_str());
            SetDlgItemTextA(d, IDC_SRD_ROUTE_COPY, "Copy Route String To Clipboard");
            EnableWindow(GetDlgItem(d, IDC_SRD_ROUTE_COPY), TRUE);
            return TRUE;
        }
        }
    } catch (...) {}
    return FALSE;
}
// Before either dialog opens: the list view class, and this DLL (the dialogs are in its resources)
static void PrepareDialog() {
    static bool commonControls = false;
    if (!commonControls) {
        INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_LISTVIEW_CLASSES };
        commonControls = InitCommonControlsEx(&ic) != FALSE;
    }
    if (!gMod)
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&SrdDialogProc, &gMod);
}
void ShowSrd(const std::string& adep, const std::string& ades, const std::string& lvl) {
    PrepareDialog();
    SrdDialog s{ adep, ades, lvl, {} };
    DialogBoxParamW(gMod, MAKEINTRESOURCEW(IDD_SRD_SEARCH), GetActiveWindow(), SrdDialogProc, (LPARAM)&s);
}

// Event Routes (#61): the same kind of dialog as SRD Search (IDD_EVENT_ROUTES in kwiq.rc), opened the same way.
// Today's routes for Core's event (Airports, Route String); the one selected is copied with Copy Route String
// To Clipboard, which then says "Copied". With none, the list says so. Esc or Close closes it.
struct EventRoutesDialog {
    std::string title;                           // "KWIQ Event Routes - <event> - <date>Z"
    std::vector<kwiq::EventRoute> rows;
};
static INT_PTR CALLBACK EventRoutesDialogProc(HWND d, UINT m, WPARAM w, LPARAM l) {
    try {
        EventRoutesDialog* s = (EventRoutesDialog*)GetWindowLongPtrW(d, DWLP_USER);
        switch (m) {
        case WM_INITDIALOG: {
            s = (EventRoutesDialog*)l;
            SetWindowLongPtrW(d, DWLP_USER, (LONG_PTR)s);
            SetWindowTextA(d, s->title.c_str());
            HWND list = GetDlgItem(d, IDC_EVT_ROUTES);
            SendMessageA(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
            RECT cols = { 0, 0, 70, 0 };                         // dialog units, as SRD Search's columns
            MapDialogRect(d, &cols);
            AddColumn(list, 0, "Airports", cols.right);
            AddColumn(list, 1, "Route String", 900);
            if (s->rows.empty()) {
                SetCell(list, 0, 0, "");
                SetCell(list, 0, 1, "No event routes for today (UTC).");
            }
            for (int i = 0; i < (int)s->rows.size(); i++) {
                SetCell(list, i, 0, kwiq::EventRouteAirports(s->rows[i]));
                SetCell(list, i, 1, s->rows[i].rte);
            }
            SetFocus(list);
            return FALSE;                                        // the focus is set
        }
        case WM_COMMAND:
            if (!s) return FALSE;
            switch (LOWORD(w)) {
            case IDC_EVT_ROUTE_COPY: {
                int i = (int)SendDlgItemMessageA(d, IDC_EVT_ROUTES, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
                if (i >= 0 && i < (int)s->rows.size()) {
                    CopyToClipboard(d, s->rows[i].rte);
                    SetDlgItemTextA(d, IDC_EVT_ROUTE_COPY, "Copied");
                }
                return TRUE;
            }
            case IDOK: case IDCANCEL: EndDialog(d, 0); return TRUE;   // Close, Enter, Esc and the title bar's X
            }
            return FALSE;
        case WM_NOTIFY: {
            // a route selected: it can be copied
            NMHDR* nh = (NMHDR*)l;
            if (!s || nh->idFrom != IDC_EVT_ROUTES || nh->code != LVN_ITEMCHANGED) return FALSE;
            NMLISTVIEW* nv = (NMLISTVIEW*)l;
            if (!(nv->uNewState & LVIS_SELECTED) || (nv->uOldState & LVIS_SELECTED)) return FALSE;
            bool route = nv->iItem >= 0 && nv->iItem < (int)s->rows.size();
            SetDlgItemTextA(d, IDC_EVT_ROUTE_COPY, "Copy Route String To Clipboard");
            EnableWindow(GetDlgItem(d, IDC_EVT_ROUTE_COPY), route ? TRUE : FALSE);
            return TRUE;
        }
        }
    } catch (...) {}
    return FALSE;
}
void ShowEvent(const std::string& title, const std::vector<kwiq::EventRoute>& rows) {
    PrepareDialog();
    EventRoutesDialog s{ title, rows };
    DialogBoxParamW(gMod, MAKEINTRESOURCEW(IDD_EVENT_ROUTES), GetActiveWindow(), EventRoutesDialogProc, (LPARAM)&s);
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

void ShowKeyPrompt() { gKeyBox.Show(); }
bool KeyPromptOpen() { return gKeyBox.Open(); }
bool KeyPromptReopen() { return gKeyBox.TakeReopen(); }
void CloseKeyPrompt() { gKeyBox.Close(); }

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
void (*gOnMarkRead)(const std::string& id, bool read) = nullptr;

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
void ShowNotices(const std::vector<kwiq::ProcedureNotice>& items) {
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
void UpdateNotices(const std::vector<kwiq::ProcedureNotice>& items) {
    if (!gNotices.hwnd) return;
    gNotices.items = items;
    NoticesFill();
}

void Shutdown() {
    if (gNotices.hwnd) DestroyWindow(gNotices.hwnd);
    if (gNoticesReg && gMod) { UnregisterClassA(kNoticesClass, gMod); gNoticesReg = false; }
    gKeyBox.Close();                                     // its thread must end before the plugin does
}

} // namespace kwiq_ui
