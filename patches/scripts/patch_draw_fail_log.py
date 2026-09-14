"""Diagnostics: a draw that fails in the backend says WHY, once per 500 per
reason (the "Failed in backend" line names only the draw). The silent
returns: submission, primitive processing, render targets, pipeline,
bindings, and inside the bindings the constant-buffer requests. Keep.
APPLY ONCE."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
s = open(P, encoding="utf-8").read()
assert "DrawFailReason(" not in s, "already applied"

def rep(old, new, count=1):
    global s
    assert s.count(old) == count, ("expected %d, found %d: %s" % (count, s.count(old), old[:90]))
    s = s.replace(old, new)

# the helper, before IssueDraw
rep("bool D3D12CommandProcessor::IssueDraw(", '''// [diag] Which silent failure a draw hit; rate-limited per reason.
static void DrawFailReason(const char* what) {
  static std::atomic<uint32_t> counts[16];
  static const char* names[16] = {};
  uint32_t slot = 0;
  for (; slot < 16; ++slot) {
    if (names[slot] == what) break;
    if (names[slot] == nullptr) { names[slot] = what; break; }
  }
  if (slot >= 16) return;
  const uint32_t n = ++counts[slot];
  if (n == 1 || n % 500 == 0) REXGPU_ERROR("[diag] draw failed: {} ({} so far)", what, n);
}

bool D3D12CommandProcessor::IssueDraw(''')

rep('''  if (!BeginSubmission(true)) {
    return false;
  }

  // Process primitives.
''', '''  if (!BeginSubmission(true)) {
    DrawFailReason("BeginSubmission");
    return false;
  }

  // Process primitives.
''')
rep('''  if (!primitive_processor_->Process(primitive_processing_result)) {
    return false;
  }
''', '''  if (!primitive_processor_->Process(primitive_processing_result)) {
    DrawFailReason("primitive processing");
    return false;
  }
''')
rep('''  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
    return false;
  }
''', '''  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
    DrawFailReason("UpdateBindings");
    return false;
  }
''')
# the constant buffer requests inside UpdateBindings: descriptor indices
rep('''          &cbuffer_binding_descriptor_indices_vertex_.address));
      if (!descriptor_indices) {
        return false;
      }
''', '''          &cbuffer_binding_descriptor_indices_vertex_.address));
      if (!descriptor_indices) {
        DrawFailReason("descriptor indices cbuffer (vertex)");
        return false;
      }
''')
rep('''          &cbuffer_binding_descriptor_indices_pixel_.address));
      if (!descriptor_indices) {
        return false;
      }
''', '''          &cbuffer_binding_descriptor_indices_pixel_.address));
      if (!descriptor_indices) {
        DrawFailReason("descriptor indices cbuffer (pixel)");
        return false;
      }
''')

rep('''  if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                    normalized_color_mask, *vertex_shader)) {
    return false;
  }
''', '''  if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                    normalized_color_mask, *vertex_shader)) {
    DrawFailReason("render target update");
    return false;
  }
''')
open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: draw failure reasons")
