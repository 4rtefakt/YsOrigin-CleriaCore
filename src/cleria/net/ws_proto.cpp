// The WebSocket protocol and permessage-deflate, with no I/O — see ws_proto.hpp.
#include "cleria/net/ws_proto.hpp"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES   // its zlib names are macros (deflate, ...) that clash with ours
#include <miniz.h>

#include <cstring>

namespace cleria::net::ws {

// ---- SHA-1 and base64 (the handshake's accept key) ------------------------------------------

std::string sha1(std::string_view data) {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m(data);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    m.push_back(static_cast<char>(0x80));
    while (m.size() % 64 != 56) m.push_back('\0');
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<char>((bits >> (8 * i)) & 0xff));
    auto rol = [](std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (std::size_t off = 0; off < m.size(); off += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(m.data() + off + 4 * i);
            w[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
        }
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
    for (std::uint32_t v : h)
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    return out;
}

std::string base64(std::string_view data) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t(static_cast<unsigned char>(data[i])) << 16) |
                                (std::uint32_t(static_cast<unsigned char>(data[i + 1])) << 8) |
                                static_cast<unsigned char>(data[i + 2]);
        out += abc[v >> 18]; out += abc[(v >> 12) & 63]; out += abc[(v >> 6) & 63]; out += abc[v & 63];
    }
    if (i + 1 == data.size()) {
        const std::uint32_t v = std::uint32_t(static_cast<unsigned char>(data[i])) << 16;
        out += abc[v >> 18]; out += abc[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == data.size()) {
        const std::uint32_t v = (std::uint32_t(static_cast<unsigned char>(data[i])) << 16) |
                                (std::uint32_t(static_cast<unsigned char>(data[i + 1])) << 8);
        out += abc[v >> 18]; out += abc[(v >> 12) & 63]; out += abc[(v >> 6) & 63]; out += '=';
    }
    return out;
}

std::string accept_key(std::string_view client_key) {
    return base64(sha1(std::string(client_key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

// ---- the handshake ---------------------------------------------------------------------------

std::string upgrade_request(const std::string& host, int port, bool secure, const std::string& path,
                            const std::string& key, const std::string& user_agent) {
    std::string h = host.find(':') != std::string::npos ? "[" + host + "]" : host;
    if (port != (secure ? 443 : 80)) h += ":" + std::to_string(port);
    return "GET " + path + " HTTP/1.1\r\n"
           "Host: " + h + "\r\n"
           "User-Agent: " + user_agent + "\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: " + key + "\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
           "\r\n";
}

static std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}
static std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

Handshake parse_handshake(std::string_view data, const std::string& key) {
    Handshake r;
    const std::size_t end = data.find("\r\n\r\n");
    if (end == std::string_view::npos) {
        if (data.size() > 16384) { r.complete = true; r.error = "the server's answer has no end"; }
        return r;
    }
    r.complete = true;
    r.header_size = end + 4;
    std::string_view head = data.substr(0, end);
    const std::size_t eol = head.find("\r\n");
    const std::string_view status = head.substr(0, eol);
    if (status.size() < 12 || status.substr(0, 5) != "HTTP/" || status.substr(9, 3) != "101") {
        r.error = "HTTP " + std::string(status.size() >= 12 ? status.substr(9, 3) : status) + " instead of a WebSocket upgrade";
        return r;
    }
    bool upgrade = false, accept = false;
    std::string ext;
    std::size_t p = eol == std::string_view::npos ? head.size() : eol + 2;
    while (p < head.size()) {
        std::size_t q = head.find("\r\n", p);
        if (q == std::string_view::npos) q = head.size();
        const std::string_view line = head.substr(p, q - p);
        p = q + 2;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string name = lower(trim(line.substr(0, colon)));
        const std::string_view value = trim(line.substr(colon + 1));
        if (name == "upgrade") upgrade = lower(value) == "websocket";
        else if (name == "sec-websocket-accept") accept = value == accept_key(key);
        else if (name == "sec-websocket-extensions") ext += (ext.empty() ? "" : ",") + std::string(value);
    }
    if (!upgrade) { r.error = "the server did not upgrade to a WebSocket"; return r; }
    if (!accept) { r.error = "the server's Sec-WebSocket-Accept is wrong"; return r; }
    // Only permessage-deflate was offered: anything else, or the extension twice, is a protocol error.
    std::size_t s = 0;
    while (s < ext.size()) {
        std::size_t e = ext.find(',', s);
        if (e == std::string::npos) e = ext.size();
        const std::string one = std::string(trim(std::string_view(ext).substr(s, e - s)));
        s = e + 1;
        if (one.empty()) continue;
        std::size_t t = 0;
        bool first = true;
        while (t <= one.size()) {
            std::size_t u = one.find(';', t);
            if (u == std::string::npos) u = one.size();
            const std::string param = lower(trim(std::string_view(one).substr(t, u - t)));
            t = u + 1;
            if (first) {
                if (param != "permessage-deflate" || r.deflate) { r.error = "the server chose an extension not offered: " + one; return r; }
                r.deflate = true;
                first = false;
            } else if (param == "server_no_context_takeover") {
                r.server_no_context_takeover = true;
            } else if (param.rfind("server_max_window_bits", 0) == 0 || param.rfind("client_max_window_bits", 0) == 0 ||
                       param == "client_no_context_takeover" || param.empty()) {
                // window sizes up to 15 all inflate with a 32 KB window; the client never compresses
            } else {
                r.error = "unknown permessage-deflate parameter: " + param;
                return r;
            }
        }
    }
    r.ok = true;
    return r;
}

// ---- frames ----------------------------------------------------------------------------------

std::string frame(Opcode op, std::string_view payload, std::uint32_t mask) {
    std::string f;
    f.push_back(static_cast<char>(0x80 | op));
    const std::size_t n = payload.size();
    if (n < 126) {
        f.push_back(static_cast<char>(0x80 | n));
    } else if (n < 65536) {
        f.push_back(static_cast<char>(0x80 | 126));
        f.push_back(static_cast<char>(n >> 8));
        f.push_back(static_cast<char>(n & 0xff));
    } else {
        f.push_back(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i) f.push_back(static_cast<char>((static_cast<std::uint64_t>(n) >> (8 * i)) & 0xff));
    }
    const unsigned char m[4] = {static_cast<unsigned char>(mask >> 24), static_cast<unsigned char>(mask >> 16),
                                static_cast<unsigned char>(mask >> 8), static_cast<unsigned char>(mask)};
    f.append(reinterpret_cast<const char*>(m), 4);
    const std::size_t at = f.size();
    f.append(payload);
    for (std::size_t i = 0; i < n; ++i) f[at + i] = static_cast<char>(f[at + i] ^ m[i & 3]);
    return f;
}

std::string close_frame(std::uint16_t status, std::uint32_t mask) {
    const char p[2] = {static_cast<char>(status >> 8), static_cast<char>(status & 0xff)};
    return frame(kClose, std::string_view(p, 2), mask);
}

// ---- the decoder -----------------------------------------------------------------------------

struct Decoder::Inflater {
    mz_stream s{};
    bool ok = false;
    Inflater() { ok = mz_inflateInit2(&s, -MZ_DEFAULT_WINDOW_BITS) == MZ_OK; }
    ~Inflater() { if (ok) mz_inflateEnd(&s); }
    // A whole message: its deflate data then the 00 00 FF FF the sender removed (RFC 7692 §7.2.2).
    bool run(std::string in, std::string* out, std::size_t max) {
        static const char tail[4] = {0, 0, static_cast<char>(0xff), static_cast<char>(0xff)};
        in.append(tail, 4);
        s.next_in = reinterpret_cast<const unsigned char*>(in.data());
        s.avail_in = static_cast<unsigned int>(in.size());
        unsigned char buf[16384];
        while (true) {
            s.next_out = buf;
            s.avail_out = sizeof buf;
            const int r = mz_inflate(&s, MZ_SYNC_FLUSH);
            out->append(reinterpret_cast<const char*>(buf), sizeof buf - s.avail_out);
            if (out->size() > max) return false;
            if (r == MZ_STREAM_END) return s.avail_in == 0;
            if (r != MZ_OK && r != MZ_BUF_ERROR) return false;
            if (s.avail_in == 0 && s.avail_out != 0) return true;
            if (r == MZ_BUF_ERROR && s.avail_out != 0) return false;   // no progress with room to write
        }
    }
};

Decoder::Decoder(bool deflate, bool no_context_takeover, std::size_t max_message)
    : deflate_(deflate), no_takeover_(no_context_takeover), max_(max_message) {}
Decoder::~Decoder() = default;

void Decoder::feed(std::string_view bytes) {
    if (pos_ > 0 && pos_ == buf_.size()) { buf_.clear(); pos_ = 0; }
    else if (pos_ > (1u << 20)) { buf_.erase(0, pos_); pos_ = 0; }
    buf_.append(bytes);
}

Decoder::Out Decoder::next() {
    Out o;
    while (true) {
        const std::size_t avail = buf_.size() - pos_;
        if (avail < 2) return o;
        const auto* b = reinterpret_cast<const unsigned char*>(buf_.data() + pos_);
        const bool fin = b[0] & 0x80, rsv1 = b[0] & 0x40;
        const std::uint8_t op = b[0] & 0x0f;
        const bool masked = b[1] & 0x80;
        std::uint64_t len = b[1] & 0x7f;
        std::size_t hdr = 2;
        if (len == 126) {
            if (avail < 4) return o;
            len = (std::uint64_t(b[2]) << 8) | b[3];
            hdr = 4;
        } else if (len == 127) {
            if (avail < 10) return o;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | b[2 + i];
            hdr = 10;
        }
        if (masked) hdr += 4;
        auto error = [&](std::string why) { o.kind = Out::Kind::Error; o.data = std::move(why); pos_ = buf_.size(); return o; };
        if ((b[0] & 0x30) || (rsv1 && (!deflate_ || op >= 8 || op == kCont))) return error("a frame with reserved bits set");
        if (len > max_ + 64) return error("message larger than " + std::to_string(max_) + " bytes");
        if (avail < hdr + len) return o;
        std::string payload(buf_.data() + pos_ + hdr, static_cast<std::size_t>(len));
        if (masked)
            for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ b[hdr - 4 + (i & 3)]);
        pos_ += hdr + static_cast<std::size_t>(len);

        if (op >= 8) {                                  // control frames: whole, never fragmented
            if (!fin || len > 125) return error("a broken control frame");
            if (op == kPing) { o.kind = Out::Kind::Ping; o.data = std::move(payload); return o; }
            if (op == kClose) {
                o.kind = Out::Kind::Close;
                o.status = payload.size() >= 2 ? static_cast<std::uint16_t>((static_cast<unsigned char>(payload[0]) << 8) |
                                                                             static_cast<unsigned char>(payload[1]))
                                               : 1005;
                if (payload.size() > 2) o.data = payload.substr(2);
                return o;
            }
            continue;                                   // a pong (unasked) or an unknown control opcode
        }
        if (op == kCont) {
            if (!in_msg_) return error("a continuation with no message");
        } else if (op == kText || op == kBinary) {
            if (in_msg_) return error("a new message inside a fragmented one");
            in_msg_ = true;
            msg_compressed_ = rsv1;
            msg_.clear();
        } else {
            return error("unknown opcode " + std::to_string(op));
        }
        msg_ += payload;
        if (msg_.size() > max_) return error("message larger than " + std::to_string(max_) + " bytes");
        if (!fin) continue;
        in_msg_ = false;
        if (msg_compressed_) {
            if (!inf_) inf_ = std::make_unique<Inflater>();
            if (no_takeover_ && inf_->ok) mz_inflateReset(&inf_->s);
            std::string text;
            if (!inf_->ok || !inf_->run(std::move(msg_), &text, max_)) return error("a compressed message that does not inflate");
            msg_ = std::move(text);
        }
        o.kind = Out::Kind::Message;
        o.data = std::move(msg_);
        msg_.clear();
        return o;
    }
}

}  // namespace cleria::net::ws
