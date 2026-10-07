// An Archipelago session: the protocol Client (client.hpp) driven over the WebSocket
// (websocket.hpp), with automatic reconnects, a keep-alive and a one-line status for a UI
// (build/shell_re/ARCHIPELAGO.md §2-3). The game owns one Session and calls update() once a frame
// on its main thread; everything it needs (events, received items, names) comes out of client().
#pragma once
#include <string>
#include <vector>

#include "cleria/ap/client.hpp"
#include "cleria/ap/websocket.hpp"

namespace cleria::ap {

class Session {
public:
    struct Options {
        bool auto_reconnect = true;
        double first_retry_s = 2.0;     // [H] backoff: 2, 4, 8, 16, 30, 30 ... s (apclientpp: 1.5 .. 15 s,
        double max_retry_s = 30.0;      //     CommonClient.py: 5 s doubling without a cap)
        double keepalive_s = 100.0;     // CommonClient.py keep_alive: an empty Bounce after 100 s idle
        WsOptions ws;
    };
    enum class Status {
        Idle,          // never connected / disconnect()ed
        Connecting,    // opening the socket (a URL candidate at a time)
        LoggingIn,     // socket open: RoomInfo, data package, Connect
        Connected,     // logged in
        Refused,       // ConnectionRefused or seed mismatch (no auto-retry; retry_login())
        Disconnected,  // lost / failed; a retry may be scheduled
    };

    explicit Session(DataPackageStore* store = nullptr) : store_(store), client_(Config{}, store) {}

    // Starts a new session (a fresh Client: no items, no checks) to `address` ("host:port",
    // "wss://host:port", ...).
    void connect(std::string address, Config cfg, Options opt);
    void connect(std::string address, Config cfg) { connect(std::move(address), std::move(cfg), Options{}); }
    void disconnect();                       // closes; no reconnect
    void retry_login(Config cfg);            // after Refused: new slot / password on the same socket
    void update(double now_s);               // monotonic seconds

    Client& client() { return client_; }
    const Client& client() const { return client_; }
    std::vector<Event> take_events();        // the Client's events, in order
    Status status() const { return status_; }
    std::string status_text(double now_s) const;   // "connected as Hugo (slot 2)", "auth failed: ...", ...
    const std::string& last_reason() const { return reason_; }
    const std::string& url() const { return ws_.url(); }
    double retry_at() const { return retry_at_; }  // < 0: none scheduled

private:
    void open_socket(double now_s);

    DataPackageStore* store_;
    Client client_;
    WebSocket ws_;
    Options opt_;
    std::string address_;
    Status status_ = Status::Idle;
    std::string reason_;
    double retry_at_ = -1, delay_ = 0, last_send_ = 0, now_ = 0;
    bool want_open_ = false;
    std::vector<Event> events_;
};

// "InvalidSlot" -> "no slot with that name in this room", ... (ConnectionRefused errors + SeedMismatch).
std::string describe_refusal(const std::string& error);

}  // namespace cleria::ap
