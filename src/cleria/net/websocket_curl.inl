// The WebSocket transport over libcurl (Linux / macOS, CMake CLERIA_CURL) — see websocket.hpp.
// Textual part of websocket.cpp (#included inside namespace cleria::net); not standalone.
//
// libcurl only connects (CURLOPT_CONNECT_ONLY = 1: TCP, TLS for wss://, a proxy tunnel when one is set);
// the HTTP upgrade and the frames are ours (websocket_frames.inl), as curl's own WebSocket API cannot take
// permessage-deflate. A curl handle is not thread-safe, so ONE worker thread does everything: the handshake,
// the queued sends, and the receives, waiting on the socket and on a wake-up pipe. The messages and the events
// match the Windows transport (apps/cleria-nettest runs the same loopback checks on both).
// (its system headers, <curl/curl.h> and <poll.h>, are included at the top of websocket.cpp)

namespace {
// curl_global_init is not thread-safe: once, before the first handle (a function-local static).
bool curl_ready() {
    static const bool ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return ok;
}
}  // namespace

bool WebSocket::supported() { return curl_ready(); }

struct WebSocket::Conn {
    WsOptions opt;
    std::atomic<bool> stop{false};        // close() asked
    std::atomic<bool> by_user{false};
    std::atomic<bool> aborted{false};     // abort(): the worker drops the connection
    int wake_rd = -1, wake_wr = -1;       // a pipe: a byte written wakes the worker's wait on the socket
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

    Conn() {
        int fd[2];
        if (::pipe(fd) == 0) {
            wake_rd = fd[0];
            wake_wr = fd[1];
            fcntl(wake_rd, F_SETFL, O_NONBLOCK);
            fcntl(wake_wr, F_SETFL, O_NONBLOCK);
        }
    }
    ~Conn() {
        if (wake_rd >= 0) ::close(wake_rd);
        if (wake_wr >= 0) ::close(wake_wr);
    }
    void wake_up() {
        const char b = 1;
        if (wake_wr >= 0 && ::write(wake_wr, &b, 1) < 0) {}   // full: a wake-up is pending anyway
    }
    void abort() { aborted = true; wake_up(); }
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

// libcurl's connection as a byte stream (curl_easy_send / curl_easy_recv).
class CurlStream final : public Stream {
public:
    CurlStream(CURL* h, curl_socket_t sock, int send_timeout_ms, int wake_rd)
        : h_(h), sock_(sock), send_timeout_ms_(send_timeout_ms), wake_rd_(wake_rd) {}
    bool write(const char* p, std::size_t n) override {
        auto last = std::chrono::steady_clock::now();
        while (n) {
            std::size_t sent = 0;
            const CURLcode rc = curl_easy_send(h_, p, n, &sent);
            if (rc == CURLE_AGAIN) {
                if (std::chrono::steady_clock::now() - last > std::chrono::milliseconds(send_timeout_ms_)) {
                    err = "timed out";
                    return false;
                }
                pollfd pf{sock_, POLLOUT, 0};
                ::poll(&pf, 1, 50);
                continue;
            }
            if (rc != CURLE_OK) { err = curl_easy_strerror(rc); return false; }
            p += sent;
            n -= sent;
            last = std::chrono::steady_clock::now();
        }
        return true;
    }
    long read(char* p, std::size_t n, int wait_ms) override {
        for (int pass = 0; pass < 2; ++pass) {   // what the TLS layer holds first, then wait on the socket
            std::size_t got = 0;
            const CURLcode rc = curl_easy_recv(h_, p, n, &got);
            if (rc == CURLE_OK && got > 0) return static_cast<long>(got);
            if (rc == CURLE_OK) { err = "the server dropped the connection"; return -1; }
            if (rc != CURLE_AGAIN) { err = curl_easy_strerror(rc); return -1; }
            if (pass == 0) {
                pollfd pf[2] = {{sock_, POLLIN, 0}, {wake_rd_, POLLIN, 0}};
                if (::poll(pf, wake_rd_ >= 0 ? 2 : 1, wait_ms) <= 0) return 0;
                char drain[64];
                if (pf[1].revents & POLLIN)
                    while (::read(wake_rd_, drain, sizeof drain) > 0) {}
                if (!(pf[0].revents & (POLLIN | POLLHUP | POLLERR))) return 0;
            }
        }
        return 0;
    }

private:
    CURL* h_;
    curl_socket_t sock_;
    int send_timeout_ms_;
    int wake_rd_;
};
}  // namespace

void WebSocket::worker(std::shared_ptr<Conn> cp, std::vector<WsUrl> urls) {
    Conn& c = *cp;
    const WsOptions& o = c.opt;
    std::string failures, close_reason;
    bool opened = false;

    const bool ok = curl_ready();
    if (!ok) failures = "curl_global_init failed (unsupported on this platform)";
    Progress prog{&c.stop, &c.aborted, {}};
    for (std::size_t ui = 0; ok && ui < urls.size() && !c.stop && !opened; ++ui) {
        const WsUrl& u = urls[ui];
        CURL* h = curl_easy_init();
        if (!h) { failures += (failures.empty() ? "" : "; ") + u.str() + ": curl_easy_init failed"; continue; }
        char err[CURL_ERROR_SIZE] = {};
        const std::string host = u.host.find(':') != std::string::npos ? "[" + u.host + "]" : u.host;
        const std::string url = std::string(u.secure ? "https://" : "http://") + host + ":" + std::to_string(u.port) + u.path;
        prog.timed_out = false;
        prog.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(o.resolve_timeout_ms + o.connect_timeout_ms);
        curl_easy_setopt(h, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 1L);     // connect (and TLS), then bytes by hand
        curl_easy_setopt(h, CURLOPT_HTTPPROXYTUNNEL, 1L);  // through a proxy: a CONNECT tunnel for ws:// too
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h, CURLOPT_ERRORBUFFER, err);
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, long(o.resolve_timeout_ms + o.connect_timeout_ms));
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, curl_progress);
        curl_easy_setopt(h, CURLOPT_XFERINFODATA, &prog);
        const CURLcode rc = curl_easy_perform(h);
        std::string why;
        curl_socket_t sock = CURL_SOCKET_BAD;
        if (rc != CURLE_OK) why = prog.timed_out ? "timed out" : err[0] ? std::string(err) : std::string(curl_easy_strerror(rc));
        else if (curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &sock) != CURLE_OK || sock == CURL_SOCKET_BAD) why = "no socket";
        if (why.empty() && !c.stop) {
            CurlStream st(h, sock, o.send_timeout_ms, c.wake_rd);
            bool deflate = false;
            std::string rest;
            why = ws_upgrade(c, st, u, o, deflate, rest);
            if (why.empty()) {
                opened = true;
                state_ = State::Open;
                push({Event::Kind::Opened, u.str(), false});
                close_reason = ws_session(c, st, deflate, std::move(rest), o.max_message,
                                          [this](std::string m) { push({Event::Kind::Message, std::move(m), false}); });
            }
        }
        curl_easy_cleanup(h);
        if (!opened) {
            if (c.stop) why = "cancelled";
            failures += (failures.empty() ? "" : "; ") + u.str() + ": " + why;
        }
    }

    {
        std::lock_guard<std::mutex> l(c.qmu);
        c.writer_stop = true;
        if (opened && !c.send_error.empty() && !c.stop) close_reason = c.send_error;
    }
    if (!opened) close_reason = c.stop ? "closed" : "could not connect: " + failures;

    state_ = State::Closed;
    {
        std::lock_guard<std::mutex> l(c.dmu);
        c.done = true;
    }
    c.dcv.notify_all();
    push({Event::Kind::Closed, close_reason, !c.by_user.load()});
}
