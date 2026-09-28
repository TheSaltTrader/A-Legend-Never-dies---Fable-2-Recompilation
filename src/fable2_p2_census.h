/**
 * @file        fable2_p2_census.h
 * @brief       P2 ATTRIBUTION CENSUS, guest side (full-native plan, 2026-09-27): which Direct3D library entry point
 *              (or direct ring site) produced each PM4 packet. At the ENTRY and every EXIT of the 304 hooked XDK
 *              functions (config/hooks/native_gpu_trace.toml; exits injected into the generated code by
 *              tools/native_gpu/p2_inject_exits.py) the XDK device's ring write cursor is read (device+0x30, the
 *              push pointer every draw entry point advances, and device+0x3484, the copy some inline paths commit
 *              through - both, per the Fable II notes), so each call owns a byte range of whatever command buffer
 *              the library was writing. The plugin records every packet's address (fork command_processor.cpp,
 *              FABLE2_P2) and tools/native_gpu/p2_census.py joins the two by frame and address.
 *
 *              Off unless FABLE2_P2=<first frame>:<frames> is set (frames counted by the swap entry point, the same
 *              count the plugin keeps by swap packet). FABLE2_P2_CURSOR / FABLE2_P2_CURSOR2 override the two offsets;
 *              FABLE2_P2_DISCOVER=1 logs the device object's first 32 words at the first calls, for finding the
 *              cursor on another build. Output: p2_guest.bin in the working directory.
 */

#pragma once

#include <cstdint>

namespace fable2::p2 {

// From the trace hook at the first instruction of hooked function `hook` (index into native_gpu_trace.cpp's
// kEntries); r3 = the first argument (the device for library entry points, `lib` true), which is how the device
// is learned at the FIRST library call - the library writes its persistent packet templates (NG2 found a 24-draw
// block sub_8373B060 emits once) at device creation, long before the first swap.
void Enter(int hook, uint32_t r3, bool lib, const uint32_t* args8 = nullptr);   // args8: r3..r10 at entry
// From the injected call before each `return;` of the same function.
void Exit(int hook);
// From the frame marker hook (the swap entry point), once per guest frame; r3 = the device.
void FrameMarker(uint32_t r3);

// P3 REGISTER-MAP DISCOVERY (FABLE2_P3MAP=<guest frame>:<max calls>, needs FABLE2_P2 on): at the exit of every call
// in that guest frame whose cursor advanced (up to max), the XDK device object (0x5000 bytes, big-endian) is written
// to p3_guest.bin with the call's cursor range; every bridge draw whose packet address falls in a recorded range
// writes its register file (0x2000-0x23FF, 0x4000-0x4927) to p3_bridge.bin. Offline, each register is matched to
// the device offset that always holds its value (tools/native_gpu/p3_regmap.py).
void BridgeDraw(uint32_t packet_addr, const uint32_t* regs, uint32_t reg_count, uint32_t draw_initiator);
// From the bridge's swap callback (GPU thread): restarts the bridge's per-address execution ordinals.
void BridgeSwap();
// [p3 draw] FABLE2_P3DRAW: the front end draws; the bridge asks so its plugin callbacks go compare-only.
bool FrontEndDraws();
bool FrontEndRecorderMode();
// [gs] The game's own graphics system: the kick (wptr 0xFFFFFFFF = ring re-initialised) and the front end's start.
void NativeKick(uint32_t ring_ptr, uint32_t ring_bytes, uint32_t wptr);
bool StartNativeFrontEnd();   // design (b): the recorder thread draws, at the plugin's read point
// [p3 rec] FABLE2_P3DRAW=2: the plugin's draw callback waits for the recorder; the swap callback reports.
void WaitRecorder(uint32_t packet_addr, uint32_t draw_initiator);
void RecorderSwapReport();
void FrontEndCounts(uint64_t& draws, uint64_t& swaps);

}  // namespace fable2::p2

extern "C" void fable2_p2_exit(int hook);
