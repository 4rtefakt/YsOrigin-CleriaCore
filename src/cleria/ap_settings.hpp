#pragma once
// cleria.ini [cleria.archipelago] (build/shell_re/AP_GAME.md §2): the Archipelago client's connection
// and overlay switches. CleriaCore's own layer, not retail: everything is off / empty by default, and
// with no slot the game never opens a socket. The password is stored like the retail mod's
// yso_ap.cfg stores it (plain text in the user's own settings folder) and is never printed or logged.
#include <string>

namespace cleria {
class Ini;

namespace apcfg {

struct Settings {
    std::string server = "archipelago.gg:38281";   // Server: host:port, ws:// / wss:// optional [H]
    std::string slot;                              // Slot: the player (slot) name
    std::string password;                          // Password (never logged)
    std::string uuid;                              // Uuid: made once (ap::make_uuid) and kept
    bool death_link = false;                       // DeathLink
    bool auto_connect = false;                     // AutoConnect: connect when the game starts
    bool show_status = true;                       // ShowStatus: the connection line overlay
    bool show_feed = true;                         // ShowFeed: the item / chat feed overlay
    bool show_tracker = false;                     // ShowTracker: the per-area checks overlay
    bool export_state = true;                      // ExportState: archipelago/state.json for trackers
};

void read(const Ini& ini, Settings& s);
void write(Ini& ini, const Settings& s);

}  // namespace apcfg
}  // namespace cleria
