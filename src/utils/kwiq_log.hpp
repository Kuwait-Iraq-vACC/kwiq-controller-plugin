// kwiq_log.hpp - the plugin's own log: one line per event, in files next to KWIQPlugin.dll, so a controller can
// send one in and it can be filtered by aircraft or area.
//   HH:MM:SS.mmm AREA key=value key=value ... | text        (UTC)
// One file per UTC day (KWIQPlugin-YYYYMMDD.log), at most 20 MB each, kept for 7 days. Never the API key, a claim
// code, a query string or a response body. Standard library only, unit-tested in test/test_logic.cpp.
// License: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <ctime>
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <functional>
#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

namespace kwiq::log {

using Fields = std::vector<std::pair<std::string, std::string>>;

// \r and \n written as two characters, so one event is always one line
inline std::string Escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\r') out += "\\r";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}
// A field's value: quoted when it is empty or holds a space, ", = or |, so fields can't be split or forged
inline std::string Value(const std::string& v) {
    bool quote = v.empty();
    for (char c : v) if (c == ' ' || c == '"' || c == '=' || c == '|' || c == '\t') { quote = true; break; }
    if (!quote) return Escape(v);
    std::string q;                                    // " and \ escaped first, then \r and \n
    for (char c : v) {
        if (c == '"' || c == '\\') q += '\\';
        q += c;
    }
    return "\"" + Escape(q) + "\"";
}
inline std::string FormatLine(const std::tm& utc, int ms, const std::string& area, const Fields& f, const std::string& text) {
    char t[64];
    std::snprintf(t, sizeof t, "%02d:%02d:%02d.%03d", utc.tm_hour, utc.tm_min, utc.tm_sec, ms < 0 ? 0 : ms % 1000);
    std::string line = std::string(t) + " " + area;
    for (const auto& kv : f) line += " " + kv.first + "=" + Value(kv.second);
    if (!text.empty()) line += " | " + Escape(text);
    return line;
}

// ---- files ----------------------------------------------------------------------------------------
inline std::string FileName(const std::tm& utc) {
    char b[64];
    std::snprintf(b, sizeof b, "KWIQPlugin-%04d%02d%02d.log", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    return b;
}
inline bool IsLogFile(const std::string& name) {
    static const std::string pre = "KWIQPlugin-", post = ".log";
    if (name.size() != pre.size() + 8 + post.size()) return false;
    if (name.compare(0, pre.size(), pre) != 0 || name.compare(name.size() - post.size(), post.size(), post) != 0) return false;
    for (size_t i = pre.size(); i < pre.size() + 8; i++) if (name[i] < '0' || name[i] > '9') return false;
    return true;
}
// Days since 1970-01-01 of a civil date (Howard Hinnant's days_from_civil): no time zone involved
inline long DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
// A log file dated more than keepDays before today
inline bool Expired(const std::string& name, const std::tm& todayUtc, int keepDays) {
    if (!IsLogFile(name)) return false;
    const char* d = name.c_str() + 11;
    auto num = [&](int from, int len) { int v = 0; for (int i = 0; i < len; i++) v = v * 10 + (d[from + i] - '0'); return v; };
    long file = DaysFromCivil(num(0, 4), num(4, 2), num(6, 2));
    long today = DaysFromCivil(todayUtc.tm_year + 1900, todayUtc.tm_mon + 1, todayUtc.tm_mday);
    return today - file > keepDays;
}

// A URL fit for the log: no query or fragment, and no myKWIQ claim code
inline std::string SafeUrl(const std::string& url) {
    std::string u = url.substr(0, url.find_first_of("?#"));
    static const std::string claim = "/claim/";
    size_t i = u.find(claim);
    if (i != std::string::npos) {
        size_t from = i + claim.size(), to = u.find('/', from);
        u = u.substr(0, from) + "***" + (to == std::string::npos ? "" : u.substr(to));
    }
    return u;
}

// Results asked for on every tag refresh (FPC, sector indicator) are logged only when they change
class Changes {
public:
    bool Changed(const std::string& key, const std::string& value) {
        auto it = last_.find(key);
        if (it != last_.end() && it->second == value) return false;
        last_[key] = value;
        return true;
    }
    void Forget(const std::string& key) { last_.erase(key); }
private:
    std::unordered_map<std::string, std::string> last_;
};

// ---- the writer -------------------------------------------------------------------------------------
// Write only queues (the time is taken then); one thread appends to the day's file and flushes after each batch.
// The file is opened for each batch, so it can be read while EuroScope runs and a new day just picks a new name.
inline std::tm UtcOf(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    return tm;
}
class Writer {
public:
    using Clock = std::function<std::chrono::system_clock::time_point()>;
    explicit Writer(Clock c = {}) : clock_(c ? std::move(c) : Clock([] { return std::chrono::system_clock::now(); })) {}
    ~Writer() { Stop(); }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    // The first folder that can be created and written to; old log files there are deleted. False: no log at all.
    bool Start(const std::vector<std::string>& dirs, int keepDays = 7, std::uintmax_t capBytes = std::uintmax_t(20) << 20,
               size_t maxQueue = 10000) {
        namespace fs = std::filesystem;
        Stop();
        for (const auto& d : dirs) {
            if (d.empty()) continue;
            std::error_code ec;
            fs::create_directories(fs::path(d), ec);
            fs::path probe = fs::path(d) / ".kwiq-log-probe";
            { std::ofstream f(probe, std::ios::binary | std::ios::app); if (!f) continue; }
            fs::remove(probe, ec);
            std::lock_guard<std::mutex> g(mu_);
            dir_ = d;
            break;
        }
        std::lock_guard<std::mutex> g(mu_);
        if (dir_.empty()) return false;
        cap_ = capBytes;
        maxQueue_ = maxQueue;
        quit_ = false;
        Prune(keepDays);
        thread_ = std::thread([this] { Run(); });
        return true;
    }
    void Write(const std::string& area, const Fields& f, const std::string& text = "") {
        auto tp = clock_();
        std::time_t secs = std::chrono::system_clock::to_time_t(tp);
        int ms = (int)(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() % 1000);
        std::tm utc = UtcOf(secs);
        std::string name = FileName(utc), line = FormatLine(utc, ms, area, f, text);
        {
            std::lock_guard<std::mutex> g(mu_);
            if (!thread_.joinable() || quit_) return;
            if (queue_.size() >= maxQueue_) { dropped_++; pendingDrop_++; return; }
            queue_.push_back({ std::move(name), std::move(line), utc, ms });
        }
        cv_.notify_one();
    }
    // What's queued is written, then the thread ends. Safe to call more than once.
    void Stop() {
        {
            std::lock_guard<std::mutex> g(mu_);
            quit_ = true;
            held_ = false;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    std::string Dir() const { std::lock_guard<std::mutex> g(mu_); return dir_; }
    size_t Dropped() const { std::lock_guard<std::mutex> g(mu_); return dropped_; }
    // Tests only: the thread writes nothing while held
    void Hold(bool on) { { std::lock_guard<std::mutex> g(mu_); held_ = on; } cv_.notify_all(); }

private:
    struct Item { std::string file, line; std::tm utc; int ms; };
    void Prune(int keepDays) {                                       // mu_ held
        namespace fs = std::filesystem;
        std::tm today = UtcOf(std::chrono::system_clock::to_time_t(clock_()));
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(dir_), ec), end; !ec && it != end; it.increment(ec)) {
            std::string n = it->path().filename().string();
            if (Expired(n, today, keepDays)) { std::error_code rm; fs::remove(it->path(), rm); }
        }
    }
    void Run() {
        namespace fs = std::filesystem;
        for (;;) {
            std::deque<Item> batch;
            size_t dropped = 0;
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return quit_ || (!held_ && (!queue_.empty() || pendingDrop_)); });
                if (held_ && !quit_) continue;
                batch.swap(queue_);
                dropped = pendingDrop_;
                pendingDrop_ = 0;
                if (batch.empty() && !dropped && quit_) return;
            }
            try {
                std::string open;
                std::ofstream f;
                auto to = [&](const std::string& name) -> std::ofstream* {
                    if (name != open) {
                        if (f.is_open()) f.close();
                        f.clear();
                        f.open(fs::path(dir_) / name, std::ios::binary | std::ios::app);
                        open = name;
                    }
                    return f ? &f : nullptr;
                };
                auto put = [&](const std::string& name, const std::string& line, const std::tm& utc, int ms) {
                    if (capped_ == name) return;
                    std::error_code ec;
                    std::uintmax_t size = 0;
                    if (open != name || !f.is_open()) { auto s = fs::file_size(fs::path(dir_) / name, ec); size = ec ? 0 : s; }
                    else { f.flush(); auto s = fs::file_size(fs::path(dir_) / name, ec); size = ec ? 0 : s; }
                    std::ofstream* o = to(name);
                    if (!o) return;
                    if (size + line.size() + 1 > cap_) {
                        *o << FormatLine(utc, ms, "LOG", { { "cap", "20MB" } }, "log full for today") << '\n';
                        capped_ = name;
                        return;
                    }
                    *o << line << '\n';
                };
                if (dropped && !batch.empty())
                    put(batch.front().file, FormatLine(batch.front().utc, batch.front().ms, "LOG", { { "dropped", std::to_string(dropped) } }, ""),
                        batch.front().utc, batch.front().ms);
                for (const auto& it : batch) put(it.file, it.line, it.utc, it.ms);
                if (f.is_open()) f.flush();
            } catch (...) {}                                         // a log that can't be written never stops the plugin
        }
    }

    Clock clock_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    std::thread thread_;
    std::string dir_, capped_;
    std::uintmax_t cap_ = std::uintmax_t(20) << 20;
    size_t maxQueue_ = 10000, dropped_ = 0, pendingDrop_ = 0;
    bool quit_ = false, held_ = false;
};

// ---- the plugin's one log ---------------------------------------------------------------------------
inline Writer& Global() { static Writer w; return w; }
inline std::atomic<bool>& VerboseFlag() { static std::atomic<bool> v{ false }; return v; }
inline bool Verbose() { return VerboseFlag().load(); }
inline void SetVerbose(bool on) { VerboseFlag().store(on); }
inline void Write(const std::string& area, const Fields& f, const std::string& text = "") noexcept {
    try { Global().Write(area, f, text); } catch (...) {}
}

} // namespace kwiq::log
