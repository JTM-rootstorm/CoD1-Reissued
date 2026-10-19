// XAnim evaluation: a DObj's local bone poses from its anim tree (0x47CD80)
// and its bone matrices from those poses (0x4766E0), read in place from the
// anim tree, the xanims and the xmodels in guest memory. Replacements of
// lifted functions named with `replace` in manifest/CoDSP.manifest;
// FINDINGS.md "XAnim evaluation". Names and evidence are in the Ghidra plate
// comments.
//
// The originals are x87 code; the arithmetic is src/x87_value.h's, operand
// for operand (a value the original keeps on the x87 stack is an St, one it
// stores is an F32), which gives the original's bits at the main thread's
// control word. Built with -ffp-contract=off (CMakeLists.txt).
//
// The anim tree recursion's temporary pose buffers stay where the original
// allocates them, on the guest stack (its alloca), so every pose is guest
// memory; the lifted functions called from here (the part-to-bone map and
// its release, the part duplicates) get the original's register and stack
// arguments. Not reproduced: EAX, ECX, EDX and EFLAGS on return (every call
// site overwrites them or returns to code that does: the cgame and game
// syscall exits do `xor eax,eax`), the x87 status word, and the dead stack
// below ESP.

#include <cstdint>

#include "runtime/lifted.h"
#include "x87_value.h"

namespace recomp::gen {

void f_00478FC0(Ctx& c);  // the part-to-bone map of EAX's anim for (models, count) -> AX
void f_0045D580(Ctx& c);  // releases string-table entry AX (EDX bytes)
void f_004761F0(Ctx& c);  // the DObj's part duplicates (dobj)

namespace {

using lift::Wr;

// ---- data -------------------------------------------------------------------------

// A bone's local pose: the rotation as a quaternion (blended, not yet
// normalised while an anim tree is summed), the summed weight, the
// translation.
struct Pose {
  F32 quat[4];
  F32 weight;
  F32 trans[3];
};
static_assert(sizeof(Pose) == 0x20);

// The anim being evaluated and its DObj.
constexpr uint32_t kTree = 0xA6D008;      // XAnimTree*: anims (+0), info index per anim (+8, u16)
constexpr uint32_t kTempSize = 0xA6D00C;  // numBones * sizeof(Pose)
// XAnimInfo[] (0x44 each): two states of 0x1C at +0xC, time (+0) and
// weight (+0x14) among them; kState picks one.
constexpr uint32_t kInfo = 0xA6D010;
constexpr uint32_t kDObj = 0xA8F014;
constexpr uint32_t kSkip = 0xA8F018;    // bones not to compute: 128 bits
constexpr uint32_t kCalced = 0xA8F030;  // bones posed by the tree: 128 bits
constexpr uint32_t kNumBones = 0xA8F640;
constexpr uint32_t kState = 0xA8F648;
constexpr uint32_t kModels = 0xA8F650;  // the DObj's XModel* array
constexpr uint32_t kNumModels = 0xA8F668;
constexpr uint32_t kStrings = 0x8D8BF8;  // the string table: 8-byte entries, data at +4

// The anim tree (*kTree): an anim count (+4), then 8-byte anims from +8:
// child count (u16, +0), the xanim (+4) or for a parent flags, first child
// (u16, +6).
//
// An xanim (anim +4) points to its data (+4): frame count (u16, +0), part
// names (+0xC: int16 count, then the names), tracks per part (+0x10:
// translation, rotation) and the parts whose rotation is about z alone
// (+0x1C, one bit each: two quaternion components). A track has its key
// frames (+0) and key count (u16, +4), then the frame of each key (from +6,
// bytes for fewer than 256 frames, else u16). With no keys the track holds
// its one value itself: int16 rotations at +0, +2, (+6, +8), a float
// translation at +0, +8, +0xC. Rotations are int16 per component, scaled by
// 1/32767.
//
// A DObj: anim tree (+0), skeleton (+4), cached part-to-bone maps per anim
// (+0xC: u16 string-table entries, then a byte per anim for the generation
// they were made in, after the current one), part duplicates (+0x14, u16
// string-table entry: 128 bits of duplicated bones, then pairs of bone + 1,
// source + 1 up to a zero), model count (u8, +0x16), bone count (u8,
// +0x17), XModel* models (+0x18), the bone each model hangs from (u8 per
// model, +0x48, 0xFF for none).
//
// A skeleton: bones with poses from the tree (128 bits, +0), bones whose
// rotation is set elsewhere (+0x10), bones with matrices (+0x20), then the
// matrices (+0x30, 16 floats each: three axes and the origin, a zero after
// each) and the poses after them.
//
// An xmodel's parts (*(*(model + 4)) + 4): bone names (+0: a pointer to an
// int16 count, then the names; the parent of each non-root bone follows the
// pointer as a byte, bones back), root bone count (int16, +4), the base
// rotation (int16[4], +0xC) and translation (float[3], +0x10) of each
// non-root bone.

constexpr float kQuatScale = std::bit_cast<float>(0x38000100u);  // 1/32767, 0x55B5F4

template <typename T>
T& At(uint32_t va) {
  return *reinterpret_cast<T*>(g_guest_base + va);
}
uint32_t Global(uint32_t va) { return At<uint32_t>(va); }
// Bit i of the bit array at va (the originals test bytes).
bool Bit(uint32_t va, int32_t i) { return (At<uint8_t>(va + (i >> 3)) >> (i & 7)) & 1; }
bool Bit(const uint32_t* bits, int32_t i) {
  return (reinterpret_cast<const uint8_t*>(bits)[i >> 3] >> (i & 7)) & 1;
}
uint32_t ModelParts(uint32_t model) {
  return At<uint32_t>(At<uint32_t>(At<uint32_t>(model + 4)) + 4);
}

void Push(Ctx& c, uint32_t v) {
  c.esp -= 4;
  Wr<uint32_t>(g_guest_base, c.esp, v);
}
// The lifted function fn, called from the original's call site whose
// return address is ret. Arguments already pushed stay for the caller.
void Call(Ctx& c, GuestFn fn, uint32_t ret) {
  Push(c, ret);
  fn(c);
}

// Guest stack below the caller's ESP (the anim tree's alloca). A guest
// longjmp (the error exit) unwinding through it has set ESP already, to its
// setjmp's frame above this one.
struct GuestFrame {
  Ctx& c;
  uint32_t esp;
  GuestFrame(Ctx& c, uint32_t size) : c(c), esp(c.esp) { c.esp -= size; }
  ~GuestFrame() {
    if (c.esp <= esp) c.esp = esp;
  }
};

// ---- one xanim's parts --------------------------------------------------------------

// The key before frame (0x4790C0 for byte frames, 0x479140 for u16): a
// search of the track's key frames from the guess numKeys * time.
template <typename T>
int32_t FindKey(uint32_t frames, int32_t frame, int32_t numKeys, F32 time) {
  auto key = [&](int32_t i) { return int32_t(At<T>(frames + i * int32_t(sizeof(T)))); };
  int32_t lo = 0, hi = numKeys;
  int32_t i = Ftol(FromInt(numKeys) * time);
  if (frame < key(i)) {
    int32_t top = i;
    for (;;) {
      i = (lo + top) / 2;
      if (frame < key(i)) {
        top = i;
        continue;
      }
      if (frame < key(i + 1)) return i;
      top--;
      lo = i + 1;
      if (!(frame < key(top))) return top;
    }
  }
  for (;;) {
    if (frame < key(i + 1)) return i;
    int32_t next = i + 1;
    for (;;) {
      i = (next + hi) / 2;
      if (!(frame < key(i))) break;
      next++;
      hi = i;
      if (frame < key(next)) return next - 1;
    }
  }
}

// The parts of an xanim at time (0 to 1, not 1) added to out with weight
// (0x4791C0 for fewer than 256 frames, byte key frames; 0x4795B0 for more,
// u16): each track's two keys around the frame, interpolated linearly.
// bones: the DObj bone of each part.
template <typename T>
void AddInterpolated(uint32_t data, uint32_t bones, F32 time, F32 weight, uint32_t out) {
  const F32 wq = Fst(St(weight) * kQuatScale);
  const int32_t numFrames = At<uint16_t>(data);
  const St at = FromInt(numFrames) * time;
  const F32 atf = Fst(at);
  const int32_t frame = Ftol(at);
  const uint32_t simple = At<uint32_t>(data + 0x1C);
  const int32_t numParts = At<int16_t>(At<uint32_t>(data + 0xC));
  // The key before the frame and the fraction of the way to the next.
  auto key = [&](uint32_t track, int32_t& k) {
    const int32_t numKeys = At<uint16_t>(track + 4);
    if (numKeys < numFrames) {
      k = FindKey<T>(track + 6, frame, numKeys, time);
      const int32_t a = At<T>(track + 6 + k * int32_t(sizeof(T)));
      const int32_t b = At<T>(track + 6 + (k + 1) * int32_t(sizeof(T)));
      return (atf - FromInt(a)) / FromInt(b - a);
    }
    k = frame;
    return atf - FromInt(frame);
  };
  // Component i of an int16 rotation, interpolated from key k to k + 1.
  auto lerp = [](uint32_t keys, int32_t k, int32_t stride, int i, St f) {
    const int32_t a = At<int16_t>(keys + k * stride + i * 2);
    const int32_t b = At<int16_t>(keys + k * stride + stride + i * 2);
    return FromInt(b - a) * f + FromInt(a);
  };
  for (int32_t p = 0; p < numParts; p++) {
    const uint8_t bone = At<uint8_t>(bones + p);
    if (Bit(kSkip, bone)) continue;
    const uint32_t tracks = At<uint32_t>(data + 0x10) + p * 8;
    const uint32_t rot = At<uint32_t>(tracks + 4);
    Pose& o = At<Pose>(out + bone * 0x20u);
    if (Bit(simple, p)) {
      if (!rot) {
        o.quat[3] = Fst(St(weight) + o.quat[3]);
      } else if (!At<uint16_t>(rot + 4)) {
        o.quat[2] = Fst(FromInt(At<int16_t>(rot)) * wq + o.quat[2]);
        o.quat[3] = Fst(FromInt(At<int16_t>(rot + 2)) * wq + o.quat[3]);
      } else {
        int32_t k;
        const St f = key(rot, k);
        const uint32_t keys = At<uint32_t>(rot);
        o.quat[2] = Fst(lerp(keys, k, 4, 0, f) * wq + o.quat[2]);
        o.quat[3] = Fst(lerp(keys, k, 4, 1, f) * wq + o.quat[3]);
      }
    } else if (!At<uint16_t>(rot + 4)) {
      o.quat[0] = Fst(FromInt(At<int16_t>(rot)) * wq + o.quat[0]);
      o.quat[1] = Fst(FromInt(At<int16_t>(rot + 2)) * wq + o.quat[1]);
      o.quat[2] = Fst(FromInt(At<int16_t>(rot + 6)) * wq + o.quat[2]);
      o.quat[3] = Fst(FromInt(At<int16_t>(rot + 8)) * wq + o.quat[3]);
    } else {
      int32_t k;
      const St f = key(rot, k);
      const uint32_t keys = At<uint32_t>(rot);
      for (int i = 0; i < 4; i++) o.quat[i] = Fst(lerp(keys, k, 8, i, f) * wq + o.quat[i]);
    }
    if (const uint32_t tr = At<uint32_t>(tracks)) {
      if (!At<uint16_t>(tr + 4)) {
        const F32* v = &At<F32>(tr);
        o.trans[0] = Fst(St(weight) * v[0] + o.trans[0]);
        o.trans[1] = Fst(St(weight) * v[2] + o.trans[1]);
        o.trans[2] = Fst(St(weight) * v[3] + o.trans[2]);
      } else {
        int32_t k;
        const St f = key(tr, k);
        const F32* a = &At<F32>(At<uint32_t>(tr) + k * 12u);
        for (int i = 0; i < 3; i++)
          o.trans[i] = Fst(((St(a[i + 3]) - a[i]) * f + a[i]) * weight + o.trans[i]);
      }
    }
    o.weight = Fst(St(weight) + o.weight);
  }
}

// The parts of an xanim at its last frame, or with no frames (0x4799A0),
// added to out with weight.
void AddLast(uint32_t data, uint32_t bones, F32 weight, uint32_t out) {
  const F32 wq = Fst(St(weight) * kQuatScale);
  const uint32_t simple = At<uint32_t>(data + 0x1C);
  const int32_t numParts = At<int16_t>(At<uint32_t>(data + 0xC));
  for (int32_t p = 0; p < numParts; p++) {
    const uint8_t bone = At<uint8_t>(bones + p);
    if (Bit(kSkip, bone)) continue;
    const uint32_t tracks = At<uint32_t>(data + 0x10) + p * 8;
    const uint32_t rot = At<uint32_t>(tracks + 4);
    Pose& o = At<Pose>(out + bone * 0x20u);
    if (Bit(simple, p)) {
      if (!rot) {
        o.quat[3] = Fst(St(weight) + o.quat[3]);
      } else {
        const uint32_t numKeys = At<uint16_t>(rot + 4);
        const uint32_t v = numKeys ? At<uint32_t>(rot) + numKeys * 4 : rot;
        o.quat[2] = Fst(FromInt(At<int16_t>(v)) * wq + o.quat[2]);
        o.quat[3] = Fst(FromInt(At<int16_t>(v + 2)) * wq + o.quat[3]);
      }
    } else if (const uint32_t numKeys = At<uint16_t>(rot + 4)) {
      const uint32_t v = At<uint32_t>(rot) + numKeys * 8;
      for (int i = 0; i < 4; i++)
        o.quat[i] = Fst(FromInt(At<int16_t>(v + i * 2)) * wq + o.quat[i]);
    } else {
      o.quat[0] = Fst(FromInt(At<int16_t>(rot)) * wq + o.quat[0]);
      o.quat[1] = Fst(FromInt(At<int16_t>(rot + 2)) * wq + o.quat[1]);
      o.quat[2] = Fst(FromInt(At<int16_t>(rot + 6)) * wq + o.quat[2]);
      o.quat[3] = Fst(FromInt(At<int16_t>(rot + 8)) * wq + o.quat[3]);
    }
    if (const uint32_t tr = At<uint32_t>(tracks)) {
      const uint32_t numKeys = At<uint16_t>(tr + 4);
      const F32* v = &At<F32>(numKeys ? At<uint32_t>(tr) + numKeys * 12 : tr);
      const int at[2][3] = {{0, 2, 3}, {0, 1, 2}};  // the value in the track: x, y, z
      for (int i = 0; i < 3; i++)
        o.trans[i] = Fst(St(weight) * v[at[numKeys != 0][i]] + o.trans[i]);
    }
    o.weight = Fst(St(weight) + o.weight);
  }
}

// ---- the anim tree ------------------------------------------------------------------

// Zeroes the poses of the bones to compute (0x479BD0).
void Clear(uint32_t out) {
  const int32_t n = At<int32_t>(kNumBones);
  for (int32_t b = 0; b < n; b++, out += 0x20)
    if (!Bit(kSkip, b)) At<Pose>(out) = Pose{};
}

// An anim's state in the tree: its info's weight or time.
F32 InfoState(uint16_t info, uint32_t field) {
  return At<F32>(kInfo + info * 0x44u + At<uint32_t>(kState) * 0x1Cu + field);
}
constexpr uint32_t kTime = 0xC, kWeight = 0x20;

// A leaf of the tree (0x479C20): the xanim's parts posed with weight at
// time, through the DObj's map of its parts to bones (map: 128 bits of the
// bones it poses, then each part's bone).
void AddAnim(uint32_t anim, F32 weight, F32 time, uint32_t map, uint32_t out) {
  for (int w = 0; w < 4; w++)
    At<uint32_t>(kCalced + w * 4) |= At<uint32_t>(map + w * 4) & ~At<uint32_t>(kSkip + w * 4);
  const uint32_t data = At<uint32_t>(anim + 4);
  const uint32_t numFrames = At<uint16_t>(data);
  if (!(St(time) == 1.0f) && numFrames) {
    if (numFrames < 0x100)
      AddInterpolated<uint8_t>(data, map + 0x10, time, weight, out);
    else
      AddInterpolated<uint16_t>(data, map + 0x10, time, weight, out);
    return;
  }
  AddLast(data, map + 0x10, weight, out);
}

// The anim tree below node with weight, into out (0x47BD40). A node with one
// child of nonzero weight is that child; with more, their blend: each child
// added at its own weight into out (clear: out is zeroed first and the
// result is normalised there) or into a buffer on the guest stack, whose
// normalised poses are then added to out. accumulate is false for the root
// only, whose sum is divided by the summed weights. A leaf is added through
// the DObj's map of its parts to bones, made again (lifted) when the DObj's
// generation has moved on.
void Calc(Ctx& c, uint32_t node, F32 weight, uint32_t out, bool clear, bool accumulate) {
  const uint32_t anims = At<uint32_t>(Global(kTree));
  uint32_t entry = anims + 8 + node * 8;
  uint32_t count = At<uint16_t>(entry);
  uint32_t first = 0, second = 0;
  F32 w1, w2;
  while (count) {
    const uint32_t tree = Global(kTree);
    uint32_t i = 0;
    for (; i < count; i++) {
      const uint16_t info = At<uint16_t>(tree + 8 + (At<uint16_t>(entry + 6) + i) * 2);
      if (!info) continue;
      const St w = InfoState(info, kWeight);
      w1 = Fst(w);
      if (!(w == 0.0f)) break;
    }
    if (i == count) {
      if (clear) Clear(out);
      return;
    }
    for (uint32_t j = i + 1; j < count; j++) {
      const uint16_t info = At<uint16_t>(tree + 8 + (At<uint16_t>(entry + 6) + j) * 2);
      if (!info) continue;
      const St w = InfoState(info, kWeight);
      w2 = Fst(w);
      if (!(w == 0.0f)) {
        first = i;
        second = j;
        goto blend;
      }
    }
    node = At<uint16_t>(entry + 6) + i;
    entry = anims + 8 + node * 8;
    count = At<uint16_t>(entry);
  }
  {
    if (clear) Clear(out);
    const uint32_t dobj = Global(kDObj);
    const uint32_t cache = At<uint32_t>(dobj + 0xC);
    const uint32_t gen = cache + At<uint32_t>(At<uint32_t>(Global(kTree)) + 4) * 2;
    const uint8_t current = At<uint8_t>(gen);
    uint32_t maps = dobj + 0xC;
    auto make = [&](uint32_t ret) {
      Push(c, Global(kNumModels));
      Push(c, Global(kModels));
      c.eax = entry;
      Call(c, f_00478FC0, ret);
      c.esp += 8;
      At<uint16_t>(At<uint32_t>(maps) + node * 2) = uint16_t(c.eax);
    };
    if (!At<uint16_t>(cache + node * 2)) {
      At<uint8_t>(gen + node + 1) = current;
      make(0x47BEB0);
    } else if (At<uint8_t>(gen + node + 1) != current) {
      At<uint8_t>(gen + node + 1) = current;
      const uint32_t names = At<uint32_t>(At<uint32_t>(At<uint32_t>(entry + 4) + 4) + 0xC);
      c.edx = uint32_t(int32_t(At<int16_t>(names))) + 0x10;
      c.eax = (cache & 0xFFFF0000u) | At<uint16_t>(cache + node * 2);
      Call(c, f_0045D580, 0x47BE92);
      maps = Global(kDObj) + 0xC;
      make(0x47BEB0);
    }
    const uint16_t handle = At<uint16_t>(At<uint32_t>(maps) + node * 2);
    const uint32_t map = handle ? Global(kStrings) + handle * 8u + 4 : 0;
    const uint16_t info = At<uint16_t>(Global(kTree) + node * 2 + 8);
    AddAnim(At<uint32_t>(entry + 4), weight, InfoState(info, kTime), map, out);
    return;
  }
blend:
  GuestFrame f(c, clear ? 0 : (Global(kTempSize) + 3) & ~3u);
  const uint32_t buf = clear ? out : c.esp;
  Calc(c, At<uint16_t>(entry + 6) + first, w1, buf, true, true);
  Calc(c, At<uint16_t>(entry + 6) + second, w2, buf, false, true);
  for (uint32_t j = second + 1; j < count; j++) {
    const uint32_t child = At<uint16_t>(entry + 6) + j;
    const uint16_t info = At<uint16_t>(Global(kTree) + child * 2 + 8);
    if (!info) continue;
    const St w = InfoState(info, kWeight);
    const F32 wj = Fst(w);
    if (!(w == 0.0f)) Calc(c, child, wj, buf, false, true);
  }
  const int32_t n = At<int32_t>(kNumBones);
  if (!accumulate) {
    for (int32_t b = 0; b < n; b++) {
      Pose& o = At<Pose>(out + b * 0x20u);
      if (Bit(kSkip, b) || o.weight == 0.0f) continue;
      const St s = St(1.0f) / o.weight;
      for (int i = 0; i < 4; i++) o.quat[i] = Fst(s * o.quat[i]);
      for (int i = 0; i < 3; i++) o.trans[i] = Fst(s * o.trans[i]);
    }
    return;
  }
  for (int32_t b = 0; b < n; b++) {
    if (Bit(kSkip, b)) continue;
    const Pose& p = At<Pose>(buf + b * 0x20u);
    Pose& o = At<Pose>(out + b * 0x20u);
    const St q0 = p.quat[0], q1 = p.quat[1], q2 = p.quat[2], q3 = p.quat[3];
    const St len2 = ((q3 * q3 + q1 * q1) + q0 * q0) + q2 * q2;
    if (clear) {
      if (!(len2 == 0.0f)) {
        const St s = weight / Sqrt(len2);
        for (int i = 0; i < 4; i++) o.quat[i] = Fst(s * o.quat[i]);
      }
      if (!(o.weight == 0.0f)) {
        const St s = St(weight) / o.weight;
        o.weight = weight;
        for (int i = 0; i < 3; i++) o.trans[i] = Fst(s * o.trans[i]);
      }
    } else {
      if (!(len2 == 0.0f)) {
        const St s = weight / Sqrt(len2);
        for (int i = 0; i < 4; i++) o.quat[i] = Fst(s * p.quat[i] + o.quat[i]);
      }
      if (!(p.weight == 0.0f)) {
        const St s = St(weight) / p.weight;
        o.weight = Fst(St(weight) + o.weight);
        for (int i = 0; i < 3; i++) o.trans[i] = Fst(s * p.trans[i] + o.trans[i]);
      }
    }
  }
}

// The poses of the DObj's bones in mask (0x47CD80), those not computed yet:
// from its anim tree, and for the bones it does not pose, the models' base
// pose (roots: identity at the origin; the others: their base rotation, no
// translation; the weight is left).
void CalcAnim(Ctx& c, uint32_t dobj, const uint32_t* mask) {
  const uint32_t skel = At<uint32_t>(dobj + 4);
  bool done = true;
  for (int w = 0; w < 4; w++) {
    const uint32_t s = ~mask[w] | At<uint32_t>(skel + w * 4);
    At<uint32_t>(kCalced + w * 4) = s;
    if (~s) done = false;
  }
  if (done) return;
  for (int w = 0; w < 4; w++) {
    At<uint32_t>(skel + w * 4) |= mask[w];
    At<uint32_t>(kSkip + w * 4) = At<uint32_t>(kCalced + w * 4);
  }
  At<uint32_t>(kNumModels) = At<uint8_t>(dobj + 0x16);
  At<uint32_t>(kModels) = dobj + 0x18;
  At<uint32_t>(kDObj) = dobj;
  const uint32_t tree = At<uint32_t>(dobj);
  At<uint32_t>(kTree) = tree;
  const uint32_t n = At<uint8_t>(dobj + 0x17);
  At<uint32_t>(kNumBones) = n;
  uint32_t out = skel + 0x30 + n * 0x40;
  if (tree) {
    At<uint32_t>(kTempSize) = n << 5;
    At<uint8_t>(kSkip + 15) = At<uint8_t>(kCalced + 15) | 0x80;
    Calc(c, 0, F32{0x3F800000u}, out, true, false);
  }
  int32_t b = 0;
  for (int32_t k = 0; k < At<int32_t>(kNumModels); k++) {
    const uint32_t parts = ModelParts(At<uint32_t>(Global(kModels) + k * 4));
    const int32_t numRoot = At<int16_t>(parts + 4);
    for (int32_t r = numRoot; r != 0; r--, b++, out += 0x20) {
      if (Bit(kCalced, b)) continue;
      Pose& o = At<Pose>(out);
      o.quat[0] = o.quat[1] = o.quat[2] = F32{0};
      o.quat[3] = F32{0x3F800000u};
      o.trans[0] = o.trans[1] = o.trans[2] = F32{0};
    }
    uint32_t base = At<uint32_t>(parts + 0xC);
    for (int32_t r = At<int16_t>(At<uint32_t>(At<uint32_t>(parts))) - numRoot; r != 0;
         r--, b++, out += 0x20, base += 8) {
      if (Bit(kCalced, b)) continue;
      Pose& o = At<Pose>(out);
      for (int i = 0; i < 4; i++) o.quat[i] = Fst(FromInt(At<int16_t>(base + i * 2)) * kQuatScale);
      o.trans[0] = o.trans[1] = o.trans[2] = F32{0};
    }
  }
}

// ---- the skeleton -------------------------------------------------------------------

// q = p q (0x476070) and q = q p (0x476100).
void QuatMultiplyLeft(F32* q, const F32* p) {
  const St a = ((St(q[3]) * p[0] + St(p[1]) * q[2]) + St(p[3]) * q[0]) - St(p[2]) * q[1];
  const St b = ((St(p[3]) * q[1] - St(p[0]) * q[2]) + St(q[3]) * p[1]) + St(p[2]) * q[0];
  const F32 c = Fst(((St(p[3]) * q[2] + St(q[1]) * p[0]) - St(p[1]) * q[0]) + St(q[3]) * p[2]);
  const St d = ((St(p[3]) * q[3] - St(q[0]) * p[0]) - St(q[1]) * p[1]) - St(p[2]) * q[2];
  q[2] = c;
  q[3] = Fst(d);
  q[0] = Fst(a);
  q[1] = Fst(b);
}
void QuatMultiplyRight(F32* q, const F32* p) {
  const St a = ((St(p[3]) * q[0] + St(q[1]) * p[2]) + St(q[3]) * p[0]) - St(q[2]) * p[1];
  const St b = ((St(q[3]) * p[1] - St(q[0]) * p[2]) + St(p[3]) * q[1]) + St(q[2]) * p[0];
  const F32 c = Fst(((St(q[3]) * p[2] + St(p[1]) * q[0]) - St(q[1]) * p[0]) + St(p[3]) * q[2]);
  const St d = ((St(q[3]) * p[3] - St(p[0]) * q[0]) - St(p[1]) * q[1]) - St(q[2]) * p[2];
  q[2] = c;
  q[3] = Fst(d);
  q[0] = Fst(a);
  q[1] = Fst(b);
}

// The rotation matrix of quaternion q, which need not be normalised
// (0x475D80): its three axes, a zero after each.
void QuatToAxis(const F32* q, F32* m) {
  F32 t0 = Fst(St(q[0]) * St(q[0]));
  F32 t1 = Fst(St(q[1]) * St(q[1]));
  F32 t2 = Fst(St(q[2]) * St(q[2]));
  const St len2 = ((St(q[3]) * St(q[3]) + t2) + t1) + t0;
  if (len2 == 0.0f) {
    const F32 one{0x3F800000u}, zero{0};
    m[0] = one, m[1] = zero, m[2] = zero, m[4] = zero, m[5] = one, m[6] = zero;
    m[8] = zero, m[9] = zero, m[10] = one, m[3] = zero, m[7] = zero, m[11] = zero;
    return;
  }
  const St s = 2.0f / len2;
  const St xx = St(t0) * s, yy = s * t1, zz = s * t2;
  const St x = s * q[0];
  const St xy = x * q[1];
  t1 = Fst(x * q[2]);
  const F32 xw = Fst(x * q[3]);
  const St y = s * q[1];
  t0 = Fst(y * q[2]);
  t2 = Fst(y * q[3]);
  const St zw = (St(q[3]) * q[2]) * s;
  m[3] = m[7] = m[11] = F32{0};
  m[0] = Fst(1.0f - (zz + yy));
  m[1] = Fst(zw + xy);
  m[2] = Fst(St(t1) - t2);
  m[4] = Fst(xy - zw);
  m[5] = Fst(1.0f - (zz + xx));
  m[6] = Fst(St(t0) + xw);
  m[8] = Fst(St(t2) + t1);
  m[9] = Fst(St(t0) - xw);
  m[10] = Fst(1.0f - (yy + xx));
}

// The DObj's bone matrices for the bones in mask not made yet (0x4766E0),
// from their poses: each rotation taken into its parent's (the bone it
// hangs from for a model's roots), parent first, for the bones whose
// rotation is set elsewhere the other way round; a duplicated bone copies
// its source's rotation, translation and matrix.
void CalcSkel(Ctx& c, const uint32_t* mask, uint32_t dobj) {
  const uint32_t skel = At<uint32_t>(dobj + 4);
  // The original's three bit arrays: compute, skip, keep.
  uint32_t compute[4], skip[4], keep[4];
  bool done = true;
  for (int w = 0; w < 4; w++) {
    skip[w] = ~mask[w] | At<uint32_t>(skel + 0x20 + w * 4);
    if (~skip[w]) done = false;
  }
  if (done) return;
  if (!At<int16_t>(dobj + 0x14)) {
    Push(c, dobj);
    Call(c, f_004761F0, 0x47676C);
    c.esp += 4;
  }
  const uint16_t handle = At<uint16_t>(dobj + 0x14);
  const uint32_t dup = handle ? Global(kStrings) + handle * 8u + 4 : 0;
  for (int w = 0; w < 4; w++) {
    At<uint32_t>(skel + 0x20 + w * 4) |= mask[w];
    const uint32_t set = At<uint32_t>(skel + 0x10 + w * 4) & ~skip[w];
    keep[w] = At<uint32_t>(dup + w * 4) | set | skip[w];
    compute[w] = set | ~keep[w];
  }
  const uint32_t numModels = At<uint8_t>(dobj + 0x16);
  const uint32_t matrices = skel + 0x30;
  const uint32_t poses = matrices + At<uint8_t>(dobj + 0x17) * 0x40u;
  auto pose = [&](int32_t b) -> Pose& { return At<Pose>(poses + b * 0x20u); };
  auto matrix = [&](int32_t b) { return &At<F32>(matrices + b * 0x40u); };
  // The next duplicated bone, if it is bone b: true when b is to be copied.
  uint32_t next;
  auto duplicate = [&](int32_t b, int32_t& source) {
    if (b != At<uint8_t>(next) - 1) return false;
    next += 2;
    source = At<uint8_t>(next - 1) - 1;
    return !Bit(skip, b);
  };

  // Rotations.
  next = dup + 0x10;
  int32_t b = 0;
  for (uint32_t k = 0; k < numModels; k++) {
    const uint32_t parts = ModelParts(At<uint32_t>(dobj + 0x18 + k * 4));
    const uint8_t parent = At<uint8_t>(dobj + 0x48 + k);
    const int32_t numRoot = At<int16_t>(parts + 4);
    int32_t source;
    for (int32_t r = numRoot; r != 0; r--, b++) {
      if (parent == 0xFF) {
        if (!Bit(compute, b) && duplicate(b, source))
          for (int i = 0; i < 4; i++) pose(b).quat[i] = pose(source).quat[i];
      } else if (!Bit(keep, b)) {
        QuatMultiplyLeft(pose(b).quat, pose(parent).quat);
      } else if (Bit(compute, b)) {
        QuatMultiplyRight(pose(b).quat, pose(parent).quat);
      }
    }
    const uint32_t bones = At<uint32_t>(parts);
    uint32_t up = bones + 4;
    for (int32_t r = At<int16_t>(At<uint32_t>(bones)) - numRoot; r != 0; r--, b++, up++) {
      if (!Bit(keep, b))
        QuatMultiplyLeft(pose(b).quat, pose(b - At<uint8_t>(up)).quat);
      else if (Bit(compute, b))
        QuatMultiplyRight(pose(b).quat, pose(b - At<uint8_t>(up)).quat);
      else if (duplicate(b, source))
        for (int i = 0; i < 4; i++) pose(b).quat[i] = pose(source).quat[i];
    }
  }

  // Matrices and the translations into the model's space.
  next = dup + 0x10;
  b = 0;
  auto finish = [&](int32_t b) {
    F32* m = matrix(b);
    for (int i = 0; i < 3; i++) m[12 + i] = pose(b).trans[i];
    m[15] = F32{0};
  };
  auto copy = [&](int32_t b, int32_t source) {
    for (int i = 0; i < 3; i++) pose(b).trans[i] = pose(source).trans[i];
    for (int i = 0; i < 16; i++) matrix(b)[i] = matrix(source)[i];
  };
  for (uint32_t k = 0; k < numModels; k++) {
    const uint32_t parts = ModelParts(At<uint32_t>(dobj + 0x18 + k * 4));
    const uint8_t parent = At<uint8_t>(dobj + 0x48 + k);
    const int32_t numRoot = At<int16_t>(parts + 4);
    int32_t source;
    for (int32_t r = numRoot; r != 0; r--, b++) {
      if (parent == 0xFF) {
        if (Bit(compute, b)) {
          QuatToAxis(pose(b).quat, matrix(b));
          finish(b);
        } else if (duplicate(b, source)) {
          copy(b, source);
        }
      } else if (Bit(compute, b)) {
        QuatToAxis(pose(b).quat, matrix(b));
        const F32* pm = matrix(parent);
        F32* t = pose(b).trans;
        const St x = ((St(pm[8]) * t[2] + St(pm[4]) * t[1]) + St(pm[0]) * t[0]) + pm[12];
        const St y = ((St(pm[9]) * t[2] + St(pm[5]) * t[1]) + St(pm[1]) * t[0]) + pm[13];
        const St z = ((St(pm[10]) * t[2] + St(pm[6]) * t[1]) + St(pm[2]) * t[0]) + pm[14];
        t[2] = Fst(z);
        t[0] = Fst(x);
        t[1] = Fst(y);
        finish(b);
      }
    }
    const uint32_t bones = At<uint32_t>(parts);
    uint32_t up = bones + 4;
    const F32* base = &At<F32>(At<uint32_t>(parts + 0x10));
    for (int32_t r = At<int16_t>(At<uint32_t>(bones)) - numRoot; r != 0;
         r--, b++, up++, base += 3) {
      if (Bit(compute, b)) {
        QuatToAxis(pose(b).quat, matrix(b));
        F32* t = pose(b).trans;
        for (int i = 0; i < 3; i++) t[i] = Fst(St(base[i]) + t[i]);
        const F32* pm = matrix(b) - At<uint8_t>(up) * 16;
        const St x = ((St(pm[8]) * t[2] + St(pm[4]) * t[1]) + St(t[0]) * pm[0]) + pm[12];
        const St y = ((St(pm[1]) * t[0] + St(pm[9]) * t[2]) + St(pm[5]) * t[1]) + pm[13];
        const St z = ((St(pm[2]) * t[0] + St(pm[10]) * t[2]) + St(pm[6]) * t[1]) + pm[14];
        t[2] = Fst(z);
        t[0] = Fst(x);
        t[1] = Fst(y);
        finish(b);
      } else if (duplicate(b, source)) {
        copy(b, source);
      }
    }
  }
}

}  // namespace

// 0x47CD80 (see CalcAnim): EAX dobj, EDX the bone mask (uint32[4]).
void f_0047CD80(Ctx& c) {
  const uint32_t* mask = &At<uint32_t>(c.edx);
  const uint32_t m[4] = {mask[0], mask[1], mask[2], mask[3]};
  CalcAnim(c, c.eax, m);
  c.esp += 4;
}

// 0x4766E0 (see CalcSkel): EAX the bone mask (uint32[4]); dobj on the stack.
void f_004766E0(Ctx& c) {
  const uint32_t* mask = &At<uint32_t>(c.eax);
  const uint32_t m[4] = {mask[0], mask[1], mask[2], mask[3]};
  CalcSkel(c, m, At<uint32_t>(c.esp + 4));
  c.esp += 4;
}

}  // namespace recomp::gen
