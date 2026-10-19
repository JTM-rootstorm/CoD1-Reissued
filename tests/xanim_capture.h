#pragma once
// Captured XAnim evaluation calls (src/xanim.cpp), for tests/xanim_test.cpp.
// A capture file is a sequence of records, each a u32 tag, then:
//
//   "XAC1" (a call): u32 function VA, u32 EAX, EDX, the stack argument,
//   u32 event count and the events, then regions: u32 region count, then
//   per region u32 VA, u32 size, size bytes. Everything the call reads: the
//   globals, the DObj, its skeleton and mask, its models' parts, its anim
//   tree with the anims' infos, xanims, tracks and keys, the cached
//   part-to-bone maps and the part duplicates.
//   An event is a call the function made to one of the lifted functions it
//   calls (kMakeMap, kFreeMap, kDuplicates): u32 function VA, u32 EAX,
//   EDX, two stack arguments as it was called, u32 EAX it returned, then
//   regions as above: what it wrote that the call reads back (the new map
//   or part duplicates in the string table, the DObj's entry for them).
//   "XAR1" (after the call): the same regions as the call's, as the game
//   left them.
//
// Written by tests/xanim_capture.cpp (a cod1 built with
// -DCOD1_XANIM_CAPTURE=ON).

#include <cstdint>

namespace xanim_capture {

inline constexpr uint32_t kCall = 0x31434158;    // "XAC1"
inline constexpr uint32_t kResult = 0x31524158;  // "XAR1"

inline constexpr uint32_t kCalcAnim = 0x47CD80;  // EAX dobj, EDX mask
inline constexpr uint32_t kCalcSkel = 0x4766E0;  // EAX mask; dobj
inline constexpr uint32_t kMakeMap = 0x478FC0;   // EAX anim; models, count -> AX
inline constexpr uint32_t kFreeMap = 0x45D580;   // AX entry, EDX size
inline constexpr uint32_t kDuplicates = 0x4761F0;  // dobj

// The globals the calls read and write: the anim tree and info pool's
// (0xA6D000-0xA6D010) and the evaluation's (0xA8F000-0xA8F680), and the
// string table's address.
inline constexpr uint32_t kGlobals1 = 0xA6D000, kGlobals1Size = 0x10;
inline constexpr uint32_t kGlobals2 = 0xA8F000, kGlobals2Size = 0x680;
inline constexpr uint32_t kStrings = 0x8D8BF8;

}  // namespace xanim_capture
