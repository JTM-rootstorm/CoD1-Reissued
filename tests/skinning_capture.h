#pragma once
// Captured calls of the CPU skinning functions (src/skinning.cpp), for
// tests/skinning_test.cpp. A capture file is a sequence of records:
//
//   "SKN1", u32 function VA, u32 argument, u32 region count,
//   then per region: u32 VA, u32 size, size bytes
//
// The regions are everything the call reads besides the stack: the
// argument block, the model fields it uses, the surface, its indexes,
// texture coordinates, vertices, extra bones and bone matrices, and the
// tessellator counts. Written by tests/skinning_capture.cpp (a cod1 built
// with -DCOD1_SKINNING_CAPTURE=ON).

#include <cstdint>
#include <vector>

namespace skinning {

inline constexpr uint32_t kMagic = 0x314E4B53;  // "SKN1"
inline constexpr uint32_t kRigid = 0x5070E0;
inline constexpr uint32_t kWeighted = 0x507D20;

struct Region {
  uint32_t va;
  std::vector<uint8_t> bytes;
};

}  // namespace skinning
