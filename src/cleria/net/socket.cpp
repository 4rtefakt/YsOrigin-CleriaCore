// Plain sockets and one-shot HTTP for the Lua mods: see socket.hpp.
#include "cleria/net/socket.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

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
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef CLERIA_CURL
#include <curl/curl.h>
#endif
#endif

namespace cleria::net {
namespace {

#ifdef _WIN32
using sock_t = SOCKET;
const sock_t kBad = INVALID_SOCKET;
void close_fd(sock_t s) { ::closesocket(s); }
bool would_block() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINTR; }
std::string last_error() { return "socket error " + std::to_string(WSAGetLastError()); }
bool set_nonblocking(sock_t s, bool on) { u_long v = on ? 1 : 0; return ::ioctlsocket(s, FIONBIO, &v) == 0; }
bool startup() {
    static const bool ok = [] { WSADATA wd; return WSAStartup(MAKEWORD(2, 2), &wd) == 0; }();
    return ok;
}
using socklen = int;
#else
using sock_t = int;
const sock_t kBad = -1;
void close_fd(sock_t s) { ::close(s); }
bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR; }
std::string last_error() { return std::string("socket error: ") + std::strerror(errno); }
bool set_nonblocking(sock_t s, bool on) {
    const int f = ::fcntl(s, F_GETFL, 0);
    return f >= 0 && ::fcntl(s, F_SETFL, on ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK)) == 0;
}
bool startup() { return true; }
using socklen = socklen_t;
#endif

sock_t S(std::intptr_t fd) { return sock_t(fd); }

// An IPv4 address of a host: a dotted address at once, a name through the resolver (this waits).
bool resolve(const std::string& host, std::uint32_t* out) {
    in_addr a{};
    if (::inet_pton(AF_INET, host.c_str(), &a) == 1) { *out = a.s_addr; return true; }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    *out = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    ::freeaddrinfo(res);
    return true;
}

std::string name_of(const sockaddr_in& a) {
    char b[64] = "";
    ::inet_ntop(AF_INET, const_cast<in_addr*>(&a.sin_addr), b, sizeof b);
    return b;
}

int local_port(sock_t s) {
    sockaddr_in a{};
    socklen n = sizeof a;
    return ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &n) == 0 ? int(ntohs(a.sin_port)) : 0;
}

sockaddr_in addr_of(std::uint32_t ip, int port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = ip;
    a.sin_port = htons(std::uint16_t(port));
    return a;
}

}  // namespace

// ---- UDP -------------------------------------------------------------------------------------------------------

bool UdpSocket::bind(int port, bool any, std::string* err) {
    close();
    if (!startup() || port < 0 || port > 65535) { if (err) *err = "bad port"; return false; }
    const sock_t s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBad) { if (err) *err = last_error(); return false; }
    const sockaddr_in a = addr_of(any ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK), port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0 || !set_nonblocking(s, true)) {
        if (err) *err = last_error();
        close_fd(s);
        return false;
    }
    fd_ = std::intptr_t(s);
    port_ = local_port(s);
    return true;
}

bool UdpSocket::send_to(const std::string& host, int port, const std::string& data, std::string* err) {
    if (port < 1 || port > 65535 || data.size() > 65507) { if (err) *err = "bad port or a datagram over 65507 bytes"; return false; }
    if (fd_ == kNone && !bind(0, true, err)) return false;
    auto it = names_.find(host);
    if (it == names_.end()) {
        std::uint32_t ip = 0;
        if (!resolve(host, &ip)) { if (err) *err = "unknown host"; return false; }
        if (names_.size() >= 256) names_.clear();
        it = names_.emplace(host, ip).first;
    }
    const sockaddr_in a = addr_of(it->second, port);
    const auto n = ::sendto(S(fd_), data.data(), int(data.size()), 0, reinterpret_cast<const sockaddr*>(&a), sizeof a);
    if (n < 0 && !would_block()) { if (err) *err = last_error(); return false; }
    return true;                                    // a full send buffer drops the datagram, as UDP may
}

std::vector<UdpSocket::Datagram> UdpSocket::poll(std::size_t max) {
    std::vector<Datagram> out;
    if (fd_ == kNone) return out;
    static thread_local std::vector<char> buf(65536);
    while (out.size() < max) {
        sockaddr_in from{};
        socklen fl = sizeof from;
        const auto n = ::recvfrom(S(fd_), buf.data(), int(buf.size()), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAECONNRESET) continue;   // an ICMP "port unreachable" for an earlier send: not ours to report
#endif
            break;
        }
        out.push_back({name_of(from), int(ntohs(from.sin_port)), std::string(buf.data(), std::size_t(n))});
    }
    return out;
}

void UdpSocket::close() {
    if (fd_ != kNone) close_fd(S(fd_));
    fd_ = kNone;
    port_ = 0;
}

// ---- TCP -------------------------------------------------------------------------------------------------------

struct TcpSocket::Pending {
    std::mutex mu;
    bool done = false, abandoned = false;
    std::intptr_t fd = -1;
    std::string peer, error;
};

TcpSocket::TcpSocket() = default;
TcpSocket::~TcpSocket() { close(); }

TcpSocket::State TcpSocket::state() const { return state_; }

void TcpSocket::adopt(std::intptr_t fd, std::string peer) {
    fd_ = fd;
    peer_ = std::move(peer);
    state_ = State::Open;
    port_ = local_port(S(fd));
    announced_ = false;
    const int one = 1;
    ::setsockopt(S(fd), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

void TcpSocket::connect(const std::string& host, int port, int timeout_ms) {
    close();
    state_ = State::Connecting;
    pending_ = std::make_shared<Pending>();
    std::shared_ptr<Pending> p = pending_;
    std::thread([p, host, port, timeout_ms] {
        std::string err;
        sock_t s = kBad;
        std::uint32_t ip = 0;
        if (!startup() || port < 1 || port > 65535) err = "bad port";
        else if (!resolve(host, &ip)) err = "unknown host";
        else if ((s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == kBad) err = last_error();
        else {
            // A non-blocking connect waited on with select, so the timeout is ours and not the system's.
            set_nonblocking(s, true);
            const sockaddr_in a = addr_of(ip, port);
            int r = ::connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a);
            if (r != 0) {
#ifdef _WIN32
                const bool wait = WSAGetLastError() == WSAEWOULDBLOCK;
#else
                const bool wait = errno == EINPROGRESS;
#endif
                if (!wait) err = last_error();
                else {
                    fd_set wr, ex;
                    FD_ZERO(&wr); FD_ZERO(&ex);
                    FD_SET(s, &wr); FD_SET(s, &ex);
                    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
                    r = ::select(int(s) + 1, nullptr, &wr, &ex, &tv);
                    int soerr = 0;
                    socklen sl = sizeof soerr;
                    ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &sl);
                    if (r <= 0) err = "the connection timed out";
                    else if (soerr != 0 || FD_ISSET(s, &ex)) err = "the connection was refused";
                }
            }
        }
        if (!err.empty() && s != kBad) { close_fd(s); s = kBad; }
        std::lock_guard<std::mutex> lk(p->mu);
        if (p->abandoned) { if (s != kBad) close_fd(s); return; }
        p->done = true;
        p->fd = s == kBad ? -1 : std::intptr_t(s);
        p->peer = host + ":" + std::to_string(port);
        p->error = err;
    }).detach();
}

bool TcpSocket::listen(int port, bool any, std::string* err) {
    close();
    if (!startup() || port < 0 || port > 65535) { if (err) *err = "bad port"; return false; }
    const sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kBad) { if (err) *err = last_error(); return false; }
#ifndef _WIN32
    const int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);   // (on Windows this would let two programs share the port)
#endif
    const sockaddr_in a = addr_of(any ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK), port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0 || ::listen(s, 8) != 0 || !set_nonblocking(s, true)) {
        if (err) *err = last_error();
        close_fd(s);
        return false;
    }
    fd_ = std::intptr_t(s);
    port_ = local_port(s);
    state_ = State::Listening;
    return true;
}

bool TcpSocket::send(const std::string& data) {
    if (state_ != State::Open || out_.size() + data.size() > (16u << 20)) return false;
    out_ += data;
    while (!out_.empty()) {                          // what the socket takes now; the rest (or an error) is poll()'s
        const auto n = ::send(S(fd_), out_.data(), int(std::min<std::size_t>(out_.size(), 1u << 16)), 0);
        if (n <= 0) break;
        out_.erase(0, std::size_t(n));
    }
    return true;
}

void TcpSocket::fail(std::vector<Event>& ev, const std::string& why, bool error) {
    if (fd_ != kNone) close_fd(S(fd_));
    fd_ = kNone;
    out_.clear();
    state_ = State::Closed;
    Event e;
    e.kind = Event::Kind::Closed;
    e.text = why;
    e.error = error;
    ev.push_back(std::move(e));
}

std::vector<TcpSocket::Event> TcpSocket::poll() {
    std::vector<Event> ev;
    if (state_ == State::Connecting && pending_) {
        std::lock_guard<std::mutex> lk(pending_->mu);
        if (pending_->done) {
            if (pending_->fd >= 0) {
                adopt(pending_->fd, pending_->peer);
            } else {
                state_ = State::Closed;
                Event e;
                e.kind = Event::Kind::Closed;
                e.text = pending_->error;
                e.error = true;
                ev.push_back(std::move(e));
            }
        }
    }
    if (state_ != State::Connecting) pending_.reset();
    if (state_ == State::Open && !announced_) {
        announced_ = true;
        Event e;
        e.kind = Event::Kind::Opened;
        e.text = peer_;
        ev.push_back(std::move(e));
    }
    if (state_ == State::Listening) {
        for (int i = 0; i < 16; ++i) {
            sockaddr_in from{};
            socklen fl = sizeof from;
            const sock_t c = ::accept(S(fd_), reinterpret_cast<sockaddr*>(&from), &fl);
            if (c == kBad) break;
            set_nonblocking(c, true);
            Event e;
            e.kind = Event::Kind::Accepted;
            e.text = name_of(from) + ":" + std::to_string(int(ntohs(from.sin_port)));
            e.socket = std::make_unique<TcpSocket>();
            e.socket->adopt(std::intptr_t(c), e.text);
            e.socket->announced_ = true;             // the Accepted event is its opening
            ev.push_back(std::move(e));
        }
        return ev;
    }
    if (state_ != State::Open) return ev;
    while (!out_.empty()) {                          // what the socket takes now; the rest waits for the next poll
        const auto n = ::send(S(fd_), out_.data(), int(std::min<std::size_t>(out_.size(), 1u << 16)), 0);
        if (n > 0) { out_.erase(0, std::size_t(n)); continue; }
        if (!would_block()) { fail(ev, last_error(), true); return ev; }
        break;
    }
    static thread_local std::vector<char> buf(65536);
    for (int i = 0; i < 64; ++i) {                   // at most 4 MiB a poll
        const auto n = ::recv(S(fd_), buf.data(), int(buf.size()), 0);
        if (n > 0) {
            if (!ev.empty() && ev.back().kind == Event::Kind::Data && ev.back().text.size() < (1u << 20)) ev.back().text.append(buf.data(), std::size_t(n));
            else { Event e; e.kind = Event::Kind::Data; e.text.assign(buf.data(), std::size_t(n)); ev.push_back(std::move(e)); }
            continue;
        }
        if (n == 0) { fail(ev, "closed by the other side", false); return ev; }
        if (!would_block()) fail(ev, last_error(), true);
        break;
    }
    return ev;
}

void TcpSocket::close() {
    if (pending_) {
        std::lock_guard<std::mutex> lk(pending_->mu);
        pending_->abandoned = true;
        if (pending_->done && pending_->fd >= 0) close_fd(S(pending_->fd));
    }
    pending_.reset();
    if (fd_ != kNone) {
        if (state_ == State::Open && !out_.empty()) {   // a last try for what is still queued (a "bye" before close)
            set_nonblocking(S(fd_), true);
            ::send(S(fd_), out_.data(), int(std::min<std::size_t>(out_.size(), 1u << 16)), 0);
        }
        close_fd(S(fd_));
    }
    fd_ = kNone;
    out_.clear();
    port_ = 0;
    state_ = state_ == State::Idle ? State::Idle : State::Closed;
    announced_ = true;
}

// ---- HTTP ------------------------------------------------------------------------------------------------------

#ifdef _WIN32

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(std::max(n, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};
}  // namespace

bool http_supported() { return true; }

HttpResponse http_request(const HttpRequest& rq) {
    HttpResponse rs;
    auto fail = [&](const char* what) {
        rs.ok = false;
        rs.error = std::string(what) + " (" + std::to_string(GetLastError()) + ")";
        return rs;
    };
    const std::wstring url = widen(rq.url);
    wchar_t host[256] = L"", path[4096] = L"", extra[4096] = L"";
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)) {
        rs.error = "not an http:// or https:// address";
        return rs;
    }
    Handle ses, con, req;
    ses.h = WinHttpOpen(L"CleriaCore", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses.h) return fail("WinHttpOpen");
    const int t = std::clamp(rq.timeout_ms, 1000, 120000);
    WinHttpSetTimeouts(ses.h, t, t, t, t);
    con.h = WinHttpConnect(ses.h, host, uc.nPort, 0);
    if (!con.h) return fail("WinHttpConnect");
    const std::wstring target = std::wstring(*path ? path : L"/") + extra;
    req.h = WinHttpOpenRequest(con.h, widen(rq.method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                               uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) return fail("WinHttpOpenRequest");
    for (const auto& [k, v] : rq.headers) {
        if (k.find_first_of("\r\n:") != std::string::npos || v.find_first_of("\r\n") != std::string::npos) continue;   // no header smuggling
        const std::wstring line = widen(k + ": " + v + "\r\n");
        WinHttpAddRequestHeaders(req.h, line.c_str(), DWORD(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, rq.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(rq.body.data()),
                            DWORD(rq.body.size()), DWORD(rq.body.size()), 0))
        return fail("the request could not be sent");
    if (!WinHttpReceiveResponse(req.h, nullptr)) return fail("no answer");
    DWORD code = 0, sz = sizeof code;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
    rs.status = int(code);
    std::vector<char> buf(16384);
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(req.h, buf.data(), DWORD(buf.size()), &got)) return fail("the answer was cut short");
        if (got == 0) break;
        const std::size_t room = rq.max_body - std::min(rq.max_body, rs.body.size());
        rs.body.append(buf.data(), std::min<std::size_t>(got, room));
        if (got > room) { rs.truncated = true; break; }
    }
    rs.ok = true;
    return rs;
}

#elif defined(CLERIA_CURL)

bool http_supported() { return true; }

HttpResponse http_request(const HttpRequest& rq) {
    HttpResponse rs;
    if (rq.url.rfind("http://", 0) != 0 && rq.url.rfind("https://", 0) != 0) { rs.error = "not an http:// or https:// address"; return rs; }
    CURL* c = curl_easy_init();
    if (!c) { rs.error = "curl unavailable"; return rs; }
    struct Sink { HttpResponse* rs; std::size_t max; } sink{&rs, rq.max_body};
    curl_easy_setopt(c, CURLOPT_URL, rq.url.c_str());
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, rq.method.c_str());
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "CleriaCore");
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, long(std::clamp(rq.timeout_ms, 1000, 120000)));
    if (!rq.body.empty()) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, rq.body.data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(rq.body.size()));
    }
    curl_slist* hl = nullptr;
    for (const auto& [k, v] : rq.headers) {
        if (k.find_first_of("\r\n:") != std::string::npos || v.find_first_of("\r\n") != std::string::npos) continue;
        hl = curl_slist_append(hl, (k + ": " + v).c_str());
    }
    if (hl) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](char* p, std::size_t sz, std::size_t n, void* ud) -> std::size_t {
        Sink& s = *static_cast<Sink*>(ud);
        const std::size_t got = sz * n, room = s.max - std::min(s.max, s.rs->body.size());
        s.rs->body.append(p, std::min(got, room));
        if (got > room) { s.rs->truncated = true; return 0; }   // stops the transfer
        return got;
    });
    const CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    rs.status = int(code);
    rs.ok = r == CURLE_OK || rs.truncated;
    if (!rs.ok) rs.error = curl_easy_strerror(r);
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    return rs;
}

#else

bool http_supported() { return false; }
HttpResponse http_request(const HttpRequest&) {
    HttpResponse rs;
    rs.error = "HTTP is not supported by this build";
    return rs;
}

#endif

}  // namespace cleria::net
