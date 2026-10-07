// [cleria.archipelago] — see ap_settings.hpp.
#include "cleria/ap_settings.hpp"

#include "cleria/settings.hpp"

namespace cleria::apcfg {

namespace {
constexpr const char* kSec = "cleria.archipelago";
}

void read(const Ini& ini, Settings& s) {
    auto rb = [&](const char* key, bool& v) { if (auto x = ini.get_int(kSec, key)) v = *x != 0; };
    if (auto v = ini.get_string(kSec, "Server")) s.server = *v;
    if (auto v = ini.get_string(kSec, "Slot")) s.slot = *v;
    if (auto v = ini.get_string(kSec, "Password")) s.password = *v;
    if (auto v = ini.get_string(kSec, "Uuid")) s.uuid = *v;
    rb("DeathLink", s.death_link);
    rb("AutoConnect", s.auto_connect);
    rb("ShowStatus", s.show_status);
    rb("ShowFeed", s.show_feed);
    rb("ShowTracker", s.show_tracker);
    rb("ExportState", s.export_state);
}

void write(Ini& ini, const Settings& s) {
    ini.set_string(kSec, "Server", s.server);
    ini.set_string(kSec, "Slot", s.slot);
    ini.set_string(kSec, "Password", s.password);
    ini.set_string(kSec, "Uuid", s.uuid);
    ini.set_int(kSec, "DeathLink", s.death_link);
    ini.set_int(kSec, "AutoConnect", s.auto_connect);
    ini.set_int(kSec, "ShowStatus", s.show_status);
    ini.set_int(kSec, "ShowFeed", s.show_feed);
    ini.set_int(kSec, "ShowTracker", s.show_tracker);
    ini.set_int(kSec, "ExportState", s.export_state);
}

}  // namespace cleria::apcfg
