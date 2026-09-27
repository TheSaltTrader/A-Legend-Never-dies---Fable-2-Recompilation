// Native-GPU draw dump: per-draw records read straight from the XDK device
// struct at the Direct3D draw entry points (see ng2recomp/docs/native_gpu/
// M4_fable2.md for the device map). Called from the generated tracer hooks in
// native_gpu_trace.cpp; every function takes plain guest register values.
#pragma once

#include <cstdint>

namespace ngpu {

// sub_8221DFC0 DrawIndexedVertices(dev, primType, baseVertexIndex, startIndex, indexCount)
void OnDrawIndexed(uint32_t dev, uint32_t prim, uint32_t base_vertex, uint32_t start, uint32_t count);
// 0x8221E408: DrawIndexedVertices' exit - the draw's state and DRAW packets are in the ring; the native draw runs here.
void OnDrawIndexedDone(uint32_t r3);
// sub_8221C3E8 DrawVertices(dev, primType, startVertex, vertexCount)
void OnDrawVertices(uint32_t dev, uint32_t prim, uint32_t start, uint32_t count);
// 0x8221C7D0: DrawVertices' exit - the native non-indexed draw runs here.
void OnDrawVerticesDone(uint32_t r3);
void OnDrawUPDone(uint32_t r3);
void OnDrawUPBegin(uint32_t dev, uint32_t prim, uint32_t min_index, uint32_t num_vertices, uint32_t index_count, uint32_t idx_ptr, uint32_t idx_fmt, uint32_t vdata, uint32_t r1);
void OnDrawUPEnd(uint32_t r3);
// sub_82217DB8 DrawVerticesUP(dev, primType, r5, r6, r7, r8, r9, r10)
void OnDrawUP(uint32_t dev, uint32_t prim, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, uint32_t r1);
// sub_82221858 SetShader(dev, shader, type)
void OnSetShader(uint32_t dev, uint32_t shader, uint32_t type);
// sub_82221B90 LoadShaderConstants(dev, table, base, base2, n)
void OnLoadConstants(uint32_t dev, uint32_t table, uint32_t base, uint32_t base2, uint32_t n);
// sub_822192E8 SetRenderTarget(dev, surface, index)
void OnSetRenderTarget(uint32_t dev, uint32_t surface, uint32_t index);
// sub_82196750 Resolve(dev, flags, srcRect, destTexture, destPoint, destLevel, destSlice, clearColor)
void OnResolve(uint32_t dev, uint32_t flags, uint32_t src_rect, uint32_t dest, uint32_t dest_point, uint32_t level, uint32_t slice, uint32_t clear);
// sub_82206888 D3DDevice_Resolve(dev, flags, pSourceRect, pDestTexture, pDestPoint, destLevel, destSliceOrFace, pClearColor, ...)
void OnResolveXdk(uint32_t dev, uint32_t flags, uint32_t src_rect, uint32_t dest, uint32_t dest_point, uint32_t level, uint32_t slice, uint32_t clear);
// sub_82BA34D8 Present(dev, ...)
void OnPresent(uint32_t dev);
// The vertex shader object last passed to SetVertexShader (render thread),
// and the variant entry the shader-load flush (sub_82221858) last received
// for it (0 when the flush took its first path).
uint32_t CurrentVertexShader();
uint32_t CurrentVertexShaderEntry();
// The surface object last bound as render target 0 (SetRenderTarget hook).
uint32_t CurrentRenderTarget();
// The pixel shader object last passed to SetPixelShader (render thread).
uint32_t CurrentPixelShader();

}  // namespace ngpu
