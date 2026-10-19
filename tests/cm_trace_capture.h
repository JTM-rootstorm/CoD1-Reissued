#pragma once
// Captured collision traces (src/cm_trace.cpp), for tests/cm_trace_test.cpp.
// A capture file is a sequence of records, each a u32 tag, then:
//
//   "CMP1" (a clip map): u32 region count, then per region u32 VA, u32
//   size, size bytes. Everything a trace reads that does not change from
//   one trace to the next: the clip map's globals, its nodes, leaves,
//   planes, brushes, sides, shaders, leaf surfaces with their patch
//   collides and terrain, and the inline models. Written when the map
//   changes.
//   "CMT1" (a call): u32 function VA, u32 EAX, ECX, EDX, u32 kArgs stack
//   arguments, then regions as above: the clip map's globals again (the
//   checkcount and the temporary box model), the box brush with its sides
//   and planes, and what the arguments point to.
//   "CMR1" (after each call): the trace_t (0x30 bytes) the game got; for
//   point contents, the contents (EAX) and 0x2C zero bytes.
//
// Point contents calls (0x41A730, 0x41A820) are recorded the same way.
//
// Written by tests/cm_trace_capture.cpp (a cod1 built with
// -DCOD1_CM_CAPTURE=ON).

#include <cstdint>

namespace cm_capture {

inline constexpr uint32_t kMap = 0x31504D43;   // "CMP1"
inline constexpr uint32_t kCall = 0x31544D43;  // "CMT1"
inline constexpr uint32_t kResult = 0x31524D43;  // "CMR1"
inline constexpr int kArgs = 8;

inline constexpr uint32_t kTrace = 0x41CD60;
inline constexpr uint32_t kBoxTrace = 0x41D380;
inline constexpr uint32_t kTransformed = 0x41D3B0;
inline constexpr uint32_t kTransformedBoxTrace = 0x41D870;
// ECX p, EAX model; and ECX p, EAX origin, EDX angles, model on the stack.
inline constexpr uint32_t kPointContents = 0x41A730;
inline constexpr uint32_t kTransformedPointContents = 0x41A820;

// The clip map's globals: cm (0x14BD640 on) and the temporary box model.
inline constexpr uint32_t kGlobals = 0x14BD640, kGlobalsSize = 0x140;

}  // namespace cm_capture
