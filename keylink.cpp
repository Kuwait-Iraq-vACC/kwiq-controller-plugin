// keylink.cpp - see keylink.hpp
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <random>
#include <ctime>
#include "keylink.hpp"
#include "kwiq_logic.hpp"

static const int kMaxSeconds = 15 * 60;      // stops listening after this, if nothing arrives

static std::string RandomState() {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;                   // rand_s on Windows
    std::string s;
    for (int i = 0; i < 32; i++) s += chars[rd() % 62];
    return s;
}

static void Reply(SOCKET c, int status, const std::string& message) {
    std::string html =
        "<!doctype html><html lang=\"en-GB\"><head><meta charset=\"utf-8\"><title>KWIQ Controller Plugin</title>"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head>"
        "<body style=\"margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
        "background:#0b1220;color:#e5e7eb;font-family:Raleway,Segoe UI,sans-serif\">"
        "<div style=\"max-width:32rem;padding:2rem;border-radius:12px;background:#13203a;border-top:4px solid #b91c1c\">"
        "<h1 style=\"margin:0 0 .75rem;font-size:1.4rem\">KWIQ Controller Plugin</h1><p style=\"margin:0;line-height:1.6\">" +
        message + "</p></div></body></html>";
    std::string head = "HTTP/1.1 " + std::string(status == 200 ? "200 OK" : status == 404 ? "404 Not Found" : "400 Bad Request") +
                       "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\n"
                       "Connection: close\r\nContent-Length: " + std::to_string(html.size()) + "\r\n\r\n";
    std::string out = head + html;
    send(c, out.data(), (int)out.size(), 0);
}

bool KeyLink::Start() {
    if (running_) return true;
    if (thread_.joinable()) thread_.join();
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { WSACleanup(); return false; }
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);    // this computer only
    a.sin_port = 0;                                 // any free port
    int len = sizeof(a);
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 4) != 0 || getsockname(s, (sockaddr*)&a, &len) != 0) {
        closesocket(s); WSACleanup(); return false;
    }
    sock_ = (std::uintptr_t)s;
    port_ = ntohs(a.sin_port);
    state_ = RandomState();
    { std::lock_guard<std::mutex> g(mu_); received_.clear(); }
    stop_ = false;
    running_ = true;
    thread_ = std::thread(&KeyLink::Loop, this);
    return true;
}

void KeyLink::Stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::string KeyLink::Take() {
    std::lock_guard<std::mutex> g(mu_);
    std::string k;
    k.swap(received_);
    return k;
}

void KeyLink::Loop() {
    SOCKET s = (SOCKET)sock_;
    std::time_t until = std::time(nullptr) + kMaxSeconds;
    while (!stop_ && std::time(nullptr) < until) {
        fd_set fds; FD_ZERO(&fds); FD_SET(s, &fds);
        timeval tv = { 0, 300000 };
        if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET c = accept(s, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        DWORD timeout = 3000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        std::string req;
        char buf[2048];
        while (req.size() < 8192 && req.find("\r\n") == std::string::npos) {
            int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            req.append(buf, n);
        }
        std::string key = kwiq::QueryValue(req, "key"), state = kwiq::QueryValue(req, "state");
        bool ok = kwiq::IsApiKey(key) && state == state_;
        if (ok) {
            Reply(c, 200, "Your API key has been received. You can close this window and return to EuroScope.");
            { std::lock_guard<std::mutex> g(mu_); received_ = key; }
            stop_ = true;
        } else if (req.compare(0, 6, "GET /?") == 0) {
            Reply(c, 400, "This link is not valid. Please start again from the KWIQ button in EuroScope, and choose API Key.");
        } else {
            Reply(c, 404, "Not found.");
        }
        shutdown(c, SD_SEND);
        closesocket(c);
    }
    closesocket(s);
    sock_ = ~(std::uintptr_t)0;
    WSACleanup();
    running_ = false;
}
