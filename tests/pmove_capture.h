#pragma once
// Captured player movement (src/pmove.cpp), for tests/pmove_test.cpp. A
// capture file is a sequence of records, each a u32 tag, then:
//
//   "PMW1" (weapons): u32 module (0 game, 1 cgame), then regions: u32
//   count, then per region u32 VA, u32 size, size bytes. The weapon
//   definitions' pointer table and every definition. Written when the
//   module's table or count changes.
//   "PMC1" (a call of Pmove): u32 module, u32 pm (ESI), u32 pmove_fixed
//   on, u32 its step, then regions as above: pmove_t, playerState_t, the
//   module's pmove globals and the cvars and tables pmove reads.
//   "PMT1" (each trace or point contents call the game made in it): u32
//   which (0-2 pm->trace, traceProne, traceUpright, 3 pointcontents),
//   start, end, mins, maxs (3 u32 each; point contents: the point in start),
//   u32 passEntityNum, u32 mask, then the trace_t the game got (0x30
//   bytes; point contents: the contents and 0x2C zero bytes).
//   "PMR1" (after the call): pmove_t and playerState_t (0x120 + 0x400
//   bytes) as the game left them.
//
// Written by tests/pmove_capture.cpp (a cod1 built with
// -DCOD1_PMOVE_CAPTURE=ON).

#include <cstdint>

namespace pmove_capture {

inline constexpr uint32_t kWeapons = 0x31574D50;  // "PMW1"
inline constexpr uint32_t kCall = 0x31434D50;     // "PMC1"
inline constexpr uint32_t kTrace = 0x31544D50;    // "PMT1"
inline constexpr uint32_t kResult = 0x31524D50;   // "PMR1"

inline constexpr uint32_t kPmSize = 0x120, kPsSize = 0x400, kWeaponDefSize = 0x420;

// What pmove reads in each module besides pm, ps and the weapons.
struct ModuleData {
  uint32_t pmove;  // Pmove's VA
  uint32_t lo, hi;  // the pmove globals (pml, pmove pointer and count, cvars)
  uint32_t debugLo, debugHi;  // the weapon debug state
  uint32_t numWeapons, weaponDefs, clipSizes, noFire;
  uint32_t proneDebug, proneDebugTime;  // game only
  uint32_t syscall;
  uint32_t levelTime, eventInterval, lastEvent;  // game only: 0x20073F80's
};

inline constexpr ModuleData kModuleData[2] = {
    {0x20008580, 0x20125600, 0x20128000, 0x200A6F50, 0x200A6F58, 0x2012564C, 0x20125650,
     0x200A8E30, 0x20298CAC, 0x20295D6C, 0x20295A0C, 0x200A4834, 0x202DB258, 0x20293BAC,
     0x2012560C},
    {0x30007C30, 0x300CEB00, 0x300D1400, 0x30066FD8, 0x30066FE0, 0x300CEB88, 0x300CEB8C,
     0x30068E50, 0x301E87AC, 0, 0, 0x300657E8, 0, 0, 0},
};
inline constexpr uint32_t kClipSizesSize = 0x100;  // 64 clips

}  // namespace pmove_capture
