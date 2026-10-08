// cleria-nettest — the WinHTTP WebSocket client (src/cleria/net/websocket; the transport of the Lua mods'
// cleria.net.websocket, build/shell_re/LUA_API.md §8). Split out of the former cleria-aptest when the
// built-in C++ Archipelago client was removed (its Lua replacement, samples/mods/archipelago_lua, rides on
// this socket):
//   1. address parsing (bare host:port -> wss then ws, default port, IPv6, bad input);
//   2. a LOOPBACK WebSocket server (winsock, 127.0.0.1, an echo server) driven through net::WebSocket:
//      the wss -> ws fallback, a greeting, echo, a 2 MB message (fragments reassembled), an idle period
//      longer than the handshake timeout, a server kick, a polite close, a close the server ignores,
//      nothing listening, an invalid address. No internet is used.
// Usage: cleria-nettest            (ctest `nettest`)
//        cleria-nettest --live <address>   (manual, never run by ctest: opens a socket for 10 s, prints)
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else   // BSD sockets under the winsock names this test uses
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
static int closesocket(SOCKET s) { return ::close(s); }
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "cleria/net/websocket.hpp"

using namespace cleria::net;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[net] %-86s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

// ---- addresses ----------------------------------------------------------------------------

static void url_tests() {
    auto u = candidate_urls("archipelago.gg:38281");
    check(u.size() == 2 && u[0].str() == "wss://archipelago.gg:38281/" && u[1].str() == "ws://archipelago.gg:38281/",
          "bare host:port -> wss then ws");
    u = candidate_urls("  localhost  ");
    check(u.size() == 2 && u[0].port == 38281 && u[1].host == "localhost", "bare host -> default port 38281, trimmed");
    u = candidate_urls("wss://archipelago.gg:51234");
    check(u.size() == 1 && u[0].secure && u[0].port == 51234, "wss:// -> only wss");
    u = candidate_urls("WS://127.0.0.1:1/room?x=1");
    check(u.size() == 1 && !u[0].secure && u[0].path == "/room?x=1", "ws:// with a path, case-insensitive scheme");
    u = candidate_urls("archipelago://example.org:5000");
    check(u.size() == 1 && !u[0].secure && u[0].host == "example.org", "archipelago:// -> ws (the AP reference client)");
    u = candidate_urls("[::1]:4000");
    check(u.size() == 2 && u[0].host == "::1" && u[0].str() == "wss://[::1]:4000/", "IPv6 literal");
    check(candidate_urls("").empty() && candidate_urls("http://x").empty() && candidate_urls("host:0").empty() &&
              candidate_urls("host:99999").empty() && candidate_urls("host:12ab").empty() && candidate_urls("user@host:1").empty(),
          "bad addresses refused");
}

// ---- loopback WebSocket server ------------------------------------------------------------

// SHA-1 (FIPS 180-1) for the Sec-WebSocket-Accept header; test-only.
static std::string sha1(const std::string& msg) {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m = msg;
    const std::uint64_t bits = static_cast<std::uint64_t>(msg.size()) * 8;
    m += '\x80';
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xFF);
    const auto rol = [](std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (std::size_t off = 0; off < m.size(); off += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(std::uint8_t(m[off + i * 4])) << 24) | (std::uint32_t(std::uint8_t(m[off + i * 4 + 1])) << 16) |
                   (std::uint32_t(std::uint8_t(m[off + i * 4 + 2])) << 8) | std::uint32_t(std::uint8_t(m[off + i * 4 + 3]));
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::string out;
    for (std::uint32_t x : h)
        for (int i = 3; i >= 0; --i) out += static_cast<char>((x >> (i * 8)) & 0xFF);
    return out;
}

static std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const std::uint32_t v = (std::uint8_t(in[i]) << 16) | (std::uint8_t(in[i + 1]) << 8) | std::uint8_t(in[i + 2]);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const std::uint32_t v = std::uint8_t(in[i]) << 16;
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        const std::uint32_t v = (std::uint8_t(in[i]) << 16) | (std::uint8_t(in[i + 1]) << 8);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '=';
    }
    return out;
}

// A one-client-at-a-time WebSocket echo server on 127.0.0.1: it greets with "hello", echoes every text
// frame, answers "big" with a 2 MB message, and can drop a client (kick) or ignore its close frame.
struct MiniServer {
    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    std::thread th;
    std::atomic<bool> quit{false}, kick{false}, ignore_close{false};
    std::atomic<int> connections{0}, tls_attempts{0}, polite_closes{0}, texts{0};

    bool start() {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a) || listen(listener, 4)) return false;
        socklen_t len = sizeof a;
        getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        th = std::thread([this] { run(); });
        return true;
    }
    void stop() {
        quit = true;
        closesocket(listener);
        if (th.joinable()) th.join();
    }

    static bool send_all(SOCKET s, const std::string& d) {
        std::size_t off = 0;
        while (off < d.size()) {
            const int n = ::send(s, d.data() + off, static_cast<int>(std::min<std::size_t>(d.size() - off, 1 << 20)), 0);
            if (n <= 0) return false;
            off += n;
        }
        return true;
    }
    static bool send_frame(SOCKET s, int opcode, const std::string& payload) {
        std::string f;
        f += static_cast<char>(0x80 | opcode);
        const std::uint64_t n = payload.size();
        if (n < 126) f += static_cast<char>(n);
        else if (n < 65536) { f += static_cast<char>(126); f += static_cast<char>(n >> 8); f += static_cast<char>(n & 0xFF); }
        else { f += static_cast<char>(127); for (int i = 7; i >= 0; --i) f += static_cast<char>((n >> (i * 8)) & 0xFF); }
        return send_all(s, f + payload);
    }

    void run() {
        while (!quit) {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(listener, &rs);
            timeval tv{0, 50000};
            if (select(int(listener) + 1, &rs, nullptr, nullptr, &tv) <= 0) continue;
            SOCKET s = accept(listener, nullptr, nullptr);
            if (s == INVALID_SOCKET) continue;
            serve(s);
            closesocket(s);
        }
    }

    void serve(SOCKET s) {
        std::string req;
        char buf[65536];
        while (req.find("\r\n\r\n") == std::string::npos) {
            const int n = recv(s, buf, sizeof buf, 0);
            if (n <= 0) return;
            req.append(buf, n);
            if (req.size() >= 4 && req.compare(0, 4, "GET ") != 0) { ++tls_attempts; return; }   // a TLS hello: not ours
        }
        std::string key;
        {
            std::string low = req;
            for (char& ch : low) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            const std::size_t k = low.find("sec-websocket-key:");
            if (k == std::string::npos) return;
            std::size_t b = k + 18;
            while (req[b] == ' ') ++b;
            key = req.substr(b, req.find("\r\n", b) - b);
        }
        const std::string accept = base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
        send_all(s, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                        accept + "\r\n\r\n");
        ++connections;
        send_frame(s, 1, "hello");

        std::string in, msg;
        while (!quit) {
            if (kick.exchange(false)) return;       // drop the TCP connection without a close frame
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(s, &rs);
            timeval tv{0, 20000};
            if (select(int(s) + 1, &rs, nullptr, nullptr, &tv) < 0) return;
            if (FD_ISSET(s, &rs)) {
                const int n = recv(s, buf, sizeof buf, 0);
                if (n <= 0) return;
                in.append(buf, n);
            }
            while (in.size() >= 2) {                // complete client frames (always masked)
                const int op = in[0] & 0x0F;
                const bool fin = (in[0] & 0x80) != 0;
                std::uint64_t len = in[1] & 0x7F;
                std::size_t hdr = 2;
                if (len == 126) { if (in.size() < 4) break; len = (std::uint8_t(in[2]) << 8) | std::uint8_t(in[3]); hdr = 4; }
                else if (len == 127) { if (in.size() < 10) break; len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | std::uint8_t(in[2 + i]); hdr = 10; }
                if (in.size() < hdr + 4 + len) break;
                const char* mask = in.data() + hdr;
                std::string payload = in.substr(hdr + 4, static_cast<std::size_t>(len));
                for (std::size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i % 4];
                in.erase(0, hdr + 4 + static_cast<std::size_t>(len));
                if (op == 8) {
                    if (ignore_close) continue;
                    ++polite_closes;
                    send_frame(s, 8, payload.substr(0, 2));
                    return;
                }
                if (op == 9) { send_frame(s, 10, payload); continue; }
                if (op == 10) continue;
                msg += payload;
                if (!fin) continue;
                ++texts;
                send_frame(s, 1, msg == "big" ? std::string(2u << 20, 'x') : "echo: " + msg);
                msg.clear();
            }
        }
    }
};

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Polls the socket until `done` or the timeout; collects events.
static bool pump(WebSocket& ws, std::vector<WebSocket::Event>& ev, const std::function<bool()>& done, double timeout_s) {
    const double end = now_s() + timeout_s;
    while (now_s() < end) {
        for (WebSocket::Event& e : ws.poll()) ev.push_back(std::move(e));
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}
static const WebSocket::Event* find(const std::vector<WebSocket::Event>& ev, WebSocket::Event::Kind k, const std::string& text = {}) {
    for (const auto& e : ev)
        if (e.kind == k && (text.empty() || e.text == text)) return &e;
    return nullptr;
}

static void loopback_tests() {
    using K = WebSocket::Event::Kind;
    if (!WebSocket::supported()) {   // a build without a transport (off Windows without CLERIA_CURL)
        WebSocket ws;
        ws.open("127.0.0.1:1");
        std::vector<WebSocket::Event> ev;
        pump(ws, ev, [&] { return find(ev, K::Closed) != nullptr; }, 5);   // a worker thread may report it
        std::printf("      \"%s\"\n", ev.empty() ? "-" : ev[0].text.c_str());
        check(ev.size() == 1 && ev[0].kind == K::Closed && ev[0].error && ev[0].text.find("unsupported") != std::string::npos,
              "no WebSocket transport in this build: open() ends in Closed, \"unsupported\"");
        std::printf("[net] (no WebSocket transport in this build: the loopback checks are skipped)\n");
        return;
    }
#ifdef _WIN32
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
#else
    signal(SIGPIPE, SIG_IGN);   // the server writes to clients that may have gone
#endif
    check(base64(sha1("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11")) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
          "test server's Sec-WebSocket-Accept (RFC 6455 §1.3 example)");
    MiniServer srv;
    check(srv.start(), "loopback server listening on 127.0.0.1");
    const std::string addr = "127.0.0.1:" + std::to_string(srv.port);
    std::printf("      server at %s\n", addr.c_str());

    WebSocket ws;
    check(!ws.send("too early") && ws.state() == WebSocket::State::Idle, "send() before open: refused, idle");
    WsOptions opt;
    opt.handshake_timeout_ms = 800;                 // shorter than the idle wait below
    std::vector<WebSocket::Event> ev;
    ws.open(addr, opt);
    check(ws.state() == WebSocket::State::Connecting, "open(): connecting");
    const bool hello = pump(ws, ev, [&] { return find(ev, K::Message, "hello") != nullptr; }, 15);
    const WebSocket::Event* op = find(ev, K::Opened);
    std::printf("      opened %s | tls attempts seen %d\n", op ? op->text.c_str() : "-", srv.tls_attempts.load());
    check(hello && op && op->text.rfind("ws://127.0.0.1:", 0) == 0 && srv.tls_attempts >= 1 && ws.state() == WebSocket::State::Open,
          "bare address: the wss attempt reaches the server first, ws:// opens; the greeting arrives");
    ev.clear();
    check(ws.send("ping 1"), "send() while open");
    check(pump(ws, ev, [&] { return find(ev, K::Message, "echo: ping 1") != nullptr; }, 5), "a text frame is echoed");
    ev.clear();
    ws.send("big");
    pump(ws, ev, [&] { return find(ev, K::Message) != nullptr; }, 10);
    const WebSocket::Event* big = find(ev, K::Message);
    check(big && big->text.size() == (2u << 20), "a 2 MB message arrives whole (fragments reassembled)");

    // idle longer than the handshake timeout, then use the socket
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    ev.clear();
    ws.send("after idle");
    check(pump(ws, ev, [&] { return find(ev, K::Message, "echo: after idle") != nullptr; }, 5) && ws.state() == WebSocket::State::Open,
          "after 1.5 s idle (beyond the 0.8 s handshake timeout) the socket still works");

    // the server drops the connection
    ev.clear();
    srv.kick = true;
    const bool lost = pump(ws, ev, [&] { return find(ev, K::Closed) != nullptr; }, 5);
    const WebSocket::Event* cl = find(ev, K::Closed);
    std::printf("      \"%s\"\n", cl ? cl->text.c_str() : "-");
    check(lost && cl->error && cl->text.rfind("connection lost", 0) == 0 && ws.state() == WebSocket::State::Closed,
          "kick -> Closed with error \"connection lost: ...\"");

    // reopen, then a polite close
    ev.clear();
    ws.open("ws://" + addr, opt);
    const bool again = pump(ws, ev, [&] { return find(ev, K::Message, "hello") != nullptr; }, 10);
    check(again && srv.connections >= 2, "open() again on the same object: a new connection");
    ev.clear();
    ws.close();
    const bool closed = pump(ws, ev, [&] { return find(ev, K::Closed) != nullptr; }, 5);
    check(closed && !find(ev, K::Closed)->error && srv.polite_closes >= 1 && ws.state() == WebSocket::State::Closed,
          "close(): the close frame is answered, Closed without error");

    // a server that ignores the close frame: close() still finishes (abort after ~2 s)
    srv.ignore_close = true;
    WebSocket ws2;
    ws2.open("ws://" + addr);
    bool opened = false, closed2 = false;
    const double t0 = now_s();
    while (now_s() - t0 < 10 && !closed2) {
        for (auto& e : ws2.poll()) {
            if (e.kind == K::Opened) { opened = true; ws2.close(); }
            if (e.kind == K::Closed) closed2 = !e.error;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(opened && closed2 && now_s() - t0 < 6, "close() against a server that never answers finishes by itself");
    srv.ignore_close = false;

    // nothing listening: a clear failure
    WebSocket dead;
    dead.open("ws://127.0.0.1:1");
    std::string reason;
    const double t1 = now_s();
    while (now_s() - t1 < 15 && reason.empty()) {
        for (auto& e : dead.poll())
            if (e.kind == K::Closed) reason = e.text;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("      \"%s\"\n", reason.c_str());
    check(reason.find("could not connect: ws://127.0.0.1:1/") == 0, "nothing listening: \"could not connect: <url>: <why>\"");
    WebSocket bad;
    bad.open("http://nope");
    auto be = bad.poll();
    check(be.size() == 1 && be[0].kind == K::Closed && be[0].text.find("invalid server address") == 0,
          "an invalid address fails at once");

    srv.stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

// Manual check, never run by ctest: `cleria-nettest --live <address>` opens a socket and prints what
// happens for 10 s (e.g. an Archipelago room's RoomInfo, or the failure). Sends nothing but the handshake.
static int live(const char* address) {
    WebSocket ws;
    ws.open(address);
    const double t0 = now_s();
    while (now_s() - t0 < 10) {
        for (auto& e : ws.poll()) {
            if (e.kind == WebSocket::Event::Kind::Opened) std::printf("[live] opened %s\n", e.text.c_str());
            if (e.kind == WebSocket::Event::Kind::Message) std::printf("[live] message: %.300s\n", e.text.c_str());
            if (e.kind == WebSocket::Event::Kind::Closed) {
                std::printf("[live] closed: %s\n", e.text.c_str());
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ws.close();
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--live") return live(argv[2]);
    url_tests();
    loopback_tests();
    std::printf("[net] %s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
