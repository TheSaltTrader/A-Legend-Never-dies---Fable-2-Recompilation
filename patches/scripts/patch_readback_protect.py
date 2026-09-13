"""In-flight resolve ranges are protected from stale CPU uploads.

With readback "some" every resolve is copied to the CPU side once its GPU
work completes - a frame or two later. In that window the game's CPU often
touches the same memory PAGE (the impostor pool: the next allocation lands
beside the one just rendered), the page is marked dirty, and the next GPU
use re-uploads the whole page from the CPU copy - which still holds the old
contents of the resolved bytes. Result: a distant tree cluster flashes white
or purple for a frame or two (13:57 recording; the lake's purple flashes).

Now a resolve whose copy is still pending registers its byte range with the
shared memory; an upload that overlaps such a range copies the page in
pieces around it, so the GPU keeps its fresh render there. The protection is
lifted when the copy lands (the CPU copy is then exact) or the pending copy
is dropped.
"""
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

# ---- the generic shared memory: the protected-range list ----------------------
edit("include/rex/graphics/shared_memory.h", [
    ("  virtual bool UploadRanges(\n"
     "      const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) = 0;\n",
     "  virtual bool UploadRanges(\n"
     "      const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) = 0;\n"
     "\n"
     "  // [readback] Byte ranges the GPU has written (a resolve) whose copy to\n"
     "  // the CPU side is still pending: an upload of a page overlapping one\n"
     "  // must not carry the CPU's stale bytes over the fresh render, so the\n"
     "  // upload is split around them (SplitAroundProtected). GPU thread only.\n"
     " public:\n"
     "  void ProtectGpuRange(uint32_t start, uint32_t length);\n"
     "  void UnprotectGpuRange(uint32_t start, uint32_t length);\n"
     "\n"
     " protected:\n"
     "  // Appends to `pieces` the parts of [start, start + length) that are not\n"
     "  // protected, in order.\n"
     "  void SplitAroundProtected(uint32_t start, uint32_t length,\n"
     "                            std::vector<std::pair<uint32_t, uint32_t>>& pieces) const;\n"
     "  std::vector<std::pair<uint32_t, uint32_t>> gpu_protected_ranges_;\n"),
])

edit("src/graphics/shared_memory.cpp", [
    ("bool SharedMemory::RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count) {\n",
     "void SharedMemory::ProtectGpuRange(uint32_t start, uint32_t length) {\n"
     "  if (!length) return;\n"
     "  gpu_protected_ranges_.emplace_back(start, length);\n"
     "}\n"
     "\n"
     "void SharedMemory::UnprotectGpuRange(uint32_t start, uint32_t length) {\n"
     "  for (size_t i = 0; i < gpu_protected_ranges_.size(); ++i) {\n"
     "    if (gpu_protected_ranges_[i].first == start && gpu_protected_ranges_[i].second == length) {\n"
     "      gpu_protected_ranges_[i] = gpu_protected_ranges_.back();\n"
     "      gpu_protected_ranges_.pop_back();\n"
     "      return;\n"
     "    }\n"
     "  }\n"
     "}\n"
     "\n"
     "void SharedMemory::SplitAroundProtected(\n"
     "    uint32_t start, uint32_t length,\n"
     "    std::vector<std::pair<uint32_t, uint32_t>>& pieces) const {\n"
     "  if (!length) return;\n"
     "  const uint64_t end = uint64_t(start) + length;\n"
     "  // The protected ranges overlapping this one, in address order (few).\n"
     "  std::pair<uint32_t, uint32_t> hits[16];\n"
     "  size_t hit_count = 0;\n"
     "  for (const auto& r : gpu_protected_ranges_) {\n"
     "    const uint64_t r_end = uint64_t(r.first) + r.second;\n"
     "    if (r.first >= end || r_end <= start) continue;\n"
     "    if (hit_count < 16) hits[hit_count++] = r;\n"
     "  }\n"
     "  if (!hit_count) {\n"
     "    pieces.emplace_back(start, length);\n"
     "    return;\n"
     "  }\n"
     "  std::sort(hits, hits + hit_count);\n"
     "  uint64_t cursor = start;\n"
     "  for (size_t i = 0; i < hit_count; ++i) {\n"
     "    const uint64_t h_start = std::max<uint64_t>(hits[i].first, start);\n"
     "    const uint64_t h_end = std::min<uint64_t>(uint64_t(hits[i].first) + hits[i].second, end);\n"
     "    if (h_start > cursor) pieces.emplace_back(uint32_t(cursor), uint32_t(h_start - cursor));\n"
     "    if (h_end > cursor) cursor = h_end;\n"
     "  }\n"
     "  if (cursor < end) pieces.emplace_back(uint32_t(cursor), uint32_t(end - cursor));\n"
     "}\n"
     "\n"
     "bool SharedMemory::RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count) {\n"),
])

# ---- the D3D12 upload: copy the page in pieces around protected bytes ----------
edit("src/graphics/d3d12/shared_memory.cpp", [
    ("      command_list.D3DCopyBufferRegion(buffer_, upload_range_start << page_size_log2(),\n"
     "                                       upload_buffer, UINT64(upload_buffer_offset),\n"
     "                                       UINT64(upload_buffer_size));\n",
     "      {\n"
     "        // [readback] Not over a resolve whose CPU copy is still pending: the\n"
     "        // GPU keeps its fresh render there, the rest of the page is uploaded.\n"
     "        const uint32_t byte_start = upload_range_start << page_size_log2();\n"
     "        std::vector<std::pair<uint32_t, uint32_t>> pieces;\n"
     "        SplitAroundProtected(byte_start, uint32_t(upload_buffer_size), pieces);\n"
     "        for (const auto& piece : pieces) {\n"
     "          command_list.D3DCopyBufferRegion(\n"
     "              buffer_, piece.first, upload_buffer,\n"
     "              UINT64(upload_buffer_offset) + (piece.first - byte_start), UINT64(piece.second));\n"
     "        }\n"
     "      }\n"),
])

# ---- the command processor: protect on push, unprotect on landing / drop --------
edit("src/graphics/d3d12/command_processor.cpp", [
    # "some": the pending push
    ("    } else {\n"
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "    }\n"
     "    rb.current_index = 1 - rb.current_index;\n"
     "    return true;\n"
     "  }\n",
     "    } else {\n"
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "    }\n"
     "    rb.current_index = 1 - rb.current_index;\n"
     "    return true;\n"
     "  }\n"),
    # fast/full over-budget push
    ("      // Over budget: the copy is in the command list; hand it to guest\n"
     "      // memory when the submission completes (BeginSubmission), not now.\n"
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      rb.current_index = 1 - rb.current_index;\n"
     "      return true;\n",
     "      // Over budget: the copy is in the command list; hand it to guest\n"
     "      // memory when the submission completes (BeginSubmission), not now.\n"
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "      rb.current_index = 1 - rb.current_index;\n"
     "      return true;\n"),
    # landing
    ("      if (p.submission > completed) {\n"
     "        pending_resolve_readbacks_[kept++] = p;\n"
     "        continue;\n"
     "      }\n"
     "      auto it = readback_buffers_.find(p.key);\n",
     "      if (p.submission > completed) {\n"
     "        pending_resolve_readbacks_[kept++] = p;\n"
     "        continue;\n"
     "      }\n"
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);  // the copy lands now\n"
     "      auto it = readback_buffers_.find(p.key);\n"),
    # drop
    ("void D3D12CommandProcessor::DropPendingResolveReadbacks(uint64_t key, uint32_t index) {\n"
     "  size_t kept = 0;\n"
     "  for (size_t i = 0; i < pending_resolve_readbacks_.size(); ++i) {\n"
     "    const PendingResolveReadback& p = pending_resolve_readbacks_[i];\n"
     "    if (p.key != key || p.index != index) pending_resolve_readbacks_[kept++] = p;\n"
     "  }\n",
     "void D3D12CommandProcessor::DropPendingResolveReadbacks(uint64_t key, uint32_t index) {\n"
     "  size_t kept = 0;\n"
     "  for (size_t i = 0; i < pending_resolve_readbacks_.size(); ++i) {\n"
     "    const PendingResolveReadback& p = pending_resolve_readbacks_[i];\n"
     "    if (p.key != key || p.index != index) {\n"
     "      pending_resolve_readbacks_[kept++] = p;\n"
     "    } else if (shared_memory_) {\n"
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);\n"
     "    }\n"
     "  }\n"),
])
print("done")
