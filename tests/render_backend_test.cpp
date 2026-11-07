// The native renderer back end (src/render_backend.cpp) against the
// originals, lifted from the EXE at build time with `recomp_lift --only`
// (CMakeLists.txt) and run on SwARc's exact x87 model. CoDSP.exe is
// mapped as the game maps it, for its constants and globals. The renderer's
// qgl pointers (and ri.Printf, ri.Error and the element function of the
// strips) point at recorders that log each call: the GL entry point, its
// return address, ESP and stack arguments (and the four floats glFogfv
// reads), pop the arguments as the stdcall entry points do, and leave EAX,
// ECX and EDX changed. Each case runs one entry through the lifted original
// and the native code from the same guest memory, and both must leave the
// same bytes in the renderer's globals, the case's structures and the stack
// arguments, the same calls, and the same ESP, EBX, ESI, EDI, EBP and x87
// stack (top, empty registers, control word). Not compared: EAX, ECX, EDX,
// EFLAGS, the x87 status word and the stack below the return address
// (which both runs start from as the last case left it: the optimised
// iterators pass locals they leave unwritten on some paths), but for the
// shadow volumes' frames, which the native code leaves as the original.
//
//   render_backend_test EXE
//
// Random cases (mt19937, fixed seed): random cached GL state (state bits,
// client arrays, cull type, units, bound textures, environments, targets),
// cvars, images, fog colours (now and then zeros, denormals, infinities,
// NaNs or huge), and index lists for the strips that continue a strip or
// start a new one at random.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "runtime/guest_memory.h"
#include "win32/pe_image.h"
#include "x86/lifted.h"
#include "x86/x87.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;
PeImage* g_exe = nullptr;
}  // namespace recomp

namespace {

using recomp::Ctx;

recomp::GuestMemory g_mem;

uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }

// The entry points the functions call: the pointer they call through, its
// argument count (popped by the callee unless cdecl), and the argument
// pointing at floats the callee reads (with their count).
struct Gl {
  uint32_t ptr;
  const char* name;
  int args;
  bool cdecl = false;
  int ptr_arg = -1, floats = 0;
};
constexpr Gl kGl[] = {
    {0x1534F68, "glEnable", 1},
    {0x15349E0, "glDisable", 1},
    {0x153507C, "glBindTexture", 2},
    {0x153515C, "glTexEnvi", 3},
    {0x1535284, "glCullFace", 1},
    {0x1534844, "glDepthFunc", 1},
    {0x1534D68, "glDepthMask", 1},
    {0x1534D74, "glPolygonMode", 2},
    {0x1535208, "glAlphaFunc", 2},
    {0x1535050, "glColor3f", 3},
    {0x15350B0, "glPolygonOffset", 2},
    {0x1534ABC, "glBlendFunc", 2},
    {0x15349F4, "glFogfv", 2, false, 1, 4},
    {0x1534938, "glEnableClientState", 1},
    {0x153495C, "glDisableClientState", 1},
    {0x1534B78, "glClientActiveTextureARB", 1},
    {0x1535360, "glBegin", 1},
    {0x1534EE0, "glEnd", 0},
    {0x153513C, "glDrawElements", 4},
    {0x1535104, "glArrayElement", 1},
    {0x1534C14, "glActiveTextureARB", 1},
    {0x1534A10, "glTexEnvfv", 3, false, 2, 4},
    {0x15349B8, "glTexEnvf", 3},
    {0x15349A0, "glTexCoordPointer", 4},
    {0x1534C84, "glFogi", 2},
    {0x1535598, "glFogf", 2},
    {0x15351A0, "glHint", 2},
    {0x1535014, "qgl 0x1535014", 1},
    {0x153542C, "glColorPointer", 4},
    {0x153503C, "glNormalPointer", 3},
    {0x15354F0, "glVertexPointer", 4},
    {0x1534DDC, "glLockArraysEXT", 2},
    {0x1534C2C, "glUnlockArraysEXT", 0},
    {0x1534DB0, "glDrawRangeElementsEXT", 6},
    {0x1534DE4, "glBindBufferARB", 2},
    {0x1535390, "glArrayObjectATI", 6},
    {0x1534AE0, "glDrawElementArrayATI", 2},
    {0x153539C, "glDepthRange", 4},
    {0x1534EB0, "glColor4ubv", 1, false, 0, 1},
    {0x153501C, "glDrawArrays", 3},
    {0x1534DCC, "glNormal3f", 3},
    {0x1535744, "glLineWidth", 1},
    {0x1534DF8, "glColorMask", 4},
    {0x1534E7C, "glStencilFunc", 3},
    {0x1535428, "glStencilOp", 3},
    {0x1830CA0, "stage iterator", 1, true},
    {0x1549600, "ri.Printf", 2, true},
    {0x1549604, "ri.Error", 2, true},
    {0x1549570, "tris", 1, true},
    // The CRT's thread data (its square root's errno for a NaN): the
    // case's, through TlsGetValue.
    {0x5331D4, "GetLastError", 0},
    {0x5330F4, "TlsGetValue", 1},
    {0x533100, "SetLastError", 1},
    {0x1549684, "ri 0x1549684", 1, true},
    {0x154967C, "ri 0x154967C", 1, true},
    {0x1534A58, "glCombinerParameterfvNV", 2, false, 1, 4},
    {0x15352A0, "glCombinerParameteriNV", 2},
    {0x1535580, "glCombinerInputNV", 6},
    {0x1534810, "glCombinerOutputNV", 10},
    {0x1534954, "glCombinerStageParameterfvNV", 3, false, 2, 4},
    {0x1534AD0, "glFinalCombinerInputNV", 4},
    {0x1534CA4, "glBindProgramARB", 2},
    {0x1534AA4, "glProgramLocalParameter4fvARB", 3, false, 2, 4},
    {0x1534D24, "glProgramLocalParameter4fARB", 6},
    {0x1534E4C, "glTexSubImage2D", 9},
};
constexpr int kNumGl = sizeof(kGl) / sizeof(kGl[0]);
constexpr uint32_t kGlTarget = 0x7FFD0000;  // + 0x10 * index
// The stage iterator RB_EndSurface tests for (a sky's), recorded as the
// other iterators are.
constexpr uint32_t kSkyIterator = 0x4DD6E0;

struct CallRec {
  uint32_t target, ret, esp, args[11];
  uint32_t data[4];
  uint32_t regs[7];  // stubs: the registers their callee reads
  bool operator==(const CallRec&) const = default;
};
std::vector<CallRec> g_calls;

enum Reg { kEax = 1, kEcx = 2, kEdx = 4, kEbx = 8, kEsi = 16, kEdi = 32, kEbp = 64 };
constexpr int kAll = 127, kSaved = kEbx | kEsi | kEdi | kEbp;

// The lifted functions the native ones call (tests/render_backend_stubs.manifest),
// recorded as the GL calls are, with the registers each reads.
void Stub(Ctx& c, uint32_t va, int regs, int args) {
  CallRec r{};
  r.target = va;
  r.esp = c.esp;
  r.ret = Rd(c.esp);
  for (int k = 0; k < args; k++) r.args[k] = Rd(c.esp + 4 + 4 * uint32_t(k));
  const uint32_t all[7] = {c.eax, c.ecx, c.edx, c.ebx, c.esi, c.edi, c.ebp};
  for (int i = 0; i < 7; i++)
    if (regs & (1 << i)) r.regs[i] = all[i];
  g_calls.push_back(r);
  const uint32_t junk = 0xC0DE0000u + uint32_t(g_calls.size());
  c.eax = junk;
  c.ecx = junk ^ 1;
  c.edx = junk ^ 2;
  c.esp += 4;
}

}  // namespace

namespace recomp {
namespace gen {
// The stubs: the CRT's rand and srand (the debug colours; its state is
// the thread's).
#define COD1_STUB(va, regs, args) \
  void f_##va(Ctx& c) { Stub(c, 0x##va, regs, args); }
COD1_STUB(0051B5EC, 0, 0)
COD1_STUB(0051B5DF, 0, 1)
// Under the stages and the flush: ComputeTexCoords, setting a cvar,
// printing, va and fprintf, each with the registers the native code keeps
// as the original has them.
COD1_STUB(004F5940, kEax | kSaved, 0)
COD1_STUB(00431CF0, kEax | kSaved, 2)
COD1_STUB(0042E300, kSaved, 1)
COD1_STUB(00440940, kSaved, 2)
COD1_STUB(0051D45A, kSaved, 3)
// Under the combiners and the DXF file: GL_CheckErrors (the caller's
// name) and FS_Write (buffer, length, file).
COD1_STUB(004A5440, 0, 1)
COD1_STUB(004230E0, 0, 3)
#undef COD1_STUB
// sprintf (buffer, format, ...: the DXF file's face, its number, or its
// corners, three of an index and a double), writing a hex number of the
// arguments' hash into the buffer.
void f_0051A70E(Ctx& c) {
  const uint32_t buf = Rd(c.esp + 4);
  Stub(c, 0x51A70E, 0, Rd(c.esp + 8) == 0x53C0B0 ? 3 : 11);
  uint32_t h = 2166136261u;
  for (uint32_t v : g_calls.back().args) h = (h ^ v) * 16777619u;
  std::snprintf(g_mem.ptr<char>(buf), 9, "%x", h >> (h & 31));
}
// The immediate-mode batch's allocation (size), answering with the case's
// buffer.
uint32_t g_batch = 0;
// The CRT's thread data (TlsGetValue's answer).
uint32_t g_tiddata = 0;
void f_0042A480(Ctx& c) {
  Stub(c, 0x42A480, kSaved, 1);
  c.eax = g_batch;
}
// _stricmp, recorded and answering as the CRT's: 0 when the strings match
// but for case.
void f_0052AB6F(Ctx& c) {
  const uint32_t a = Rd(c.esp + 4), b = Rd(c.esp + 8);
  Stub(c, 0x52AB6F, kSaved, 2);
  c.eax = strcasecmp(g_mem.ptr<char>(a), g_mem.ptr<char>(b)) != 0;
}
// unzCloseCurrentFile, tolower and the memcpy 0x42B0A0, native in the
// game (src/unzip.cpp, src/crt.cpp, src/render.cpp): here only the stubbed
// functions' lifted bodies reach them.
void f_0049EAA0_lifted(Ctx& c);
void f_0049EAA0(Ctx& c) { f_0049EAA0_lifted(c); }
void f_0051B3DD_lifted(Ctx& c);
void f_0051B3DD(Ctx& c) { f_0051B3DD_lifted(c); }
void f_0042B0A0_lifted(Ctx& c);
void f_0042B0A0(Ctx& c) { f_0042B0A0_lifted(c); }
// The originals...
void f_004C89D0_lifted(Ctx& c);
void f_004C8A20_lifted(Ctx& c);
void f_004CB3B0_lifted(Ctx& c);
void f_004CB5D0_lifted(Ctx& c);
void f_004CB640_lifted(Ctx& c);
void f_004CB710_lifted(Ctx& c);
void f_004CBBE0_lifted(Ctx& c);
void f_004F3180_lifted(Ctx& c);
void f_004F32C0_lifted(Ctx& c);
void f_004F3330_lifted(Ctx& c);
void f_004F3410_lifted(Ctx& c);
void f_004F3810_lifted(Ctx& c);
void f_004F38B0_lifted(Ctx& c);
void f_004F3920_lifted(Ctx& c);
void f_004F3D80_lifted(Ctx& c);
void f_004C8830_lifted(Ctx& c);
void f_004C89A0_lifted(Ctx& c);
void f_004F5D50_lifted(Ctx& c);
void f_004F4090_lifted(Ctx& c);
void f_00509F20_lifted(Ctx& c);
void f_004F4FA0_lifted(Ctx& c);
void f_004F5DE0_lifted(Ctx& c);
void f_004F6070_lifted(Ctx& c);
void f_004F60E0_lifted(Ctx& c);
void f_004F7C30_lifted(Ctx& c);
void f_004F7D00_lifted(Ctx& c);
void f_004F39B0_lifted(Ctx& c);
void f_004F3F00_lifted(Ctx& c);
void f_004F6380_lifted(Ctx& c);
void f_004F66C0_lifted(Ctx& c);
void f_004F6A40_lifted(Ctx& c);
void f_004F6D70_lifted(Ctx& c);
void f_004F70F0_lifted(Ctx& c);
void f_004F73D0_lifted(Ctx& c);
void f_004F76B0_lifted(Ctx& c);
void f_004F7950_lifted(Ctx& c);
void f_004F30A0_lifted(Ctx& c);
void f_004F4BD0_lifted(Ctx& c);
void f_004F5F50_lifted(Ctx& c);
void f_00507F40_lifted(Ctx& c);
void f_005082D0_lifted(Ctx& c);
void f_0050A030_lifted(Ctx& c);
void f_0050A070_lifted(Ctx& c);
void f_0050A0E0_lifted(Ctx& c);
void f_0050A120_lifted(Ctx& c);
void f_0050A170_lifted(Ctx& c);
void f_0050A270_lifted(Ctx& c);
void f_0050AEC0_lifted(Ctx& c);
void f_0050B040_lifted(Ctx& c);
void f_004F2E60_lifted(Ctx& c);
void f_00508AE0_lifted(Ctx& c);
void f_00508C50_lifted(Ctx& c);
void f_00508DE0_lifted(Ctx& c);
void f_00508F00_lifted(Ctx& c);
void f_00509040_lifted(Ctx& c);
void f_005090E0_lifted(Ctx& c);
void f_00508800_lifted(Ctx& c);
void f_005095F0_lifted(Ctx& c);
void f_005098A0_lifted(Ctx& c);
void f_00509190_lifted(Ctx& c);
void f_0050A2B0_lifted(Ctx& c);
void f_0050A380_lifted(Ctx& c);
void f_0050A3D0_lifted(Ctx& c);
void f_0050A420_lifted(Ctx& c);
void f_0050A4D0_lifted(Ctx& c);
void f_0050A5C0_lifted(Ctx& c);
void f_0050A720_lifted(Ctx& c);
void f_0050A7C0_lifted(Ctx& c);
void f_0050A820_lifted(Ctx& c);
void f_0050A890_lifted(Ctx& c);
void f_0050A8D0_lifted(Ctx& c);
void f_0050A980_lifted(Ctx& c);
void f_0050A9B0_lifted(Ctx& c);
void f_0050AA30_lifted(Ctx& c);
void f_0050AA80_lifted(Ctx& c);
void f_0050AE10_lifted(Ctx& c);
void f_00508A50_lifted(Ctx& c);
void f_0050ABF0_lifted(Ctx& c);
void f_004FAAB0_lifted(Ctx& c);
void f_004FABD0_lifted(Ctx& c);
void f_004FACD0_lifted(Ctx& c);
void f_004FAF60_lifted(Ctx& c);
void f_004FB1B0_lifted(Ctx& c);
void f_004FB660_lifted(Ctx& c);
void f_004FB8C0_lifted(Ctx& c);
void f_004FBAF0_lifted(Ctx& c);
void f_004FBD50_lifted(Ctx& c);
void f_00503A60_lifted(Ctx& c);
void f_00503A80_lifted(Ctx& c);
void f_005016B0_lifted(Ctx& c);
void f_005039A0_lifted(Ctx& c);
void f_005033B0_lifted(Ctx& c);
void f_00503590_lifted(Ctx& c);
void f_00503760_lifted(Ctx& c);
void f_00500F20_lifted(Ctx& c);
void f_00502110_lifted(Ctx& c);
void f_00502AF0_lifted(Ctx& c);
void f_00502CE0_lifted(Ctx& c);
void f_005012D0_lifted(Ctx& c);
void f_00501240_lifted(Ctx& c);
void f_005014C0_lifted(Ctx& c);
void f_00502350_lifted(Ctx& c);
void f_00503010_lifted(Ctx& c);
void f_005030E0_lifted(Ctx& c);
void f_00503230_lifted(Ctx& c);
void f_00502450_lifted(Ctx& c);
void f_00501900_lifted(Ctx& c);
void f_00503860_lifted(Ctx& c);
void f_004F4AB0_lifted(Ctx& c);
void f_00500A60_lifted(Ctx& c);
void f_004F4A20_lifted(Ctx& c);
void f_004ADC20_lifted(Ctx& c);
void f_004ADEC0_lifted(Ctx& c);
void f_004F44A0_lifted(Ctx& c);
void f_004F4560_lifted(Ctx& c);
void f_004F4850_lifted(Ctx& c);
void f_004F3A70_lifted(Ctx& c);
void f_004F3C70_lifted(Ctx& c);
void f_00509500_lifted(Ctx& c);
void f_00508940_lifted(Ctx& c);
void f_004D3AA0_lifted(Ctx& c);
void f_004FCE80_lifted(Ctx& c);
void f_004FC960_lifted(Ctx& c);
void f_004FCA50_lifted(Ctx& c);
void f_004FC4E0_lifted(Ctx& c);
void f_004FCAE0_lifted(Ctx& c);
void f_004F4260_lifted(Ctx& c);
void f_004F41C0_lifted(Ctx& c);
// ...call each other, not the native code (tests/render_backend_native.cpp
// renames its entries).
void f_004C89D0(Ctx& c) { f_004C89D0_lifted(c); }
void f_004C8A20(Ctx& c) { f_004C8A20_lifted(c); }
void f_004CB3B0(Ctx& c) { f_004CB3B0_lifted(c); }
void f_004CB5D0(Ctx& c) { f_004CB5D0_lifted(c); }
void f_004CB640(Ctx& c) { f_004CB640_lifted(c); }
void f_004CB710(Ctx& c) { f_004CB710_lifted(c); }
void f_004CBBE0(Ctx& c) { f_004CBBE0_lifted(c); }
void f_004F3180(Ctx& c) { f_004F3180_lifted(c); }
void f_004F32C0(Ctx& c) { f_004F32C0_lifted(c); }
void f_004F3330(Ctx& c) { f_004F3330_lifted(c); }
void f_004F3410(Ctx& c) { f_004F3410_lifted(c); }
void f_004F3810(Ctx& c) { f_004F3810_lifted(c); }
void f_004F38B0(Ctx& c) { f_004F38B0_lifted(c); }
void f_004F3920(Ctx& c) { f_004F3920_lifted(c); }
void f_004F3D80(Ctx& c) { f_004F3D80_lifted(c); }
void f_004C8830(Ctx& c) { f_004C8830_lifted(c); }
void f_004C89A0(Ctx& c) { f_004C89A0_lifted(c); }
void f_004F5D50(Ctx& c) { f_004F5D50_lifted(c); }
void f_004F4090(Ctx& c) { f_004F4090_lifted(c); }
void f_00509F20(Ctx& c) { f_00509F20_lifted(c); }
void f_004F4FA0(Ctx& c) { f_004F4FA0_lifted(c); }
void f_004F5DE0(Ctx& c) { f_004F5DE0_lifted(c); }
void f_004F6070(Ctx& c) { f_004F6070_lifted(c); }
void f_004F60E0(Ctx& c) { f_004F60E0_lifted(c); }
void f_004F7C30(Ctx& c) { f_004F7C30_lifted(c); }
void f_004F7D00(Ctx& c) { f_004F7D00_lifted(c); }
void f_004F39B0(Ctx& c) { f_004F39B0_lifted(c); }
void f_004F3F00(Ctx& c) { f_004F3F00_lifted(c); }
void f_004F6380(Ctx& c) { f_004F6380_lifted(c); }
void f_004F66C0(Ctx& c) { f_004F66C0_lifted(c); }
void f_004F6A40(Ctx& c) { f_004F6A40_lifted(c); }
void f_004F6D70(Ctx& c) { f_004F6D70_lifted(c); }
void f_004F70F0(Ctx& c) { f_004F70F0_lifted(c); }
void f_004F73D0(Ctx& c) { f_004F73D0_lifted(c); }
void f_004F76B0(Ctx& c) { f_004F76B0_lifted(c); }
void f_004F7950(Ctx& c) { f_004F7950_lifted(c); }
void f_004F30A0(Ctx& c) { f_004F30A0_lifted(c); }
void f_004F4BD0(Ctx& c) { f_004F4BD0_lifted(c); }
void f_004F5F50(Ctx& c) { f_004F5F50_lifted(c); }
void f_00507F40(Ctx& c) { f_00507F40_lifted(c); }
void f_005082D0(Ctx& c) { f_005082D0_lifted(c); }
void f_0050A030(Ctx& c) { f_0050A030_lifted(c); }
void f_0050A070(Ctx& c) { f_0050A070_lifted(c); }
void f_0050A0E0(Ctx& c) { f_0050A0E0_lifted(c); }
void f_0050A120(Ctx& c) { f_0050A120_lifted(c); }
void f_0050A170(Ctx& c) { f_0050A170_lifted(c); }
void f_0050A270(Ctx& c) { f_0050A270_lifted(c); }
void f_0050AEC0(Ctx& c) { f_0050AEC0_lifted(c); }
void f_0050B040(Ctx& c) { f_0050B040_lifted(c); }
void f_004F2E60(Ctx& c) { f_004F2E60_lifted(c); }
void f_00508AE0(Ctx& c) { f_00508AE0_lifted(c); }
void f_00508C50(Ctx& c) { f_00508C50_lifted(c); }
void f_00508DE0(Ctx& c) { f_00508DE0_lifted(c); }
void f_00508F00(Ctx& c) { f_00508F00_lifted(c); }
void f_00509040(Ctx& c) { f_00509040_lifted(c); }
void f_005090E0(Ctx& c) { f_005090E0_lifted(c); }
void f_00508800(Ctx& c) { f_00508800_lifted(c); }
void f_005095F0(Ctx& c) { f_005095F0_lifted(c); }
void f_005098A0(Ctx& c) { f_005098A0_lifted(c); }
void f_00509190(Ctx& c) { f_00509190_lifted(c); }
void f_0050A2B0(Ctx& c) { f_0050A2B0_lifted(c); }
void f_0050A380(Ctx& c) { f_0050A380_lifted(c); }
void f_0050A3D0(Ctx& c) { f_0050A3D0_lifted(c); }
void f_0050A420(Ctx& c) { f_0050A420_lifted(c); }
void f_0050A4D0(Ctx& c) { f_0050A4D0_lifted(c); }
void f_0050A5C0(Ctx& c) { f_0050A5C0_lifted(c); }
void f_0050A720(Ctx& c) { f_0050A720_lifted(c); }
void f_0050A7C0(Ctx& c) { f_0050A7C0_lifted(c); }
void f_0050A820(Ctx& c) { f_0050A820_lifted(c); }
void f_0050A890(Ctx& c) { f_0050A890_lifted(c); }
void f_0050A8D0(Ctx& c) { f_0050A8D0_lifted(c); }
void f_0050A980(Ctx& c) { f_0050A980_lifted(c); }
void f_0050A9B0(Ctx& c) { f_0050A9B0_lifted(c); }
void f_0050AA30(Ctx& c) { f_0050AA30_lifted(c); }
void f_0050AA80(Ctx& c) { f_0050AA80_lifted(c); }
void f_0050AE10(Ctx& c) { f_0050AE10_lifted(c); }
void f_00508A50(Ctx& c) { f_00508A50_lifted(c); }
void f_0050ABF0(Ctx& c) { f_0050ABF0_lifted(c); }
void f_004FAAB0(Ctx& c) { f_004FAAB0_lifted(c); }
void f_004FABD0(Ctx& c) { f_004FABD0_lifted(c); }
void f_004FACD0(Ctx& c) { f_004FACD0_lifted(c); }
void f_004FAF60(Ctx& c) { f_004FAF60_lifted(c); }
void f_004FB1B0(Ctx& c) { f_004FB1B0_lifted(c); }
void f_004FB660(Ctx& c) { f_004FB660_lifted(c); }
void f_004FB8C0(Ctx& c) { f_004FB8C0_lifted(c); }
void f_004FBAF0(Ctx& c) { f_004FBAF0_lifted(c); }
void f_004FBD50(Ctx& c) { f_004FBD50_lifted(c); }
void f_00503A60(Ctx& c) { f_00503A60_lifted(c); }
void f_00503A80(Ctx& c) { f_00503A80_lifted(c); }
void f_005016B0(Ctx& c) { f_005016B0_lifted(c); }
void f_005039A0(Ctx& c) { f_005039A0_lifted(c); }
void f_005033B0(Ctx& c) { f_005033B0_lifted(c); }
void f_00503590(Ctx& c) { f_00503590_lifted(c); }
void f_00503760(Ctx& c) { f_00503760_lifted(c); }
void f_00500F20(Ctx& c) { f_00500F20_lifted(c); }
void f_00502110(Ctx& c) { f_00502110_lifted(c); }
void f_00502AF0(Ctx& c) { f_00502AF0_lifted(c); }
void f_00502CE0(Ctx& c) { f_00502CE0_lifted(c); }
void f_005012D0(Ctx& c) { f_005012D0_lifted(c); }
void f_00501240(Ctx& c) { f_00501240_lifted(c); }
void f_005014C0(Ctx& c) { f_005014C0_lifted(c); }
void f_00502350(Ctx& c) { f_00502350_lifted(c); }
void f_00503010(Ctx& c) { f_00503010_lifted(c); }
void f_005030E0(Ctx& c) { f_005030E0_lifted(c); }
void f_00503230(Ctx& c) { f_00503230_lifted(c); }
void f_00502450(Ctx& c) { f_00502450_lifted(c); }
void f_00501900(Ctx& c) { f_00501900_lifted(c); }
void f_00503860(Ctx& c) { f_00503860_lifted(c); }
void f_004F4AB0(Ctx& c) { f_004F4AB0_lifted(c); }
void f_00500A60(Ctx& c) { f_00500A60_lifted(c); }
void f_004F4A20(Ctx& c) { f_004F4A20_lifted(c); }
void f_004ADC20(Ctx& c) { f_004ADC20_lifted(c); }
void f_004ADEC0(Ctx& c) { f_004ADEC0_lifted(c); }
void f_004F44A0(Ctx& c) { f_004F44A0_lifted(c); }
void f_004F4560(Ctx& c) { f_004F4560_lifted(c); }
void f_004F4850(Ctx& c) { f_004F4850_lifted(c); }
void f_004F3A70(Ctx& c) { f_004F3A70_lifted(c); }
void f_004F3C70(Ctx& c) { f_004F3C70_lifted(c); }
void f_004FCE80(Ctx& c) { f_004FCE80_lifted(c); }
void f_004F4260(Ctx& c) { f_004F4260_lifted(c); }
void f_00509500(Ctx& c) { f_00509500_lifted(c); }
void f_00508940(Ctx& c) { f_00508940_lifted(c); }
void f_004D3AA0(Ctx& c) { f_004D3AA0_lifted(c); }
void f_004FC960(Ctx& c) { f_004FC960_lifted(c); }
void f_004FCA50(Ctx& c) { f_004FCA50_lifted(c); }
void f_004FC4E0(Ctx& c) { f_004FC4E0_lifted(c); }
void f_004FCAE0(Ctx& c) { f_004FCAE0_lifted(c); }
void f_004F41C0(Ctx& c) { f_004F41C0_lifted(c); }
void n_004C89D0(Ctx& c);
void n_004C8A20(Ctx& c);
void n_004CB3B0(Ctx& c);
void n_004CB5D0(Ctx& c);
void n_004CB640(Ctx& c);
void n_004CB710(Ctx& c);
void n_004CBBE0(Ctx& c);
void n_004F3180(Ctx& c);
void n_004F32C0(Ctx& c);
void n_004F3330(Ctx& c);
void n_004F3410(Ctx& c);
void n_004F3810(Ctx& c);
void n_004F38B0(Ctx& c);
void n_004F3920(Ctx& c);
void n_004F3D80(Ctx& c);
void n_004C8830(Ctx& c);
void n_004C89A0(Ctx& c);
void n_004F5D50(Ctx& c);
void n_004F4090(Ctx& c);
void n_00509F20(Ctx& c);
void n_004F4FA0(Ctx& c);
void n_004F5DE0(Ctx& c);
void n_004F6070(Ctx& c);
void n_004F60E0(Ctx& c);
void n_004F7C30(Ctx& c);
void n_004F7D00(Ctx& c);
void n_004F39B0(Ctx& c);
void n_004F3F00(Ctx& c);
void n_004F6380(Ctx& c);
void n_004F66C0(Ctx& c);
void n_004F6A40(Ctx& c);
void n_004F6D70(Ctx& c);
void n_004F70F0(Ctx& c);
void n_004F73D0(Ctx& c);
void n_004F76B0(Ctx& c);
void n_004F7950(Ctx& c);
void n_004F30A0(Ctx& c);
void n_004F4BD0(Ctx& c);
void n_004F5F50(Ctx& c);
void n_00507F40(Ctx& c);
void n_005082D0(Ctx& c);
void n_0050A030(Ctx& c);
void n_0050A070(Ctx& c);
void n_0050A0E0(Ctx& c);
void n_0050A120(Ctx& c);
void n_0050A170(Ctx& c);
void n_0050A270(Ctx& c);
void n_0050AEC0(Ctx& c);
void n_0050B040(Ctx& c);
void n_004F2E60(Ctx& c);
void n_00508AE0(Ctx& c);
void n_00508C50(Ctx& c);
void n_00508DE0(Ctx& c);
void n_00508F00(Ctx& c);
void n_00509040(Ctx& c);
void n_005090E0(Ctx& c);
void n_00508800(Ctx& c);
void n_005095F0(Ctx& c);
void n_005098A0(Ctx& c);
void n_00509190(Ctx& c);
void n_0050A2B0(Ctx& c);
void n_0050A380(Ctx& c);
void n_0050A3D0(Ctx& c);
void n_0050A420(Ctx& c);
void n_0050A4D0(Ctx& c);
void n_0050A5C0(Ctx& c);
void n_0050A720(Ctx& c);
void n_0050A7C0(Ctx& c);
void n_0050A820(Ctx& c);
void n_0050A890(Ctx& c);
void n_0050A8D0(Ctx& c);
void n_0050A980(Ctx& c);
void n_0050A9B0(Ctx& c);
void n_0050AA30(Ctx& c);
void n_0050AA80(Ctx& c);
void n_0050AE10(Ctx& c);
void n_00508A50(Ctx& c);
void n_0050ABF0(Ctx& c);
void n_004FAAB0(Ctx& c);
void n_004FABD0(Ctx& c);
void n_004FACD0(Ctx& c);
void n_004FAF60(Ctx& c);
void n_004FB1B0(Ctx& c);
void n_004FB660(Ctx& c);
void n_004FB8C0(Ctx& c);
void n_004FBAF0(Ctx& c);
void n_004FBD50(Ctx& c);
void n_00503A60(Ctx& c);
void n_00503A80(Ctx& c);
void n_005016B0(Ctx& c);
void n_005039A0(Ctx& c);
void n_005033B0(Ctx& c);
void n_00503590(Ctx& c);
void n_00503760(Ctx& c);
void n_00500F20(Ctx& c);
void n_00502110(Ctx& c);
void n_00502AF0(Ctx& c);
void n_00502CE0(Ctx& c);
void n_005012D0(Ctx& c);
void n_00501240(Ctx& c);
void n_005014C0(Ctx& c);
void n_00502350(Ctx& c);
void n_00503010(Ctx& c);
void n_005030E0(Ctx& c);
void n_00503230(Ctx& c);
void n_00502450(Ctx& c);
void n_00501900(Ctx& c);
void n_00503860(Ctx& c);
void n_004F4AB0(Ctx& c);
void n_00500A60(Ctx& c);
void n_004F4A20(Ctx& c);
void n_004ADC20(Ctx& c);
void n_004ADEC0(Ctx& c);
void n_004F44A0(Ctx& c);
void n_004F4560(Ctx& c);
void n_004F4850(Ctx& c);
void n_004F3A70(Ctx& c);
void n_004F3C70(Ctx& c);
void n_00509500(Ctx& c);
void n_00508940(Ctx& c);
void n_004D3AA0(Ctx& c);
void n_004FCE80(Ctx& c);
void n_004FC960(Ctx& c);
void n_004FCA50(Ctx& c);
void n_004FC4E0(Ctx& c);
void n_004FCAE0(Ctx& c);
void n_004F4260(Ctx& c);
void n_004F41C0(Ctx& c);
}  // namespace gen

void Dispatch(Ctx& c, uint32_t target, uint32_t va) {
  if (target == kSkyIterator) {
    Stub(c, kSkyIterator, kSaved, 1);
    return;
  }
  const uint32_t i = (target - kGlTarget) / 0x10;
  if (target < kGlTarget || i >= uint32_t(kNumGl) || target != kGlTarget + 0x10 * i) {
    std::printf("unexpected call to %08X from %08X\n", target, va);
    std::abort();
  }
  const Gl& gl = kGl[i];
  CallRec r{};
  r.target = target;
  r.esp = c.esp;
  r.ret = Rd(c.esp);
  // ri.Error's texture environment message has the environment after it.
  const int n = gl.ptr == 0x1549604 && Rd(c.esp + 8) == 0x54303C ? 3 : gl.args;
  for (int k = 0; k < n; k++) r.args[k] = Rd(c.esp + 4 + 4 * uint32_t(k));
  if (gl.ptr_arg >= 0)
    for (int k = 0; k < gl.floats; k++) r.data[k] = Rd(r.args[gl.ptr_arg] + 4 * uint32_t(k));
  // The texels glTexSubImage2D sends (width x height RGBA bytes, at most
  // the water's buffer of 256 x 256), hashed.
  if (gl.ptr == 0x1534E4C) {
    uint32_t h = 2166136261u;
    const uint32_t n = std::min(4 * (r.args[4] & 0xFFFF) * (r.args[5] & 0xFFFF), 0x40000u);
    for (uint32_t i = 0; i < n; i++) h = (h ^ g_mem.read<uint8_t>(r.args[8] + i)) * 16777619u;
    r.data[0] = h;
  }
  // The tessellator's shader, stages and their count it draws with.
  if (gl.ptr == 0x1549570) {
    r.data[0] = Rd(0x17F0C70);
    r.data[1] = Rd(0x1830CA4);
    r.data[2] = Rd(0x1830C9C);
  }
  g_calls.push_back(r);
  const uint32_t junk = 0xC0DE0000u + uint32_t(g_calls.size());
  c.eax = junk;
  c.ecx = junk ^ 1;
  c.edx = junk ^ 2;
  if (gl.ptr == 0x5330F4) c.eax = recomp::gen::g_tiddata;
  // A stage iterator now and then leaves no indexes.
  if (gl.ptr == 0x1830CA0 && (Rd(0x17F0C88) & 1)) Wr(0x17F0C84, 0);
  c.esp += 4 + (gl.cdecl ? 0 : 4 * uint32_t(gl.args));
}
void Resume(Ctx&, GuestResume r) {
  std::printf("unexpected resume to %08X\n", r.target);
  std::abort();
}
void Unimplemented(Ctx&, uint32_t va, const char* what) {
  std::printf("unimplemented %08X %s\n", va, what);
  std::abort();
}
void BadJump(Ctx&, uint32_t from, uint32_t to) {
  std::printf("bad jump %08X -> %08X\n", from, to);
  std::abort();
}
void Unreachable(Ctx&, uint32_t va) {
  std::printf("unreachable %08X\n", va);
  std::abort();
}
void Trap(Ctx&, uint32_t va) {
  std::printf("trap %08X\n", va);
  std::abort();
}
void RaiseDivideError(Ctx&, uint32_t va) {
  std::printf("divide error %08X\n", va);
  std::abort();
}
void Cpuid(Ctx&) { std::abort(); }
uint64_t Rdtsc(Ctx&) { std::abort(); }
void Probe(Ctx&, uint32_t, const char*) {}
void FatalF(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::abort();
}

}  // namespace recomp

namespace {

using Fn = void (*)(Ctx&);

struct Entry {
  uint32_t va;
  Fn lifted, native;
  const char* name;
  uint32_t frame = 0;  // the bytes below the return address compared
  // The x87 registers it uses at most: the game calls it with an empty
  // stack, the cases with up to 2 values below, as many as leave it room.
  uint32_t x87 = 6;
};
#define COD1_ENTRY(va, name, ...) \
  {0x##va, recomp::gen::f_##va##_lifted, recomp::gen::n_##va, name __VA_OPT__(,) __VA_ARGS__}
const Entry kEntries[] = {
    COD1_ENTRY(004C89D0, "fog on"),        COD1_ENTRY(004C8A20, "fog colour"),
    COD1_ENTRY(004CB3B0, "GL_Bind"),       COD1_ENTRY(004CB5D0, "cull"),
    COD1_ENTRY(004CB640, "texture env"),   COD1_ENTRY(004CB710, "state bits"),
    COD1_ENTRY(004CBBE0, "client arrays"), COD1_ENTRY(004F3180, "strips"),
    COD1_ENTRY(004F32C0, "draw elements"), COD1_ENTRY(004F3330, "bundle image"),
    COD1_ENTRY(004F3410, "bind bundle"),   COD1_ENTRY(004F3810, "unit off"),
    COD1_ENTRY(004F38B0, "coords on"),     COD1_ENTRY(004F3920, "coords off"),
    COD1_ENTRY(004F3D80, "stage textures"), COD1_ENTRY(004C8830, "fog params"),
    COD1_ENTRY(004C89A0, "fog off"),       COD1_ENTRY(004F5D50, "flush fog"),
    COD1_ENTRY(004F4090, "units reset"),   COD1_ENTRY(00509F20, "deforms"),
    COD1_ENTRY(004F4FA0, "stage colours", 0, 7), COD1_ENTRY(004F5DE0, "stage draw", 0, 7),
    COD1_ENTRY(004F6070, "stage loop", 0, 7),    COD1_ENTRY(004F60E0, "generic iterator", 0, 7),
    COD1_ENTRY(004F7C30, "optimised"),     COD1_ENTRY(004F7D00, "end surface"),
    COD1_ENTRY(004F39B0, "object coords"), COD1_ENTRY(004F3F00, "object textures"),
    COD1_ENTRY(004F6380, "shader memory"), COD1_ENTRY(004F66C0, "shader buffer"),
    COD1_ENTRY(004F6A40, "shader object"), COD1_ENTRY(004F6D70, "shader NV"),
    COD1_ENTRY(004F70F0, "world memory"),  COD1_ENTRY(004F73D0, "world buffer"),
    COD1_ENTRY(004F76B0, "world object"),  COD1_ENTRY(004F7950, "world memory 2"),
    COD1_ENTRY(004F30A0, "axes"),          COD1_ENTRY(004F4BD0, "dlight"),
    COD1_ENTRY(004F5F50, "dlight pass", 0, 7),   COD1_ENTRY(00507F40, "silhouette", 0x30),
    COD1_ENTRY(005082D0, "shadow volume", 0x60),
    COD1_ENTRY(0050A030, "entity colour"),  COD1_ENTRY(0050A070, "one minus entity"),
    COD1_ENTRY(0050A0E0, "entity alpha"),   COD1_ENTRY(0050A120, "one minus alpha"),
    COD1_ENTRY(0050A170, "wave colour"),    COD1_ENTRY(0050A270, "wave alpha"),
    COD1_ENTRY(0050AEC0, "specular", 0, 7),      COD1_ENTRY(0050B040, "diffuse"),
    COD1_ENTRY(004F2E60, "debug colour"),
    COD1_ENTRY(00508AE0, "deform wave"),    COD1_ENTRY(00508C50, "deform wave st"),
    COD1_ENTRY(00508DE0, "deform normals"), COD1_ENTRY(00508F00, "deform noise", 0, 8),
    COD1_ENTRY(00509040, "deform bulge"),   COD1_ENTRY(005090E0, "deform move"),
    COD1_ENTRY(00508800, "deform shadow"),  COD1_ENTRY(005095F0, "deform sprite"),
    COD1_ENTRY(005098A0, "deform sprite2"), COD1_ENTRY(00509190, "deform text"),
    COD1_ENTRY(0050A2B0, "tc environment"),  COD1_ENTRY(0050A380, "tc from view"),
    COD1_ENTRY(0050A3D0, "tc to view"),      COD1_ENTRY(0050A420, "tc reflect"),
    COD1_ENTRY(0050A4D0, "tc light"),        COD1_ENTRY(0050A5C0, "tc half"),
    COD1_ENTRY(0050A720, "tc view plus"),    COD1_ENTRY(0050A7C0, "tc axes"),
    COD1_ENTRY(0050A820, "tc axes view"),    COD1_ENTRY(0050A890, "tm swap"),
    COD1_ENTRY(0050A8D0, "tm turbulence"),   COD1_ENTRY(0050A980, "tm scale"),
    COD1_ENTRY(0050A9B0, "tm scroll"),       COD1_ENTRY(0050AA30, "tm transform"),
    COD1_ENTRY(0050AA80, "tm rotate"),       COD1_ENTRY(0050AE10, "tm into axes"),
    COD1_ENTRY(00508A50, "tm stretch"),      COD1_ENTRY(0050ABF0, "vertex axes"),
    COD1_ENTRY(004FAAB0, "static model"),    COD1_ENTRY(004FABD0, "static cached"),
    COD1_ENTRY(004FACD0, "static T2V3 ARB"), COD1_ENTRY(004FAF60, "static T2N3V3 ARB"),
    COD1_ENTRY(004FB1B0, "static ATI"),      COD1_ENTRY(004FB660, "static T2V3 NV"),
    COD1_ENTRY(004FB8C0, "static T2N3V3"),   COD1_ENTRY(004FBAF0, "static T2V3"),
    COD1_ENTRY(004FBD50, "static T2N3V3 NV"),
    COD1_ENTRY(00503A60, "bad surface", 0, 0), COD1_ENTRY(00503A80, "skipped surface", 0, 0),
    COD1_ENTRY(005016B0, "polygon"),         COD1_ENTRY(005039A0, "entity surface"),
    COD1_ENTRY(005033B0, "surface 24"),      COD1_ENTRY(00503590, "surface 25 dlights"),
    COD1_ENTRY(00503760, "surface 25"),
    COD1_ENTRY(00500F20, "quad"),             COD1_ENTRY(00502110, "line quad"),
    COD1_ENTRY(00502AF0, "beam quad"),        COD1_ENTRY(00502CE0, "rings"),
    COD1_ENTRY(005012D0, "entity 4"),         COD1_ENTRY(00501240, "entity 5"),
    COD1_ENTRY(005014C0, "entity 12"),        COD1_ENTRY(00502350, "entity 13"),
    COD1_ENTRY(00503010, "entity 9"),         COD1_ENTRY(005030E0, "entity 7"),
    COD1_ENTRY(00503230, "entity 10"),        COD1_ENTRY(00502450, "entity 15"),
    COD1_ENTRY(00501900, "entity 6"),         COD1_ENTRY(00503860, "entity other"),
    COD1_ENTRY(004F4AB0, "begin surface"),    COD1_ENTRY(00500A60, "check overflow"),
    COD1_ENTRY(004F4A20, "tess arrays"),      COD1_ENTRY(004ADC20, "batch begin"),
    COD1_ENTRY(004ADEC0, "batch vertex"),     COD1_ENTRY(004F44A0, "debug tris"),
    COD1_ENTRY(004F4560, "debug axes"),       COD1_ENTRY(004F4850, "debug normals"),
    COD1_ENTRY(004F3A70, "combiners"),        COD1_ENTRY(004F3C70, "vertex program"),
    COD1_ENTRY(00509500, "entity frame"),     COD1_ENTRY(00508940, "wave table"),
    COD1_ENTRY(004D3AA0, "noise", 0, 8),      COD1_ENTRY(004FCE80, "water", 0, 8),
    COD1_ENTRY(004FC960, "water spectrum"),   COD1_ENTRY(004FCA50, "water transform", 0, 8),
    COD1_ENTRY(004FC4E0, "FFT", 0, 8),        COD1_ENTRY(004FCAE0, "water texels"),
    COD1_ENTRY(004F4260, "DXF", 0, 8),        COD1_ENTRY(004F41C0, "projection"),
};
#undef COD1_ENTRY
constexpr int kNumEntries = sizeof(kEntries) / sizeof(kEntries[0]);

// Random structures live in the arena, reset per case; the stack below.
constexpr uint32_t kArena = 0x70000000, kArenaSize = 0x100000;
constexpr uint32_t kStack = 0x6FF00000, kStackSize = 0x100000;

struct Window {
  uint32_t va, size;
};
// The globals the functions read and write: the renderer's GL state and
// qgl pointers, the refdef and counters, the world's fog, the strip
// counters, and the first page (a null image's fields).
const Window kGlobals[] = {
    {0x1534000, 0x3000}, {0x1549000, 0x1000}, {0x13F9000, 0x1000},
    {0x11E0000, 0x1000}, {0x17F0000, 0x1000}, {0x154D000, 0x1000}, {0x0, 0x100},
    {0x1830C00, 0x100},
    // The tessellator's positions (with the shadow volume's copies),
    // vertex colours and normals, and the stage colours (as many as the
    // cases' vertices use).
    {0x1630C40, 0x1000}, {0x1690C40, 0x200}, {0x1698C40, 0x600}, {0x16E0C40, 0x100},
    // The vertices' two axes, the dlight's texture coordinates (the second
    // set's) and the first set's, and the indexes (the texts' quads reach
    // 124 vertices, a cylinder's sides 192 indexes).
    {0x16B0C40, 0x200}, {0x16C8C40, 0x200}, {0x1670C40, 0x400}, {0x1650C40, 0x400},
    {0x1618C40, 0x400},
    // The triangles' facing, the vertices' edges and their counts.
    {0x11E16D8, 0x100}, {0x11F16D8, 0x2900}, {0x13F16D8, 0x100},
    // The tessellator's ends, which a quad near its limits writes past the
    // windows above and RB_EndSurface's sentinels read: the last indexes,
    // the positions of vertices 0x1FC0 to 0x2040 at either stride, their
    // texture coordinates and colours.
    {0x162FC40, 0x1000}, {0x1650840, 0x800}, {0x1670440, 0x1000},
    {0x1660A40, 0x400}, {0x1680A40, 0x400}, {0x1698B40, 0x200},
};

struct Stats {
  int cases = 0, failures = 0, calls = 0, by_entry[kNumEntries] = {};
};

std::vector<uint8_t> Save(const std::vector<Window>& w) {
  std::vector<uint8_t> out;
  for (const Window& x : w) out.insert(out.end(), g_mem.ptr<uint8_t>(x.va), g_mem.ptr<uint8_t>(x.va) + x.size);
  return out;
}
void Load(const std::vector<Window>& w, const std::vector<uint8_t>& in) {
  size_t at = 0;
  for (const Window& x : w) {
    std::memcpy(g_mem.ptr<uint8_t>(x.va), in.data() + at, x.size);
    at += x.size;
  }
}

struct Call {
  int entry;
  uint32_t preload = 0;  // x87 values below the entry's
  uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0;
  std::vector<uint32_t> args;
};

void PrintCall(const char* side, size_t i, const CallRec& r) {
  const uint32_t k = (r.target - kGlTarget) / 0x10;
  std::printf("  %zu %s: %s %08X ret %08X esp %08X args", i, side,
              k < uint32_t(kNumGl) ? kGl[k].name : "?", r.target, r.ret, r.esp);
  for (uint32_t a : r.args) std::printf(" %08X", a);
  std::printf(" data");
  for (uint32_t d : r.data) std::printf(" %08X", d);
  std::printf(" regs");
  for (uint32_t v : r.regs) std::printf(" %08X", v);
  std::printf("\n");
}

void Run(const Call& k, uint32_t arena_used, std::mt19937& g, Stats& s) {
  const Entry& e = kEntries[k.entry];
  std::vector<Window> w(std::begin(kGlobals), std::end(kGlobals));
  w.push_back({kArena, arena_used});
  const uint32_t esp = kStack + kStackSize - 0x1000;
  Wr(esp, 0xDEAD0000);
  for (size_t i = 0; i < k.args.size(); i++) Wr(esp + 4 + 4 * uint32_t(i), k.args[i]);
  w.push_back({esp, 4 + 4 * uint32_t(k.args.size())});
  if (e.frame) w.push_back({esp - e.frame, e.frame});
  const std::vector<uint8_t> before = Save(w);
  const std::vector<Window> below = {{esp - 0x20000, 0x20000}};
  const std::vector<uint8_t> stack = Save(below);
  Ctx c0{};
  c0.eax = k.eax;
  c0.ecx = k.ecx ? k.ecx : uint32_t(g());
  c0.edx = k.edx;
  c0.ebx = k.ebx ? k.ebx : uint32_t(g());
  c0.esi = k.esi;
  c0.edi = k.edi;
  c0.ebp = g();
  c0.esp = esp;
  recomp::x87::Init(c0.fpu);
  recomp::x87::SetControlWord(c0.fpu, 0x027F);
  for (uint32_t i = k.preload; i; i--) recomp::x87::LoadF32(c0.fpu, g());
  c0.mxcsr = recomp::sse::kMxcsrDefault;

  Ctx cl = c0;
  g_calls.clear();
  e.lifted(cl);
  const std::vector<uint8_t> after = Save(w);
  const std::vector<CallRec> lcalls = g_calls;
  Load(w, before);
  Load(below, stack);
  Ctx cn = c0;
  g_calls.clear();
  e.native(cn);
  const std::vector<uint8_t> got = Save(w);

  s.cases++;
  s.by_entry[k.entry]++;
  s.calls += int(lcalls.size());
  bool ok = cn.esp == cl.esp && cn.ebp == cl.ebp && cn.ebx == cl.ebx && cn.esi == cl.esi &&
            cn.edi == cl.edi && cn.fpu.top == cl.fpu.top && cn.fpu.empty == cl.fpu.empty &&
            cn.fpu.cw == cl.fpu.cw &&
            ((e.va != 0x4F3330 && e.va != 0x508940) || cn.eax == cl.eax);
  // The x87 registers in use (the noise's value; the caller's below).
  for (int r = 0; r < 8; r++)
    if (!(cl.fpu.empty & (1 << r)) &&
        (cl.fpu.r[r].sig != cn.fpu.r[r].sig || cl.fpu.r[r].se != cn.fpu.r[r].se)) {
      if (s.failures < 20) std::printf("%s: x87 register %d differs\n", e.name, r);
      ok = false;
    }
  if (!ok && s.failures < 20)
    std::printf("%s: registers differ (esp %08X/%08X ebx %08X/%08X esi %08X/%08X edi "
                "%08X/%08X ebp %08X/%08X top %d/%d empty %02X/%02X)\n",
                e.name, cl.esp, cn.esp, cl.ebx, cn.ebx, cl.esi, cn.esi, cl.edi, cn.edi, cl.ebp,
                cn.ebp, cl.fpu.top, cn.fpu.top, unsigned(cl.fpu.empty), unsigned(cn.fpu.empty));
  if (lcalls != g_calls) {
    if (s.failures < 20) {
      std::printf("%s: calls differ (%zu lifted, %zu native)\n", e.name, lcalls.size(),
                  g_calls.size());
      for (size_t i = 0; i < std::max(lcalls.size(), g_calls.size()); i++) {
        const bool have_l = i < lcalls.size(), have_n = i < g_calls.size();
        if (have_l && have_n && lcalls[i] == g_calls[i]) continue;
        if (have_l) PrintCall("lifted", i, lcalls[i]);
        if (have_n) PrintCall("native", i, g_calls[i]);
        break;
      }
    }
    ok = false;
  }
  size_t at = 0;
  for (const Window& x : w) {
    for (uint32_t i = 0; i < x.size; i++)
      if (got[at + i] != after[at + i]) {
        if (s.failures < 20) {
          const uint32_t a = (x.va + i) & ~3u;
          const size_t o = at + (a - x.va);
          uint32_t lv = 0, nv = 0;
          std::memcpy(&lv, after.data() + o, 4);
          std::memcpy(&nv, got.data() + o, 4);
          std::printf("%s: %08X lifted %08X native %08X\n", e.name, a, lv, nv);
        }
        ok = false;
        break;
      }
    at += x.size;
  }
  if (!ok) s.failures++;
}

// ---- random state -------------------------------------------------------------

struct Arena {
  uint32_t top = kArena;
  uint32_t Alloc(uint32_t size) {
    const uint32_t va = top;
    top += (size + 15) & ~15u;
    return va;
  }
  void Reset() {
    std::memset(g_mem.ptr<uint8_t>(kArena), 0, top - kArena);
    top = kArena;
  }
};

uint32_t Pick(std::mt19937& g, std::initializer_list<uint32_t> v) {
  return v.begin()[g() % v.size()];
}

uint32_t Float(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}

// A float's bits: ordinary values, now and then special ones.
uint32_t RandFloat(std::mt19937& g) {
  switch (g() % 16) {
    case 0: return 0;
    case 1: return 0x80000000;
    case 2: return g() & 0x807FFFFF;               // denormal
    case 3: return 0x7F800000 | (g() & 0x80000000);  // infinity
    case 4: return 0x7FC00000 | (g() & 0x803FFFFF);  // QNaN
    case 5: return 0x7F800001 | (g() & 0x803FFFFE);  // SNaN
    case 6: return 0x7F000000 | (g() & 0x80FFFFFF);  // huge
    default: {
      float f = std::ldexp(float(g() % 2001) / 1000.0f - 1.0f, int(g() % 9) - 4);
      uint32_t b;
      std::memcpy(&b, &f, 4);
      return b;
    }
  }
}

// A float of about 10^6, 1 or 10^-3, either sign: sums of such values
// depend on their order.
uint32_t Mixed(std::mt19937& g) {
  const float m = std::bit_cast<float>(Pick(g, {Float(1e6f), Float(1.0f), Float(1e-3f)}));
  return Float(float(int(g() % 2001) - 1000) / 1000.0f * m);
}

// Positions that give every edge a length (below FLT_MAX): within 1000 of
// 0, now and then on a grid (equal lengths) or of mixed magnitudes; and
// finite view and entity axes (a NaN length takes the CRT's square root to
// errno, which the lifted CRT reaches through the thread's data).
void Positions(std::mt19937& g) {
  const uint32_t kind = g() % 4;
  for (uint32_t i = 0; i < 0x1000; i += 4)
    Wr(0x1630C40 + i, kind == 0 ? Float(float(int(g() % 5) - 2)) : kind == 1 ? Mixed(g) : Float(float(int(g() % 200001) - 100000) / 100.0f));
  for (uint32_t i = 0; i < 0xC; i += 4) Wr(0x1549834 + i, Float(float(int(g() % 2001) - 1000) / 1000.0f));
  const bool mixed = g() % 4 == 0;
  for (uint32_t i = 0; i < 0x24; i += 4) Wr(0x1549A94 + i, mixed ? Mixed(g) : Float(float(int(g() % 2001) - 1000) / 1000.0f));
}

uint32_t Cvar(Arena& a, std::mt19937& g, uint32_t value) {
  const uint32_t v = a.Alloc(0x40);
  Wr(v + 0x1C, RandFloat(g));
  Wr(v + 0x20, value);
  return v;
}

uint32_t Image(Arena& a, std::mt19937& g) {
  const uint32_t im = a.Alloc(0x60);
  Wr(im + 0x54, Pick(g, {0xDE1, 0xDE1, 0x8513, 0, uint32_t(g())}));
  Wr(im + 0x58, g() % 6);
  Wr(im + 0x5C, g());
  return im;
}

// State bits: mostly the meaningful fields, now and then anything.
uint32_t StateBits(std::mt19937& g) {
  if (g() % 8 == 0) return g();
  uint32_t b = 0;
  if (g() % 2) b |= (g() % 11) | ((g() % 10) << 4);
  b |= g() & 0x37F100;  // depth mask, line mode, depth, lighting, fog, ext
  b |= (g() % 4 == 0 ? g() & 0xE000 : 0);
  b |= (g() % 8) << 28;
  b |= g() & 0x3C00000;
  return b;
}

// A waveform: its type (1-5 the tables, 6 the noise, now and then
// another), base, amplitude, phase and frequency.
// x87 values below the case's entry: none lets its callees use all eight
// (the noise and the water's FFT do; the game calls them at an empty
// stack), so those are built only then.
uint32_t g_preload = 0;

void Wave(std::mt19937& g, uint32_t w) {
  uint32_t type = g() % 16 ? 1 + g() % 6 : g() % 8;
  if (g_preload && type == 6) type = 1;
  Wr(w, type);
  for (uint32_t i = 4; i < 0x14; i += 4)
    Wr(w + i, g() % 4 ? Float(float(int(g() % 4001) - 2000) / 1000.0f) : RandFloat(g));
}

// A float about a lattice: within 1000 of 0 in hundredths (now and then
// exactly on a half or an integer), now and then any.
uint32_t NoiseCoord(std::mt19937& g) {
  switch (g() % 8) {
    case 0: return RandFloat(g);
    case 1: return Float(float(int(g() % 2001) - 1000) + 0.5f);
    case 2: return Float(float(int(g() % 2001) - 1000));
    default: return Float(float(int(g() % 200001) - 100000) / 100.0f);
  }
}

// A power of two from 4 to 64, now and then 128 (small: 4 to 16).
uint32_t WaterSize(std::mt19937& g, bool small) {
  return small ? 4u << (g() % 3) : g() % 16 ? 4u << (g() % 5) : 128u;
}

// Complex floats (n of them) in the arena: ordinary, now and then special.
uint32_t Complex(Arena& a, std::mt19937& g, uint32_t n) {
  const uint32_t v = a.Alloc(8 * n);
  const bool special = g() % 8 == 0;
  for (uint32_t i = 0; i < 2 * n; i++)
    Wr(v + 4 * i, special && g() % 16 == 0 ? RandFloat(g) : Float(float(int(g() % 20001) - 10000) / 1000.0f));
  return v;
}

// The water's generator: its image (of its width and height; past them
// glTexSubImage2D would read the stack under the texels), frame
// stamp (now and then this frame's), amplitudes, frequencies (now and
// then 0, -0 or special) and size.
uint32_t Water(Arena& a, std::mt19937& g, bool small = false) {
  const uint32_t gen = a.Alloc(0x18), w = WaterSize(g, small), h = g() % 8 ? w : WaterSize(g, small);
  const uint32_t im = Image(a, g);
  g_mem.write<uint16_t>(im + 0x40, uint16_t(w));
  g_mem.write<uint16_t>(im + 0x42, uint16_t(h));
  Wr(gen, im);
  Wr(gen + 4, g() % 8 ? uint32_t(g()) : Rd(0x1535A24));
  Wr(gen + 8, Complex(a, g, w * h));
  const uint32_t f = a.Alloc(4 * w * h);
  for (uint32_t i = 0; i < w * h; i++)
    Wr(f + 4 * i, Pick(g, {0, 0x80000000, RandFloat(g), Float(float(g() % 10000) / 1000.0f),
                           Float(float(g() % 10000) / 1000.0f)}));
  Wr(gen + 0xC, f);
  Wr(gen + 0x10, w);
  Wr(gen + 0x14, h);
  return gen;
}

// Register combiners: anything, their count small (now and then
// negative), per-stage constants on or off.
uint32_t Combiners(Arena& a, std::mt19937& g) {
  const uint32_t cb = a.Alloc(0x600);
  for (uint32_t i = 0; i < 0x600; i += 4) Wr(cb + i, i < 0x20 && g() % 4 ? RandFloat(g) : uint32_t(g()));
  Wr(cb + 0x5F8, g() % 8 ? g() % 9 : uint32_t(g()) | 0x80000000u);
  Wr(cb + 0x5FC, g() % 2 ? 0 : uint32_t(g()));
  return cb;
}

// A vertex program (now and then the one bound).
uint32_t Program(Arena& a, std::mt19937& g) {
  const uint32_t prog = a.Alloc(0x50);
  Wr(prog + 0x40, g());
  if (g() % 4 == 0) Wr(0x1534674, prog);
  return prog;
}

// The light the stages draw for (or none) and its scale.
void StageLight(Arena& a, std::mt19937& g) {
  uint32_t light = 0;
  if (g() % 2) {
    light = a.Alloc(0x70);
    for (uint32_t i = 0; i < 0x70; i += 4) Wr(light + i, RandFloat(g));
  }
  Wr(0x1549B40, light);
  Wr(0x1549B44, RandFloat(g));
}

// A stage: 8 bundles of 200 bytes from +4, then the combiners, vertex
// program and fragment shader at +0x644..+0x64C, the colour and alpha
// waveforms (+0x650, +0x668), the colour and alpha generators (+0x664,
// +0x67C, constant +0x680) and the state bits +0x684.
uint32_t Bundle(Arena& a, std::mt19937& g, uint32_t b) {
  // Up to 32 images (+0..+0x7C), from a few distinct ones; the count now
  // and then negative.
  uint32_t pool[4];
  for (uint32_t& im : pool) im = Image(a, g);
  for (uint32_t i = 0; i < 32; i++) Wr(b + 4 * i, pool[g() % 4]);
  Wr(b + 0x80, g() % 4 ? g() % 6 : g() % 2 ? g() % 33 : uint32_t(g()) | 0x80000000u);
  Wr(b + 0x84, RandFloat(g));
  Wr(b + 0x88, g() % 8 == 0 && !g_preload ? Water(a, g, true) : 0);
  Wr(b + 0x8C, 2 + g() % 3);
  Wr(b + 0x90, g() % 5 == 0 ? 0 : Pick(g, {0x2100, 0x1E01, 0x104, 0x8570, uint32_t(g())}));
  const uint32_t combine = a.Alloc(0x50);
  for (uint32_t i = 0; i < 0x50; i += 4) Wr(combine + i, i == 0x2C || i == 0x4C ? RandFloat(g) : uint32_t(g()));
  Wr(b + 0x94, combine);
  uint32_t shader = 0;
  if (g() % 4 == 0) {
    shader = a.Alloc(0x20);
    Wr(shader, g() % 4 ? 0x86E7 + g() % 14 : g() % 3 ? 0 : uint32_t(g()));
    for (uint32_t i = 4; i < 0x20; i += 4) Wr(shader + i, uint32_t(g()));
  }
  Wr(b + 0x98, shader);
  Wr(b + 0xC0, g() % 4);
  g_mem.write<uint8_t>(b + 0xC4, g() % 4 == 0);
  g_mem.write<uint8_t>(b + 0xC5, g() % 8 == 0);
  g_mem.write<uint8_t>(b + 0xC6, g() % 2);
  return b;
}
uint32_t Stage(Arena& a, std::mt19937& g) {
  const uint32_t s = a.Alloc(0x690);
  g_mem.write<uint8_t>(s, uint8_t(g()));  // its sign bit: drawn per light
  for (uint32_t u = 0; u < 8; u++) Bundle(a, g, s + 4 + 0xC8 * u);
  if (g() % 2) Wr(s + 0xCC, 0);  // no second bundle: one unit
  Wr(s + 0x644, g() % 4 ? Rd(0x1534670) : g() % 3);
  Wr(s + 0x648, g() % 8 == 0 ? Combiners(a, g) : 0);
  Wr(s + 0x64C, g() % 8 == 0 ? Program(a, g) : 0);
  Wave(g, s + 0x650);
  Wave(g, s + 0x668);
  Wr(s + 0x664, g() % 8 ? g() % 15 : uint32_t(g()));
  Wr(s + 0x67C, g() % 8 ? g() % 15 : uint32_t(g()));
  Wr(s + 0x680, uint32_t(g()));
  Wr(s + 0x684, StateBits(g));
  return s;
}

// A string in the arena.
uint32_t Str(Arena& a, const char* s) {
  const uint32_t va = a.Alloc(uint32_t(std::strlen(s)) + 1);
  std::strcpy(g_mem.ptr<char>(va), s);
  return va;
}

void Randomize(Arena& a, std::mt19937& g) {
  a.Reset();
  for (int i = 0; i < kNumGl; i++) Wr(kGl[i].ptr, kGlTarget + 0x10 * uint32_t(i));
  if (g() % 3 == 0) Wr(0x1534DDC, 0);  // no compiled vertex arrays
  if (g() % 3 == 0) Wr(0x1534C2C, 0);
  Wr(0x1534630, g() % 4 ? StateBits(g) : 0);
  Wr(0x1534634, g() % 4 ? g() & 0x7FF : g());
  Wr(0x153462C, g() % 4);
  Wr(0x15345A0, g() % 8);
  Wr(0x15345A4, g() % 8);
  for (uint32_t u = 0; u < 8; u++) {
    Wr(0x15345A8 + 4 * u, g() % 6);
    Wr(0x15345C8 + 4 * u, Pick(g, {0x2100, 0x104, 0x1E01, 0x2101, 0, uint32_t(g())}));
    Wr(0x15345E8 + 4 * u, Pick(g, {0xDE1, 0x8513, 0, uint32_t(g())}));
  }
  Wr(0x1534460, Cvar(a, g, g() % 4 == 0));       // r_nobind
  Wr(0x15347F4, Cvar(a, g, g() % 3 != 0));       // fog
  Wr(0x15346C4, Cvar(a, g, g() % 3 == 0));       // lighting off
  Wr(0x15346A0, Cvar(a, g, Pick(g, {0, 0, 1, 2, 3})));  // r_primitives
  Wr(0x1534530, Cvar(a, g, 0));                  // polygon offset units
  Wr(0x153447C, Cvar(a, g, 0));                  // polygon offset factor
  const uint32_t lights = a.Alloc(0x120);
  Wr(lights + 0x118, g() % 2);
  Wr(0x1535A34, lights);
  Wr(0x1535A38, Image(a, g));
  Wr(0x1535ABC, g() % 2 ? Image(a, g) : 0);
  Wr(0x1535A24, g());
  Wr(0x1549930, g() % 2);
  Wr(0x1549B50, g() % 4 == 0);
  Wr(0x15496EC, g() & 0xFF);
  Wr(0x1535890, g() % 3 ? a.Alloc(16) : 0);
  Wr(0x13F9CF8, g() % 3 ? a.Alloc(16) : 0);
  for (uint32_t i = 0; i < 4; i++) Wr(0x1534640 + 4 * i, RandFloat(g));
  Wr(0x1536290, RandFloat(g));
  Wr(0x11E0760, g());
  Wr(0x11E0764, g());
  Wr(0x1549B14, g());
  Wr(0x1534758, g() % 8 ? g() % 9 : 0);  // texture units in use
  Wr(0x15347BC, g() % 2);  // GL_ATI_element_array
  for (uint32_t u = 0; u < 8; u++) Wr(0x1534608 + 4 * u, g() % 3 == 0);
  Wr(0x1534670, g() % 3);
  Wr(0x15343B4, Cvar(a, g, g() % 4 == 0));       // r_lightmap
  // The shader time: seconds, now and then anything.
  float time = float(g() % 100000) / 100.0f;
  uint32_t time_bits;
  std::memcpy(&time_bits, &time, 4);
  Wr(0x17F0C78, g() % 4 ? time_bits : RandFloat(g));
  Wr(0x1536360, g());
  for (uint32_t i = 0; i < 4; i++) Wr(0x1535A3C + 4 * i, Image(a, g));
  // The tessellator: up to 40 vertices (positions 4 or 8 floats apart).
  Wr(0x17F0C88, g() % 16 ? g() % 41 : 0);
  Wr(0x17F0C60, g() % 4 ? 4 : 8);
  const bool mixed = g() % 4 == 0;  // magnitudes whose sums depend on their order
  for (uint32_t i = 0; i < 0x1000; i += 4) Wr(0x1630C40 + i, mixed ? Mixed(g) : RandFloat(g));
  for (uint32_t i = 0; i < 0x600; i += 4) Wr(0x1698C40 + i, RandFloat(g));
  for (uint32_t i = 0; i < 0x400; i += 4) Wr(0x1650C40 + i, RandFloat(g));
  for (uint32_t i = 0; i < 0x200; i += 4) Wr(0x1690C40 + i, uint32_t(g()));
  for (uint32_t i = 0; i < 0x100; i += 4) {
    Wr(0x16E0C40 + i, uint32_t(g()));
  }
  const uint32_t shader = a.Alloc(0x700);
  Wr(shader + 0xA4, RandFloat(g));
  Wr(shader + 0xAC, g() % 8 ? g() % 5 : g() % 9);
  // The deforms: type, move vector, waveform, spread and bulge width,
  // height and speed; quads facing the view by their long axis (10) only
  // first, on positions that give every edge a length (below).
  for (uint32_t d = 0; d < 8; d++) {
    const uint32_t ds = shader + 0xB0 + 0x34 * d;
    uint32_t type = g() % 8 ? 1 + g() % 18 : g() % 24;
    if (d && type == 10) type = 9;
    if (g_preload && type == 4) type = 5;  // the noise
    Wr(ds, type);
    for (uint32_t i = 4; i < 0x34; i += 4)
      Wr(ds + i, g() % 4 ? Float(float(int(g() % 4001) - 2000) / 1000.0f) : RandFloat(g));
    Wave(g, ds + 0x10);
    if (g() % 4 == 0) Wr(ds + 0x20, g() % 2 ? 0 : 0x80000000);  // no frequency
  }
  // The texts: up to 31 characters (now and then spaces).
  for (uint32_t t = 0; t < 8; t++) {
    const uint32_t len = g() % 4 ? g() % 8 : g() % 32;
    for (uint32_t i = 0; i < 32; i++)
      g_mem.write<uint8_t>(0x15496F4 + 32 * t + i, i < len ? uint8_t(g() % 4 ? 0x20 + g() % 0x5F : g() % 4 ? 0x20 : 1 + g() % 255) : 0);
  }
  Wr(0x15496E8, g() % 8 ? g() % 1000000 : g());  // the refdef's time
  // The view's axes and the entity's origin and axes.
  for (uint32_t i = 0; i < 0x24; i += 4) Wr(0x1549834 + i, g() % 4 ? Float(float(int(g() % 2001) - 1000) / 1000.0f) : RandFloat(g));
  for (uint32_t i = 0; i < 0x30; i += 4) Wr(0x1549A88 + i, g() % 4 ? Float(float(int(g() % 2001) - 1000) / 1000.0f) : RandFloat(g));
  if (Rd(shader + 0xB0) == 10 || g() % 4 == 0) Positions(g);
  Wr(shader + 0x48, uint32_t(g()));
  Wr(shader + 0x50, uint32_t(g()));
  g_mem.write<uint8_t>(shader + 0x54, uint8_t(g()));
  Wr(shader + 0x58, g() % 4 ? Pick(g, {0x40A00000, 0x40800000, 0x40C00000, 0x41000000, 0x3F800000}) : RandFloat(g));
  Wr(shader + 0x5C, g() % 2 ? 0 : uint32_t(g()) & 0x30007);
  Wr(shader + 0xA8, g() % 3);
  Wr(shader + 0x188, 10 + g() % 21);
  // The flush's stages: up to 8, now and then fewer than the count says.
  const uint32_t stages = a.Alloc(0x20), num = g() % 4;
  for (uint32_t i = 0; i < 8; i++) Wr(stages + 4 * i, i < num + g() % 2 ? Stage(a, g) : 0);
  if (g() % 8 == 0) Wr(stages + 4 * 7, Stage(a, g));  // one past the terminator
  Wr(0x1830CA4, stages);
  // (A count past the stages only negative: the optimised iterators read
  // as many as it says.)
  Wr(0x1830C9C, g() % 8 ? num : uint32_t(g()) | 0x80000000u);
  Wr(shader + 0x154, Rd(stages) ? Rd(stages) : Stage(a, g));
  Wr(Rd(shader + 0x154) + 0x15C, Pick(g, {0, 0, 1, 0x2100, 0x1E01, 0x104}));
  // The optimised iterators' vertices: the shader's and the world's (in
  // memory, a buffer or an array object with its offset), the batch's
  // vertex range, and the shader of their triangles.
  Wr(shader + 0x18C, g());
  Wr(shader + 0x190, g());
  Wr(0x154955C, g());
  Wr(0x1549560, g());
  Wr(0x17F0C8C, g() % 1000);
  Wr(0x17F0C90, g() % 4 ? g() % 1000 : 0);
  if (g() % 3 == 0) Wr(0x1534DB0, 0);  // no glDrawRangeElementsEXT
  const uint32_t tris = a.Alloc(0x160);
  Wr(tris + 0xA8, g() % 3);
  Wr(tris + 0x154, Stage(a, g));
  Wr(0x1535B5C, tris);
  // The dlights: a colour, radius and origin about the vertices' (which
  // are within 16 of 0), the cvar 0x15344E4 and the batch's flags for the
  // vertices' axes, and the shader 0x1535AF4 drawn in for them.
  const uint32_t num_dlights = g() % 8 ? g() % 4 : uint32_t(g()) | 0x80000000u;
  const uint32_t dlights = a.Alloc(4 * 0x88);
  for (uint32_t d = 0; d < 4; d++) {
    const uint32_t dl = dlights + 0x88 * d;
    for (uint32_t i = 4; i < 0x10; i += 4) Wr(dl + i, g() % 4 ? Float(float(g() % 1001) / 1000.0f) : RandFloat(g));
    Wr(dl + 0x74, g() % 8 ? Float(float(1 + g() % 3200) / 100.0f) : RandFloat(g));
    for (uint32_t i = 0x78; i < 0x84; i += 4) Wr(dl + i, g() % 8 ? Float(float(int(g() % 4001) - 2000) / 100.0f) : RandFloat(g));
  }
  // Now and then the first vertices on a grid and the first dlight at
  // offsets from the first that put coordinates and distances on the
  // bounds the clip bits test (radius 3: a coordinate just past 1 that its
  // store rounds to 1; radius 4: exactly 0).
  if (g() % 2) {
    const uint32_t stride = 4 * Rd(0x17F0C60);
    for (uint32_t v = 0; v < 4; v++)
      for (uint32_t k = 0; k < 12; k += 4) Wr(0x1630C40 + stride * v + k, Float(float(int(g() % 9) - 4)));
    const float r = std::bit_cast<float>(Pick(g, {Float(3.0f), Float(4.0f), Float(1.5f), Float(6.0f)}));
    Wr(dlights + 0x74, Float(r));
    for (uint32_t k = 0; k < 12; k += 4) {
      const float dx = r * std::bit_cast<float>(Pick(g, {Float(0.0f), Float(0.5f), Float(-0.5f), Float(1.0f), Float(-1.0f), Float(0.25f)}));
      Wr(dlights + 0x78 + k, Float(std::bit_cast<float>(Rd(0x1630C40 + k)) + dx));
    }
  }
  Wr(0x15497FC, num_dlights);
  Wr(0x1549804, dlights);
  Wr(0x15344E4, Cvar(a, g, g() % 2));
  Wr(0x17F0C64, g() % 2);
  Wr(0x17F0C68, g() % 2);
  Wr(0x17F0C6C, g() % 2);
  for (uint32_t i = 0; i < 0x200; i += 4) {
    Wr(0x16B0C40 + i, RandFloat(g));
    Wr(0x16C8C40 + i, RandFloat(g));
  }
  const uint32_t dlight_shader = a.Alloc(0x160);
  Wr(dlight_shader + 0x154, g() % 4 ? Stage(a, g) : 0);
  Wr(0x1535AF4, dlight_shader);
  Wr(0x1549B20, g());
  Wr(0x13F96D8, g());
  Wr(0x17F0C70, shader);
  Wr(0x1535AEC, g() % 16 ? 0 : shader);  // the shader with its own flush
  Wr(0x17F0C84, g() % 8 ? g() % 64 : 0);
  Wr(0x17F0C80, g() % 2 ? uint32_t(g()) : 0);  // dlight bits
  Wr(0x11E075C, g() % 4 == 0);
  for (uint32_t i = 0; i < 0x100; i++) g_mem.write<uint16_t>(0x1618C40 + 2 * i, uint16_t(g() % 40));
  g_mem.write<uint16_t>(0x1630C3E, g() % 16 ? 0 : uint16_t(g()));
  Wr(0x1650C30, g() % 16 ? 0 : RandFloat(g));  // the last vertex's x, 4 or
  Wr(0x1670C20, g() % 16 ? 0 : RandFloat(g));  // 8 floats apart
  for (uint32_t i = 0; i < 8; i++) Wr(0x16E8C40 + 4 * i, uint32_t(g()));
  Wr(0x154992C, g() % 4 == 0);
  Wr(0x1534438, Cvar(a, g, g() % 2));
  Wr(0x1535AC4, Image(a, g));
  Wr(0x1534704, Cvar(a, g, g() % 4 == 0));  // the log
  Wr(0x1830CE4, g() % 2 ? uint32_t(g()) : 0);  // the log file
  Wr(0x153468C, Cvar(a, g, g() % 2 ? 0 : g() % 8 ? g() % 20 : uint32_t(g())));
  const uint32_t normals = Cvar(a, g, 0);
  const char* const kShowNormals[] = {"", "", "", "white", "White", "color", "other"};
  Wr(normals + 4, Str(a, kShowNormals[g() % 7]));
  Wr(0x1534698, normals);
  Wr(0x1830CA0, g() % 4 ? Rd(0x1830CA0) : kSkyIterator);
  Wr(0x154D708, g() % 2);
  Wr(0x1830C94, g() % 2 ? g() % 1000 : 0);
  Wr(0x1830C98, g() % 1000);
  Wr(0x1549B08, g());
  StageLight(a, g);
  // The tessellator's entity (the DXF file's): one of the entities.
  Wr(0x15497F8, a.Alloc(4 * 0x2B8));
  Wr(0x17F0C7C, Rd(0x15497F8) + 0x2B8 * (g() % 4));
  Wr(0x1549B0C, g());
  Wr(0x1549B10, g());
  if (g() % 2) Wr(0x1536290, 0x3F800000);  // tr.identityLight 1
  Wr(0x1536294, g() % 8 ? g() & 0xFF : g());
  Wr(0x1536298, g() % 4 ? g() % 3 : uint32_t(g()));  // overbright bits
  for (uint32_t i = 0; i < 12; i += 4) Wr(0x1549828 + i, RandFloat(g));  // view origin
  for (uint32_t i = 0; i < 12; i += 4) Wr(0x1549AB8 + i, RandFloat(g));  // and the entity's
  for (uint32_t i = 0; i < 12; i += 4) Wr(0x15364B0 + i, RandFloat(g));  // the world's vector
  if (g() % 2) {
    Wr(0x1549B3C, 0x1535D6C);  // the world entity
  } else {
    const uint32_t ent = a.Alloc(0x120);
    for (uint32_t i = 0x18; i < 0x50; i += 4) Wr(ent + i, RandFloat(g));
    if (g() % 2) Wr(ent + 0x40, 0);  // axes not to be normalised
    if (g() % 8 == 0)
      for (uint32_t i = 0x1C; i < 0x28; i += 4) Wr(ent + i, g() % 2 ? 0 : 0x80000000);  // a zero x axis
    Wr(0x1549B3C, ent);
  }
  // The entity's lights: the light (type 8 skipped) and its scale.
  const uint32_t ent = Rd(0x1549B3C), num_lights = g() % 2 ? 0 : g() % 8 ? g() % 5 : uint32_t(g()) | 0x80000000u;
  Wr(ent + 0xCC, num_lights);
  for (uint32_t i = 0; i < 4; i++) {
    const uint32_t light = a.Alloc(16);
    Wr(light, g() % 3 ? 0 : 8);
    Wr(ent + 0xD0 + 8 * i, light);
    Wr(ent + 0xD4 + 8 * i, RandFloat(g));
  }
  for (uint32_t i = 0xA4; i < 0xB0; i += 4) Wr(ent + i, RandFloat(g));  // light direction
  // Its colour, ambient and directed light (and the ambient's colour).
  Wr(ent + 0x6C, uint32_t(g()));
  for (uint32_t i = 0xB0; i < 0xCC; i += 4)
    Wr(ent + i, g() % 4 ? Float(float(g() % 40000) / 100.0f - 50.0f) : RandFloat(g));
  Wr(ent + 0xBC, uint32_t(g()));
  Wr(0x154D734, g() % 2);
  Wr(0x15343F0, Cvar(a, g, 0));                  // the far clip
  if (g() % 2) Wr(Rd(0x15343F0) + 0x1C, 0);
  Wr(0x15343E8, Cvar(a, g, g() % 8));
  // The sky's, the view's and the world's fogs, and the values last sent.
  for (uint32_t fog : {0x1535820u, 0x1535860u, 0x1535920u}) {
    Wr(fog, Pick(g, {0, 0x2601, 0x800, 0x801, uint32_t(g())}));
    Wr(fog + 4, Pick(g, {0, 0x1100, 0x1101, 0x1102}));
    for (uint32_t i = 0x10; i < 0x30; i += 4) Wr(fog + i, RandFloat(g));
    if (g() % 4 == 0) Wr(fog + 0x2C, 0);
    Wr(fog + 0x30, g() % 3 != 0);
  }
  Wr(0x13F9CF8, g() % 3 ? g() % 3 : uint32_t(g()));
  const uint32_t sent = 0x1535820 + 0x40 * (g() % 2);
  Wr(0x1534638, g() % 2 ? Rd(sent) : uint32_t(g()));
  Wr(0x153463C, g() % 2 ? Rd(sent + 4) : uint32_t(g()));
  Wr(0x1534650, g() % 2 ? Rd(sent + 0x20) : RandFloat(g));
  Wr(0x1534654, g() % 2 ? Rd(sent + 0x24) : RandFloat(g));
  Wr(0x1534658, g() % 2 ? Rd(sent + 0x2C) : RandFloat(g));
  Wr(0x1549B18, g());
  // The shadow volume: the stencil bits, now and then too many vertices;
  // the edges left from before (counts now and then full or past it, the
  // facings 0 or 1) and the triangles' facings; the immediate-mode batch
  // (its vertices in the arena, now and then full or nearly).
  Wr(0x1534768, g() % 4 ? 8 : g() % 8);
  if (g() % 16 == 0) {
    Wr(0x17F0C88, 0x1000 + g() % 2);
    Wr(shader + 0xAC, 0);  // no deforms: they would reach past the windows
  }
  // Now and then a first triangle whose facing rests on the order of its
  // dot product's sum: the normal (1, 1, 1), the light (1e-30, 1, -1).
  if (g() % 8 == 0) {
    const uint32_t stride = 4 * Rd(0x17F0C60);
    const float v[3][3] = {{0, 0, 0}, {1, -1, 0}, {0, 1, -1}};
    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t k = 0; k < 3; k++) Wr(0x1630C40 + stride * i + 4 * k, Float(v[i][k]));
      g_mem.write<uint16_t>(0x1618C40 + 2 * i, uint16_t(i));
    }
    if (Rd(0x17F0C84) < 3) Wr(0x17F0C84, 3);
    Wr(ent + 0xA4, Float(1e-30f));
    Wr(ent + 0xA8, Float(1.0f));
    Wr(ent + 0xAC, Float(-1.0f));
  }
  for (uint32_t i = 0; i < 0x100; i += 4) {
    Wr(0x13F16D8 + i, g() % 4 ? g() % 4 : g() % 8 ? g() % 34 : 0x20);
    Wr(0x11E16D8 + i, uint32_t(g()));
  }
  for (uint32_t i = 0; i < 0x2900; i += 8) {
    Wr(0x11F16D8 + i, g() % 40);
    Wr(0x11F16DC + i, g() % 2);
  }
  const uint32_t batch = g() % 4 ? 64 : g() % 6;
  Wr(0x15495EC, batch);
  Wr(0x15495E8, batch ? g() % batch : 0);
  if (g() % 32 == 0) Wr(0x15495EC, uint32_t(g()) | 0x80000000u);  // a negative capacity
  recomp::gen::g_batch = a.Alloc(32 * 64);
  recomp::gen::g_tiddata = a.Alloc(0x88);
  Wr(0x15495F0, g() % 8 ? recomp::gen::g_batch : 0);
  // The client arrays' mode, the NV vertex array range and the arrays'
  // flag; the debug triangles' DXF cvar.
  Wr(0x153465C, g() % 4);
  Wr(0x153479C, g() % 3 ? 0 : 1 + g() % 2);
  Wr(0x15347B8, g() % 2);
  Wr(0x14E43FC, Cvar(a, g, g() % 4 ? 0 : 0xFFFFFFFF));
  for (uint32_t i = 0x15495C4; i < 0x15495DC; i += 4) Wr(i, uint32_t(g()));
}

// Triangles that continue a strip (the next triangle on the shared edge,
// in the winding a strip alternates) or start a new one.
uint32_t Indexes(Arena& a, std::mt19937& g, int tris) {
  const uint32_t p = a.Alloc(6 * uint32_t(tris) + 16);
  std::vector<uint16_t> v;
  uint16_t next = uint16_t(g() % 100);
  for (int t = 0; t < tris; t++) {
    const size_t n = v.size();
    if (t && g() % 3) {
      // Continue: (odd) a b c -> c b d, (even) a b c -> a c d; the strip
      // check takes whichever the parity says, so either may break it.
      if (g() % 2) v.insert(v.end(), {v[n - 1], v[n - 2], next++});
      else v.insert(v.end(), {v[n - 3], v[n - 1], next++});
    } else {
      v.insert(v.end(), {next, uint16_t(next + 1), uint16_t(next + 2)});
      next += 3;
    }
    if (g() % 16 == 0) next = uint16_t(g());
  }
  for (size_t i = 0; i < v.size(); i++) g_mem.write<uint16_t>(p + 2 * uint32_t(i), v[i]);
  return p;
}

// A vertex buffer of n floats a vertex (for the most vertices, 40), of
// ordinary and special values, now and then of mixed magnitudes.
uint32_t Coords(Arena& a, std::mt19937& g, uint32_t n) {
  const uint32_t p = a.Alloc(40 * 4 * n);
  const bool mixed = g() % 4 == 0;
  for (uint32_t i = 0; i < 40 * 4 * n; i += 4) Wr(p + i, mixed ? Mixed(g) : RandFloat(g));
  return p;
}

// Finite positions, normals and texture coordinates t when the vertices'
// axes are built (the lifted VectorNormalize takes a NaN length to the
// CRT's error path, as the square root above).
void AxesInputs(std::mt19937& g) {
  if (Rd(0x17F0C68) && Rd(0x17F0C6C)) return;
  Positions(g);
  const bool mixed = g() % 4 == 0;
  for (uint32_t i = 0; i < 0x600; i += 4)
    Wr(0x1698C40 + i, mixed ? Mixed(g) : Float(float(int(g() % 2001) - 1000) / 1000.0f));
  for (uint32_t i = 0; i < 0x400; i += 4)
    Wr(0x1650C40 + i, mixed ? Mixed(g) : Float(float(int(g() % 2001) - 1000) / 1000.0f));
}

// A float of 2^30, 1 or 2^-30, either sign: of three products of such
// values two often cancel and the third is below the other two's ulp, so
// the order of their sum shows.
uint32_t Power(std::mt19937& g) {
  return Pick(g, {Float(0x1p30f), Float(1.0f), Float(0x1p-30f)}) | (g() % 2 ? 0x80000000u : 0);
}

// Now and then the view's origin (in the entity's space) and the
// positions on a grid (directions to the view of equal coordinates) and
// normals of powers of 2.
void ViewGrid(std::mt19937& g) {
  if (g() % 4) return;
  for (uint32_t i = 0; i < 12; i += 4) Wr(0x1549AB8 + i, Float(float(int(g() % 5) - 2)));
  for (uint32_t i = 0; i < 0x1000; i += 4) Wr(0x1630C40 + i, Float(float(int(g() % 5) - 2)));
  for (uint32_t i = 0; i < 0x600; i += 4) Wr(0x1698C40 + i, Power(g));
}

// The light drawn for (its origin +0x44, w +0x50: now and then 0, a
// direction), or none (the first page).
void Light(Arena& a, std::mt19937& g) {
  if (g() % 8 == 0) {
    Wr(0x1549B40, 0);
    return;
  }
  const uint32_t l = a.Alloc(0x54);
  const bool powers = g() % 4 == 0;
  for (uint32_t i = 0x44; i < 0x54; i += 4) Wr(l + i, powers ? Power(g) : g() % 2 ? Mixed(g) : RandFloat(g));
  if (powers)
    for (uint32_t i = 0; i < 0x30; i += 4) Wr(0x1549A88 + i, Power(g));  // the entity's origin and axes
  if (g() % 2) Wr(l + 0x50, g() % 2 ? 0 : 0x80000000);
  Wr(0x1549B40, l);
}

void Cases(Arena& a, std::mt19937& g, Stats& s) {
  Call k{};
  k.entry = int(g() % kNumEntries);
  k.preload = g_preload = g() % (9 - kEntries[k.entry].x87);
  Randomize(a, g);
  switch (kEntries[k.entry].va) {
    case 0x4CB3B0:
      k.esi = g() % 32 ? Image(a, g) : 0;
      break;
    case 0x4CB5D0:
      k.esi = g() % 8 ? g() % 3 : g();
      break;
    case 0x4CB640:
      k.eax = Pick(g, {0x2100, 0x104, 0x1E01, 0x2101, 0x2100, 0x1E01, uint32_t(g() % 0x3000), uint32_t(g())});
      break;
    case 0x4CB710:
      k.eax = StateBits(g);
      break;
    case 0x4CBBE0:
      k.args = {uint32_t(g() % 4 ? g() & 0x7FF : g())};
      break;
    case 0x4F3180:
    case 0x4F32C0: {
      const int tris = int(g() % 24);
      k.eax = Indexes(a, g, tris);
      int n = 3 * tris;
      if (g() % 8 == 0) n = int(g() % 80) - 8;
      k.edi = kGlTarget + 0x10 * 19;  // glArrayElement
      if (kEntries[k.entry].va == 0x4F3180) k.args = {uint32_t(n)};
      else k.edx = uint32_t(n);
      break;
    }
    case 0x4F3330:
      k.esi = Bundle(a, g, a.Alloc(0xC8));
      k.args = {uint32_t(g() % 8)};
      break;
    case 0x4F3410:
      k.eax = Bundle(a, g, a.Alloc(0xC8));
      k.edi = g() % 8;
      break;
    case 0x4F3810:
      k.eax = g() % 8;
      break;
    case 0x4F38B0:
      k.esi = g() % 8;
      k.args = {Bundle(a, g, a.Alloc(0xC8)), uint32_t(g()), uint32_t(g() % 64)};
      break;
    case 0x4F3920:
      k.edi = g() % 8;
      break;
    case 0x4C8830:
      k.esi = Pick(g, {0x1535820, 0x1535860, 0x1535920});
      break;
    case 0x4F4FA0:
      k.args = {Stage(a, g)};
      break;
    case 0x4F5DE0:
      k.eax = Stage(a, g);
      k.ebx = 0x1618C40;
      k.args = {uint32_t(g() % 64)};
      break;
    case 0x4F6070:
      k.args = {0x1618C40};
      break;
    case 0x4F60E0:
      k.args = {g() % 2 ? 0 : g() % 4 ? 1 : uint32_t(g())};
      break;
    case 0x4F39B0:
      k.esi = g() % 8;
      k.args = {Bundle(a, g, a.Alloc(0xC8)), uint32_t(g()), uint32_t(g()), uint32_t(g() % 64)};
      break;
    case 0x4F3F00: {
      const uint32_t coords = a.Alloc(32);
      for (uint32_t u = 0; u < 8; u++) Wr(coords + 4 * u, uint32_t(g()));
      k.args = {Stage(a, g), uint32_t(g()), coords, uint32_t(g() % 64)};
      break;
    }
    case 0x4F6380:
    case 0x4F66C0:
    case 0x4F6A40:
    case 0x4F6D70:
    case 0x4F70F0:
    case 0x4F73D0:
    case 0x4F76B0:
    case 0x4F7950:
      k.esi = g();
      k.edi = g();
      break;
    case 0x4F30A0: {
      // A normal: x above, at or below -1 now and then.
      k.ecx = a.Alloc(12);
      for (uint32_t i = 0; i < 12; i += 4) Wr(k.ecx + i, RandFloat(g));
      if (g() % 4 == 0) Wr(k.ecx, Pick(g, {0xBF800000, 0xBF800001, 0xBF7FFFFF, 0xC0000000}));
      k.esi = a.Alloc(12);
      k.edx = a.Alloc(12);
      break;
    }
    case 0x4F4BD0:
      k.ecx = 0x1690C40;
      k.edx = 0x1670C40;
      k.args = {Rd(0x1549804) + 0x88 * uint32_t(g() % 4), a.Alloc(0x80)};
      break;
    case 0x50A030:
    case 0x50A070:
    case 0x50A0E0:
    case 0x50A120:
      k.ecx = 0x16E0C40;
      if (g() % 8 == 0) Wr(0x1549B3C, 0);
      break;
    case 0x50A170:
      k.esi = a.Alloc(0x14);
      Wave(g, k.esi);
      k.eax = 0x16E0C40;
      break;
    case 0x50A270:
      k.eax = a.Alloc(0x14);
      Wave(g, k.eax);
      k.args = {0x16E0C40};
      break;
    case 0x50AEC0:
      k.eax = 0x16E0C40;
      break;
    case 0x50B040:
      k.args = {0x16E0C40};
      break;
    case 0x4F2E60:
      // The index count, now and then on a colour band's edge.
      k.eax = g() % 4 ? Pick(g, {30, 31, 150, 151, 450, 451, 1200, 1201, 3000, 3001})
                      : g() % 8 ? g() % 4000 : g();
      k.edi = 0x16E0C40;
      break;
    case 0x508AE0:
    case 0x508C50:
    case 0x508DE0:
    case 0x508F00:
    case 0x509040:
    case 0x5090E0:
    case 0x508800:
    case 0x5095F0: {
      // A deform of the shader's (its fields as Randomize leaves them), on
      // at most 40 vertices.
      if (Rd(0x17F0C88) > 40) Wr(0x17F0C88, g() % 41);
      const uint32_t ds = Rd(0x17F0C70) + 0xB0 + 0x34 * (g() % 8);
      k.edi = k.ebx = k.esi = ds;
      k.eax = g() % 2;
      if (kEntries[k.entry].va == 0x509040) k.args = {ds};
      break;
    }
    case 0x5098A0:
      if (Rd(0x17F0C88) > 40) Wr(0x17F0C88, g() % 41);
      Positions(g);
      break;
    case 0x509190:
      k.ebx = 0x15496F4 + 32 * (g() % 8);
      break;
    case 0x50A2B0:
    case 0x50A720:
      ViewGrid(g);
      k.eax = Coords(a, g, 3);
      break;
    case 0x50A890:
      k.eax = Coords(a, g, 3);
      break;
    case 0x50A380:
    case 0x50A3D0:
    case 0x50A4D0:
      if (kEntries[k.entry].va == 0x50A4D0) Light(a, g);
      k.ecx = Coords(a, g, 3);
      break;
    case 0x50A5C0:
      ViewGrid(g);
      Light(a, g);
      k.eax = Coords(a, g, 3);
      break;
    case 0x50A420:
      ViewGrid(g);
      k.edx = Coords(a, g, 3);
      break;
    case 0x50A7C0:
      AxesInputs(g);
      k.eax = Coords(a, g, 3);
      k.args = {uint32_t(g() % 3)};
      break;
    case 0x50A820:
      AxesInputs(g);
      k.eax = g() % 3;
      k.ecx = Coords(a, g, 4);
      break;
    case 0x50A8D0:
      k.ebx = a.Alloc(0x14);
      Wave(g, k.ebx);
      k.eax = Coords(a, g, 2);
      break;
    case 0x50A980:
      k.edx = Coords(a, g, 2);
      k.eax = Coords(a, g, 2);
      break;
    case 0x50A9B0: {
      // Finite speeds and time (the CRT's floor takes a NaN to its error
      // path, which the lifted CRT reaches through the thread's data).
      k.eax = a.Alloc(8);
      for (uint32_t i = 0; i < 8; i += 4)
        Wr(k.eax + i, g() % 4 ? Float(float(int(g() % 4001) - 2000) / 1000.0f) : Mixed(g));
      if (!std::isfinite(std::bit_cast<float>(Rd(0x17F0C78)))) Wr(0x17F0C78, Float(1.5f));
      k.ecx = Coords(a, g, 2);
      break;
    }
    case 0x50AA30:
      // A texmod's matrix (+0x18) and translation (+0x28).
      k.eax = a.Alloc(0x30);
      for (uint32_t i = 0x18; i < 0x30; i += 4) Wr(k.eax + i, g() % 2 ? Mixed(g) : RandFloat(g));
      k.ecx = Coords(a, g, 2);
      break;
    case 0x50AA80:
      k.args = {g() % 4 ? Float(float(int(g() % 4001) - 2000) / 10.0f) : RandFloat(g), Coords(a, g, 2)};
      break;
    case 0x50AE10:
      AxesInputs(g);
      k.eax = Coords(a, g, 3);
      if (g() % 4 == 0) {
        // Vectors and axes of powers of 2, the axes as they are.
        Wr(0x17F0C68, 1);
        Wr(0x17F0C6C, 1);
        for (uint32_t i = 0; i < 40 * 12; i += 4) {
          Wr(k.eax + i, Power(g));
          for (const uint32_t axes : {0x1698C40u, 0x16B0C40u, 0x16C8C40u}) Wr(axes + i, Power(g));
        }
      }
      break;
    case 0x50ABF0: {
      Wr(0x17F0C68, 0);
      AxesInputs(g);
      // Stale counts where its frame keeps them (read for an index past
      // the vertex count).
      const uint32_t esp = kStack + kStackSize - 0x1000;
      for (uint32_t i = esp - 0x8040; i < esp; i += 4) Wr(i, g() % 3);
      break;
    }
    case 0x508A50:
      k.esi = a.Alloc(0x14);
      Wave(g, k.esi);
      k.args = {Coords(a, g, 2)};
      break;
    case 0x4F3D80: {
      const uint32_t coords = a.Alloc(32);
      for (uint32_t u = 0; u < 8; u++) Wr(coords + 4 * u, uint32_t(g()));
      k.args = {Stage(a, g), coords, uint32_t(g() % 64)};
      break;
    }
    case 0x4FAAB0: {
      // A surface of at most 40 vertices and 120 indexes into a tessellator
      // of at most 40 vertices (as the windows hold).
      if (Rd(0x17F0C88) > 40) Wr(0x17F0C88, g() % 41);
      const uint32_t surf = a.Alloc(0x30), verts = g() % 41, n = g() % 121;
      g_mem.write<uint16_t>(surf + 0x10, uint16_t(n));
      g_mem.write<uint16_t>(surf + 0x12, uint16_t(verts));
      for (const uint32_t at : {0x14u, 0x18u, 0x1Cu}) {
        Wr(surf + at, a.Alloc(12 * 40));
        for (uint32_t i = 0; i < 12 * 40; i += 4) Wr(Rd(surf + at) + i, RandFloat(g));
      }
      Wr(surf + 0x20, a.Alloc(2 * 120));
      for (uint32_t i = 0; i < 2 * 120; i += 4) Wr(Rd(surf + 0x20) + i, uint32_t(g()));
      k.args = {surf};
      break;
    }
    case 0x4FABD0: {
      // A draw surface: the model's surface, its first vertex (now and then
      // past 16 bits or negative); indexes in sixes, now and then with a
      // few over; a batch of at most 100 indexes so far.
      const uint32_t draw = a.Alloc(12), surf = a.Alloc(0x30);
      const uint32_t n = 6 * (1 + g() % 20) + (g() % 4 ? 0 : g() % 6);
      Wr(draw + 4, surf);
      Wr(draw + 8, g() % 4 ? g() % 2000 : uint32_t(g()));
      g_mem.write<uint16_t>(surf + 0x10, uint16_t(n));
      g_mem.write<uint16_t>(surf + 0x12, uint16_t(g() % 4 ? g() % 500 : g()));
      Wr(surf + 0x20, a.Alloc(2 * n));
      for (uint32_t i = 0; i < 2 * n; i += 2) g_mem.write<uint16_t>(Rd(surf + 0x20) + i, uint16_t(g() % 4 ? g() % 500 : g()));
      Wr(0x1830C94, g() % 101);
      k.args = {draw};
      break;
    }
    case 0x4FACD0:
    case 0x4FAF60:
    case 0x4FB1B0:
    case 0x4FB660:
    case 0x4FB8C0:
    case 0x4FBAF0:
    case 0x4FBD50: {
      // A surface drawn from its own vertices (the ATI one's of type 18 or
      // 22, now and then another).
      const uint32_t surf = a.Alloc(0x30);
      Wr(surf, g() % 8 ? Pick(g, {0x12, 0x16}) : g() % 32);
      for (uint32_t i = 4; i < 0x30; i += 4) Wr(surf + i, uint32_t(g()));
      k.args = {surf};
      k.esi = g();
      k.edi = g();
      break;
    }
    case 0x503A60:
    case 0x503A80:
      break;
    case 0x5016B0: {
      // A polygon of at most 20 vertices (now and then a count below 3)
      // into a tessellator of at most 20, and up to 5 dlights (now and then
      // up to 32 of them) of the frame's kind.
      if (Rd(0x17F0C88) > 20) Wr(0x17F0C88, g() % 21);
      if (g() % 32 == 0) Wr(0x17F0C88, 0x2000 - g() % 24);  // over: RB_CheckOverflow
      const uint32_t surf = a.Alloc(0x10), verts = a.Alloc(0x20 * 20);
      const uint32_t n = g() % 8 ? g() % 21 : uint32_t(int(g() % 6) - 3);
      Wr(surf + 8, n);
      if (g() % 16 == 0) Wr(0x17F0C84, 0xC000 - 3 * (n - 2) + int(g() % 9) - 6);  // its triangles' limit
      Wr(surf + 0xC, verts);
      for (uint32_t i = 0; i < 0x20 * 20; i += 4) Wr(verts + i, i % 0x20 < 0x1C ? RandFloat(g) : uint32_t(g()));
      const uint32_t num = g() % 8 ? g() % 6 : g() % 33, dlights = a.Alloc(0x88 * 32);
      for (uint32_t d = 0; d < num; d++) {
        const uint32_t dl = dlights + 0x88 * d;
        Wr(dl + 0x74, g() % 8 ? Float(float(1 + g() % 3200) / 100.0f) : RandFloat(g));
        for (uint32_t i = 0x78; i < 0x84; i += 4) Wr(dl + i, g() % 8 ? Float(float(int(g() % 3201) - 1600) / 100.0f) : RandFloat(g));
      }
      // Now and then a vertex on the first dlight's sphere.
      if (g() % 4 == 0) {
        const float r = std::bit_cast<float>(Pick(g, {Float(3.0f), Float(4.0f), Float(0.5f)}));
        Wr(dlights + 0x74, Float(r));
        for (uint32_t k = 0; k < 12; k += 4) Wr(verts + k, Float(std::bit_cast<float>(Rd(dlights + 0x78 + k)) + (k ? 0.0f : r)));
      }
      Wr(0x1536474, num);
      Wr(0x153647C, dlights);
      k.args = {surf};
      break;
    }
    case 0x4F4AB0: {
      // A shader's stages, their count, its stage iterator and flags; the
      // refdef's time; the shader's time offset and its clamp (now and
      // then 0, -0, the time it gives or any).
      const uint32_t sh = Rd(0x17F0C70);
      Wr(sh + 0x150, g());
      Wr(sh + 0x174, g());
      Wr(sh + 0x50, g());
      const float now = float(g() % 100000) / 100.0f, off = float(g() % 10000) / 100.0f;
      Wr(0x15496F0, g() % 4 ? Float(now) : RandFloat(g));
      Wr(sh + 0x17C, g() % 4 ? Float(off) : RandFloat(g));
      Wr(sh + 0x178, Pick(g, {0, 0x80000000, Float(now - off), Float(float(g() % 100000) / 100.0f), RandFloat(g)}));
      k.ecx = sh;
      k.args = {g() % 4 ? Pick(g, {4, 8}) : uint32_t(g())};
      break;
    }
    case 0x500A60: {
      // Counts that take the tessellator to its limits or not, and now
      // and then past them on their own.
      auto count = [&](uint32_t limit, uint32_t have) {
        switch (g() % 6) {
          case 0: return uint32_t(limit - have - 1 + g() % 3);
          case 1: return uint32_t(limit + g() % 3 - 1);
          case 2: return uint32_t(g());
          default: return uint32_t(g() % 16);
        }
      };
      k.args = {count(0x2000, Rd(0x17F0C88)), count(0xC000, Rd(0x17F0C84))};
      break;
    }
    case 0x4F4A20:
      k.eax = Pick(g, {3, 3, 0, 1, 2, uint32_t(g())});
      break;
    case 0x4ADEC0:
      k.args = {uint32_t(g()), uint32_t(g()), uint32_t(g())};
      break;
    case 0x4F44A0:
    case 0x4F4560:
    case 0x4F4850:
      // The tessellator, its entity (the two the triangles skip, or
      // another), now and then over 256 vertices (a draw every 256; with
      // both axes, which are otherwise computed from the vertices past the
      // windows); the inputs of the axes; the normals cvar's value.
      Wr(0x17F0C7C, Pick(g, {0x1549B5C, 0x1549E14, a.Alloc(0x2B8)}));
      if (g() % 8 == 0) {
        Wr(0x17F0C88, 250 + g() % 60);
        Wr(0x17F0C68, 1);
        Wr(0x17F0C6C, 1);
      }
      AxesInputs(g);
      Wr(Rd(0x1534698) + 0x20, g() % 3);
      if (kEntries[k.entry].va == 0x4F44A0) k.eax = 0x1618C40;
      else if (kEntries[k.entry].va == 0x4F4560) k.args = {0x1618C40};
      else k.ebx = 0x1618C40;
      break;
    case 0x4F3A70: {
      // The combiners, with one to three units.
      Wr(0x15347A4, g() % 4);
      k.args = {Combiners(a, g)};
      break;
    }
    case 0x4F3C70:
      k.esi = Program(a, g);
      break;
    case 0x509500: {
      const uint32_t p = a.Alloc(12);
      for (uint32_t i = 0; i < 12; i += 4) Wr(p + i, g() % 4 ? Mixed(g) : RandFloat(g));
      // Now and then products that cancel, for the sum's order: an axis
      // (1, -1, 1) from the origin 0 and the point (2^60, 2^60, 1).
      if (g() % 8 == 0) {
        const uint32_t axis = 0x1549A94 + 0xC * (g() % 3);
        Wr(axis, Float(1.0f));
        Wr(axis + 4, Float(-1.0f));
        Wr(axis + 8, Float(1.0f));
        for (uint32_t i = 0; i < 12; i += 4) Wr(0x1549A88 + i, 0);
        Wr(p, Float(0x1p60f));
        Wr(p + 4, Float(0x1p60f));
        Wr(p + 8, Float(1.0f));
      }
      k.ecx = g() % 8 ? p : 0x1549828;
      k.eax = g() % 4 ? a.Alloc(12) : k.ecx;
      break;
    }
    case 0x508940:
      k.ecx = g() % 8 ? g() % 7 : uint32_t(g());
      break;
    case 0x4D3AA0:
      k.args = {NoiseCoord(g), NoiseCoord(g), NoiseCoord(g), NoiseCoord(g)};
      break;
    case 0x4FCE80:
      k.ecx = Water(a, g);
      k.args = {g() % 8 ? uint32_t(g() % 1000000) : uint32_t(g()), uint32_t(g() % 8)};
      break;
    case 0x4FC960: {
      const uint32_t gen = Water(a, g);
      k.esi = gen;
      k.edi = a.Alloc(8 * Rd(gen + 0x10) * Rd(gen + 0x14));
      k.args = {g() % 4 ? Float(float(g() % 100000) / 1000.0f) : RandFloat(g)};
      break;
    }
    case 0x4FCA50: {
      const uint32_t gen = Water(a, g);
      k.esi = gen;
      k.args = {Complex(a, g, Rd(gen + 0x10) * Rd(gen + 0x14))};
      break;
    }
    case 0x4FC4E0: {
      // 4 to 256 values, 1 to 3 apart.
      const uint32_t lg = 2 + g() % 7, stride = 1 + g() % 3;
      k.ecx = lg;
      k.args = {Complex(a, g, (1u << lg) * stride), stride};
      break;
    }
    case 0x4FCAE0: {
      const uint32_t gen = Water(a, g), n = Rd(gen + 0x10) * Rd(gen + 0x14);
      k.args = {a.Alloc(4 * n), Complex(a, g, n), gen};
      break;
    }
    case 0x4F41C0: {
      // A matrix (or the view's), a point and its result (now and then
      // the point itself).
      uint32_t m = 0x1549AC4;
      if (g() % 2) {
        m = a.Alloc(64);
        for (uint32_t i = 0; i < 64; i += 4) Wr(m + i, g() % 8 ? Mixed(g) : RandFloat(g));
      }
      const uint32_t p = a.Alloc(12);
      for (uint32_t i = 0; i < 12; i += 4) Wr(p + i, g() % 4 ? Mixed(g) : RandFloat(g));
      k.eax = m;
      k.ecx = p;
      k.edx = g() % 4 ? a.Alloc(12) : p;
      break;
    }
    case 0x4F4260: {
      // The entity (the two skipped, the world or one of the entities at
      // 0x15497F8 by a type and flags), the shader's flags, the matrices,
      // the file.
      const uint32_t ents = a.Alloc(4 * 0x2B8);
      Wr(0x15497F8, ents);
      uint32_t ent = Pick(g, {0x1549B5C, 0x1549E14, 0x1535D6C, 0, 0, 0});
      if (!ent) {
        ent = ents + 0x2B8 * (g() % 4);
        Wr(ent, g() % 4 ? g() % 3 : g() % 12);
        Wr(ent + 4, g() % 4 ? 0 : uint32_t(g()));
      }
      Wr(0x17F0C7C, ent);
      if (g() % 8 == 0) Wr(0x15497F8, ents + 0x2B8 * (1 + g() % 3));  // past it: a negative index
      g_mem.write<uint8_t>(Rd(0x17F0C70) + 0x4C, uint8_t(g() % 4 ? 0 : g()));
      for (uint32_t i = 0; i < 64; i += 4) {
        Wr(0x1549AC4 + i, g() % 8 ? Mixed(g) : RandFloat(g));
        Wr(0x15498E0 + i, g() % 8 ? Mixed(g) : RandFloat(g));
      }
      Wr(0x13F9D04, g());
      Wr(0x17F0C60, g() % 4 ? 8 : 4);  // positions 8 floats apart (4 are skipped)
      if (g() % 2) Wr(0x17F0C84, g() % 16);
      k.ebx = 0x1618C40;
      break;
    }
    case 0x5033B0:
    case 0x503590:
    case 0x503760: {
      // A surface of at most 20 vertices and 120 indexes into a tessellator
      // of at most 20 (now and then over, for RB_CheckOverflow), each array
      // apart (an axis now and then none); for the optimised batch at most
      // 100 indexes so far, now and then so many that its own take it to
      // 0x20000.
      if (Rd(0x17F0C88) > 20) Wr(0x17F0C88, g() % 21);
      if (g() % 32 == 0) Wr(0x17F0C88, 0x2000 - g() % 24);
      if (g() % 32 == 0) Wr(0x17F0C84, 0xC000 - g() % 140);
      const uint32_t verts = g() % 8 ? g() % 21 : 0;
      uint32_t n = g() % 8 ? g() % 121 : 0;
      Wr(0x1830C94, g() % 101);
      Wr(0x1830C98, g() % 1000);
      Wr(0x17F0C8C, g() % 4 ? g() % 1000 : uint32_t(g()));
      Wr(0x17F0C90, g() % 4 ? g() % 1000 : g() % 2 ? 0 : uint32_t(g()));
      Wr(0x15497FC, g() % 2 ? 0 : 1 + g() % 4);
      Wr(0x153479C, g() % 2);
      Wr(0x1549548, g() % 2);
      Wr(0x153465C, g() % 2);
      Wr(0x17F0C7C, Rd(0x15497F8) + 0x2B8 * (g() % 4));
      if (kEntries[k.entry].va == 0x503760 && g() % 32 == 0) {
        n %= 41;
        Wr(0x1830C94, 0x20000 - n + g() % 4);
      }
      const uint32_t surf = a.Alloc(0x48 + 2 * std::min(n, 120u));
      Wr(surf + 8, g() % 2 ? 0 : uint32_t(g()));
      Wr(surf + 0x24, verts);
      Wr(surf + 0x44, n);
      for (uint32_t i = 0; i < 2 * std::min(n, 120u); i += 2) g_mem.write<uint16_t>(surf + 0x48 + i, uint16_t(g() % 4 ? g() % 500 : g()));
      for (uint32_t at = 0x28; at < 0x44; at += 4) {
        const uint32_t v = a.Alloc(12 * 20);
        for (uint32_t i = 0; i < 12 * 20; i += 4) Wr(v + i, RandFloat(g));
        Wr(surf + at, at < 0x30 && g() % 4 == 0 ? 0 : v);
      }
      if (kEntries[k.entry].va == 0x503590) k.esi = surf;
      else k.args = {surf};
      break;
    }
    case 0x500F20:
    case 0x502110:
    case 0x502AF0:
    case 0x502CE0:
    case 0x5012D0:
    case 0x501240:
    case 0x5014C0:
    case 0x502350:
    case 0x503010:
    case 0x5030E0:
    case 0x503230:
    case 0x502450:
    case 0x501900:
    case 0x503860:
    case 0x5039A0: {
      // An entity (now and then its ends the same, a rotation out of
      // FSINCOS's range) and its quads into a tessellator of at most 20
      // vertices, now and then at its limits; the view's axes, its field
      // of view, the cvars the beams read, the line width last set. The
      // entity's positions and first axis and the view's origin finite, as
      // VectorNormalize needs them (above); the quads' own inputs any. For
      // the entity's surface, each type (now and then any).
      if (Rd(0x17F0C88) > 20) Wr(0x17F0C88, g() % 21);
      if (g() % 32 == 0) Wr(0x17F0C88, 0x2000 - g() % 70);
      if (g() % 32 == 0) Wr(0x17F0C84, 0xC000 - g() % 200);
      bool special = false;
      auto coord = [&] {
        return !special && g() % 8 ? Float(float(int(g() % 20001) - 10000) / 100.0f) : RandFloat(g);
      };
      auto vec = [&] {
        const uint32_t v = a.Alloc(12);
        for (uint32_t i = 0; i < 12; i += 4) Wr(v + i, coord());
        return v;
      };
      auto finite = [&] { return g() % 8 ? Float(float(int(g() % 20001) - 10000) / 100.0f) : Mixed(g); };
      const uint32_t ent = a.Alloc(0x90);
      for (uint32_t i = 0x18; i < 0x90; i += 4) Wr(ent + i, i < 0x60 ? finite() : coord());
      // The sprites' origin any (they never normalise it).
      const uint32_t va = kEntries[k.entry].va;
      if (va == 0x501240 || va == 0x5012D0 || va == 0x5014C0)
        for (uint32_t i = 0x44; i < 0x50; i += 4) Wr(ent + i, g() % 2 ? RandFloat(g) : coord());
      for (uint32_t i = 0; i < 12; i += 4) Wr(0x1549828 + i, finite());
      Wr(ent, g() % 8 ? g() % 17 : uint32_t(g()));  // its type, for 0x5039A0
      // Its ends now and then the same, but for type 10, whose turns take
      // a zero axis to a NaN in VectorNormalize (above).
      if (g() % 8 == 0 && kEntries[k.entry].va != 0x503230 && Rd(ent) != 10)
        for (uint32_t i = 0; i < 12; i += 4) Wr(ent + 0x54 + i, Rd(ent + 0x44 + i));
      for (uint32_t r : {0x64u, 0x7Cu}) Wr(ent + r, g() % 8 ? Float(float(1 + g() % 3200) / 100.0f) : RandFloat(g));
      Wr(ent + 0x6C, uint32_t(g()));
      Wr(ent + 0x80, Pick(g, {0, 0, Float(float(int(g() % 1441) - 720)), RandFloat(g), Float(0x1p70f)}));
      Wr(0x1549B3C, ent);
      Wr(0x17F0C7C, g() % 2 ? ent : a.Alloc(0x90));
      Wr(Rd(0x17F0C7C) + 0x6C, uint32_t(g()));
      for (uint32_t i = 0; i < 0x24; i += 4) Wr(0x1549834 + i, g() % 8 ? Float(float(int(g() % 2001) - 1000) / 1000.0f) : RandFloat(g));
      Wr(0x1549960, g() % 8 ? Float(float(30 + g() % 100)) : RandFloat(g));
      Wr(0x1534470, Cvar(a, g, 0));
      // Type 9's ring length: its length over 1 to 40 rings (more would
      // run the tessellator past the windows),
      // now and then 0, negative, infinite or a NaN (one ring).
      double len = 0;
      for (uint32_t i = 0; i < 12; i += 4) {
        const double d = double(std::bit_cast<float>(Rd(ent + 0x44 + i))) - std::bit_cast<float>(Rd(ent + 0x54 + i));
        len += d * d;
      }
      Wr(Rd(0x1534470) + 0x1C, g() % 8 ? Float(float(std::sqrt(len) / double(1 + g() % 40)) + 1e-3f)
                                       : Pick(g, {0, 0xBF800000, 0x7F800000, 0x7FC00000}));
      Wr(0x1534540, Cvar(a, g, g() % 8 ? g() % 12 : uint32_t(g())));
      Wr(0x15344A8, Cvar(a, g, 0));
      Wr(0x15495CC, Pick(g, {Float(1.0f), Float(3.0f), RandFloat(g)}));
      switch (kEntries[k.entry].va) {
        case 0x500F20: {
          const uint32_t colour = a.Alloc(4);
          Wr(colour, uint32_t(g()));
          k.args = {vec(), vec(), vec(), colour, RandFloat(g), RandFloat(g), RandFloat(g), RandFloat(g)};
          break;
        }
        case 0x502110:
          k.eax = vec();
          k.ecx = vec();
          k.edi = vec();
          k.args = {coord()};
          break;
        case 0x502AF0:
          k.eax = vec();
          k.edx = vec();
          k.ecx = vec();
          k.args = {coord(), coord()};
          break;
        case 0x502CE0:
          // Now and then every input special (a step's SNaN against a
          // point's QNaN).
          special = g() % 4 == 0;
          k.eax = vec();
          k.ecx = vec();
          k.ebx = vec();
          k.args = {g() % 8 ? uint32_t(g() % 6) : uint32_t(int(g() % 5) - 2), vec()};
          break;
        default:
          k.esi = g();
          k.edi = g();
          break;
      }
      break;
    }
  }
  // Near the tessellator's limits, which RB_CheckOverflow flushes, not
  // the shader with its own flush (its shadow volume would read the edges
  // of 0x2000 vertices).
  if (Rd(0x17F0C88) > 0x400 || Rd(0x17F0C84) > 0x1000) Wr(0x1535AEC, 0);
  Run(k, a.top - kArena, g, s);
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // messages before an abort
  if (argc < 2) {
    std::printf("usage: %s CoDSP.exe\n", argv[0]);
    return 2;
  }
  if (!g_mem.Reserve()) return 1;
  recomp::g_guest_base = g_mem.base();
  recomp::PeImage exe;
  recomp::g_exe = &exe;
  std::string err;
  if (!exe.Load(argv[1], g_mem, &err)) {
    std::printf("%s: %s\n", argv[1], err.c_str());
    return 1;
  }
  g_mem.Commit(kArena, kArenaSize, recomp::Prot::RW);
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);
  // A null image's fields (GL_Bind reads them after its warning).
  g_mem.Commit(0, 0x1000, recomp::Prot::RW);
  recomp::x87::run::g_doubles = false;

  std::mt19937 g(20261025);
  // The waveforms' tables (sin, square, triangle, sawtooth, inverse
  // sawtooth: 1024 floats each) and the noise's permutation and values, as
  // the renderer's init fills them, with now and then a special value.
  for (uint32_t i = 0; i < 5 * 0x1000; i += 4)
    Wr(0x1544530 + i, g() % 16 ? Float(float(int(g() % 2001) - 1000) / 1000.0f) : RandFloat(g));
  // The FFT's bit reversals and twiddles (cos, sin), as the water's init
  // makes them, the twiddles now and then special.
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t r = 0;
    for (uint32_t b = 0; b < 8; b++) r |= ((i >> b) & 1) << (7 - b);
    Wr(0x11E12B0 + 4 * i, r);
    const float t = float(i) * 6.2831855f / 256.0f;
    Wr(0x11E0AA8 + 8 * i, g() % 64 ? Float(std::cos(t)) : RandFloat(g));
    Wr(0x11E0AAC + 8 * i, g() % 64 ? Float(std::sin(t)) : RandFloat(g));
  }
  for (uint32_t i = 0; i < 0x400; i += 4) {
    Wr(0x1195C38 + i, g() & 0xFF);
    Wr(0x1196038 + i, g() % 16 ? Float(float(int(g() % 2001) - 1000) / 1000.0f) : RandFloat(g));
  }

  Stats s;
  Arena a;
  for (int i = 0; i < 200000; i++) Cases(a, g, s);
  std::printf("%d random cases (", s.cases);
  for (int i = 0; i < kNumEntries; i++)
    std::printf("%s%s %d", i ? ", " : "", kEntries[i].name, s.by_entry[i]);
  std::printf("; %d calls out): %d mismatched\n", s.calls, s.failures);
  return s.failures ? 1 : 0;
}
