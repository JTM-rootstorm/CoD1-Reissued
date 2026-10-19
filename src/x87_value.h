#pragma once
// x87 arithmetic of the main thread, in host doubles, for native
// replacements that must give the lifted original's bits (src/cm_trace.cpp).
//
// CoD runs its x87 code at control word 0x027F: 53-bit precision, round to
// nearest, all exceptions masked (FINDINGS.md, Run log, "faster exact
// x87"). A value on the x87 stack is then an IEEE double with a wider
// exponent range: every add, subtract, multiply, divide and square root
// rounds to 53 bits exactly as IEEE double arithmetic does, an m32 load
// is exact, and an m32 store rounds the double to float. So St (a value on
// the x87 stack) is a double, F32 (a float in guest memory, an m32
// operand) is stored with float(), and the results are the original's as
// long as no value leaves the double range, where the x87 would keep a
// wider exponent. The callers' inputs are floats (below 2^128 and above
// 2^-149), and none of them takes them through enough products or
// quotients to come near 2^1024 or 2^-1022.
//
// NaNs follow the x87, not the host: an operation with a NaN result is
// redone here (NanResult). One NaN operand gives that NaN, quietened; of
// two, a QNaN beats a memory SNaN, else the larger significand wins and
// equal ones give the positive NaN (Intel SDM vol. 1, table 4-7, as
// x86recomp's x87 model has it); an invalid operation gives the real
// indefinite, -QNaN. SSE (first operand) and arm64 (first operand, default
// NaN positive) differ. Loads quieten an SNaN, as FLD m32 does; an SNaN
// matters only as the memory operand of an operation, so the operators
// taking F32 look at its bits.
//
// F32 has no arithmetic or conversions of its own: an expression says
// which operand is on the stack, as the original does. `St(a) + b` is
// fld a / fadd b; `a + b` with two F32s does not compile, so nothing is
// computed in float precision by mistake. A float literal is an .rdata
// constant (never a NaN). Comparisons are C++'s, which are false for
// unordered operands like the x87 flag tests they replace (each site in
// the replacements is written to keep the original's NaN branch).
//
// Not modelled: the status word (exception flags, C0-C3) and the stale
// register contents below TOP. The game reads the flags only in the CRT's
// math-error path (FINDINGS.md, Run log, "collision traces").
//
// Needs -ffp-contract=off: contraction into FMA would skip roundings.

#include <bit>
#include <cmath>
#include <cstdint>

namespace recomp::gen {

struct F32 {
  uint32_t bits;
};
static_assert(sizeof(F32) == 4);

// A value on the x87 stack.
struct St {
  double v;
  St() = default;
  explicit constexpr St(double d) : v(d) {}
  constexpr St(float f) : v(f) {}  // FLD of an .rdata constant
  St(F32 m) : v(std::bit_cast<float>(m.bits)) {}  // FLD m32 (SNaN quietened)
};

// FST m32: round to float. A NaN stored is quiet: the x87 holds no SNaN,
// and the compiler may fold a float's conversion to double and back
// (Fst(St(m))) into a copy that would keep one.
inline F32 Fst(St v) {
  const float f = float(v.v);
  uint32_t b = std::bit_cast<uint32_t>(f);
  if (f != f) [[unlikely]]
    b |= 0x400000u;
  return {b};
}

namespace x87v_detail {

constexpr uint64_t kSignBit = 1ull << 63;
constexpr uint64_t kQuietBit = 1ull << 51;
constexpr uint64_t kFraction = (1ull << 52) - 1;
constexpr double kIndefinite = std::bit_cast<double>(0xFFF8000000000000ull);

inline bool IsSNaN32(uint32_t b) {
  return (b & 0x7F800000u) == 0x7F800000u && (b & 0x7FFFFFu) && !(b & 0x400000u);
}
// An m32 SNaN, quietened as the stack holds it.
inline double Quiet(double d) {
  return std::bit_cast<double>(std::bit_cast<uint64_t>(d) | kQuietBit);
}

// The result of `a op b` when the host's is a NaN. a_snan / b_snan: that
// operand is an m32 SNaN (its double is already quiet).
[[gnu::noinline]] inline double NanResult(double a, bool a_snan, double b, bool b_snan) {
  bool na = a != a, nb = b != b;
  if (!na && !nb) return kIndefinite;
  if (!nb) return Quiet(a);
  if (!na) return Quiet(b);
  if (a_snan != b_snan) return Quiet(a_snan ? b : a);
  uint64_t ua = std::bit_cast<uint64_t>(a), ub = std::bit_cast<uint64_t>(b);
  if ((ua & kFraction) != (ub & kFraction))
    return Quiet((ua & kFraction) > (ub & kFraction) ? a : b);
  return Quiet(ua & kSignBit ? b : a);
}

}  // namespace x87v_detail

#define COD1_X87_OP(op)                                                                 \
  inline St operator op(St a, St b) {                                                   \
    double r = a.v op b.v;                                                              \
    if (r != r) [[unlikely]]                                                            \
      r = x87v_detail::NanResult(a.v, false, b.v, false);                               \
    return St(r);                                                                       \
  }                                                                                     \
  inline St operator op(St a, F32 m) {                                                  \
    double b = St(m).v, r = a.v op b;                                                   \
    if (r != r) [[unlikely]]                                                            \
      r = x87v_detail::NanResult(a.v, false, b, x87v_detail::IsSNaN32(m.bits));         \
    return St(r);                                                                       \
  }                                                                                     \
  inline St operator op(F32 m, St b) {                                                  \
    double a = St(m).v, r = a op b.v;                                                   \
    if (r != r) [[unlikely]]                                                            \
      r = x87v_detail::NanResult(a, x87v_detail::IsSNaN32(m.bits), b.v, false);         \
    return St(r);                                                                       \
  }                                                                                     \
  inline St operator op(St a, float b) { return a op St(b); }                           \
  inline St operator op(float a, St b) { return St(a) op b; }
COD1_X87_OP(+)
COD1_X87_OP(-)
COD1_X87_OP(*)
COD1_X87_OP(/)
#undef COD1_X87_OP

inline St operator-(St a) { return St(-a.v); }            // FCHS
inline St Abs(St a) { return St(std::fabs(a.v)); }        // FABS
inline St Sqrt(St a) {                                    // FSQRT
  double r = std::sqrt(a.v);
  if (r != r) [[unlikely]]
    r = a.v != a.v ? x87v_detail::Quiet(a.v) : x87v_detail::kIndefinite;
  return St(r);
}

// FCOM and friends: C++ comparisons (false when unordered).
#define COD1_X87_CMP(op)                                                   \
  inline bool operator op(St a, St b) { return a.v op b.v; }               \
  inline bool operator op(St a, F32 b) { return a.v op St(b).v; }          \
  inline bool operator op(F32 a, St b) { return St(a).v op b.v; }          \
  inline bool operator op(St a, float b) { return a.v op double(b); }      \
  inline bool operator op(F32 a, float b) { return St(a).v op double(b); }
COD1_X87_CMP(<)
COD1_X87_CMP(>)
COD1_X87_CMP(<=)
COD1_X87_CMP(>=)
COD1_X87_CMP(==)
COD1_X87_CMP(!=)
#undef COD1_X87_CMP

}  // namespace recomp::gen
