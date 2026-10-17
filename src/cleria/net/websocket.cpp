// WebSocket client — see websocket.hpp. Windows: its own framing with permessage-deflate (ws_proto) over winsock and
// SChannel, WinHTTP when that cannot connect. Off Windows: libcurl (CLERIA_CURL, websocket_curl.inl) or a stub whose
// open() reports "unsupported on this platform".
#include "cleria/net/websocket.hpp"
#include "cleria/net/ws_proto.hpp"

#include <chrono>
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
#include <windows.h>
#include <winhttp.h>
#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>
#include <sspi.h>
#include <cstdlib>
#include <random>
#elif defined(CLERIA_CURL)
#include <curl/curl.h>
#include <poll.h>

#include <algorithm>
#include <cstring>
#endif

namespace cleria::net {

// ---- URLs ----------------------------------------------------------------------------------

std::string WsUrl::str() const {
    std::string h = host.find(':') != std::string::npos ? "[" + host + "]" : host;
    return std::string(secure ? "wss://" : "ws://") + h + ":" + std::to_string(port) + path;
}

static std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

std::vector<WsUrl> candidate_urls(std::string_view address) {
    while (!address.empty() && (address.front() == ' ' || address.front() == '\t')) address.remove_prefix(1);
    while (!address.empty() && (address.back() == ' ' || address.back() == '\t' || address.back() == '\r' ||
                                address.back() == '\n'))
        address.remove_suffix(1);
    std::vector<WsUrl> out;
    if (address.empty()) return out;
    int scheme = 0;   // 0 bare, 1 ws, 2 wss
    const std::size_t sep = address.find("://");
    if (sep != std::string_view::npos) {
        const std::string s = lower(address.substr(0, sep));
        if (s == "ws" || s == "archipelago") scheme = 1;
        else if (s == "wss") scheme = 2;
        else return out;
        address.remove_prefix(sep + 3);
    }
    WsUrl u;
    std::string_view rest = address;
    if (!rest.empty() && rest.front() == '[') {
        const std::size_t close = rest.find(']');
        if (close == std::string_view::npos || close == 1) return out;
        u.host = std::string(rest.substr(1, close - 1));
        rest.remove_prefix(close + 1);
    } else {
        const std::size_t end = rest.find_first_of(":/?");
        u.host = std::string(rest.substr(0, end));
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end);
    }
    if (u.host.empty() || u.host.find_first_of(" \t@") != std::string::npos) return out;
    if (!rest.empty() && rest.front() == ':') {
        rest.remove_prefix(1);
        std::size_t k = 0;
        long port = 0;
        while (k < rest.size() && rest[k] >= '0' && rest[k] <= '9' && port <= 65535) port = port * 10 + (rest[k++] - '0');
        if (k == 0 || port < 1 || port > 65535) return out;
        u.port = static_cast<int>(port);
        rest.remove_prefix(k);
    }
    if (!rest.empty()) {
        if (rest.front() != '/' && rest.front() != '?') return out;
        u.path = rest.front() == '?' ? "/" + std::string(rest) : std::string(rest);
    }
    if (scheme == 2 || scheme == 0) { u.secure = true; out.push_back(u); }
    if (scheme == 1 || scheme == 0) { u.secure = false; out.push_back(u); }
    return out;
}

#ifdef _WIN32
bool WebSocket::supported() { return true; }

// ---- helpers -------------------------------------------------------------------------------

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static std::string error_text(DWORD e) {
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return "timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "server name not found";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "cannot connect (nothing listening, or blocked)";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "connection reset";
    case ERROR_WINHTTP_SECURE_FAILURE: return "TLS failure (certificate or protocol)";
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: return "invalid server response";
    case ERROR_WINHTTP_OPERATION_CANCELLED: return "cancelled";
    case ERROR_WINHTTP_INVALID_URL: return "invalid URL";
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return "unrecognised scheme";
    case ERROR_INVALID_HANDLE: return "cancelled";
    case ERROR_WINHTTP_SHUTDOWN: return "WinHTTP shut down";
    }
    char buf[256] = {};
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             GetModuleHandleA("winhttp.dll"), e, 0, buf, sizeof buf, nullptr);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '.')) buf[--n] = 0;
    return n ? std::string(buf) + " (" + std::to_string(e) + ")" : "error " + std::to_string(e);
}

#include "ws_stream_win.inl"   // Stream: winsock + SChannel

// ---- connection state shared by the threads ----------------------------------------------

struct WebSocket::Conn {
    WsOptions opt;
    std::mutex hmu;                       // guards the handles below (abort() vs the worker)
    HINTERNET session = nullptr, connect = nullptr, request = nullptr, ws = nullptr;
    Stream* stream = nullptr;             // the framed connection being made or used
    std::atomic<bool> stop{false};        // close() asked
    std::atomic<bool> by_user{false};
    std::mutex qmu;
    std::condition_variable qcv;
    std::deque<std::string> outq;
    bool want_close = false;              // writer: send the close frame
    bool writer_stop = false;
    std::mutex dmu;
    std::condition_variable dcv;
    bool done = false;
    std::chrono::steady_clock::time_point close_deadline{};
    std::string send_error;

    void set(HINTERNET& slot, HINTERNET h) {
        std::lock_guard<std::mutex> l(hmu);
        if (stop.load() && h) { WinHttpCloseHandle(h); h = nullptr; }
        slot = h;
    }
    HINTERNET get(HINTERNET& slot) {
        std::lock_guard<std::mutex> l(hmu);
        return slot;
    }
    void close_one(HINTERNET& slot) {
        std::lock_guard<std::mutex> l(hmu);
        if (slot) { WinHttpCloseHandle(slot); slot = nullptr; }
    }
    // Closing the handles makes any blocked WinHTTP call on them return.
    void abort() {
        std::lock_guard<std::mutex> l(hmu);
        if (stream) stream->abort();
        for (HINTERNET* h : {&ws, &request, &connect, &session})
            if (*h) { WinHttpCloseHandle(*h); *h = nullptr; }
    }
};
#elif defined(CLERIA_CURL)
#include "websocket_curl.inl"   // WebSocket::supported, WebSocket::Conn, WebSocket::worker over libcurl
#endif

#if defined(_WIN32) || defined(CLERIA_CURL)
WebSocket::WebSocket() = default;

WebSocket::~WebSocket() { stop_and_join(); }

void WebSocket::push(Event e) {
    std::lock_guard<std::mutex> l(mu_);
    events_.push_back(std::move(e));
}

void WebSocket::open(std::string_view address, WsOptions opt) {
    stop_and_join();
    {
        std::lock_guard<std::mutex> l(mu_);
        events_.clear();
    }
    auto urls = candidate_urls(address);
    if (urls.empty()) {
        state_ = State::Closed;
        push({Event::Kind::Closed, "invalid server address \"" + std::string(address) + "\"", true});
        return;
    }
    url_ = urls.front().str();
    conn_ = std::make_shared<Conn>();
    conn_->opt = std::move(opt);
    state_ = State::Connecting;
    thread_ = std::thread(&WebSocket::worker, this, conn_, std::move(urls));
}

bool WebSocket::send(std::string text) {
    if (state_ != State::Open || !conn_) return false;
    {
        std::lock_guard<std::mutex> l(conn_->qmu);
        if (conn_->want_close || conn_->writer_stop) return false;
        conn_->outq.push_back(std::move(text));
    }
    conn_->qcv.notify_one();
    return true;
}

void WebSocket::close() {
    if (!conn_ || state_ == State::Closed || state_ == State::Idle) return;
    Conn& c = *conn_;
    if (c.stop.exchange(true)) return;
    c.by_user = true;
    c.close_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    if (state_ == State::Open) {
        state_ = State::Closing;
        {
            std::lock_guard<std::mutex> l(c.qmu);
            c.want_close = true;                // the writer sends the close frame after the queue
        }
        c.qcv.notify_one();
    } else {
        state_ = State::Closing;
        c.abort();                              // still connecting: cancel the handshake
    }
}

std::vector<WebSocket::Event> WebSocket::poll() {
    if (conn_ && state_ == State::Closing && std::chrono::steady_clock::now() > conn_->close_deadline)
        conn_->abort();                         // the server did not answer the close frame
    std::vector<Event> ev;
    {
        std::lock_guard<std::mutex> l(mu_);
        ev.swap(events_);
    }
    for (const Event& e : ev) {
        if (e.kind == Event::Kind::Opened) url_ = e.text;
        if (e.kind == Event::Kind::Closed && thread_.joinable()) thread_.join();   // its last act
    }
    return ev;
}

void WebSocket::stop_and_join() {
    if (!thread_.joinable()) return;
    close();
    {
        std::unique_lock<std::mutex> l(conn_->dmu);
        conn_->dcv.wait_for(l, std::chrono::milliseconds(2500), [&] { return conn_->done; });
    }
    conn_->abort();
    thread_.join();
    state_ = State::Closed;
}
#endif

#ifdef _WIN32
// ---- the framed connection (permessage-deflate) ------------------------------------------------

// Connects to `u` with the own framing. False (with `why`) when no WebSocket was opened; true once it was, after
// the whole session, with `close_reason` set. One thread: the game's queued frames, then up to 50 ms of reading.
bool WebSocket::framed(Conn& c, const WsUrl& u, std::string* why, std::string* close_reason) {
    const WsOptions& o = c.opt;
    Stream st;
    {
        std::lock_guard<std::mutex> l(c.hmu);
        if (c.stop) { *why = "cancelled"; return false; }
        c.stream = &st;
    }
    struct Unset {
        Conn& c;
        ~Unset() { std::lock_guard<std::mutex> l(c.hmu); c.stream = nullptr; }
    } unset{c};
    std::mt19937 rng(std::random_device{}());
    if (!st.connect(u.host, u.port, u.secure, o.connect_timeout_ms, why)) return false;
    std::string raw;
    for (int i = 0; i < 16; ++i) raw.push_back(static_cast<char>(rng() & 0xff));
    const std::string key = ws::base64(raw);
    if (!st.send(ws::upgrade_request(u.host, u.port, u.secure, u.path, key, o.user_agent))) {
        *why = "connection lost";
        return false;
    }
    std::string in;
    ws::Handshake hs;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(o.handshake_timeout_ms);
    while (!hs.complete) {
        if (std::chrono::steady_clock::now() > deadline) { *why = "timed out"; return false; }
        std::string err;
        if (!st.recv(&in, 100, &err)) { *why = c.stop ? "cancelled" : err; return false; }
        if (c.stop) { *why = "cancelled"; return false; }
        hs = ws::parse_handshake(in, key);
    }
    if (!hs.ok) { *why = hs.error; return false; }

    state_ = State::Open;
    push({Event::Kind::Opened, u.str(), false});
    ws::Decoder dec(hs.deflate, hs.server_no_context_takeover, o.max_message);
    dec.feed(std::string_view(in).substr(hs.header_size));
    bool sent_close = false;
    std::string reason;
    while (reason.empty()) {
        // what the game queued, then the close frame once asked
        std::deque<std::string> out;
        bool want_close = false;
        {
            std::lock_guard<std::mutex> l(c.qmu);
            out.swap(c.outq);
            want_close = c.want_close;
        }
        for (const std::string& m : out)
            if (!st.send(ws::frame(ws::kText, m, rng()))) { reason = "send failed: connection lost"; break; }
        if (!reason.empty()) break;
        if (want_close && !sent_close) {
            st.send(ws::close_frame(1000, rng()));
            sent_close = true;
        }
        // the server's frames
        for (ws::Decoder::Out f = dec.next(); f.kind != ws::Decoder::Out::Kind::None && reason.empty(); f = dec.next()) {
            using K = ws::Decoder::Out::Kind;
            if (f.kind == K::Message) {
                push({Event::Kind::Message, std::move(f.data), false});
            } else if (f.kind == K::Ping) {
                st.send(ws::frame(ws::kPong, f.data, rng()));
            } else if (f.kind == K::Error) {
                st.send(ws::close_frame(1002, rng()));
                reason = "protocol error: " + f.data;
            } else if (f.kind == K::Close) {
                if (c.stop || sent_close) {
                    reason = "closed";
                } else {
                    st.send(ws::close_frame(f.status == 1005 ? 1000 : f.status, rng()));
                    reason = "server closed the connection (" + std::to_string(f.status) +
                             (f.data.empty() ? "" : ", " + f.data) + ")";
                }
            }
        }
        if (!reason.empty()) break;
        std::string more, err;
        if (!st.recv(&more, 50, &err)) { reason = c.stop || sent_close ? "closed" : "connection lost: " + err; break; }
        if (!more.empty()) dec.feed(more);
    }
    {
        std::lock_guard<std::mutex> l(c.qmu);
        c.writer_stop = true;
    }
    *close_reason = reason;
    return true;
}

// ---- the worker ----------------------------------------------------------------------------

void WebSocket::worker(std::shared_ptr<Conn> cp, std::vector<WsUrl> urls) {
    Conn& c = *cp;
    const WsOptions& o = c.opt;
    std::string failures;
    std::string close_reason;
    bool opened = false;

    c.set(c.session, WinHttpOpen(widen(o.user_agent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    HINTERNET session = c.get(c.session);
    if (!session) failures = c.stop ? "cancelled" : "WinHttpOpen: " + error_text(GetLastError());
    else WinHttpSetTimeouts(session, o.resolve_timeout_ms, o.connect_timeout_ms, o.send_timeout_ms, o.handshake_timeout_ms);

    // CLERIA_WS_WINHTTP=1: WinHTTP alone (no compression), as before the framed client.
    const char* only_winhttp = std::getenv("CLERIA_WS_WINHTTP");
    const bool framed_first = !(only_winhttp && *only_winhttp == '1');
    bool framed_done = false;
    for (std::size_t ui = 0; session && ui < urls.size() && !c.stop && !opened; ++ui) {
        const WsUrl& u = urls[ui];
        std::string why;
        if (framed_first) {
            std::string fwhy;
            if (framed(c, u, &fwhy, &close_reason)) { opened = framed_done = true; break; }
            if (c.stop) break;
            // The framed client could not make the TLS session (a TLS 1.3-only server, say): WinHTTP tries the same
            // URL. Any other failure (nothing listening, no such name, an HTTP answer) would be WinHTTP's as well.
            if (fwhy.rfind("TLS", 0) != 0) {
                failures += (failures.empty() ? "" : "; ") + u.str() + ": " + fwhy;
                continue;
            }
        }
        c.set(c.connect, WinHttpConnect(session, widen(u.host).c_str(), static_cast<INTERNET_PORT>(u.port), 0));
        HINTERNET con = c.get(c.connect);
        HINTERNET req = nullptr;
        if (!con) why = "connect: " + error_text(GetLastError());
        if (con) {
            c.set(c.request, WinHttpOpenRequest(con, L"GET", widen(u.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0));
            req = c.get(c.request);
            if (!req) why = error_text(GetLastError());
        }
        if (req) {
            if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
                why = "WebSocket upgrade unsupported: " + error_text(GetLastError());
            else if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                     !WinHttpReceiveResponse(req, nullptr))
                why = error_text(GetLastError());
            else {
                DWORD status = 0, len = sizeof status;
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
                if (status != 101) {
                    why = "HTTP " + std::to_string(status) + " instead of a WebSocket upgrade";
                } else {
                    // after the handshake a receive may wait forever (an idle room is normal)
                    DWORD infinite = 0;
                    WinHttpSetOption(req, WINHTTP_OPTION_RECEIVE_TIMEOUT, &infinite, sizeof infinite);
                    c.set(c.ws, WinHttpWebSocketCompleteUpgrade(req, 0));
                    if (!c.get(c.ws)) why = "upgrade: " + error_text(GetLastError());
                }
            }
        }
        c.close_one(c.request);
        if (c.get(c.ws)) {
            opened = true;
            state_ = State::Open;
            push({Event::Kind::Opened, u.str(), false});
        } else {
            c.close_one(c.connect);
            if (c.stop) why = "cancelled";
            failures += (failures.empty() ? "" : "; ") + u.str() + ": " + why;
        }
    }

    if (opened && !framed_done) {
        HINTERNET ws = c.get(c.ws);
        // the writer: queued frames, then the close frame when asked
        std::thread writer([&c, ws] {
            while (true) {
                std::string msg;
                bool do_close = false;
                {
                    std::unique_lock<std::mutex> l(c.qmu);
                    c.qcv.wait(l, [&] { return !c.outq.empty() || c.want_close || c.writer_stop; });
                    if (c.writer_stop) return;
                    if (!c.outq.empty()) { msg = std::move(c.outq.front()); c.outq.pop_front(); }
                    else { do_close = true; c.writer_stop = true; }
                }
                if (do_close) {
                    WinHttpWebSocketShutdown(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
                    return;
                }
                const DWORD err = WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                                       msg.data(), static_cast<DWORD>(msg.size()));
                if (err != NO_ERROR) {
                    {
                        std::lock_guard<std::mutex> l(c.qmu);
                        c.send_error = "send failed: " + error_text(err);
                        c.writer_stop = true;
                    }
                    c.abort();   // unblocks the reader
                    return;
                }
            }
        });

        std::string msg;
        std::vector<char> buf(64 * 1024);
        while (true) {
            DWORD n = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD err = WinHttpWebSocketReceive(ws, buf.data(), static_cast<DWORD>(buf.size()), &n, &type);
            if (err != NO_ERROR) {
                close_reason = c.stop ? "closed" : "connection lost: " + error_text(err);
                break;
            }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                USHORT status = 0;
                char reason[128] = {};
                DWORD rlen = 0;
                WinHttpWebSocketQueryCloseStatus(ws, &status, reason, sizeof reason - 1, &rlen);
                if (c.stop) {
                    close_reason = "closed";
                } else {
                    // [H] not answered with our own close frame (the writer may be mid-send and
                    // WinHTTP allows one pending send); the handles are closed right below.
                    close_reason = "server closed the connection (" + std::to_string(status) +
                                   (rlen ? ", " + std::string(reason, rlen) : std::string()) + ")";
                }
                break;
            }
            msg.append(buf.data(), n);
            if (msg.size() > o.max_message) {
                close_reason = "message larger than " + std::to_string(o.max_message) + " bytes";
                break;
            }
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                push({Event::Kind::Message, std::move(msg), false});
                msg.clear();
            }
        }
        {
            std::lock_guard<std::mutex> l(c.qmu);
            c.writer_stop = true;
            if (!c.send_error.empty() && !c.stop) close_reason = c.send_error;
        }
        c.qcv.notify_one();
        c.abort();          // unblocks a writer stuck in a send
        writer.join();
    } else if (!opened) {
        close_reason = c.stop ? "closed" : "could not connect: " + failures;
    }

    c.abort();
    state_ = State::Closed;
    {
        std::lock_guard<std::mutex> l(c.dmu);
        c.done = true;
    }
    c.dcv.notify_all();
    push({Event::Kind::Closed, close_reason, !c.by_user.load()});
}
#elif !defined(CLERIA_CURL)
// ---- no transport on this platform (a build without CLERIA_CURL off Windows) ----------------

struct WebSocket::Conn {};
bool WebSocket::supported() { return false; }
WebSocket::WebSocket() = default;
WebSocket::~WebSocket() = default;
void WebSocket::push(Event e) {
    std::lock_guard<std::mutex> l(mu_);
    events_.push_back(std::move(e));
}
void WebSocket::open(std::string_view address, WsOptions) {
    {
        std::lock_guard<std::mutex> l(mu_);
        events_.clear();
    }
    const std::vector<WsUrl> urls = candidate_urls(address);
    state_ = State::Closed;
    if (urls.empty()) {
        push({Event::Kind::Closed, "invalid server address \"" + std::string(address) + "\"", true});
        return;
    }
    url_ = urls.front().str();
    push({Event::Kind::Closed, "could not connect: WebSockets are unsupported on this platform (a build without CLERIA_CURL)",
          true});
}
bool WebSocket::send(std::string) { return false; }
void WebSocket::close() {}
std::vector<WebSocket::Event> WebSocket::poll() {
    std::vector<Event> ev;
    std::lock_guard<std::mutex> l(mu_);
    ev.swap(events_);
    return ev;
}
void WebSocket::stop_and_join() {}
void WebSocket::worker(std::shared_ptr<Conn>, std::vector<WsUrl>) {}
#endif

}  // namespace cleria::net
