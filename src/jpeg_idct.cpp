// jpeg_idct_float (0x519140) of the EXE's IJG libjpeg 6, natively: the
// inverse DCT of one 8x8 block, the only one that build has (FINDINGS.md
// "JPEG decoding stays lifted"; the rest of libjpeg stays lifted).
// Replacement of the lifted function named with `replace` in
// manifest/CoDSP.manifest; the name and evidence are in the Ghidra plate
// comment.
//
// It is the AA&N float IDCT (a column pass into a float workspace, then a
// row pass that descales and range-limits each sample), compiled for the
// x87 and run at CW 0x027F. The arithmetic is src/x87_value.h's: a value
// the original keeps on the x87 stack is an St, one it stores is an F32,
// and each expression keeps the original's operands and rounding points.
// They differ from libjpeg's source in places (the compiler keeps some of
// its float variables on the stack, unrounded), and the eight rows of the
// unrolled second pass all round at the same points. Coefficients times
// multipliers (16 x 24 bits) are exact in doubles. Built with
// -ffp-contract=off.
//
// Not reproduced: the original's workspace and locals in its guest stack
// frame (dead after it returns), EAX, ECX, EDX and EFLAGS on return, the
// x87 status word and stale registers.

#include <bit>
#include <cmath>
#include <cstdint>

#include "runtime/lifted.h"
#include "x87_value.h"

namespace recomp::gen {

namespace {

using lift::Rd;

// .rdata 0x55B548, 0x55B544, 0x55B540, 0x55B53C.
constexpr float k1_414 = std::bit_cast<float>(0x3FB504F3u);  // 1.414213562
constexpr float k1_848 = std::bit_cast<float>(0x3FEC835Eu);  // 1.847759065
constexpr float k2_613 = std::bit_cast<float>(0x40273D75u);  // 2.613125930
constexpr float k1_082 = std::bit_cast<float>(0x3F8A8BD4u);  // 1.082392200

// A sample: the CRT's float-to-integer conversion (0x52E218, _ftol2:
// truncation to 64 bits, the indefinite value out of range) of which the
// low 32 bits are descaled by 8 with rounding, then looked up in
// range_limit (cinfo->sample_range_limit + CENTERJSAMPLE) under
// RANGE_MASK.
uint8_t Sample(St v, const uint8_t* range_limit) {
  const uint64_t t =
      std::fabs(v.v) < 0x1p63 ? uint64_t(int64_t(v.v)) : 0x8000000000000000ull;
  const int32_t x = int32_t(uint32_t(t) + 4u) >> 3;
  return range_limit[x & 0x3FF];
}

// One column of pass 1: coef and q step by 8, so does ws.
void Column(const int16_t* coef, const F32* q, F32* ws) {
  auto d = [&](int k) { return St(double(coef[k * 8])) * q[k * 8]; };
  if (!(coef[8] | coef[16] | coef[24] | coef[32] | coef[40] | coef[48] | coef[56])) {
    const F32 dc = Fst(d(0));
    for (int k = 0; k < 8; k++) ws[k * 8] = dc;
    return;
  }
  // Even part.
  const F32 d6 = Fst(d(6));
  const F32 tmp10 = Fst(d(0) + d(4)), tmp11 = Fst(d(0) - d(4));
  const F32 tmp13 = Fst(d6 + d(2));
  const St tmp12 = (d(2) - d6) * k1_414 - tmp13;
  const St tmp0 = St(tmp10) + tmp13;
  const F32 tmp3 = Fst(tmp10 - St(tmp13));
  const F32 tmp1 = Fst(tmp12 + tmp11);
  const F32 tmp2 = Fst(tmp11 - tmp12);
  // Odd part.
  const F32 d7 = Fst(d(7));
  const F32 z13 = Fst(d(5) + d(3));
  const St z10 = d(5) - d(3);
  const F32 z11 = Fst(d7 + d(1)), z12 = Fst(d(1) - d7);
  const F32 tmp7 = Fst(z11 + St(z13));
  const F32 z5 = Fst((z12 + z10) * k1_848);
  const F32 tmp6 = Fst(z5 - z10 * k2_613 - tmp7);
  const F32 tmp5 = Fst((z11 - St(z13)) * k1_414 - tmp6);
  const F32 tmp4 = Fst(St(z12) * k1_082 - z5 + tmp5);

  ws[0] = Fst(tmp7 + tmp0);
  ws[56] = Fst(tmp0 - tmp7);
  ws[8] = Fst(tmp6 + St(tmp1));
  ws[48] = Fst(tmp1 - St(tmp6));
  ws[16] = Fst(tmp5 + St(tmp2));
  ws[40] = Fst(tmp2 - St(tmp5));
  ws[32] = Fst(tmp4 + St(tmp3));
  ws[24] = Fst(tmp3 - St(tmp4));
}

// One row of pass 2 into out[0..7].
void Row(const F32* x, uint8_t* out, const uint8_t* range_limit) {
  // Even part.
  const St tmp10 = x[0] + St(x[4]), tmp11 = x[0] - St(x[4]);
  const St tmp13 = x[2] + St(x[6]);
  const F32 tmp12 = Fst((x[2] - St(x[6])) * k1_414 - tmp13);
  const F32 tmp0 = Fst(tmp13 + tmp10);
  const F32 tmp3 = Fst(tmp10 - tmp13);
  const F32 tmp1 = Fst(tmp12 + tmp11);
  const St tmp2 = tmp11 - tmp12;
  // Odd part.
  const St z13 = x[3] + St(x[5]), z10 = x[5] - St(x[3]);
  const F32 z11 = Fst(x[1] + St(x[7])), z12 = Fst(x[1] - St(x[7]));
  const F32 tmp7 = Fst(z11 + z13);
  const St tmp11o = (z11 - z13) * k1_414;
  const F32 z5 = Fst((z12 + z10) * k1_848);
  const St tmp6 = z5 - z10 * k2_613 - tmp7;
  const F32 tmp5 = Fst(tmp11o - tmp6);
  const F32 tmp4 = Fst(St(z12) * k1_082 - z5 + tmp5);

  out[0] = Sample(tmp7 + St(tmp0), range_limit);
  out[7] = Sample(tmp0 - St(tmp7), range_limit);
  out[1] = Sample(tmp1 + tmp6, range_limit);
  out[6] = Sample(tmp1 - tmp6, range_limit);
  out[2] = Sample(tmp5 + tmp2, range_limit);
  out[5] = Sample(tmp2 - tmp5, range_limit);
  out[4] = Sample(tmp4 + St(tmp3), range_limit);
  out[3] = Sample(tmp3 - St(tmp4), range_limit);
}

template <typename T>
T* At(uint32_t va) {
  return reinterpret_cast<T*>(g_guest_base + va);
}

}  // namespace

// jpeg_idct_float(cinfo, compptr, coef_block, output_buf, output_col),
// cdecl.
void f_00519140(Ctx& c) {
  auto arg = [&](int n) { return Rd<uint32_t>(g_guest_base, c.esp + 4 + 4 * n); };
  const uint32_t cinfo = arg(0), compptr = arg(1), coef = arg(2), output_buf = arg(3),
                 output_col = arg(4);
  const F32* q = At<F32>(Rd<uint32_t>(g_guest_base, compptr + 0x50));  // dct_table
  const uint8_t* range_limit = At<uint8_t>(Rd<uint32_t>(g_guest_base, cinfo + 0x11C) + 0x80);
  F32 ws[64];
  for (int col = 0; col < 8; col++) Column(At<int16_t>(coef) + col, q + col, ws + col);
  for (int row = 0; row < 8; row++)
    Row(ws + row * 8, At<uint8_t>(Rd<uint32_t>(g_guest_base, output_buf + 4 * row) + output_col),
        range_limit);
  c.esp += 4;
}

}  // namespace recomp::gen
