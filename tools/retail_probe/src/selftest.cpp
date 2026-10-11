// Offline check of the test list, with the DLL's own parser:
//   probe_selftest <retail_probe_tests.ini> [<game>\release\... or assets\MAP\SCENELIST.SL]
// Reports parse problems, duplicate ids, and rooms that are not in the scene list.
// Exit code 0 = clean.
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "tests.h"

static std::set<std::string> scene_rooms(const char* path) {
    std::set<std::string> out;
    FILE* f = fopen(path, "rb");
    if (!f) return out;
    std::vector<unsigned char> b;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    if (b.size() < 8) return out;
    unsigned count = b[4] | b[5] << 8 | b[6] << 16 | b[7] << 24;
    for (unsigned i = 0; i < count && 8 + (size_t)(i + 1) * 0x1c8 <= b.size(); i++) {
        std::string dir((const char*)&b[8 + (size_t)i * 0x1c8 + 4]);
        while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) dir.pop_back();
        size_t s = dir.find_last_of("\\/");
        std::string leaf = s == std::string::npos ? dir : dir.substr(s + 1);
        for (char& c : leaf) c = (char)toupper((unsigned char)c);
        if (!leaf.empty()) out.insert(leaf);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: probe_selftest <tests.ini> [SCENELIST.SL]\n");
        return 2;
    }
    TestFile tf;
    int bad = 0;
    if (!load_tests(argv[1], tf)) bad++;
    for (const std::string& e : tf.errors) { printf("PARSE  %s\n", e.c_str()); bad++; }
    std::set<std::string> rooms;
    if (argc > 2) {
        rooms = scene_rooms(argv[2]);
        printf("scene list: %d rooms\n", (int)rooms.size());
        if (rooms.empty()) bad++;
    }
    std::set<std::string> ids;
    for (const Test& t : tf.tests) {
        if (!ids.insert(t.id).second) { printf("DUP    %s\n", t.id.c_str()); bad++; }
        std::string room = t.room;
        for (char& c : room) c = (char)toupper((unsigned char)c);
        if (argc > 2 && !room.empty() && !rooms.count(room)) { printf("ROOM   %s: %s is not in the scene list\n", t.id.c_str(), t.room.c_str()); bad++; }
        printf("%-28s %-7s %-9s %-7s lv%-2d tier%-2d flags%d items%d cfgbtn%d helpers%d answers%d  %s\n", t.id.c_str(),
               char_name(t.character), diff_name(t.difficulty), t.room.empty() ? "-" : t.room.c_str(), t.level, t.weapon_tier,
               (int)t.flags.size(), (int)t.items.size(), (int)t.cfg_buttons.size(), (int)t.helpers.size(), (int)t.answers.size(),
               t.group.c_str());
    }
    printf("%d tests, toggle key 0x%X, %d problem(s)\n", (int)tf.tests.size(), tf.toggle_vk, bad);
    return bad ? 1 : 0;
}
