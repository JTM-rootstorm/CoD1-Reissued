// The renderer's back end natively, from the bottom up: first the layer
// that turns the renderer's state into GL calls (GL_Bind, the cull face,
// the texture environment, the state bits, the client arrays, the fog
// colour and the element draws). Replacements of lifted functions named
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
                   kglArrayElement = 0x1535104, kglLockArraysEXT = 0x1534DDC;
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
// tr.identityLight (float).
constexpr uint32_t kIdentityLight = 0x1536290;

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

}  // namespace recomp::gen
