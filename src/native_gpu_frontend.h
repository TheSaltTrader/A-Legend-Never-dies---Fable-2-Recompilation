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
  uint32_t vs_code_gen, ps_code_gen;   // [split] bumped at each IM_LOAD_IMMEDIATE, so inline code is sent once per change
};
void FrontEndDraw(const uint32_t* regs, uint64_t* dirty, const FeDrawInfo& d);
void FrontEndSwap(uint32_t fb, uint32_t w, uint32_t h, const uint32_t* regs, uint64_t* dirty);
// [gs] A register the backend must see at once (the gamma port), buffered until the backend exists.
void FrontEndRegisterNow(uint32_t reg, uint32_t value);

// [split] (2026-09-28 optimisation, ngpu_opt_split) the recording thread's work in two: the DECODE thread (the recorder)
// decodes and diffs registers; a DRAW thread applies them and records the D3D12 draws, in one ordered stream. Every
// call above is then queued, and each batch's side effects follow its draws, so a fence still follows the recording.
// Called by the decode thread at a batch boundary: applies the ngpu_opt_split switch (turning it off drains the stream).
void FrontEndSplitUpdate();
// LOAD_ALU_CONSTANT in split mode: the DRAW thread reads the guest memory at record time (the read point the decode
// thread would reach too early - FD A/B 09-27: an animation step early). False = not in split mode, read it yourself.
bool FrontEndLoadConstants(uint32_t guest_addr, uint32_t first_reg, uint32_t count, const uint32_t* regs, uint64_t* dirty);
// The batch's side effects (4-dword records): queued behind its draws in split mode (true), else false.
// more_pending: another batch is already queued (the chunk may wait for it; with none, it is handed over now).
bool FrontEndBatchEnd(const uint32_t* recs, uint32_t count4, void (*push)(const uint32_t*, uint32_t), bool more_pending);
}  // namespace fable2::ngpu
