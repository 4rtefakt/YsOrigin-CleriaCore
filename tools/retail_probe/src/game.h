// Retail Ys Origin (yso_win.exe v1.1.1.0, Steam, image base 0x400000, no ASLR) —
// every address this mod reads, writes, calls or hooks, with where it was checked.
//
// Evidence tags (same as the CleriaCore specs):
//   [EXPORT f:l]  read in the Ghidra export D:\ghidra-work\export\funcs\<f>.c at line l
//   [BYTES a]     read in the exe bytes with tools/exe_peek.py at address a
//   [AP]          used by the user's Archipelago mod (mod/src/hook_ap.cpp), which is
//                 played in the retail game — i.e. exercised live, not only read
//   [DOC f]       taken from a CleriaCore spec and NOT re-read here
#pragma once
#include <cstdint>

namespace ga {

// ---- code ---------------------------------------------------------------------
// The one scene loader. Called by FUN_00442dd0 at the start of a frame, AFTER the
// old room's objects were deleted (FUN_00428ec0) and BEFORE anything of the new
// room exists. [EXPORT 00442dd0.c:17-25, 00431260.c] [BYTES 0x431260: 55 8b ec 83 e4 f8 81 ec e8 01 00 00]
const uintptr_t kFnSceneLoad = 0x00431260;
// The save writer (both callers ignore its result). __fastcall, one register
// argument, plain `ret`. [EXPORT 0042e9a0.c, 005b5100.c:612, 005b7cc0.c:290] [BYTES 0x42e9a0, ret at 0x42ef3f] [AP kSaveWrite]
const uintptr_t kFnSaveWrite = 0x0042E9A0;
// Set level N: EXP = threshold, level, stat recompute. __fastcall(ecx = level).
// The new-game init calls it with no player. [EXPORT 004200f0.c, 0041fd10.c:157] [BYTES 0x4200f0] [AP kSetLevel]
const uintptr_t kFnSetLevel = 0x004200F0;

// ---- scene / transition state ---------------------------------------------------
const uintptr_t kSceneTable   = 0x0074C124;  // u8* : SCENELIST.SL records, 0x1c8 each, dir string at +4 [EXPORT 004358b0.c:45-50]
const uintptr_t kSceneCount   = 0x0074E230;  // u32 [EXPORT 004358b0.c:39]
const int       kSceneStride  = 0x1C8;
const uintptr_t kCurScene     = 0x0076A534;  // current scene id [EXPORT 00431260.c:99-101]
const uintptr_t kNextScene    = 0x0076A538;  // pending scene id, 0 = none [EXPORT 00430ef0.c:271-277, 0042cf00.c:71,86]
const uintptr_t kSceneCmd     = 0x0074C120;  // bit1 tear down, bit0 load [EXPORT 00442dd0.c:17-25]
const uintptr_t kSceneNumber  = 0x0076C100;  // g_flags[0x1f9]: the room number as a decimal (1004) [AP kCurScene]
// The fader, and the pending door it commits when the screen is black. These are
// exactly the cells the door trigger FUN_0042cf00 writes on a hit. [EXPORT 0042cf00.c:72-86, 004106e0.c:82-101]
const uintptr_t kFadePrev     = 0x00769F18;  // f32
const uintptr_t kFadeCur      = 0x00769F1C;  // f32
const uintptr_t kFadeTarget   = 0x00769F24;  // f32
const uintptr_t kFadeSpeed    = 0x00769F28;  // f32 per step, 0.05 after every scene entry [EXPORT 004106e0.c:105-109] [DOC warp_re/WARP.md §4]
const uintptr_t kFadeHold     = 0x00769F2C;  // i32
const uintptr_t kPendScene    = 0x00769FB8;  // i32: scene the fade machine commits
const uintptr_t kPendDesc     = 0x00769FBC;  // 0x194-byte arrival descriptor
const uintptr_t kArriveLabel  = 0x0076A3B4;  // active descriptor's label; "load" = a save file was just read into the save block [EXPORT 00431260.c:44,69]
const int       kDescSize     = 0x194;      // +0 x, +4 y, +8 z, +0xc facing (rad), +0x10 sub-area, +0x14 label[0xc0], +0xd4 label copy
                                             // [EXPORT 004106e0.c:89-99, 00434020.c:60-64] [DOC warp_re/WARP.md §3]

// ---- "is it safe to act" gates (the door trigger's own) -------------------------
const uintptr_t kPlayer       = 0x0074C09C;  // player actor*, 0 outside a room [EXPORT 00434020.c:83, 0042cf00.c:48] [AP kPlayerEntPtr]
const uintptr_t kMenuFreeze   = 0x0074AB8F;  // u8 [EXPORT 0042cf00.c:47]
const uintptr_t kGameOver     = 0x0074AB71;  // u8 [EXPORT 0042cf00.c:70, 004106e0.c:86]
const uintptr_t kFixA         = 0x0075AD90;  // i32, Fix slots: both 0 = no cutscene [EXPORT 0042cf00.c:70, 004106e0.c:50]
const uintptr_t kFixB         = 0x0075AD94;
const uintptr_t kBonusMode    = 0x0074E25E;  // u8: 1 Time Attack, 2 Arena, 0 story [EXPORT 00430ef0.c:44-50, 0041fd10.c:52]

// ---- player actor fields ---------------------------------------------------------
const int kActPos     = 0x50;   // f32 x, y, z [EXPORT 0042cf00.c:59-61]
const int kActId      = 0x14;   // handle the HUD gauges key on [EXPORT 004fa0e0.c:12]
const int kActDead    = 0x11;   // u8, non-zero = being deleted [EXPORT 0042cf00.c:187]
const int kActMaxHp   = 0x94;   // f32 [EXPORT 00420c40.c:362]
const int kActHp      = 0x98;   // f32 [EXPORT 00420c40.c:352,363] [DOC audit/combat.md: damage subtracts at +0x98]
const int kActHeading = 0x308;  // f32 [EXPORT 0042cf00.c:143]
const int kActIframes = 0x734;  // u32 countdown; the engine itself uses up to 9999 [EXPORT 0042cf00.c:116-120] [DOC audit/combat.md #11]
const uintptr_t kActorPool    = 0x00730098;  // actor** [EXPORT 0042cf00.c:180-187, 00434db0.c:13-17]
const uintptr_t kActorCount   = 0x0073009C;  // u32

// ---- the save block (0x76a2d0, 0x1fc8 bytes) --------------------------------------
const uintptr_t kSaveBlock    = 0x0076A2D0;  // [EXPORT 0041fd10.c:27, 0042e9a0.c:67]
const int       kSaveBlockLen = 0x1FC8;
const uintptr_t kGFlags       = 0x0076B91C;  // i32[]; 0..127 = item counts (-1 never owned) [EXPORT 0041fd10.c:29,43-46] [AP kGFlagsAbs]
const int kFlagEasy = 143, kFlagDiff = 144, kFlagVeryEasy = 203;   // [EXPORT 00430ef0.c:182-204] [DOC mech_re/DIFFICULTY.md §1]
const int kFlagWeaponTier = 148;              // derived from kWeaponCells by the recompute [EXPORT 00420c40.c:83]
const int kFlagChar = 150;                    // 0 Adol 1 Yunica 2 Hugo 3 Toal [EXPORT 00430ef0.c:52, 0041fd10.c:98-124]
const int kFlagWeaponId = 151, kFlagArmorId = 152, kFlagBootsId = 153;   // [EXPORT 0041fd10.c:100-123,150]
const int kFlagBossFight = 156;               // 0x76bb8c: doors closed while set [EXPORT 0042cf00.c:48]
const int kFlagBrainHold = 131;
const int kFlagRetry = 205;                   // a boss room's root script starts the fight at once when 1 [DATA S_1099/S_3099/S_4099/S_6082/S_7080 root XSO]
const int kFlagSkillLv = 182;                 // 182 wind, 183 thunder, 184 fire, 0..3 [DOC ability_re/ABILITIES_VERIFIED.md §2] [AP 0xB6..0xB8]
const int kFlagBoostOn = 187;                 // [EXPORT 00465170.c:510] [DOC ABILITIES_VERIFIED §1]
const uintptr_t kEquipSkill   = 0x0076A630;   // 0x74 wind / 0x75 thunder / 0x76 fire, -1 none [EXPORT 0046d560.c, 0041fd10.c:86]
const uintptr_t kWeaponCells  = 0x0076A634;   // i32 at +ch*8, ch 0..3: weapon tier 0..10 [EXPORT 004201d0.c, 00420c40.c:83] [AP kRavalBase-0x20]
const uintptr_t kGearCells    = 0x0076A654;   // u32[54] by item id [EXPORT 0041fd10.c:165-169] [AP kRavalBase]
const uintptr_t kMaxHp        = 0x0076A72C;   // f32 [EXPORT 0041fd10.c:172, 00420c40.c:362]
const uintptr_t kHp           = 0x0076A730;   // f32 [EXPORT 004106e0.c:86]
const uintptr_t kMaxMp        = 0x0076A734;   // f32 [EXPORT 004fa1e0.c]
const uintptr_t kMp           = 0x0076A738;   // f32
const uintptr_t kExp          = 0x0076A748;   // f32 [EXPORT 004200f0.c:44] [AP kExpAbs]
const uintptr_t kSp           = 0x0076A75C;   // i32 [AP kSpAbs] [DOC RETAIL_ANSWERS C1]
const uintptr_t kLevel        = 0x0076A760;   // i32 [EXPORT 004200f0.c:45] [AP kLevelAbs]
const uintptr_t kRecalc       = 0x0076B914;   // dirty bits; 0x75 = what a pickup and the spawn set [EXPORT 00434020.c:80] [AP kRecalcBits]
const uintptr_t kBoost        = 0x0076C1B4;   // f32, full = 30000.0 [EXPORT 00465170.c:510] [BYTES 0x6bdf68 = 30000.0]

// ---- HUD boss gauge (which actor is "the boss") --------------------------------------
const uintptr_t kBossGaugeId  = 0x006D5FD8;   // widget+4 = actor +0x14 of the gauge's owner, 0 = no gauge [BYTES 0x43a48f] [DOC hud_re/HUD_HP_BARS.md]
const uintptr_t kBossGaugeCur = 0x006D6004;   // widget+0x30 f32 [BYTES 0x43a492] [DOC hud_re/BOSS_GAUGE_DRAW.md §2.1]

// ---- graphics config bytes ([DOC shell_re/CONFIG.md §2.3; scratchpad SHADOWS.md §1]) ---
const uintptr_t kCfgOtherSimpl = 0x00765171;  // OtherLightWeightMode 0/1
const uintptr_t kCfgShadows    = 0x00765198;  // 0..2
const uintptr_t kCfgWater      = 0x00765199;  // "Reflections" 0..2
const uintptr_t kCfgGlare      = 0x0076519A;  // 0..2
const uintptr_t kCfgCaustics   = 0x0076519C;  // WaterCaustics: 1 = caustics OFF

// Always 0 in the game. Only the offline simulation (probe_simtest.exe, which cannot
// map memory at the exe's own addresses) moves the whole image by this amount.
extern uintptr_t g_delta;
inline uintptr_t va(uintptr_t a) { return a + g_delta; }
template <class T> inline T& at(uintptr_t a) { return *reinterpret_cast<T*>(a + g_delta); }
inline int& flag(int i) { return at<int>(kGFlags + (uintptr_t)i * 4); }

}  // namespace ga
