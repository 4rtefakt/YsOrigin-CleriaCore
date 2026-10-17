// The WebSocket transport over libcurl (Linux / macOS, CMake CLERIA_CURL) — see websocket.hpp.
// Textual part of websocket.cpp (#included inside namespace cleria::net); not standalone.
//
// libcurl makes the TCP connection and its TLS session (CURLOPT_CONNECT_ONLY = 1: the certificate checked as for
// any https transfer), then carries raw bytes (curl_easy_send / curl_easy_recv); the WebSocket itself is
// ws_proto's, as on Windows (WebSocket::framed), so the server's compressed frames are inflated: libcurl's own
// WebSocket API has no permessage-deflate. A curl handle is not thread-safe, so ONE worker thread does
// everything, waiting on the socket in short slices. (its system headers, <curl/curl.h> and <poll.h>, are
// included at the top of websocket.cpp)

namespace {
// curl_global_init is not thread-safe: once, before the first handle (a function-local static).
bool curl_ready() {
    static const bool ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return ok;
}
struct Progress {
    const std::atomic<bool>* aborted;
    std::chrono::steady_clock::time_point deadline;
    bool timed_out = false;
};
int curl_progress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* g = static_cast<Progress*>(p);
    if (g->aborted->load()) return 1;
    if (std::chrono::steady_clock::now() > g->deadline) { g->timed_out = true; return 1; }
    return 0;
}
}  // namespace

bool WebSocket::supported() { return curl_ready(); }

// A TCP (and TLS) connection by libcurl, carrying the framed WebSocket's bytes.
class Stream {
public:
    ~Stream() { if (h_) curl_easy_cleanup(h_); }

    bool connect(const std::string& host, int port, bool secure, int timeout_ms, std::string* why) {
        if (!curl_ready() || !(h_ = curl_easy_init())) { *why = "libcurl unavailable"; return false; }
        const std::string h = host.find(':') != std::string::npos ? "[" + host + "]" : host;
        const std::string url = std::string(secure ? "https://" : "http://") + h + ":" + std::to_string(port) + "/";
        char err[CURL_ERROR_SIZE] = {};
        Progress prog{&aborted_, std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms)};
        curl_easy_setopt(h_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h_, CURLOPT_CONNECT_ONLY, 1L);
        curl_easy_setopt(h_, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h_, CURLOPT_ERRORBUFFER, err);
        curl_easy_setopt(h_, CURLOPT_CONNECTTIMEOUT_MS, long(timeout_ms));
        curl_easy_setopt(h_, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h_, CURLOPT_XFERINFOFUNCTION, curl_progress);
        curl_easy_setopt(h_, CURLOPT_XFERINFODATA, &prog);
        const CURLcode rc = curl_easy_perform(h_);
        curl_easy_setopt(h_, CURLOPT_NOPROGRESS, 1L);
        if (rc != CURLE_OK) {
            if (aborted_) *why = "cancelled";
            else if (prog.timed_out) *why = "timed out";
            else if (rc == CURLE_COULDNT_RESOLVE_HOST) *why = "server name not found";
            else if (rc == CURLE_COULDNT_CONNECT) *why = "cannot connect (nothing listening, or blocked)";
            else if (rc == CURLE_SSL_CONNECT_ERROR || rc == CURLE_PEER_FAILED_VERIFICATION) *why = std::string("TLS failure (") + (err[0] ? err : curl_easy_strerror(rc)) + ")";
            else *why = err[0] ? std::string(err) : std::string(curl_easy_strerror(rc));
            return false;
        }
        curl_easy_getinfo(h_, CURLINFO_ACTIVESOCKET, &sock_);
        return sock_ != CURL_SOCKET_BAD;
    }

    bool send(std::string_view data) {
        int spins = 0;
        while (!data.empty()) {
            if (aborted_) return false;
            std::size_t sent = 0;
            const CURLcode rc = curl_easy_send(h_, data.data(), data.size(), &sent);
            if (rc == CURLE_AGAIN) {
                pollfd pf{sock_, POLLOUT, 0};
                ::poll(&pf, 1, 50);
                if (++spins > 300) return false;   // [H] ~15 s without progress
                continue;
            }
            if (rc != CURLE_OK) return false;
            data.remove_prefix(sent);
            spins = 0;
        }
        return true;
    }

    bool recv(std::string* out, int ms, std::string* why) {
        if (aborted_) { *why = "closed"; return false; }
        pollfd pf{sock_, POLLIN, 0};
        ::poll(&pf, 1, ms);   // (TLS may hold decrypted bytes already: curl_easy_recv is tried either way)
        char buf[16384];
        for (;;) {
            std::size_t n = 0;
            const CURLcode rc = curl_easy_recv(h_, buf, sizeof buf, &n);
            if (rc == CURLE_AGAIN) return true;
            if (rc != CURLE_OK) { *why = aborted_ ? "closed" : std::string("connection lost: ") + curl_easy_strerror(rc); return false; }
            if (n == 0) { *why = "the server closed the connection"; return false; }
            out->append(buf, n);
            if (out->size() > (1u << 20)) return true;
        }
    }

    // From another thread: the worker gives up at its next slice (a curl handle is not thread-safe).
    void abort() { aborted_ = true; }

private:
    CURL* h_ = nullptr;
    curl_socket_t sock_ = CURL_SOCKET_BAD;
    std::atomic<bool> aborted_{false};
};

struct WebSocket::Conn {
    WsOptions opt;
    std::mutex hmu;                       // guards `stream` (abort() vs the worker)
    Stream* stream = nullptr;
    std::atomic<bool> stop{false};        // close() asked
    std::atomic<bool> by_user{false};
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

    void abort() {
        std::lock_guard<std::mutex> l(hmu);
        if (stream) stream->abort();
    }
};
