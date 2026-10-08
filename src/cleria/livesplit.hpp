// A LiveSplit Server client (build/shell_re/SPEEDRUN.md §5): CleriaCore's run mirrored into
// LiveSplit over its TCP server (Control > Start TCP Server; default port 16834). Plain sockets (winsock on
// Windows, BSD sockets on Linux / macOS), non-blocking, no new dependency. The protocol (LiveSplit README "The LiveSplit Server",
// src/LiveSplit.Core/Server/CommandServer.cs + Connection.cs): one command per line,
// `<command>[ <args>]\n` (a ReadLine, so "\r\n" works too); times as `[-][[[d.]hh:]mm:]ss[.fffffff]`.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cleria::speedrun {

// "1:02:03.456" — LiveSplit's TimeSpanParser accepts hours past 23 and unpadded fields.
std::string ls_time(double ms);
// The command lines CleriaCore sends (without the line end).
inline std::string ls_setgametime(double ms) { return "setgametime " + ls_time(ms); }

class LiveSplitClient {
  public:
    LiveSplitClient();
    ~LiveSplitClient();
    LiveSplitClient(const LiveSplitClient&) = delete;
    LiveSplitClient& operator=(const LiveSplitClient&) = delete;

    // Connect (and keep reconnecting) while enabled. A host / port change reconnects.
    void configure(bool enabled, const std::string& host, int port);
    // Drive the connection: finish a pending connect, notice a close, retry every kRetryMs.
    void poll(double now_ms);
    bool connected() const { return state_ == State::Connected; }
    // Queue one command; sent at once when connected (dropped otherwise: LiveSplit only cares about
    // the current state). Returns false when not connected.
    bool send(std::string_view cmd);
    std::string status() const;
    // Every command line sent so far (tests; capped at kLogMax).
    const std::vector<std::string>& log() const { return log_; }

    static constexpr double kRetryMs = 2000.0;   // [H]
    static constexpr std::size_t kLogMax = 256;

  private:
    enum class State { Off, Idle, Connecting, Connected };
    void close_socket();
    void begin_connect(double now_ms);
    State state_ = State::Off;
    std::uintptr_t sock_;                         // SOCKET (INVALID_SOCKET when none)
    bool wsa_ = false;
    std::string host_ = "127.0.0.1";
    int port_ = 16834;
    double next_try_ = 0;
    std::string last_error_;
    std::vector<std::string> log_;
};

}  // namespace cleria::speedrun
