#include "cleria/mod_events.hpp"

#include <algorithm>

namespace cleria::events {

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::RoomEnter: return "room_enter";
        case Kind::FlagChanged: return "flag";
        case Kind::ChestOpened: return "chest";
        case Kind::ItemObtained: return "item";
        case Kind::PlayerDeath: return "death";
        case Kind::NewGame: return "new_game";
        case Kind::Save: return "save";
        case Kind::Load: return "load";
        case Kind::BossDefeated: return "boss_defeated";
        case Kind::ItemUsed: return "item_used";
        case Kind::DoorOpened: return "door_opened";
    }
    return "?";
}

int Bus::subscribe(Handler h) {
    subs_.push_back({next_, true, Kind::RoomEnter, std::move(h)});
    return next_++;
}

int Bus::subscribe(Kind k, Handler h) {
    subs_.push_back({next_, false, k, std::move(h)});
    return next_++;
}

void Bus::unsubscribe(int id) {
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(), [id](const Sub& s) { return s.id == id; }), subs_.end());
}

void Bus::clear() { subs_.clear(); }

void Bus::emit(const Event& e) {
    if (depth_ >= kMaxDepth) { ++dropped; return; }
    ++emitted;
    ++depth_;
    // A copy: a handler may subscribe / unsubscribe while we iterate.
    const std::vector<Sub> subs = subs_;
    for (const Sub& s : subs)
        if (s.all || s.kind == e.kind) s.h(e);
    --depth_;
}

Bus& bus() {
    static Bus b;
    return b;
}

void FlagWatch::prime(const std::int32_t* flags) {
    std::copy(flags, flags + 0x200, prev.begin());
    primed = true;
}

int FlagWatch::diff(const std::int32_t* flags, Bus& b, const std::string& room) {
    if (!primed) { prime(flags); return 0; }
    int n = 0;
    for (int i = 0; i < 0x200; ++i) {
        if (flags[i] == prev[std::size_t(i)]) continue;
        Event e;
        e.kind = Kind::FlagChanged;
        e.room = room;
        e.index = i;
        e.old_value = prev[std::size_t(i)];
        e.new_value = flags[i];
        prev[std::size_t(i)] = flags[i];
        b.emit(e);
        ++n;
    }
    return n;
}

}  // namespace cleria::events
