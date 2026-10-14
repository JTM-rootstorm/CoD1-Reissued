// 2D placement on screens wider than 4:3 (FINDINGS.md "Widescreen, FOV, 2D
// placement, frame rate", items 5-6). Replacements of lifted functions
// named with `replace` in manifest/CoDSP.manifest, cgamex86.manifest and
// uix86.manifest.
//
// The original lays its 640x480 2D out across the whole width, so on a
// wide screen everything is stretched sideways, text included. All of it
// reaches the renderer through a few entries: StretchPic and its
// coloured, rotated and four-vertex forms (in pixels), StretchRaw for
// cinematic frames (pixels, drawn at once) and the text command (640x480
// units, turned into pixels glyph by glyph by the client's AdjustFrom640
// when the render commands run). Each is placed by who is drawing:
//   Stretch  as the original (the engine's own 2D: console, game
//            messages);
//   Pillar   the 4:3 picture centred (menus, loading screens, cinematics:
//            4:3 assets that stay 4:3, with black bars);
//   Center   centred, but rectangles across the whole width stay across
//            it (the HUD: fades and full-screen overlays keep covering
//            the screen);
//   Left, Right  scaled toward that edge (HUD groups anchored there);
//   Scope    centred, and rectangles touching a side of the screen are
//            extended to it (the scope overlay's black fills).
// With A = W/H > 4/3, k = (4/3)/A and b = W(1 - k)/2 (pixels), centred is
// x' = kx + b, w' = kw; Left x' = kx; Right x' = kx + 2b. At 4:3 and
// narrower nothing changes.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <unordered_map>

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_004D42A0_lifted(Ctx& c);
void f_004D4330_lifted(Ctx& c);
void f_004D43D0_lifted(Ctx& c);
void f_004D44D0_lifted(Ctx& c);
void f_004D45B0_lifted(Ctx& c);
void f_004CCD10_lifted(Ctx& c);
void f_004D6400_lifted(Ctx& c);
void f_004CDCC0_lifted(Ctx& c);
void f_0040FED0_lifted(Ctx& c);
void f_004076F0_lifted(Ctx& c);
void f_400079A0_lifted(Ctx& c);
void f_30018CD0_lifted(Ctx& c);
void f_3003B710_lifted(Ctx& c);
void f_3000F770_lifted(Ctx& c);
void f_3000F500_lifted(Ctx& c);
void f_00409130_lifted(Ctx& c);

namespace {

// The client's copy of the screen size (glconfig vidWidth, vidHeight).
constexpr uint32_t kVidWidth = 0x143942C;
constexpr uint32_t kVidHeight = 0x1439430;
// Return address of the black StretchPic with which the screen-field
// function 0x410410 clears a wide screen before the menus and cinematics
// (not during play).
constexpr uint32_t kSideClearCall = 0x41047C;
// The client's white shader and the EXE's black colour (the side clear's).
constexpr uint32_t kWhiteShader = 0x1439448;
constexpr uint32_t kBlack = 0x537930;
// The colour 2D was last given (SetColor; none: white), for putting it
// back after the bars below.
bool g_colour_set = false;
uint32_t g_colour[4];
// The render command list: pointer to the frame's data, which holds the
// commands at +0x24D700 and the bytes used at +0x28D700.
constexpr uint32_t kBackEndData = 0x1549698;
constexpr uint32_t kCommands = 0x24D700;
constexpr uint32_t kCommandsUsed = 0x28D700;
// A text command's string starts at +0x25.
constexpr uint32_t kTextString = 0x25;

enum class Place : uint8_t { Stretch, Pillar, Center, Left, Right, Scope };

// Who is drawing: set by the wrappers of the drawing code below.
Place g_stack[32];
int g_depth = 0;
Place Current() { return g_depth ? g_stack[g_depth - 1] : Place::Stretch; }

struct Placing {
  explicit Placing(Place p) {
    if (g_depth < 32) g_stack[g_depth] = p;
    g_depth++;
  }
  ~Placing() { g_depth--; }
};

// Text commands' placements, by command address, from when they are
// queued until the render commands run.
std::unordered_map<uint32_t, Place> g_text;
// The placement of the text command being drawn (glyphs go through
// AdjustFrom640).
Place g_glyphs = Place::Stretch;

struct Screen {
  float w, k, b;
};
// The screen when it is wider than 4:3.
bool Wide(Screen* s) {
  const int32_t w = int32_t(Rd32(kVidWidth)), h = int32_t(Rd32(kVidHeight));
  if (w <= 0 || h <= 0 || int64_t(w) * 3 <= int64_t(h) * 4) return false;
  s->w = float(w);
  s->k = float(h) * 4.0f / (float(w) * 3.0f);
  s->b = (s->w - s->w * s->k) * 0.5f;
  return true;
}

float PlaceX(const Screen& s, Place p, float x) {
  switch (p) {
    case Place::Stretch: return x;
    case Place::Left: return s.k * x;
    case Place::Right: return s.k * x + 2 * s.b;
    default: return s.k * x + s.b;
  }
}

// A rectangle's x and width.
void PlaceRect(const Screen& s, Place p, float* x, float* w) {
  const float x0 = *x, x1 = *x + *w;
  if (p == Place::Stretch) return;
  if (p == Place::Center && x0 <= 0.5f && x1 >= s.w - 0.5f) return;
  float n0 = PlaceX(s, p, x0), n1 = PlaceX(s, p, x1);
  if (p == Place::Scope) {
    if (x0 <= 0.5f) n0 = 0;
    if (x1 >= s.w - 0.5f) n1 = s.w;
  }
  *x = n0;
  *w = n1 - n0;
}

float RdF(uint32_t va) { return std::bit_cast<float>(Rd32(va)); }
void WrF(uint32_t va, float v) { Wr32(va, std::bit_cast<uint32_t>(v)); }

// Dev aid: COD1_2D_LOG=1 logs each caller of each entry once.
void Log(const char* entry, Ctx& c, Place p) {
  static const bool on = Env("2D_LOG") != nullptr;
  if (!on) return;
  static uint32_t seen[512];
  static int n = 0;
  const uint32_t ret = Rd32(c.esp);
  for (int i = 0; i < n; i++)
    if (seen[i] == ret) return;
  if (n < 512) seen[n++] = ret;
  std::fprintf(stderr, "2d: %s from %08X place %d |", entry, ret, int(p));
  for (int i = 0; i < 6; i++) std::fprintf(stderr, " %g", RdF(c.esp + 4 + 4 * i));
  std::fprintf(stderr, "\n");
}

// Calls a lifted cdecl function with `args` on a frame of its own below
// the guest stack. `scratch` (up to 16 bytes) is copied just below the
// stack first; its guest address replaces any argument equal to kScratch.
constexpr uint32_t kScratch = 0xFFFFFFFF;
void CallBelow(Ctx& c, void (*fn)(Ctx&), std::initializer_list<uint32_t> args,
               const uint32_t* scratch = nullptr) {
  const uint32_t sp = c.esp, data = sp - 16;
  if (scratch)
    for (uint32_t i = 0; i < 4; i++) Wr32(data + 4 * i, scratch[i]);
  c.esp = data - 4 * uint32_t(args.size()) - 4;
  Wr32(c.esp, 0);  // return address, never used
  uint32_t a = c.esp + 4;
  for (uint32_t v : args) {
    Wr32(a, v == kScratch ? data : v);
    a += 4;
  }
  fn(c);
  c.esp = sp;
}

uint32_t Bits(float v) { return std::bit_cast<uint32_t>(v); }

// Black bars from 0 to b and from W - b to W over the whole height: black,
// two white-shader StretchPics, the colour put back.
void SideBars(Ctx& c, const Screen& s) {
  const uint32_t h = Bits(float(int32_t(Rd32(kVidHeight)))), shader = Rd32(kWhiteShader);
  CallBelow(c, f_004D42A0_lifted, {kBlack});
  for (float x : {0.0f, s.w - s.b})
    CallBelow(c, f_004D4330_lifted, {Bits(x), 0, Bits(s.b), h, 0, 0, 0, 0, shader});
  if (g_colour_set)
    CallBelow(c, f_004D42A0_lifted, {kScratch}, g_colour);
  else
    CallBelow(c, f_004D42A0_lifted, {0});
}

// A StretchPic-like entry with x, y, w, h as its first float arguments.
template <void (*Lifted)(Ctx&)>
void PlacedRect(Ctx& c, const char* name) {
  Screen s;
  if (!Wide(&s)) return Lifted(c);
  const Place p = Current();
  Log(name, c, p);
  float x = RdF(c.esp + 4), w = RdF(c.esp + 12);
  // A 4:3 screen's whole-screen backdrop (a full-screen menu) gets black
  // bars beside it: during play nothing else clears the sides.
  if (p == Place::Pillar && x <= 0.5f && x + w >= s.w - 0.5f && RdF(c.esp + 8) <= 0.5f &&
      RdF(c.esp + 8) + RdF(c.esp + 16) >= float(int32_t(Rd32(kVidHeight))) - 0.5f)
    SideBars(c, s);
  PlaceRect(s, p, &x, &w);
  WrF(c.esp + 4, x);
  WrF(c.esp + 12, w);
  Lifted(c);
}

}  // namespace

// SetColor(rgba or null): remembered for SideBars.
void f_004D42A0(Ctx& c) {
  const uint32_t rgba = Rd32(c.esp + 4);
  g_colour_set = rgba != 0;
  for (uint32_t i = 0; rgba && i < 4; i++) g_colour[i] = Rd32(rgba + 4 * i);
  f_004D42A0_lifted(c);
}

// StretchPic(x, y, w, h, s1, t1, s2, t2, shader). The engine's wide-screen
// clear (one black rectangle over the whole screen, queued before the
// menus) becomes the two side bars: as a whole-screen rectangle it ran
// after the cinematic frames, which StretchRaw draws at once, and hid
// them.
void f_004D4330(Ctx& c) {
  Screen s;
  if (!Wide(&s) || Rd32(c.esp) != kSideClearCall)
    return PlacedRect<f_004D4330_lifted>(c, "StretchPic");
  const uint32_t sp = c.esp;
  WrF(sp + 12, s.b);
  f_004D4330_lifted(c);
  c.esp = sp;
  WrF(sp + 4, s.w - s.b);
  f_004D4330_lifted(c);
}

// The same with a second colour for its lower edge (a vertical gradient).
void f_004D43D0(Ctx& c) { PlacedRect<f_004D43D0_lifted>(c, "StretchPicColor"); }
// The same rotated by an angle.
void f_004D44D0(Ctx& c) { PlacedRect<f_004D44D0_lifted>(c, "StretchPicRotate"); }

// A quad from four (x, y) points and their texture coordinates:
// Quad(points, st, shader). The points are the caller's; they are placed
// for the call and put back.
void f_004D45B0(Ctx& c) {
  Screen s;
  if (!Wide(&s)) return f_004D45B0_lifted(c);
  const Place p = Current();
  Log("Quad", c, p);
  const uint32_t pts = Rd32(c.esp + 4);
  uint32_t saved[4];
  for (int i = 0; i < 4; i++) {
    saved[i] = Rd32(pts + 8 * i);
    WrF(pts + 8 * i, PlaceX(s, p, RdF(pts + 8 * i)));
  }
  f_004D45B0_lifted(c);
  for (int i = 0; i < 4; i++) Wr32(pts + 8 * i, saved[i]);
}

// StretchRaw(x, y, w, h, cols, rows, data, client, dirty), integers: a
// cinematic frame, drawn at once.
void f_004CCD10(Ctx& c) {
  Screen s;
  if (!Wide(&s)) return f_004CCD10_lifted(c);
  const Place p = Current();
  Log("StretchRaw", c, p);
  float x = float(int32_t(Rd32(c.esp + 4))), w = float(int32_t(Rd32(c.esp + 12)));
  PlaceRect(s, p, &x, &w);
  Wr32(c.esp + 4, uint32_t(int32_t(std::lround(x))));
  Wr32(c.esp + 12, uint32_t(int32_t(std::lround(w))));
  f_004CCD10_lifted(c);
}

// Queues a text command (x, y, font, scale, colour, string, ...; 640x480
// units). Every text draw ends here; its placement is kept for when the
// command runs.
void f_004D6400(Ctx& c) {
  Screen s;
  if (!Wide(&s)) return f_004D6400_lifted(c);
  const Place p = Current();
  Log("Text", c, p);
  const uint32_t data = Rd32(kBackEndData);
  const uint32_t used = Rd32(data + kCommandsUsed);
  f_004D6400_lifted(c);
  if (Rd32(data + kCommandsUsed) != used) g_text[data + kCommands + used] = p;
}

// Runs a text command (string in EAX): its glyphs are placed as it was.
void f_004CDCC0(Ctx& c) {
  const auto it = g_text.find(c.eax - kTextString);
  if (it == g_text.end()) return f_004CDCC0_lifted(c);
  g_glyphs = it->second;
  g_text.erase(it);
  f_004CDCC0_lifted(c);
  g_glyphs = Place::Stretch;
}

// AdjustFrom640(&x, &y, &w, &h): 640x480 units to pixels. While a text
// command runs, its glyphs are placed in pixels.
void f_0040FED0(Ctx& c) {
  Screen s;
  if (g_glyphs == Place::Stretch || !Wide(&s)) return f_0040FED0_lifted(c);
  const uint32_t px = Rd32(c.esp + 4), pw = Rd32(c.esp + 12);
  f_0040FED0_lifted(c);
  if (!px || !pw) return;
  float x = RdF(px), w = RdF(pw);
  PlaceRect(s, g_glyphs, &x, &w);
  WrF(px, x);
  WrF(pw, w);
}

// Cinematic draw (slot in the first argument): its frame and letterbox
// bars, pillarboxed.
void f_004076F0(Ctx& c) {
  Placing p(Place::Pillar);
  f_004076F0_lifted(c);
}

// The UI module's vmMain: menus and loading screens stay 4:3.
void f_400079A0(Ctx& c) {
  Placing p(Place::Pillar);
  f_400079A0_lifted(c);
}

// The cgame module's vmMain: the HUD.
void f_30018CD0(Ctx& c) {
  Placing p(Place::Center);
  f_30018CD0_lifted(c);
}

// cgame's Menu_Paint (menu in ECX, its name at +0x20): HUD groups by name.
void f_3003B710(Ctx& c) {
  Place place = Place::Center;
  if (const uint32_t name = c.ecx ? Rd32(c.ecx + 0x20) : 0) {
    char n[16] = {};
    for (int i = 0; i < 15; i++)
      if (!(n[i] = char(GPtr(name + uint32_t(i))[0]))) break;
    if (!std::strcmp(n, "Compass") || !std::strcmp(n, "stance") || !std::strcmp(n, "tankstatus"))
      place = Place::Left;
    else if (!std::strcmp(n, "weaponinfo") || !std::strcmp(n, "Health"))
      place = Place::Right;
  }
  Placing p(place);
  f_3003B710_lifted(c);
}

// The scope overlay.
void f_3000F770(Ctx& c) {
  Placing p(Place::Scope);
  f_3000F770_lifted(c);
}

// Where a view direction lands on screen, as an offset from the centre in
// 640x480 units (x to [EDI], y to [ESI]), for the scope overlay and the
// reticles. It is computed with the 3D view's real FOV, so it is already
// right across the full width; centring it would shrink it by k.
void f_3000F500(Ctx& c) {
  const uint32_t px = c.edi;
  f_3000F500_lifted(c);
  Screen s;
  const Place p = Current();
  if (!Wide(&s) || p == Place::Stretch || p == Place::Left || p == Place::Right) return;
  WrF(px, RdF(px) / s.k);
}

// The engine's notify lines ("Objectives updated.", ...), drawn while the
// HUD is: they keep to the screen's left edge.
void f_00409130(Ctx& c) {
  Placing p(Place::Left);
  f_00409130_lifted(c);
}

}  // namespace recomp::gen
