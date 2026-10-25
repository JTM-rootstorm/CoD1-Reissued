// The renderer's back end natively, from the bottom up: the layer that
// turns the renderer's state into GL calls (GL_Bind, the cull face, the
// texture environment, the state bits, the client arrays, the fog colour
// and the element draws), a stage's texture units (each bundle's image,
// texture, environment and coordinates), and what a flush sets up before
// its stages draw: the fog, the texture units back to one, the vertex
// deforms and each stage's colours. Replacements of lifted functions named
// with `replace` in manifest/CoDSP.manifest; FINDINGS.md "Renderer back
// end". Names and evidence are in the Ghidra plate comments.
//
// The GL calls go through the renderer's qgl function pointers (set by
// its QGL_Init from opengl32's exports and wglGetProcAddress; the names
// below are the entry points it loads into each), with the original's
// arguments on the guest stack at the original's ESP and its return
// addresses, as the lifted call sites make them. Calling conventions are
// the originals' (register arguments from whole-program optimisation; see
// each function). Not reproduced: EAX, ECX and EDX on return (no caller
// reads them), the saved registers and locals in the original's frame
// below ESP, EFLAGS and the x87 status word (x87_value.h).

#include <cstdint>

#include "runtime/lifted.h"
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
// The vertex deforms (registers: f_00509F20).
void f_00508AE0(Ctx& c);
void f_00508C50(Ctx& c);
void f_00508F00(Ctx& c);
void f_00508DE0(Ctx& c);
void f_00509040(Ctx& c);
void f_005090E0(Ctx& c);
void f_00508800(Ctx& c);
void f_005095F0(Ctx& c);
void f_005098A0(Ctx& c);
void f_00509190(Ctx& c);
// The colour and alpha generators (registers: f_004F4FA0).
void f_0050A030(Ctx& c);
void f_0050A070(Ctx& c);
void f_0050A170(Ctx& c);
void f_0050B040(Ctx& c);
void f_004F2E60(Ctx& c);
void f_0050A0E0(Ctx& c);
void f_0050A120(Ctx& c);
void f_0050AEC0(Ctx& c);
void f_0050A270(Ctx& c);
void f_00426A60(Ctx& c);  // rotates ECX by the axes EAX into EDX
void f_004268B0(Ctx& c);  // normalises EAX (approximate inverse square root)

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
                   kglFogi = 0x1534C84, kglFogf = 0x1535598, kglHint = 0x15351A0;
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
// apart), normals (12 bytes apart) and vertex colours; and the stage
// colours computed from them.
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
// 0x16E0C40, by its rgbGen (+0x664: 2 white, 3/4 and 8-10 lifted
// generators, 5 the vertex colours, 6 them shifted down by the overbright
// bits, 7 one minus them, 12 a constant (+0x680), 13 a constant or the
// debug colours, otherwise tr.identityLight's byte) and alphaGen (+0x67C:
// 0 opaque, 2/3/6/7 lifted generators, 4 the vertices', 5 one minus them,
// 8 the portal distance, 9 a constant (+0x683), 10-13 view-dependent),
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

}  // namespace recomp::gen
