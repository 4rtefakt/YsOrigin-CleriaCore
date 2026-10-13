// Plain sockets and one-shot HTTP for the Lua mods (cleria.net.udp / tcp / http; LUA_API.md §12.16, Mod API 7).
// CleriaCore's own: nothing here is retail. No third-party code: BSD sockets (winsock on Windows), and for HTTP
// the same transports as the WebSocket next door (WinHTTP on Windows, libcurl under CLERIA_CURL, else unsupported).
//
// The game thread only calls bind / connect / listen / send / poll / close; none of them waits on the network:
// the sockets are non-blocking, and a TCP connect (name lookup included) runs on a worker thread whose result
// poll() picks up. IPv4 only [H].
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cleria::net {

// A UDP socket. Datagrams of at most 64 KiB; nothing is ordered or guaranteed, as UDP is.
class UdpSocket {
public:
    struct Datagram { std::string host; int port = 0; std::string data; };

    UdpSocket() = default;
    ~UdpSocket() { close(); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Opens the socket on a local port (0 = any free one; `any` false = this machine only, 127.0.0.1).
    bool bind(int port, bool any, std::string* err);
    int port() const { return port_; }              // the local port once bound, else 0
    // Sends one datagram (binds to a free port first when not bound). `host` is an address or a name; a name is
    // looked up once (that first call waits for the lookup) and remembered.
    bool send_to(const std::string& host, int port, const std::string& data, std::string* err);
    std::vector<Datagram> poll(std::size_t max = 256);   // what arrived since the last call
    void close();
    bool is_open() const { return fd_ != kNone; }

private:
    static constexpr std::intptr_t kNone = -1;
    std::intptr_t fd_ = kNone;
    int port_ = 0;
    std::map<std::string, std::uint32_t> names_;     // host -> IPv4 (network order)
};

// A TCP socket: a client connection, a listening socket, or a connection a listener accepted.
class TcpSocket {
public:
    enum class State { Idle, Connecting, Open, Listening, Closed };
    struct Event {
        enum class Kind { Opened, Data, Closed, Accepted };
        Kind kind;
        std::string text;                            // Data: the bytes; Closed: the reason; Opened / Accepted: "host:port"
        bool error = false;                          // Closed: true when not closed by close() or by the peer's own close
        std::unique_ptr<TcpSocket> socket;           // Accepted: the new connection (already Open)
    };

    TcpSocket();
    ~TcpSocket();
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    void connect(const std::string& host, int port, int timeout_ms = 10000);   // Opened or Closed follows in poll()
    bool listen(int port, bool any, std::string* err);                         // Accepted events follow in poll()
    int port() const { return port_; }               // the local port of a listener (or of a connection), else 0
    bool send(const std::string& data);              // queued; false when not open (or 16 MiB are already waiting)
    std::vector<Event> poll();
    void close();
    State state() const;

private:
    struct Pending;                                  // the connect worker's result
    static constexpr std::intptr_t kNone = -1;
    void adopt(std::intptr_t fd, std::string peer);
    void fail(std::vector<Event>& ev, const std::string& why, bool error);
    std::intptr_t fd_ = kNone;
    State state_ = State::Idle;
    int port_ = 0;
    std::string peer_;
    std::string out_;                                // bytes not sent yet
    std::shared_ptr<Pending> pending_;
    bool announced_ = true;                          // an adopted connection reports Opened on its first poll
};

struct HttpRequest {
    std::string url;                                 // http:// or https://
    std::string method = "GET";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    int timeout_ms = 15000;
    std::size_t max_body = 8u << 20;                 // a longer answer is cut there (HttpResponse::truncated)
};
struct HttpResponse {
    bool ok = false;                                 // the exchange completed (whatever the status)
    int status = 0;
    std::string body;
    std::string error;                               // when !ok
    bool truncated = false;
};
// False when this build has no HTTP transport (off Windows without CLERIA_CURL).
bool http_supported();
// One request, start to end, on the calling thread (it waits: call it from a worker).
HttpResponse http_request(const HttpRequest& rq);

}  // namespace cleria::net
