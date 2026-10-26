// keylink.hpp - receives the personal API key from myKWIQ.
//
// The plugin listens on 127.0.0.1 (a free port) and opens myKWIQ's Connect EuroScope page with that
// port and a one-off code. After the controller clicks Connect there, the browser is sent to
// http://127.0.0.1:<port>/?key=...&state=<code>; the key is accepted only with the right code.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class KeyLink {
public:
    ~KeyLink() { Stop(); }
    // Starts listening (if not already); false if no port could be opened
    bool Start();
    void Stop();
    bool Running() const { return running_; }
    int Port() const { return port_; }
    const std::string& State() const { return state_; }
    // The key, once received (then the listener has stopped); empty until then
    std::string Take();

private:
    void Loop();
    std::thread thread_;
    std::atomic<bool> running_{ false }, stop_{ false };
    std::uintptr_t sock_ = ~(std::uintptr_t)0;
    int port_ = 0;
    std::string state_;
    std::mutex mu_;
    std::string received_;
};
