// Shader-ISA census + in-app translation coverage (branch native-gpu).
//
// The census's shader surface (ALU vector/scalar, fetch, control-flow opcodes)
// was PARKED because instrumenting it needed a microcode parser, and a hand
// parser that mis-decodes an op poisons the census. This module uses the SDK's
// OWN parser instead: rex::graphics::ShaderTranslator walks the analysed
// microcode and calls Process*Instruction per op - a subclass that records the
// opcodes is the SEEN axis with no decode of ours in it. The same shader is then
// run through the SDK's DxbcShaderTranslator compiled INTO this app (see
// src/native_gpu_xlat/): a shader that translates marks every op it contains
// PORTED by that in-app handler (with the honest ledger entry: the plugin's own
// copy still translates the same shaders for the main window - DUPLICATED, not
// replaced, until the native draw path consumes the in-app DXBC).
#pragma once
#include <cstddef>
#include <cstdint>

namespace fable2 {
namespace ngpu {
namespace shader_census {

// A guest shader's microcode as the GPU loads it: `be_dwords` big-endian in
// guest memory, `n` dwords. Deduplicated by content hash; the work (analysis +
// translation) runs on the module's own worker thread, never the caller's.
// Gated on ngpu_census (the one switch for all coverage instrumentation).
// `from_object`: the microcode came from the XDK shader object at the SetShader
// hook (the D3D9-level source) rather than from the ring parse (an instrument
// of the parser, off by default - it hallucinates loads out of inline data).
void Note(bool pixel, const uint32_t* be_dwords, size_t n, uint32_t guest_addr, bool from_object = false);

// One summary line + the first failures, to the log. Called by the census reporter.
void Report();

}  // namespace shader_census
}  // namespace ngpu
}  // namespace fable2
