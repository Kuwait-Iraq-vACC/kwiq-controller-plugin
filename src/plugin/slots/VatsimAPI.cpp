// VatsimAPI.cpp - see VatsimAPI.h and NOTICE.md for provenance.
#include "VatsimAPI.h"
#include "version.h"

#include <ctime>
#include <exception>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

using json = nlohmann::json;
using std::map;
using std::string;

int CDataHandler::refreshInterval = 60;
string CDataHandler::eventCode_;
std::atomic<bool> CDataHandler::fetchInProgress{ false };
bool CDataHandler::firstSlotPull = true;
time_t CDataHandler::lastSlotPull = 0;
string CDataHandler::vatsimJson3URL;
std::thread CDataHandler::worker;
std::mutex CDataHandler::pendingMutex;
std::optional<FetchResult> CDataHandler::pending;
map<int, string> CDataHandler::slotTime_;
map<string, int> CDataHandler::callsignToCid_;
map<string, ACData> CDataHandler::acData_;

static size_t write_data(void* buffer, size_t size, size_t nmemb, void* userp) {
    ((string*)userp)->append((char*)buffer, size * nmemb);
    return size * nmemb;
}

static void follow_redirects(CURL* h, const string& url);

static void setup_curl_modern(CURL* h, const char* url, string* response) {
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, response);
    follow_redirects(h, url);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "KWIQPlugin/" KWIQ_PLUGIN_VERSION);

    // Native Windows trust store, no certificate file needed
    curl_easy_setopt(h, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA | CURLSSLOPT_NO_REVOKE);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);

    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    // A limit on what one answer may be (#43): several times the VATSIM data feed, far above anything Core sends.
    // Larger, curl stops the transfer and it fails with "Maximum file size exceeded".
    curl_easy_setopt(h, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)32 * 1024 * 1024);
}

// The personal API key and plugin version, sent to Core only (never to VATSIM or VATCAN)
static std::mutex authMutex;
static string authPrefix, authKey, authVersion;

// curl sends headers set with CURLOPT_HTTPHEADER to every host a redirect leads to, so a Core request, which
// carries X-KWIQ-Key, never follows one: Core answers itself. Other requests follow redirects, but only to https.
static void follow_redirects(CURL* h, const string& url) {
    bool core;
    { std::lock_guard<std::mutex> g(authMutex); core = !authPrefix.empty() && url.compare(0, authPrefix.size(), authPrefix) == 0; }
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, core ? 0L : 1L);
    curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, "https");
}

static curl_slist* core_headers(const string& url) {
    std::lock_guard<std::mutex> g(authMutex);
    if (authPrefix.empty() || url.compare(0, authPrefix.size(), authPrefix) != 0) return nullptr;
    curl_slist* h = curl_slist_append(nullptr, ("X-KWIQ-Version: " + authVersion).c_str());
    if (!authKey.empty()) h = curl_slist_append(h, ("X-KWIQ-Key: " + authKey).c_str());
    return h;
}

static CURLcode fetch_url(const char* url, string& response, long* httpCode = nullptr) {
    CURL* curl = curl_easy_init();
    if (!curl) { return CURLE_FAILED_INIT; }
    setup_curl_modern(curl, url, &response);
    curl_slist* headers = core_headers(url);
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    CURLcode res = curl_easy_perform(curl);
    if (httpCode) { curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, httpCode); }
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return res;
}

namespace kwiq_net {

void GlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void GlobalCleanup() { curl_global_cleanup(); }

void SetCoreAuth(const string& urlPrefix, const string& key, const string& version) {
    std::lock_guard<std::mutex> g(authMutex);
    authPrefix = urlPrefix; authKey = key; authVersion = version;
}

bool HttpGet(const string& url, string& body, string& err) {
    body.clear();
    long code = 0;
    CURLcode res = fetch_url(url.c_str(), body, &code);
    if (res != CURLE_OK) { err = curl_easy_strerror(res); return false; }
    if (code != 200) { err = "HTTP " + std::to_string(code); return false; }
    return true;
}

long HttpSend(const string& method, const string& url, const string& form, string& body, string& err) {
    body.clear();
    err.clear();
    CURL* curl = curl_easy_init();
    if (!curl) { err = curl_easy_strerror(CURLE_FAILED_INIT); return 0; }
    setup_curl_modern(curl, url.c_str(), &body);
    if (method != "GET") curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!form.empty()) curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, form.c_str());
    curl_slist* headers = core_headers(url);
    headers = curl_slist_append(headers, "Accept: text/plain");
    if (!form.empty()) headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    else err = curl_easy_strerror(res);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if (code && (code < 200 || code > 299)) err = "HTTP " + std::to_string(code);
    return code;
}

} // namespace kwiq_net

// Errors only: a successful fetch says nothing
static void add_message(FetchResult& r, const char* sender, string text) {
    r.messages.push_back({ sender, text });
}

// Streaming parser for the VATSIM data feed: only reads callsign + cid of each pilot
struct DataFeedSax {
    map<std::string, int>& out;
    std::string timeStamp;
    std::string error;

    int depth = 0;
    int pilotsDepth = -1;
    bool inGeneral = false;
    std::string lastKey;
    std::string callsign;
    int cid = 0;

    DataFeedSax(map<std::string, int>& o) : out(o) {}

    bool inPilot() const { return pilotsDepth >= 0 && depth == pilotsDepth + 1; }

    bool start_object(std::size_t) {
        depth++;
        if (inPilot()) { callsign.clear(); cid = 0; }
        if (depth == 2 && lastKey == "general") { inGeneral = true; }
        return true;
    }
    bool end_object() {
        if (inPilot() && !callsign.empty() && cid != 0) { out[callsign] = cid; }
        if (depth == 2) { inGeneral = false; }
        depth--;
        return true;
    }
    bool start_array(std::size_t) {
        depth++;
        if (depth == 2 && lastKey == "pilots") { pilotsDepth = depth; }
        return true;
    }
    bool end_array() {
        if (depth == pilotsDepth) { pilotsDepth = -1; }
        depth--;
        return true;
    }
    bool key(std::string& val) { lastKey = val; return true; }
    bool string(std::string& val) {
        if (inPilot() && lastKey == "callsign") { callsign = val; }
        if (inGeneral && depth == 2 && lastKey == "update_timestamp") { timeStamp = val; }
        return true;
    }
    bool number_integer(json::number_integer_t val) {
        if (inPilot() && lastKey == "cid") { cid = (int)val; }
        return true;
    }
    bool number_unsigned(json::number_unsigned_t val) {
        if (inPilot() && lastKey == "cid") { cid = (int)val; }
        return true;
    }
    bool null() { return true; }
    bool boolean(bool) { return true; }
    bool number_float(json::number_float_t, const std::string&) { return true; }
    bool binary(json::binary_t&) { return true; }
    bool parse_error(std::size_t, const std::string&, const json::exception& ex) {
        error = ex.what();
        return false;
    }
};

void CDataHandler::GetVatsimAPIurlData() {
    string vatsimURLs;
    if (fetch_url("https://status.vatsim.net/", vatsimURLs) == CURLE_OK) {
        size_t pos = vatsimURLs.find("json3=");
        if (pos != string::npos) {
            vatsimURLs = vatsimURLs.substr(pos + 6);
            vatsimURLs = vatsimURLs.substr(0, vatsimURLs.find("\n"));
            vatsimURLs.erase(vatsimURLs.find_last_not_of(" \r\t") + 1);
            if (vatsimURLs.rfind("https://", 0) == 0) { CDataHandler::vatsimJson3URL = vatsimURLs; }
        }
    }
    if (CDataHandler::vatsimJson3URL.empty()) {
        CDataHandler::vatsimJson3URL = "https://data.vatsim.net/v3/vatsim-data.json";
    }
}

bool CDataHandler::SetEventCode(const string& code) {
    if (code == eventCode_) { return false; }
    eventCode_ = code;
    firstSlotPull = true;
    lastSlotPull = 0;
    slotTime_.clear();
    callsignToCid_.clear();
    acData_.clear();
    return true;
}

void CDataHandler::StartFetch() {
    if (eventCode_.empty()) { return; }
    bool expected = false;
    if (!fetchInProgress.compare_exchange_strong(expected, true)) { return; }

    time_t now = time(nullptr);
    bool pullSlots = firstSlotPull || (now - lastSlotPull) > 300;   // slots every 5 minutes, feed every cycle
    if (pullSlots) { lastSlotPull = now; firstSlotPull = false; }

    if (worker.joinable()) { worker.join(); }                        // previous worker has finished
    worker = std::thread(FetchWorker, eventCode_, pullSlots);
}

void CDataHandler::Shutdown() {
    if (worker.joinable()) { worker.join(); }
}

// A function-try-block: nothing may leave a thread's function, and the next fetch must still be able to start (#45)
void CDataHandler::FetchWorker(string eventCode, bool pullSlots) try {
    FetchResult result;

    if (CDataHandler::vatsimJson3URL.empty()) {
        GetVatsimAPIurlData();
    }

    // --- PART 1: Fetch VATCAN slots ---
    if (pullSlots) {
        string url = "https://bookings.vatcan.ca/api/event/" + eventCode;
        string cidString;
        CURLcode res = fetch_url(url.c_str(), cidString);

        if (res == CURLE_OK) {
            try {
                json cidJson = json::parse(cidString);
                if (cidJson.contains("error")) {
                    string error = cidJson.at("error");
                    add_message(result, "API Error", error);
                }
                else {
                    for (auto& pilots : cidJson) {
                        int cid = pilots["cid"];
                        string slot = pilots["slot"];
                        result.slotTime[cid] = slot;
                    }
                    result.slotsUpdated = true;
                }
            }
            catch (std::exception& e) {
                add_message(result, "Parse Error", string("Slots failed: ") + e.what());
            }
        }
        else {
            add_message(result, "Network Error", string("Connect failed: ") + curl_easy_strerror(res));
        }
    }

    // --- PART 2: Fetch VATSIM data file ---
    if (!CDataHandler::vatsimJson3URL.empty()) {
        string responseString;
        if (fetch_url(CDataHandler::vatsimJson3URL.c_str(), responseString) == CURLE_OK) {
            try {
                DataFeedSax sax(result.callsignToCid);
                if (!json::sax_parse(responseString, &sax)) {
                    throw std::runtime_error(sax.error);
                }
                result.feedUpdated = true;
            }
            catch (std::exception& e) {
                add_message(result, "Parse Error", string("VATSIM Data failed: ") + e.what());
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pending = std::move(result);
    }
    fetchInProgress = false;
} catch (...) {
    fetchInProgress = false;
}

// Runs on the main thread
void CDataHandler::ApplyPending(EuroScopePlugIn::CPlugIn* plugin) {
    std::optional<FetchResult> result;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        result.swap(pending);
    }
    if (!result) { return; }
    if (eventCode_.empty()) { return; }          // event ended while the fetch was running

    bool changed = false;
    if (result->slotsUpdated) { slotTime_ = std::move(result->slotTime); changed = true; }
    if (result->feedUpdated) { callsignToCid_ = std::move(result->callsignToCid); changed = true; }

    if (changed) {
        acData_.clear();
        for (auto& [callsign, cid] : callsignToCid_) {
            ACData& d = acData_[callsign];
            d.CID = std::to_string(cid);
            auto slot = slotTime_.find(cid);
            if (slot != slotTime_.end()) {
                d.slotTime = slot->second;
                d.hasCTP = true;
            }
        }
    }

    for (auto& m : result->messages) {
        plugin->DisplayUserMessage("KWIQ Events Manager", m.sender.c_str(), m.text.c_str(), true, true, true, true, true);
    }
}

const ACData* CDataHandler::Get(const string& callsign) {
    auto it = acData_.find(callsign);
    return it == acData_.end() ? nullptr : &it->second;
}
size_t CDataHandler::SlotCount() { return slotTime_.size(); }
size_t CDataHandler::PilotCount() { return callsignToCid_.size(); }
