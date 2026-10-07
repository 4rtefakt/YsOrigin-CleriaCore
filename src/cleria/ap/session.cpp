// Archipelago session — see session.hpp.
#include "cleria/ap/session.hpp"

#include <algorithm>
#include <cmath>

namespace cleria::ap {

std::string describe_refusal(const std::string& e) {
    if (e == "InvalidSlot") return "no slot with that name in this room";
    if (e == "InvalidGame") return "that slot is not a Ys Origin slot";
    if (e == "IncompatibleVersion") return "the server wants a newer client version";
    if (e == "InvalidPassword") return "wrong or missing password";
    if (e == "InvalidItemsHandling") return "the server refused the item settings";
    if (e == "SeedMismatch") return "this room runs a different seed than the save";
    return e;
}

void Session::connect(std::string address, Config cfg, Options opt) {
    ws_.close();
    opt_ = std::move(opt);
    address_ = std::move(address);
    client_ = Client(std::move(cfg), store_);
    events_.clear();
    reason_.clear();
    delay_ = opt_.first_retry_s;
    retry_at_ = -1;
    want_open_ = true;
    open_socket(now_);
}

void Session::open_socket(double now_s) {
    retry_at_ = -1;
    status_ = Status::Connecting;
    last_send_ = now_s;
    ws_.open(address_, opt_.ws);
}

void Session::disconnect() {
    want_open_ = false;
    retry_at_ = -1;
    ws_.close();
    status_ = Status::Idle;
    reason_.clear();
}

void Session::retry_login(Config cfg) {
    client_.retry_connect(std::move(cfg));
    if (client_.state() == Client::State::AwaitConnected) status_ = Status::LoggingIn;
}

std::vector<Event> Session::take_events() {
    std::vector<Event> e;
    e.swap(events_);
    return e;
}

void Session::update(double now_s) {
    now_ = now_s;
    for (WebSocket::Event& e : ws_.poll()) {
        switch (e.kind) {
        case WebSocket::Event::Kind::Opened:
            status_ = Status::LoggingIn;
            client_.on_open();
            break;
        case WebSocket::Event::Kind::Message:
            client_.on_message(e.text);
            break;
        case WebSocket::Event::Kind::Closed: {
            const bool refused = client_.state() == Client::State::Refused;
            client_.on_close();
            if (!refused) reason_ = e.text;     // a refusal keeps its own explanation
            if (!want_open_) { status_ = Status::Idle; break; }
            status_ = refused ? Status::Refused : Status::Disconnected;
            const bool bad_address = candidate_urls(address_).empty();
            if (opt_.auto_reconnect && !refused && !bad_address) {
                retry_at_ = now_s + delay_;
                delay_ = std::min(delay_ * 2, opt_.max_retry_s);
            }
            break;
        }
        }
    }

    for (Event& e : client_.take_events()) {
        if (e.kind == Event::Kind::Connected) {
            status_ = Status::Connected;
            delay_ = opt_.first_retry_s;
        } else if (e.kind == Event::Kind::Refused || e.kind == Event::Kind::SeedMismatch) {
            status_ = Status::Refused;
            reason_.clear();
            for (const std::string& r : e.errors) reason_ += (reason_.empty() ? "" : ", ") + describe_refusal(r);
        }
        events_.push_back(std::move(e));
    }

    if (status_ == Status::Connected && opt_.keepalive_s > 0 && now_s - last_send_ >= opt_.keepalive_s)
        client_.keep_alive();
    for (std::string& f : client_.take_outgoing()) {
        if (ws_.send(std::move(f))) last_send_ = now_s;
    }

    if (want_open_ && retry_at_ >= 0 && now_s >= retry_at_ &&
        (ws_.state() == WebSocket::State::Closed || ws_.state() == WebSocket::State::Idle))
        open_socket(now_s);
}

std::string Session::status_text(double now_s) const {
    switch (status_) {
    case Status::Idle: return "not connected";
    case Status::Connecting: return "connecting to " + ws_.url();
    case Status::LoggingIn:
        return client_.state() == Client::State::AwaitDataPackage ? "downloading item names from " + ws_.url()
                                                                  : "logging in to " + ws_.url();
    case Status::Connected:
        return "connected as " + client_.player_name(client_.slot()) + " (slot " + std::to_string(client_.slot()) + ")";
    case Status::Refused: return "auth failed: " + reason_;
    case Status::Disconnected: {
        std::string s = "disconnected: " + reason_;
        if (retry_at_ >= 0) {
            const int secs = std::max(0, static_cast<int>(std::ceil(retry_at_ - now_s)));
            s += " (retrying in " + std::to_string(secs) + " s)";
        }
        return s;
    }
    }
    return {};
}

}  // namespace cleria::ap
