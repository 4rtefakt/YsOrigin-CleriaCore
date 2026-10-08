// The LiveSplit Server client (livesplit.hpp). Winsock on Windows; BSD sockets on Linux / macOS under the
// winsock names (the shim below), so both run the same code.
#include "cleria/livesplit.hpp"

#include <cmath>
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cleria::speedrun {

std::string ls_time(double ms) {
    const bool neg = ms < 0;
    const std::uint64_t t = std::uint64_t(std::llround(std::fabs(ms)));
    const std::uint64_t s = t / 1000, m = s / 60, h = m / 60;
    char b[48];
    std::snprintf(b, sizeof b, "%s%llu:%02llu:%02llu.%03llu", neg ? "-" : "", (unsigned long long)h,
                  (unsigned long long)(m % 60), (unsigned long long)(s % 60), (unsigned long long)(t % 1000));
    return b;
}

#ifndef _WIN32
namespace {
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
using BOOL = int;
#ifndef TRUE
#define TRUE 1
#endif
int closesocket(SOCKET s) { return ::close(s); }
bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;   // LiveSplit gone: an error, not a SIGPIPE
#else
constexpr int kSendFlags = 0;              // macOS: SO_NOSIGPIPE on the socket instead
#endif
}  // namespace
#endif

LiveSplitClient::LiveSplitClient() : sock_(std::uintptr_t(INVALID_SOCKET)) {}

LiveSplitClient::~LiveSplitClient() {
    close_socket();
#ifdef _WIN32
    if (wsa_) WSACleanup();
#endif
}

void LiveSplitClient::close_socket() {
    if (SOCKET(sock_) != INVALID_SOCKET) closesocket(SOCKET(sock_));
    sock_ = std::uintptr_t(INVALID_SOCKET);
}

void LiveSplitClient::configure(bool enabled, const std::string& host, int port) {
    const bool changed = host != host_ || port != port_;
    host_ = host;
    port_ = port;
    if (!enabled) {
        close_socket();
        state_ = State::Off;
        return;
    }
    if (state_ == State::Off || changed) {
        close_socket();
        state_ = State::Idle;
        next_try_ = 0;
    }
}

void LiveSplitClient::begin_connect(double now_ms) {
    next_try_ = now_ms + kRetryMs;
#ifdef _WIN32
    if (!wsa_) {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { last_error_ = "winsock unavailable"; return; }
        wsa_ = true;
    }
#endif
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* ai = nullptr;
    const std::string port = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), port.c_str(), &hints, &ai) != 0 || !ai) {
        last_error_ = "unknown host " + host_;
        return;
    }
    SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET) { freeaddrinfo(ai); last_error_ = "no socket"; return; }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
#endif
    BOOL nodelay = TRUE;                          // small commands, sent as they happen
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);
    const int r = connect(s, ai->ai_addr, int(ai->ai_addrlen));
    freeaddrinfo(ai);
    if (r == 0) {
        sock_ = std::uintptr_t(s);
        state_ = State::Connected;
        return;
    }
#ifdef _WIN32
    if (WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(s); last_error_ = "refused"; return; }
#else
    if (errno != EINPROGRESS) { closesocket(s); last_error_ = "refused"; return; }
#endif
    sock_ = std::uintptr_t(s);
    state_ = State::Connecting;
}

void LiveSplitClient::poll(double now_ms) {
    if (state_ == State::Off) return;
    if (state_ == State::Idle) {
        if (now_ms >= next_try_) begin_connect(now_ms);
        return;
    }
    const SOCKET s = SOCKET(sock_);
    if (state_ == State::Connecting) {
        fd_set w, e;
        FD_ZERO(&w); FD_ZERO(&e);
        FD_SET(s, &w); FD_SET(s, &e);
        timeval tv = {0, 0};
        if (select(int(s) + 1, nullptr, &w, &e, &tv) > 0) {
#ifndef _WIN32
            // BSD sockets report a failed connect as writable with SO_ERROR set (winsock: in `e`).
            int so = 0;
            socklen_t sl = sizeof so;
            if (FD_ISSET(s, &w) && (getsockopt(s, SOL_SOCKET, SO_ERROR, &so, &sl) != 0 || so != 0)) FD_SET(s, &e);
#endif
            if (FD_ISSET(s, &e)) {                // the connect failed (nothing listening)
                close_socket();
                state_ = State::Idle;
                last_error_ = "LiveSplit's TCP server is not running";
            } else if (FD_ISSET(s, &w)) {
                state_ = State::Connected;
                last_error_.clear();
            }
        } else if (now_ms >= next_try_ + kRetryMs) {   // [H] give a hung connect two retry periods
            close_socket();
            state_ = State::Idle;
            last_error_ = "no answer";
        }
        return;
    }
    // Connected: read and drop any reply (CleriaCore sends no query); 0 = closed by LiveSplit.
    char buf[512];
    for (;;) {
        const int n = recv(s, buf, sizeof buf, 0);
        if (n > 0) continue;
#ifdef _WIN32
        if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
#else
        if (n == 0 || !would_block()) {
#endif
            close_socket();
            state_ = State::Idle;
            next_try_ = now_ms + kRetryMs;
            last_error_ = "LiveSplit closed the connection";
        }
        break;
    }
}

bool LiveSplitClient::send(std::string_view cmd) {
    if (state_ != State::Connected) return false;
    const std::string line = std::string(cmd) + "\r\n";
#ifdef _WIN32
    const int n = ::send(SOCKET(sock_), line.data(), int(line.size()), 0);
#else
    const int n = int(::send(SOCKET(sock_), line.data(), line.size(), kSendFlags));
#endif
    if (n != int(line.size())) {                  // a short write never happens for a line this size
        close_socket();
        state_ = State::Idle;
        last_error_ = "send failed";
        return false;
    }
    if (log_.size() < kLogMax) log_.push_back(std::string(cmd));
    return true;
}

std::string LiveSplitClient::status() const {
    switch (state_) {
        case State::Off: return "Off";
        case State::Connected: return "Connected to " + host_ + ":" + std::to_string(port_);
        case State::Connecting: return "Connecting to " + host_ + ":" + std::to_string(port_) + "...";
        case State::Idle: break;
    }
    return "Not connected" + (last_error_.empty() ? std::string() : " (" + last_error_ + ")");
}


}  // namespace cleria::speedrun
