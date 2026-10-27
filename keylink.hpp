// keylink.hpp - receives the personal API key from myKWIQ.
//
// The plugin listens on 127.0.0.1 (a free port) and opens myKWIQ's Connect EuroScope page with that
// port and a one-off state. After the controller clicks Connect there, the browser is sent to
// http://127.0.0.1:<port>/?code=...&state=<state>, and only with the right state is the code swapped
// for the key with Core (Claim), so the key never goes through the browser. ?key= is still accepted
// from a myKWIQ that hands the key over itself.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class KeyLink {
public:
    ~KeyLink() { Stop(); }
    // Swaps myKWIQ's one-off code for the key with Core; "" if Core says no. Runs on the listener's thread
    std::function<std::string(const std::string& code)> Claim;
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
