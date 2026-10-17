// The WebSocket protocol itself (RFC 6455) and its permessage-deflate extension (RFC 7692), with no I/O: the
// upgrade request and the check of its answer, client frames, a decoder for the server's frames, and the
// inflater of compressed messages. websocket.cpp runs them over its own TCP / TLS stream on Windows, where
// WinHTTP's WebSocket refuses a server that agrees to the extension (ERROR_WINHTTP_INVALID_SERVER_RESPONSE,
// seen against Archipelago's MultiServer, which warns uncompressed clients that they "may stop working").
//
// This client offers the extension and inflates what the server compresses; it sends its own frames
// uncompressed, which RFC 7692 §6 allows per message, so only the server's parameters matter.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cleria::net::ws {

std::string sha1(std::string_view data);                 // the 20-byte digest
std::string base64(std::string_view data);
std::string accept_key(std::string_view client_key);     // base64(sha1(key + the RFC's GUID))

// The HTTP upgrade request for `host:port/path` with the client's base64 key, offering permessage-deflate.
std::string upgrade_request(const std::string& host, int port, bool secure, const std::string& path,
                            const std::string& key, const std::string& user_agent);

struct Handshake {
    bool complete = false;      // the header block has ended (the bytes after it are frames)
    std::size_t header_size = 0;
    bool ok = false;            // 101 with the right Sec-WebSocket-Accept and nothing it cannot do
    std::string error;          // why not, when complete and not ok
    bool deflate = false;       // the server agreed to permessage-deflate
    bool server_no_context_takeover = false;
};
// Reads the server's answer so far (call again with more bytes until `complete`).
Handshake parse_handshake(std::string_view data, const std::string& key);

enum Opcode : std::uint8_t { kCont = 0, kText = 1, kBinary = 2, kClose = 8, kPing = 9, kPong = 10 };

// A client frame (masked, FIN set, uncompressed). `mask` is the 4-byte masking key.
std::string frame(Opcode op, std::string_view payload, std::uint32_t mask);
std::string close_frame(std::uint16_t status, std::uint32_t mask);

// The server's frames, fed as they arrive: complete messages (fragments joined and inflated), and the control
// frames to answer.
class Decoder {
public:
    struct Out {
        enum class Kind { None, Message, Ping, Close, Error } kind = Kind::None;
        std::string data;               // Message: the text; Ping: its payload; Error: why
        std::uint16_t status = 0;       // Close: the status (1005 when none)
    };
    Decoder(bool deflate, bool no_context_takeover, std::size_t max_message);
    ~Decoder();
    void feed(std::string_view bytes);
    Out next();                         // Kind::None when more bytes are needed

private:
    struct Inflater;
    std::string buf_;
    std::size_t pos_ = 0;
    std::string msg_;
    bool in_msg_ = false, msg_compressed_ = false;
    bool deflate_, no_takeover_;
    std::size_t max_;
    std::unique_ptr<Inflater> inf_;
};

}  // namespace cleria::net::ws
