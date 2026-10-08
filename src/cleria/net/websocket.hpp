// A WebSocket client over WinHTTP (Windows 8+; TLS by SChannel and the OS certificate store): the
// transport of the Lua mods' cleria.net.websocket (LUA_API.md §8; first written for the Archipelago
// connection, ARCHIPELAGO.md §2, now the Lua mod samples/mods/archipelago_lua). No third-party code.
// Linux / macOS: the same class over libcurl's WebSocket API (CMake CLERIA_CURL, websocket_curl.inl: one
// worker thread does the handshake, the sends and the receives); without it, a stub (supported() false).
//
// One worker thread per connection does the handshake and then blocks in WinHttpWebSocketReceive;
// a second thread drains the send queue (WinHTTP allows one pending send and one pending receive).
// The game thread only calls open / send / close / poll, which never block on the network
// (close() waits at most ~2 s for a polite close handshake, then aborts the handles).
//
// Addresses: "ws://host:port/path", "wss://...", "archipelago://host:port" (= ws://, as the
// reference client), or a bare "host[:port]". A bare address tries wss:// first and falls back to
// ws:// (apclientpp's order, which the retail Ys Origin mod's F8 menu also follows by prefixing
// wss://). The reference CommonClient.py does it the other way round (ws:// first, wss:// when the
// reply is not HTTP); either way the first scheme that completes the handshake wins. Default port
// 38281 (CommonClient.py server_loop).
//
// Not supported by WinHTTP: the permessage-deflate extension. AP's protocol doc says clients
// "should support per-message compression" and calls uncompressed connections "deprecated" (still
// accepted today). [H] If AP ever requires compression, replace this transport with our own
// framing (miniz has raw deflate) over winsock + SChannel; the Client above it is unaffected.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cleria::net {

struct WsUrl {
    bool secure = false;
    std::string host;          // without brackets for IPv6
    int port = 38281;
    std::string path = "/";
    std::string str() const;   // "wss://host:port/path"
};

// The URLs to try for a user-typed address, in order (empty when it cannot be parsed).
std::vector<WsUrl> candidate_urls(std::string_view address);

struct WsOptions {
    int resolve_timeout_ms = 10000;
    int connect_timeout_ms = 10000;
    int send_timeout_ms = 15000;
    int handshake_timeout_ms = 15000;     // the HTTP upgrade response
    std::size_t max_message = 16u << 20;  // the reference client's max_size (16 MB)
    std::string user_agent = "CleriaCore";
};

class WebSocket {
public:
    enum class State { Idle, Connecting, Open, Closing, Closed };
    struct Event {
        enum class Kind { Opened, Message, Closed };
        Kind kind;
        std::string text;      // Opened: the URL that worked; Message: the frame; Closed: the reason
        bool error = false;    // Closed: true when not closed by close()
    };

    // False when this build has no transport (off Windows without CLERIA_CURL, or a libcurl built
    // without WebSockets): open() then ends in Closed at once with the reason.
    static bool supported();

    WebSocket();
    ~WebSocket();              // closes and joins
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    // Starts connecting (closes a previous connection first). Each candidate URL is tried in
    // turn; Opened or Closed follows in poll().
    void open(std::string_view address, WsOptions opt = {});
    // Sends a text frame (queued; false when not open).
    bool send(std::string text);
    // Starts a polite close (status 1000); Closed follows. Safe in any state.
    void close();
    std::vector<Event> poll();
    State state() const { return state_.load(); }
    const std::string& url() const { return url_; }   // the URL being tried / in use (game thread)

private:
    struct Conn;
    void stop_and_join();
    void worker(std::shared_ptr<Conn> c, std::vector<WsUrl> urls);
    void push(Event e);

    std::atomic<State> state_{State::Idle};
    std::shared_ptr<Conn> conn_;
    std::thread thread_;
    std::mutex mu_;
    std::vector<Event> events_;
    std::string url_;
};

}  // namespace cleria::net
