#include "native_gpu_state_decode.h"

#include <rex/graphics/registers.h>

namespace fable2 {
namespace ngpu {
namespace state {

// Lifted VERBATIM from rexgpu-xenos src/graphics/d3d12/pipeline_cache.cpp
// (rexglue-src c94f5eb, BSD): PipelineCache::GetCurrentStateDescription. "32
// because of 0x1F mask, for safety (all unknown to zero)" - entries 17..31 are
// zero-initialised, i.e. kZero, and 2/3 are "?" -> kZero in the reference.
static const BlendFactor kBlendFactorMap[32] = {
    /*  0 */ BlendFactor::kZero,
    /*  1 */ BlendFactor::kOne,
    /*  2 */ BlendFactor::kZero,  // ?
    /*  3 */ BlendFactor::kZero,  // ?
    /*  4 */ BlendFactor::kSrcColor,
    /*  5 */ BlendFactor::kInvSrcColor,
    /*  6 */ BlendFactor::kSrcAlpha,
    /*  7 */ BlendFactor::kInvSrcAlpha,
    /*  8 */ BlendFactor::kDestColor,
    /*  9 */ BlendFactor::kInvDestColor,
    /* 10 */ BlendFactor::kDestAlpha,
    /* 11 */ BlendFactor::kInvDestAlpha,
    // CONSTANT_COLOR
    /* 12 */ BlendFactor::kBlendFactor,
    // ONE_MINUS_CONSTANT_COLOR
    /* 13 */ BlendFactor::kInvBlendFactor,
    // CONSTANT_ALPHA
    /* 14 */ BlendFactor::kBlendFactor,
    // ONE_MINUS_CONSTANT_ALPHA
    /* 15 */ BlendFactor::kInvBlendFactor,
    /* 16 */ BlendFactor::kSrcAlphaSat,
};
// Like kBlendFactorMap, but with color modes changed to alpha. Some
// pipelines aren't created in 545407E0 because a color mode is used for
// alpha.
static const BlendFactor kBlendFactorAlphaMap[32] = {
    /*  0 */ BlendFactor::kZero,
    /*  1 */ BlendFactor::kOne,
    /*  2 */ BlendFactor::kZero,  // ?
    /*  3 */ BlendFactor::kZero,  // ?
    /*  4 */ BlendFactor::kSrcAlpha,
    /*  5 */ BlendFactor::kInvSrcAlpha,
    /*  6 */ BlendFactor::kSrcAlpha,
    /*  7 */ BlendFactor::kInvSrcAlpha,
    /*  8 */ BlendFactor::kDestAlpha,
    /*  9 */ BlendFactor::kInvDestAlpha,
    /* 10 */ BlendFactor::kDestAlpha,
    /* 11 */ BlendFactor::kInvDestAlpha,
    /* 12 */ BlendFactor::kBlendFactor,
    // ONE_MINUS_CONSTANT_COLOR
    /* 13 */ BlendFactor::kInvBlendFactor,
    // CONSTANT_ALPHA
    /* 14 */ BlendFactor::kBlendFactor,
    // ONE_MINUS_CONSTANT_ALPHA
    /* 15 */ BlendFactor::kInvBlendFactor,
    /* 16 */ BlendFactor::kSrcAlphaSat,
};
// "8 entries for safety since 3 bits from the guest are passed directly."
static const BlendOp kBlendOpMap[8] = {
    BlendOp::kAdd, BlendOp::kSubtract, BlendOp::kMin, BlendOp::kMax, BlendOp::kRevSubtract,
    BlendOp::kAdd, BlendOp::kAdd, BlendOp::kAdd,
};

BlendFactor DecodeBlendFactor(uint32_t raw5, bool alpha) {
  return (alpha ? kBlendFactorAlphaMap : kBlendFactorMap)[raw5 & 31];
}

BlendOp DecodeBlendOp(uint32_t raw3) { return kBlendOpMap[raw3 & 7]; }

BlendControl DecodeBlendControl(uint32_t rb_blendcontrol) {
  rex::graphics::reg::RB_BLENDCONTROL r;
  r.value = rb_blendcontrol;
  BlendControl b;
  b.raw_color_src = uint32_t(r.color_srcblend);
  b.raw_color_dst = uint32_t(r.color_destblend);
  b.raw_alpha_src = uint32_t(r.alpha_srcblend);
  b.raw_alpha_dst = uint32_t(r.alpha_destblend);
  b.raw_color_op = uint32_t(r.color_comb_fcn);
  b.raw_alpha_op = uint32_t(r.alpha_comb_fcn);
  b.color_src = DecodeBlendFactor(b.raw_color_src, false);
  b.color_dst = DecodeBlendFactor(b.raw_color_dst, false);
  b.alpha_src = DecodeBlendFactor(b.raw_alpha_src, true);
  b.alpha_dst = DecodeBlendFactor(b.raw_alpha_dst, true);
  b.color_op = DecodeBlendOp(b.raw_color_op);
  b.alpha_op = DecodeBlendOp(b.raw_alpha_op);
  return b;
}

const char* BlendFactorName(BlendFactor f) {
  switch (f) {
    case BlendFactor::kZero: return "ZERO";
    case BlendFactor::kOne: return "ONE";
    case BlendFactor::kSrcColor: return "SRC_COLOR";
    case BlendFactor::kInvSrcColor: return "INV_SRC_COLOR";
    case BlendFactor::kSrcAlpha: return "SRC_ALPHA";
    case BlendFactor::kInvSrcAlpha: return "INV_SRC_ALPHA";
    case BlendFactor::kDestColor: return "DEST_COLOR";
    case BlendFactor::kInvDestColor: return "INV_DEST_COLOR";
    case BlendFactor::kDestAlpha: return "DEST_ALPHA";
    case BlendFactor::kInvDestAlpha: return "INV_DEST_ALPHA";
    case BlendFactor::kBlendFactor: return "BLEND_FACTOR";
    case BlendFactor::kInvBlendFactor: return "INV_BLEND_FACTOR";
    case BlendFactor::kSrcAlphaSat: return "SRC_ALPHA_SAT";
  }
  return "?";
}

}  // namespace state
}  // namespace ngpu
}  // namespace fable2
