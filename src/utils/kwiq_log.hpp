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

} // namespace kwiq::log
