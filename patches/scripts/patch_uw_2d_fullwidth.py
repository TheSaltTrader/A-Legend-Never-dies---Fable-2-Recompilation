"""s81 - full-width 2D quads keep their width at ultrawide. The fade to black
of a scene transition is a c8 quad drawn by the SAME shader pair as the HUD
(vs BD2477FEDE6BEAB1 / ps 95503A81AE52EA05, six vertices, a 16x16 texture),
so neither the shader nor a textureless test (s80) can tell it from the
health bar. The geometry can: the fade spans the whole 1280-pixel width of
the game's 2D space, a HUD element does not. For every c8 draw of at most
six host vertices the vertex x coordinates are read from guest memory (the
first attribute of the first vertex binding, float format, through the
draw's own index buffer when it is a guest one) and a span of 1200 pixels or
more keeps its width. The first 40 measured spans are logged ("[uw-2d] c8
quad span"). A pointer to the draw's primitive processing result is stashed
in IssueDraw for the binding code. APPLY ONCE (requires
patch_uw_c8_census.py)."""
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
H = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\include\rex\graphics\d3d12\command_processor.h"
q = open(Q, encoding="utf-8").read()
h = open(H, encoding="utf-8").read()
assert "uw_diag_vertex_count_" in h, "needs patch_uw_c8_census.py first"
assert "uw_ppr_" not in h, "already applied"

def rep(s, old, new):
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    return s.replace(old, new)

h = rep(h, '''  uint32_t uw_diag_vertex_count_ = 0;
''', '''  uint32_t uw_diag_vertex_count_ = 0;
  // [uw-2d] The current draw's primitive processing result (index buffer
  // kind, guest index base), valid during UpdateBindings only.
  const PrimitiveProcessor::ProcessingResult* uw_ppr_ = nullptr;
  // [uw-2d] Pixel x span of a c8 quad read from guest memory; < 0 = unknown.
  float C8QuadSpan(const D3D12Shader* vertex_shader, float& x_min, float& x_max) const;
''')

q = rep(q, '''  uw_diag_vertex_count_ = primitive_processing_result.host_draw_vertex_count;
  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
''', '''  uw_diag_vertex_count_ = primitive_processing_result.host_draw_vertex_count;
  uw_ppr_ = &primitive_processing_result;
  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
    uw_ppr_ = nullptr;
''')
q = rep(q, '''    DrawFailReason("UpdateBindings");
    return false;
  }
  // Must not call anything that can change the descriptor heap from now on!
''', '''    DrawFailReason("UpdateBindings");
    return false;
  }
  uw_ppr_ = nullptr;
  // Must not call anything that can change the descriptor heap from now on!
''')

# the span reader, placed before UpdateBindings' definition
q = rep(q, '''bool D3D12CommandProcessor::UpdateBindings(''', '''float D3D12CommandProcessor::C8QuadSpan(const D3D12Shader* vertex_shader, float& x_min,
                                        float& x_max) const {
  x_min = x_max = 0.0f;
  if (!uw_ppr_ || uw_ppr_->host_draw_vertex_count == 0 || uw_ppr_->host_draw_vertex_count > 6) {
    return -1.0f;
  }
  const auto& vbs = vertex_shader->vertex_bindings();
  if (vbs.empty() || vbs[0].attributes.empty()) return -1.0f;
  const auto& attr = vbs[0].attributes[0].fetch_instr.attributes;
  if (attr.data_format != xenos::VertexFormat::k_32_32_FLOAT &&
      attr.data_format != xenos::VertexFormat::k_32_32_32_FLOAT &&
      attr.data_format != xenos::VertexFormat::k_32_32_32_32_FLOAT) {
    return -1.0f;
  }
  const xenos::xe_gpu_vertex_fetch_t vf = register_file_->GetVertexFetch(vbs[0].fetch_constant);
  if (vf.type != xenos::FetchConstantType::kVertex || vf.size == 0) return -1.0f;
  const uint32_t stride = vbs[0].stride_words * 4;
  const uint32_t offset = uint32_t(attr.offset) * 4;
  if (stride == 0) return -1.0f;
  const uint8_t* base = memory_->TranslatePhysical(vf.address << 2);
  if (!base) return -1.0f;
  const uint8_t* indices = nullptr;
  bool indices_32 = false;
  if (uw_ppr_->index_buffer_type == PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA) {
    indices = memory_->TranslatePhysical(uw_ppr_->guest_index_base);
    if (!indices) return -1.0f;
    indices_32 = uw_ppr_->host_index_format == xenos::IndexFormat::kInt32;
  } else if (uw_ppr_->index_buffer_type != PrimitiveProcessor::ProcessedIndexBufferType::kNone) {
    return -1.0f;
  }
  const uint64_t buffer_bytes = uint64_t(vf.size) * 4;
  bool first = true;
  for (uint32_t i = 0; i < uw_ppr_->host_draw_vertex_count; ++i) {
    uint32_t index = i;
    if (indices) {
      if (indices_32) {
        uint32_t raw;
        std::memcpy(&raw, indices + size_t(i) * 4, 4);
        index = xenos::GpuSwap(raw, uw_ppr_->host_shader_index_endian);
      } else {
        uint16_t raw;
        std::memcpy(&raw, indices + size_t(i) * 2, 2);
        index = xenos::GpuSwap(raw, uw_ppr_->host_shader_index_endian);
      }
    }
    const uint64_t at = uint64_t(index) * stride + offset;
    if (at + 4 > buffer_bytes) return -1.0f;
    uint32_t raw_x;
    std::memcpy(&raw_x, base + at, 4);
    raw_x = xenos::GpuSwap(raw_x, vf.endian);
    float x;
    std::memcpy(&x, &raw_x, 4);
    if (!(x == x) || x < -1e5f || x > 1e5f) return -1.0f;
    if (first) { x_min = x_max = x; first = false; }
    else { x_min = std::min(x_min, x); x_max = std::max(x_max, x); }
  }
  return x_max - x_min;
}

bool D3D12CommandProcessor::UpdateBindings(''')

q = rep(q, '''          if (pixel_scale && !solid_fill) {
            // c8's packed position = the number of used registers below 8.
''', '''          // [uw-2d] Geometry: a quad spanning the 2D space's full width (the
          // fade to black, a full-screen tint) keeps its width; the HUD's
          // shader pair is the same, only the extent tells them apart.
          bool full_width = false;
          if (pixel_scale && !solid_fill) {
            float x0 = 0.0f, x1 = 0.0f;
            const float span = C8QuadSpan(vertex_shader, x0, x1);
            full_width = span >= 1200.0f;
            static uint32_t span_logged = 0;
            if (span_logged < 40) {
              ++span_logged;
              REXLOG_INFO("[uw-2d] c8 quad span {:.0f}..{:.0f} ({:.0f} px) verts {} indexed {} -> {}",
                          x0, x1, span, uw_diag_vertex_count_,
                          uw_ppr_ && uw_ppr_->index_buffer_type !=
                                         PrimitiveProcessor::ProcessedIndexBufferType::kNone,
                          span < 0.0f ? "unknown, compress" : full_width ? "full width" : "compress");
            }
          }
          if (pixel_scale && !solid_fill && !full_width) {
            // c8's packed position = the number of used registers below 8.
''')
open(H, "w", encoding="utf-8", newline="").write(h)
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: full-width c8 quads keep their width")
