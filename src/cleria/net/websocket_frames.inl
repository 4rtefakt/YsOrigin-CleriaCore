// The WebSocket protocol over a byte stream we carry ourselves (RFC 6455 framing, RFC 7692 permessage-deflate)
// — see websocket.hpp. Textual part of websocket.cpp (#included inside namespace cleria::net); not standalone.
//
// Both own transports use it: winsock + SChannel on Windows (websocket_tls_win.inl), libcurl's raw connection
// elsewhere (websocket_curl.inl). We offer permessage-deflate and inflate the server's compressed messages; ours
// go out uncompressed, which the extension allows message by message.

namespace {

// A connected byte stream (TCP, TLS over TCP, or libcurl's connection).
struct Stream {
    virtual ~Stream() = default;
    // Sends all of it; false (and err) on failure.
    virtual bool write(const char* p, std::size_t n) = 0;
    // Waits at most wait_ms: > 0 bytes read, 0 nothing yet, -1 closed or failed (err).
    virtual long read(char* p, std::size_t n, int wait_ms) = 0;
    std::string err;
};

std::string sha1(const std::string& msg) {
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

std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const std::size_t n = std::min<std::size_t>(3, in.size() - i);
        std::uint32_t v = std::uint32_t(std::uint8_t(in[i])) << 16;
        if (n > 1) v |= std::uint32_t(std::uint8_t(in[i + 1])) << 8;
        if (n > 2) v |= std::uint8_t(in[i + 2]);
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += n > 1 ? t[(v >> 6) & 63] : '=';
        out += n > 2 ? t[v & 63] : '=';
    }
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
    return s.substr(b);
}

// The HTTP upgrade over a connected stream. "" when open (`deflate` set when the server took permessage-deflate,
// `rest` holding what came after the response), else why not.
template <class ConnT>
std::string ws_upgrade(ConnT& c, Stream& s, const WsUrl& u, const WsOptions& o, bool& deflate, std::string& rest) {
    std::random_device rd;
    std::string nonce;
    for (int i = 0; i < 16; ++i) nonce += static_cast<char>(rd() & 0xFF);
    const std::string key = base64(nonce);
    const std::string host = u.host.find(':') != std::string::npos ? "[" + u.host + "]" : u.host;
    const std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(u.port) +
                            "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                            "\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Extensions: permessage-deflate\r\nUser-Agent: " +
                            o.user_agent + "\r\n\r\n";
    if (!s.write(req.data(), req.size())) return s.err;

    std::string resp;
    char buf[4096];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(o.handshake_timeout_ms);
    std::size_t end;
    while ((end = resp.find("\r\n\r\n")) == std::string::npos) {
        if (c.stop || c.aborted) return "cancelled";
        if (std::chrono::steady_clock::now() > deadline) return "timed out";
        if (resp.size() > 65536) return "invalid server response";
        const long n = s.read(buf, sizeof buf, 50);
        if (n < 0) return s.err;
        resp.append(buf, static_cast<std::size_t>(n));
    }
    rest = resp.substr(end + 4);
    resp.resize(end);

    int status = 0;
    std::string upgrade, accept, ext;
    std::size_t pos = 0;
    for (bool first = true; pos <= resp.size(); first = false) {
        std::size_t eol = resp.find("\r\n", pos);
        if (eol == std::string::npos) eol = resp.size();
        const std::string line = resp.substr(pos, eol - pos);
        pos = eol + 2;
        if (first) {
            const std::size_t sp = line.find(' ');
            if (line.compare(0, 5, "HTTP/") != 0 || sp == std::string::npos) return "invalid server response";
            status = std::atoi(line.c_str() + sp + 1);
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower(line.substr(0, colon)), value = trim(line.substr(colon + 1));
        if (name == "upgrade") upgrade = lower(value);
        else if (name == "sec-websocket-accept") accept = value;
        else if (name == "sec-websocket-extensions") ext += (ext.empty() ? "" : ", ") + lower(value);
    }
    if (status != 101) return "HTTP " + std::to_string(status) + " instead of a WebSocket upgrade";
    if (upgrade != "websocket" || accept != base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")))
        return "invalid server response (bad WebSocket handshake)";
    // permessage-deflate is the only extension we offer, and only once
    deflate = !ext.empty();
    if (deflate && (ext.compare(0, 18, "permessage-deflate") != 0 || ext.find(',') != std::string::npos))
        return "invalid server response (unexpected extension \"" + ext + "\")";
    return {};
}

// Inflates one compressed message in place. The inflater lives as long as the connection, so a server that keeps
// its context between messages (no server_no_context_takeover) is understood too.
bool inflate_message(mz_stream& z, std::string& msg, std::size_t max) {
    msg.append("\x00\x00\xff\xff", 4);       // RFC 7692 §7.2.2: the tail the sender took off
    std::string out;
    std::vector<unsigned char> tmp(64 * 1024);
    z.next_in = reinterpret_cast<const unsigned char*>(msg.data());
    z.avail_in = static_cast<unsigned int>(msg.size());
    while (true) {
        z.next_out = tmp.data();
        z.avail_out = static_cast<unsigned int>(tmp.size());
        const int st = mz_inflate(&z, MZ_SYNC_FLUSH);
        out.append(reinterpret_cast<const char*>(tmp.data()), tmp.size() - z.avail_out);
        if (out.size() > max) return false;
        if (st == MZ_STREAM_END) { mz_inflateReset(&z); break; }
        if (st == MZ_BUF_ERROR || (st == MZ_OK && z.avail_in == 0 && z.avail_out != 0)) break;
        if (st != MZ_OK) return false;
    }
    msg.swap(out);
    return true;
}

// The open connection: sends the queue (then the close frame when asked), answers pings and the server's close,
// delivers whole messages. Returns the close reason. `in` holds bytes already read past the handshake.
template <class ConnT, class Deliver>
std::string ws_session(ConnT& c, Stream& s, bool deflate, std::string in, std::size_t max_message, Deliver deliver) {
    std::mt19937 rng(std::random_device{}());
    mz_stream z{};
    if (deflate) mz_inflateInit2(&z, -MZ_DEFAULT_WINDOW_BITS);
    struct ZEnd {
        mz_stream& z;
        bool on;
        ~ZEnd() { if (on) mz_inflateEnd(&z); }
    } zend{z, deflate};

    const auto send_frame = [&](int op, const char* p, std::size_t n) {
        std::string f;
        f += static_cast<char>(0x80 | op);
        if (n < 126) f += static_cast<char>(0x80 | n);
        else if (n < 65536) { f += static_cast<char>(0x80 | 126); f += static_cast<char>(n >> 8); f += static_cast<char>(n & 0xFF); }
        else { f += static_cast<char>(0x80 | 127); for (int i = 7; i >= 0; --i) f += static_cast<char>((std::uint64_t(n) >> (i * 8)) & 0xFF); }
        const std::uint32_t m = rng();
        const char mask[4] = {char(m), char(m >> 8), char(m >> 16), char(m >> 24)};
        f.append(mask, 4);
        const std::size_t h = f.size();
        f.append(p, n);
        for (std::size_t i = 0; i < n; ++i) f[h + i] ^= mask[i & 3];
        return s.write(f.data(), f.size());
    };

    std::string msg;
    bool in_msg = false, msg_z = false, close_sent = false;
    std::vector<char> buf(64 * 1024);
    while (true) {
        if (c.aborted) return c.stop ? "closed" : "connection lost: aborted";
        std::string out;
        bool have = false, do_close = false;
        {
            std::lock_guard<std::mutex> l(c.qmu);
            if (!c.outq.empty()) { out = std::move(c.outq.front()); c.outq.pop_front(); have = true; }
            else if (c.want_close && !close_sent) { do_close = true; c.writer_stop = true; }
        }
        if (have) {
            if (!send_frame(1, out.data(), out.size())) {
                std::lock_guard<std::mutex> l(c.qmu);
                c.send_error = "send failed: " + s.err;
                c.writer_stop = true;
                return c.stop ? "closed" : c.send_error;
            }
            continue;
        }
        if (do_close) {
            const char st[2] = {'\x03', '\xE8'};   // 1000 normal closure
            send_frame(8, st, 2);
            close_sent = true;
        }
        const long n = s.read(buf.data(), buf.size(), 10);   // [H] 10 ms slices: a send waits at most this long
        if (n < 0) return c.stop ? "closed" : "connection lost: " + s.err;
        in.append(buf.data(), static_cast<std::size_t>(n));

        std::size_t at = 0;
        while (in.size() - at >= 2) {
            const std::uint8_t b0 = std::uint8_t(in[at]), b1 = std::uint8_t(in[at + 1]);
            const int op = b0 & 0x0F;
            const bool fin = (b0 & 0x80) != 0, rsv1 = (b0 & 0x40) != 0, masked = (b1 & 0x80) != 0;
            std::uint64_t len = b1 & 0x7F;
            std::size_t hdr = 2;
            if (len == 126) {
                if (in.size() - at < 4) break;
                len = (std::uint64_t(std::uint8_t(in[at + 2])) << 8) | std::uint8_t(in[at + 3]);
                hdr = 4;
            } else if (len == 127) {
                if (in.size() - at < 10) break;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | std::uint8_t(in[at + 2 + i]);
                hdr = 10;
            }
            if (len > max_message) return "message larger than " + std::to_string(max_message) + " bytes";
            const std::size_t mlen = masked ? 4 : 0;
            if (in.size() - at < hdr + mlen + len) break;
            std::string payload = in.substr(at + hdr + mlen, static_cast<std::size_t>(len));
            if (masked)
                for (std::size_t i = 0; i < payload.size(); ++i) payload[i] ^= in[at + hdr + (i & 3)];
            at += hdr + mlen + static_cast<std::size_t>(len);
            if ((b0 & 0x30) || (rsv1 && (!deflate || op == 0 || op >= 8)))
                return "connection lost: protocol error (reserved bits)";

            if (op == 8) {
                if (!close_sent) send_frame(8, payload.data(), std::min<std::size_t>(payload.size(), 2));
                if (c.stop) return "closed";
                const int st = payload.size() >= 2 ? (std::uint8_t(payload[0]) << 8) | std::uint8_t(payload[1]) : 0;
                return "server closed the connection (" + std::to_string(st) +
                       (payload.size() > 2 ? ", " + payload.substr(2) : std::string()) + ")";
            }
            if (op == 9) { send_frame(10, payload.data(), payload.size()); continue; }
            if (op == 10) continue;
            if (op == 1 || op == 2) {
                if (in_msg) return "connection lost: protocol error (unfinished message)";
                in_msg = true;
                msg_z = rsv1;
                msg = std::move(payload);
            } else if (op == 0) {
                if (!in_msg) return "connection lost: protocol error (stray continuation)";
                msg += payload;
            } else {
                return "connection lost: protocol error (opcode " + std::to_string(op) + ")";
            }
            if (msg.size() > max_message) return "message larger than " + std::to_string(max_message) + " bytes";
            if (!fin) continue;
            in_msg = false;
            if (msg_z && !inflate_message(z, msg, max_message))
                return "connection lost: a compressed message could not be inflated";
            deliver(std::move(msg));
            msg.clear();
        }
        in.erase(0, at);
    }
}

}  // namespace
