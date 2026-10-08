// cleria-speedruntest — Speedrun mode's logic (src/cleria/speedrun, livesplit) against
// build/shell_re/SPEEDRUN.md: the time formats, the split conditions, the observer's events, the
// run on a fake clock (splits, golds, PB, undo / skip, comparisons), the splits file, the session's
// LiveSplit command stream, the autosplitter block, and the LiveSplit client over a loopback socket.
// No assets needed (the argument is accepted and ignored, like the other test apps).
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "cleria/livesplit.hpp"
#include "cleria/speedrun.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
static void nap_ms(int ms) { Sleep(DWORD(ms)); }
#else   // BSD sockets under the winsock names the loopback check uses
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SOCKET = int;
static int closesocket(SOCKET s) { return ::close(s); }
static void nap_ms(int ms) { usleep(useconds_t(ms) * 1000); }
#endif

using namespace cleria::speedrun;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}

// A game state to feed the observer.
struct Fake {
    std::string room = "S_1004";
    bool gauge = false;
    float hp = 0;
    bool alive = true;
    std::array<std::int32_t, 0x200> flags{};
    GameView view() const {
        GameView v;
        v.room = room; v.gauge_visible = gauge; v.gauge_hp = hp; v.flags = flags.data(); v.player_alive = alive;
        return v;
    }
};

static std::vector<Event> step(Observer& o, const Fake& f) {
    std::vector<Event> ev;
    o.step(f.view(), ev);
    return ev;
}

int main(int, char**) {
    // ---- formats ----
    check(format_time(0) == "0:00.00" && format_time(3456) == "0:03.45" && format_time(123450) == "2:03.45" &&
          format_time(3723450) == "1:02:03.45", "format_time: M:SS.cc, H:MM:SS.cc past an hour");
    check(format_play_time(0) == "0:00:00:000" && format_play_time(3723456) == "1:02:03:456",
          "format_play_time: retail \"%d:%02d:%02d:%03d\"");
    check(ticks_to_ms(60) == 1000 && ticks_to_ms(1) == 16 && ticks_to_ms(7) == 116,
          "ticks_to_ms: steps * 1000 / 60, integer (FUN_0043a340)");
    check(format_delta(-3200) == "-3.2" && format_delta(1250) == "+1.2" && format_delta(62000) == "+1:02" &&
          format_delta(-3723000) == "-1:02:03", "format_delta");
    check(ls_time(0) == "0:00:00.000" && ls_time(3723456) == "1:02:03.456" && ls_time(90061001) == "25:01:01.001",
          "ls_time: LiveSplit's [[hh:]mm:]ss.fff (hours past 23)");
    check(ls_setgametime(16) == "setgametime 0:00:00.016", "setgametime command line");

    // ---- split conditions ----
    {
        Event r; r.kind = Event::Room; r.from = "S_1004"; r.to = "S_1001";
        check(matches("enter:S_1001", r) && matches("enter:s_1001", r) && !matches("enter:S_1005", r),
              "enter: matches the room entered, case-blind");
        check(matches("leave:S_1004", r) && matches("room:S_1004>S_1001", r) && matches("room:*>S_1001", r) &&
              !matches("room:S_1001>S_1004", r), "leave: / room:A>B / * wildcard");
        Event b; b.kind = Event::Boss; b.room = "S_1099"; b.count = 2;
        check(matches("boss:S_1099", b) && matches("boss:*", b) && matches("boss:S_1099#2", b) &&
              !matches("boss:S_1099#1", b) && !matches("enter:S_1099", b), "boss:ROOM[#n]");
        Event f; f.kind = Event::Flag; f.flag = 58; f.old_v = -1; f.new_v = 1;
        check(matches("item:58", f) && !matches("item:57", f) && matches("flag:58=1", f), "item: / flag:");
        f.old_v = 1; f.new_v = 2;
        check(!matches("item:58", f) && !matches("flag:58=1", f), "item: only the first get; flag: only a change");
        check(!matches("manual", r) && valid_spec("manual") && !valid_spec("bogus:1") && !valid_spec("item:x") &&
              !valid_spec("room:S_1004") && !valid_spec("boss:S_1#0"), "manual never matches; bad specs rejected");
        bool all = true;
        for (int c = 1; c <= 3; ++c)
            for (const Segment& s : default_route(c, false).segs) all &= valid_spec(s.event);
        check(all && default_route(3, false).segs.back().event == "boss:S_7099" &&
              default_route(1, true).segs.back().event == "boss:S_7080" &&
              default_route(1, true).category == "Any% (Yunica EX)", "the default routes: valid, Darm / Dalles end");
    }

    // ---- the observer ----
    {
        Observer o;
        Fake g;
        check(step(o, g).empty(), "first view: no events");
        g.room = "S_1001";
        auto ev = step(o, g);
        check(ev.size() == 1 && ev[0].kind == Event::Room && ev[0].from == "S_1004" && ev[0].to == "S_1001",
              "room change -> Room event");
        g.gauge = true; g.hp = 300;
        check(step(o, g).empty(), "gauge up: nothing");
        g.hp = 0;
        ev = step(o, g);
        check(ev.size() == 1 && ev[0].kind == Event::Boss && ev[0].room == "S_1001" && ev[0].count == 1,
              "gauge HP to 0 -> Boss #1");
        g.hp = 500; step(o, g);                            // a second phase refills it
        g.gauge = false;
        ev = step(o, g);
        check(ev.size() == 1 && ev[0].kind == Event::Boss && ev[0].count == 2, "gauge withdrawn alive -> Boss #2");
        // a death: the retry's withdrawal is no defeat
        g.gauge = true; g.hp = 400; step(o, g);
        g.alive = false; step(o, g);
        g.alive = true; g.gauge = false;
        check(step(o, g).empty(), "withdrawn after a player death: no Boss");
        g.gauge = true; g.hp = 400; step(o, g);
        g.hp = 0;
        check(step(o, g).size() == 1, "the next real depletion counts again");
        // a lost duel: 133 set, 156 cleared, gauge withdrawn
        g.flags[133] = 1; g.flags[156] = 1; g.gauge = true; g.hp = 200; step(o, g);
        g.flags[156] = 0; g.gauge = false;
        ev = step(o, g);
        bool boss = false;
        for (const Event& e : ev) boss |= e.kind == Event::Boss;
        check(!boss, "a lost duel (133 set, 156 -> 0) is no Boss");
        // leaving the room with the gauge up: no Boss
        g.gauge = true; g.hp = 200; step(o, g);
        g.room = "S_1002";
        ev = step(o, g);
        check(ev.size() == 1 && ev[0].kind == Event::Room, "leaving with the gauge up: Room only");
        // flags
        g.flags[58] = -1; step(o, g);
        g.flags[58] = 1;
        ev = step(o, g);
        check(ev.size() == 1 && ev[0].kind == Event::Flag && ev[0].flag == 58 && ev[0].old_v == -1 && ev[0].new_v == 1 &&
              matches("item:58", ev[0]), "a flag change -> Flag event (item:58)");
        for (int i = 300; i < 340; ++i) g.flags[std::size_t(i)] = 7;
        check(step(o, g).empty(), "a bulk change (a load / new game) reports nothing");
    }

    // ---- the run on a fake clock ----
    {
        Run r;
        r.route.category = "Test";
        for (const char* n : {"A", "B", "C"}) { Segment s; s.name = n; s.event = "manual"; r.route.segs.push_back(s); }
        r.start(1000.0);
        check(r.running() && r.index() == 0 && r.route.attempts == 1 && r.rta(1500.0) == 500.0, "start: RTA from now");
        for (int i = 0; i < 600; ++i) r.tick();
        check(r.igt_ticks() == 600 && r.igt() == 10000.0, "IGT: 600 steps = 10 s");
        r.split(13000.0);                                   // A at 12 s RTA, 10 s IGT
        for (int i = 0; i < 300; ++i) r.tick();
        r.split(19000.0);                                   // B at 18 s
        check(r.index() == 2 && r.times()[0].rta == 12000.0 && r.times()[1].igt == 15000.0 && r.times()[1].gold,
              "splits record RTA and IGT; a first run's segments are golds");
        const bool fin = r.split(25000.0);                  // C at 24 s
        check(fin && r.phase() == Run::Phase::Finished && r.new_pb() && r.route.segs[2].pb_rta == 24000.0 &&
              r.route.completed == 1 && r.rta(99999.0) == 24000.0, "the last split finishes; the first finish is the PB");
        check(r.delta(2, Compare::PersonalBest) == Run::kNone, "no delta against the PB the run itself just set");
        check(r.undo() && r.running() && r.route.segs[2].pb_rta < 0 && r.route.completed == 0 && r.index() == 2,
              "undo after the finish takes the PB back");
        r.split(26000.0);
        check(r.new_pb() && r.route.segs[2].pb_rta == 25000.0, "re-finish");
        r.reset();
        // run 2: faster A, slower B, slower total
        r.start(0.0);
        r.split(10000.0);
        check(r.delta(0, Compare::PersonalBest) == -2000.0 && r.times()[0].gold && r.route.segs[0].gold_rta == 10000.0,
              "delta vs PB; a faster segment is a gold");
        r.split(20000.0);                                   // B segment 10 s (gold was 6 s)
        check(!r.times()[1].gold && r.route.segs[1].gold_rta == 6000.0, "a slower segment keeps the gold");
        check(r.comparison(1, Compare::BestSegments) == 12000.0 + 6000.0 && r.sum_of_best() == 10000.0 + 6000.0 + 7000.0,
              "best-segments comparison (the golds as the run started) and sum of best (as they are now)");
        r.split(30000.0);
        check(!r.new_pb() && r.route.segs[2].pb_rta == 25000.0 && r.route.attempts == 2 && r.route.completed == 2,
              "a slower finish keeps the PB");
        r.reset();
        r.start(0.0);
        check(r.skip(0) && r.index() == 1 && r.times()[0].rta < 0, "skip leaves the split empty");
        r.split(9000.0);
        check(!r.times()[1].gold && r.route.segs[1].gold_rta == 6000.0, "no gold after a skipped split");
        check(!r.skip(0), "the last split cannot be skipped");
        r.undo(); r.undo();
        check(r.index() == 0 && r.route.segs[1].gold_rta == 6000.0, "undo walks back over a skip");
        r.timing = Timing::Igt;
        for (int i = 0; i < 120; ++i) r.tick();
        r.split(50000.0);
        // run 2 split A with no steps (IGT 0): that is A's IGT gold, so 2 s is not one
        check(r.split_time(0) == 2000.0 && !r.times()[0].gold && r.route.segs[0].gold_igt == 0.0 &&
              r.delta(0, Compare::BestSegments) == 2000.0, "IGT timing: split times, golds and deltas in IGT");
        r.record_mods({"Infinite HP"});
        r.record_mods({"Infinite HP", "Game speed 2.00x"});
        check(r.mods_used().size() == 2, "the run remembers every modification once");
    }

    // ---- the splits file ----
    {
        Route a = default_route(1, false);
        a.attempts = 7; a.completed = 2;
        a.segs[0].pb_rta = 123456; a.segs[0].gold_igt = 4321;
        Route b;
        check(b.parse(a.text()) && b.category == "Any% (Yunica)" && b.attempts == 7 && b.segs.size() == a.segs.size() &&
              b.segs[0].pb_rta == 123456 && b.segs[0].gold_igt == 4321 && b.segs[1].pb_rta < 0 &&
              b.segs[4].event == "room:S_4099>S_4097", "the splits file round-trips");
        Route h;
        check(h.parse("[run]\r\nCategory=Mine\r\n[split.0]\r\nName=First\r\nEvent=enter:S_1001\r\n") &&
              h.segs.size() == 1 && h.segs[0].name == "First" && h.segs[0].event == "enter:S_1001" && h.segs[0].pb_rta < 0,
              "a hand-written file (no quotes, no times) reads");
        const fs::path dir = fs::temp_directory_path() / "cleria_speedruntest";
        std::error_code ec;
        fs::remove_all(dir, ec);
        check(a.save(dir / "x.splits") && b.load(dir / "x.splits") && b.segs[0].pb_rta == 123456, "save / load a file");
        fs::remove_all(dir, ec);
    }

    // ---- the session: auto start / split, LiveSplit commands, modifications ----
    {
        const fs::path dir = fs::temp_directory_path() / "cleria_speedruntest_s";
        std::error_code ec;
        fs::remove_all(dir, ec);
        Session s;
        s.dir = dir;
        std::vector<std::string> sent;
        s.livesplit = [&](std::string_view c) { sent.emplace_back(c); };
        s.new_game(1, false, 0.0);
        check(s.run.phase() == Run::Phase::Idle && sent.empty(), "speedrun mode off: a new game starts nothing");
        s.set_enabled(true, 0.0);
        s.new_game(1, false, 100.0);
        check(s.run.running() && fs::exists(dir / "yunica.splits") && s.run.route.attempts == 1,
              "a new game starts the run and writes the splits file");
        check(sent.size() == 4 && sent[0] == "reset" && sent[1] == "starttimer" && sent[2] == "pausegametime" &&
              sent[3] == "setgametime 0:00:00.000", "LiveSplit: reset, starttimer, pausegametime, setgametime 0");
        // a custom route: enter S_1001, then a boss in S_1001
        s.reset(0.0);
        s.run.route.segs.clear();
        for (auto [n, e] : {std::pair{"Door", "enter:S_1001"}, std::pair{"Boss", "boss:S_1001"}}) {
            Segment sg; sg.name = n; sg.event = e; s.run.route.segs.push_back(sg);
        }
        sent.clear();
        Fake g;
        s.tick(g.view(), 0.0);                              // prime the observer (idle)
        s.start(1000.0);
        for (int i = 0; i < 90; ++i) s.tick(g.view(), 1000.0 + i * 16.0);
        s.frame(2500.0, g.room);
        check(sent.back() == "setgametime 0:00:01.500", "frame: the IGT goes to LiveSplit (90 steps = 1.5 s)");
        const std::size_t n_before = sent.size();
        s.frame(2510.0, g.room);
        check(sent.size() == n_before, "frame: nothing sent while the IGT has not moved");
        g.room = "S_1001";
        s.tick(g.view(), 3000.0);
        check(s.run.index() == 1 && s.run.times()[0].rta == 2000.0 && sent.back() == "split" &&
              sent[sent.size() - 2] == "setgametime 0:00:01.516", "a real room event auto-splits (IGT sent first)");
        g.gauge = true; g.hp = 50; s.tick(g.view(), 3100.0);
        g.hp = 0; s.tick(g.view(), 3200.0);
        check(s.run.phase() == Run::Phase::Finished && s.run.new_pb() && s.notice.find("personal best") != std::string::npos,
              "the boss gauge emptying ends the run: a PB");
        Route back;
        check(back.load(dir / "yunica.splits") && back.segs.size() == 2 && back.segs[1].pb_rta == 2200.0,
              "the PB is saved at the finish");
        // modifications
        set_provider("test", [](std::vector<std::string>& out) { out.push_back("Infinite HP"); });
        s.start(0.0);
        clear_provider("test");
        note_modification("Debug warp");
        s.tick(g.view(), 10.0);
        std::vector<bool> act;
        const auto mods = s.modifications(&act);
        check(mods.size() == 2 && mods[0] == "Infinite HP" && !act[0] && mods[1] == "Debug warp",
              "modifications: a provider's line stays listed after it is switched off; notes are kept");
        check(active_modifications().empty(), "providers can be removed");
        s.set_enabled(false, 20.0);
        check(s.run.phase() == Run::Phase::Idle && sent.back() == "reset", "turning the mode off resets the run");
        fs::remove_all(dir, ec);
    }

    // ---- the autosplitter block ----
    {
        Run r;
        Segment sg; sg.name = "A"; sg.event = "manual";
        r.route.segs = {sg, sg};
        r.start(0.0);
        for (int i = 0; i < 60; ++i) r.tick();
        const std::uint32_t seq0 = asl_block().split_seq;
        publish(r, 5000.0, "S_1004");
        const volatile AslBlock& b = asl_block();
        char magic[17] = {};
        for (int i = 0; i < 16; ++i) magic[i] = b.magic[i];
        check(std::strcmp(magic, "CleriaSpeedrun01") == 0 && b.version == 1 && b.phase == 1 && b.split_count == 2 &&
              b.igt_ticks == 60 && b.igt_ms == 1000.0 && b.rta_ms == 5000.0 && b.room[0] == 'S' && b.room[6] == 0,
              "the ASL block: magic, phase, IGT, RTA, room");
        r.split(6000.0);
        publish(r, 6000.0, "S_1004");
        check(b.split_index == 1 && b.split_seq != seq0, "the ASL block: split index and sequence");
    }

    // ---- the LiveSplit client over loopback ----
    {
#ifdef _WIN32
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
#endif
        SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        bind(ls, reinterpret_cast<sockaddr*>(&a), sizeof a);
        socklen_t al = sizeof a;
        getsockname(ls, reinterpret_cast<sockaddr*>(&a), &al);
        listen(ls, 1);
        const int port = ntohs(a.sin_port);
        LiveSplitClient c;
        c.configure(true, "127.0.0.1", port);
        double t = 0;
        for (int i = 0; i < 200 && !c.connected(); ++i) { c.poll(t); t += 10; nap_ms(5); }
        check(c.connected(), "the client connects to a listening server");
        SOCKET srv = accept(ls, nullptr, nullptr);
        c.send("starttimer");
        c.send(ls_setgametime(1500));
        c.send("split");
        std::string got;
        char buf[256];
#ifdef _WIN32
        u_long nb = 0;
        ioctlsocket(srv, FIONBIO, &nb);
        DWORD to = 2000;
#else
        timeval to{2, 0};   // an accepted BSD socket blocks already
#endif
        setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof to);
        while (got.find("split\r\n") == std::string::npos) {
            const int n = int(recv(srv, buf, sizeof buf, 0));
            if (n <= 0) break;
            got.append(buf, std::size_t(n));
        }
        check(got == "starttimer\r\nsetgametime 0:00:01.500\r\nsplit\r\n", "the server reads the exact command lines");
        closesocket(srv);
        for (int i = 0; i < 50 && c.connected(); ++i) { c.poll(t); t += 10; nap_ms(5); }
        check(!c.connected() && c.status().find("Not connected") == 0, "a closed connection is noticed");
        check(!c.send("split"), "nothing is sent while disconnected");
        c.configure(false, "127.0.0.1", port);
        check(c.status() == "Off", "configure(false) turns it off");
        closesocket(ls);
        // nothing listening any more: the connect fails and the status says why
        LiveSplitClient d;
        d.configure(true, "127.0.0.1", port);
        double t2 = 0;
        for (int i = 0; i < 400 && d.status().find("not running") == std::string::npos; ++i) { d.poll(t2); t2 += 10; nap_ms(5); }
        check(!d.connected() && d.status().find("not running") != std::string::npos,
              "nothing listening: \"LiveSplit's TCP server is not running\"");
#ifdef _WIN32
        WSACleanup();
#endif
    }

    std::printf("%s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
