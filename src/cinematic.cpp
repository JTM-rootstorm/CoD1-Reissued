// Cinematic (RoQ) playback fixes. Replacements of lifted functions named
// with `replace` in manifest/CoDSP.manifest; FINDINGS.md "Cinematics".

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

// Cinematic slots: 16 of 0x1D0 bytes at 0x86A4A8; the current one's
// index is at 0x56EE44.
constexpr uint32_t kCurrentSlot = 0x56EE44;
constexpr uint32_t kSlots = 0x86A4A8;
constexpr uint32_t kSlotSize = 0x1D0;
// Highest frame the playback clock has asked for. The run loop
// (0x407040) only raises it and decodes until the frame count matches.
constexpr uint32_t kTargetFrame = 0x14C;

}  // namespace

void f_00406DA0_lifted(Ctx& c);

// Rewinds the current slot's RoQ: playback clock, frame rate, frame count
// (-1) and file position, from the header just read. Called when a
// cinematic starts and when a looping one restarts.
//
// The original leaves the target frame from the slot's previous use. It
// recovers only if the first info chunk restarts the clock at a different
// millisecond; when a cinematic chains straight into the next (atvi.roq's
// nextmap starting iw_logo.roq) both happen in the same millisecond, the
// stale target survives, and the run loop decodes the whole file at once,
// paced only by the audio ring, so the movie ends early.
void f_00406DA0(Ctx& c) {
  f_00406DA0_lifted(c);
  Wr32(kSlots + Rd32(kCurrentSlot) * kSlotSize + kTargetFrame, 0);
}

}  // namespace recomp::gen
