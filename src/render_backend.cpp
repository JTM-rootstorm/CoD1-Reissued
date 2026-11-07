// The renderer's back end natively, from the bottom up: the layer that
// turns the renderer's state into GL calls (GL_Bind, the cull face, the
// texture environment, the state bits, the client arrays, the fog colour
// and the element draws), a stage's texture units (each bundle's image,
// texture, environment and coordinates, also from an ATI vertex array
// object), what a flush sets up before its stages draw: the fog, the
// texture units back to one, the vertex deforms and each stage's colours
// (with the colour and alpha generators), and the flush itself: a stage's
// draw, the stage loop, the generic stage iterator with the dlight pass,
// the optimised iterators and their dispatch, the shadow volumes of shader
// 0x1535AEC, and RB_EndSurface; and the static model surfaces, which fill
// the tessellator or the optimised batch or draw themselves, and the
// table's other surfaces: bad, skipped, a polygon, an entity's (by its
// type) and the two of 24 and 25 that fill the tessellator and the
// optimised batch; and an entity's surfaces by its type, with the quads
// they add. Replacements of lifted functions named with `replace`
// in manifest/CoDSP.manifest; FINDINGS.md "Renderer back end". Names and
// evidence are in the Ghidra plate comments.
//
// The GL calls go through the renderer's qgl function pointers (set by
// its QGL_Init from opengl32's exports and wglGetProcAddress; the names
// below are the entry points it loads into each), with the original's
// arguments on the guest stack at the original's ESP and its return
// addresses, as the lifted call sites make them. Calling conventions are
// the originals' (register arguments from whole-program optimisation; see
// each function). Not reproduced: EAX, ECX and EDX on return (no caller
// reads them), the saved registers and locals in the original's frame
// below ESP (but for the shadow volumes', f_005082D0), EFLAGS and the x87
// status word (x87_value.h).

#include <cstdint>
#include <initializer_list>

#include "x86/lifted.h"
#include "x87_value.h"

namespace recomp::gen {

void f_004C89D0(Ctx& c);
void f_004C8A20(Ctx& c);
void f_004F3180(Ctx& c);
void f_004F3330(Ctx& c);
void f_004F3410(Ctx& c);
void f_004F3810(Ctx& c);
void f_004F38B0(Ctx& c);
void f_004F3920(Ctx& c);
void f_004FCE80(Ctx& c);  // updates a video generator's texture (ECX; time, unit)
void f_004F3A70(Ctx& c);  // sets up NV register combiners (combiners)
void f_004F3C70(Ctx& c);  // sets up a vertex program (ESI program)
void f_004C8830(Ctx& c);
void f_004C89A0(Ctx& c);
void f_0050ABF0(Ctx& c);
void f_0050AA30(Ctx& c);
void f_00508940(Ctx& c);  // a waveform's table (ECX type; ri.Error for another)
void f_004D3AA0(Ctx& c);  // the noise (x, y, z, t; ST0)
void f_00426710(Ctx& c);  // VectorNormalize (ESI; the length in ST0)
void f_00500F20(Ctx& c);  // adds a quad (origin, left, up, colour, s1, t1, s2, t2)
void f_0052E610(Ctx& c);  // the CRT's square root (ST0)
void f_0051AB30(Ctx& c);  // the CRT's floor (a double; ST0)
void f_0051B5EC(Ctx& c);  // rand
void f_0051B5DF(Ctx& c);  // srand (seed)
void f_00426A60(Ctx& c);  // rotates ECX by the axes EAX into EDX
void f_004268B0(Ctx& c);  // normalises EAX (approximate inverse square root)
void f_004F5940(Ctx& c);  // ComputeTexCoords (EAX stage; src/render.cpp)
void f_004ADC20(Ctx& c);  // begins the immediate-mode batch
void f_004F44A0(Ctx& c);  // the debug lines of cvar 0x15343E8 (EAX the tessellator)
void f_004F4560(Ctx& c);  // the white normals (the tessellator)
void f_004F4850(Ctx& c);  // the coloured normals (EBX the tessellator)
void f_0052AB6F(Ctx& c);  // _stricmp
void f_00431CF0(Ctx& c);  // sets a cvar (name, value; EAX force)
void f_0042E300(Ctx& c);  // prints (format, ...)
void f_00440940(Ctx& c);  // va (format, ...)
void f_0051D45A(Ctx& c);  // fprintf
void f_0042B0A0(Ctx& c);  // the memcpy (src/render.cpp)
void f_00500A60(Ctx& c);  // RB_CheckOverflow (verts, indexes)
void f_004F4AB0(Ctx& c);  // begins the tessellator (ECX shader; position stride)
void f_00426D30(Ctx& c);  // two axes across EDI into EAX and EBX
void f_00426AB0(Ctx& c);  // rotates a point about EAX (out, point, degrees)
void f_004273C0(Ctx& c);  // a vector across EBX into EDX
void f_004ADEC0(Ctx& c);  // the immediate-mode batch's vertex (x, y, z)
// An entity's surfaces by its type (0x5039A0).
void f_005012D0(Ctx& c);
void f_00501240(Ctx& c);
void f_00501900(Ctx& c);
void f_005030E0(Ctx& c);
void f_00503010(Ctx& c);
void f_00503230(Ctx& c);
void f_005014C0(Ctx& c);
void f_00502350(Ctx& c);
void f_00502450(Ctx& c);
void f_00503860(Ctx& c);

namespace {

using lift::Rd;
using lift::Wr;

// ---- guest memory and calls ----------------------------------------------

uint32_t U32(uint32_t va) { return Rd<uint32_t>(g_guest_base, va); }
uint16_t U16(uint32_t va) { return Rd<uint16_t>(g_guest_base, va); }
uint8_t U8(uint32_t va) { return Rd<uint8_t>(g_guest_base, va); }
int32_t I32(uint32_t va) { return int32_t(U32(va)); }
void Put(uint32_t va, uint32_t v) { Wr<uint32_t>(g_guest_base, va, v); }
F32 Ld(uint32_t va) { return {U32(va)}; }
void Put(uint32_t va, F32 v) { Put(va, v.bits); }

void Push(Ctx& c, uint32_t v) {
  c.esp -= 4;
  Put(c.esp, v);
}
void Call(Ctx& c, GuestFn fn, uint32_t ret) {
  Push(c, ret);
  fn(c);
}
// An indirect call to target from the call instruction at va, `call reg`.
void CallTo(Ctx& c, uint32_t target, uint32_t va) {
  Push(c, va + 2);
  Dispatch(c, target, va);
}
// An indirect call from the call instruction at va, `call [ptr]`.
void CallPtr(Ctx& c, uint32_t ptr, uint32_t va) {
  Push(c, va + 6);
  Dispatch(c, U32(ptr), va);
}
uint32_t Arg(const Ctx& c, uint32_t e, int n) { return U32(e + 4 * n); }
// The integer of a cvar_t* held at va.
uint32_t CvarInt(uint32_t va) { return U32(U32(va) + 0x20); }

// The qgl pointers.
constexpr uint32_t kglEnable = 0x1534F68, kglDisable = 0x15349E0,
                   kglBindTexture = 0x153507C, kglTexEnvi = 0x153515C,
                   kglCullFace = 0x1535284, kglDepthFunc = 0x1534844,
                   kglDepthMask = 0x1534D68, kglPolygonMode = 0x1534D74,
                   kglAlphaFunc = 0x1535208, kglColor3f = 0x1535050,
                   kglPolygonOffset = 0x15350B0, kglBlendFunc = 0x1534ABC,
                   kglFogfv = 0x15349F4, kglEnableClientState = 0x1534938,
                   kglDisableClientState = 0x153495C,
                   kglClientActiveTextureARB = 0x1534B78, kglBegin = 0x1535360,
                   kglEnd = 0x1534EE0, kglDrawElements = 0x153513C,
                   kglArrayElement = 0x1535104, kglLockArraysEXT = 0x1534DDC,
                   kglActiveTextureARB = 0x1534C14, kglTexEnvfv = 0x1534A10,
                   kglTexEnvf = 0x15349B8, kglTexCoordPointer = 0x15349A0,
                   kglFogi = 0x1534C84, kglFogf = 0x1535598, kglHint = 0x15351A0,
                   kglColorPointer = 0x153542C, kglNormalPointer = 0x153503C,
                   kglVertexPointer = 0x15354F0, kglUnlockArraysEXT = 0x1534C2C,
                   kglArrayObjectATI = 0x1535390, kglBindBufferARB = 0x1534DE4,
                   kglDrawRangeElementsEXT = 0x1534DB0, kglDepthRange = 0x153539C,
                   kglColor4ubv = 0x1534EB0, kglDrawArrays = 0x153501C,
                   kglColorMask = 0x1534DF8, kglStencilFunc = 0x1534E7C,
                   kglStencilOp = 0x1535428;
// The refimport table's ri.Printf and ri.Error (cdecl).
constexpr uint32_t kPrintf = 0x1549600, kError = 0x1549604;

// The renderer's cached GL state: the state bits, the client array bits
// (texture coordinates of unit n: bit n; colours 0x100, normals 0x200,
// vertices 0x400), the cull type, the current texture unit and client
// unit, and per unit the bound texture, the texture environment and the
// enabled texture target.
constexpr uint32_t kStateBits = 0x1534630, kArrayBits = 0x1534634,
                   kCullType = 0x153462C, kUnit = 0x15345A0,
                   kClientUnit = 0x15345A4, kBound = 0x15345A8,
                   kTexEnv = 0x15345C8, kTarget = 0x15345E8;
// The number of texture units the renderer uses.
constexpr uint32_t kNumUnits = 0x1534758;
// tr.identityLight (float) and its byte.
constexpr uint32_t kIdentityLight = 0x1536290, kIdentityLightByte = 0x1536294;
// The tessellator: its shader, vertex count, positions (kXyzStride dwords
// apart), normals (12 bytes apart) and vertex colours, its indexes and
// index count; and the stage colours computed from them.
constexpr uint32_t kTessIndexes = 0x1618C40, kTessIndexCount = 0x17F0C84;
constexpr uint32_t kTessShader = 0x17F0C70, kTessVerts = 0x17F0C88,
                   kTessXyz = 0x1630C40, kXyzStride = 0x17F0C60,
                   kTessNormals = 0x1698C40, kVertColors = 0x1690C40,
                   kColors = 0x16E0C40;

void Enable(Ctx& c, uint32_t cap, uint32_t va) {
  Push(c, cap);
  CallPtr(c, kglEnable, va);
}
void Disable(Ctx& c, uint32_t cap, uint32_t va) {
  Push(c, cap);
  CallPtr(c, kglDisable, va);
}

}  // namespace

// ---- fog ------------------------------------------------------------------

// Turns fog on for the state bits' fog flag (0x200000), unless it is on or
// the frame has no fog: the fog cvar (0x15347F4) on and the view's fog
// (0x1535890 when the refdef's flags have 8, else the world's 0x13F9CF8).
// Sets the flag in the cached state bits.
void f_004C89D0(Ctx& c) {
  if (!U32(0x1549B50) && !(U32(kStateBits) & 0x200000) && CvarInt(0x15347F4)) {
    const uint32_t fog = (U8(0x15496EC) & 8) ? U32(0x1535890) : U32(0x13F9CF8);
    if (fog) {
      Enable(c, 0xB60, 0x4C8A0D);
      Put(kStateBits, U32(kStateBits) | 0x200000);
    }
  }
  c.esp += 4;
}

// The fog colour for the current blend: black where the destination blend
// is GL_ONE (it would brighten what is under), else the fog colour
// (0x1534640, rgb) times tr.identityLight with its alpha.
void f_004C8A20(Ctx& c) {
  const uint32_t e = c.esp;
  if (U32(0x13F9CF8)) {
    const uint32_t col = e - 0x10;
    c.esp = col;
    if ((U32(kStateBits) & 0xF0) == 0x20) {
      Put(col + 8, 0);
      Put(col + 4, 0);
      Put(col, 0);
    } else {
      Put(col, Fst(St(Ld(kIdentityLight)) * Ld(0x1534640)));
      Put(col + 4, Fst(St(Ld(0x1534644)) * Ld(kIdentityLight)));
      Put(col + 8, Fst(St(Ld(0x1534648)) * Ld(kIdentityLight)));
    }
    Put(col + 0xC, U32(0x153464C));
    Push(c, col);
    Push(c, 0xB66);
    CallPtr(c, kglFogfv, 0x4C8A95);
  }
  c.esp = e + 4;
}

// ---- textures ---------------------------------------------------------------

// GL_Bind (ESI image): enables the image's target (+0x54) on the current
// unit in place of the one enabled there, and binds its texture (+0x58;
// the default image's for a null one, after a warning; the r_nobind image's
// when r_nobind is set), stamping the image with the frame count (+0x5C).
void f_004CB3B0(Ctx& c) {
  const uint32_t e = c.esp, image = c.esi;
  c.esp = e - 4;
  uint32_t texnum;
  if (!image) {
    Push(c, 0x543064);
    Push(c, 2);
    CallPtr(c, kPrintf, 0x4CB3BC);
    texnum = U32(U32(0x1535A38) + 0x58);
    c.esp += 8;
  } else {
    texnum = U32(image + 0x58);
  }
  if (CvarInt(0x1534460) && U32(0x1535ABC)) texnum = U32(U32(0x1535ABC) + 0x58);
  const uint32_t target = U32(kTarget + 4 * U32(kUnit));
  if (target != U32(image + 0x54)) {
    if (target) Disable(c, target, 0x4CB402);
    Enable(c, U32(image + 0x54), 0x4CB40C);
    Put(kTarget + 4 * U32(kUnit), U32(image + 0x54));
  }
  if (U32(kBound + 4 * U32(kUnit)) != texnum) {
    Put(image + 0x5C, U32(0x1535A24));
    Put(kBound + 4 * U32(kUnit), texnum);
    Push(c, texnum);
    Push(c, U32(image + 0x54));
    CallPtr(c, kglBindTexture, 0x4CB44B);
  }
  c.esp = e + 4;
}

// The current unit's texture environment mode (EAX): GL_MODULATE, GL_ADD,
// GL_REPLACE or GL_DECAL; anything else is an error.
void f_004CB640(Ctx& c) {
  const uint32_t env = c.eax, slot = kTexEnv + 4 * U32(kUnit);
  if (env != U32(slot)) {
    Put(slot, env);
    uint32_t va = 0;
    switch (env) {
      case 0x1E01: va = 0x4CB67C; break;
      case 0x104: va = 0x4CB692; break;
      case 0x2100: va = 0x4CB6A8; break;
      case 0x2101: va = 0x4CB6D7; break;
    }
    if (va) {
      Push(c, env);
      Push(c, 0x2200);
      Push(c, 0x2300);
      CallPtr(c, kglTexEnvi, va);
    } else {
      Push(c, env);
      Push(c, 0x54303C);
      Push(c, 1);
      CallPtr(c, kError, 0x4CB6BE);
      c.esp += 0xC;
    }
  }
  c.esp += 4;
}

// ---- state ------------------------------------------------------------------

// The cull type (ESI): 0 front-facing, 1 back-facing (swapped while the
// view is mirrored, 0x1549930), 2 none.
void f_004CB5D0(Ctx& c) {
  const uint32_t type = c.esi, cur = U32(kCullType);
  if (cur != type) {
    if (type == 2) {
      Disable(c, 0xB44, 0x4CB5E3);
    } else {
      if (cur == 2) Enable(c, 0xB44, 0x4CB5FA);
      const bool back = (type == 1) == (U32(0x1549930) == 0);
      Push(c, back ? 0x405 : 0x404);
      CallPtr(c, kglCullFace, back ? 0x4CB613 : 0x4CB629);
    }
    Put(kCullType, type);
  }
  c.esp += 4;
}

// The state bits (EAX), against the cached ones: depth test (0x10000 off)
// and function (0x20000 equal, 0x40000 always, else lequal), depth mask
// (0x100), line mode (0x1000), alpha test (0x70000000: gt 0, lt 0.5, ge
// 0.5), lighting (0x100000; only with the light cvars, else a white
// colour and the flag cleared), fog (0x200000), polygon offset (0xE000),
// four extension capabilities (0x2000000, 0x400000, 0x800000, 0x1000000)
// and the blend (the low byte: source and destination factors), after
// which the fog colour follows the blend.
void f_004CB710(Ctx& c) {
  const uint32_t e = c.esp;
  uint32_t bits = c.eax;
  const uint32_t diff = U32(kStateBits) ^ bits;
  c.esp = e - 8;
  if (!diff) {
    c.esp = e + 4;
    return;
  }
  if (diff & 0x70000) {
    if (diff & 0x10000) {
      if (bits & 0x10000) Disable(c, 0xB71, 0x4CB740);
      else Enable(c, 0xB71, 0x4CB748);
    }
    Push(c, (bits & 0x20000) ? 0x202 : (bits & 0x40000) ? 0x207 : 0x203);
    CallPtr(c, kglDepthFunc, 0x4CB771);
  }
  c.esp = e - 0xC;
  if (diff & 0x100) {
    Push(c, (bits & 0x100) ? 1 : 0);
    CallPtr(c, kglDepthMask, 0x4CB78F);
  }
  if (diff & 0x1000) {
    Push(c, (bits & 0x1000) ? 0x1B01 : 0x1B02);
    Push(c, 0x408);
    CallPtr(c, kglPolygonMode, 0x4CB7B4);
  }
  if (diff & 0x70000000) {
    switch (bits & 0x70000000) {
      case 0:
        Disable(c, 0xBC0, 0x4CB7F8);
        break;
      case 0x10000000:
        Enable(c, 0xBC0, 0x4CB7E4);
        Push(c, 0);
        Push(c, 0x204);
        CallPtr(c, kglAlphaFunc, 0x4CB833);
        break;
      case 0x20000000:
        Enable(c, 0xBC0, 0x4CB805);
        Push(c, 0x3F000000);
        Push(c, 0x201);
        CallPtr(c, kglAlphaFunc, 0x4CB833);
        break;
      case 0x40000000:
        Enable(c, 0xBC0, 0x4CB823);
        Push(c, 0x3F000000);
        Push(c, 0x206);
        CallPtr(c, kglAlphaFunc, 0x4CB833);
        break;
    }
  }
  if (diff & 0x100000) {
    if (U32(U32(0x1535A34) + 0x118) && !CvarInt(0x15346C4) && (bits & 0x100000)) {
      Enable(c, 0xB50, 0x4CB86C);
    } else {
      Disable(c, 0xB50, 0x4CB879);
      if (bits & 0x100000) {
        if (U32(kArrayBits) & 0x100) {
          Push(c, 0x8076);
          CallPtr(c, kglDisableClientState, 0x4CB894);
          Put(kArrayBits, U32(kArrayBits) & ~0x100u);
        }
        const uint32_t white = U32(kIdentityLight);
        Push(c, white);
        Push(c, white);
        Push(c, white);
        CallPtr(c, kglColor3f, 0x4CB8AC);
        bits &= ~0x100000u;
      }
    }
  }
  if (diff & 0x200000) {
    if (bits & 0x200000) {
      Call(c, f_004C89D0, 0x4CB8CE);
    } else if (U32(kStateBits) & 0x200000) {
      Disable(c, 0xB60, 0x4CB8DD);
      Put(kStateBits, U32(kStateBits) & ~0x200000u);
    }
  }
  if (diff & 0xE000) {
    if (bits & 0xE000) {
      Enable(c, 0x8037, 0x4CB900);
      const uint32_t units = U32(0x1534530) + 0x1C, factor = U32(0x153447C) + 0x1C;
      if (bits & 0x2000) {
        Push(c, U32(units));
        Push(c, U32(factor));
        CallPtr(c, kglPolygonOffset, 0x4CB91F);
      } else if (bits & 0x4000) {
        c.esp -= 8;
        const St u(Ld(units));
        Put(c.esp + 4, Fst(u + u));
        const St f(Ld(factor));
        Put(c.esp, Fst(f + f));
        CallPtr(c, kglPolygonOffset, 0x4CB94B);
      } else {
        Push(c, U32(units));
        Push(c, 0);
        CallPtr(c, kglPolygonOffset, 0x4CB95F);
      }
    } else {
      Disable(c, 0x8037, 0x4CB967);
    }
  }
  struct Cap {
    uint32_t bit, cap, enable_va, disable_va;
  };
  static constexpr Cap kCaps[] = {
      {0x2000000, 0x8620, 0x4CB984, 0x4CB98C},
      {0x400000, 0x86DE, 0x4CB9A9, 0x4CB9B1},
      {0x800000, 0x8522, 0x4CB9CE, 0x4CB9D6},
      {0x1000000, 0x8920, 0x4CB9F3, 0x4CB9FB},
  };
  for (const Cap& k : kCaps) {
    if (!(diff & k.bit)) continue;
    if (bits & k.bit) Enable(c, k.cap, k.enable_va);
    else Disable(c, k.cap, k.disable_va);
  }
  Put(kStateBits, bits);
  if (!(diff & 0xFF)) {
    c.esp = e + 4;
    return;
  }
  if (bits & 0xFF) {
    c.esp = e - 0x10;
    static constexpr uint32_t kSrc[9] = {0, 1, 0x306, 0x307, 0x302, 0x303, 0x304, 0x305, 0x308};
    static constexpr uint32_t kDst[8] = {0, 1, 0x300, 0x301, 0x302, 0x303, 0x304, 0x305};
    const uint32_t s = bits & 0xF, d = bits & 0xF0;
    uint32_t src, dst;
    if (s >= 1 && s <= 9) {
      src = kSrc[s - 1];
    } else {
      src = 1;
      Push(c, 0x543010);
      Push(c, 1);
      CallPtr(c, kError, 0x4CBA77);
      c.esp += 8;
    }
    if (d >= 0x10 && d <= 0x80) {
      dst = kDst[(d >> 4) - 1];
    } else {
      dst = 1;
      Push(c, 0x542FE4);
      Push(c, 1);
      CallPtr(c, kError, 0x4CBADC);
      c.esp += 8;
    }
    Enable(c, 0xBE2, 0x4CBAEA);
    Push(c, dst);
    Push(c, src);
    CallPtr(c, kglBlendFunc, 0x4CBAF2);
  } else {
    Disable(c, 0xBE2, 0x4CBB08);
  }
  c.esp = e;
  f_004C8A20(c);
}

// The client arrays (bits on the stack, as the cached array bits): each
// unit's texture coordinates (selecting the unit for it and the cached
// client unit again after), colours, normals and vertices.
void f_004CBBE0(Ctx& c) {
  const uint32_t e = c.esp, bits = Arg(c, e, 1);
  const uint32_t diff = U32(kArrayBits) ^ bits;
  c.esp = e - 4;
  if (diff) {
    // Per unit: the calls selecting it, enabling, disabling and selecting
    // the cached client unit again.
    static constexpr uint32_t kUnitVa[8][4] = {
        {0x4CBC06, 0x4CBC18, 0x4CBC20, 0x4CBC35}, {0x4CBC4F, 0x4CBC61, 0x4CBC69, 0x4CBC7F},
        {0x4CBC99, 0x4CBCAB, 0x4CBCB3, 0x4CBCC9}, {0x4CBCE3, 0x4CBCF5, 0x4CBCFD, 0x4CBD13},
        {0x4CBD2D, 0x4CBD3F, 0x4CBD47, 0x4CBD5D}, {0x4CBD77, 0x4CBD89, 0x4CBD91, 0x4CBDA7},
        {0x4CBDC1, 0x4CBDD3, 0x4CBDDB, 0x4CBDF1}, {0x4CBE0A, 0x4CBE1D, 0x4CBE25, 0x4CBE3B},
    };
    for (uint32_t u = 0; u < 8; u++) {
      const uint32_t bit = 1u << u;
      if (!(diff & bit)) continue;
      if (U32(kClientUnit) != u) {
        Push(c, 0x84C0 + u);
        CallPtr(c, kglClientActiveTextureARB, kUnitVa[u][0]);
      }
      Push(c, 0x8078);
      if (bits & bit) CallPtr(c, kglEnableClientState, kUnitVa[u][1]);
      else CallPtr(c, kglDisableClientState, kUnitVa[u][2]);
      const uint32_t cur = U32(kClientUnit);
      if (cur != u) {
        Push(c, cur + 0x84C0);
        CallPtr(c, kglClientActiveTextureARB, kUnitVa[u][3]);
      }
    }
    struct Array {
      uint32_t bit, array, enable_va, disable_va;
    };
    static constexpr Array kArrays[] = {
        {0x100, 0x8076, 0x4CBE54, 0x4CBE5C},
        {0x200, 0x8075, 0x4CBE75, 0x4CBE7D},
        {0x400, 0x8074, 0x4CBE96, 0x4CBEA7},
    };
    for (const Array& a : kArrays) {
      if (!(diff & a.bit)) continue;
      Push(c, a.array);
      if (bits & a.bit) CallPtr(c, kglEnableClientState, a.enable_va);
      else CallPtr(c, kglDisableClientState, a.disable_va);
    }
    Put(kArrayBits, bits);
  }
  c.esp = e + 4;
}

// ---- drawing ----------------------------------------------------------------

// Triangles (EAX the indexes, numIndexes on the stack) as strips in
// immediate mode through the element function in EDI (glArrayElement):
// each triangle continues the strip when it shares the edge a strip's
// next triangle would (alternating windings), else starts a new one.
// Counts the strips (0x11E0764) and vertices sent (0x11E0760); the
// numIndexes slot ends up the loop's counter (0).
void f_004F3180(Ctx& c) {
  const uint32_t e = c.esp, element = c.edi;
  const int32_t n = I32(e + 4);
  uint32_t p = c.eax;
  Put(0x11E0764, U32(0x11E0764) + 1);
  if (n > 0) {
    c.esp = e - 0x18;
    Push(c, 5);
    CallPtr(c, kglBegin, 0x4F31A3);
    Push(c, U16(p));
    CallTo(c, element, 0x4F31AD);  // call edi
    Push(c, U16(p + 2));
    CallTo(c, element, 0x4F31B4);
    Push(c, U16(p + 4));
    CallTo(c, element, 0x4F31BB);
    uint32_t i0 = U16(p), i1 = U16(p + 2), i2 = U16(p + 4);
    Put(0x11E0760, U32(0x11E0760) + 3);
    if (n > 3) {
      uint32_t left = uint32_t(n - 4) / 3 + 1;
      Put(e + 4, left);
      bool odd = false;
      p += 0xA;
      do {
        uint32_t sent;
        if (!odd && U16(p - 4) == i2 && U16(p - 2) == i1) {
          Push(c, U16(p));
          CallTo(c, element, 0x4F321A);
          sent = U32(0x11E0760) + 1;
          odd = true;
        } else if (odd && i2 == U16(p - 2) && i0 == U16(p - 4)) {
          Push(c, U16(p));
          CallTo(c, element, 0x4F323D);
          sent = U32(0x11E0760) + 1;
          odd = false;
        } else {
          CallPtr(c, kglEnd, 0x4F3247);
          Push(c, 5);
          CallPtr(c, kglBegin, 0x4F324F);
          Put(0x11E0764, U32(0x11E0764) + 1);
          Push(c, U16(p - 4));
          CallTo(c, element, 0x4F3267);
          Push(c, U16(p - 2));
          CallTo(c, element, 0x4F326E);
          Push(c, U16(p));
          CallTo(c, element, 0x4F3274);
          sent = U32(0x11E0760) + 3;
          odd = false;
        }
        i0 = U16(p - 4);
        i2 = U16(p);
        Put(0x11E0760, sent);
        i1 = U16(p - 2);
        p += 6;
        Put(e + 4, --left);
      } while (left);
    }
    c.esp = e;
    Dispatch(c, U32(kglEnd), 0x4F32AC);  // jmp [glEnd]
    return;
  }
  c.esp = e + 4;
}

// The triangles (EAX the indexes, EDX numIndexes) by r_primitives: 0
// glDrawElements with compiled vertex arrays, else strips; 1 strips; 2
// glDrawElements; anything else nothing. Counts the indexes and draws.
void f_004F32C0(Ctx& c) {
  const uint32_t e = c.esp, edi = c.edi, n = c.edx;
  c.esp = e - 4;
  const uint32_t prim = CvarInt(0x15346A0);
  uint32_t strips_ret = 0;
  bool elements = false;
  if (prim == 0) {
    if (U32(kglLockArraysEXT)) elements = true;
    else strips_ret = 0x4F32E4;
  } else if (prim == 2) {
    elements = true;
  } else if (prim == 1) {
    strips_ret = 0x4F332B;
  }
  if (elements) {
    Put(0x1549B14, U32(0x1549B14) + n);
    Put(0x1549B18, U32(0x1549B18) + 1);
    Push(c, c.eax);
    Push(c, 0x1403);
    Push(c, n);
    Push(c, 4);
    CallPtr(c, kglDrawElements, 0x4F3312);
  } else if (strips_ret) {
    c.edi = U32(kglArrayElement);
    Push(c, n);
    Call(c, f_004F3180, strips_ret);
    c.esp += 4;
    c.edi = edi;
  }
  c.esp = e + 4;
}

// ---- a stage's texture units --------------------------------------------------

// A texture bundle (200 bytes in a stage from +4, one per unit): images
// (+0), the animation's image count (+0x80) and speed (+0x84), a video
// generator (+0x88), the texture coordinate size (+0x8C), the texture
// environment (+0x90; GL_REPLACE when +0xC4 and r_lightmap), combiner
// parameters (+0x94, for GL_COMBINE), an NV texture shader (+0x98), a
// cinematic handle (+0xC0, used when +0xC5) and clamping of the animation
// (+0xC6).

// The bundle's image now (ESI bundle, unit on the stack; returns it in
// EAX): a cinematic's frame (run and uploaded through the refimport
// table), after the video generator updates its texture, the animation's
// frame at the shader time (0x17F0C78; frames per second x 1024, clamped
// at the last frame or wrapped), or the first image.
void f_004F3330(Ctx& c) {
  const uint32_t e = c.esp, b = c.esi;
  c.esp = e - 4;
  if (U8(b + 0xC5)) {
    Push(c, U32(b + 0xC0));
    CallPtr(c, 0x1549684, 0x4F3342);
    Push(c, U32(b + 0xC0));
    CallPtr(c, 0x154967C, 0x4F334F);
    c.eax = U32(0x1535A3C + 4 * U32(b + 0xC0));
  } else if (U32(b + 0x88)) {
    Push(c, Arg(c, e, 1));
    Push(c, U32(0x1536360));
    c.ecx = U32(b + 0x88);
    Call(c, f_004FCE80, 0x4F3382);
    c.eax = U32(b);
  } else if (I32(b + 0x80) <= 1) {
    c.eax = U32(b);
  } else {
    const F32 t = Fst(St(Ld(0x17F0C78)) * Ld(b + 0x84) * Ld(0x55B774));
    Put(e - 4, t);
    const int32_t frame = Fistp(St(t));
    Put(0x13F96D8, uint32_t(frame));
    int32_t i = frame >> 10;
    if (i < 0) i = 0;
    const int32_t n = I32(b + 0x80);
    if (U8(b + 0xC6)) {
      c.eax = U32(b + 4 * uint32_t(i < n ? i : n - 1));
    } else {
      c.edx = uint32_t(i % n);
      c.eax = U32(b + 4 * c.edx);
    }
  }
  c.esp = e + 4;
}

// Binds a bundle (EAX) on unit EDI: its image now, the image's target and
// texture, and its texture environment (selecting the unit if any of them
// changes); then GL_COMBINE's parameters, and the NV texture shader when
// the bundle has one or the unit had one on.
void f_004F3410(Ctx& c) {
  const uint32_t e = c.esp, b = c.eax, unit = c.edi, esi = c.esi;
  uint32_t env = U32(b + 0x90);
  if (U8(b + 0xC4) && CvarInt(0x15343B4)) env = 0x1E01;
  c.esp = e - 0x10;
  Push(c, unit);
  c.esi = b;
  Call(c, f_004F3330, 0x4F3445);
  c.esi = esi;
  c.esp += 4;
  const uint32_t image = c.eax, target = U32(image + 0x54);
  const uint32_t tgt = kTarget + 4 * unit, bound = kBound + 4 * unit, slot = kTexEnv + 4 * unit;
  if (U32(tgt) != target || U32(image + 0x58) != U32(bound) || env != U32(slot)) {
    if (unit != U32(kUnit)) {
      Push(c, unit + 0x84C0);
      CallPtr(c, kglActiveTextureARB, 0x4F3484);
      Put(kUnit, unit);
    }
    const uint32_t cur = U32(tgt);
    if (cur != target) {
      if (cur) Disable(c, cur, 0x4F34A0);
      Enable(c, target, 0x4F34A7);
      Put(tgt, target);
    }
    if (U32(image + 0x58) != U32(bound)) {
      Push(c, U32(image + 0x58));
      Push(c, target);
      Put(image + 0x5C, U32(0x1535A24));
      CallPtr(c, kglBindTexture, 0x4F34CB);
      Put(bound, U32(image + 0x58));
    }
    if (env != U32(slot)) {
      Push(c, env);
      Push(c, 0x2200);
      Push(c, 0x2300);
      CallPtr(c, kglTexEnvi, 0x4F34F3);
      Put(slot, env);
    }
  }
  c.esp = e - 8;
  if (env == 0x8570) {
    if (unit != U32(kUnit)) {
      Push(c, unit + 0x84C0);
      CallPtr(c, kglActiveTextureARB, 0x4F3523);
      Put(kUnit, unit);
    }
    Push(c, U32(b + 0x94));
    Push(c, 0x2201);
    Push(c, 0x2300);
    CallPtr(c, kglTexEnvfv, 0x4F3540);
    // The combiner's parameters and their fields, each its own call site.
    static constexpr struct {
      uint32_t pname, field;
      bool f;
    } kCombine[] = {
        {0x8571, 0x10}, {0x8573, 0x2C, true}, {0x8580, 0x14}, {0x8581, 0x18},
        {0x8582, 0x1C}, {0x8590, 0x20},       {0x8591, 0x24}, {0x8592, 0x28},
        {0x8572, 0x30}, {0xD1C, 0x4C, true},  {0x8588, 0x34}, {0x8589, 0x38},
        {0x858A, 0x3C}, {0x8598, 0x40},       {0x8599, 0x44}, {0x859A, 0x48},
    };
    uint32_t va = 0x4F355A;
    for (const auto& k : kCombine) {
      Push(c, U32(U32(b + 0x94) + k.field));
      Push(c, k.pname);
      Push(c, 0x2300);
      CallPtr(c, k.f ? kglTexEnvf : kglTexEnvi, va);
      va += 0x1A;
    }
  }
  const uint32_t shader = U32(b + 0x98), on = 0x1534608 + 4 * unit;
  if (shader && (U32(shader) || U32(on))) {
    if (unit != U32(kUnit)) {
      Push(c, unit + 0x84C0);
      CallPtr(c, kglActiveTextureARB, 0x4F3717);
      Put(kUnit, unit);
    }
    Push(c, U32(shader));
    Push(c, 0x86DF);
    Push(c, 0x86DE);
    CallPtr(c, kglTexEnvi, 0x4F3736);
    // The shader operations' cases (0x86E7 on, from the original's
    // table): 0 the offset matrix, 2 the previous texture input and the
    // dot product's, 3 the const eye vector and those, 1 the previous
    // texture input only, 4 nothing.
    static constexpr uint8_t kCase[13] = {0, 4, 1, 1, 4, 2, 4, 2, 4, 2, 2, 2, 3};
    const uint32_t op = U32(shader) - 0x86E7;
    const uint8_t k = op <= 12 ? kCase[op] : 4;
    if (k == 0) {
      Push(c, shader + 0xC);
      Push(c, 0x86E0);
      Push(c, 0x86DE);
      CallPtr(c, kglTexEnvfv, 0x4F376B);
    } else if (k == 1) {
      Push(c, U32(shader + 4));
      Push(c, 0x86E4);
      Push(c, 0x86DE);
      CallPtr(c, kglTexEnvi, 0x4F37C0);
    } else if (k != 4) {
      if (k == 3) {
        Push(c, shader + 0xC);
        Push(c, 0x86E5);
        Push(c, 0x86DE);
        CallPtr(c, kglTexEnvfv, 0x4F378C);
      }
      Push(c, U32(shader + 4));
      Push(c, 0x86E4);
      Push(c, 0x86DE);
      CallPtr(c, kglTexEnvi, 0x4F37A6);
      Push(c, U32(shader + 8));
      Push(c, 0x86D9);
      Push(c, 0x86DE);
      CallPtr(c, kglTexEnvi, 0x4F37C0);
    }
    Put(on, U32(shader) != 0);
  }
  c.esp = e + 4;
}

// Turns texturing off on unit EAX: its target disabled (selecting the
// unit) and its NV texture shader off.
void f_004F3810(Ctx& c) {
  const uint32_t unit = c.eax, tgt = kTarget + 4 * unit;
  c.esp -= 4;
  if (U32(tgt)) {
    if (unit != U32(kUnit)) {
      Push(c, unit + 0x84C0);
      CallPtr(c, kglActiveTextureARB, 0x4F382D);
      Put(kUnit, unit);
    }
    Disable(c, U32(tgt), 0x4F3841);
    Put(tgt, 0);
    if (U32(0x1534608 + 4 * unit)) {
      Push(c, 0);
      Push(c, 0x86DF);
      Push(c, 0x86DE);
      CallPtr(c, kglTexEnvi, 0x4F3870);
      Put(0x1534608 + 4 * unit, 0);
    }
  }
  c.esp += 8;
}

// The texture coordinates of client unit ESI (stack: bundle, pointer,
// stride): selects the unit, enables its array and points it at them, of
// the bundle's size.
void f_004F38B0(Ctx& c) {
  const uint32_t e = c.esp, unit = c.esi;
  if (U32(kClientUnit) != unit) {
    Push(c, unit + 0x84C0);
    CallPtr(c, kglClientActiveTextureARB, 0x4F38C1);
    Put(kClientUnit, unit);
  }
  if (!((1u << (U32(kClientUnit) & 31)) & U32(kArrayBits))) {
    Push(c, 0x8078);
    CallPtr(c, kglEnableClientState, 0x4F38E4);
    Put(kArrayBits, U32(kArrayBits) | 1u << (U32(kClientUnit) & 31));
  }
  Push(c, Arg(c, e, 2));
  Push(c, Arg(c, e, 3));
  Push(c, 0x1406);
  Push(c, U32(Arg(c, e, 1) + 0x8C));
  CallPtr(c, kglTexCoordPointer, 0x4F3917);
  c.esp = e + 4;
}

// Turns client unit EDI's texture coordinate array off (selecting it).
void f_004F3920(Ctx& c) {
  const uint32_t unit = c.edi, bit = 1u << (unit & 31);
  c.esp -= 4;
  if (bit & U32(kArrayBits)) {
    if (U32(kClientUnit) != unit) {
      Push(c, unit + 0x84C0);
      CallPtr(c, kglClientActiveTextureARB, 0x4F3942);
      Put(kClientUnit, unit);
    }
    Push(c, 0x8078);
    CallPtr(c, kglDisableClientState, 0x4F3953);
    Put(kArrayBits, U32(kArrayBits) & ~bit);
  }
  c.esp += 8;
}

// A stage's textures on every unit (stack: stage, the texture coordinate
// pointers per unit, stride; the stage's slot is left holding the client
// unit): the current client unit's array and the
// current unit's texture first, then the other units', so that the units
// end selected as they were; then the stage's register combiners, vertex
// program and fragment shader (+0x648, +0x64C, +0x644).
void f_004F3D80(Ctx& c) {
  const uint32_t e = c.esp, stage = Arg(c, e, 1), coords = Arg(c, e, 2),
                 stride = Arg(c, e, 3);
  const uint32_t esi = c.esi, edi = c.edi;
  auto bundle = [&](uint32_t u) { return stage + 4 + 0xC8 * u; };
  auto coords_on = [&](uint32_t u, uint32_t ret) {
    Push(c, stride);
    Push(c, U32(coords + 4 * u));
    Push(c, bundle(u));
    c.esi = u;
    Call(c, f_004F38B0, ret);
    c.esp += 0xC;
  };
  auto bind = [&](uint32_t u, uint32_t bind_ret, uint32_t off_ret) {
    if (U32(bundle(u) + 0x90)) {
      c.eax = bundle(u);
      c.edi = u;
      Call(c, f_004F3410, bind_ret);
    } else {
      c.eax = u;
      Call(c, f_004F3810, off_ret);
    }
  };
  c.esp = e - 0x10;
  const uint32_t client = U32(kClientUnit);
  Put(e + 4, client);  // the stage's slot keeps the client unit
  if (U32(bundle(client) + 0x90)) {
    coords_on(client, 0x4F3DBD);
  } else {
    c.edi = client;
    Call(c, f_004F3920, 0x4F3DC9);
  }
  const uint32_t unit = U32(kUnit);
  c.edi = unit;
  bind(unit, 0x4F3DF1, 0x4F3DFA);
  if (I32(kNumUnits) > 0) {
    c.esp = e - 0x14;
    for (uint32_t u = 0; int32_t(u) < I32(kNumUnits); u++) {
      if (u != client) {
        if (U32(bundle(u) + 0x90)) {
          coords_on(u, 0x4F3E33);
        } else {
          const uint32_t bit = 1u << (u & 31);
          if (U32(kArrayBits) & bit) {
            if (U32(kClientUnit) != u) {
              Push(c, u + 0x84C0);
              CallPtr(c, kglClientActiveTextureARB, 0x4F3E59);
              Put(kClientUnit, u);
            }
            Push(c, 0x8078);
            CallPtr(c, kglDisableClientState, 0x4F3E6A);
            Put(kArrayBits, U32(kArrayBits) & ~bit);
          }
        }
      }
      if (u != unit) bind(u, 0x4F3E97, 0x4F3EA0);
    }
    c.esp = e - 0x10;
  }
  if (U32(stage + 0x648)) {
    Push(c, U32(stage + 0x648));
    Call(c, f_004F3A70, 0x4F3EC5);
    c.esp += 4;
  }
  if (U32(stage + 0x64C)) {
    c.esi = U32(stage + 0x64C);
    Call(c, f_004F3C70, 0x4F3ED7);
  }
  const uint32_t frag = U32(stage + 0x644);
  if (frag != U32(0x1534670)) {
    Push(c, frag);
    CallPtr(c, 0x1535014, 0x4F3EE6);
    Put(0x1534670, frag);
  }
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// f_004F38B0 from an ATI vertex array object (stack: bundle, the object,
// offset, stride): the coordinates through glArrayObjectATI.
void f_004F39B0(Ctx& c) {
  const uint32_t e = c.esp, unit = c.esi;
  if (U32(kClientUnit) != unit) {
    Push(c, unit + 0x84C0);
    CallPtr(c, kglClientActiveTextureARB, 0x4F39C1);
    Put(kClientUnit, unit);
  }
  if (!((1u << (U32(kClientUnit) & 31)) & U32(kArrayBits))) {
    Push(c, 0x8078);
    CallPtr(c, kglEnableClientState, 0x4F39E4);
    Put(kArrayBits, U32(kArrayBits) | 1u << (U32(kClientUnit) & 31));
  }
  Push(c, Arg(c, e, 3));
  Push(c, Arg(c, e, 2));
  Push(c, Arg(c, e, 4));
  Push(c, 0x1406);
  Push(c, U32(Arg(c, e, 1) + 0x8C));
  Push(c, 0x8078);
  CallPtr(c, kglArrayObjectATI, 0x4F3A21);
  c.esp = e + 4;
}

// f_004F3D80 from an ATI vertex array object (stack: stage, the object,
// the texture coordinate offsets per unit, stride): the coordinates through
// f_004F39B0; the stage's slot is left as it is.
void f_004F3F00(Ctx& c) {
  const uint32_t e = c.esp, stage = Arg(c, e, 1), object = Arg(c, e, 2),
                 coords = Arg(c, e, 3), stride = Arg(c, e, 4);
  const uint32_t esi = c.esi, edi = c.edi;
  auto bundle = [&](uint32_t u) { return stage + 4 + 0xC8 * u; };
  auto coords_on = [&](uint32_t u, uint32_t ret) {
    Push(c, stride);
    Push(c, U32(coords + 4 * u));
    Push(c, object);
    Push(c, bundle(u));
    c.esi = u;
    Call(c, f_004F39B0, ret);
    c.esp += 0x10;
  };
  auto bind = [&](uint32_t u, uint32_t bind_ret, uint32_t off_ret) {
    if (U32(bundle(u) + 0x90)) {
      c.eax = bundle(u);
      c.edi = u;
      Call(c, f_004F3410, bind_ret);
    } else {
      c.eax = u;
      Call(c, f_004F3810, off_ret);
    }
  };
  c.esp = e - 0x18;
  const uint32_t client = U32(kClientUnit);
  if (U32(bundle(client) + 0x90)) {
    coords_on(client, 0x4F3F45);
  } else {
    c.esi = client;
    c.edi = client;
    Call(c, f_004F3920, 0x4F3F51);
  }
  const uint32_t unit = U32(kUnit);
  c.edi = unit;
  bind(unit, 0x4F3F79, 0x4F3F82);
  for (uint32_t u = 0; int32_t(u) < I32(kNumUnits); u++) {
    if (u != client) {
      if (U32(bundle(u) + 0x90)) {
        coords_on(u, 0x4F3FB8);
      } else {
        const uint32_t bit = 1u << (u & 31);
        if (U32(kArrayBits) & bit) {
          if (U32(kClientUnit) != u) {
            Push(c, u + 0x84C0);
            CallPtr(c, kglClientActiveTextureARB, 0x4F3FDE);
            Put(kClientUnit, u);
          }
          Push(c, 0x8078);
          CallPtr(c, kglDisableClientState, 0x4F3FEF);
          Put(kArrayBits, U32(kArrayBits) & ~bit);
        }
      }
    }
    if (u != unit) bind(u, 0x4F401C, 0x4F4025);
  }
  if (U32(stage + 0x648)) {
    Push(c, U32(stage + 0x648));
    Call(c, f_004F3A70, 0x4F404D);
    c.esp += 4;
  }
  if (U32(stage + 0x64C)) {
    c.esi = U32(stage + 0x64C);
    Call(c, f_004F3C70, 0x4F405F);
  }
  const uint32_t frag = U32(stage + 0x644);
  if (frag != U32(0x1534670)) {
    Push(c, frag);
    CallPtr(c, 0x1535014, 0x4F406E);
    Put(0x1534670, frag);
  }
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// ---- fog per flush ----------------------------------------------------------

// Fog off: disables it if the state bits have it on.
void f_004C89A0(Ctx& c) {
  if (U32(kStateBits) & 0x200000) {
    Disable(c, 0xB60, 0x4C89B1);
    Put(kStateBits, U32(kStateBits) & ~0x200000u);
  }
  c.esp += 4;
}

// A fog's parameters (ESI: mode +0, hint +4, colour +0x10, start +0x20, end
// +0x24, density +0x2C, on +0x30), for the fog cvar: defaults for a zero
// density (1), hint (GL_DONT_CARE) and mode (GL_LINEAR), fog on, the mode,
// the colour (into the fog colour the blend follows), the density, the
// hint, the start and the end (the far clip cvar's when nonzero), each
// sent only when it differs from the one last sent.
void f_004C8830(Ctx& c) {
  const uint32_t e = c.esp, fog = c.esi;
  c.esp = e - 4;
  if (!CvarInt(0x15347F4) || !U32(fog + 0x30)) {
    if (U32(kStateBits) & 0x200000) {
      Disable(c, 0xB60, 0x4C8852);
      Put(kStateBits, U32(kStateBits) & ~0x200000u);
    }
    c.esp = e + 4;
    return;
  }
  if (St(Ld(fog + 0x2C)) == 0.0f) Put(fog + 0x2C, 0x3F800000);
  if (!U32(fog + 4)) Put(fog + 4, 0x1100);
  if (!U32(fog)) Put(fog, 0x2601);
  Call(c, f_004C89D0, 0x4C88A0);
  if (U32(fog) != U32(0x1534638)) {
    Push(c, U32(fog));
    Push(c, 0xB65);
    CallPtr(c, kglFogi, 0x4C88B0);
    Put(0x1534638, U32(fog));
  }
  for (uint32_t i = 0; i < 0x10; i += 4) Put(0x1534640 + i, U32(fog + 0x10 + i));
  Call(c, f_004C8A20, 0x4C88E6);
  if (!(St(Ld(fog + 0x2C)) == Ld(0x1534658))) {
    Push(c, U32(fog + 0x2C));
    Push(c, 0xB62);
    CallPtr(c, kglFogf, 0x4C88FF);
    Put(0x1534658, U32(fog + 0x2C));
  }
  if (U32(0x153463C) != U32(fog + 4)) {
    Push(c, U32(fog + 4));
    Push(c, 0xC54);
    CallPtr(c, kglHint, 0x4C891F);
    Put(0x153463C, U32(fog + 4));
  }
  if (!(St(Ld(0x1534650)) == Ld(fog + 0x20))) {
    Push(c, U32(fog + 0x20));
    Push(c, 0xB63);
    CallPtr(c, kglFogf, 0x4C8947);
    Put(0x1534650, U32(fog + 0x20));
  }
  const uint32_t zfar = U32(0x15343F0) + 0x1C;
  const St end(Ld(!(St(Ld(zfar)) == 0.0f) ? zfar : fog + 0x24));
  Put(e - 4, Fst(end));
  if (!(end == Ld(0x1534654))) {
    Push(c, U32(e - 4));
    Push(c, 0xB64);
    CallPtr(c, kglFogf, 0x4C898F);
    Put(0x1534654, U32(e - 4));
  }
  c.esp = e + 4;
}

// The fog of a flush, by the refdef's flags (0x15496EC): none (1), the
// sky's fog 0x1535820 (0x40; none without it), the view's 0x1535860 (8,
// when 0x154D734 is set) or the world's 0x1535920 when it has one; fog off
// otherwise.
void f_004F5D50(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, flags = U8(0x15496EC);
  c.esp = e - 4;
  auto off = [&] {
    if (U32(kStateBits) & 0x200000) {
      Disable(c, 0xB60, 0x4F5D6B);
      Put(kStateBits, U32(kStateBits) & ~0x200000u);
    }
  };
  auto params = [&](uint32_t fog, uint32_t ret) {
    c.esi = fog;
    Call(c, f_004C8830, ret);
    c.esi = esi;
  };
  if (flags & 1) {
    off();
  } else if (flags & 0x40) {
    if (U32(0x1535850)) params(0x1535820, 0x4F5D94);
    else off();
  } else if (U32(0x154D734) && (flags & 8)) {
    if (!U32(0x1535890)) {
      c.esp = e;
      return f_004C89A0(c);
    }
    params(0x1535860, 0x4F5DB7);
  } else if (I32(0x13F9CF8) > 0) {
    params(0x1535920, 0x4F5DCC);
  } else {
    c.esp = e;
    return f_004C89A0(c);
  }
  c.esp = e + 4;
}

// ---- a flush's setup ----------------------------------------------------------

// Back to one texture unit: units 1 on have their coordinate arrays and
// textures off; unit 0 is selected (and its client unit) with GL_MODULATE.
void f_004F4090(Ctx& c) {
  const uint32_t e = c.esp;
  c.esp = e - 4;
  if (I32(kNumUnits) > 1) {
    c.esp = e - 8;
    for (uint32_t u = 1; int32_t(u) < I32(kNumUnits); u++) {
      const uint32_t bit = 1u << (u & 31);
      if (U32(kArrayBits) & bit) {
        if (U32(kClientUnit) != u) {
          Push(c, u + 0x84C0);
          CallPtr(c, kglClientActiveTextureARB, 0x4F40C1);
          Put(kClientUnit, u);
        }
        Push(c, 0x8078);
        CallPtr(c, kglDisableClientState, 0x4F40D2);
        Put(kArrayBits, U32(kArrayBits) & ~bit);
      }
      c.eax = u;
      Call(c, f_004F3810, 0x4F40ED);
    }
  }
  c.esp = e;
  if (U32(kUnit)) {
    Push(c, 0x84C0);
    CallPtr(c, kglActiveTextureARB, 0x4F4107);
    Put(kUnit, 0);
  }
  if (U32(kClientUnit)) {
    Push(c, 0x84C0);
    CallPtr(c, kglClientActiveTextureARB, 0x4F4123);
    Put(kClientUnit, 0);
  }
  const uint32_t slot = kTexEnv + 4 * U32(kUnit);
  if (U32(slot) != 0x2100) {
    Push(c, 0x2100);
    Push(c, 0x2200);
    Push(c, 0x2300);
    Put(slot, 0x2100);
    CallPtr(c, kglTexEnvi, 0x4F415A);
  }
  c.esp = e + 4;
}

// ---- the vertex deforms ----------------------------------------------------------

namespace {

// tess.shaderTime.
constexpr uint32_t kShaderTime = 0x17F0C78;
// tess.texCoords (8 bytes a vertex) and the frame's first-vertex flags of
// the vertices' axes (0x4F30A0), which a change of normals clears.
constexpr uint32_t kTessTexCoords = 0x1650C40, kAxesSet = 0x17F0C68, kAxesSet2 = 0x17F0C6C;
// The entity drawn (tr.worldEntity 0x1535D6C for the world's surfaces),
// the axes it is drawn with (three rows of three) and the view's axes.
constexpr uint32_t kEntity = 0x1549B3C, kWorldEntity = 0x1535D6C, kEntityAxes = 0x1549A94,
                   kViewAxes = 0x1549834;

// ST0 as a double, popped (a lifted callee's return value).
St PopSt0(Ctx& c) {
  St v(std::bit_cast<double>(x87::StoreF64(c.fpu)));
  x87::Pop(c.fpu);
  return v;
}

// A waveform's table (0x508940, ECX the type; ri.Error and 0 for another
// than 1-5), called (ret: its call's return).
uint32_t CallWaveTable(Ctx& c, uint32_t type, uint32_t ret) {
  c.ecx = type;
  Call(c, f_00508940, ret);
  return c.eax;
}

// A waveform (type, base, amplitude, phase, frequency) at the shader time,
// from its table of 1024 (the index through the scratch 0x13F96D8).
St WaveAt(uint32_t wave, uint32_t table) {
  const int32_t i =
      Fistp(St(Fst((St(Ld(wave + 0x10)) * Ld(kShaderTime) + Ld(wave + 0xC)) * 1024.0f)));
  Put(0x13F96D8, uint32_t(i));
  return St(Ld(wave + 8)) * Ld(table + 4 * (uint32_t(i) & 0x3FF)) + Ld(wave + 4);
}

// The waveform spread along a vertex: its phase plus the sum of the
// position's coordinates (in the order sum) times the spread, at the shader
// time; the index through the scratch.
uint32_t SpreadIndex(uint32_t ds, St sum) {
  const int32_t i = Fistp(St(Fst(
      (sum * Ld(ds + 0x24) + Ld(ds + 0x1C) + St(Ld(kShaderTime)) * Ld(ds + 0x20)) * 1024.0f)));
  Put(0x13F96D8, uint32_t(i));
  return uint32_t(i);
}

// The next position (kXyzStride dwords on).
uint32_t NextXyz(uint32_t p) { return p + 4 * U32(kXyzStride); }

// Each position (p) moved along its normal by v(vertex, p): x by the
// unrounded product, y and z through their stores where the original
// stores them.
template <bool kStoreY, bool kStoreZ, typename Scale>
void AlongNormals(Scale v) {
  uint32_t p = kTessXyz, n = kTessNormals;
  for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++, p = NextXyz(p), n += 0xC) {
    const St s = v(i, p);
    const St x = s * Ld(n);
    const St y = kStoreY ? St(Fst(s * Ld(n + 4))) : s * Ld(n + 4);
    const St z = kStoreZ ? St(Fst(s * Ld(n + 8))) : s * Ld(n + 8);
    Put(p, Fst(x + Ld(p)));
    Put(p + 4, Fst(y + Ld(p + 4)));
    Put(p + 8, Fst(z + Ld(p + 8)));
  }
}

// The vector at va in the entity's space (0x509580 inlined): its dot
// products with the entity's axes into out.
void ToEntity(uint32_t va, uint32_t out) {
  for (uint32_t k = 0; k < 12; k += 4) {
    const uint32_t row = kEntityAxes + 3 * k;
    Put(out + k, Fst(St(Ld(row)) * Ld(va) + St(Ld(row + 8)) * Ld(va + 8) +
                     St(Ld(row + 4)) * Ld(va + 4)));
  }
}

// ri.Printf(PRINT_WARNING, format, the shader) from the call at va.
void Warn(Ctx& c, uint32_t format, uint32_t va) {
  Push(c, U32(kTessShader));
  Push(c, format);
  Push(c, 2);
  CallPtr(c, kPrintf, va);
  c.esp += 0xC;
}

// The CRT's square root (0x52E610) of a float (ret: its call's return).
St CrtSqrt(Ctx& c, F32 v, uint32_t ret) {
  x87::LoadF32(c.fpu, v.bits);
  Call(c, f_0052E610, ret);
  return PopSt0(c);
}

}  // namespace

// The deform's waveform (+0x10) along the normals (EDI the deform): with
// no frequency (+0x20) the one value for every vertex, else spread along
// each by +0x24.
void f_00508AE0(Ctx& c) {
  const uint32_t e = c.esp, ds = c.edi, wave = ds + 0x10;
  if (St(Ld(ds + 0x20)) == 0.0f) {
    c.esp = e - 0x14;
    const F32 s = Fst(WaveAt(wave, CallWaveTable(c, U32(wave), 0x508B05)));
    AlongNormals<false, true>([&](uint32_t, uint32_t) { return St(s); });
  } else {
    c.esp = e - 0x18;
    const uint32_t table = CallWaveTable(c, U32(wave), 0x508BA1);
    AlongNormals<true, true>([&](uint32_t, uint32_t p) {
      const uint32_t i = SpreadIndex(ds, St(Ld(p + 8)) + Ld(p + 4) + Ld(p));
      return St(Ld(table + 4 * (i & 0x3FF))) * Ld(ds + 0x18) + Ld(ds + 0x14);
    });
  }
  c.esp = e + 4;
}

// The same scaled by each vertex's texture coordinate s (EAX 0) or t
// (EAX 1).
void f_00508C50(Ctx& c) {
  const uint32_t e = c.esp, ds = c.edi, wave = ds + 0x10, coord = kTessTexCoords + 4 * c.eax;
  if (St(Ld(ds + 0x20)) == 0.0f) {
    c.esp = e - 0x18;
    const F32 s = Fst(WaveAt(wave, CallWaveTable(c, U32(wave), 0x508C7F)));
    AlongNormals<true, true>([&](uint32_t i, uint32_t) { return St(s) * Ld(coord + 8 * i); });
  } else {
    c.esp = e - 0x1C;
    const uint32_t table = CallWaveTable(c, U32(wave), 0x508D29);
    AlongNormals<true, true>([&](uint32_t v, uint32_t p) {
      const uint32_t i = SpreadIndex(ds, St(Ld(p)) + Ld(p + 8) + Ld(p + 4));
      return (St(Ld(table + 4 * (i & 0x3FF))) * Ld(ds + 0x18) + Ld(ds + 0x14)) *
             Ld(coord + 8 * v);
    });
  }
  c.esp = e + 4;
}

// The normals bent by the waveform's slope (EBX the deform): each by
// minus the table's step at its spread index times the amplitude, 1024
// and the spread, along (1 - n.sum(n) * n), then normalised by 0x4268B0;
// the vertices' axes then rebuilt.
void f_00508DE0(Ctx& c) {
  const uint32_t e = c.esp, ds = c.ebx;
  c.esp = e - 0x1C;
  const uint32_t table = CallWaveTable(c, U32(ds + 0x10), 0x508DF2);
  uint32_t p = kTessXyz, n = kTessNormals;
  for (uint32_t v = 0; int32_t(v) < I32(kTessVerts); v++, p = NextXyz(p), n += 0xC) {
    const St sum = St(Ld(n + 4)) + Ld(n + 8) + Ld(n);
    F32 a[3];
    for (uint32_t k = 0; k < 3; k++) a[k] = Fst(1.0f - sum * Ld(n + 4 * k));
    const uint32_t i = SpreadIndex(ds, St(Ld(p + 8)) + Ld(p + 4) + Ld(p));
    const St d = -((St(Ld(table + 4 * ((i + 1) & 0x3FF))) - Ld(table + 4 * (i & 0x3FF))) *
                   Ld(ds + 0x18) * 1024.0f * Ld(ds + 0x24));
    Put(n, Fst(St(a[0]) * d + Ld(n)));
    Put(n + 4, Fst(St(a[1]) * d + Ld(n + 4)));
    Put(n + 8, Fst(d * a[2] + Ld(n + 8)));
    c.eax = n;
    c.esp = e - 0x20;
    Call(c, f_004268B0, 0x508EBE);
  }
  Put(kAxesSet, 0u);
  Put(kAxesSet2, 0u);
  c.esp = e + 4;
}

// The normals moved by noise (EBX the deform): each coordinate by the
// noise 0x4D3AA0 at the position x 0.98 (x offset 0, 100 and 200 for the
// three) and the frequency (+0x20) x the shader time, times the amplitude
// (+0x18), then normalised; the vertices' axes then rebuilt.
void f_00508F00(Ctx& c) {
  const uint32_t e = c.esp, ds = c.ebx;
  static constexpr uint32_t kRet[3] = {0x508F5C, 0x508FA9, 0x508FF3};
  uint32_t p = kTessXyz, n = kTessNormals;
  for (uint32_t v = 0; int32_t(v) < I32(kTessVerts); v++, p = NextXyz(p), n += 0xC) {
    for (uint32_t k = 0; k < 3; k++) {
      c.esp = e - 0x14;
      Push(c, Fst(St(Ld(kShaderTime)) * Ld(ds + 0x20)).bits);
      Push(c, Fst(St(Ld(p + 8)) * 0.98f).bits);
      Push(c, Fst(St(Ld(p + 4)) * 0.98f).bits);
      const St x = St(Ld(p)) * 0.98f;
      Push(c, Fst(k == 0 ? x : x + (k == 1 ? 100.0f : 200.0f)).bits);
      Call(c, f_004D3AA0, kRet[k]);
      c.esp += 0x10;
      Put(n + 4 * k, Fst(PopSt0(c) * Ld(ds + 0x18) + Ld(n + 4 * k)));
    }
    c.eax = n;
    Call(c, f_004268B0, 0x509004);
  }
  Put(kAxesSet, 0u);
  Put(kAxesSet2, 0u);
  c.esp = e + 4;
}

// The bulge (stack: the deform): along the normals by the sine table at
// (the width +0x28 x s + now) x 1024 / 2pi through _ftol, times the height
// +0x2C; now the refdef's time (ms, 0x15496E8) x the speed +0x30 / 1000.
void f_00509040(Ctx& c) {
  const uint32_t e = c.esp, ds = Arg(c, e, 1);
  const F32 now = Fst(FromInt(I32(0x15496E8)) * Ld(ds + 0x30) * 0.001f);
  AlongNormals<false, false>([&](uint32_t v, uint32_t) {
    const int32_t i =
        Ftol((St(Ld(ds + 0x28)) * Ld(kTessTexCoords + 8 * v) + now) * 0x1.45f306p+7f);
    return St(Ld(0x1544530 + 4 * (uint32_t(i) & 0x3FF))) * Ld(ds + 0x2C);
  });
  c.esp = e + 4;
}

// The move (ESI the deform): every position by the waveform times the
// vector +4.
void f_005090E0(Ctx& c) {
  const uint32_t e = c.esp, ds = c.esi;
  c.esp = e - 0x10;
  const St v = WaveAt(ds + 0x10, CallWaveTable(c, U32(ds + 0x10), 0x5090EB));
  F32 m[3];
  for (uint32_t k = 0; k < 3; k++) m[k] = Fst(v * Ld(ds + 4 + 4 * k));
  for (uint32_t i = 0, p = kTessXyz; int32_t(i) < I32(kTessVerts); i++, p = NextXyz(p))
    for (uint32_t k = 0; k < 3; k++) Put(p + 4 * k, Fst(St(m[k]) + Ld(p + 4 * k)));
  c.esp = e + 4;
}

// The projection shadow: each position onto the ground plane (the
// entity's z axis column 0x1549A9C/AA8/AB4, at its origin's z 0x1549A90
// less the entity's shadow plane +0x18) along the entity's light direction
// (+0xA4; tilted by 0.5 less its dot when that is below 0.5).
void f_00508800(Ctx& c) {
  const uint32_t e = c.esp, ent = U32(kEntity);
  const F32 dist = Fst(St(Ld(0x1549A90)) - Ld(ent + 0x18));
  const F32 g[3] = {Ld(0x1549A9C), Ld(0x1549AA8), Ld(0x1549AB4)};
  St lx = St(Ld(ent + 0xA4)), ly = St(Ld(ent + 0xA8));
  F32 lz = Ld(ent + 0xAC);
  St d = St(lz) * g[2] + ly * g[1] + lx * g[0];
  if (d < 0.5f) {
    const St t = 0.5f - d;
    lx = St(g[0]) * t + lx;
    ly = St(g[1]) * t + ly;
    const St z = t * g[2] + lz;
    lz = Fst(z);
    d = z * g[2] + ly * g[1] + lx * g[0];
  }
  const F32 inv = Fst(1.0f / d);
  const F32 kx = Fst(St(inv) * lx), ky = Fst(St(inv) * ly), kz = Fst(St(inv) * lz);
  for (uint32_t i = 0, p = kTessXyz; int32_t(i) < I32(kTessVerts); i++, p = NextXyz(p)) {
    const St h = St(g[2]) * Ld(p + 8) + St(g[1]) * Ld(p + 4) + St(g[0]) * Ld(p) + dist;
    Put(p, Fst(Ld(p) - St(kx) * h));
    Put(p + 4, Fst(Ld(p + 4) - St(ky) * h));
    Put(p + 8, Fst(Ld(p + 8) - h * kz));
  }
  c.esp = e + 4;
}

// Autosprite (its warnings' name): each quad added again by 0x500F20
// about its centre, facing the view (its axes in the entity's space, but
// for the world), its radius the centre's distance to the first vertex x
// 0.707 (left negated in a mirror; both over the entity's x axis length
// when +0x40 is set), with the quad's first colour; the counts restarted
// (ri.Printf warnings for counts not of quads).
void f_005095F0(Ctx& c) {
  const uint32_t e = c.esp, b = e - 0x4C;
  c.esp = b;
  if (U32(kTessVerts) & 3) Warn(c, 0x5390DC, 0x50960D);
  if (U32(kTessIndexCount) != uint32_t(I32(kTessVerts) >> 2) * 6) Warn(c, 0x5390B0, 0x50963A);
  uint32_t ent = U32(kEntity);
  const int32_t old = I32(kTessVerts);
  Put(kTessVerts, 0u);
  Put(kTessIndexCount, 0u);
  // In the original's frame, as 0x500F20 reads them: the centre, the left
  // and up vectors scaled, and the view's left and up.
  const uint32_t mid = b + 0x40, left = b + 0x10, up = b + 0x1C, axes = b + 0x28;
  if (ent != kWorldEntity) {
    ToEntity(kViewAxes + 0xC, axes);
    ToEntity(kViewAxes + 0x18, axes + 0xC);
  } else {
    for (uint32_t k = 0; k < 0x18; k += 4) Put(axes + k, U32(kViewAxes + 0xC + k));
  }
  if (old > 0) {
    uint32_t quads = ((uint32_t(old) - 1) >> 2) + 1, p = kTessXyz, colour = kVertColors;
    uint32_t s = 4 * U32(kXyzStride);
    for (;;) {
      Put(mid, Fst((St(Ld(p + 2 * s)) + Ld(p + s) + Ld(p) + Ld(p + 3 * s)) * 0.25f));
      for (uint32_t k = 4; k < 12; k += 4)
        Put(mid + k,
            Fst((St(Ld(p + s + k)) + Ld(p + 2 * s + k) + Ld(p + 3 * s + k) + Ld(p + k)) * 0.25f));
      const St dx = St(Ld(p)) - Ld(mid), dy = St(Ld(p + 4)) - Ld(mid + 4),
               dz = St(Ld(p + 8)) - Ld(mid + 8);
      const St r = Sqrt(dz * dz + dy * dy + dx * dx) * 0.707f;
      for (uint32_t k = 0; k < 0x14; k += 4) Put(left + k, Fst(St(Ld(axes + k)) * r));
      Put(left + 0x14, Fst(r * Ld(axes + 0x14)));
      if (U32(0x1549930))
        for (uint32_t k = 0; k < 12; k += 4)
          Put(left + k, Fst(St(Ld(0x537E54 + k)) - Ld(left + k)));
      if (!(St(Ld(ent + 0x40)) == 0.0f)) {
        const St ax = St(Ld(ent + 0x1C)), ay = St(Ld(ent + 0x20)), az = St(Ld(ent + 0x24));
        const St length = Sqrt(ax * ax + ay * ay + az * az);
        const St scale = length == 0.0f ? St(0.0f) : 1.0f / length;
        for (uint32_t k = 0; k < 0x14; k += 4) Put(left + k, Fst(St(Ld(left + k)) * scale));
        Put(left + 0x14, Fst(scale * Ld(left + 0x14)));
      }
      for (uint32_t v : {0x3F800000u, 0x3F800000u, 0u, 0u, colour, up, left, mid}) Push(c, v);
      Call(c, f_00500F20, 0x509876);
      c.esp += 0x20;
      s = 4 * U32(kXyzStride);
      colour += 0x10;
      p += 4 * s;
      if (--quads == 0) break;
      ent = U32(kEntity);
    }
  }
  c.esp = e + 4;
}

// Autosprite2 (its warnings' name): quads turned about their long axis to
// face the view (its forward axis in the entity's space, but for the
// world): of each quad's 6 indexes, the two shortest of its triangles' six
// edges (the first shorter; an edge never shorter than FLT_MAX leaves the
// last quad's, or at first the original's frame's, which the native
// deforms do not keep), their midpoints as the axis, and each edge's ends
// put half its length either side of its midpoint across it (the axis x
// forward, normalised by 0x426710; lengths by the CRT's square root).
void f_005098A0(Ctx& c) {
  const uint32_t e = c.esp, b = e - 0x6C, esi = c.esi;
  c.esp = b;
  if (U32(kTessVerts) & 3) Warn(c, 0x539084, 0x5098B9);
  if (U32(kTessIndexCount) != uint32_t(I32(kTessVerts) >> 2) * 6) Warn(c, 0x539058, 0x5098E6);
  F32 fwd[3];
  if (U32(kEntity) != kWorldEntity) {
    ToEntity(kViewAxes, b + 0x2C);
    for (uint32_t k = 0; k < 3; k++) fwd[k] = Ld(b + 0x2C + 4 * k);
  } else {
    for (uint32_t k = 0; k < 3; k++) fwd[k] = Ld(kViewAxes + 4 * k);
  }
  if (I32(kTessIndexCount) <= 0) {
    c.esp = e + 4;
    return;
  }
  // The shortest edge's ends (a0, b0) and the second's (a1, b1).
  uint32_t a0 = U32(b + 0x44), b0 = U32(b + 0x48), a1 = U32(b + 0x4C), b1 = U32(b + 0x50);
  const uint32_t left = b + 0x20;
  for (uint32_t i = 0; int32_t(i) < I32(kTessIndexCount); i += 6) {
    uint32_t v[6];
    for (uint32_t k = 0; k < 6; k++)
      v[k] = kTessXyz + 4 * U16(kTessIndexes + 2 * (i + k)) * U32(kXyzStride);
    const uint32_t edges[6][2] = {{v[0], v[1]}, {v[1], v[2]}, {v[2], v[0]},
                                  {v[3], v[4]}, {v[4], v[5]}, {v[5], v[3]}};
    F32 s0{0x7F7FFFFF}, s1{0x7F7FFFFF};
    for (const auto& [from, to] : edges) {
      F32 d[3];
      for (uint32_t k = 0; k < 3; k++) d[k] = Fst(St(Ld(to + 4 * k)) - Ld(from + 4 * k));
      const F32 length = Fst(St(d[2]) * d[2] + St(d[1]) * d[1] + St(d[0]) * d[0]);
      if (St(length) < s0) {
        s1 = s0, s0 = length;
        a1 = a0, b1 = b0, a0 = from, b0 = to;
      } else if (St(length) < s1) {
        s1 = length;
        a1 = from, b1 = to;
      }
    }
    F32 m0[3], m1[3], axis[3];
    m0[0] = Fst((St(Ld(b0)) + Ld(a0)) * 0.5f);
    for (uint32_t k = 1; k < 3; k++) m0[k] = Fst((St(Ld(a0 + 4 * k)) + Ld(b0 + 4 * k)) * 0.5f);
    for (uint32_t k = 0; k < 3; k++) m1[k] = Fst((St(Ld(b1 + 4 * k)) + Ld(a1 + 4 * k)) * 0.5f);
    for (uint32_t k = 0; k < 3; k++) axis[k] = Fst(St(m1[k]) - m0[k]);
    Put(left, Fst(St(axis[1]) * fwd[2] - St(axis[2]) * fwd[1]));
    Put(left + 4, Fst(St(axis[2]) * fwd[0] - St(axis[0]) * fwd[2]));
    Put(left + 8, Fst(St(axis[0]) * fwd[1] - St(axis[1]) * fwd[0]));
    c.esp = b - 0x10;
    c.esi = left;
    Call(c, f_00426710, 0x509E00);
    PopSt0(c);
    const F32 l0 = Fst(CrtSqrt(c, s0, 0x509E0B) * St(0.5)), n0 = Fst(-St(l0));
    for (uint32_t k = 0; k < 12; k += 4) Put(a0 + k, Fst(St(n0) * Ld(left + k) + m0[k / 4]));
    for (uint32_t k = 0; k < 12; k += 4) Put(b0 + k, Fst(St(Ld(left + k)) * l0 + m0[k / 4]));
    const F32 l1 = Fst(CrtSqrt(c, s1, 0x509E80) * St(0.5)), n1 = Fst(-St(l1));
    for (uint32_t k = 0; k < 12; k += 4) Put(a1 + k, Fst(St(Ld(left + k)) * l1 + m1[k / 4]));
    for (uint32_t k = 0; k < 12; k += 4) Put(b1 + k, Fst(St(n1) * Ld(left + k) + m1[k / 4]));
  }
  c.esi = esi;
  c.esp = e + 4;
}

// A text (EBX its string): a quad per character but spaces, from the
// first vertex's normal and its four positions' extent (height 0.75 of it
// across), added by 0x500F20 in the font's 16 x 16 grid, white; the counts
// restarted.
void f_00509190(Ctx& c) {
  const uint32_t e = c.esp, b = e - 0x3C, text = c.ebx;
  const F32 h = Fst(St(Ld(kTessNormals + 8)) * 0.0f);
  F32 width[3] = {Fst(St(Ld(kTessNormals + 4)) * -1.0f - h),
                  Fst(h - St(Ld(kTessNormals)) * -1.0f),
                  Fst(St(Ld(kTessNormals)) * 0.0f - St(Ld(kTessNormals + 4)) * 0.0f)};
  F32 sum[3] = {Ld(kTessXyz), Ld(kTessXyz + 4), Ld(kTessXyz + 8)};
  F32 top{0x497423F0}, bottom{0xC97423F0};
  if (St(Ld(kTessXyz + 8)) < 999999.0f) top = Ld(kTessXyz + 8);
  if (St(Ld(kTessXyz + 8)) > -999999.0f) bottom = Ld(kTessXyz + 8);
  const uint32_t s = 4 * U32(kXyzStride);
  for (uint32_t k = 1, p = kTessXyz + s; k < 4; k++, p += s) {
    for (uint32_t j = 0; j < 3; j++) sum[j] = Fst(St(sum[j]) + Ld(p + 4 * j));
    if (St(Ld(p + 8)) < top) top = Ld(p + 8);
    if (St(Ld(p + 8)) > bottom) bottom = Ld(p + 8);
  }
  // In the original's frame, as 0x500F20 reads them: the origin, the width
  // and height vectors and the colour.
  const uint32_t origin = b + 0x30, across = b + 0x24, height = b + 0x18, colour = b + 4;
  for (uint32_t j = 0; j < 3; j++) Put(origin + 4 * j, Fst(St(sum[j]) * 0.25f));
  const F32 tall = Fst((St(bottom) - top) * 0.5f), half = Fst(St(tall) * -0.75f);
  for (F32& w : width) w = Fst(St(half) * w);
  Put(height, 0u);
  Put(height + 4, 0u);
  Put(height + 8, tall);
  uint32_t len = 0;
  while (U8(text + len)) len++;
  const int32_t n = int32_t(len);
  const F32 first = Fst(FromInt(n - 1));
  Put(kTessIndexCount, 0u);
  Put(kTessVerts, 0u);
  for (uint32_t j = 0; j < 3; j++)
    Put(origin + 4 * j, Fst(St(first) * width[j] + Ld(origin + 4 * j)));
  Put(colour, 0xFFFFFFFFu);
  for (uint32_t j = 0; j < 3; j++) Put(across + 4 * j, width[j]);
  if (n > 0) {
    F32 step[3];
    for (uint32_t j = 0; j < 3; j++) step[j] = Fst(St(width[j]) + St(width[j]));
    for (int32_t i = 0; i < n; i++) {
      const uint32_t ch = U8(text + uint32_t(i));
      if (ch != 0x20) {
        const F32 row = Fst(St(Fst(FromInt(int32_t(ch >> 4)))) * 0.0625f);
        const F32 col = Fst(St(Fst(FromInt(int32_t(ch & 0xF)))) * 0.0625f);
        c.esp = b - 8;
        for (uint32_t v : {Fst(St(row) + 0.0625f).bits, Fst(St(col) + 0.0625f).bits, row.bits,
                           col.bits, colour, height, across, origin})
          Push(c, v);
        Call(c, f_00500F20, 0x5094C8);
      }
      for (uint32_t j = 0; j < 3; j++) Put(origin + 4 * j, Fst(St(Ld(origin + 4 * j)) - step[j]));
    }
  }
  c.esp = e + 4;
}

// The shader's vertex deforms (tess.shader 0x17F0C70: count +0xAC, 52-byte
// deforms from +0xB0, type first), each by its function with the
// original's registers: EDI the deform, EBP its offset, EAX its type (0 or
// 1 for the two kinds of 0x508C50), ECX the type - 1, EBX the deform for
// types 4 and 5 and the text's buffer (32 bytes from 0x15496F4) for the
// texts 11-18, ESI the deform for type 7; EBX, ESI and EDX keep what the
// previous call left as they do in the original.
void f_00509F20(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  c.esp = e - 0x14;
  for (uint32_t i = 0, off = 0; int32_t(i) < I32(U32(kTessShader) + 0xAC); i++, off += 0x34) {
    const uint32_t ds = U32(kTessShader) + 0xB0 + off, type = U32(ds);
    Put(e - 4, i);
    c.edi = ds;
    c.ebp = off;
    c.eax = type;
    c.ecx = type - 1;
    switch (type) {
      case 1: Call(c, f_00508AE0, 0x509F6F); break;
      case 2: c.eax = 0; Call(c, f_00508C50, 0x509F78); break;
      case 3: c.eax = 1; Call(c, f_00508C50, 0x509F84); break;
      case 4: c.ebx = ds; Call(c, f_00508F00, 0x509F5F); break;
      case 5: c.ebx = ds; Call(c, f_00508DE0, 0x509F68); break;
      case 6:
        Push(c, ds);
        Call(c, f_00509040, 0x509F8C);
        c.esp += 4;
        break;
      case 7: c.esi = ds; Call(c, f_005090E0, 0x509F98); break;
      case 8: Call(c, f_00508800, 0x509F9F); break;
      case 9: Call(c, f_005095F0, 0x509FA6); break;
      case 10: Call(c, f_005098A0, 0x509FAD); break;
      case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18:
        c.ebx = ((type - 11) << 5) + 0x15496F4;
        Call(c, f_00509190, 0x509FC0);
        break;
    }
    c.edx = U32(U32(kTessShader) + 0xAC);
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// ---- the texture coordinate generators and modifiers -------------------------

namespace {

// The view's origin in the entity's space, the light drawn (its origin
// +0x44, w +0x50) and the entity's origin; the vertices' axes (two per
// vertex, 12 bytes apart, beside the normals) and the sine table.
constexpr uint32_t kViewOrigin = 0x1549AB8, kLight = 0x1549B40, kEntityOrigin = 0x1549A88,
                   kBinormals = 0x16B0C40, kTangents = 0x16C8C40, kSineTable = 0x1544530;

// The vertices' axes, built (0x50ABF0) unless both flags are set (ret: the
// call's return).
void Axes(Ctx& c, uint32_t ret) {
  if (U32(kAxesSet) == 0 || U32(kAxesSet2) == 0) Call(c, f_0050ABF0, ret);
}

// The direction from each position to the view (into the local at va),
// normalised by 0x4268B0 (ret: its call's return).
void ToView(Ctx& c, uint32_t p, uint32_t va, uint32_t ret) {
  for (uint32_t k = 0; k < 12; k += 4) Put(va + k, Fst(St(Ld(kViewOrigin + k)) - Ld(p + k)));
  c.eax = va;
  Call(c, f_004268B0, ret);
}

// The light's origin (or for a w of 0 its direction) less the entity's,
// in the entity's space.
void LightVector(F32 out[3]) {
  const uint32_t l = U32(kLight);
  St v[3];
  for (uint32_t k = 0; k < 3; k++)
    v[k] = St(Ld(l + 0x50)) == 0.0f ? St(Ld(l + 0x44 + 4 * k))
                                    : St(Ld(l + 0x44 + 4 * k)) - Ld(kEntityOrigin + 4 * k);
  for (uint32_t k = 0; k < 3; k++) {
    const uint32_t row = kEntityAxes + 12 * k;
    out[k] = Fst(St(Ld(row + 8)) * v[2] + St(Ld(row + 4)) * v[1] + St(Ld(row)) * v[0]);
  }
}

// The CRT's floor (0x51AB30) of a float, passed as a double at va (ret:
// its call's return).
St CrtFloor(Ctx& c, F32 v, uint32_t va, uint32_t ret) {
  const uint64_t d = std::bit_cast<uint64_t>(St(v).v);
  Put(va, uint32_t(d));
  Put(va + 4, uint32_t(d >> 32));
  Call(c, f_0051AB30, ret);
  return PopSt0(c);
}

}  // namespace

// The environment map (EAX the coordinates): the direction to the view,
// normalised, reflected about the normal; s from its y, t from its z.
void f_0050A2B0(Ctx& c) {
  const uint32_t e = c.esp, d = e - 0x18;
  uint32_t tc = c.eax, p = kTessXyz, n = kTessNormals;
  for (int32_t i = 0; i < I32(kTessVerts); i++, tc += 8, n += 0xC, p = NextXyz(p)) {
    c.esp = e - 0x28;
    ToView(c, p, d, 0x50A301);
    const St dot = St(Ld(d)) * Ld(n) + St(Ld(d + 4)) * Ld(n + 4) + St(Ld(d + 8)) * Ld(n + 8);
    const St y = dot * Ld(n + 4), z = dot * Ld(n + 8);
    const F32 rz = Fst(z + z - Ld(d + 8));
    Put(tc, Fst((y + y - Ld(d + 4) + 1.0f) * 0.5f));
    Put(tc + 4, Fst(0.5f - St(rz) * 0.5f));
  }
  c.esp = e + 4;
}

// Each position less the view's origin (ECX, three floats a vertex).
void f_0050A380(Ctx& c) {
  uint32_t out = c.ecx, p = kTessXyz;
  for (int32_t i = 0; i < I32(kTessVerts); i++, out += 0xC, p = NextXyz(p))
    for (uint32_t k = 0; k < 12; k += 4) Put(out + k, Fst(St(Ld(p + k)) - Ld(kViewOrigin + k)));
  c.esp += 4;
}

// The view's origin less each position (ECX).
void f_0050A3D0(Ctx& c) {
  uint32_t out = c.ecx, p = kTessXyz;
  for (int32_t i = 0; i < I32(kTessVerts); i++, out += 0xC, p = NextXyz(p))
    for (uint32_t k = 0; k < 12; k += 4) Put(out + k, Fst(St(Ld(kViewOrigin + k)) - Ld(p + k)));
  c.esp += 4;
}

// The direction to the view reflected about each normal (EDX): twice its
// dot with the normal along the normal, less it (z through its store).
void f_0050A420(Ctx& c) {
  uint32_t out = c.edx, p = kTessXyz, n = kTessNormals;
  for (int32_t i = 0; i < I32(kTessVerts); i++, out += 0xC, n += 0xC, p = NextXyz(p)) {
    const St dx = St(Ld(kViewOrigin)) - Ld(p), dy = St(Ld(kViewOrigin + 4)) - Ld(p + 4);
    const F32 dz = Fst(St(Ld(kViewOrigin + 8)) - Ld(p + 8));
    const St dot = dy * Ld(n + 4) + dx * Ld(n) + St(dz) * Ld(n + 8), k = dot + dot;
    const St x = k * Ld(n);
    const F32 y = Fst(k * Ld(n + 4)), z = Fst(k * Ld(n + 8));
    Put(out, Fst(x - dx));
    Put(out + 4, Fst(St(y) - dy));
    Put(out + 8, Fst(St(z) - dz));
  }
  c.esp += 4;
}

// The light's vector less each position (ECX).
void f_0050A4D0(Ctx& c) {
  F32 l[3];
  LightVector(l);
  uint32_t out = c.ecx, p = kTessXyz;
  for (int32_t i = 0; i < I32(kTessVerts); i++, out += 0xC, p = NextXyz(p))
    for (uint32_t k = 0; k < 3; k++) Put(out + 4 * k, Fst(St(l[k]) - Ld(p + 4 * k)));
  c.esp += 4;
}

// The half vector (EAX): the directions to the view and to the light,
// each normalised, summed.
void f_0050A5C0(Ctx& c) {
  const uint32_t e = c.esp, v = e - 0x18, l = e - 0x24;
  F32 light[3];
  LightVector(light);
  uint32_t out = c.eax, p = kTessXyz;
  for (int32_t i = 0; i < I32(kTessVerts); i++, out += 0xC, p = NextXyz(p)) {
    c.esp = e - 0x30;
    ToView(c, p, v, 0x50A6AF);
    for (uint32_t k = 0; k < 3; k++) Put(l + 4 * k, Fst(St(light[k]) - Ld(p + 4 * k)));
    c.eax = l;
    Call(c, f_004268B0, 0x50A6D8);
    for (uint32_t k = 0; k < 12; k += 4) Put(out + k, Fst(St(Ld(l + k)) + Ld(v + k)));
  }
  c.esp = e + 4;
}

// The direction to the view, normalised, plus the world's vector 0x15364B0
// (EAX).
void f_0050A720(Ctx& c) {
  const uint32_t e = c.esp, v = e - 0xC;
  uint32_t out = c.eax, p = kTessXyz;
  for (int32_t i = 0; i < I32(kTessVerts); i++, out += 0xC, p = NextXyz(p)) {
    c.esp = e - 0x18;
    ToView(c, p, v, 0x50A76F);
    for (uint32_t k = 0; k < 12; k += 4) Put(out + k, Fst(St(Ld(0x15364B0 + k)) + Ld(v + k)));
  }
  c.esp = e + 4;
}

// Coordinate k (the stack) of each vertex's tangent, binormal and normal
// (EAX, three floats a vertex), the axes built first.
void f_0050A7C0(Ctx& c) {
  const uint32_t e = c.esp, k = 4 * Arg(c, e, 1);
  uint32_t out = c.eax;
  c.esp = e - 4;
  Axes(c, 0x50A7DA);
  for (int32_t i = 0; i < I32(kTessVerts); i++, out += 0xC) {
    const uint32_t b = kBinormals + k + 0xC * uint32_t(i);
    Put(out, U32(b + 0x18000));
    Put(out + 4, U32(b));
    Put(out + 8, U32(b - 0x18000));
  }
  c.esp = e + 4;
}

// The same for coordinate EAX into ECX, four floats a vertex: the fourth
// the view's origin (0x1549828) less the position (12 bytes apart, not the
// positions' stride).
void f_0050A820(Ctx& c) {
  const uint32_t e = c.esp, k = 4 * c.eax;
  uint32_t out = c.ecx;
  c.esp = e - 8;
  Axes(c, 0x50A83D);
  for (int32_t i = 0; i < I32(kTessVerts); i++, out += 0x10) {
    const uint32_t b = kBinormals + k + 0xC * uint32_t(i);
    Put(out, U32(b + 0x18000));
    Put(out + 4, U32(b));
    Put(out + 8, U32(b - 0x18000));
    Put(out + 0xC, Fst(St(Ld(0x1549828 + k)) - Ld(b - 0x80000)));
  }
  c.esp = e + 4;
}

// s and t swapped (EAX), t then 1 - s.
void f_0050A890(Ctx& c) {
  uint32_t tc = c.eax;
  for (int32_t i = 0; i < I32(kTessVerts); i++, tc += 8) {
    const F32 s = Ld(tc);
    Put(tc, U32(tc + 4));
    Put(tc + 4, Fst(St(1.0f) - St(s)));
  }
  c.esp += 4;
}

// The turbulence (EBX the waveform, EAX the coordinates): s by the sine
// table at (z + x) / 1024 + now, t at y / 1024 + now, x 1024 through
// _ftol, times the amplitude; now the phase plus frequency x time.
void f_0050A8D0(Ctx& c) {
  const uint32_t w = c.ebx;
  const F32 now = Fst(St(Ld(kShaderTime)) * Ld(w + 0x10) + Ld(w + 0xC));
  uint32_t tc = c.eax, p = kTessXyz;
  for (int32_t i = 0; i < I32(kTessVerts); i++, tc += 8, p = NextXyz(p)) {
    const int32_t s = Ftol(((St(Ld(p + 8)) + Ld(p)) * St(0x1p-10) + now) * St(1024.0));
    Put(tc, Fst(St(Ld(kSineTable + 4 * (uint32_t(s) & 0x3FF))) * Ld(w + 8) + Ld(tc)));
    const int32_t t = Ftol((St(Ld(p + 4)) * St(0x1p-10) + now) * St(1024.0));
    Put(tc + 4, Fst(St(Ld(kSineTable + 4 * (uint32_t(t) & 0x3FF))) * Ld(w + 8) + Ld(tc + 4)));
  }
  c.esp += 4;
}

// The scale (EDX s and t, EAX the coordinates).
void f_0050A980(Ctx& c) {
  uint32_t tc = c.eax;
  for (int32_t i = 0; i < I32(kTessVerts); i++, tc += 8) {
    Put(tc, Fst(St(Ld(c.edx)) * Ld(tc)));
    Put(tc + 4, Fst(St(Ld(c.edx + 4)) * Ld(tc + 4)));
  }
  c.esp += 4;
}

// The scroll (EAX the speeds, ECX the coordinates): by time x speed less
// its floor (the CRT's 0x51AB30).
void f_0050A9B0(Ctx& c) {
  const uint32_t e = c.esp, sp = c.eax;
  uint32_t tc = c.ecx;
  const St now = St(Ld(kShaderTime));
  F32 s = Fst(now * Ld(sp)), t = Fst(now * Ld(sp + 4));
  c.esp = e - 0x14;
  s = Fst(s - CrtFloor(c, s, e - 0x14, 0x50A9DA));
  t = Fst(t - CrtFloor(c, t, e - 0x14, 0x50A9EE));
  for (int32_t i = 0; i < I32(kTessVerts); i++, tc += 8) {
    Put(tc, Fst(St(s) + Ld(tc)));
    Put(tc + 4, Fst(St(t) + Ld(tc + 4)));
  }
  c.esp = e + 4;
}

// The transform (EAX the texmod, ECX the coordinates): s' = t x +0x20 +
// s x +0x18 + +0x28, t' = t x +0x24 + s x +0x1C + +0x2C.
void f_0050AA30(Ctx& c) {
  const uint32_t m = c.eax;
  uint32_t tc = c.ecx;
  for (int32_t i = 0; i < I32(kTessVerts); i++, tc += 8) {
    const St s = St(Ld(tc)), t = St(Ld(tc + 4));
    Put(tc, Fst(t * Ld(m + 0x20) + s * Ld(m + 0x18) + Ld(m + 0x28)));
    Put(tc + 4, Fst(t * Ld(m + 0x24) + s * Ld(m + 0x1C) + Ld(m + 0x2C)));
  }
  c.esp += 4;
}

// The rotation (stack: degrees a second, the coordinates): the sine
// table at -(time x speed) x 1024/360 through _ftol and a quarter turn on,
// as a transform about (0.5, 0.5) in the original's frame, through
// 0x50AA30. The speed's slot is left holding sin x 0.5.
void f_0050AA80(Ctx& c) {
  const uint32_t e = c.esp, m = e - 0x44;
  const int32_t i = Ftol(-(St(Ld(kShaderTime)) * Ld(e + 4)) * 0x1.6c16c2p+1f);
  const F32 sn = Ld(kSineTable + 4 * (uint32_t(i) & 0x3FF));
  const F32 cs = Ld(kSineTable + 4 * ((uint32_t(i) + 0x100) & 0x3FF));
  const F32 hc = Fst(St(0.5f) * St(cs)), hs = Fst(St(sn) * 0.5f);
  Put(m + 0x18, Fst(St(cs)));
  Put(m + 0x20, Fst(-St(sn)));
  Put(e + 4, hs);
  Put(m + 0x28, Fst(St(0.5f) - hc + hs));
  Put(m + 0x1C, Fst(St(sn)));
  Put(m + 0x24, Fst(St(cs)));
  Put(m + 0x2C, Fst(St(0.5f) - hs - hc));
  c.esp = e - 0x48;
  c.eax = m;
  c.ecx = Arg(c, e, 2);
  Call(c, f_0050AA30, 0x50AB19);
  c.esp = e + 4;
}

// Into the vertices' axes (EAX, three floats a vertex): each vector's dots
// with the tangent, binormal and normal, the axes built first.
void f_0050AE10(Ctx& c) {
  const uint32_t e = c.esp;
  uint32_t o = c.eax;
  c.esp = e - 4;
  Axes(c, 0x50AE2A);
  for (int32_t i = 0; i < I32(kTessVerts); i++, o += 0xC) {
    const uint32_t a = kTangents + 0xC * uint32_t(i);
    const St x = St(Ld(o)), y = St(Ld(o + 4)), z = St(Ld(o + 8));
    Put(o, Fst(z * Ld(a + 8) + x * Ld(a) + y * Ld(a + 4)));
    const uint32_t b = a - 0x18000, n = a - 0x30000;
    Put(o + 4, Fst(z * Ld(b + 8) + y * Ld(b + 4) + x * Ld(b)));
    Put(o + 8, Fst(z * Ld(n + 8) + y * Ld(n + 4) + x * Ld(n)));
  }
  c.esp = e + 4;
}

// The stretch (ESI the waveform, the stack the coordinates): a transform
// about (0.5, 0.5) by 1 over the waveform, through 0x50AA30.
void f_00508A50(Ctx& c) {
  const uint32_t e = c.esp, w = c.esi, m = e - 0x44;
  c.esp = e - 0x48;
  const St p = 1.0f / WaveAt(w, CallWaveTable(c, U32(w), 0x508A5A));
  Put(m + 0x20, 0u);
  Put(m + 0x1C, 0u);
  Put(m + 0x18, Fst(p));
  const F32 o = Fst(0.5f - St(0.5f) * p);
  Put(m + 0x28, o);
  Put(m + 0x24, Fst(p));
  Put(m + 0x2C, o);
  c.eax = m;
  c.ecx = Arg(c, e, 1);
  Call(c, f_0050AA30, 0x508AD2);
  c.esp = e + 4;
}

// The vertices' axes: per triangle (0x50AB40 inlined) the positions
// weighted by the texture coordinate t's differences, normalised by
// 0x4268B0 (in the original's frame), summed into its vertices' tangents;
// those of more than one triangle normalised (0x426710), each binormal
// the tangent x the normal, normalised; then both flags set. The counts
// are the original's locals (an index past the vertex count reads and
// writes whatever its slot holds).
void f_0050ABF0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi;
  const uint32_t f = ((e - 4) & ~7u) - 0x8010 - 0x10, v = f + 0x10, count = f + 0x20;
  for (uint32_t i = 0, n = U32(kTessVerts) * 3; i < n; i++) Put(kTangents + 4 * i, 0u);
  for (uint32_t i = 0, n = U32(kTessVerts) * 3; i < n; i++) Put(kBinormals + 4 * i, 0u);
  for (uint32_t i = 0, n = U32(kTessVerts); i < n; i++) Put(count + 4 * i, 0u);
  for (int32_t s = 0; s < I32(kTessIndexCount); s += 3) {
    const uint32_t i0 = U16(kTessIndexes + 2 * s), i2 = U16(kTessIndexes + 2 * s + 4),
                   i1 = U16(kTessIndexes + 2 * s + 2);
    auto t = [](uint32_t i) { return Ld(kTessTexCoords + 4 + 8 * i); };
    const St a = St(t(i1)) - t(i2), b = St(t(i2)) - t(i0), d = St(t(i0)) - t(i1);
    const uint32_t stride = U32(kXyzStride), p0 = kTessXyz + 4 * (stride * i0),
                   p1 = kTessXyz + 4 * (stride * i1), p2 = kTessXyz + 4 * (stride * i2);
    for (uint32_t k = 0; k < 12; k += 4)
      Put(v + k, Fst(a * Ld(p0 + k) + b * Ld(p1 + k) + d * Ld(p2 + k)));
    c.esp = f - 4;
    Put(c.esp, v);
    c.eax = v;
    Call(c, f_004268B0, 0x50AC9C);
    for (const uint32_t i : {i0, i1, i2}) Put(count + 4 * i, U32(count + 4 * i) + 1);
    for (const uint32_t i : {i0, i1, i2})
      for (uint32_t k = 0; k < 12; k += 4)
        Put(kTangents + 0xC * i + k, Fst(St(Ld(v + k)) + Ld(kTangents + 0xC * i + k)));
  }
  c.esp = f;
  for (int32_t i = 0; i < I32(kTessVerts); i++) {
    const uint32_t a = kTangents + 0xC * uint32_t(i), b = a - 0x18000, n = a - 0x30000;
    if (I32(count + 4 * uint32_t(i)) > 1) {
      c.esi = a;
      Call(c, f_00426710, 0x50AD8E);
      PopSt0(c);
    }
    Put(b, Fst(St(Ld(a + 8)) * Ld(n + 4) - St(Ld(n + 8)) * Ld(a + 4)));
    Put(b + 4, Fst(St(Ld(a)) * Ld(n + 8) - St(Ld(a + 8)) * Ld(n)));
    Put(b + 8, Fst(St(Ld(n)) * Ld(a + 4) - St(Ld(a)) * Ld(n + 4)));
    c.esi = b;
    Call(c, f_00426710, 0x50ADE3);
    PopSt0(c);
  }
  Put(kAxesSet, 1u);
  Put(kAxesSet2, 1u);
  c.esi = esi;
  c.esp = e + 4;
}

// ---- the colour and alpha generators ---------------------------------------------

namespace {

// A waveform's table (0x508940 inlined; ret: its call's return) for types
// 1-5, 4 KB apart from 0x1544530; another type goes to the original, which
// reports it with ri.Error.
uint32_t WaveTable(Ctx& c, uint32_t type, uint32_t ret) {
  static constexpr uint32_t kTables[5] = {0x1544530, 0x1545530, 0x1546530, 0x1547530,
                                          0x1548530};
  if (type - 1 < 5) return kTables[type - 1];
  return CallWaveTable(c, type, ret);
}

// A waveform at the shader time.
St WaveValue(Ctx& c, uint32_t wave, uint32_t ret) {
  return WaveAt(wave, WaveTable(c, U32(wave), ret));
}

// Clamped to 0..1, a NaN kept.
St Clamp01(St v) {
  if (v < 0.0f) return St(0.0f);
  if (v > 1.0f) return St(1.0f);
  return v;
}

}  // namespace

// The entity's colour (+0x6C) for every vertex (ECX colours); nothing
// without an entity.
void f_0050A030(Ctx& c) {
  if (const uint32_t ent = U32(0x1549B3C)) {
    const uint32_t colour = U32(ent + 0x6C);
    for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) Put(c.ecx + 4 * i, colour);
  }
  c.esp += 4;
}

// One minus the entity's colour, alpha too (ECX colours).
void f_0050A070(Ctx& c) {
  if (const uint32_t ent = U32(0x1549B3C)) {
    const uint32_t colour = ~U32(ent + 0x6C);
    for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) Put(c.ecx + 4 * i, colour);
  }
  c.esp += 4;
}

// The entity's alpha (+0x6F) for every vertex (ECX colours).
void f_0050A0E0(Ctx& c) {
  if (U32(0x1549B3C))
    for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++)
      Wr<uint8_t>(g_guest_base, c.ecx + 4 * i + 3, U8(U32(0x1549B3C) + 0x6F));
  c.esp += 4;
}

// One minus the entity's alpha (ECX colours).
void f_0050A120(Ctx& c) {
  if (U32(0x1549B3C))
    for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++)
      Wr<uint8_t>(g_guest_base, c.ecx + 4 * i + 3, uint8_t(~U8(U32(0x1549B3C) + 0x6F)));
  c.esp += 4;
}

// A waveform's colour (ESI the stage's rgbWave, EAX colours): the noise
// (0x4D3AA0 at the shader time plus the phase, times the frequency) times
// the amplitude plus the base, or the waveform times tr.identityLight;
// clamped to 0..1, grey and opaque.
void f_0050A170(Ctx& c) {
  const uint32_t e = c.esp, wave = c.esi, colours = c.eax;
  c.esp = e - 0xC;
  St v;
  if (U32(wave) == 6) {
    Push(c, Fst((St(Ld(kShaderTime)) + Ld(wave + 0xC)) * Ld(wave + 0x10)).bits);
    for (int k = 0; k < 3; k++) Push(c, 0);
    Call(c, f_004D3AA0, 0x50A198);
    c.esp += 0x10;
    v = PopSt0(c) * Ld(wave + 8) + Ld(wave + 4);
  } else {
    v = WaveValue(c, wave, 0x50A1A8) * Ld(kIdentityLight);
  }
  const uint32_t grey = uint32_t(Fistp(St(Fst(Clamp01(v) * 255.0f))));
  Put(0x13F96D8, grey);
  const uint32_t colour = 0xFF000000u | (grey & 0xFF) * 0x010101u;
  for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) Put(colours + 4 * i, colour);
  c.esp = e + 4;
}

// A waveform's alpha (EAX the stage's alphaWave; stack: colours), clamped
// to 0..1 (0x5089E0 inlined, its call's depth kept for the table's).
void f_0050A270(Ctx& c) {
  const uint32_t e = c.esp, colours = Arg(c, e, 1);
  c.esp = e - 0xC;
  const uint32_t a = uint32_t(Ftol(Clamp01(WaveValue(c, c.eax, 0x5089E8)) * 255.0f));
  for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++)
    Wr<uint8_t>(g_guest_base, colours + 4 * i + 3, uint8_t(a));
  c.esp = e + 4;
}

// The specular alpha (EAX colours): the direction to the fixed light
// (0x564140) normalised (0x4268B0, on the original's local at e - 0x18),
// reflected about the vertex normal, against the direction to the view
// (0x1549AB8) over its length (the inverse square root's approximation);
// its fourth power, 0 below 0, 255 at most.
void f_0050AEC0(Ctx& c) {
  const uint32_t e = c.esp, l = e - 0x18;
  const int32_t n = I32(kTessVerts);
  c.esp = e - 0x34;
  uint32_t xyz = kTessXyz, normal = kTessNormals + 8, alpha = c.eax + 3;
  for (int32_t i = 0; i < n; i++) {
    for (uint32_t k = 0; k < 12; k += 4) Put(l + k, Fst(St(Ld(0x564140 + k)) - Ld(xyz + k)));
    c.eax = l;
    Call(c, f_004268B0, 0x50AF1F);
    const St d = St(Ld(l + 8)) * Ld(normal) + St(Ld(l + 4)) * Ld(normal - 4) +
                 St(Ld(l)) * Ld(normal - 8);
    St rx = d * Ld(normal - 8), ry = d * Ld(normal - 4), rz = d * Ld(normal);
    rx = rx + rx - Ld(l);
    ry = ry + ry - Ld(l + 4);
    rz = rz + rz - Ld(l + 8);
    const St vx = St(Ld(0x1549AB8)) - Ld(xyz), vz = St(Ld(0x1549AC0)) - Ld(xyz + 8);
    const F32 vy = Fst(St(Ld(0x1549ABC)) - Ld(xyz + 4)), vzf = Fst(vz);
    Put(e - 8, vy);
    Put(e - 4, vzf);
    const F32 length2 = Fst(vz * vzf + St(vy) * vy + vx * vx);
    Put(e - 0x20, length2);
    Put(e - 0x1C, length2);
    const F32 y = {0x5F3759DFu - uint32_t(int32_t(length2.bits) >> 1)};
    Put(e - 0x24, y);
    const St inverse = (1.5f - St(length2) * 0.5f * y * y) * y;
    const St dot = St(vzf) * rz + St(vy) * ry + vx * rx;
    const F32 spec = Fst(inverse * dot);
    Put(e - 0x20, spec);
    uint32_t a = 0;
    if (!(St(spec) < 0.0f)) {
      const St s2 = St(spec) * spec;
      a = uint32_t(Ftol(s2 * s2 * 255.0f));
      if (int32_t(a) > 0xFF) a = 0xFF;
    }
    Wr<uint8_t>(g_guest_base, alpha, uint8_t(a));
    xyz += 4 * U32(kXyzStride);
    normal += 0xC;
    alpha += 4;
  }
  c.esp = e + 4;
}

// The diffuse lighting (stack: colours): the entity's light direction
// (+0xA4) against the vertex normal; at most 0 the ambient light's colour
// (+0xBC), else per channel the ambient (+0xB0) plus the directed light
// (+0xC0) times it, 255 at most (through the scratch 0x13F96D8), opaque.
void f_0050B040(Ctx& c) {
  const uint32_t e = c.esp, ent = U32(0x1549B3C);
  const F32 dir[3] = {Ld(ent + 0xA4), Ld(ent + 0xA8), Ld(ent + 0xAC)};
  const F32 ambient[3] = {Ld(ent + 0xB0), Ld(ent + 0xB4), Ld(ent + 0xB8)};
  const F32 directed[3] = {Ld(ent + 0xC0), Ld(ent + 0xC4), Ld(ent + 0xC8)};
  const uint32_t ambient_colour = U32(ent + 0xBC);
  uint32_t colour = Arg(c, e, 1), normal = kTessNormals + 4;
  for (int32_t i = 0, n = I32(kTessVerts); i < n; i++) {
    const St d = St(dir[2]) * Ld(normal + 4) + St(dir[0]) * Ld(normal - 4) +
                 St(dir[1]) * Ld(normal);
    const F32 df = Fst(d);
    if (d <= 0.0f) {
      Put(colour, ambient_colour);
    } else {
      for (uint32_t k = 0; k < 3; k++) {
        int32_t v = Fistp(St(Fst(St(df) * directed[k] + ambient[k])));
        Put(0x13F96D8, uint32_t(v));
        if (v > 0xFF) v = 0xFF;
        Wr<uint8_t>(g_guest_base, colour + k, uint8_t(v));
      }
      Wr<uint8_t>(g_guest_base, colour + 3, 0xFF);
    }
    colour += 4;
    normal += 0xC;
  }
  c.esp = e + 4;
}

// The debug colour of a surface (EAX its index count, EDI the colour) for
// the cvar 0x15343E8 above 6: a random direction seeded by the shader's
// sort index (+0x48) and the count (rand's state kept), normalised; else
// by the count, from grey for 30 or fewer to green above 3000, in
// tr.identityLightByte.
void f_004F2E60(Ctx& c) {
  const uint32_t e = c.esp, n = c.eax, to = c.edi;
  auto byte = [](uint32_t va, uint32_t v) { Wr<uint8_t>(g_guest_base, va, uint8_t(v)); };
  if (int32_t(CvarInt(0x15343E8)) > 6) {
    const uint32_t esi = c.esi, v = e - 0xC;
    c.esp = e - 0x2C;
    Call(c, f_0051B5EC, 0x4F2E7B);
    const uint32_t seed = c.eax;
    Push(c, (U32(U32(kTessShader) + 0x48) + n) * 0x7A69);
    Call(c, f_0051B5DF, 0x4F2E94);
    c.esp += 4;
    static constexpr uint32_t kRand[3] = {0x4F2E9C, 0x4F2EB3, 0x4F2ECA};
    for (uint32_t k = 0; k < 3; k++) {
      Call(c, f_0051B5EC, kRand[k]);
      Put(v + 4 * k, Fst(FromInt(int32_t(c.eax)) * 0x1p-15f));
    }
    c.esi = v;
    Call(c, f_00426710, 0x4F2EE5);
    PopSt0(c);
    for (uint32_t k = 0; k < 3; k++)
      byte(to + k, uint32_t(Fistp(St(Fst(St(Ld(v + 4 * k)) * 255.0f)) + St(0x1p-30))));
    Push(c, seed);
    Call(c, f_0051B5DF, 0x4F2F7F);
    byte(to + 3, 0xFF);
    c.esi = esi;
    c.esp = e + 4;
    return;
  }
  const int32_t light = I32(kIdentityLightByte), m = int32_t(n);
  int32_t rgb[3];
  if (m <= 30) rgb[0] = rgb[1] = rgb[2] = light;
  else if (m <= 150) rgb[0] = light, rgb[1] = rgb[2] = light >> 1;
  else if (m <= 450) rgb[0] = light, rgb[1] = rgb[2] = 0;
  else if (m <= 1200) rgb[0] = rgb[1] = light, rgb[2] = 0;
  else if (m > 3000) rgb[0] = rgb[2] = 0, rgb[1] = light;
  else rgb[0] = rgb[1] = light >> 2, rgb[2] = light;
  for (uint32_t k = 0; k < 3; k++) byte(to + k, uint32_t(rgb[k]));
  byte(to + 3, 0xFF);
  c.esp = e + 4;
}

// ---- a stage's colours ----------------------------------------------------------

namespace {

void Alpha(uint32_t i, uint32_t a) { Wr<uint8_t>(g_guest_base, kColors + 4 * i + 3, uint8_t(a)); }

// The view-dependent alphas' view origin (at e - 0xC): the view's, or for
// an entity the difference rotated into its space (0x426A60, through the
// original's locals).
void ViewOrigin(Ctx& c, uint32_t e, uint32_t ret) {
  const uint32_t v = e - 0x18, o = e - 0xC, ent = U32(0x1549B3C);
  if (ent == 0x1535D6C) {
    for (uint32_t k = 0; k < 12; k += 4) Put(o + k, U32(0x1549828 + k));
    return;
  }
  for (uint32_t k = 0; k < 12; k += 4) Put(v + k, Fst(St(Ld(0x1549828 + k)) - Ld(ent + 0x44 + k)));
  c.eax = ent + 0x1C;
  c.edx = o;
  c.ecx = v;
  Call(c, f_00426A60, ret);
}

// Per vertex of the view-dependent alphas: the normalised direction to the
// view origin (0x4268B0 on the original's local at e - 0x18) against the
// vertex normal, stored as a float in the stage's slot; `alpha` takes the
// slot's bits to the alpha.
template <typename F>
void ViewAlphas(Ctx& c, uint32_t e, uint32_t ret, bool xz, bool negate, F alpha) {
  const uint32_t v = e - 0x18, o = e - 0xC;
  uint32_t xyz = kTessXyz, normal = kTessNormals + 4;
  for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) {
    for (uint32_t k = 0; k < 12; k += 4) Put(v + k, Fst(St(Ld(o + k)) - Ld(xyz + k)));
    c.eax = v;
    Call(c, f_004268B0, ret);
    const St x = St(Ld(v)) * Ld(normal - 4), z = St(Ld(v + 8)) * Ld(normal + 4);
    const St dot = (xz ? x + z : z + x) + St(Ld(v + 4)) * Ld(normal);
    Put(e + 4, Fst(negate ? -dot : dot));
    Alpha(i, alpha(U32(e + 4)));
    xyz += 4 * U32(kXyzStride);
    normal += 0xC;
  }
}

}  // namespace

// A stage's vertex colours (stage on the stack) into the stage colours
// 0x16E0C40, by its rgbGen (+0x664: 2 white, 3 the entity's, 4 one minus
// it, 5 the vertex colours, 6 them shifted down by the overbright bits, 7
// one minus them, 8 a waveform, 9/10 the diffuse lighting, 12 a constant
// (+0x680), 13 a constant or the debug colours, otherwise
// tr.identityLight's byte) and alphaGen (+0x67C: 0 opaque, 2 the
// entity's, 3 one minus it, 4 the vertices', 5 one minus them, 6
// specular, 7 a waveform, 8 the portal distance, 9 a constant (+0x683),
// 10-13 view-dependent),
// from the tessellator's vertices (colours 0x1690C40). The stage's slot is
// the original's scratch for the values it converts.
void f_004F4FA0(Ctx& c) {
  const uint32_t e = c.esp, stage = Arg(c, e, 1);
  const uint32_t esi = c.esi, edi = c.edi;
  c.esp = e - 0x28;
  auto fill = [](uint32_t v) {
    for (uint32_t i = 0, n = U32(kTessVerts); i < n; i++) Put(kColors + 4 * i, v);
  };
  auto byte = [](uint32_t va, uint32_t v) { Wr<uint8_t>(g_guest_base, va, uint8_t(v)); };
  const uint32_t rgb = U32(stage + 0x664);
  switch (rgb) {
    case 2:
      fill(0xFFFFFFFF);
      break;
    case 3:
      c.ecx = kColors;
      Call(c, f_0050A030, 0x4F51CD);
      break;
    case 4:
      c.ecx = kColors;
      Call(c, f_0050A070, 0x4F51D9);
      break;
    case 6:
      if (const uint32_t shift = U32(0x1536298)) {
        const uint32_t s = shift & 31;
        for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) {
          const uint32_t from = kVertColors + 4 * i, to = kColors + 4 * i;
          byte(to, U8(from) >> s);
          byte(to + 1, U8(from + 1) >> s);
          byte(to + 3, U8(from + 3));
          byte(to + 2, U8(from + 2) >> s);
        }
        break;
      }
      [[fallthrough]];
    case 5:
      for (uint32_t i = 0, n = U32(kTessVerts); i < n; i++) Put(kColors + 4 * i, U32(kVertColors + 4 * i));
      break;
    case 7:
      for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++)
        for (uint32_t k = 0; k < 3; k++) {
          const uint32_t from = U8(kVertColors + 4 * i + k);
          if (St(Ld(kIdentityLight)) == 1.0f) {
            byte(kColors + 4 * i + k, 0xFF - from);
          } else {
            Put(e + 4, 0xFF - from);
            byte(kColors + 4 * i + k, uint32_t(Ftol(FromInt(int32_t(0xFF - from)) * Ld(kIdentityLight))));
          }
        }
      break;
    case 8:
      c.esi = stage + 0x650;
      c.eax = kColors;
      Call(c, f_0050A170, 0x4F51BE);
      c.esi = esi;
      break;
    case 9:
    case 10:
      if (U32(U32(0x1535A34) + 0x118)) {
        fill(0xFFFFFFFF);
      } else {
        Push(c, kColors);
        Call(c, f_0050B040, 0x4F4FE5);
        c.esp += 4;
      }
      break;
    case 12:
      for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) Put(kColors + 4 * i, U32(stage + 0x680));
      break;
    case 13:
      if (I32(U32(0x15343E8) + 0x20) >= 5) {
        c.eax = U32(0x17F0C84);
        c.edi = kColors;
        Call(c, f_004F2E60, 0x4F51F5);
        c.edi = edi;
      } else {
        Put(kColors, U32(stage + 0x680));
      }
      for (uint32_t i = 1; int32_t(i) < I32(kTessVerts); i++) Put(kColors + 4 * i, U32(kColors));
      break;
    default:
      fill(U8(kIdentityLightByte) * 0x01010101u);
      break;
  }

  // Clamps of a float's bits: negative (sign set) to +0, or positive to +0.
  auto non_negative = [](uint32_t b) { return int32_t(b) < 0 ? 0u : b; };
  auto non_positive = [](uint32_t b) { return int32_t(b) < 0 ? b : 0u; };
  switch (U32(stage + 0x67C)) {
    case 0:
      if (rgb != 2 && !(rgb == 6 && St(Ld(kIdentityLight)) == 1.0f))
        for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) Alpha(i, 0xFF);
      break;
    case 2:
      c.ecx = kColors;
      Call(c, f_0050A0E0, 0x4F5337);
      break;
    case 3:
      c.ecx = kColors;
      Call(c, f_0050A120, 0x4F5349);
      break;
    case 4:
      if (rgb != 6)
        for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) Alpha(i, U8(kVertColors + 4 * i + 3));
      break;
    case 5:
      for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) Alpha(i, 0xFF - U8(kVertColors + 4 * i + 3));
      break;
    case 6:
      c.eax = kColors;
      Call(c, f_0050AEC0, 0x4F5325);
      break;
    case 7:
      Push(c, kColors);
      c.eax = stage + 0x668;
      Call(c, f_0050A270, 0x4F5310);
      c.esp += 4;
      break;
    case 8: {
      // The distance to the view over the shader's portal range (+0xA4):
      // 0 in front, 255 beyond 1.
      uint32_t xyz = kTessXyz;
      for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) {
        const St dx = St(Ld(xyz)) - Ld(0x1549828), dy = St(Ld(xyz + 4)) - Ld(0x154982C),
                 dz = St(Ld(xyz + 8)) - Ld(0x1549830);
        const St d = Sqrt(dz * dz + dy * dy + dx * dx) / Ld(U32(kTessShader) + 0xA4);
        Put(e + 4, Fst(d));
        uint32_t a;
        if (d < 0.0f) a = 0;
        else if (!(Ld(e + 4) > 1.0f)) a = uint32_t(Ftol(St(Ld(e + 4)) * 255.0f));
        else a = 0xFF;
        Alpha(i, a);
        xyz += 4 * U32(kXyzStride);
      }
      break;
    }
    case 9:
      if (rgb != 12)
        for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) Alpha(i, U8(stage + 0x683));
      break;
    case 10:
      ViewOrigin(c, e, 0x4F54DE);
      ViewAlphas(c, e, 0x4F5529, false, false, [&](uint32_t b) {
        Put(e + 4, non_negative(b));
        return uint32_t(Ftol(St(Ld(e + 4)) * 255.0f + 0.5f));
      });
      break;
    case 11:
      ViewOrigin(c, e, 0x4F55F6);
      ViewAlphas(c, e, 0x4F5639, false, false, [&](uint32_t b) {
        Put(e + 4, non_negative(b));
        return uint32_t(Ftol((St(1.0f) - Ld(e + 4)) * 255.0f + 0.5f));
      });
      break;
    case 12:
      ViewOrigin(c, e, 0x4F570C);
      ViewAlphas(c, e, 0x4F574E, true, false, [&](uint32_t b) {
        Put(e + 4, non_positive(b));
        return uint32_t(Ftol((St(Ld(e + 4)) + 1.0f) * 255.0f + 0.5f));
      });
      break;
    case 13:
      ViewOrigin(c, e, 0x4F581F);
      ViewAlphas(c, e, 0x4F5869, false, true, [&](uint32_t b) {
        Put(e + 4, non_negative(b));
        return uint32_t(Ftol(St(Ld(e + 4)) * 255.0f + 0.5f));
      });
      break;
  }
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// ---- the stages and the flush ------------------------------------------------

// The flush's stages (eight pointers, null-terminated), how many there are,
// and whether the generic iterator has set the arrays up for a single stage
// (then a stage leaves them).
constexpr uint32_t kTessStages = 0x1830CA4, kNumStages = 0x1830C9C, kArraysSet = 0x11E075C;

// One stage drawn (EAX stage, EBX indexes; stack: index count): its
// colours, its texture coordinates, the colour array, then either all its
// texture units (a second bundle with an image, +0xCC: lines for the cvar
// 0x154992C) or the first unit's image (the shader's first stage state bit
// 0x100000 with the cvar 0x1534438: the image 0x1535AC4) with the stage's
// combiners, vertex program and fragment shader; then the draw.
void f_004F5DE0(Ctx& c) {
  const uint32_t e = c.esp, stage = c.eax, n = Arg(c, e, 1);
  const uint32_t esi = c.esi, edi = c.edi, ebp = c.ebp;
  c.esp = e - 0x10;
  c.edi = stage;
  c.ebp = n;
  Push(c, stage);
  Call(c, f_004F4FA0, 0x4F5DF0);
  c.esp += 4;
  c.eax = stage;
  Call(c, f_004F5940, 0x4F5DFA);
  if (!U32(kArraysSet)) {
    c.esi = 0x100;
    if (!(U32(kArrayBits) & 0x100)) {
      Push(c, 0x8076);
      CallPtr(c, kglEnableClientState, 0x4F5E16);
      Put(kArrayBits, U32(kArrayBits) | 0x100);
    }
    Push(c, kColors);
    Push(c, 0);
    Push(c, 0x1401);
    Push(c, 4);
    CallPtr(c, kglColorPointer, 0x4F5E30);
  }
  uint32_t ret;
  if (U32(stage + 0xCC)) {
    c.eax = U32(stage + 0x684);
    Call(c, f_004CB710, 0x4F5E4B);
    if (U32(0x154992C)) {
      Push(c, 0x1B02);
      Push(c, 0x408);
      CallPtr(c, kglPolygonMode, 0x4F5E5E);
    }
    Push(c, 0);
    Push(c, 0x16E8C40);
    Push(c, stage);
    Call(c, f_004F3D80, 0x4F5E71);
    c.esp += 0xC;
    ret = 0x4F5E7D;
  } else {
    if (!U32(kArraysSet)) {
      Push(c, U32(0x16E8C40));
      Push(c, 0);
      Push(c, 0x1406);
      Push(c, U32(stage + 0x90));
      CallPtr(c, kglTexCoordPointer, 0x4F5E9F);
    }
    if ((U32(U32(U32(kTessShader) + 0x154) + 0x684) & 0x100000) && CvarInt(0x1534438)) {
      c.esi = U32(0x1535AC4);
    } else {
      c.esi = stage + 4;
      Push(c, U32(kUnit));
      Call(c, f_004F3330, 0x4F5EE1);
      c.esp += 4;
      c.esi = c.eax;
    }
    Call(c, f_004CB3B0, 0x4F5EEB);
    c.eax = U32(stage + 0x684);
    Call(c, f_004CB710, 0x4F5EF6);
    if (U32(stage + 0x648)) {
      Push(c, U32(stage + 0x648));
      Call(c, f_004F3A70, 0x4F5F06);
      c.esp += 4;
    }
    c.esi = U32(stage + 0x64C);
    if (c.esi) Call(c, f_004F3C70, 0x4F5F18);
    c.edi = U32(stage + 0x644);
    if (c.edi != U32(0x1534670)) {
      Push(c, c.edi);
      CallPtr(c, 0x1535014, 0x4F5F27);
      Put(0x1534670, c.edi);
    }
    ret = 0x4F5F3C;
  }
  c.edx = n;
  c.eax = c.ebx;
  Call(c, f_004F32C0, ret);
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}

// The flush's stages drawn in turn (stack: the indexes, unused) from one
// texture unit, but for those with the sign bit of their first byte; with
// the cvar 0x15343B4 the first stage with a lightmap bundle (+0xC8 or
// +0x190 set) is the last.
void f_004F6070(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi;
  c.esp = e - 0x10;
  Call(c, f_004F4090, 0x4F6079);
  for (c.edi = 0; int32_t(c.edi) < 0x20; c.edi += 4) {
    c.esi = U32(c.edi + U32(kTessStages));
    if (!c.esi) break;
    if (int8_t(U8(c.esi)) < 0) continue;
    Push(c, U32(kTessIndexCount));
    c.ebx = kTessIndexes;
    c.eax = c.esi;
    Call(c, f_004F5DE0, 0x4F60A4);
    c.esp += 4;
    if (CvarInt(0x15343B4) && (U8(c.esi + 0xC8) || U8(c.esi + 0x190))) break;
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// ---- the dlights -----------------------------------------------------------------

// Two axes across the normal ECX, into ESI and EDX: from 1 / (1 + x) for a
// normal with x above -1, else from 1 / (1 + z).
void f_004F30A0(Ctx& c) {
  const uint32_t n = c.ecx, s = c.esi, t = c.edx;
  if (Ld(n) > -1.0f) {
    const F32 k = Fst(1.0f / (St(Ld(n)) + 1.0f));
    const uint32_t y = U32(n + 4);
    const St ny(Ld(n + 4)), t1 = ny * ny * k, t2 = St(Ld(n + 8)) * Ld(n + 4) * k;
    const St nz(Ld(n + 8));
    Put(s, y);
    const F32 k2 = Fst(nz * nz * k);
    Put(s + 4, Fst(t1 - 1.0f));
    Put(s + 8, Fst(t2));
    const uint32_t z = U32(n + 8);
    Put(t + 4, Fst(t2));
    Put(t, z);
    Put(t + 8, Fst(St(k2) - 1.0f));
  } else {
    const F32 k = Fst(1.0f / (St(Ld(n + 8)) + 1.0f));
    const St nx(Ld(n)), t1 = nx * nx * k, t2 = St(Ld(n + 4)) * Ld(n) * k;
    const St ny(Ld(n + 4)), t3 = ny * ny * k;
    Put(s, Fst(-t2));
    Put(s + 4, Fst(St(1.0f) - t3));
    Put(s + 8, Fst(-St(Ld(n + 4))));
    Put(t, Fst(t1 - 1.0f));
    Put(t + 4, Fst(t2));
    Put(t + 8, U32(n));
  }
  c.esp += 4;
}

// A dlight on the tessellator (stack: the dlight, the index buffer; ECX
// the colours, EDX the texture coordinates; returns the index count): with
// the cvar 0x15344E4 and 0x17F0C64 the vertices' axes (0x16B0C40,
// 0x16C8C40, 0x4F30A0) once per batch (0x17F0C68, 0x17F0C6C), the light's
// offset from each vertex projected onto them, else taken as it is; the
// coordinates 0.5 plus the offset over the radius (+0x74), the colour the
// light's (+4, 0-1) times its strength along the third axis (full within
// half the radius, falling to none at it), and per vertex the clip bits
// outside 0-1 and the radius, in the original's frame; then the triangles
// not wholly outside one side. The cookie check and __chkstk's probes
// (the guest stack is committed) are left out.
void f_004F4BD0(Ctx& c) {
  const uint32_t e = c.esp, light = Arg(c, e, 1), out = Arg(c, e, 2);
  const uint32_t ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const uint32_t clip = e - 0x2004;
  auto byte = [](uint32_t va, uint32_t v) { Wr<uint8_t>(g_guest_base, va, uint8_t(v)); };
  const F32 radius = Ld(light + 0x74), inv = Fst(1.0f / St(radius));
  const F32 colour[3] = {Fst(St(Ld(light + 4)) * 255.0f), Fst(St(Ld(light + 8)) * 255.0f),
                         Fst(St(Ld(light + 0xC)) * 255.0f)};
  const F32 origin[3] = {Ld(light + 0x78), Ld(light + 0x7C), Ld(light + 0x80)};
  uint32_t coords = c.edx, colours = c.ecx;
  c.ebx = coords;
  c.edi = colours;
  c.esp = e - 0x2058;
  auto axes = [] { return CvarInt(0x15344E4) && U32(0x17F0C64); };
  if (axes() && (!U32(0x17F0C6C) || !U32(0x17F0C68))) {
    for (c.ebp = 0; int32_t(c.ebp) < I32(kTessVerts); c.ebp++) {
      c.esi = 0x16C8C40 + 0xC * c.ebp;
      c.edx = c.esi - 0x18000;
      c.ecx = c.esi - 0x30000;
      Call(c, f_004F30A0, 0x4F4CA1);
    }
    Put(0x17F0C6C, 1);
    Put(0x17F0C68, 1);
  }
  uint32_t xyz = kTessXyz;
  for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) {
    St s, t, dist;
    if (axes()) {
      const uint32_t a = 0x16C8C40 + 0xC * i, b = a - 0x18000, n = a - 0x30000;
      const F32 d0 = Fst(St(origin[0]) - Ld(xyz)), d1 = Fst(St(origin[1]) - Ld(xyz + 4));
      const St d2s = St(origin[2]) - Ld(xyz + 8);  // kept on the stack for the first axis
      const F32 d2 = Fst(d2s);
      s = d2s * Ld(a + 8) + St(d0) * Ld(a) + St(d1) * Ld(a + 4);
      t = St(d2) * Ld(b + 8) + St(d1) * Ld(b + 4) + St(d0) * Ld(b);
      dist = St(d2) * Ld(n + 8) + St(d1) * Ld(n + 4) + St(d0) * Ld(n);
    } else {
      s = St(origin[0]) - Ld(xyz);
      t = St(origin[1]) - Ld(xyz + 4);
      dist = St(origin[2]) - Ld(xyz + 8);
    }
    const F32 d = Fst(dist);
    Put(0x1549B20, U32(0x1549B20) + 1);
    const F32 sf = Fst(s * inv + 0.5f);
    Put(coords, sf);
    const St tv = t * inv + 0.5f;  // compared before its store rounds it
    Put(coords + 4, Fst(tv));
    uint32_t bits = 0;
    if (sf < 0.0f) bits = 1;
    else if (sf > 1.0f) bits = 2;
    if (tv < 0.0f) bits |= 4;
    else if (tv > 1.0f) bits |= 8;
    F32 strength;
    if (d > St(radius)) {
      bits |= 0x10;
      strength = {0};
    } else if (-St(radius) > d) {
      bits |= 0x20;
      strength = {0};
    } else {
      const F32 a = {d.bits & 0x7FFFFFFF};
      if (St(radius) * 0.5f > a) {
        strength = {0x3F800000};
      } else {
        const St x = (St(radius) - a) * inv;
        strength = Fst(x + x);
      }
    }
    byte(clip + i, bits);
    for (uint32_t k = 0; k < 3; k++) {
      const uint32_t v = uint32_t(Fistp(St(Fst(St(strength) * colour[k]))));
      Put(0x13F96D8, v);
      byte(colours + k, v);
    }
    byte(colours + 3, 0xFF);
    xyz += 4 * U32(kXyzStride);
    coords += 8;
    colours += 4;
  }
  uint32_t n = 0;
  for (uint32_t j = 0; int32_t(j) < I32(kTessIndexCount); j += 3) {
    const uint32_t i0 = U16(kTessIndexes + 2 * j), i1 = U16(kTessIndexes + 2 * j + 2),
                   i2 = U16(kTessIndexes + 2 * j + 4);
    if (!(U8(clip + i0) & U8(clip + i1) & U8(clip + i2))) {
      Wr<uint16_t>(g_guest_base, out + 2 * n, uint16_t(i0));
      Wr<uint16_t>(g_guest_base, out + 2 * n + 2, uint16_t(i1));
      Wr<uint16_t>(g_guest_base, out + 2 * n + 4, uint16_t(i2));
      n += 3;
    }
  }
  c.eax = n;
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}

// The dlight pass: the texture units back to one, then each dlight
// (0x15497FC of them, 0x88 bytes from 0x1549804) whose bit the batch has
// (0x17F0C80) with triangles left (f_004F4BD0 into the original's frame,
// colours into the vertex colours, coordinates 0x1670C40): drawn in the
// stages with the sign bit of their first byte for a shader with the sign
// bit of +0x54 (the light 0x1549B40, left set), else in the first stage of
// the shader 0x1535AF4. The cookie check and __chkstk's probes are left
// out.
void f_004F5F50(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const uint32_t indexes = e - 0x1800C;
  c.esp = e - 0x18024;
  if (U32(0x15497FC)) {
    Call(c, f_004F4090, 0x4F5F83);
    c.ebp = 0;
    for (uint32_t i = 0; int32_t(i) < I32(0x15497FC); i++, c.ebp += 0x88) {
      if (!(U32(0x17F0C80) & 1u << (i & 31))) continue;
      c.esi = U32(0x1549804) + c.ebp;
      Push(c, indexes);
      Push(c, c.esi);
      c.ecx = kVertColors;
      c.edx = 0x1670C40;
      Call(c, f_004F4BD0, 0x4F5FD1);
      c.esp += 8;
      c.edi = c.eax;
      if (!c.edi) continue;
      auto draw = [&](uint32_t stage, uint32_t ret) {
        Push(c, c.edi);
        c.ebx = indexes;
        c.eax = stage;
        Call(c, f_004F5DE0, ret);
        c.esp += 4;
      };
      if (int8_t(U8(U32(kTessShader) + 0x54)) < 0) {
        Put(0x1549B40, c.esi);
        for (c.esi = 0; int32_t(c.esi) < 0x20; c.esi += 4) {
          const uint32_t stage = U32(c.esi + U32(kTessStages));
          if (!stage) break;
          if (int8_t(U8(stage)) < 0) draw(stage, 0x4F600C);
        }
      } else if (const uint32_t stage = U32(U32(0x1535AF4) + 0x154)) {
        draw(stage, 0x4F6033);
      }
    }
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}

// RB_StageIteratorGeneric ("--- RB_StageIteratorGeneric( %s ) ---" to the
// log file), on an 8-aligned frame (stack: dlights only, after an
// optimised iterator): the deforms, the log line (cvar 0x1534704, the file
// 0x1830CE4), one texture unit, the fog and the shader's cull; the client
// arrays (normals with the shader's flag 0x100 at +0x50), and for a single
// stage of a shader without +0x15C the colour and texture coordinate
// arrays here (kArraysSet); the vertex array, locked when the extension
// is there; the stages, then the dlights (the shader's sort +0x58 at most
// 5, flags +0x5C without 0x20004) or, for a shader with the sign bit of
// +0x54, the entity's (0x1549B3C) lights over the stages with the sign bit
// of their first byte (each light 0x1549B40, its scale 0x1549B44).
void f_004F60E0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  c.ebp = e - 4;
  c.esp = ((e - 4) & ~7u) - 0x10;
  const uint32_t dlights_only = Arg(c, e, 1);
  if (I32(kNumStages) > 0 && (!dlights_only || U32(0x17F0C80))) {
    Call(c, f_00509F20, 0x4F6110);
    const uint32_t normals = (U32(U32(kTessShader) + 0x50) >> 8) & 1;
    c.ebx = normals;
    c.edi = 1;
    if (CvarInt(0x1534704)) {
      Push(c, U32(kTessShader));
      Push(c, 0x53C044);
      Call(c, f_00440940, 0x4F613A);
      c.esp += 8;
      if (const uint32_t log = U32(0x1830CE4)) {
        Push(c, c.eax);
        Push(c, 0x55B2B4);
        Push(c, log);
        Call(c, f_0051D45A, 0x4F6153);
        c.esp += 0xC;
      }
    }
    Call(c, f_004F4090, 0x4F615B);
    Call(c, f_004F5D50, 0x4F6160);
    c.esi = U32(U32(kTessShader) + 0xA8);
    Call(c, f_004CB5D0, 0x4F6171);
    auto arrays = [&](uint32_t bits, uint32_t ret) {
      Push(c, bits);
      Call(c, f_004CBBE0, ret);
      c.esp += 4;
    };
    if (I32(kNumStages) > 1 || U32(U32(U32(kTessShader) + 0x154) + 0x15C)) {
      if (normals) arrays(0x600, 0x4F6252);
      else arrays(0x400, 0x4F6261);
    } else {
      Put(kArraysSet, 1);
      arrays(normals ? 0x701 : 0x501, 0x4F61B1);
      Push(c, kColors);
      Push(c, 0);
      Push(c, 0x1401);
      Push(c, 4);
      CallPtr(c, kglColorPointer, 0x4F61C2);
      Push(c, 0x16E8C60);
      Push(c, 0);
      Push(c, 0x1406);
      Push(c, U32(U32(U32(kTessShader) + 0x154) + 0x90));
      CallPtr(c, kglTexCoordPointer, 0x4F61E7);
    }
    if (normals) {
      Push(c, kTessNormals);
      Push(c, 0xC);
      Push(c, 0x1406);
      CallPtr(c, kglNormalPointer, 0x4F61FD);
    }
    Push(c, kTessXyz);
    Push(c, 0);
    Push(c, 0x1406);
    Push(c, U32(kXyzStride));
    CallPtr(c, kglVertexPointer, 0x4F6216);
    if (const uint32_t lock = U32(kglLockArraysEXT)) {
      Push(c, U32(kTessVerts));
      Push(c, 0);
      CallTo(c, lock, 0x4F622E);
    }
    if (!U32(kArraysSet)) arrays(normals ? 0x701 : 0x501, 0x4F6270);
    if (!dlights_only) {
      Push(c, kTessIndexes);
      Call(c, f_004F6070, 0x4F6284);
      c.esp += 4;
    }
    const uint32_t shader = U32(kTessShader);
    if (U32(0x17F0C80) && St(Ld(shader + 0x58)) <= 5.0f && !(U32(shader + 0x5C) & 0x20004)) {
      Call(c, f_004F5F50, 0x4F62B4);
    } else if (int8_t(U8(shader + 0x54)) < 0 && U32(0x1549B3C) && U32(U32(0x1549B3C) + 0xCC)) {
      uint32_t ent = U32(0x1549B3C);
      if (I32(ent + 0xCC) > 0) {
        for (c.edi = 0; int32_t(c.edi) < I32(ent + 0xCC); c.edi++) {
          const uint32_t light = U32(ent + 8 * c.edi + 0xD0);
          Put(0x1549B40, light);
          if (U32(light) == 8) continue;
          Put(0x1549B44, Fst(St(Ld(ent + 8 * c.edi + 0xD4))));
          for (c.esi = 0; int32_t(c.esi) < 0x20; c.esi += 4) {
            const uint32_t stage = U32(c.esi + U32(kTessStages));
            if (!stage) break;
            if (int8_t(U8(stage)) >= 0) continue;
            Push(c, U32(kTessIndexCount));
            c.ebx = kTessIndexes;
            c.eax = stage;
            Call(c, f_004F5DE0, 0x4F6333);
            ent = U32(0x1549B3C);
            c.esp += 4;
          }
        }
      }
      Put(0x1549B40, 0);
    }
    if (const uint32_t unlock = U32(kglUnlockArraysEXT)) CallTo(c, unlock, 0x4F6362);
    Put(kArraysSet, 0);
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}

// ---- the shadow volumes ---------------------------------------------------------

// The tessellator's edges (written by f_005082D0): per vertex a count
// (32 at most) and 32 edges of 8 bytes, the other vertex and the facing of
// their triangle (0 or 1); and per triangle its facing.
constexpr uint32_t kEdgeCounts = 0x13F16D8, kEdges = 0x11F16D8, kFacing = 0x11E16D8;

// The silhouette: each edge of a triangle facing the light that no edge
// back from its other vertex with a facing triangle pairs, as a strip from
// both vertices to their copies away from the light (the vertex count on)
// through the immediate-mode batch: f_004ADC20 begins it (32-byte
// vertices from 0x15495F0, 0x15495EC of them at most, the texture
// coordinates 0x15495D0 and colour 0x15495D8 with each), glDrawArrays with
// its mode 0x15495C8 draws each strip. Then the batch's flag 0x15495C4
// off, the fog back on (the state bits' fog flag, unless 0x1549B50, as
// f_004C89D0 has it) and the tessellator's vertex array. The pairing
// counts are in the original's frame, indexed by the edges' facing; the
// frame is left as the original leaves it (f_005082D0).
void f_00507F40(Ctx& c) {
  const uint32_t e = c.esp, hit = e - 8, edges = e - 0xC, left = e - 0x10;
  Put(e - 0x14, c.ebx);
  Put(e - 0x18, c.esi);
  Put(e - 0x1C, c.edi);
  c.esp = e - 0x1C;
  Call(c, f_004ADC20, 0x507F4B);
  auto vertex = [](uint32_t& count, uint32_t v, bool first) {
    const uint32_t xyz = kTessXyz + 4 * (U32(kXyzStride) * v);
    const F32 x = Fst(St(Ld(xyz))), y = Fst(St(Ld(xyz + 4))), z = Fst(St(Ld(xyz + 8)));
    if (first) Put(0x15495C8, 5);
    if (int32_t(count) >= I32(0x15495EC)) return;
    const uint32_t out = U32(0x15495F0) + (count << 5);
    for (uint32_t k = 0; k < 4; k++) Wr<uint8_t>(g_guest_base, out + 0x1C + k, U8(0x15495D8 + k));
    Put(out + 0xC, U32(0x15495D0));
    const uint32_t t = U32(0x15495D4);
    Put(out, x);
    Put(out + 0x10, t);
    Put(out + 4, y);
    Put(out + 8, z);
    count = U32(0x15495E8) + 1;
    Put(0x15495E8, count);
  };
  if (I32(kTessVerts) > 0) {
    Push(c, c.ebp);
    uint32_t count = U32(0x15495E8), i = 0;
    for (; int32_t(i) < I32(kTessVerts); i++) {
      const int32_t n = I32(kEdgeCounts + 4 * i);
      uint32_t edge = kEdges + 0x100 * i;
      Put(edges, edge);
      for (int32_t j = 0; j < n; j++, edge += 8) {
        Put(left, uint32_t(n - j - 1));
        if (!U32(edge + 4)) continue;
        const uint32_t i2 = U32(edge);
        const int32_t n2 = I32(kEdgeCounts + 4 * i2);
        Put(hit, 0);
        Put(hit + 4, 0);
        for (int32_t k = 0; k < n2; k++) {
          const uint32_t back = kEdges + 0x100 * i2 + 8 * k;
          if (U32(back) != i) continue;
          const uint32_t f = hit + 4 * U32(back + 4);
          Put(f, U32(f) + 1);
        }
        if (U32(hit + 4)) continue;
        vertex(count, i, true);
        vertex(count, U32(kTessVerts) + i, false);
        vertex(count, i2, false);
        vertex(count, U32(kTessVerts) + i2, false);
        Push(c, count);
        Push(c, 0);
        Push(c, U32(0x15495C8));
        CallPtr(c, kglDrawArrays, 0x508216);
        count = 0;
        Put(0x15495E8, 0);
        Put(0x15495C8, 0);
      }
    }
    Put(edges, kEdges + 0x100 * i);
    c.esp = e - 0x1C;
  }
  Put(0x15495C4, 0);
  if (!U32(0x1549B50) && !(U32(kStateBits) & 0x200000) && CvarInt(0x15347F4) &&
      U32(U8(0x15496EC) & 8 ? 0x1535890 : 0x13F9CF8)) {
    Enable(c, 0xB60, 0x50829C);
    Put(kStateBits, U32(kStateBits) | 0x200000);
  }
  Push(c, kTessXyz);
  Push(c, 0);
  Push(c, 0x1406);
  Push(c, 3);
  CallPtr(c, kglVertexPointer, 0x5082B5);
  c.esp = e + 4;
}

// The flush of shader 0x1535AEC: the tessellator's shadow volume, with a
// stencil buffer of 4 bits or more (0x1534768) and fewer than 0x1000
// vertices. The vertices' copies 512 away along the entity's light
// direction (0x1549B3C +0xA4) after them; each triangle's facing (its
// normal towards the light) and its three edges (32 per vertex at most);
// then the white image, state bits 0x12, a grey colour, colour writes off
// and the stencil test always passing, and the silhouette (f_00507F40)
// twice: back faces incrementing, front faces decrementing (the cull type
// set as f_004CB5D0 sets it); colour writes back on. Its locals stay in
// the original's frame: the world's optimised iterators (f_004F70F0),
// which RB_EndSurface calls from the same depth, take their second unit's
// texture coordinates from a local they never write, in this frame (the
// last triangle's second edge).
void f_005082D0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi;
  const uint32_t verts = U32(kTessVerts);
  if (int32_t(verts) >= 0x1000 || I32(0x1534768) < 4) {
    c.esp = e + 4;
    return;
  }
  const uint32_t ent = U32(0x1549B3C);
  const F32 light[3] = {Ld(ent + 0xA4), Ld(ent + 0xA8), Ld(ent + 0xAC)};
  for (uint32_t k = 0; k < 3; k++) Put(e - 0x18 + 4 * k, light[k]);
  if (int32_t(verts) > 0) {
    const F32 away[3] = {Fst(St(light[0]) * 512.0f), Fst(St(light[1]) * 512.0f),
                         Fst(St(light[2]) * 512.0f)};
    for (uint32_t k = 0; k < 3; k++) Put(e - 0x3C + 4 * k, away[k]);
    uint32_t from = kTessXyz, to = kTessXyz + 4 * (U32(kXyzStride) * verts);
    for (uint32_t i = 0; int32_t(i) < I32(kTessVerts); i++) {
      for (uint32_t k = 0; k < 3; k++) Put(to + 4 * k, Fst(St(Ld(from + 4 * k)) - away[k]));
      from += 4 * U32(kXyzStride);
      to += 4 * U32(kXyzStride);
    }
  }
  for (uint32_t i = 0, n = U32(kTessVerts); i < n; i++) Put(kEdgeCounts + 4 * i, 0);
  auto edge = [](uint32_t a, uint32_t b, uint32_t t) {
    const uint32_t n = U32(kEdgeCounts + 4 * a);
    if (n == 0x20) return;
    const uint32_t facing = U32(kFacing + 4 * t), at = kEdges + 8 * (32 * a + n);
    Put(at, b);
    Put(at + 4, facing);
    Put(kEdgeCounts + 4 * a, n + 1);
  };
  const int32_t tris = I32(kTessIndexCount) / 3;
  Put(e - 0x38, uint32_t(tris));
  for (int32_t t = 0; t < tris; t++) {
    const uint32_t ix = kTessIndexes + 6 * uint32_t(t);
    const uint32_t i1 = U16(ix), i2 = U16(ix + 2), i3 = U16(ix + 4);
    const uint32_t stride = U32(kXyzStride);
    const uint32_t v1 = kTessXyz + 4 * (stride * i1), v2 = kTessXyz + 4 * (stride * i2),
                   v3 = kTessXyz + 4 * (stride * i3);
    F32 d1[3], d2[3];
    for (uint32_t k = 0; k < 3; k++) d1[k] = Fst(St(Ld(v2 + 4 * k)) - Ld(v1 + 4 * k));
    for (uint32_t k = 0; k < 3; k++) d2[k] = Fst(St(Ld(v3 + 4 * k)) - Ld(v1 + 4 * k));
    const F32 n0 = Fst(St(d2[2]) * d1[1] - St(d2[1]) * d1[2]);
    const F32 n1 = Fst(St(d2[0]) * d1[2] - St(d2[2]) * d1[0]);
    const F32 n2 = Fst(St(d2[1]) * d1[0] - St(d2[0]) * d1[1]);
    const F32 d = Fst(St(n2) * light[2] + St(n1) * light[1] + St(n0) * light[0]);
    for (uint32_t k = 0; k < 3; k++) {
      Put(e - 0x24 + 4 * k, d1[k]);
      Put(e - 0x30 + 4 * k, d2[k]);
    }
    Put(e - 0xC, n0);
    Put(e - 8, n1);
    Put(e - 4, n2);
    Put(e - 0x34, d);
    Put(kFacing + 4 * uint32_t(t), d > 0.0f ? 1 : 0);
    edge(i1, i2, uint32_t(t));
    edge(i2, i3, uint32_t(t));
    edge(i3, i1, uint32_t(t));
  }
  if (tris > 0) Put(e - 0x3C, kTessIndexes + 2 + 6 * uint32_t(tris));
  c.esp = e - 0x48;
  c.esi = U32(0x1535AC4);
  Call(c, f_004CB3B0, 0x50858F);
  c.eax = 0x12;
  Call(c, f_004CB710, 0x508599);
  for (int k = 0; k < 3; k++) Push(c, 0x3E4CCCCD);  // 0.2
  CallPtr(c, kglColor3f, 0x5085A8);
  for (int k = 0; k < 4; k++) Push(c, 0);
  CallPtr(c, kglColorMask, 0x5085B6);
  Enable(c, 0xB90, 0x5085C1);
  Push(c, 0xFF);
  Push(c, 1);
  Push(c, 0x207);
  CallPtr(c, kglStencilFunc, 0x5085D3);
  c.esi = esi;
  c.esp = e - 0x3C;
  if (const uint32_t cull = U32(kCullType); cull != 1) {
    if (cull == 2) Enable(c, 0xB44, 0x5085F0);
    Push(c, U32(0x1549930) ? 0x404 : 0x405);
    CallPtr(c, kglCullFace, 0x50860B);
    Put(kCullType, 1);
  }
  Push(c, 0x1E02);
  Push(c, 0x1E00);
  Push(c, 0x1E00);
  CallPtr(c, kglStencilOp, 0x50862A);
  Call(c, f_00507F40, 0x508635);
  if (const uint32_t cull = U32(kCullType)) {
    if (cull == 2) Enable(c, 0xB44, 0x508648);
    Push(c, U32(0x1549930) ? 0x405 : 0x404);
    CallPtr(c, kglCullFace, 0x508663);
    Put(kCullType, 0);
  }
  Push(c, 0x1E03);
  Push(c, 0x1E00);
  Push(c, 0x1E00);
  CallPtr(c, kglStencilOp, 0x508682);
  Call(c, f_00507F40, 0x50868D);
  for (int k = 0; k < 4; k++) Push(c, 1);
  CallPtr(c, kglColorMask, 0x508695);
  c.esp = e + 4;
}

// ---- the optimised iterators --------------------------------------------------

namespace {

// A register of the original's (member of Ctx).
using Reg = uint32_t Ctx::*;
// Where the original leaves the new index and draw counts of a draw, by
// the form it takes (EAX for a scratch register).
struct DrawRegs {
  Reg range_indexes, range_draws, indexes, draws;
};

// The optimised batch's triangles (0x1830C94 indexes at 0x17F0C94): with
// glDrawRangeElementsEXT over the vertices 0x17F0C8C..0x17F0C90 when it
// is there and the range is set, else glDrawElements; counting the indexes
// and the draws (0x1549B14, 0x1549B18).
void DrawOptimised(Ctx& c, uint32_t range_va, uint32_t elements_va, const DrawRegs& r) {
  const uint32_t n = U32(0x1830C94), indexes = U32(0x1549B14) + n,
                 draws = U32(0x1549B18) + 1, range = U32(kglDrawRangeElementsEXT);
  Put(0x1549B14, indexes);
  Put(0x1549B18, draws);
  if (range && U32(0x17F0C90)) {
    c.*r.range_indexes = indexes;
    c.*r.range_draws = draws;
    Push(c, 0x17F0C94);
    Push(c, 0x1403);
    Push(c, n);
    Push(c, U32(0x17F0C90));
    Push(c, U32(0x17F0C8C));
    Push(c, 4);
    CallTo(c, range, range_va);
  } else {
    c.*r.indexes = indexes;
    c.*r.draws = draws;
    Push(c, 0x17F0C94);
    Push(c, 0x1403);
    Push(c, n);
    Push(c, 4);
    CallPtr(c, kglDrawElements, elements_va);
  }
}

// An optimised iterator: its form and its call sites (the addresses of the
// call instructions; 0 where it has none).
struct OptimisedIterator {
  // The vertices: in memory, in a vertex buffer (bound around the draws),
  // or in an ATI vertex array object.
  enum { kMemory, kBuffer, kObject } source;
  // The world's vertices (0x154955C, the object's offset 0x1549560:
  // 24 bytes each, one set of texture coordinates, then the colour and the
  // position) rather than the shader's (+0x18C, +0x190: 32 bytes, two
  // sets); the world's have no texture environment of their own and,
  // but for the array object's, set up the fog and the cull first.
  bool world;
  struct {
    uint32_t format, va, print;  // the log line (0: none)
  } log;
  struct {
    uint32_t replace, mode;  // glTexEnvi, f_004CB640
  } env;
  struct {
    uint32_t fog, cull;  // f_004F5D50, f_004CB5D0
  } setup;
  struct {
    uint32_t bind, colours_on, normals_off, colours, positions;
  } arrays;
  struct {
    uint32_t state, units, range, elements;
  } stage;
  // The triangles drawn over the surface for the cvar 0x15343E8.
  struct {
    uint32_t depth, cull, state, units, colours_off, colour, colour4ubv, colour3f,
        range, elements, depth_back;
    DrawRegs regs;
  } tris;
  uint32_t unbind, print;  // the vertex buffer unbound; the log's end
};

// The draws of the stage loop and (most often) of the triangles.
constexpr DrawRegs kStageDraw = {&Ctx::esi, &Ctx::esi, &Ctx::eax, &Ctx::eax},
                   kTrisDraw = {&Ctx::esi, &Ctx::edi, &Ctx::eax, &Ctx::edi};

// The optimised iterators (f_004F7C30): the log line (cvar 0x1534704;
// the shader's iterators by vertex buffer and in memory, as
// "RB_SurfaceOptimizedARB" and "RB_SurfaceOptimizedNV"); the texture
// environment (GL_REPLACE for the cvar 0x15343B4, else the shader's first
// stage's +0x15C); the colour array on and the normals off, the colours
// and positions from the vertices; the fog and the cull; then each stage
// but those with the sign bit of their first byte: its state bits, its
// textures (f_004F3D80, or f_004F3F00 from the array object) with the
// texture coordinates from the vertices, and the batch's triangles. With
// the cvar 0x15343E8 the triangles again in the shader 0x1535B5C's first
// stage (bit 1: in front, at a depth range of 0 to 0.1f), coloured by
// f_004F2E60 from 5 up, else (0, tr.identityLight, 0) or, for the world's,
// tr.identityLight with red halved; "----------" to the log file.
void Optimised(Ctx& c, const OptimisedIterator& it) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi;
  const uint32_t shader = U32(kTessShader);
  const bool object = it.source == OptimisedIterator::kObject;
  const uint32_t stride = it.world ? 0x18 : 0x20, colours = it.world ? 8 : 0x10,
                 positions = it.world ? 0xC : 0x14;
  const uint32_t vertices = it.world ? U32(0x154955C) : U32(shader + 0x18C);
  // The vertices' address in what the arrays point into: memory, the
  // buffer (0) or the object (EBX, the offset in EDI).
  uint32_t base = it.source == OptimisedIterator::kMemory ? vertices : 0;
  c.esp = e - 0x34;
  if (object) {
    c.ebx = vertices;
    c.edi = base = it.world ? U32(0x1549560) : U32(shader + 0x190);
  }
  if (it.log.format && CvarInt(0x1534704)) {
    Push(c, shader);
    Push(c, it.log.format);
    Call(c, f_00440940, it.log.va + 5);
    c.esp += 8;
    if (U32(0x1830CE4)) {
      Push(c, c.eax);
      Push(c, 0x55B2B4);  // "%s"
      Push(c, U32(0x1830CE4));
      Call(c, f_0051D45A, it.log.print + 5);
      c.esp += 0xC;
    }
  }
  if (!it.world) {
    if (CvarInt(0x15343B4)) {
      const uint32_t env = kTexEnv + 4 * U32(kUnit);
      if (U32(env) != 0x1E01) {
        Push(c, 0x1E01);
        Push(c, 0x2200);
        Push(c, 0x2300);
        Put(env, 0x1E01);
        CallPtr(c, kglTexEnvi, it.env.replace);
      }
    } else {
      c.eax = U32(U32(shader + 0x154) + 0x15C);
      Call(c, f_004CB640, it.env.mode + 5);
    }
  }
  auto setup = [&] {
    Call(c, f_004F5D50, it.setup.fog + 5);
    c.esi = U32(U32(kTessShader) + 0xA8);
    Call(c, f_004CB5D0, it.setup.cull + 5);
  };
  const bool setup_first = it.world && !object;
  if (setup_first) setup();
  if (it.source == OptimisedIterator::kBuffer) {
    Push(c, vertices);
    Push(c, 0x8892);
    CallPtr(c, kglBindBufferARB, it.arrays.bind);
  }
  if (it.world && object) c.esi = 0x100;  // the colour array's bit
  if (!(U32(kArrayBits) & 0x100)) {
    Push(c, 0x8076);
    CallPtr(c, kglEnableClientState, it.arrays.colours_on);
    Put(kArrayBits, U32(kArrayBits) | 0x100);
  }
  if (U32(kArrayBits) & 0x200) {
    Push(c, 0x8075);
    CallPtr(c, kglDisableClientState, it.arrays.normals_off);
    Put(kArrayBits, U32(kArrayBits) & ~0x200u);
  }
  if (object) {
    Push(c, base + colours);
    Push(c, vertices);
    Push(c, stride);
    Push(c, 0x1401);
    Push(c, 4);
    Push(c, 0x8076);
    CallPtr(c, kglArrayObjectATI, it.arrays.colours);
    Push(c, base + positions);
    Push(c, vertices);
    Push(c, stride);
    Push(c, 0x1406);
    Push(c, 3);
    Push(c, 0x8074);
    CallPtr(c, kglArrayObjectATI, it.arrays.positions);
  } else {
    Push(c, base + colours);
    Push(c, stride);
    Push(c, 0x1401);
    Push(c, 4);
    CallPtr(c, kglColorPointer, it.arrays.colours);
    Push(c, base + positions);
    Push(c, stride);
    Push(c, 0x1406);
    Push(c, 3);
    CallPtr(c, kglVertexPointer, it.arrays.positions);
  }
  if (!setup_first) setup();
  // The texture coordinates per unit (the world's only the first).
  const uint32_t coords = e - 0x24;
  Put(coords, base);
  if (!it.world) Put(coords + 4, base + 8);
  auto units = [&](uint32_t stage, uint32_t va) {
    Push(c, stride);
    Push(c, coords);
    if (object) Push(c, vertices);
    Push(c, stage);
    Call(c, object ? f_004F3F00 : f_004F3D80, va + 5);
    c.esp += object ? 0x10 : 0xC;
  };
  c.edi = 0;
  if (I32(kNumStages) > 0) {
    do {
      c.esi = U32(U32(kTessStages) + 4 * c.edi);
      if (int8_t(U8(c.esi)) >= 0) {
        c.eax = U32(c.esi + 0x684);
        Call(c, f_004CB710, it.stage.state + 5);
        units(c.esi, it.stage.units);
        DrawOptimised(c, it.stage.range, it.stage.elements, kStageDraw);
      }
    } while (int32_t(++c.edi) < I32(kNumStages));
  }
  if (const uint32_t tris = CvarInt(0x15343E8)) {
    if (tris & 1) {
      Push(c, 0x3FB99999);  // 0.1f as a double
      Push(c, 0xA0000000);
      Push(c, 0);
      Push(c, 0);
      CallPtr(c, kglDepthRange, it.tris.depth);
    }
    if (it.tris.cull) {
      c.esi = U32(U32(0x1535B5C) + 0xA8);
      Call(c, f_004CB5D0, it.tris.cull + 5);
    }
    c.esi = U32(U32(0x1535B5C) + 0x154);
    c.eax = U32(c.esi + 0x684);
    Call(c, f_004CB710, it.tris.state + 5);
    units(c.esi, it.tris.units);
    if (it.tris.colours_off && (U32(kArrayBits) & 0x100)) {
      Push(c, 0x8076);
      CallPtr(c, kglDisableClientState, it.tris.colours_off);
      Put(kArrayBits, U32(kArrayBits) & ~0x100u);
    }
    if (I32(U32(0x15343E8) + 0x20) >= 5) {
      const uint32_t colour = e - 0x28;
      c.eax = U32(0x1830C94);
      c.edi = colour;
      Call(c, f_004F2E60, it.tris.colour + 5);
      Push(c, colour);
      CallPtr(c, kglColor4ubv, it.tris.colour4ubv);
    } else if (it.world) {
      Push(c, U32(kIdentityLight));
      Push(c, U32(kIdentityLight));
      Push(c, Fst(St(Ld(kIdentityLight)) * Ld(0x55B590)).bits);  // 0.5
      CallPtr(c, kglColor3f, it.tris.colour3f);
    } else {
      Push(c, 0);
      Push(c, U32(kIdentityLight));
      Push(c, 0);
      CallPtr(c, kglColor3f, it.tris.colour3f);
    }
    DrawOptimised(c, it.tris.range, it.tris.elements, it.tris.regs);
    Push(c, 0x3FF00000);  // 1.0
    Push(c, 0);
    Push(c, 0);
    Push(c, 0);
    CallPtr(c, kglDepthRange, it.tris.depth_back);
  }
  if (it.source == OptimisedIterator::kBuffer) {
    Push(c, 0);
    Push(c, 0x8892);
    CallPtr(c, kglBindBufferARB, it.unbind);
  }
  if (it.print && U32(0x1830CE4)) {
    Push(c, 0x541D04);  // "----------\n"
    Push(c, 0x55B2B4);
    Push(c, U32(0x1830CE4));
    Call(c, f_0051D45A, it.print + 5);
    c.esp += 0xC;
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

}  // namespace

// The shader's vertices in memory.
void f_004F6380(Ctx& c) {
  static constexpr OptimisedIterator it = {
      .source = OptimisedIterator::kMemory,
      .env = {0x4F63B8, 0x4F63D2},
      .setup = {0x4F6452, 0x4F6463},
      .arrays = {0, 0x4F63E6, 0x4F6405, 0x4F642D, 0x4F644C},
      .stage = {0x4F64A7, 0x4F64B4, 0x4F6505, 0x4F6536},
      .tris = {0x4F656C, 0x4F657E, 0x4F6595, 0x4F65A2, 0x4F65B9, 0x4F65DE, 0x4F65E8, 0x4F65FA,
               0x4F6649, 0x4F667A, 0x4F668B, kTrisDraw},
      .print = 0x4F66A5};
  Optimised(c, it);
}
// The shader's vertices in a vertex buffer ("--- RB_SurfaceOptimizedARB( %s ) ---").
void f_004F66C0(Ctx& c) {
  static constexpr OptimisedIterator it = {
      .source = OptimisedIterator::kBuffer,
      .log = {0x53C01C, 0x4F66DD, 0x4F66F6},
      .env = {0x4F6732, 0x4F674B},
      .setup = {0x4F67C8, 0x4F67D9},
      .arrays = {0x4F6762, 0x4F6777, 0x4F6796, 0x4F67B1, 0x4F67C2},
      .stage = {0x4F6818, 0x4F6825, 0x4F6876, 0x4F68A7},
      .tris = {0x4F68DD, 0x4F68EF, 0x4F6906, 0x4F6913, 0x4F692A, 0x4F694F, 0x4F6959, 0x4F696B,
               0x4F69BA, 0x4F69EB, 0x4F69FC, kTrisDraw},
      .unbind = 0x4F6A09,
      .print = 0x4F6A23};
  Optimised(c, it);
}
// The shader's vertices in an ATI vertex array object.
void f_004F6A40(Ctx& c) {
  static constexpr OptimisedIterator it = {
      .source = OptimisedIterator::kObject,
      .env = {0x4F6A8B, 0x4F6A9F},
      .setup = {0x4F6B14, 0x4F6B25},
      .arrays = {0, 0x4F6AB3, 0x4F6AD2, 0x4F6AF5, 0x4F6B0E},
      .stage = {0x4F6B5B, 0x4F6B69, 0x4F6BBA, 0x4F6BEB},
      .tris = {0x4F6C21, 0x4F6C33, 0x4F6C4A, 0x4F6C58, 0x4F6C6F, 0x4F6C94, 0x4F6C9E, 0x4F6CB0,
               0x4F6CFF, 0x4F6D30, 0x4F6D41, {&Ctx::esi, &Ctx::ebx, &Ctx::ebx, &Ctx::edi}},
      .print = 0x4F6D5B};
  Optimised(c, it);
}
// The shader's vertices in memory ("--- RB_SurfaceOptimizedNV( %s ) ---").
void f_004F6D70(Ctx& c) {
  static constexpr OptimisedIterator it = {
      .source = OptimisedIterator::kMemory,
      .log = {0x53BFF4, 0x4F6D8D, 0x4F6DA6},
      .env = {0x4F6DE2, 0x4F6DFB},
      .setup = {0x4F6E7C, 0x4F6E8C},
      .arrays = {0, 0x4F6E0F, 0x4F6E2E, 0x4F6E57, 0x4F6E76},
      .stage = {0x4F6ED8, 0x4F6EE5, 0x4F6F36, 0x4F6F67},
      .tris = {0x4F6F9D, 0x4F6FAF, 0x4F6FC6, 0x4F6FD3, 0x4F6FEA, 0x4F700F, 0x4F7019, 0x4F702B,
               0x4F707A, 0x4F70AB, 0x4F70BC, kTrisDraw},
      .print = 0x4F70D6};
  Optimised(c, it);
}
// The world's vertices in memory (0x4F7950 is the same code).
void f_004F70F0(Ctx& c) {
  static constexpr OptimisedIterator it = {
      .source = OptimisedIterator::kMemory,
      .world = true,
      .setup = {0x4F70F5, 0x4F7105},
      .arrays = {0, 0x4F7119, 0x4F7138, 0x4F715B, 0x4F7174},
      .stage = {0x4F71AA, 0x4F71B7, 0x4F7207, 0x4F7238},
      .tris = {0x4F726F, 0x4F7281, 0x4F7297, 0x4F72A4, 0x4F72BB, 0x4F72E0, 0x4F72EA, 0x4F7309,
               0x4F7358, 0x4F7389, 0x4F739A, kTrisDraw},
      .print = 0x4F73B4};
  Optimised(c, it);
}
// The world's vertices in a vertex buffer.
void f_004F73D0(Ctx& c) {
  static constexpr OptimisedIterator it = {
      .source = OptimisedIterator::kBuffer,
      .world = true,
      .setup = {0x4F73D5, 0x4F73E5},
      .arrays = {0x4F73F6, 0x4F740B, 0x4F742A, 0x4F7445, 0x4F7456},
      .stage = {0x4F748B, 0x4F7498, 0x4F74E9, 0x4F751A},
      .tris = {0x4F7550, 0x4F7562, 0x4F7579, 0x4F7586, 0x4F759D, 0x4F75C2, 0x4F75CC, 0x4F75EB,
               0x4F7639, 0x4F766A, 0x4F767B, {&Ctx::edi, &Ctx::esi, &Ctx::eax, &Ctx::edi}},
      .unbind = 0x4F7688,
      .print = 0x4F76A2};
  Optimised(c, it);
}
// The world's vertices in an ATI vertex array object; its triangles keep
// the cull and the colour array, and it logs no end.
void f_004F76B0(Ctx& c) {
  static constexpr OptimisedIterator it = {
      .source = OptimisedIterator::kObject,
      .world = true,
      .setup = {0x4F7732, 0x4F7743},
      .arrays = {0, 0x4F76D5, 0x4F76F0, 0x4F7713, 0x4F772C},
      .stage = {0x4F7777, 0x4F7785, 0x4F77D6, 0x4F7807},
      .tris = {0x4F783D, 0, 0x4F7855, 0x4F7863, 0, 0x4F7881, 0x4F788B, 0x4F78AA, 0x4F78F8,
               0x4F7929, 0x4F793A, {&Ctx::ebx, &Ctx::edi, &Ctx::ebx, &Ctx::edi}}};
  Optimised(c, it);
}
// The world's vertices in memory, as f_004F70F0.
void f_004F7950(Ctx& c) {
  static constexpr OptimisedIterator it = {
      .source = OptimisedIterator::kMemory,
      .world = true,
      .setup = {0x4F7955, 0x4F7965},
      .arrays = {0, 0x4F7979, 0x4F7998, 0x4F79BB, 0x4F79D4},
      .stage = {0x4F7A0A, 0x4F7A17, 0x4F7A67, 0x4F7A98},
      .tris = {0x4F7ACF, 0x4F7AE1, 0x4F7AF7, 0x4F7B04, 0x4F7B1B, 0x4F7B40, 0x4F7B4A, 0x4F7B69,
               0x4F7BB8, 0x4F7BE9, 0x4F7BFA, kTrisDraw},
      .print = 0x4F7C14};
  Optimised(c, it);
}

// The optimised stage iterator of the shader's kind (+0x188: 12-15, 25-28),
// on an 8-aligned frame; then its vertex and index counts (0x1830C98,
// 0x1830C94) into the frame's (0x1549B0C, 0x1549B10).
void f_004F7C30(Ctx& c) {
  const uint32_t e = c.esp, ebp = c.ebp;
  c.ebp = e - 4;
  c.esp = (e - 4) & ~7u;
  switch (U32(U32(kTessShader) + 0x188)) {
    case 12: Call(c, f_004F70F0, 0x4F7C78); break;
    case 13: Call(c, f_004F73D0, 0x4F7C7F); break;
    case 14: Call(c, f_004F76B0, 0x4F7C86); break;
    case 15: Call(c, f_004F7950, 0x4F7C8D); break;
    case 25: Call(c, f_004F6380, 0x4F7C5C); break;
    case 26: Call(c, f_004F66C0, 0x4F7C63); break;
    case 27: Call(c, f_004F6A40, 0x4F7C6A); break;
    case 28: Call(c, f_004F6D70, 0x4F7C71); break;
  }
  Put(0x1549B10, U32(0x1549B10) + U32(0x1830C94));
  Put(0x1549B0C, U32(0x1549B0C) + U32(0x1830C98));
  Put(0x1830C94, 0);
  Put(0x1830C98, 0);
  c.ebp = ebp;
  c.esp = e + 4;
}

// RB_EndSurface (its overflow errors): the optimised iterator's
// batch first if it has one; then, for a surface with indexes, the overflow
// checks (the last index and the last vertex's x must be untouched), the
// shader 0x1535AEC by its own flush; a shader is skipped for the cvar
// 0x153468C below its sort, and by the view's portal state (0x154D734:
// the refdef's flag 8 and 0x154D708) against the stage iterator 0x4DD6E0;
// otherwise the counters (surfaces 0x1549B08; for a shader with stages
// vertices and indexes), the shader's stage iterator (0x1830CA0; dlights
// only after the optimised one), the debug lines (cvar 0x15343E8) and the
// normals (r_shownormals, 0x1534698: "white", "color", or anything else
// reset with its usage printed); the index count back to zero, and
// "----------" to the log file.
void f_004F7D00(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi;
  c.esp = e - 0xC;
  c.ebx = 0;
  c.esi = 0;
  auto done = [&] {
    c.ebx = ebx;
    c.esi = esi;
    c.esp = e + 4;
  };
  if (U32(0x1830C94)) {
    Call(c, f_004F7C30, 0x4F7D15);
    c.esi = 1;
  }
  if (!U32(kTessIndexCount)) return done();
  if (U16(0x1630C3E)) {
    Push(c, 0x53BFC8);
    Push(c, 1);
    CallPtr(c, kError, 0x4F7D36);
    c.esp += 8;
  }
  if (!(St(Ld(kTessXyz + U32(kXyzStride) * 0x7FFC)) == 0.0f)) {
    Push(c, 0x53BF9C);
    Push(c, 1);
    CallPtr(c, kError, 0x4F7D64);
    c.esp += 8;
  }
  const uint32_t shader = U32(kTessShader);
  auto skip = [&] {
    Put(kTessIndexCount, 0);
    done();
  };
  if (shader == U32(0x1535AEC)) {
    Call(c, f_005082D0, 0x4F7D80);
    return skip();
  }
  const uint32_t sort = CvarInt(0x153468C);
  if (sort && FromInt(int32_t(sort)) < Ld(shader + 0x58)) return skip();
  const uint32_t iterator = U32(0x1830CA0);
  if (U32(0x154D734)) {
    if (!(U8(0x15496EC) & 8)) {
      if (iterator == 0x4DD6E0) return skip();
    } else if (!U32(0x154D708) && iterator != 0x4DD6E0) {
      return skip();
    }
  }
  Put(0x1549B08, U32(0x1549B08) + 1);
  if (U32(kNumStages)) {
    Put(0x1549B0C, U32(0x1549B0C) + U32(kTessVerts));
    Put(0x1549B10, U32(0x1549B10) + U32(kTessIndexCount));
  }
  Push(c, c.esi);
  CallTo(c, iterator, 0x4F7E1F);
  c.esp += 4;
  if (CvarInt(0x15343E8) && U32(kTessIndexCount)) {
    c.eax = kTessIndexes;
    Call(c, f_004F44A0, 0x4F7E42);
  }
  if (U8(U32(U32(0x1534698) + 4))) {
    Push(c, 0x559E60);
    Push(c, U32(U32(0x1534698) + 4));
    Call(c, f_0052AB6F, 0x4F7E5A);
    c.esp += 8;
    if (!c.eax) {
      Push(c, kTessIndexes);
      Call(c, f_004F4560, 0x4F7E6B);
      c.esp += 4;
    } else {
      Push(c, 0x53BF94);
      Push(c, U32(U32(0x1534698) + 4));
      Call(c, f_0052AB6F, 0x4F7E81);
      c.esp += 8;
      if (!c.eax) {
        c.ebx = kTessIndexes;
        Call(c, f_004F4850, 0x4F7E92);
        c.ebx = 0;
      } else {
        Push(c, 0x54ED00);
        Push(c, 0x54D8D8);
        c.eax = 1;
        Call(c, f_00431CF0, 0x4F7EAA);
        c.esp += 8;
        Push(c, 0x53BF68);
        Call(c, f_0042E300, 0x4F7EB7);
        c.esp += 4;
      }
    }
  }
  Put(kTessIndexCount, 0);
  if (const uint32_t log = U32(0x1830CE4)) {
    Push(c, 0x541D04);
    Push(c, 0x55B2B4);
    Push(c, log);
    Call(c, f_0051D45A, 0x4F7ED9);
    c.esp += 0xC;
  }
  done();
}

// ---- the static model surfaces ---------------------------------------------------

namespace {

// A static model surface (the draw surface list's argument): +0 its type,
// +0x10 its index count and +0x12 its vertex count (16 bits), +0x14 its
// texture coordinates, +0x18 normals, +0x1C positions and +0x20 indexes;
// +0x24 its vertices (in memory, a vertex buffer or an ATI array object),
// +0x28 the index buffer or the object's offset, +0x2C the indexes' offset
// in the object.
constexpr uint32_t kglDrawElementArrayATI = 0x1534AE0;
// GL_ATI_element_array is in use.
constexpr uint32_t kElementArray = 0x15347BC;

// The log line of cvar 0x1534704 (va of the format and the shader, then
// "%s" to the log file), at the original's ESP; va at va_at, fprintf at
// print_at.
void LogShader(Ctx& c, uint32_t format, uint32_t va_at, uint32_t print_at) {
  if (!CvarInt(0x1534704)) return;
  Push(c, U32(kTessShader));
  Push(c, format);
  Call(c, f_00440940, va_at + 5);
  c.esp += 8;
  if (const uint32_t log = U32(0x1830CE4)) {
    Push(c, c.eax);
    Push(c, 0x55B2B4);  // "%s"
    Push(c, log);
    Call(c, f_0051D45A, print_at + 5);
    c.esp += 0xC;
  }
}
// A fixed line to the log file, if there is one.
void LogLine(Ctx& c, uint32_t line, uint32_t va) {
  if (const uint32_t log = U32(0x1830CE4)) {
    Push(c, line);
    Push(c, 0x55B2B4);
    Push(c, log);
    Call(c, f_0051D45A, va + 5);
    c.esp += 0xC;
  }
}

// A stage's constant colour for a static model: for rgbGen 11 white
// (tr.identityLight) with the cvar 0x15346C4, else the entity's colour; for
// any other its constant +0x680.
void StaticColour(Ctx& c, uint32_t stage, uint32_t colour3f, uint32_t colour4ubv) {
  if (U32(stage + 0x664) == 0xB) {
    if (CvarInt(0x15346C4)) {
      Push(c, U32(kIdentityLight));
      Push(c, U32(kIdentityLight));
      Push(c, U32(kIdentityLight));
      CallPtr(c, kglColor3f, colour3f);
      return;
    }
    Push(c, U32(kEntity) + 0x6C);
  } else {
    Push(c, stage + 0x680);
  }
  CallPtr(c, kglColor4ubv, colour4ubv);
}

// The surface's indexes counted (0x1549B14, 0x1549B18) and drawn; the
// original leaves the draw count in EDI.
void StaticDraw(Ctx& c, uint32_t surf, uint32_t indexes, uint32_t va) {
  const uint32_t n = U16(surf + 0x10);
  Put(0x1549B14, U32(0x1549B14) + n);
  c.edi = U32(0x1549B18) + 1;
  Put(0x1549B18, c.edi);
  Push(c, indexes);
  Push(c, 0x1403);
  Push(c, n);
  Push(c, 4);
  CallPtr(c, kglDrawElements, va);
}

// A static model surface drawn from its own vertices: in memory or in a
// vertex buffer (with its index buffer), 20 bytes a vertex (texture
// coordinates and position) or 32 (with the normal between).
struct StaticModel {
  bool buffer, normals;
  uint32_t log, log_at;  // the line and its fprintf
  uint32_t fog, cull, arrays, bind[2], normal_ptr, vertex_ptr;
  // Each stage: its colour (20-byte vertices only: in memory before its
  // state, in a buffer after its textures), its state bits, its textures,
  // the draw.
  struct {
    uint32_t colour3f, colour4ubv, state, units, draw;
  } stage;
  // The triangles drawn over it for the cvar 0x15343E8: the shader
  // 0x1535B5C's first stage's textures (20-byte vertices in memory) or its
  // first bundle's image bound.
  struct {
    uint32_t depth, state, units, image, bind, colour, colour4ubv, colour3f, draw, depth_back;
  } tris;
  uint32_t unbind[2], print;
};

// The static model surfaces (log lines "--- RB_SurfaceStaticModelT2V3_ARB
// ---" and the like: T2 the texture coordinates, N3 the normal, V3 the
// position): the line to the log file; the fog, the cull and the client
// arrays (colours off for the normals'); the vertex buffer and the index
// buffer bound; the normals and positions; then each stage of the batch
// (0x1830C9C of 0x1830CA4, none skipped): its colour (for 20-byte
// vertices, StaticColour), its state bits, its textures from the vertices
// (f_004F3D80) and the surface's triangles. With the cvar 0x15343E8 they
// are drawn again in the shader 0x1535B5C's first stage (bit 1: at a depth
// range of 0 to 0.1f), coloured by f_004F2E60 from 5 up, else
// tr.identityLight with red and green x 0.25 (their float left in the
// argument slot) or, with that stage's textures, red x 0.5 and green x
// 0.75; then the buffers unbound, "----------" to the log file and the
// surface's indexes and vertices counted (0x1549B10, 0x1549B0C).
void DrawStaticModel(Ctx& c, const StaticModel& m) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi;
  c.esp = e - 0x20;
  LogLine(c, m.log, m.log_at);
  c.esp = e - 0x2C;
  Call(c, f_004F5D50, m.fog + 5);
  c.esi = U32(U32(kTessShader) + 0xA8);
  Call(c, f_004CB5D0, m.cull + 5);
  Push(c, m.normals ? 0x601 : 0x401);
  Call(c, f_004CBBE0, m.arrays + 5);
  c.esp += 4;
  const uint32_t surf = Arg(c, e, 1), stride = m.normals ? 0x20 : 0x14;
  c.ebx = surf;
  if (m.buffer) {
    Push(c, U32(surf + 0x24));
    Push(c, 0x8892);
    CallPtr(c, kglBindBufferARB, m.bind[0]);
    Push(c, U32(surf + 0x28));
    Push(c, 0x8893);
    CallPtr(c, kglBindBufferARB, m.bind[1]);
  }
  const uint32_t base = m.buffer ? 0 : U32(surf + 0x24);
  if (m.normals) {
    Push(c, base + 8);
    Push(c, stride);
    Push(c, 0x1406);
    CallPtr(c, kglNormalPointer, m.normal_ptr);
  }
  Push(c, base + (m.normals ? 0x14 : 8));
  Push(c, stride);
  Push(c, 0x1406);
  Push(c, 3);
  CallPtr(c, kglVertexPointer, m.vertex_ptr);
  const uint32_t coords = e - 0x20, indexes = m.buffer ? 0 : U32(surf + 0x20);
  Put(coords, base);
  auto units = [&](uint32_t stage, uint32_t va) {
    Push(c, stride);
    Push(c, coords);
    Push(c, stage);
    Call(c, f_004F3D80, va + 5);
    c.esp += 0xC;
  };
  c.esi = 0;
  if (I32(kNumStages) > 0) {
    do {
      if (!m.normals && !m.buffer)
        StaticColour(c, U32(U32(kTessStages) + 4 * c.esi), m.stage.colour3f, m.stage.colour4ubv);
      c.edi = U32(U32(kTessStages) + 4 * c.esi);
      c.eax = U32(c.edi + 0x684);
      Call(c, f_004CB710, m.stage.state + 5);
      units(c.edi, m.stage.units);
      if (!m.normals && m.buffer)
        StaticColour(c, U32(U32(kTessStages) + 4 * c.esi), m.stage.colour3f, m.stage.colour4ubv);
      StaticDraw(c, surf, indexes, m.stage.draw);
    } while (int32_t(++c.esi) < I32(kNumStages));
  }
  if (const uint32_t tris = CvarInt(0x15343E8)) {
    if (tris & 1) {
      Push(c, 0x3FB99999);  // 0.1f as a double
      Push(c, 0xA0000000);
      Push(c, 0);
      Push(c, 0);
      CallPtr(c, kglDepthRange, m.tris.depth);
    }
    c.eax = U32(U32(U32(0x1535B5C) + 0x154) + 0x684);
    Call(c, f_004CB710, m.tris.state + 5);
    c.esi = U32(U32(0x1535B5C) + 0x154);
    if (m.tris.units) {
      units(c.esi, m.tris.units);
    } else {
      c.esi += 4;
      Push(c, U32(kUnit));
      Call(c, f_004F3330, m.tris.image + 5);
      c.esp += 4;
      c.esi = c.eax;
      Call(c, f_004CB3B0, m.tris.bind + 5);
    }
    if (I32(U32(0x15343E8) + 0x20) >= 5) {
      c.eax = U16(surf + 0x10);
      c.edi = e + 4;
      Call(c, f_004F2E60, m.tris.colour + 5);
      Push(c, e + 4);
      CallPtr(c, kglColor4ubv, m.tris.colour4ubv);
    } else if (m.tris.units) {
      Push(c, U32(kIdentityLight));
      Push(c, Fst(St(Ld(kIdentityLight)) * Ld(0x55B834)).bits);  // 0.75
      Push(c, Fst(St(Ld(kIdentityLight)) * Ld(0x55B590)).bits);  // 0.5
      CallPtr(c, kglColor3f, m.tris.colour3f);
    } else {
      Push(c, U32(kIdentityLight));
      const F32 quarter = Fst(St(Ld(kIdentityLight)) * Ld(0x55B5EC));  // 0.25
      Put(e + 4, quarter);
      Push(c, quarter.bits);
      Push(c, quarter.bits);
      CallPtr(c, kglColor3f, m.tris.colour3f);
    }
    StaticDraw(c, surf, indexes, m.tris.draw);
    Push(c, 0x3FF00000);  // 1.0
    Push(c, 0);
    Push(c, 0);
    Push(c, 0);
    CallPtr(c, kglDepthRange, m.tris.depth_back);
  }
  if (m.buffer) {
    Push(c, 0);
    Push(c, 0x8892);
    CallPtr(c, kglBindBufferARB, m.unbind[0]);
    Push(c, 0);
    Push(c, 0x8893);
    CallPtr(c, kglBindBufferARB, m.unbind[1]);
  }
  LogLine(c, 0x541D04, m.print);  // "----------\n"
  Put(0x1549B10, U32(0x1549B10) + U16(surf + 0x10));
  Put(0x1549B0C, U32(0x1549B0C) + U16(surf + 0x12));
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

}  // namespace

// "--- RB_SurfaceStaticModel( %s ) ---": the surface into the tessellator
// (stack: surface): its positions (12 bytes a vertex, at the tessellator's
// position stride), texture coordinates and, for a shader with +0x50 bit
// 0x102, normals (then 0x17F0C64 set), copied by the memcpy 0x42B0A0; its
// indexes plus the vertex count so far (16 bits); then both counts.
void f_004FAAB0(Ctx& c) {
  const uint32_t e = c.esp;
  LogShader(c, 0x53BCB0, 0x4FAAC8, 0x4FAAE1);
  c.esp = e - 4;
  const uint32_t surf = Arg(c, e, 1), verts = U32(kTessVerts);
  auto copy = [&](uint32_t dst, uint32_t src, uint32_t n, uint32_t va) {
    Push(c, n);
    Push(c, src);
    Push(c, dst);
    Call(c, f_0042B0A0, va + 5);
  };
  copy(kTessXyz + 4 * U32(kXyzStride) * verts, U32(surf + 0x1C), 12 * U16(surf + 0x12), 0x4FAB12);
  copy(kTessTexCoords + 8 * U32(kTessVerts), U32(surf + 0x14), 8 * U16(surf + 0x12), 0x4FAB31);
  c.esp = e - 4;
  if (U32(U32(kTessShader) + 0x50) & 0x102) {
    copy(kTessNormals + 12 * U32(kTessVerts), U32(surf + 0x18), 12 * U16(surf + 0x12), 0x4FAB68);
    c.esp = e - 4;
    Put(0x17F0C64, 1);
  }
  for (uint32_t i = 0; int32_t(i) < int32_t(U16(surf + 0x10)); i++)
    Wr<uint16_t>(g_guest_base, kTessIndexes + 2 * (U32(kTessIndexCount) + i),
                 uint16_t(U16(U32(surf + 0x20) + 2 * i) + U16(kTessVerts)));
  Put(kTessIndexCount, U32(kTessIndexCount) + U16(surf + 0x10));
  Put(kTessVerts, U32(kTessVerts) + U16(surf + 0x12));
  c.esp = e + 4;
}

// "--- RB_SurfaceStaticModelCached( %s ) ---" (stack: the draw surface: +4
// the model's surface, +8 its first vertex in the cached vertices): the
// batch's vertex range widened to it (set by the first), and its indexes
// plus that vertex added to the optimised batch, two to a dword (the low
// one's carry reaches the high one) and six at a time, as many sixes as
// it has (none: the original runs on for 2^32 of them).
void f_004FABD0(Ctx& c) {
  const uint32_t e = c.esp;
  LogShader(c, 0x53BC84, 0x4FABE8, 0x4FAC01);
  const uint32_t draw = Arg(c, e, 1), surf = U32(draw + 4), first = U32(draw + 8);
  const uint32_t end = U16(surf + 0x12) + first;
  if (!U32(0x17F0C90)) {
    Put(0x17F0C8C, first);
    Put(0x17F0C90, end);
  } else {
    if (I32(0x17F0C8C) > int32_t(first)) Put(0x17F0C8C, first);
    if (I32(0x17F0C90) < int32_t(end)) Put(0x17F0C90, end);
  }
  const uint32_t n = U16(surf + 0x10), add = (first << 16) | first;
  uint32_t dst = 0x17F0C94 + 2 * U32(0x1830C94), src = U32(surf + 0x20), sixes = n / 6;
  Put(0x1830C94, U32(0x1830C94) + n);
  do {
    for (int k = 0; k < 3; k++, dst += 4, src += 4) Put(dst, U32(src) + add);
  } while (--sixes);
  c.esp = e + 4;
}

// RB_SurfaceStaticModelT2V3_ARB: 20-byte vertices in a vertex buffer.
void f_004FACD0(Ctx& c) {
  static constexpr StaticModel m = {
      .buffer = true,
      .log = 0x53BC5C, .log_at = 0x4FACE7,
      .fog = 0x4FACF2, .cull = 0x4FAD02, .arrays = 0x4FAD0C,
      .bind = {0x4FAD21, 0x4FAD30}, .vertex_ptr = 0x4FAD41,
      .stage = {0x4FADAB, 0x4FADC5, 0x4FAD6E, 0x4FAD7B, 0x4FADF4},
      .tris = {0x4FAE2B, 0x4FAE42, 0, 0x4FAE5C, 0x4FAE66, 0x4FAE7F, 0x4FAE89, 0x4FAEAD, 0x4FAEDC,
               0x4FAEED},
      .unbind = {0x4FAEFA, 0x4FAF07}, .print = 0x4FAF21};
  DrawStaticModel(c, m);
}
// RB_SurfaceStaticModelT2N3V3_ARB: 32-byte vertices in a vertex buffer.
void f_004FAF60(Ctx& c) {
  static constexpr StaticModel m = {
      .buffer = true, .normals = true,
      .log = 0x53BC30, .log_at = 0x4FAF77,
      .fog = 0x4FAF82, .cull = 0x4FAF92, .arrays = 0x4FAF9C,
      .bind = {0x4FAFB1, 0x4FAFC0}, .normal_ptr = 0x4FAFCF, .vertex_ptr = 0x4FAFE0,
      .stage = {0, 0, 0x4FB00E, 0x4FB01B, 0x4FB04C},
      .tris = {0x4FB07F, 0x4FB096, 0, 0x4FB0B0, 0x4FB0BA, 0x4FB0D3, 0x4FB0DD, 0x4FB101, 0x4FB130,
               0x4FB141},
      .unbind = {0x4FB14E, 0x4FB15B}, .print = 0x4FB175};
  DrawStaticModel(c, m);
}
// RB_SurfaceStaticModelT2V3_NV: 20-byte vertices in memory (0x4FBAF0, the
// Generic one, is the same code).
void f_004FB660(Ctx& c) {
  static constexpr StaticModel m = {
      .log = 0x53BBE0, .log_at = 0x4FB677,
      .fog = 0x4FB682, .cull = 0x4FB692, .arrays = 0x4FB69C, .vertex_ptr = 0x4FB6B8,
      .stage = {0x4FB706, 0x4FB720, 0x4FB734, 0x4FB741, 0x4FB774},
      .tris = {0x4FB7AB, 0x4FB7C2, 0x4FB7CF, 0, 0, 0x4FB7ED, 0x4FB7F7, 0x4FB828, 0x4FB859, 0x4FB86A},
      .print = 0x4FB884};
  DrawStaticModel(c, m);
}
// RB_SurfaceStaticModelT2V3_Generic, as f_004FB660.
void f_004FBAF0(Ctx& c) {
  static constexpr StaticModel m = {
      .log = 0x53BB84, .log_at = 0x4FBB07,
      .fog = 0x4FBB12, .cull = 0x4FBB22, .arrays = 0x4FBB2C, .vertex_ptr = 0x4FBB48,
      .stage = {0x4FBB96, 0x4FBBB0, 0x4FBBC4, 0x4FBBD1, 0x4FBC04},
      .tris = {0x4FBC3B, 0x4FBC52, 0x4FBC5F, 0, 0, 0x4FBC7D, 0x4FBC87, 0x4FBCB8, 0x4FBCE9, 0x4FBCFA},
      .print = 0x4FBD14};
  DrawStaticModel(c, m);
}
// RB_SurfaceStaticModelT2N3V3_Generic: 32-byte vertices in memory (0x4FBD50,
// the NV one, is the same code).
void f_004FB8C0(Ctx& c) {
  static constexpr StaticModel m = {
      .normals = true,
      .log = 0x53BBB0, .log_at = 0x4FB8D7,
      .fog = 0x4FB8E2, .cull = 0x4FB8F2, .arrays = 0x4FB8FC, .normal_ptr = 0x4FB916,
      .vertex_ptr = 0x4FB92C,
      .stage = {0, 0, 0x4FB95F, 0x4FB96C, 0x4FB99F},
      .tris = {0x4FB9D1, 0x4FB9E9, 0, 0x4FBA03, 0x4FBA0D, 0x4FBA26, 0x4FBA30, 0x4FBA55, 0x4FBA86,
               0x4FBA97},
      .print = 0x4FBAB1};
  DrawStaticModel(c, m);
}
// RB_SurfaceStaticModelT2N3V3_NV, as f_004FB8C0.
void f_004FBD50(Ctx& c) {
  static constexpr StaticModel m = {
      .normals = true,
      .log = 0x53BB5C, .log_at = 0x4FBD67,
      .fog = 0x4FBD72, .cull = 0x4FBD82, .arrays = 0x4FBD8C, .normal_ptr = 0x4FBDA6,
      .vertex_ptr = 0x4FBDBC,
      .stage = {0, 0, 0x4FBDEF, 0x4FBDFC, 0x4FBE2F},
      .tris = {0x4FBE61, 0x4FBE79, 0, 0x4FBE93, 0x4FBE9D, 0x4FBEB6, 0x4FBEC0, 0x4FBEE5, 0x4FBF16,
               0x4FBF27},
      .print = 0x4FBF41};
  DrawStaticModel(c, m);
}

// "--- RB_SurfaceStaticModelATI( %s ) ---": a static model surface in an
// ATI vertex array object (+0x24, its offset +0x28), type 18 with 20-byte
// vertices, else 32 with normals. As DrawStaticModel, but: the log line by
// the cvar 0x1534704 (LogShader); the vertex stride left in the argument
// slot; the stages' textures from the object (f_004F3F00), the colours for
// type 18 only, before the state bits; and with GL_ATI_element_array the
// indexes from the object too (+0x2C; the element array on and off around
// the draws, by glDrawElementArrayATI), else from +0x20. Its triangles
// take that stage's textures, red x 0.5 and green x 0.75.
void f_004FB1B0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  c.esp = e - 0x20;
  LogShader(c, 0x53BC08, 0x4FB1CB, 0x4FB1E4);
  c.esp = e - 0x28;
  Call(c, f_004F5D50, 0x4FB1F3);
  c.esi = U32(U32(kTessShader) + 0xA8);
  Call(c, f_004CB5D0, 0x4FB204);
  Push(c, 0x401);
  Call(c, f_004CBBE0, 0x4FB20E);
  c.esp += 4;
  const uint32_t surf = Arg(c, e, 1), object = U32(surf + 0x24), offset = U32(surf + 0x28);
  const bool coloured = U32(surf) == 0x12;
  const uint32_t stride = coloured ? 0x14 : 0x20, coords = e - 0x20;
  c.esi = surf;
  Put(coords, offset);
  Put(e + 4, stride);
  auto array = [&](uint32_t type, uint32_t at, uint32_t va) {
    Push(c, offset + at);
    Push(c, object);
    Push(c, stride);
    Push(c, 0x1406);
    Push(c, 3);
    Push(c, type);
    CallPtr(c, kglArrayObjectATI, va);
  };
  if (!coloured) array(0x8075, 8, 0x4FB255);
  array(0x8074, coloured ? 8 : 0x14, 0x4FB274);
  c.esp = e - 0x30;
  const bool elements = U32(kElementArray);
  if (elements) {
    Push(c, 0x8768);
    CallPtr(c, kglEnableClientState, 0x4FB28E);
    Push(c, U32(surf + 0x2C));
    Push(c, object);
    Push(c, 0);
    Push(c, 0x1403);
    Push(c, 1);
    Push(c, 0x8768);
    CallPtr(c, kglArrayObjectATI, 0x4FB2AA);
  }
  // The call sites, with and without the element array.
  struct Sites {
    uint32_t colour3f, colour4ubv, state, units, draw;
    uint32_t depth, tris_state, tris_units, colour, tris_colour4ubv, tris_colour3f, tris_draw,
        depth_back;
  };
  static constexpr Sites kElements = {0x4FB2EB, 0x4FB305, 0x4FB31C, 0x4FB32D, 0x4FB357,
                                      0x4FB38D, 0x4FB3A8, 0x4FB3B9, 0x4FB3D7, 0x4FB3E1,
                                      0x4FB411, 0x4FB439, 0x4FB44A},
                         kIndexes = {0x4FB49C, 0x4FB4B4, 0x4FB4CC, 0x4FB4DD, 0x4FB510,
                                     0x4FB547, 0x4FB562, 0x4FB573, 0x4FB591, 0x4FB59B,
                                     0x4FB5CC, 0x4FB5FD, 0x4FB60E};
  const Sites& at = elements ? kElements : kIndexes;
  auto units = [&](uint32_t stage, uint32_t va) {
    Push(c, U32(e + 4));
    Push(c, coords);
    Push(c, object);
    Push(c, stage);
    Call(c, f_004F3F00, va + 5);
    c.esp += 0x10;
  };
  auto draw = [&](uint32_t va) {
    const uint32_t n = U16(surf + 0x10);
    Put(0x1549B14, U32(0x1549B14) + n);
    Put(0x1549B18, U32(0x1549B18) + 1);
    if (elements) {
      Push(c, n);
      Push(c, 4);
      CallPtr(c, kglDrawElementArrayATI, va);
    } else {
      Push(c, U32(surf + 0x20));
      Push(c, 0x1403);
      Push(c, n);
      Push(c, 4);
      CallPtr(c, kglDrawElements, va);
    }
  };
  c.edi = 0;
  if (I32(kNumStages) > 0) {
    do {
      if (U32(surf) == 0x12) StaticColour(c, U32(U32(kTessStages) + 4 * c.edi), at.colour3f, at.colour4ubv);
      const uint32_t stage = U32(U32(kTessStages) + 4 * c.edi);
      c.eax = U32(stage + 0x684);
      Call(c, f_004CB710, at.state + 5);
      units(stage, at.units);
      draw(at.draw);
    } while (int32_t(++c.edi) < I32(kNumStages));
  }
  if (const uint32_t tris = CvarInt(0x15343E8)) {
    if (tris & 1) {
      Push(c, 0x3FB99999);  // 0.1f as a double
      Push(c, 0xA0000000);
      Push(c, 0);
      Push(c, 0);
      CallPtr(c, kglDepthRange, at.depth);
    }
    c.edi = U32(U32(0x1535B5C) + 0x154);
    c.eax = U32(c.edi + 0x684);
    Call(c, f_004CB710, at.tris_state + 5);
    units(c.edi, at.tris_units);
    if (I32(U32(0x15343E8) + 0x20) >= 5) {
      c.eax = U16(surf + 0x10);
      c.edi = e + 4;
      Call(c, f_004F2E60, at.colour + 5);
      Push(c, e + 4);
      CallPtr(c, kglColor4ubv, at.tris_colour4ubv);
    } else {
      Push(c, U32(kIdentityLight));
      Push(c, Fst(St(Ld(kIdentityLight)) * Ld(0x55B834)).bits);  // 0.75
      Push(c, Fst(St(Ld(kIdentityLight)) * Ld(0x55B590)).bits);  // 0.5
      CallPtr(c, kglColor3f, at.tris_colour3f);
    }
    draw(at.tris_draw);
    if (!elements) c.edi = U32(0x1549B18);
    Push(c, 0x3FF00000);  // 1.0
    Push(c, 0);
    Push(c, 0);
    Push(c, 0);
    CallPtr(c, kglDepthRange, at.depth_back);
  }
  if (elements) {
    Push(c, 0x8768);
    CallPtr(c, kglDisableClientState, 0x4FB455);
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esp = e - 0x28;
  LogLine(c, 0x541D04, 0x4FB62A);  // "----------\n"
  Put(0x1549B10, U32(0x1549B10) + U16(surf + 0x10));
  Put(0x1549B0C, U32(0x1549B0C) + U16(surf + 0x12));
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// ---- the other surfaces ----------------------------------------------------------

namespace {

// The tessellator's second texture coordinates (8 bytes a vertex).
constexpr uint32_t kTessTexCoords2 = 0x1670C40;

void Put16(uint32_t va, uint32_t v) { Wr<uint16_t>(g_guest_base, va, uint16_t(v)); }

// The memcpy 0x42B0A0 (dst, src, size) from the call at va, its arguments
// left on the stack.
void Copy(Ctx& c, uint32_t dst, uint32_t src, uint32_t size, uint32_t va) {
  Push(c, size);
  Push(c, src);
  Push(c, dst);
  Call(c, f_0042B0A0, va + 5);
}

// RB_CheckOverflow (verts, indexes) when the tessellator cannot take them,
// as the surfaces' own tests have it (signed sums).
void CheckOverflow(Ctx& c, uint32_t verts, uint32_t indexes, uint32_t index_sum, uint32_t va) {
  if (int32_t(verts + U32(kTessVerts)) >= 0x2000 || int32_t(index_sum) >= 0xC000) {
    Push(c, indexes);
    Push(c, verts);
    Call(c, f_00500A60, va + 5);
    c.esp += 8;
  }
}

}  // namespace

// Surface type 0, "Bad surface tesselated.\n" through ri.Printf.
void f_00503A60(Ctx& c) {
  const uint32_t e = c.esp;
  Push(c, 0x539304);
  Push(c, 0);
  CallPtr(c, kPrintf, 0x503A67);
  c.esp = e + 4;
}

// Surface type 1: nothing.
void f_00503A80(Ctx& c) { c.esp += 4; }

// Surface type 2, a polygon (stack: surface; +8 its vertex count, +0xC its
// 32-byte vertices: position, texture coordinates, the second texture
// coordinates and colour) into the tessellator, positions at its position
// stride; for a shader with the sign bit of +0x54, the dlight bits
// (0x17F0C80) of the dlights (0x1536474 of them at 0x153647C, 0x88 bytes:
// radius +0x74, origin +0x78) within whose radius a vertex lies, the
// squared radius left in the argument slot (before that, the vertex loop's
// count); then a fan of triangles from its first vertex. The original
// rereads the surface's fields as it goes, and so does this.
void f_005016B0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const uint32_t surf = Arg(c, e, 1), n = U32(surf + 8);
  c.esi = surf;
  c.ebp = U32(kTessVerts);
  c.esp = e - 0xC;
  CheckOverflow(c, n, 3 * n - 6, U32(kTessIndexCount) + 3 * (n - 2), 0x5016E7);
  const uint32_t verts = U32(kTessVerts);
  uint32_t xyz = kTessXyz + 4 * (U32(kXyzStride) * verts), st = kTessTexCoords + 8 * verts,
           st2 = kTessTexCoords2 + 8 * verts, colour = kVertColors + 4 * verts;
  Put(e + 4, 0);
  if (I32(surf + 8) > 0) {
    uint32_t i = 0, at = 0;
    do {
      auto in = [&](uint32_t k) { return U32(U32(surf + 0xC) + at + k); };
      Put(xyz, in(0));
      Put(xyz + 4, in(4));
      Put(xyz + 8, in(8));
      Put(st, in(0xC));
      Put(st + 4, in(0x10));
      Put(st2, in(0x14));
      Put(st2 + 4, in(0x18));
      Put(colour, in(0x1C));
      xyz += 4 * U32(kXyzStride);
      st += 8;
      st2 += 8;
      colour += 4;
      at += 0x20;
      Put(e + 4, ++i);
    } while (int32_t(i) < I32(surf + 8));
  }
  if (I32(0x1536474) && int8_t(U8(U32(kTessShader) + 0x54)) < 0 && I32(0x1536474) > 0) {
    uint32_t i = 0, dl = U32(0x153647C) + 0x78;
    do {
      const uint32_t bit = 1u << (i & 31);
      if (!(U32(0x17F0C80) & bit)) {
        const St r = Ld(dl - 4);
        Put(e + 4, Fst(r * r));
        const uint32_t nv = U32(surf + 8);
        uint32_t v = U32(surf + 0xC);
        for (uint32_t k = 0; int32_t(k) < int32_t(nv); k++, v += 0x20) {
          const St dx = St(Ld(v)) - Ld(dl), dy = St(Ld(v + 4)) - Ld(dl + 4),
                   dz = St(Ld(v + 8)) - Ld(dl + 8);
          if (dz * dz + dy * dy + dx * dx < Ld(e + 4)) {
            Put(0x17F0C80, U32(0x17F0C80) | bit);
            break;
          }
        }
      }
      i++;
      dl += 0x88;
    } while (int32_t(i) < I32(0x1536474));
  }
  for (uint32_t i = 0; int32_t(i) < int32_t(U32(surf + 8) - 2); i++) {
    const uint32_t at = kTessIndexes + 2 * U32(kTessIndexCount);
    Put16(at, U32(kTessVerts));
    Put16(at + 2, i + U32(kTessVerts) + 1);
    Put16(at + 4, i + U32(kTessVerts) + 2);
    Put(kTessIndexCount, U32(kTessIndexCount) + 3);
  }
  Put(kTessVerts, U32(kTessVerts) + U32(surf + 8));
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}

// Surface type 3, an entity's: by the entity's type (0x1549B3C +0), on an
// 8-aligned frame, the lifted function of types 4-7, 9, 10, 12, 13 and
// 15, "FXPORT RT_ELECTRICITY TDB\n" through ri.Printf for 14, and
// 0x503860 (ESI) for any other.
void f_005039A0(Ctx& c) {
  const uint32_t e = c.esp, ebp = c.ebp;
  c.ebp = e - 4;
  c.esp = (e - 4) & ~7u;
  switch (U32(U32(kEntity))) {
    case 4: Call(c, f_005012D0, 0x5039CA); break;
    case 5: Call(c, f_00501240, 0x5039C1); break;
    case 6: Call(c, f_00501900, 0x5039DC); break;
    case 7: Call(c, f_005030E0, 0x5039E5); break;
    case 9: Call(c, f_00503010, 0x5039EE); break;
    case 10: Call(c, f_00503230, 0x5039F7); break;
    case 12: Call(c, f_005014C0, 0x5039D3); break;
    case 13: Call(c, f_00502350, 0x503A14); break;
    case 14:
      Push(c, 0x539320);
      Push(c, 0);
      CallPtr(c, kPrintf, 0x503A02);
      break;
    case 15: Call(c, f_00502450, 0x503A1D); break;
    default: Call(c, f_00503860, 0x503A26); break;
  }
  c.ebp = ebp;
  c.esp = e + 4;
}

// Surface type 24 (stack: surface; +8 its dlight bits, +0x24 its vertex
// count, +0x28 and +0x2C its vertices' two axes, +0x30 normals, +0x34 and
// +0x38 texture coordinates, +0x3C colours, +0x40 positions, +0x44 its
// index count, +0x48 its indexes) into the tessellator: the dlight bits,
// its indexes plus the vertex count so far (from the last), then by the
// memcpy for a shader with +0x50 bit 2, or with the cvar 0x15344E4 and
// dlight bits, the normals (0x17F0C64 set) and, for +0x50 bits 0xC, each
// axis it has (0x17F0C68, 0x17F0C6C set); positions 12 bytes apart, the
// coordinates and colours; returns the vertex count.
void f_005033B0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi;
  const uint32_t surf = Arg(c, e, 1);
  c.esi = surf;
  c.esp = e - 0xC;
  CheckOverflow(c, U32(surf + 0x24), U32(surf + 0x44), U32(surf + 0x44) + U32(kTessIndexCount),
                0x5033E1);
  const uint32_t indexes = kTessIndexes + 2 * U32(kTessIndexCount);
  Put(0x17F0C80, U32(0x17F0C80) | U32(surf + 8));
  const uint32_t verts = U32(kTessVerts), n = U32(surf + 0x44);
  c.ebx = verts;
  if (int32_t(n - 1) >= 0)
    for (uint32_t k = n; k; k--)
      Put16(indexes + 2 * (k - 1), U16(surf + 0x48 + 2 * (k - 1)) + verts);
  Put(kTessIndexCount, U32(kTessIndexCount) + U32(surf + 0x44));
  const uint32_t nv = U32(surf + 0x24);
  c.edi = nv;
  if ((U8(U32(kTessShader) + 0x50) & 2) || (CvarInt(0x15344E4) && U32(0x17F0C80))) {
    c.ebx = 12 * nv;
    Copy(c, kTessNormals + 12 * verts, U32(surf + 0x30), c.ebx, 0x503481);
    c.esp += 0xC;
    Put(0x17F0C64, 1);
    if (U8(U32(kTessShader) + 0x50) & 0xC) {
      if (const uint32_t src = U32(surf + 0x28)) {
        Copy(c, kTangents + 12 * U32(kTessVerts), src, c.ebx, 0x5034B9);
        c.esp += 0xC;
        Put(0x17F0C68, 1);
      }
      if (const uint32_t src = U32(surf + 0x2C)) {
        Copy(c, kBinormals + 12 * U32(kTessVerts), src, c.ebx, 0x5034E4);
        c.esp += 0xC;
        Put(0x17F0C6C, 1);
      }
    }
  }
  Copy(c, kTessXyz + 12 * U32(kTessVerts), U32(surf + 0x40), 12 * nv, 0x503512);
  c.ebx = 8 * nv;
  Copy(c, kTessTexCoords + 8 * U32(kTessVerts), U32(surf + 0x34), c.ebx, 0x503531);
  Copy(c, kTessTexCoords2 + 8 * U32(kTessVerts), U32(surf + 0x38), c.ebx, 0x503549);
  Copy(c, kVertColors + 4 * U32(kTessVerts), U32(surf + 0x3C), 4 * nv, 0x503568);
  c.eax = U32(kTessVerts) + U32(surf + 0x24);
  Put(kTessVerts, c.eax);
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// A type 25 surface (ESI) into the tessellator as well, for its dlights:
// the normals and both axes for the cvar 0x15344E4 (0x17F0C64, 0x17F0C68,
// 0x17F0C6C set), the coordinates, colours and positions (12 bytes apart)
// by the memcpy; its indexes less its first plus the vertex count so far;
// then both counts and its dlight bits.
void f_00503590(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, edi = c.edi, surf = c.esi;
  c.esp = e - 0xC;
  CheckOverflow(c, U32(surf + 0x24), U32(surf + 0x44), U32(surf + 0x44) + U32(kTessIndexCount),
                0x5035BC);
  auto copy = [&](uint32_t dst, uint32_t size, uint32_t src, uint32_t va) {
    Copy(c, dst + size * U32(kTessVerts), U32(surf + src), size * U32(surf + 0x24), va);
    c.esp += 0xC;
  };
  if (CvarInt(0x15344E4)) {
    copy(kTangents, 12, 0x28, 0x5035F3);
    copy(kBinormals, 12, 0x2C, 0x503619);
    copy(kTessNormals, 12, 0x30, 0x50363F);
    Put(0x17F0C68, 1);
    Put(0x17F0C6C, 1);
    Put(0x17F0C64, 1);
  }
  copy(kTessTexCoords, 8, 0x34, 0x503673);
  copy(kTessTexCoords2, 8, 0x38, 0x503693);
  copy(kVertColors, 4, 0x3C, 0x5036B4);
  copy(kTessXyz, 12, 0x40, 0x5036DA);
  const uint32_t add = U32(kTessVerts) - U16(surf + 0x48);
  for (uint32_t i = 0; int32_t(i) < I32(surf + 0x44); i++)
    Put16(kTessIndexes + 2 * (U32(kTessIndexCount) + i), U16(surf + 0x48 + 2 * i) + add);
  Put(kTessVerts, U32(kTessVerts) + U32(surf + 0x24));
  Put(kTessIndexCount, U32(kTessIndexCount) + U32(surf + 0x44));
  Put(0x17F0C80, U32(0x17F0C80) | U32(surf + 8));
  c.ebx = ebx;
  c.edi = edi;
  c.esp = e + 4;
}

// Surface type 25 (stack: surface, as type 24's) into the optimised batch,
// on an 8-aligned frame: when its indexes would take the batch to 0x20000,
// RB_EndSurface and the tessellator begun again (its shader and position
// stride, the entity 0x17F0C7C); the batch's vertex range widened to its
// first index and that plus its vertex count (signed, set by the first);
// its indexes as they are (by the memcpy) and both counts; then, with
// dlights (0x15497FC) and dlight bits of its own, unless 0x153479C is set
// and 0x1549548 differs from 0x153465C, also into the tessellator
// (f_00503590).
void f_00503760(Ctx& c) {
  const uint32_t e = c.esp, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t surf = Arg(c, e, 1);
  c.ebp = e - 4;
  c.esp = ((e - 4) & ~7u) - 8;
  c.esi = surf;
  if (int32_t(U32(surf + 0x44) + U32(0x1830C94)) >= 0x20000) {
    Call(c, f_004F7D00, 0x503782);
    const uint32_t stride = U32(kXyzStride);
    Put(kEntity, U32(0x17F0C7C));
    c.ecx = U32(kTessShader);
    Push(c, stride);
    Call(c, f_004F4AB0, 0x5037A0);
    c.esp += 4;
  }
  const uint32_t hi = U32(0x17F0C90), first = U16(surf + 0x48);
  if (!hi) {
    Put(0x17F0C8C, first);
    Put(0x17F0C90, U16(surf + 0x48) + U32(surf + 0x24));
  } else {
    if (I32(0x17F0C8C) > int32_t(first)) Put(0x17F0C8C, first);
    const uint32_t end = U16(surf + 0x48) + U32(surf + 0x24);
    if (int32_t(hi) < int32_t(end)) Put(0x17F0C90, end);
  }
  Copy(c, 0x17F0C94 + 2 * U32(0x1830C94), surf + 0x48, 2 * U32(surf + 0x44), 0x5037FC);
  c.edi = U32(0x1830C94) + U32(surf + 0x44);
  const uint32_t count = U32(0x1830C98);
  Put(0x1830C94, c.edi);
  Put(0x1830C98, count + U32(surf + 0x24));
  c.esp += 0xC;
  if (U32(0x15497FC) && U32(surf + 8) && (!U32(0x153479C) || U32(0x1549548) == U32(0x153465C)))
    Call(c, f_00503590, 0x503852);
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}


// ---- an entity's surfaces --------------------------------------------------------

namespace {

constexpr float kDeg2Rad = std::bit_cast<float>(0x3C8EFA35u);
// The view's origin, left and up axes (after its forward 0x1549834), and
// whether it is mirrored.
constexpr uint32_t kViewOrg = 0x1549828, kViewLeft = 0x1549840, kViewUp = 0x154984C,
                   kMirrored = 0x1549930;

// VectorNormalize (0x426710) of the vector at va (ESI), its length (ret:
// the call's return).
St Normalize(Ctx& c, uint32_t va, uint32_t ret) {
  c.esi = va;
  Call(c, f_00426710, ret);
  return PopSt0(c);
}

// FSINCOS of the float at va, the cosine stored to cos and the sine to
// sin, on the guest's x87 stack (out of range FSINCOS pushes nothing: the
// float itself is stored as the cosine and the register below it as the
// sine, which the second pop takes too).
void SinCos(Ctx& c, uint32_t va, uint32_t cos, uint32_t sin) {
  x87::LoadF32(c.fpu, U32(va));
  x87::SinCos(c.fpu);
  Put(cos, x87::StoreF32(c.fpu));
  x87::Pop(c.fpu);
  Put(sin, x87::StoreF32(c.fpu));
  x87::Pop(c.fpu);
}

// The vector product the beams take (b x a), into out.
void Cross(uint32_t out, uint32_t a, uint32_t b) {
  Put(out, Fst(St(Ld(a + 8)) * Ld(b + 4) - St(Ld(a + 4)) * Ld(b + 8)));
  Put(out + 4, Fst(St(Ld(a)) * Ld(b + 8) - St(Ld(a + 8)) * Ld(b)));
  Put(out + 8, Fst(St(Ld(a + 4)) * Ld(b) - St(Ld(a)) * Ld(b + 4)));
}

// RB_CheckOverflow for a quad, inlined: RB_EndSurface and the tessellator
// begun again (its shader and position stride, the entity 0x17F0C7C) when
// it cannot take 4 more vertices or 6 more indexes (signed). The original
// loads the indexes plus 6 into scratch only once the vertices pass (end:
// the call of RB_EndSurface, begin: of the tessellator's begin).
void QuadRoom(Ctx& c, uint32_t Ctx::*scratch, uint32_t end, uint32_t begin) {
  if (I32(kTessVerts) + 4 < 0x2000) {
    c.*scratch = U32(kTessIndexCount) + 6;
    if (int32_t(c.*scratch) < 0xC000) return;
  }
  Call(c, f_004F7D00, end + 5);
  const uint32_t stride = U32(kXyzStride);
  Put(kEntity, U32(0x17F0C7C));
  c.ecx = U32(kTessShader);
  Push(c, stride);
  Call(c, f_004F4AB0, begin + 5);
  c.esp += 4;
}

// A quad's two triangles, the vertex count plus each of ks, the index
// count stepped after each.
void QuadIndexes(std::initializer_list<uint32_t> ks) {
  for (uint32_t k : ks) {
    Put16(kTessIndexes + 2 * U32(kTessIndexCount), U32(kTessVerts) + k);
    Put(kTessIndexCount, U32(kTessIndexCount) + 1);
  }
}

// A beam's quad from the vertex count: the positions (at the stride) from
// and to plus w and -w along dir, texture coordinates st (8 dwords) and
// the entity's colour; then the triangles (0, 1, 2), (2, 1, 3) and the
// count.
void BeamQuad(F32 w, uint32_t dir, uint32_t from, uint32_t to, const uint32_t (&st)[8],
              uint32_t colour) {
  const uint32_t v = U32(kTessVerts);
  const St ws[4] = {St(w), -St(w), St(w), -St(w)};
  const uint32_t at[4] = {from, from, to, to};
  uint32_t p = kTessXyz + 4 * (U32(kXyzStride) * v);
  for (uint32_t i = 0; i < 4; i++, p = NextXyz(p)) {
    for (uint32_t k = 0; k < 12; k += 4) Put(p + k, Fst(ws[i] * Ld(dir + k) + Ld(at[i] + k)));
    Put(kTessTexCoords + 8 * v + 8 * i, st[2 * i]);
    Put(kTessTexCoords + 8 * v + 8 * i + 4, st[2 * i + 1]);
    Put(kVertColors + 4 * (v + i), colour);
  }
  QuadIndexes({0, 1, 2, 2, 1, 3});
  Put(kTessVerts, U32(kTessVerts) + 4);
}

// A sprite's quad (0x500F20, from the call at va): the entity's origin,
// left and up (locals), its colour, texture coordinates 0 to 1.
void SpriteQuad(Ctx& c, uint32_t ent, uint32_t left, uint32_t up, uint32_t va) {
  for (uint32_t v : {0x3F800000u, 0x3F800000u, 0u, 0u, ent + 0x6C, up, left, ent + 0x44}) Push(c, v);
  Call(c, f_00500F20, va + 5);
  c.esp += 0x20;
}

// The three floats at va less vec3_origin's, for a mirrored view.
void Mirror(uint32_t va) {
  if (U32(kMirrored))
    for (uint32_t k = 0; k < 12; k += 4) Put(va + k, Fst(St(Ld(0x537E54 + k)) - Ld(va + k)));
}

}  // namespace

// A quad into the tessellator (stack: origin, left, up, colour, s1, t1,
// s2, t2): the overflow tested and handled as RB_CheckOverflow would; the
// triangles (0, 1, 3), (3, 1, 2); its corners left + up + origin (y and
// z left + origin + up), origin - left + up, - left - up, left + origin -
// up (12 bytes apart from the position
// stride's first), the normals vec3_origin less the view's forward axis
// (0x1549834), both sets of texture coordinates (s1, t1), (s2, t1), (s2,
// t2), (s1, t2) through the x87 stack and the colour, these three by the
// vertex count's low 16 bits.
void f_00500F20(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi;
  c.esp = e - 0x14;
  c.edi = U32(kTessVerts);
  QuadRoom(c, &Ctx::edx, 0x500F57, 0x500F75);
  const uint32_t v = U32(kTessVerts), w = v & 0xFFFF, i = U32(kTessIndexCount);
  uint32_t at = 0;
  for (uint32_t k : {0u, 1u, 3u, 3u, 1u, 2u}) Put16(kTessIndexes + 2 * i + 2 * at++, v + k);
  const uint32_t origin = Arg(c, e, 1), left = Arg(c, e, 2), up = Arg(c, e, 3);
  const uint32_t p = kTessXyz + 4 * (U32(kXyzStride) * v);
  for (uint32_t k = 0; k < 12; k += 4) {
    Put(p + k, Fst(k ? St(Ld(left + k)) + Ld(origin + k) + Ld(up + k)
                     : St(Ld(left + k)) + Ld(up + k) + Ld(origin + k)));
    Put(p + 0xC + k, Fst(St(Ld(origin + k)) - Ld(left + k) + Ld(up + k)));
    Put(p + 0x18 + k, Fst(St(Ld(origin + k)) - Ld(left + k) - Ld(up + k)));
    Put(p + 0x24 + k, Fst(St(Ld(left + k)) + Ld(origin + k) - Ld(up + k)));
  }
  for (uint32_t k = 0; k < 12; k += 4) {
    const F32 n = Fst(St(Ld(0x537E54 + k)) - Ld(kViewAxes + k));
    for (uint32_t j = 0; j < 4; j++) Put(kTessNormals + 12 * (w + j) + k, n);
  }
  const uint32_t s1 = Arg(c, e, 5), t1 = Arg(c, e, 6), s2 = Arg(c, e, 7), t2 = Arg(c, e, 8);
  uint32_t k = 0;
  for (uint32_t x : {s1, t1, s2, t1, s2, t2, s1, t2}) {
    const F32 f = Fst(St(F32{x}));
    Put(kTessTexCoords2 + 8 * w + 4 * k, f);
    Put(kTessTexCoords + 8 * w + 4 * k++, f);
  }
  const uint32_t colour = U32(Arg(c, e, 4));
  for (uint32_t j = 0; j < 4; j++) Put(kVertColors + 4 * (w + j), colour);
  Put(kTessVerts, U32(kTessVerts) + 4);
  Put(kTessIndexCount, U32(kTessIndexCount) + 6);
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// A line's quad (EAX its direction, ECX its start, EDI its end; stack:
// half its width) after the quad's overflow test: from and to plus and
// minus the width along the direction, texture coordinates (0, 0), (1, 0),
// (0, 1), (1, 1).
void f_00502110(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi;
  const uint32_t dir = c.eax, from = c.ecx, to = c.edi;
  c.esp = e - 0x10;
  c.ebx = from;
  c.esi = dir;
  QuadRoom(c, &Ctx::ebp, 0x50214B, 0x502167);
  BeamQuad(Ld(e + 4), dir, from, to, {0, 0, 0x3F800000, 0, 0, 0x3F800000, 0x3F800000, 0x3F800000},
           U32(U32(kEntity) + 0x6C));
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.esp = e + 4;
}

// A beam's quad without the overflow test (EAX its end, EDX its start,
// ECX the direction; stack: its length, half its width): s 0 at the start
// and the length / 256 at the end, t 0 and 1 across (the colour kept in
// the length's slot).
void f_00502AF0(Ctx& c) {
  const uint32_t e = c.esp;
  const F32 s = Fst(St(Ld(e + 4)) * 0x1p-8f);
  Put(e + 4, U32(U32(kEntity) + 0x6C));
  BeamQuad(Ld(e + 8), c.ecx, c.edx, c.eax, {0, 0, 0, 0x3F800000, s.bits, 0, s.bits, 0x3F800000},
           U32(e + 4));
  c.esp = e + 4;
}

// Square rings along a beam (EAX right, ECX up, EBX the step; stack:
// count, start): with more than one, one fewer; four points 45 degrees
// apart from 45 about the start, a quarter of the cvar 0x1534540's
// integer out along up (the cosine) and right (the sine), each a step on
// for more than one; then for each a quad of the four points (after the
// overflow test), texture coordinates (1, 0), (1, 1), (0, 1), (0, 0)
// (integers through the x87 stack, t left in the count's slot), each
// point moved a step on; the triangles (0, 1, 3), (3, 1, 2).
void f_00502CE0(Ctx& c) {
  const uint32_t e = c.esp, ebp = c.ebp, esi = c.esi, edi = c.edi, f = e - 0x6C;
  const uint32_t right = c.eax, up = c.ecx, step = c.ebx, from = Arg(c, e, 2);
  c.esp = f;
  c.ebp = from;
  c.esi = right;
  c.edi = up;
  Put(f + 0x18, U32(U32(0x1534540) + 0x20));
  if (I32(e + 4) > 1) Put(e + 4, U32(e + 4) - 1);
  if (!U32(e + 4)) {
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
    return;
  }
  Put(f + 0x14, Fst(FromInt(I32(f + 0x18))));
  const float kPi = std::bit_cast<float>(0x40490FDBu), k1_180 = std::bit_cast<float>(0x3BB60B61u);
  uint32_t pt = f + 0x38;
  for (int32_t a = 45; a < 405; a += 90, pt += 0xC) {
    Put(f + 0x24, Fst(FromInt(a) * kPi * k1_180));
    SinCos(c, f + 0x24, f + 0x10, f + 0xC);
    const St cs = St(Ld(f + 0x10)), sn = St(Ld(f + 0xC));
    const St x = (cs * Ld(up) + sn * Ld(right)) * Ld(f + 0x14) * 0.25f;
    const St y = (sn * Ld(right + 4) + cs * Ld(up + 4)) * Ld(f + 0x14) * 0.25f;
    Put(f + 0x34, Fst((sn * Ld(right + 8) + cs * Ld(up + 8)) * Ld(f + 0x14) * 0.25f));
    const St px = x + Ld(from), py = y + Ld(from + 4);
    Put(f + 0x24, Fst(px));
    Put(pt, Fst(px));
    Put(pt + 4, Fst(py));
    const St pz = St(Ld(f + 0x34)) + Ld(from + 8);
    Put(f + 0x28, Fst(pz));
    Put(pt + 8, Fst(pz));
    if (I32(e + 4) > 1) {
      Put(pt, Fst(St(Ld(f + 0x24)) + Ld(step)));
      Put(pt + 4, Fst(py + Ld(step + 4)));
      Put(pt + 8, Fst(St(Ld(f + 0x28)) + Ld(step + 8)));
    }
    Put(f + 0x18, uint32_t(a + 90));
  }
  Put(f + 0x28, U32(U32(kEntity) + 0x6C));
  for (int32_t n = I32(e + 4); n > 0; n--) {
    QuadRoom(c, &Ctx::esi, 0x502E69, 0x502E85);
    const uint32_t v = U32(kTessVerts);
    uint32_t p = kTessXyz + 4 * (U32(kXyzStride) * v);
    for (uint32_t i = 0; i < 4; i++, p = NextXyz(p)) {
      const uint32_t q = f + 0x38 + 12 * i;
      for (uint32_t k = 0; k < 12; k += 4) Put(p + k, U32(q + k));
      Put(e + 4, i < 2);
      Put(kTessTexCoords + 8 * (v + i), Fst(FromInt(i < 2)));
      Put(e + 4, i == 0 || i == 3 ? 0 : 1);
      Put(kTessTexCoords + 8 * (v + i) + 4, Fst(FromInt(I32(e + 4))));
      Put(kVertColors + 4 * (v + i), U32(f + 0x28));
      Put(q, Fst(St(Ld(step)) + Ld(q)));
      Put(q + 4, Fst(St(Ld(q + 4)) + Ld(step + 4)));
      Put(q + 8, Fst(St(Ld(q + 8)) + Ld(step + 8)));
    }
    c.esi = 4;
    c.edi = kTessTexCoords + 8 * v + 0x20;
    c.ebp = kVertColors + 4 * v + 0x10;
    QuadIndexes({0, 1, 3, 3, 1, 2});
    Put(kTessVerts, U32(kTessVerts) + 4);
  }
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Entity type 4, a sprite facing the view: the view's left and up axes
// times its radius (+0x7C) and height (+0x64), turned by its rotation
// (+0x80, degrees, by FSINCOS) when it has one; left negated for a
// mirrored view.
void f_005012D0(Ctx& c) {
  const uint32_t e = c.esp, f = e - 0x34;
  uint32_t ent = U32(kEntity);
  c.esp = f;
  Put(f, U32(ent + 0x7C));
  Put(f + 4, U32(ent + 0x64));
  if (Ld(ent + 0x80) == 0.0f) {
    for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x1C + k, Fst(St(Ld(kViewLeft + k)) * Ld(f)));
    for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x28 + k, Fst(St(Ld(kViewUp + k)) * Ld(f + 4)));
  } else {
    Put(f + 0x18, Fst(St(Ld(ent + 0x80)) * kDeg2Rad));
    SinCos(c, f + 0x18, f + 8, f + 0xC);
    ent = U32(kEntity);
    const St t = St(Ld(f + 8)) * Ld(f), a = St(Ld(kViewLeft)) * t;
    Put(f + 0x20, Fst(t * Ld(kViewLeft + 4)));
    Put(f + 0x24, Fst(t * Ld(kViewLeft + 8)));
    const St u = -(St(Ld(f + 0xC)) * Ld(f));
    Put(f, Fst(u));
    Put(f + 0x1C, Fst(u * Ld(kViewUp) + a));
    Put(f + 0x20, Fst(St(Ld(f)) * Ld(kViewUp + 4) + Ld(f + 0x20)));
    Put(f + 0x24, Fst(St(Ld(f)) * Ld(kViewUp + 8) + Ld(f + 0x24)));
    const St t2 = St(Ld(f + 8)) * Ld(f + 4), b = St(Ld(kViewUp)) * t2;
    Put(f + 0x2C, Fst(t2 * Ld(kViewUp + 4)));
    Put(f + 0x30, Fst(t2 * Ld(kViewUp + 8)));
    const St w = St(Ld(f + 0xC)) * Ld(f + 4);
    Put(f + 0xC, Fst(w));
    Put(f + 0x28, Fst(w * Ld(kViewLeft) + b));
    Put(f + 0x2C, Fst(St(Ld(f + 0xC)) * Ld(kViewLeft + 4) + Ld(f + 0x2C)));
    Put(f + 0x30, Fst(St(Ld(f + 0xC)) * Ld(kViewLeft + 8) + Ld(f + 0x30)));
  }
  Mirror(f + 0x1C);
  SpriteQuad(c, ent, f + 0x1C, f + 0x28, 0x5014AC);
  c.esp = e + 4;
}

// Entity type 5: a quad of left (-radius, 0, 0) (vec3_origin less it for
// a mirrored view, its other two from vec3_origin) and up (0, radius, 0)
// in the world's axes.
void f_00501240(Ctx& c) {
  const uint32_t e = c.esp, f = e - 0x1C, ent = U32(kEntity);
  c.esp = f;
  const St r = St(Ld(ent + 0x7C));
  Put(f + 8, 0u);
  Put(f + 4, Fst(-r));
  Put(f + 0xC, 0u);
  Put(f + 0x14, Fst(r));
  Put(f + 0x10, 0u);
  Put(f + 0x18, 0u);
  if (U32(kMirrored)) {
    Put(f + 4, Fst(St(Ld(0x537E54)) - Ld(f + 4)));
    Put(f + 8, U32(0x537E58));
    Put(f + 0xC, U32(0x537E5C));
  }
  SpriteQuad(c, ent, f + 4, f + 0x10, 0x5012C3);
  c.esp = e + 4;
}

// Entity type 12, a sprite in its own plane: right and up from its first
// axis (+0x1C, by 0x426D30), times its radius and height, turned by its
// rotation as type 4's; right negated for a mirrored view.
void f_005014C0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, f = e - 0x5C;
  uint32_t ent = U32(kEntity);
  c.esp = f;
  Put(f + 0x10, U32(ent + 0x7C));
  Put(f + 0x14, U32(ent + 0x64));
  c.esi = ent;
  c.edi = ent + 0x1C;
  c.ebx = f + 0x38;
  c.eax = f + 0x2C;
  Call(c, f_00426D30, 0x5014EA);
  if (Ld(ent + 0x80) == 0.0f) {
    for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x2C + k, Fst(St(Ld(f + 0x2C + k)) * Ld(f + 0x10)));
    for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x38 + k, Fst(St(Ld(f + 0x38 + k)) * Ld(f + 0x14)));
  } else {
    Put(f + 0x28, Fst(St(Ld(ent + 0x80)) * kDeg2Rad));
    SinCos(c, f + 0x28, f + 0x18, f + 0x1C);
    ent = U32(kEntity);
    c.esi = ent;
    const St t = St(Ld(f + 0x18)) * Ld(f + 0x10);
    Put(f + 0x44, Fst(St(Ld(f + 0x2C)) * t));
    Put(f + 0x48, Fst(St(Ld(f + 0x30)) * t));
    Put(f + 0x4C, Fst(t * Ld(f + 0x34)));
    const St u = -(St(Ld(f + 0x1C)) * Ld(f + 0x10));
    Put(f + 0x10, Fst(u));
    const St x = u * Ld(f + 0x38) + Ld(f + 0x44);
    const St y = St(Ld(f + 0x10)) * Ld(f + 0x3C) + Ld(f + 0x48);
    Put(f + 0x4C, Fst(St(Ld(f + 0x10)) * Ld(f + 0x40) + Ld(f + 0x4C)));
    const uint32_t z = U32(f + 0x4C);
    const St t2 = St(Ld(f + 0x18)) * Ld(f + 0x14), b = St(Ld(f + 0x38)) * t2;
    Put(f + 0x54, Fst(t2 * Ld(f + 0x3C)));
    Put(f + 0x58, Fst(t2 * Ld(f + 0x40)));
    const St w = St(Ld(f + 0x1C)) * Ld(f + 0x14);
    Put(f + 0x1C, Fst(w));
    Put(f + 0x38, Fst(w * Ld(f + 0x2C) + b));
    Put(f + 0x3C, Fst(St(Ld(f + 0x1C)) * Ld(f + 0x30) + Ld(f + 0x54)));
    Put(f + 0x40, Fst(St(Ld(f + 0x1C)) * Ld(f + 0x34) + Ld(f + 0x58)));
    Put(f + 0x34, z);
    Put(f + 0x2C, Fst(x));
    Put(f + 0x30, Fst(y));
  }
  Mirror(f + 0x2C);
  c.esi = ent + 0x44;
  SpriteQuad(c, ent, f + 0x2C, f + 0x38, 0x50169B);
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Entity type 13, a line from its origin (+0x44) to its old origin
// (+0x54), its width +0x7C: across both ends' directions from the view
// (0x1549828), normalised, by 0x502110.
void f_00502350(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi, f = e - 0x44, ent = U32(kEntity);
  c.esp = f;
  for (uint32_t k = 0; k < 12; k += 4) {
    Put(f + 0x20 + k, U32(ent + 0x54 + k));
    Put(f + 0x2C + k, U32(ent + 0x44 + k));
  }
  for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x14 + k, Fst(St(Ld(f + 0x2C + k)) - Ld(kViewOrg + k)));
  for (uint32_t k = 0; k < 12; k += 4) Put(f + 8 + k, Fst(St(Ld(f + 0x20 + k)) - Ld(kViewOrg + k)));
  Cross(f + 0x38, f + 8, f + 0x14);
  Normalize(c, f + 0x38, 0x502424);
  Push(c, U32(ent + 0x7C));
  c.eax = f + 0x38;
  c.edi = f + 0x20;
  c.ecx = f + 0x2C;
  Call(c, f_00502110, 0x50243B);
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Entity type 9: rings (0x502CE0) from the old origin (+0x54) towards the
// origin, one per the cvar 0x1534470's value (+0x1C) of the distance (by
// _ftol2, at least 1) and stepped by it along the direction, right and up
// from 0x426D30.
void f_00503010(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, f = e - 0x38;
  const uint32_t ent = U32(kEntity);
  Put(f + 0x14, U32(ent + 0x54));
  Put(f + 0x18, U32(ent + 0x58));
  Put(f + 0x28, U32(ent + 0x4C));
  Put(f + 0x1C, U32(ent + 0x5C));
  Put(f + 8, Fst(St(Ld(ent + 0x44)) - Ld(f + 0x14)));
  Put(f + 0xC, Fst(St(Ld(ent + 0x48)) - Ld(f + 0x18)));
  Put(f + 0x10, Fst(St(Ld(f + 0x28)) - Ld(f + 0x1C)));
  c.esp = f - 0xC;
  Put(f + 4, Fst(Normalize(c, f + 8, 0x503064)));
  c.ebx = f + 0x20;
  c.eax = f + 0x2C;
  c.edi = f + 8;
  Call(c, f_00426D30, 0x503079);
  const uint32_t cvar = U32(0x1534470);
  c.esi = cvar;
  int32_t n = Ftol(St(Ld(f + 4)) / Ld(cvar + 0x1C));
  if (n <= 0) n = 1;
  for (uint32_t k = 0; k < 12; k += 4) Put(f + 8 + k, Fst(St(Ld(f + 8 + k)) * Ld(cvar + 0x1C)));
  Push(c, f + 0x14);
  Push(c, uint32_t(n));
  c.eax = f + 0x20;
  c.ecx = f + 0x2C;
  c.ebx = f + 8;
  Call(c, f_00502CE0, 0x5030CC);
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// The ends' directions from the view (into a, b) and the beam's direction
// across them, each normalised by 0x426710 (from the calls at rets).
void BeamAxis(Ctx& c, uint32_t from, uint32_t to, uint32_t a, uint32_t b, uint32_t out,
              const uint32_t (&rets)[3]) {
  for (uint32_t k = 0; k < 12; k += 4) Put(b + k, Fst(St(Ld(to + k)) - Ld(kViewOrg + k)));
  Normalize(c, b, rets[0]);
  for (uint32_t k = 0; k < 12; k += 4) Put(a + k, Fst(St(Ld(from + k)) - Ld(kViewOrg + k)));
  Normalize(c, a, rets[1]);
  Cross(out, a, b);
  Normalize(c, out, rets[2]);
}

// Entity type 7: a beam's quad (0x502AF0) from the origin (+0x44) to the
// old origin (+0x54), its length, as wide as the cvar 0x15344A8's value
// (+0x1C), across the ends' directions from the view.
void f_005030E0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, f = e - 0x40, ent = U32(kEntity);
  for (uint32_t k = 0; k < 12; k += 4) {
    Put(f + 0x28 + k, U32(ent + 0x54 + k));
    Put(f + 0x1C + k, U32(ent + 0x44 + k));
  }
  for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x34 + k, Fst(St(Ld(f + 0x1C + k)) - Ld(f + 0x28 + k)));
  c.esp = f - 4;
  Put(f, Fst(Normalize(c, f + 0x34, 0x503140)));
  BeamAxis(c, f + 0x1C, f + 0x28, f + 4, f + 0x10, f + 0x34, {0x503177, 0x5031AC, 0x5031F9});
  Push(c, U32(U32(0x15344A8) + 0x1C));
  Push(c, U32(f));
  c.ecx = f + 0x34;
  c.eax = f + 0x1C;
  c.edx = f + 0x28;
  Call(c, f_00502AF0, 0x50321A);
  c.esi = esi;
  c.esp = e + 4;
}

// Entity type 10: type 7's quad 8 wide (its ends swapped) four times,
// turned 45 degrees about the beam (0x426AB0) after each.
void f_00503230(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi, f = e - 0x4C, ent = U32(kEntity);
  for (uint32_t k = 0; k < 12; k += 4) {
    Put(f + 0x1C + k, U32(ent + 0x54 + k));
    Put(f + 0x28 + k, U32(ent + 0x44 + k));
  }
  for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x40 + k, Fst(St(Ld(f + 0x1C + k)) - Ld(f + 0x28 + k)));
  c.esp = f - 8;
  Put(f, Fst(Normalize(c, f + 0x40, 0x503291)));
  BeamAxis(c, f + 0x1C, f + 0x28, f + 4, f + 0x10, f + 0x34, {0x5032C8, 0x5032FD, 0x50334A});
  c.edi = U32(f);
  for (c.esi = 4; c.esi; c.esi--) {
    Push(c, 0x41000000);
    Push(c, c.edi);
    c.ecx = f + 0x34;
    c.eax = f + 0x1C;
    c.edx = f + 0x28;
    Call(c, f_00502AF0, 0x50336C);
    Push(c, 0x42340000);
    Push(c, f + 0x34);
    Push(c, f + 0x10);
    c.eax = f + 0x40;
    Call(c, f_00426AB0, 0x503384);
    c.esp += 0x14;
    for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x34 + k, U32(f + 0x10 + k));
  }
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Entity type 15, a cylinder from the origin (+0x44, radius +0x64) to the
// old origin (+0x54, radius +0x7C): its sides from the distance of its
// middle to the view and 0x1549960 ((1 - dist x it / 90 / 1024) x 32,
// FISTP after a 2^-30 nudge, 8 to 32); RB_CheckOverflow for twice
// that and 2 vertices, 6 indexes a side; rings about the axis from
// 0x426D30 (the sine on its first vector, the cosine on its second, by
// FSINCOS of the side's angle), the first repeated at the end; two
// triangles a side, then each ring's vertex pair (near: t 1, far: t 0; s
// the side's fraction) at the stride, the entity's colour.
constexpr uint32_t kSideA[3] = {0, 1, 3}, kSideB[3] = {3, 2, 0};
void f_00502450(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi, f = e - 0x37C;
  const uint32_t ent = U32(kEntity);
  c.esp = f;
  c.ebp = ent;
  const St s0 = St(Ld(ent + 0x44)) + Ld(ent + 0x54), s1 = St(Ld(ent + 0x58)) + Ld(ent + 0x48);
  Put(f + 0x2C, Fst(St(Ld(ent + 0x5C)) + Ld(ent + 0x4C)));
  Put(f + 0x24, Fst(s0 * 0.5f));
  const St h1 = s1 * 0.5f, h2 = St(Ld(f + 0x2C)) * 0.5f;
  Put(f + 0x24, Fst(St(Ld(f + 0x24)) - Ld(kViewOrg)));
  Put(f + 0x28, Fst(h1 - Ld(kViewOrg + 4)));
  Put(f + 0x2C, Fst(h2 - Ld(kViewOrg + 8)));
  const St dist = Normalize(c, f + 0x24, 0x5024BF);
  Put(f + 0x24, 0u);
  Put(f + 0x28, 0x3E100000u);
  const St fov = St(Ld(0x1549960)) * std::bit_cast<float>(0x3C360B61u);
  Put(f + 0x20, Fst((1.0f - dist * fov * 0x1p-10f) * 32.0f));
  const int32_t r = Fistp(St(Ld(f + 0x20)) + St(0x1p-30));
  Put(f + 0x18, uint32_t(r));
  const int32_t n = r < 8 ? 8 : r > 32 ? 32 : r;
  Put(f + 0x1C, uint32_t(n));
  if (I32(kTessVerts) + 2 * n + 2 >= 0x2000 || I32(kTessIndexCount) + 6 * n >= 0xC000) {
    Push(c, uint32_t(6 * n));
    Push(c, uint32_t(2 * n + 2));
    Call(c, f_00500A60, 0x50255D);
    c.esp += 8;
  }
  for (uint32_t k = 0; k < 12; k += 4) Put(f + 0x24 + k, Fst(St(Ld(ent + 0x44 + k)) - Ld(ent + 0x54 + k)));
  Normalize(c, f + 0x24, 0x502587);
  c.ebx = f + 0x40;
  c.eax = f + 0x4C;
  c.edi = f + 0x24;
  Call(c, f_00426D30, 0x50259A);
  const St inv = 1.0f / FromInt(n);
  Put(f + 0x20, Fst(inv));
  Put(f + 0x18, Fst(inv * std::bit_cast<float>(0x40C90FDBu)));
  Put(f + 0x14, 0u);
  // The rings: near at 0x5C, far at 0x1EC, from the second slot of each.
  for (int32_t i = 0; i < n; i++) {
    Put(f + 0x14, Fst(FromInt(I32(f + 0x14)) * Ld(f + 0x18)));
    SinCos(c, f + 0x14, f + 0x3C, f + 0x38);
    Put(f + 0x14, uint32_t(i + 1));
    for (uint32_t ring : {0u, 1u}) {
      const uint32_t radius = ring ? 0x7C : 0x64, centre = ring ? 0x54 : 0x44;
      const uint32_t out = f + (ring ? 0x1EC : 0x5C) + 12 * uint32_t(i);
      const St a = St(Ld(f + 0x38)) * Ld(ent + radius), b = St(Ld(f + 0x3C)) * Ld(ent + radius);
      Put(out, Fst(St(Ld(f + 0x40)) * b + a * Ld(f + 0x4C) + Ld(ent + centre)));
      Put(out + 4, Fst(St(Ld(f + 0x44)) * b + a * Ld(f + 0x50) + Ld(ent + centre + 4)));
      Put(out + 8, Fst(b * Ld(f + 0x48) + a * Ld(f + 0x54) + Ld(ent + centre + 8)));
    }
  }
  for (uint32_t ring : {0x5Cu, 0x1ECu})
    for (uint32_t k = 0; k < 12; k += 4) Put(f + ring + 12 * n + k, Fst(St(Ld(f + ring + k))));
  Put(f + 0x18, U32(f + 0x20));
  for (int32_t j = 0; j < n; j++) {
    for (const uint32_t* tri : {kSideA, kSideB}) {
      for (uint32_t m = 0; m < 3; m++)
        Put16(kTessIndexes + 2 * (U32(kTessIndexCount) + m), U32(kTessVerts) + 2 * j + tri[m]);
      Put(kTessIndexCount, U32(kTessIndexCount) + 3);
    }
  }
  const uint32_t v = U32(kTessVerts), colour = U32(U32(kEntity) + 0x6C);
  uint32_t p = kTessXyz + 4 * (U32(kXyzStride) * v);
  for (int32_t k = 0; k <= n; k++) {
    const F32 s = Fst(FromInt(k) * Ld(f + 0x18));
    for (uint32_t ring : {0u, 1u}) {
      const uint32_t from = f + (ring ? 0x1EC : 0x5C) + 12 * uint32_t(k), at = v + 2 * k + ring;
      for (uint32_t i = 0; i < 12; i += 4) Put(p + i, U32(from + i));
      p = NextXyz(p);
      Put(kTessTexCoords + 8 * at, s);
      Put(kTessTexCoords + 8 * at + 4, ring ? 0u : 0x3F800000u);
      Put(kVertColors + 4 * at, colour);
    }
  }
  Put(kTessVerts, U32(kTessVerts) + 2 * (n + 1));
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Entity type 6: six points 4 out from its beam (from the origin +0x44 to
// the old origin +0x54), a perpendicular (0x4273C0) turned 60 degrees
// apart about it (0x426AB0), each also the beam's length on; drawn red
// (255, 0, 0, 255) as a triangle strip of the pairs,
// the first again at the end, through the immediate-mode batch
// (0x4ADC20, its vertex 0x4ADEC0 inlined) with the image 0x1535AC4 and
// state bits 0x22; then fog back on as a flush would set it, and the
// vertex pointer back to the tessellator's. Nothing for a beam of no
// length.
void f_00501900(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, ebx = c.ebx, f = e - 0xC4, ent = U32(kEntity);
  const St d0 = St(Ld(ent + 0x54)) - St(Ld(ent + 0x44));
  Put(f + 8, U32(ent + 0x48));
  Put(f + 0xC0, U32(ent + 0x5C));
  Put(f + 0xC, U32(ent + 0x4C));
  Put(f + 0x10, Fst(d0));
  Put(f + 0x1C, Fst(d0));
  const St d1 = St(Ld(ent + 0x58)) - Ld(f + 8);
  Put(f + 0x14, Fst(d1));
  Put(f + 0x20, Fst(d1));
  const St d2 = St(Ld(f + 0xC0)) - Ld(f + 0xC);
  Put(f + 0x18, Fst(d2));
  Put(f + 0x24, Fst(d2));
  c.esp = f - 4;
  if (Normalize(c, f + 0x1C, 0x501967) == 0.0f) {
    c.esi = esi;
    c.esp = e + 4;
    return;
  }
  c.esp = f - 8;
  c.ebx = f + 0x1C;
  c.edx = f + 4;
  Call(c, f_004273C0, 0x501986);
  Put(f + 4, Fst(St(Ld(f + 4)) * 4.0f));
  c.ebx = 0;
  Put(f, 0u);
  const uint32_t edi = c.edi;
  c.esp = f - 0xC;
  Put(f + 8, Fst(St(Ld(f + 8)) * 4.0f));
  Put(f + 0xC, Fst(St(Ld(f + 0xC)) * 4.0f));
  for (c.esi = 0; c.esi < 0x48; c.esi += 0xC) {
    const uint32_t k = c.esi;
    Push(c, Fst(FromInt(I32(f)) * 60.0f).bits);
    c.ecx = f + 4;
    c.edi = f + 0x70 + k;
    c.eax = f + 0x1C;
    Push(c, c.ecx);
    Push(c, c.edi);
    Call(c, f_00426AB0, 0x5019E4);
    Put(f, U32(f) + 1);
    for (uint32_t j = 0; j < 12; j += 4)
      Put(f + 0x28 + k + j, Fst(St(Ld(f + 0x10 + j)) + Ld(f + 0x70 + k + j)));
    c.esp += 0xC;
  }
  c.esi = U32(0x1535AC4);
  Call(c, f_004CB3B0, 0x501A25);
  c.eax = 0x22;
  Call(c, f_004CB710, 0x501A2F);
  Call(c, f_004ADC20, 0x501A34);
  for (uint32_t va : {0x15495D8u, 0x15495D9u, 0x15495DAu, 0x15495DBu})
    Wr<uint8_t>(g_guest_base, va, va == 0x15495D8 || va == 0x15495DB ? 0xFF : 0);
  Put(0x15495C8, 5u);
  c.edi = edi;
  c.esp = f - 8;
  for (uint32_t i = 0; i < 14; i++) {
    const uint32_t from = f + ((i & 1) ? 0x28 : 0x70) + 12 * (i / 2 % 6);
    const uint32_t n = U32(0x15495E8);
    if (I32(0x15495E8) >= I32(0x15495EC)) break;
    const uint32_t b = U32(0x15495F0) + 32 * n;
    for (uint32_t k = 0; k < 4; k++) Wr<uint8_t>(g_guest_base, b + 0x1C + k, U8(0x15495D8 + k));
    Put(b + 0xC, U32(0x15495D0));
    Put(b + 0x10, U32(0x15495D4));
    for (uint32_t k = 0; k < 12; k += 4) Put(b + k, U32(from + k));
    Put(0x15495E8, n + 1);
  }
  Push(c, U32(0x15495E8));
  Push(c, 0);
  Push(c, U32(0x15495C8));
  CallPtr(c, 0x153501C, 0x50208B);
  Put(0x15495E8, 0u);
  Put(0x15495C8, 0u);
  Put(0x15495C4, 0u);
  if (!U32(0x1549B50)) {
    c.esi = 0x200000;
    if (!(U32(kStateBits) & 0x200000) && CvarInt(0x15347F4) &&
        ((U8(0x15496EC) & 8) ? U32(0x1535890) : U32(0x13F9CF8))) {
      Enable(c, 0xB60, 0x5020E0);
      Put(kStateBits, U32(kStateBits) | 0x200000);
    }
  }
  for (uint32_t v : {0x1630C40u, 0u, 0x1406u, 3u}) Push(c, v);
  CallPtr(c, 0x15354F0, 0x5020F9);
  c.ebx = ebx;
  c.esi = esi;
  c.esp = e + 4;
}

// Any other entity type: the axes, lines 16 long in red, green and blue
// from (0, 0, 0), through the immediate-mode batch (0x4ADC20
// with EBX as given; the vertices by 0x4ADEC0) with the image 0x1535AC4,
// 3 wide (glLineWidth 0x1535744, the width last set 0x15495CC), as lines;
// then width 1, fog back on (0x4C89D0) and the vertex pointer back to the
// tessellator's.
void f_00503860(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, ebx = c.ebx;
  c.esp = e - 4;
  Call(c, f_004ADC20, 0x503866);
  c.esi = U32(0x1535AC4);
  Call(c, f_004CB3B0, 0x503871);
  c.esi = esi;
  c.esp = e;
  auto width = [&](float w, uint32_t va) {
    if (!(Ld(0x15495CC) == w)) {
      Push(c, std::bit_cast<uint32_t>(w));
      Put(0x15495CC, std::bit_cast<uint32_t>(w));
      CallPtr(c, 0x1535744, va);
    }
  };
  width(3.0f, 0x503894);
  Push(c, ebx);
  c.ebx = 0;
  Put(0x15495C8, 1u);
  const uint32_t rets[6] = {0x5038C9, 0x5038D5, 0x5038F7, 0x503903, 0x503925, 0x503931};
  for (uint32_t axis = 0; axis < 3; axis++) {
    for (uint32_t k = 0; k < 4; k++)
      Wr<uint8_t>(g_guest_base, 0x15495D8 + k, k == axis || k == 3 ? 0xFF : 0);
    for (uint32_t end = 0; end < 2; end++) {
      for (uint32_t k = 3; k-- > 0;) Push(c, end && k == axis ? 0x41800000u : 0u);
      Call(c, f_004ADEC0, rets[2 * axis + end]);
    }
  }
  c.esp += 0x48;
  Push(c, U32(0x15495E8));
  Push(c, 0);
  Push(c, U32(0x15495C8));
  CallPtr(c, 0x153501C, 0x503942);
  Put(0x15495E8, 0u);
  Put(0x15495C8, 0u);
  width(1.0f, 0x503976);
  Put(0x15495C4, 0u);
  Call(c, f_004C89D0, 0x503987);
  for (uint32_t v : {0x1630C40u, 0u, 0x1406u, 3u}) Push(c, v);
  CallPtr(c, 0x15354F0, 0x503994);
  c.ebx = ebx;
  c.esp = e + 4;
}

}  // namespace recomp::gen
