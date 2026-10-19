#pragma once
// Captured calls of the renderer functions src/render.cpp replaces, for
// tests/render_test.cpp. A capture file is a sequence of records:
//
//   "RND1", u32 function VA, u32 EAX, ECX, EDX, EBX, ESI, EDI,
//   u32 stack argument count, the arguments, u32 region count,
//   then per region: u32 VA, u32 size, size bytes
//
// The regions are what the call (not its callees, which the test stubs)
// reads besides the stack: for a cull, the cell or AABB tree with its
// surfaces, static models and entities, the planes, portals, dlights and
// the cull's globals; for the sky, the polygon and the sky bounds; for
// ComputeTexCoords, the shader's stages and texmods and the tessellator's
// arrays; for qsortFast and the matrix product, their operands. Written by
// tests/render_capture.cpp (a cod1 built with -DCOD1_RENDER_CAPTURE=ON).

#include <cstdint>
#include <vector>

namespace render_capture {

inline constexpr uint32_t kMagic = 0x31444E52;  // "RND1"

// The captured entry points (the others run under them).
inline constexpr uint32_t kCell = 0x4D9650;       // a cell's static models and entities
inline constexpr uint32_t kAabbTree = 0x4D8420;   // a surface AABB tree's node
inline constexpr uint32_t kSurface = 0x4D8280;    // a world surface reference
inline constexpr uint32_t kClipSky = 0x4DB660;    // ClipSkyPolygon
inline constexpr uint32_t kTexCoords = 0x4F5940;  // ComputeTexCoords
inline constexpr uint32_t kSort = 0x4CADC0;       // qsortFast
inline constexpr uint32_t kMatrix = 0x4C94D0;     // the 4x4 matrix product

struct Region {
  uint32_t va;
  std::vector<uint8_t> bytes;
};

struct Record {
  uint32_t fn;
  uint32_t eax, ecx, edx, ebx, esi, edi;
  std::vector<uint32_t> args;
  std::vector<Region> regions;
};

}  // namespace render_capture
