// Xenos register -> render-state DECODERS, lifted from the plugin (branch native-gpu).
//
// The rexglue Xenos layer decodes registers into D3D12 state inside
// rexgpu-xenos.dll (d3d12/pipeline_cache.cpp); the native path had its own
// hand-written decoders. Per the method (replace emulation with native code,
// differential against the rexglue piece it replaces), the plugin's PURE
// decode tables are lifted here verbatim (BSD, rexglue-src c94f5eb - the
// commit matching the installed SDK) so the native path decodes exactly as the
// reference does, and the hand decoders are differential-tested against them.
//
// First finding from that differential (2026-09-22): the hand path used the
// COLOUR factor table for the ALPHA blend factors. The reference has a
// separate alpha table (SRC_COLOR->SRC_ALPHA, DEST_COLOR->DEST_ALPHA for raw
// 4/5/8/9) because a colour factor in an alpha slot is not a valid D3D12
// blend description ("some pipelines aren't created in 545407E0 because a
// color mode is used for alpha" - the reference's own comment).
#pragma once
#include <cstdint>

namespace fable2 {
namespace ngpu {
namespace state {

// The reference's intermediate blend-factor enum (PipelineBlendFactor in
// include/rex/graphics/d3d12/pipeline_cache.h @ c94f5eb), same order.
enum class BlendFactor : uint8_t {
  kZero, kOne, kSrcColor, kInvSrcColor, kSrcAlpha, kInvSrcAlpha, kDestColor, kInvDestColor,
  kDestAlpha, kInvDestAlpha, kBlendFactor, kInvBlendFactor, kSrcAlphaSat,
};
enum class BlendOp : uint8_t { kAdd, kSubtract, kMin, kMax, kRevSubtract };

// raw5 = the 5-bit Xenos blend factor field (xenos::BlendFactor); `alpha`
// selects the reference's alpha table (kBlendFactorAlphaMap) over the colour one.
BlendFactor DecodeBlendFactor(uint32_t raw5, bool alpha);
// raw3 = the 3-bit Xenos blend op field (xenos::BlendOp); 5..7 decode to add (reference table).
BlendOp DecodeBlendOp(uint32_t raw3);

// RB_BLENDCONTROL decoded through the SDK's own reg:: bitfield struct (the
// header is the layout oracle), factors through the tables above.
struct BlendControl {
  BlendFactor color_src, color_dst, alpha_src, alpha_dst;
  BlendOp color_op, alpha_op;
  uint32_t raw_color_src, raw_color_dst, raw_alpha_src, raw_alpha_dst;  // the 5-bit fields, for the differential
  uint32_t raw_color_op, raw_alpha_op;
};
BlendControl DecodeBlendControl(uint32_t rb_blendcontrol);

const char* BlendFactorName(BlendFactor f);

}  // namespace state
}  // namespace ngpu
}  // namespace fable2
