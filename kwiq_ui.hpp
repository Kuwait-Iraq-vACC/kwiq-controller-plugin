// kwiq_ui.hpp - the plugin's windows (plain Win32, no MFC), in kwiq_ui.cpp:
//   * SRD Search and Event Routes, standard dialogs from kwiq.rc (IDD_SRD_SEARCH, IDD_EVENT_ROUTES)
//   * the API key setup message box, on a thread of its own while the controller signs in to myKWIQ
//   * the procedure notifications window (KWIQ menu > Notifications)
// They only reach the plugin through the data passed in and the two callbacks below (#51).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "kwiq_logic.hpp"

namespace kwiq_ui {

enum { SRD_MAX_RESULTS = 500 };                  // the most routes SRD Search lists

// SRD Search: the routes for an origin, a destination and a level (the plugin's data from Core)
extern std::vector<kwiq::SrdRoute> (*gOnSearch)(const std::string& adep, const std::string& ades, const std::string& lvl);
// Notifications: Mark read / Mark unread on one of them, which the plugin tells Core
extern void (*gOnMarkRead)(const std::string& id, bool read);

void CopyToClipboard(HWND owner, const std::string& text);

// SRD Search, modal (as UKCP's): returns once it is closed. Searches at once if origin and destination are given.
void ShowSrd(const std::string& adep, const std::string& ades, const std::string& lvl);
// Event Routes, modal: today's routes for Core's event
void ShowEvent(const std::string& title, const std::vector<kwiq::EventRoute>& rows);

// The API key setup box (#18): shown until CloseKeyPrompt, and again after each OK
void ShowKeyPrompt();
bool KeyPromptOpen();
bool KeyPromptReopen();                          // true once after each OK: open myKWIQ's page again
void CloseKeyPrompt();

// The notifications window: opens it (or brings it to the front), and refreshes it while it is open
void ShowNotices(const std::vector<kwiq::ProcedureNotice>& items);
void UpdateNotices(const std::vector<kwiq::ProcedureNotice>& items);

// Closes every window and waits for the key setup box's thread; called before the plugin is unloaded
void Shutdown();

} // namespace kwiq_ui
