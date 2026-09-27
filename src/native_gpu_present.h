// Native-GPU M4: the Plume shadow window (native_gpu_present.cpp).
#pragma once

#include <cstdint>

namespace ngpu {
// Called from ngpu::OnPresent (native_gpu_dump.cpp) on every guest Present:
// ends the shadow frame (present) and opens the next one.
void ShadowPresent();
bool DrawAtExit();  // ngpu_draw_at_exit
bool DrawUpEnabled();  // ngpu_draw_up
void DeferUP(uint32_t dev, uint32_t prim, uint32_t min_index, uint32_t num_vertices, uint32_t index_count, uint32_t stride, uint32_t vptr, uint32_t iptr);
void FlushDeferredUP();
void ShadowDrawUP(uint32_t dev, uint32_t prim, uint32_t num_vertices, uint32_t stride, uint32_t vdata, uint32_t index_count);
void ShadowDrawUP2(uint32_t dev, uint32_t prim, uint32_t min_index, uint32_t num_vertices, uint32_t index_count, uint32_t idx_ptr, uint32_t idx_fmt, uint32_t vdata, uint32_t stride);
// The XDK Resolve hook reports every destination texture: its pages are GPU-written (served white until the native path owns render targets).
void NoteResolveDest(uint32_t base, uint32_t w, uint32_t h, uint32_t fmt, uint32_t flags, uint32_t surface_info, uint32_t color_info);
// Called from ngpu::OnDrawIndexed on every guest DrawIndexedVertices: records
// the draw into the shadow frame natively (M4-b) when ngpu_native_draws is on.
void ShadowDrawIndexed(uint32_t dev, uint32_t prim, uint32_t base_vertex, uint32_t start, uint32_t count);
void ShadowDrawVertices(uint32_t dev, uint32_t prim, uint32_t start, uint32_t count);
void NoteIssuedDraw(int kind, uint32_t prim);  // 0 indexed, 1 non-indexed, 2 user-pointer
// The D3D library's own draw emitters (sub_82B9EEE0 / sub_82B9F038), which the
// game reaches through a path our three wrappers do not cover: report which
// native render target is bound when one fires.
void NoteLibraryDraw(uint32_t which, uint32_t r3, uint32_t r4, uint32_t r5);
}  // namespace ngpu
