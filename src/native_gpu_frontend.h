/**
 * @file        native_gpu_frontend.h
 * @brief       [p3 draw] (full-native plan stage 1b, 2026-09-27): what the guest-thread front end
 *              (fable2_p2_census.cpp, FABLE2_P3DRAW=1) hands the native bridge (native_gpu_present.cpp) at a DRAW
 *              packet and at XE_SWAP - the plugin's RexNgpuDraw minus the register-file pointer, which comes as the
 *              front end's own file plus a dirty bitmap of what it wrote. Ported from NG2 (alt fd2c392).
 */
#pragma once
#include <cstdint>

namespace fable2::ngpu {
struct FeDrawInfo {
  uint32_t draw_initiator, index_addr, index_size;
  uint32_t vs_addr, vs_dwords, ps_addr, ps_dwords;
  bool vs_inline, ps_inline;
  const uint8_t* vs_code;   // inline microcode (big-endian dwords), valid for the call
  const uint8_t* ps_code;
  uint32_t vs_code_dwords, ps_code_dwords;
};
void FrontEndDraw(const uint32_t* regs, uint64_t* dirty, const FeDrawInfo& d);
void FrontEndSwap(uint32_t fb, uint32_t w, uint32_t h, const uint32_t* regs, uint64_t* dirty);
// [gs] A register the backend must see at once (the gamma port), buffered until the backend exists.
void FrontEndRegisterNow(uint32_t reg, uint32_t value);
}  // namespace fable2::ngpu
