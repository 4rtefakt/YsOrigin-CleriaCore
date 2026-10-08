// The WebSocket transport over libcurl (Linux / macOS, CMake CLERIA_CURL) — see websocket.hpp.
// Textual part of websocket.cpp (#included inside namespace cleria::net); not standalone.
//
// libcurl 7.86+ built with WebSockets (CURLOPT_CONNECT_ONLY = 2: curl_easy_perform does the HTTP upgrade,
// then curl_ws_send / curl_ws_recv move frames; curl answers pings itself). A curl handle is not
// thread-safe, so ONE worker thread does everything: the handshake, the queued sends, and the receives,
// waiting on the socket in short slices. The messages and the events match the WinHTTP transport
// (apps/cleria-nettest runs the same loopback checks on both).
// (its system headers, <curl/curl.h> and <poll.h>, are included at the top of websocket.cpp)

namespace {
// curl_global_init is not thread-safe: once, before the first handle (a function-local static).
bool curl_ready() {
    static const bool ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return ok;
}
bool curl_has_ws() {
    if (!curl_ready()) return false;
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    for (const char* const* p = v ? v->protocols : nullptr; p && *p; ++p)
        if (std::strcmp(*p, "ws") == 0) return true;
    return false;
}
}  // namespace

bool WebSocket::supported() { return curl_has_ws(); }

struct WebSocket::Conn {
    WsOptions opt;
    std::atomic<bool> stop{false};        // close() asked
    std::atomic<bool> by_user{false};
    std::atomic<bool> aborted{false};     // abort(): the worker drops the connection at its next slice
    std::mutex qmu;
    std::condition_variable qcv;
    std::deque<std::string> outq;
    bool want_close = false;              // send the close frame after the queue
    bool writer_stop = false;
    std::mutex dmu;
    std::condition_variable dcv;
    bool done = false;
    std::chrono::steady_clock::time_point close_deadline{};
    std::string send_error;

    void abort() { aborted = true; }
};

namespace {
struct Progress {
    std::atomic<bool>* stop;
    std::atomic<bool>* aborted;
    std::chrono::steady_clock::time_point deadline;
    bool timed_out = false;
};
int curl_progress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* g = static_cast<Progress*>(p);
    if (g->stop->load() || g->aborted->load()) return 1;
    if (std::chrono::steady_clock::now() > g->deadline) { g->timed_out = true; return 1; }
    return 0;
}
// Sends one whole frame (curl may take it in pieces). CURLE_OK or the error.
CURLcode ws_send_all(CURL* h, curl_socket_t sock, const char* p, std::size_t n, unsigned flags) {
    std::size_t off = 0;
    for (int spins = 0;;) {
        std::size_t sent = 0;
        const CURLcode rc = curl_ws_send(h, p + off, n - off, &sent, 0, flags);
        if (rc == CURLE_AGAIN) {
            pollfd pf{sock, POLLOUT, 0};
            ::poll(&pf, 1, 50);
            if (++spins > 400) return CURLE_SEND_ERROR;   // [H] ~20 s without progress
            continue;
        }
        if (rc != CURLE_OK) return rc;
        off += sent;
        if (off >= n) return CURLE_OK;
        spins = 0;
    }
}
}  // namespace

void WebSocket::worker(std::shared_ptr<Conn> cp, std::vector<WsUrl> urls) {
    Conn& c = *cp;
    const WsOptions& o = c.opt;
    std::string failures, close_reason;
    CURL* h = nullptr;
    bool opened = false;

    const bool ws_ok = curl_has_ws();
    if (!ws_ok) failures = "this libcurl has no WebSocket support (unsupported on this platform)";
    Progress prog{&c.stop, &c.aborted, {}};
    for (std::size_t ui = 0; ws_ok && ui < urls.size() && !c.stop && !opened; ++ui) {
        const WsUrl& u = urls[ui];
        h = curl_easy_init();
        if (!h) { failures += (failures.empty() ? "" : "; ") + u.str() + ": curl_easy_init failed"; continue; }
        char err[CURL_ERROR_SIZE] = {};
        const std::string url = u.str();
        prog.timed_out = false;
        prog.deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(o.resolve_timeout_ms + o.connect_timeout_ms + o.handshake_timeout_ms);
        curl_easy_setopt(h, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 2L);   // the WebSocket upgrade, then frames by hand
        curl_easy_setopt(h, CURLOPT_USERAGENT, o.user_agent.c_str());
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h, CURLOPT_ERRORBUFFER, err);
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, long(o.resolve_timeout_ms + o.connect_timeout_ms));
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, curl_progress);
        curl_easy_setopt(h, CURLOPT_XFERINFODATA, &prog);
        const CURLcode rc = curl_easy_perform(h);
        std::string why;
        if (rc != CURLE_OK) {
            long status = 0;
            curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
            if (prog.timed_out) why = "timed out";
            else if (status > 0 && status != 101) why = "HTTP " + std::to_string(status) + " instead of a WebSocket upgrade";
            else why = err[0] ? std::string(err) : std::string(curl_easy_strerror(rc));
        }
        if (rc == CURLE_OK && !c.stop) {
            curl_easy_setopt(h, CURLOPT_NOPROGRESS, 1L);   // the handshake deadline ends here (idle is normal)
            opened = true;
            state_ = State::Open;
            push({Event::Kind::Opened, url, false});
        } else {
            curl_easy_cleanup(h);
            h = nullptr;
            if (c.stop) why = "cancelled";
            failures += (failures.empty() ? "" : "; ") + url + ": " + why;
        }
    }

    if (opened) {
        curl_socket_t sock = CURL_SOCKET_BAD;
        curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &sock);
        std::string msg;
        std::vector<char> buf(64 * 1024);
        bool close_sent = false;
        while (true) {
            if (c.aborted) { close_reason = c.stop ? "closed" : "connection lost: aborted"; break; }
            // the queue, then the close frame when asked
            std::string out;
            bool do_close = false, have = false;
            {
                std::lock_guard<std::mutex> l(c.qmu);
                if (!c.outq.empty()) { out = std::move(c.outq.front()); c.outq.pop_front(); have = true; }
                else if (c.want_close && !close_sent) do_close = true;
            }
            if (have) {
                const CURLcode rc = ws_send_all(h, sock, out.data(), out.size(), CURLWS_TEXT);
                if (rc != CURLE_OK) {
                    std::lock_guard<std::mutex> l(c.qmu);
                    c.send_error = std::string("send failed: ") + curl_easy_strerror(rc);
                    c.writer_stop = true;
                    close_reason = c.stop ? "closed" : c.send_error;
                    break;
                }
                continue;
            }
            if (do_close) {
                const char status[2] = {char(1000 >> 8), char(1000 & 0xFF)};   // 1000 normal closure
                ws_send_all(h, sock, status, sizeof status, CURLWS_CLOSE);
                close_sent = true;
                std::lock_guard<std::mutex> l(c.qmu);
                c.writer_stop = true;
            }
            // receive whatever is there
            pollfd pf{sock, POLLIN, 0};
            ::poll(&pf, 1, 10);                  // [H] 10 ms slices: a send waits at most this long
            bool stop_loop = false;
            for (;;) {
                std::size_t n = 0;
                const struct curl_ws_frame* meta = nullptr;
                const CURLcode rc = curl_ws_recv(h, buf.data(), buf.size(), &n, &meta);
                if (rc == CURLE_AGAIN) break;
                if (rc != CURLE_OK) {
                    close_reason = c.stop ? "closed"
                                          : "connection lost: " + std::string(rc == CURLE_GOT_NOTHING
                                                                                  ? "the server dropped the connection"
                                                                                  : curl_easy_strerror(rc));
                    stop_loop = true;
                    break;
                }
                if (!meta) continue;
                if (meta->flags & CURLWS_CLOSE) {
                    if (c.stop) {
                        close_reason = "closed";
                    } else {
                        const int st = n >= 2 ? (std::uint8_t(buf[0]) << 8) | std::uint8_t(buf[1]) : 0;
                        close_reason = "server closed the connection (" + std::to_string(st) +
                                       (n > 2 ? ", " + std::string(buf.data() + 2, n - 2) : std::string()) + ")";
                    }
                    stop_loop = true;
                    break;
                }
                if (meta->flags & (CURLWS_PING | CURLWS_PONG)) continue;   // curl answers pings itself
                msg.append(buf.data(), n);
                if (msg.size() > o.max_message) {
                    close_reason = "message larger than " + std::to_string(o.max_message) + " bytes";
                    stop_loop = true;
                    break;
                }
                if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
                    push({Event::Kind::Message, std::move(msg), false});
                    msg.clear();
                }
            }
            if (stop_loop) break;
        }
        {
            std::lock_guard<std::mutex> l(c.qmu);
            c.writer_stop = true;
            if (!c.send_error.empty() && !c.stop) close_reason = c.send_error;
        }
        curl_easy_cleanup(h);
    } else {
        close_reason = c.stop ? "closed" : "could not connect: " + failures;
    }

    state_ = State::Closed;
    {
        std::lock_guard<std::mutex> l(c.dmu);
        c.done = true;
    }
    c.dcv.notify_all();
    push({Event::Kind::Closed, close_reason, !c.by_user.load()});
}
