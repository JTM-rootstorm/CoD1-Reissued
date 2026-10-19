#pragma once
// pmove_fixed's state for the native Pmove (src/pmove.cpp): src/pmove_fixed.cpp
// keeps the command times from being rounded up and runs cgame's view
// between the step ends.

#include <cstdint>

namespace recomp::gen {

// Whether pmove_fixed is on for the Pmove that runs now (the server's in
// ClientThink, cgame's in its prediction), and its step length.
bool PmoveFixedOn(bool game);
int32_t PmoveFixedStep(bool game);
// cgame: where the player was at the end of the step ending at time.
void PmoveFixedStepEnd(int32_t time, uint32_t x, uint32_t y, uint32_t z);

}  // namespace recomp::gen
