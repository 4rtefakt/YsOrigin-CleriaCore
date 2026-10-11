// Test entry point, used only by probe_simtest.exe (src/simtest.cpp): it lets the
// offline simulation drive the same functions the panel's buttons call. The game
// never calls it.
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "game.h"
#include "probe.h"
#include "tests.h"

bool host_is_simtest();   // hooks.cpp
static TestFile g_sim_file;

extern "C" int __cdecl RetailProbeSim(int op, const char* arg, char* out, int out_len) {
    std::string msg;
    int r = 0;
    switch (op) {
    case 0:  // load the test list
        load_tests(arg, g_sim_file);
        r = g_sim_file.errors.empty() ? (int)g_sim_file.tests.size() : -(int)g_sim_file.errors.size();
        break;
    case 1: {  // press a test's button; "id|shadows=0" = press its config button
        std::string id = arg;
        std::vector<std::pair<std::string, int>> cfg;
        size_t bar = id.find('|');
        if (bar != std::string::npos) {
            std::string kv = id.substr(bar + 1);
            id = id.substr(0, bar);
            size_t eq = kv.find('=');
            cfg.push_back({kv.substr(0, eq), atoi(kv.c_str() + eq + 1)});
        }
        r = -1;
        msg = "no such test";
        for (const Test& t : g_sim_file.tests)
            if (t.id == id) r = probe::go(t, &cfg, msg) ? 1 : 0;
        break;
    }
    case 2:  // one frame
        probe::frame();
        r = probe::status().faulted ? -1 : 1;
        break;
    case 3:  // a helper button
        r = probe::run_helper(arg, msg) ? 1 : 0;
        break;
    case 4:  // may a room change be staged?
        if (const char* w = probe::why_not()) { msg = w; r = 0; } else r = 1;
        break;
    case 5:  // status bits: 1 supported, 2 dirty, 4 faulted, 8 busy
        r = (probe::status().supported ? 1 : 0) | (probe::status().dirty ? 2 : 0) | (probe::status().faulted ? 4 : 0) |
            (probe::status().busy ? 8 : 0);
        msg = probe::status().room;
        break;
    case 6:  // toggles: "char_switch=1", "allow_save=1"
        if (!strcmp(arg, "char_switch=1")) probe::g_allow_char_switch = true;
        else if (!strcmp(arg, "char_switch=0")) probe::g_allow_char_switch = false;
        else if (!strcmp(arg, "allow_save=1")) probe::g_allow_save = true;
        else if (!strcmp(arg, "allow_save=0")) probe::g_allow_save = false;
        r = 1;
        break;
    case 7:  // go to a room at its default spawn
        r = probe::go_room(arg, false, nullptr, msg) ? 1 : 0;
        break;
    case 100:  // simulation host only: where the fake exe image is, then install
        if (host_is_simtest()) {
            ga::g_delta = (uintptr_t)strtoul(arg, nullptr, 10);
            probe::install();
            r = 1;
        }
        break;
    case 8: r = probe::save_pos(msg) ? 1 : 0; break;
    case 9: r = probe::restore_pos(msg) ? 1 : 0; break;
    }
    if (out && out_len > 0) {
        strncpy(out, msg.c_str(), (size_t)out_len - 1);
        out[out_len - 1] = 0;
    }
    return r;
}
