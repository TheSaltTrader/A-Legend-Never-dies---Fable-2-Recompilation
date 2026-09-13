"""Per-hitch attribution: when a guest frame's swap interval exceeds 25 ms,
one `[hitch]` line says what that frame did - textures decoded (count,
guest bytes), shared-memory uploads (bytes), pipeline-creation waits at
submission end (count, ms), synchronous resolve readbacks, draws - so the
"locked 60" campaign chases the real causes. Counters reset every swap."""
import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def edit(rel, pairs):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    t = d.decode("utf-8").replace("\r\n", "\n")
    for old, new in pairs:
        if t.count(old) == 0 and t.count(new) == 1:
            print("  already applied:", old[:50].strip()); continue
        if t.count(old) != 1:
            sys.exit("%s: %d matches for %r" % (rel, t.count(old), old[:70]))
        t = t.replace(old, new)
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", rel)

edit("include/rex/graphics/d3d12/command_processor.h", [
    ("  uint32_t frame_draws_ = 0;\n"
     "  uint32_t frame_depth_draws_ = 0;\n",
     "  uint32_t frame_draws_ = 0;\n"
     "  uint32_t frame_depth_draws_ = 0;\n"
     "  // [hitch] What the current guest frame did, for the [hitch] line.\n"
     "  uint32_t frame_textures_loaded_ = 0;\n"
     "  uint64_t frame_texture_bytes_ = 0;\n"
     "  uint64_t frame_upload_bytes_ = 0;\n"
     "  uint32_t frame_pipeline_waits_ = 0;\n"
     "  uint64_t frame_pipeline_wait_us_ = 0;\n"
     "  uint32_t frame_sync_readbacks_ = 0;\n"
     "\n"
     " public:\n"
     "  void NoteTextureLoad(uint64_t guest_bytes) {\n"
     "    ++frame_textures_loaded_;\n"
     "    frame_texture_bytes_ += guest_bytes;\n"
     "  }\n"
     "  void NoteSharedMemoryUpload(uint64_t bytes) { frame_upload_bytes_ += bytes; }\n"
     "\n"
     " private:\n"),
])

edit("src/graphics/d3d12/command_processor.cpp", [
    # the hitch line, at the swap interval
    ("  const float dt = std::chrono::duration<float, std::milli>(now - last_swap_at).count();\n"
     "  last_swap_at = now;\n"
     "  if (count < 1536)\n"
     "    ms[count++] = dt;\n",
     "  const float dt = std::chrono::duration<float, std::milli>(now - last_swap_at).count();\n"
     "  last_swap_at = now;\n"
     "  if (count < 1536)\n"
     "    ms[count++] = dt;\n"
     "  // [hitch] A long frame says what it did. At most ten lines a second.\n"
     "  {\n"
     "    static clock::time_point hitch_sec = now;\n"
     "    static int hitch_lines = 0;\n"
     "    if (dt > 25.0f && dt < 2000.0f) {\n"
     "      if (std::chrono::duration<double>(now - hitch_sec).count() > 1.0) {\n"
     "        hitch_sec = now;\n"
     "        hitch_lines = 0;\n"
     "      }\n"
     "      if (hitch_lines++ < 10) {\n"
     "        REXLOG_INFO(\"[hitch] {:.0f} ms frame: {} textures ({} KB), uploads {} KB, pipeline waits {} ({:.1f} ms), sync readbacks {}, draws {}\",\n"
     "                    dt, frame_textures_loaded_, frame_texture_bytes_ >> 10, frame_upload_bytes_ >> 10,\n"
     "                    frame_pipeline_waits_, frame_pipeline_wait_us_ / 1000.0, frame_sync_readbacks_,\n"
     "                    frame_draws_);\n"
     "      }\n"
     "    }\n"
     "    frame_textures_loaded_ = 0;\n"
     "    frame_texture_bytes_ = 0;\n"
     "    frame_upload_bytes_ = 0;\n"
     "    frame_pipeline_waits_ = 0;\n"
     "    frame_pipeline_wait_us_ = 0;\n"
     "    frame_sync_readbacks_ = 0;\n"
     "  }\n"),
    # pipeline creation waits at submission end
    ("    pipeline_cache_->EndSubmission();\n",
     "    {\n"
     "      const auto t0 = std::chrono::steady_clock::now();\n"
     "      pipeline_cache_->EndSubmission();\n"
     "      const uint64_t us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(\n"
     "                                       std::chrono::steady_clock::now() - t0)\n"
     "                                       .count());\n"
     "      if (us > 200) {  // [hitch] a real wait for pipeline creation\n"
     "        ++frame_pipeline_waits_;\n"
     "        frame_pipeline_wait_us_ += us;\n"
     "      }\n"
     "    }\n"),
    # synchronous readbacks ("some" first-seen, and the fast/full budget path)
    ("    if (sync_now) {\n"
     "      ++resolve_sync_misses_this_frame_;\n",
     "    if (sync_now) {\n"
     "      ++resolve_sync_misses_this_frame_;\n"
     "      ++frame_sync_readbacks_;  // [hitch]\n"),
    ("    if (budget < 0 || int32_t(resolve_sync_misses_this_frame_) < budget) {\n"
     "      ++resolve_sync_misses_this_frame_;\n"
     "      if (!AwaitAllQueueOperationsCompletion()) {\n",
     "    if (budget < 0 || int32_t(resolve_sync_misses_this_frame_) < budget) {\n"
     "      ++resolve_sync_misses_this_frame_;\n"
     "      ++frame_sync_readbacks_;  // [hitch]\n"
     "      if (!AwaitAllQueueOperationsCompletion()) {\n"),
])

edit("src/graphics/d3d12/shared_memory.cpp", [
    ("      MakeRangeValid(upload_range_start << page_size_log2(), uint32_t(upload_buffer_size), false);\n",
     "      MakeRangeValid(upload_range_start << page_size_log2(), uint32_t(upload_buffer_size), false);\n"
     "      command_processor_.NoteSharedMemoryUpload(uint64_t(upload_buffer_size));  // [hitch]\n"),
])

edit("src/graphics/d3d12/texture_cache.cpp", [
    ("bool D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture, bool load_base,\n"
     "                                                              bool load_mips) {\n",
     "bool D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture, bool load_base,\n"
     "                                                              bool load_mips) {\n"
     "  command_processor_.NoteTextureLoad(  // [hitch]\n"
     "      uint64_t(load_base ? texture.GetGuestBaseSize() : 0u) +\n"
     "      uint64_t(load_mips ? texture.GetGuestMipsSize() : 0u));\n"),
])
print("done")
