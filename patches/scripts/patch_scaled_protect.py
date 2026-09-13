"""Resolution-scaled resolve data survives a CPU write beside it.

With draw resolution scaling a resolve lands in the SCALED buffer and the
pages are marked "scaled resolved"; a texture there is loaded from the scaled
buffer. Any CPU write to such a page (the global watch, not from the GPU)
clears those marks for the whole pages, and the next load takes the UNSCALED
guest memory instead - the CPU copy, which for an impostor rendered a frame
ago is still the pool's magenta fill. That is the magenta tree flash (14:01
recording, several trees for one frame). The page-upload protection of
patch_readback_protect.py does not reach this path.

Now pages that hold a resolve whose CPU copy is still pending keep their
scaled marks: the CPU wrote beside the render, not over it. The texture is
still re-loaded (its own watch fires), from the scaled buffer.
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

edit("include/rex/graphics/shared_memory.h", [
    ("  void ProtectGpuRange(uint32_t start, uint32_t length);\n"
     "  void UnprotectGpuRange(uint32_t start, uint32_t length);\n",
     "  void ProtectGpuRange(uint32_t start, uint32_t length);\n"
     "  void UnprotectGpuRange(uint32_t start, uint32_t length);\n"
     "  bool IsGpuRangeProtected(uint32_t start, uint32_t length) const;\n"),
])

edit("src/graphics/shared_memory.cpp", [
    ("void SharedMemory::SplitAroundProtected(\n",
     "bool SharedMemory::IsGpuRangeProtected(uint32_t start, uint32_t length) const {\n"
     "  if (!length) return false;\n"
     "  const uint64_t end = uint64_t(start) + length;\n"
     "  for (const auto& r : gpu_protected_ranges_) {\n"
     "    if (r.first < end && uint64_t(r.first) + r.second > start) return true;\n"
     "  }\n"
     "  return false;\n"
     "}\n"
     "\n"
     "void SharedMemory::SplitAroundProtected(\n"),
])

edit("src/graphics/pipeline/texture/cache.cpp", [
    ("      if (resolve_block_index == resolve_block_last && (resolve_page_last & 31) != 31) {\n"
     "        resolve_keep_bits |= ~((UINT32_C(1) << ((resolve_page_last & 31) + 1)) - 1);\n"
     "      }\n"
     "      scaled_resolve_pages_[resolve_block_index] &= resolve_keep_bits;\n",
     "      if (resolve_block_index == resolve_block_last && (resolve_page_last & 31) != 31) {\n"
     "        resolve_keep_bits |= ~((UINT32_C(1) << ((resolve_page_last & 31) + 1)) - 1);\n"
     "      }\n"
     "      // [readback] Pages holding a resolve whose CPU copy is still pending\n"
     "      // keep their scaled data: the CPU wrote beside the render, not over\n"
     "      // it (the impostor pool fills the next slot while the last one is in\n"
     "      // flight; taking the unscaled guest copy instead showed the pool's\n"
     "      // magenta fill for a frame).\n"
     "      {\n"
     "        const uint32_t page_lo = std::max(resolve_page_first, resolve_block_index << 5);\n"
     "        const uint32_t page_hi = std::min(resolve_page_last, (resolve_block_index << 5) + 31);\n"
     "        for (uint32_t page = page_lo; page <= page_hi; ++page) {\n"
     "          if (shared_memory().IsGpuRangeProtected(page << 12, 4096))\n"
     "            resolve_keep_bits |= UINT32_C(1) << (page & 31);\n"
     "        }\n"
     "      }\n"
     "      scaled_resolve_pages_[resolve_block_index] &= resolve_keep_bits;\n"),
])
print("done")
