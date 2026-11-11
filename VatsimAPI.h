// VatsimAPI.h - event slot detection (VATCAN bookings + VATSIM data feed) and a small curl HTTP helper.
//
// Adapted from the VATCAN Slots-Plugin, in the frazerxyz fork (background fetch worker,
// results applied on the main thread, streaming data-feed parser). Changes for KWIQ: the event
// code comes from Core, the NAT track and flight-plan amend parts were removed, and the data
// is kept in a self-contained store instead of the radar screen class. See NOTICE.md.
#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>          // must precede EuroScopePlugIn.h
#include "EuroScopePlugIn.h"

struct ACData {
    bool hasCTP = false;      // pilot has a slot for the active event
    std::string slotTime;
    std::string CID;
};

struct UserMessage {
    std::string sender;
    std::string text;
};

struct FetchResult {
    bool slotsUpdated = false;
    bool feedUpdated = false;
    std::map<int, std::string> slotTime;          // CID -> slot
    std::map<std::string, int> callsignToCid;     // callsign -> CID
    std::vector<UserMessage> messages;
};

class CDataHandler {
public:
    // Main thread only
    static bool SetEventCode(const std::string& eventCode);          // true if the code changed (data reset)
    static void StartFetch();                                    // non-blocking; no-op while a fetch runs
    static void ApplyPending(EuroScopePlugIn::CPlugIn* plugin);  // call from OnTimer
    static const ACData* Get(const std::string& callsign);
    static size_t SlotCount();
    static size_t PilotCount();
    static void Shutdown();                                      // waits for the worker; call before unload

    static int refreshInterval;                                  // seconds between fetches (default 60)

private:
    static void FetchWorker(std::string eventCode, bool pullSlots);
    static void GetVatsimAPIurlData();

    static std::string eventCode_;
    static std::atomic<bool> fetchInProgress;
    static bool firstSlotPull;
    static time_t lastSlotPull;
    static std::string vatsimJson3URL;
    static std::thread worker;
    static std::mutex pendingMutex;
    static std::optional<FetchResult> pending;

    static std::map<int, std::string> slotTime_;
    static std::map<std::string, int> callsignToCid_;
    static std::map<std::string, ACData> acData_;
};

namespace kwiq_net {
// Blocking GET (use from a worker thread). Returns true on HTTP 200.
bool HttpGet(const std::string& url, std::string& body, std::string& err);
// Blocking request with any method and an optional form body (use from a worker thread). Returns the HTTP
// status, 0 if there was none; err is set for anything but 2xx.
long HttpSend(const std::string& method, const std::string& url, const std::string& form, std::string& body,
              std::string& err);
// Blocking binary GET with size cap / timeout; `cancel` (optional) aborts the transfer when set.
bool HttpGetBinary(const std::string& url, std::string& body, std::string& err, size_t maxBytes,
                   long timeoutSec, const std::atomic<bool>* cancel);
void GlobalInit();
void GlobalCleanup();
// Requests to URLs starting with urlPrefix (Core) carry X-KWIQ-Key (when set) and X-KWIQ-Version.
void SetCoreAuth(const std::string& urlPrefix, const std::string& key, const std::string& version);
}
