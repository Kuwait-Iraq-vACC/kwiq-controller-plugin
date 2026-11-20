// push.cpp - Core's push events over Laravel Reverb (the Pusher protocol, version 7), as UKCP gets its own.
//   connect    wss://<host>:<port>/app/<key>?protocol=7&client=kwiq-plugin&version=<version>
//   server     {"event":"pusher:connection_established","data":"{\"socket_id\":\"...\",\"activity_timeout\":30}"}
//   subscribe  {"event":"pusher:subscribe","data":{"channel":"metars"}}
//   events     {"event":"metars.updated","channel":"metars","data":"..."}
//   keepalive  pusher:ping / pusher:pong both ways
// Only public channels: nothing personal goes over the socket, only "something changed".

#include "push.hpp"
#include <mutex>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include "version.h"

struct KwiqPush::Impl {
    ix::WebSocket ws;
    std::mutex mu;
    std::string url;
    std::vector<std::string> channels;
    std::vector<std::pair<std::string, std::string>> events;
    bool connected = false, running = false;

    void Subscribe() {
        std::vector<std::string> ch;
        { std::lock_guard<std::mutex> g(mu); ch = channels; }
        for (const auto& c : ch) {
            nlohmann::json j = { { "event", "pusher:subscribe" }, { "data", { { "channel", c } } } };
            ws.send(j.dump());
        }
    }
    void OnMessage(const ix::WebSocketMessagePtr& m) {
        if (m->type == ix::WebSocketMessageType::Close || m->type == ix::WebSocketMessageType::Error) {
            std::lock_guard<std::mutex> g(mu); connected = false; return;
        }
        if (m->type != ix::WebSocketMessageType::Message) return;
        try {
            auto j = nlohmann::json::parse(m->str);
            std::string ev = j.value("event", "");
            if (ev == "pusher:connection_established") {
                { std::lock_guard<std::mutex> g(mu); connected = true; }
                Subscribe();
            } else if (ev == "pusher:ping") {
                ws.send(nlohmann::json({ { "event", "pusher:pong" }, { "data", nlohmann::json::object() } }).dump());
            } else if (ev.rfind("pusher", 0) != 0) {        // pusher: and pusher_internal: are the protocol's own
                std::lock_guard<std::mutex> g(mu);
                if (events.size() < 100) events.emplace_back(j.value("channel", ""), ev);
            }
        } catch (...) {}
    }
};

KwiqPush::KwiqPush() : d_(new Impl) {
    ix::initNetSystem();
    Impl* d = d_.get();
    d_->ws.setOnMessageCallback([d](const ix::WebSocketMessagePtr& m) { d->OnMessage(m); });
    d_->ws.setPingInterval(30);
    d_->ws.setMaxWaitBetweenReconnectionRetries(60 * 1000);
    d_->ws.enableAutomaticReconnection();
}

KwiqPush::~KwiqPush() {
    d_->ws.stop();
    ix::uninitNetSystem();
}

void KwiqPush::Configure(const std::string& host, int port, bool tls, const std::string& key, const std::vector<std::string>& channels) {
    std::string url = host.empty() || key.empty() ? std::string()
        : std::string(tls ? "wss://" : "ws://") + host + ":" + std::to_string(port > 0 ? port : (tls ? 443 : 80)) + "/app/" + key +
          "?protocol=7&client=kwiq-plugin&version=" KWIQ_PLUGIN_VERSION;
    {
        std::lock_guard<std::mutex> g(d_->mu);
        bool same = url == d_->url && channels == d_->channels;
        d_->channels = channels;
        if (same) return;
        d_->url = url;
        d_->connected = false;
    }
    d_->ws.stop();
    d_->running = false;
    if (url.empty()) return;
    ix::SocketTLSOptions t;
    t.caFile = "SYSTEM";                             // Windows' trusted roots
    d_->ws.setTLSOptions(t);
    d_->ws.setUrl(url);
    d_->ws.start();
    d_->running = true;
}

bool KwiqPush::Connected() const { std::lock_guard<std::mutex> g(d_->mu); return d_->connected; }

std::vector<std::pair<std::string, std::string>> KwiqPush::Take() {
    std::lock_guard<std::mutex> g(d_->mu);
    std::vector<std::pair<std::string, std::string>> out;
    out.swap(d_->events);
    return out;
}
