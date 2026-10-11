// Offline simulation of the probe's game-side logic. It does NOT run the game.
//
// The process maps plain memory over the address range of yso_win.exe's image
// (0x400000..), writes the first bytes of the three real functions the mod hooks or
// calls (followed by stubs that return), a fake scene list, a fake player actor
// and a fake save block, then loads the real dinput8.dll and presses buttons
// through its RetailProbeSim entry. It checks what the DLL writes and where:
// the staged door, the state written at the load boundary, the post-load step,
// the gates, the save block, the helpers and the access-violation guard.
//
// What it cannot show: how the real engine reacts to those writes.
//
//   probe_simtest <dinput8.dll> <retail_probe_tests.ini>      exit 0 = all checks passed
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "game.h"

using namespace ga;
typedef int(__cdecl* Sim)(int, const char*, char*, int);

static int g_fail = 0, g_load_calls = 0, g_save_calls = 0;
static void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}
static void put(uintptr_t at_, const unsigned char* b, size_t n) { memcpy((void*)va(at_), b, n); }
static size_t emit_inc_ret(unsigned char* p, int* counter, bool set_eax) {
    size_t n = 0;
    p[n++] = 0x8B; p[n++] = 0xE5;                 // mov esp, ebp
    p[n++] = 0x5D;                                // pop ebp
    p[n++] = 0xFF; p[n++] = 0x05;                 // inc dword ptr [counter]
    memcpy(p + n, &counter, 4); n += 4;
    if (set_eax) { p[n++] = 0xB8; p[n++] = 1; p[n++] = 0; p[n++] = 0; p[n++] = 0; }
    p[n++] = 0xC3;
    return n;
}

uintptr_t ga::g_delta = 0;
// The level function's real first bytes read two constants by absolute address
// (0x74e700 / 0x74e704), which are not mapped here: step over those two 8-byte
// instructions. Anything else is left to the DLL's own guard.
static LONG CALLBACK skip_absolute_movss(EXCEPTION_POINTERS* e) {
    uintptr_t ip = (uintptr_t)e->ExceptionRecord->ExceptionAddress, fn = va(kFnSetLevel);
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && (ip == fn || ip == fn + 10)) {
        e->ContextRecord->Eip += 8;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static unsigned char g_tab[0x1C8 * 16];
static unsigned char g_actor[0x1000], g_boss[0x1000], g_mob[0x1000];
static unsigned char* g_pool[3];
static Sim sim;
static char g_out[400];

static void world_room(int scene) {
    at<int>(kCurScene) = scene;
    at<int>(kNextScene) = 0;
    at<int>(kPendScene) = 0;
    at<int>(kSceneCmd) = 0;
    at<unsigned char*>(kPlayer) = g_actor;
    at<float>(kFadeSpeed) = 0.05f;
}
// What the engine does between the click and the new room: the fade machine's
// commit, the teardown, the loader call (our hook runs in front of it), the spawn.
static void engine_load() {
    int scene = at<int>(kPendScene);
    memcpy((void*)va(0x0076A3A0), (void*)va(kPendDesc), kDescSize);
    at<int>(kPendScene) = 0;
    at<int>(kSceneCmd) = 3;
    ((void(__cdecl*)())va(kFnSceneLoad))();
    at<int>(kSceneCmd) = 0;
    world_room(scene);
    if (*(char*)va(kArriveLabel)) memcpy(g_actor + kActPos, (void*)va(0x0076A3A0), 12);
    *(float*)(g_actor + kActMaxHp) = at<float>(kMaxHp);
    *(float*)(g_actor + kActHp) = at<float>(kHp);
    *(unsigned*)(g_actor + kActIframes) = 0;
}
static void frames(int n) { for (int i = 0; i < n; i++) sim(2, "", nullptr, 0); }

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: probe_simtest <dinput8.dll> <tests.ini>\n"); return 2; }
    // A process cannot count on 0x400000.. being free (its own image, heaps and stack
    // sit there), so the fake image goes wherever there is room and every address is
    // moved by the same amount - on both sides: here and, through op 100, in the DLL.
    void* img = VirtualAlloc(nullptr, 0x003DF000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!img) { printf("SKIP: could not allocate the fake image (error %lu)\n", GetLastError()); return 3; }
    g_delta = (uintptr_t)img - 0x00400000;
    AddVectoredExceptionHandler(1, skip_absolute_movss);

    // The real functions' first bytes, then stubs.
    unsigned char load[64] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec, 0xe8, 0x01, 0x00, 0x00};
    emit_inc_ret(load + 12, &g_load_calls, false);
    put(kFnSceneLoad, load, sizeof(load));
    unsigned char save[64] = {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x78, 0x4b, 0x67, 0x00, 0x64, 0xa1, 0, 0, 0, 0};
    emit_inc_ret(save + 16, &g_save_calls, true);
    put(kFnSaveWrite, save, sizeof(save));
    // movss xmm3,[0x74e704] ; mov edx,ecx ; movss xmm4,[0x74e700] ; mov [level], ecx ; ret
    unsigned char level[] = {0xf3, 0x0f, 0x10, 0x1d, 0x04, 0xe7, 0x74, 0x00, 0x8b, 0xd1, 0xf3, 0x0f, 0x10, 0x25, 0x00, 0xe7, 0x74, 0x00,
                             0x89, 0x0d, 0, 0, 0, 0, 0xc3};
    uintptr_t level_cell = va(kLevel);
    memcpy(level + 20, &level_cell, 4);
    put(kFnSetLevel, level, sizeof(level));
    const unsigned char look[] = {0x55, 0x8b, 0xec, 0x51, 0x53, 0x56, 0x57, 0x8b, 0xf1, 0x8d, 0x51, 0x01};
    put(0x004358B0, look, sizeof(look));

    // Scene list: ids 10.. are rooms.
    const char* dirs[16] = {"", "title\\", "demo1\\", "", "", "", "", "", "", "", "s_10\\s_1004\\", "s_40\\s_4099\\",
                            "s_70\\s_7080\\", "s_10\\s_1002\\", "s_20\\s_2000\\", "s_30\\s_3099\\"};
    for (int i = 0; i < 16; i++) strcpy((char*)g_tab + i * 0x1C8 + 4, dirs[i]);
    at<unsigned char*>(kSceneTable) = g_tab;
    at<unsigned>(kSceneCount) = 16;
    for (int i = 0; i < 128; i++) flag(i) = -1;
    flag(kFlagChar) = 1;                       // Yunica
    at<float>(kMaxHp) = 100.0f;
    at<float>(kHp) = 40.0f;
    at<float>(kMaxMp) = 3000.0f;
    at<int>(kLevel) = 5;
    at<unsigned char>(kCfgShadows) = 2;
    at<unsigned char>(kCfgWater) = 2;
    at<unsigned char>(kCfgGlare) = 1;
    g_pool[0] = g_actor; g_pool[1] = g_boss; g_pool[2] = g_mob;
    at<unsigned char**>(kActorPool) = g_pool;
    at<unsigned>(kActorCount) = 3;

    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { printf("FAIL  LoadLibrary(%s) error %lu\n", argv[1], GetLastError()); return 1; }
    sim = (Sim)GetProcAddress(dll, "RetailProbeSim");
    check(sim != nullptr, "the DLL loads and exports its entry points");
    if (!sim) return 1;
    char base[32];
    snprintf(base, sizeof(base), "%u", (unsigned)g_delta);
    sim(100, base, nullptr, 0);             // tell the DLL where the fake image is; it then installs its hooks
    check(*(unsigned char*)va(kFnSceneLoad) == 0xE9 && *(unsigned char*)va(kFnSaveWrite) == 0xE9, "exe bytes verified, both game hooks installed");
    check(sim(0, argv[2], g_out, sizeof(g_out)) > 0, "test list loads without problems");

    // --- gates -------------------------------------------------------------------
    frames(1);
    check((sim(5, "", g_out, sizeof(g_out)) & 1) == 1, "status: supported");
    check(sim(4, "", g_out, sizeof(g_out)) == 0, "no player: staging refused");
    world_room(10);
    frames(1);
    sim(5, "", g_out, sizeof(g_out));
    check(strcmp(g_out, "S_1004") == 0, "current room name read from the scene list");
    check(sim(4, "", g_out, sizeof(g_out)) == 1, "in a room: staging allowed");
    at<int>(kFixA) = 1;
    check(sim(1, "mini_gauge_khonsclard", g_out, sizeof(g_out)) == 0, "cutscene (Fix) running: test refused");
    at<int>(kFixA) = 0;
    at<unsigned char>(kBonusMode) = 1;
    check(sim(1, "mini_gauge_khonsclard", g_out, sizeof(g_out)) == 0, "Time Attack running: test refused");
    at<unsigned char>(kBonusMode) = 0;
    check(g_load_calls == 0 && at<int>(kPendScene) == 0 && at<int>(kNextScene) == 0, "a refused test wrote nothing");

    // --- a boss test: door staged, state at the load boundary, post-load ------------
    flag(223) = 1; flag(kFlagBossFight) = 1;
    check(sim(1, "mini_gauge_khonsclard", g_out, sizeof(g_out)) == 1, "boss test accepted");
    check(at<float>(kFadeTarget) == 1.0f && at<float>(kFadeCur) == 0.0f && at<int>(kPendScene) == 11 && at<int>(kNextScene) == 11,
          "door staged: fade to black, pending scene = S_4099");
    check(*(char*)va(kPendDesc + 0x14) == 0, "no position asked: empty label (default spawn)");
    check(*(unsigned*)(g_actor + kActIframes) >= 0xFF, "player i-frames set like a door does");
    check(flag(205) != 1 && at<int>(kLevel) == 5, "nothing of the test's state written before the load boundary");
    check(sim(1, "velagunder", g_out, sizeof(g_out)) == 0, "second click while loading: refused");
    engine_load();
    check(g_load_calls == 1, "the real loader still runs after our hook (trampoline, stack balanced)");
    check(flag(205) == 1 && flag(223) == 0 && flag(kFlagBossFight) == 0, "load boundary: retry flag set, boss undefeated, fight flag cleared");
    bool cells = true;
    for (int c = 0; c < 4; c++) cells = cells && at<int>(kWeaponCells + c * 8) == 4;
    check(cells && (at<int>(kRecalc) & 0x75) == 0x75, "weapon tier cells written, stat recompute requested");
    check(flag(kFlagDiff) == 0 && flag(kFlagEasy) == 0 && flag(kFlagVeryEasy) == 0, "difficulty Normal");
    check(at<int>(kLevel) == 5, "level not touched before the player exists");
    frames(25);
    check(at<int>(kLevel) == 34, "post-load: level set through the game's own function");
    check(at<float>(kHp) == 100.0f && *(float*)(g_actor + kActHp) == 100.0f, "post-load: HP full (global and actor)");
    check((sim(5, "", g_out, sizeof(g_out)) & 8) == 0, "not busy any more");

    // --- character gate and the experimental switch ----------------------------------
    check(sim(1, "toal_air_skills", g_out, sizeof(g_out)) == 0, "Toal test on a Yunica save: refused");
    sim(6, "char_switch=1", nullptr, 0);
    check(sim(1, "toal_air_skills", g_out, sizeof(g_out)) == 1, "with the experimental switch: accepted");
    engine_load();
    check(flag(kFlagChar) == 3 && flag(kFlagWeaponId) == 2 && flag(2) == 1 && flag(kFlagArmorId) == 0x12 && flag(kFlagBootsId) == -1,
          "character switched like the new-game init does");
    check(flag(116) == 1 && flag(118) == 1 && flag(182) == 3 && flag(184) == 3 && at<int>(kEquipSkill) == 0x74, "bracelets, skill levels, equipped skill");
    frames(25);
    sim(6, "char_switch=0", nullptr, 0);

    // --- difficulty, items, config ---------------------------------------------------
    check(sim(1, "m15_easy_hud", g_out, sizeof(g_out)) == 1, "difficulty test accepted");
    engine_load();
    check(flag(kFlagEasy) == 1 && flag(kFlagVeryEasy) == 1 && flag(kFlagDiff) == 0, "Very Easy = flags 143 and 203");
    frames(25);
    check(sim(1, "hull_zava", g_out, sizeof(g_out)) == 0 && at<int>(kPendScene) == 0, "a room missing from the scene list: refused, nothing staged");
    check(sim(1, "shadows_terrain|shadows=0", g_out, sizeof(g_out)) == 1, "config button accepted");
    check(at<unsigned char>(kCfgShadows) == 2, "config byte not written before the load boundary");
    engine_load();
    check(at<unsigned char>(kCfgShadows) == 0, "Shadows 0 written at the load boundary");
    frames(25);
    at<unsigned char>(kCfgGlare) = 1;
    sim(1, "glare_floor2|glare=2", g_out, sizeof(g_out));
    engine_load();
    check(at<unsigned char>(kCfgGlare) == 1, "Glare 2 refused: the game started with 1 (needs a restart)");
    frames(25);

    // --- saving -------------------------------------------------------------------------
    check((sim(5, "", g_out, sizeof(g_out)) & 2) == 2, "state is marked as a test state");
    ((unsigned(__fastcall*)(char*))va(kFnSaveWrite))(nullptr);
    check(g_save_calls == 0, "the game's save writer is blocked while a test state is loaded");
    sim(6, "allow_save=1", nullptr, 0);
    ((unsigned(__fastcall*)(char*))va(kFnSaveWrite))(nullptr);
    check(g_save_calls == 1, "'Allow saving' lets it through");
    sim(6, "allow_save=0", nullptr, 0);
    strcpy((char*)va(kArriveLabel), "load");
    ((void(__cdecl*)())va(kFnSceneLoad))();
    *(char*)va(kArriveLabel) = 0;
    check((sim(5, "", g_out, sizeof(g_out)) & 2) == 0, "the game's own Load clears the test-state mark");
    ((unsigned(__fastcall*)(char*))va(kFnSaveWrite))(nullptr);
    check(g_save_calls == 2, "saving works again after a real Load");

    // --- helpers ---------------------------------------------------------------------------
    *(int*)(g_boss + kActId) = 77;
    *(float*)(g_boss + kActHp) = 500.0f; *(float*)(g_boss + kActMaxHp) = 900.0f;
    *(float*)(g_mob + kActHp) = 60.0f; *(float*)(g_mob + kActMaxHp) = 60.0f;
    check(sim(3, "boss_hp_1", g_out, sizeof(g_out)) == 0, "boss HP: refused with no gauge on screen");
    at<int>(kBossGaugeId) = 77;
    at<float>(kBossGaugeCur) = 300.0f;
    check(sim(3, "boss_hp_1", g_out, sizeof(g_out)) == 0 && *(float*)(g_boss + kActHp) == 500.0f, "boss HP: refused when the gauge and the actor disagree");
    at<float>(kBossGaugeCur) = 500.0f;
    check(sim(3, "boss_hp_1", g_out, sizeof(g_out)) == 1 && *(float*)(g_boss + kActHp) == 1.0f, "boss HP -> 1");
    check(sim(3, "monsters_hp_1", g_out, sizeof(g_out)) == 1 && *(float*)(g_mob + kActHp) == 1.0f && *(float*)(g_actor + kActHp) == 100.0f,
          "monsters HP -> 1, player untouched");
    sim(3, "invincible", g_out, sizeof(g_out));
    *(unsigned*)(g_actor + kActIframes) = 0;
    frames(1);
    check(*(unsigned*)(g_actor + kActIframes) >= 120, "invincible: i-frames kept up");
    sim(3, "invincible", g_out, sizeof(g_out));
    at<float>(kBoost) = 0.0f;
    check(sim(3, "boost", g_out, sizeof(g_out)) == 1 && at<float>(kBoost) == 30000.0f && flag(kFlagBoostOn) == 1, "Boost gauge filled, gauge unlocked");
    float here[3] = {12.5f, 3.0f, -7.25f};
    memcpy(g_actor + kActPos, here, 12);
    check(sim(8, "", g_out, sizeof(g_out)) == 1, "position saved");
    check(sim(9, "", g_out, sizeof(g_out)) == 1 && memcmp((void*)va(kPendDesc), here, 12) == 0 && *(char*)va(kPendDesc + 0x14) != 0,
          "restore: room reload staged with the saved position and a label");
    engine_load();
    frames(25);
    check(memcmp(g_actor + kActPos, here, 12) == 0, "arrived at the saved position");
    check(sim(7, "S_9999", g_out, sizeof(g_out)) == 0, "unknown room: refused");

    // --- the guard: a bad pointer must not crash the process -------------------------------
    at<unsigned char*>(kPlayer) = (unsigned char*)0x10;
    check(sim(2, "", nullptr, 0) == -1, "access violation caught");
    check((sim(5, "", g_out, sizeof(g_out)) & 4) == 4, "status: faulted, game writes off");
    at<unsigned char*>(kPlayer) = g_actor;
    check(sim(1, "velagunder", g_out, sizeof(g_out)) == 0, "after a fault every test is refused");

    printf("%d check(s) failed\n", g_fail);
    return g_fail ? 1 : 0;
}
