"""The swap-rate reporter is a free function, so the frame counters live at
file scope in its anonymous namespace; the Note* entry points become
out-of-line members that write them."""
import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def rw(rel):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    return p, crlf, d.decode("utf-8").replace("\r\n", "\n")

def save(p, crlf, t):
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", os.path.relpath(p, ROOT))

def must(t, old, n=1):
    if t.count(old) != n:
        sys.exit("%d matches for %r" % (t.count(old), old[:70]))

# header: members out, declarations in
p, crlf, t = rw("include/rex/graphics/d3d12/command_processor.h")
old = ("  // [hitch] What the current guest frame did, for the [hitch] line.\n"
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
       " private:\n")
new = ("\n"
       " public:\n"
       "  // [hitch] What the current guest frame did, for the [hitch] line.\n"
       "  void NoteTextureLoad(uint64_t guest_bytes);\n"
       "  void NoteSharedMemoryUpload(uint64_t bytes);\n"
       "\n"
       " private:\n")
if t.count(old) == 0 and t.count(new) == 1:
    print("  header already patched")
else:
    must(t, old); t = t.replace(old, new); save(p, crlf, t)

# source: globals before the swap reporter, member references renamed
p, crlf, t = rw("src/graphics/d3d12/command_processor.cpp")
anchor = ("namespace {\n"
          "void ReportGuestSwapRate() {\n")
must(t, anchor)
t = t.replace(anchor,
              "namespace {\n"
              "// [hitch] What the current guest frame did (GPU worker thread only).\n"
              "uint32_t g_frame_textures_loaded = 0;\n"
              "uint64_t g_frame_texture_bytes = 0;\n"
              "uint64_t g_frame_upload_bytes = 0;\n"
              "uint32_t g_frame_pipeline_waits = 0;\n"
              "uint64_t g_frame_pipeline_wait_us = 0;\n"
              "uint32_t g_frame_sync_readbacks = 0;\n"
              "uint32_t g_frame_draws_snapshot = 0;\n"
              "void ReportGuestSwapRate() {\n")
for a, b in [("frame_textures_loaded_", "g_frame_textures_loaded"),
             ("frame_texture_bytes_", "g_frame_texture_bytes"),
             ("frame_upload_bytes_", "g_frame_upload_bytes"),
             ("frame_pipeline_waits_", "g_frame_pipeline_waits"),
             ("frame_pipeline_wait_us_", "g_frame_pipeline_wait_us"),
             ("frame_sync_readbacks_", "g_frame_sync_readbacks")]:
    n = t.count(a)
    if not n: sys.exit("no uses of " + a)
    t = t.replace(a, b)
    print("  renamed", a, n)
# the free function cannot see frame_draws_: snapshot it in IssueSwap first
must(t, "g_frame_sync_readbacks,\n                    frame_draws_);\n")
t = t.replace("g_frame_sync_readbacks,\n                    frame_draws_);\n",
              "g_frame_sync_readbacks,\n                    g_frame_draws_snapshot);\n")
must(t, "  ReportGuestSwapRate();\n")
t = t.replace("  ReportGuestSwapRate();\n",
              "  g_frame_draws_snapshot = frame_draws_;  // [hitch]\n"
              "  ReportGuestSwapRate();\n")
# the out-of-line entry points, after the anonymous namespace closes (before IssueSwap)
must(t, "void D3D12CommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,\n")
t = t.replace("void D3D12CommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,\n",
              "void D3D12CommandProcessor::NoteTextureLoad(uint64_t guest_bytes) {\n"
              "  ++g_frame_textures_loaded;\n"
              "  g_frame_texture_bytes += guest_bytes;\n"
              "}\n"
              "\n"
              "void D3D12CommandProcessor::NoteSharedMemoryUpload(uint64_t bytes) {\n"
              "  g_frame_upload_bytes += bytes;\n"
              "}\n"
              "\n"
              "void D3D12CommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,\n")
save(p, crlf, t)
print("done")
