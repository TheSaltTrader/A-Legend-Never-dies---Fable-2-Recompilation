"""Readback copies check the guest's page state instead of trusting a frame
handler: the structured exception handler of the first cut was never reached
(the run at 16:35 crashed on the same freed page). CopyToGuestMemory takes
the global critical region (the release path takes it too, so a free cannot
slip in between), asks the physical heap whether every page of the range is
committed, and only then copies."""
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
    ("  void LandPendingResolveReadbacks(uint32_t address, uint32_t length);\n",
     "  void LandPendingResolveReadbacks(uint32_t address, uint32_t length);\n"
     "  // Copies into guest memory only if every page of the range is still\n"
     "  // committed (under the global critical region, so a free cannot slip in\n"
     "  // between); returns false and counts the skip otherwise.\n"
     "  bool CopyToGuestMemory(uint32_t address, const void* source, uint32_t length);\n"),
])

edit("src/graphics/d3d12/command_processor.cpp", [
    # the helper: page-state check instead of SEH
    ("std::atomic<uint32_t> g_guest_copy_faults{0};\n"
     "bool SafeGuestCopy(void* destination, const void* source, size_t length) {\n"
     "  __try {\n"
     "    std::memcpy(destination, source, length);\n"
     "    return true;\n"
     "  } __except (EXCEPTION_EXECUTE_HANDLER) {\n"
     "    g_guest_copy_faults.fetch_add(1, std::memory_order_relaxed);\n"
     "    return false;\n"
     "  }\n"
     "}\n",
     "std::atomic<uint32_t> g_guest_copy_faults{0};\n"),
    # the sync path
    ("      if (AwaitAllQueueOperationsCompletion() && rb.mapped_data[write_index]) {\n"
     "        if (uint8_t* destination = memory_->TranslatePhysical(written_address)) {\n"
     "          SafeGuestCopy(destination, rb.mapped_data[write_index], written_length);\n"
     "        }\n"
     "      }\n",
     "      if (AwaitAllQueueOperationsCompletion() && rb.mapped_data[write_index]) {\n"
     "        CopyToGuestMemory(written_address, rb.mapped_data[write_index], written_length);\n"
     "      }\n"),
    # fast / full
    ("    uint8_t* destination = memory_->TranslatePhysical(written_address);\n"
     "    if (destination) {\n"
     "      SafeGuestCopy(destination, rb.mapped_data[read_index], written_length);\n"
     "    }\n",
     "    CopyToGuestMemory(written_address, rb.mapped_data[read_index], written_length);\n"),
    # the frame-open drain
    ("        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          if (uint8_t* destination = memory_->TranslatePhysical(p.address)) {\n"
     "            SafeGuestCopy(destination, rb.mapped_data[p.index], p.length);\n"
     "          }\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "    pending_resolve_readbacks_.resize(kept);\n"
     "  }\n",
     "        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          CopyToGuestMemory(p.address, rb.mapped_data[p.index], p.length);\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "    pending_resolve_readbacks_.resize(kept);\n"
     "  }\n"),
    # the on-demand landing
    ("        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          if (uint8_t* destination = memory_->TranslatePhysical(p.address)) {\n"
     "            SafeGuestCopy(destination, rb.mapped_data[p.index], p.length);\n"
     "          }\n"
     "        }\n"
     "      }\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n",
     "        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          CopyToGuestMemory(p.address, rb.mapped_data[p.index], p.length);\n"
     "        }\n"
     "      }\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"),
    # the helper's body
    ("void D3D12CommandProcessor::DropPendingResolveReadbacks(uint64_t key, uint32_t index) {\n",
     "bool D3D12CommandProcessor::CopyToGuestMemory(uint32_t address, const void* source,\n"
     "                                              uint32_t length) {\n"
     "  if (!length || !source) return false;\n"
     "  // The release path changes page state under the global critical region:\n"
     "  // holding it here makes \"still committed\" and the copy one step.\n"
     "  auto global_lock = rex::thread::global_critical_region::AcquireDirect();\n"
     "  memory::BaseHeap* heap = memory_->LookupHeapByType(true, 4096);\n"
     "  const uint64_t end = uint64_t(address) + length;\n"
     "  uint64_t cursor = address;\n"
     "  while (heap && cursor < end) {\n"
     "    memory::HeapAllocationInfo info{};\n"
     "    if (!heap->QueryRegionInfo(uint32_t(cursor), &info) ||\n"
     "        !(info.state & memory::kMemoryAllocationCommit)) {\n"
     "      heap = nullptr;\n"
     "      break;\n"
     "    }\n"
     "    const uint64_t region_end = uint64_t(info.base_address) + info.region_size;\n"
     "    if (region_end <= cursor) {\n"
     "      heap = nullptr;\n"
     "      break;\n"
     "    }\n"
     "    cursor = std::min(region_end, end);\n"
     "  }\n"
     "  uint8_t* destination = heap ? memory_->TranslatePhysical(address) : nullptr;\n"
     "  if (!destination) {\n"
     "    // The game freed the target meanwhile (a save thumbnail read and freed\n"
     "    // before the GPU had finished it, 16:20 and 16:35): nothing to land.\n"
     "    g_guest_copy_faults.fetch_add(1, std::memory_order_relaxed);\n"
     "    return false;\n"
     "  }\n"
     "  std::memcpy(destination, source, length);\n"
     "  return true;\n"
     "}\n"
     "\n"
     "void D3D12CommandProcessor::DropPendingResolveReadbacks(uint64_t key, uint32_t index) {\n"),
])
print("done")
