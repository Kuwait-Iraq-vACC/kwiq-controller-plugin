// push.hpp - Core's push events, as UKCP gets its own: a WebSocket to Laravel Reverb (the Pusher protocol).
//
// Core says when something changed (new METARs, new procedure notifications) instead of every plugin asking
// over and over. The plugin then fetches the data as usual. If the socket is down, the plugin polls instead.
// Core /config: push.host, push.port (443), push.key (Reverb's app key), push.tls (1).
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

class KwiqPush {
public:
    KwiqPush();
    ~KwiqPush();
    // Any thread. Connects (or reconnects, if anything changed) and subscribes to the public channels.
    // An empty host or key disconnects.
    void Configure(const std::string& host, int port, bool tls, const std::string& key, const std::vector<std::string>& channels);
    bool Connected() const;
    // Main thread: the events received since the last call, as (channel, event)
    std::vector<std::pair<std::string, std::string>> Take();

    struct Impl;
private:
    std::unique_ptr<Impl> d_;
};
