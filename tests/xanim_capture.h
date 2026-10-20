#pragma once
// Captured XAnim evaluation calls (src/xanim.cpp), for tests/xanim_test.cpp.
// A capture file is a sequence of records, each a u32 tag, then:
//
//   "XAC3" (a call): u32 function VA, u32 EAX, ECX, EDX, two stack
//   arguments, u32 event count and the events, then regions: u32 region
//   count, then per region u32 VA, u32 size, size bytes. Everything the
//   call reads: the globals, the DObj, its skeleton and mask, its models'
//   parts, its anim tree with the anims' infos, xanims, tracks and keys,
//   the cached part-to-bone maps and the part duplicates; for an update
//   the tree's anims, their infos and the free list's, and the xanims with
//   their notes (the game's update likewise).
//   An event is a call the function made to one of the lifted functions it
//   calls (kMakeMap to kScriptCall): u32 function VA, u32 EAX, EDX, ESI,
//   three stack arguments as it was called (0 for those it does not take),
//   u32 EAX it returned, then regions as above: what it wrote that the call
//   reads back (the new map or part duplicates in the string table, the
//   DObj's entry for them; none for the script calls).
//   "XAR3" (after the call): the same regions as the call's, as the game
//   left them.
//
// Written by tests/xanim_capture.cpp (a cod1 built with
// -DCOD1_XANIM_CAPTURE=ON).

#include <cstdint>

namespace xanim_capture {

inline constexpr uint32_t kCall = 0x33434158;    // "XAC3"
inline constexpr uint32_t kResult = 0x33524158;  // "XAR3"

inline constexpr uint32_t kCalcAnim = 0x47CD80;  // EAX dobj, EDX mask
inline constexpr uint32_t kCalcSkel = 0x4766E0;  // EAX mask; dobj
inline constexpr uint32_t kUpdate = 0x47CC90;    // ECX dobj; dt
inline constexpr uint32_t kGameUpdate = 0x47CCD0;  // ECX dobj; dt, notes -> EAX
inline constexpr uint32_t kMakeMap = 0x478FC0;   // EAX anim; models, count -> AX
inline constexpr uint32_t kFreeMap = 0x45D580;   // AX entry, EDX size
inline constexpr uint32_t kDuplicates = 0x4761F0;  // dobj
inline constexpr uint32_t kFreeName = 0x45D530;    // entry
inline constexpr uint32_t kSendNote = 0x47B390;    // ESI note; target, name
inline constexpr uint32_t kPushString = 0x46A7A0;  // entry
inline constexpr uint32_t kScriptCall = 0x4683B0;  // target, name, argument count

// The globals the calls read and write: the anim tree and info pool's
// (0xA6D000-0xA6D010) and the evaluation's (0xA8F000-0xA8F680), and the
// string table's address.
inline constexpr uint32_t kGlobals1 = 0xA6D000, kGlobals1Size = 0x10;
inline constexpr uint32_t kGlobals2 = 0xA8F000, kGlobals2Size = 0x680;
inline constexpr uint32_t kStrings = 0x8D8BF8;
// The update's notes queue (a count, then 12-byte notes in kGlobals2), and
// the string-table entry of the note at a cycle's end.
inline constexpr uint32_t kNumNotes = 0xA8F028, kEndNote = 0x13F9D30;

}  // namespace xanim_capture
