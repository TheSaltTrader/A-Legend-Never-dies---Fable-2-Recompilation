"""The freed-memory check uses the runtime's physical heap page table, not
VirtualQuery. The guest views carry thousands of small regions (per-page
write watches), so VirtualQuery over a multi-megabyte range is hundreds of
kernel calls per copy: 22 fps with one pass per copy (gate1), 4 fps chunked
(gate2), against 58-60 without (ctrl2). The physical heap's page table is
an array lookup, and the release path clears it under the global critical
region. The check runs under that lock; the copy itself runs without it."""
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

# runtime: the physical parent heap, whose page table the release path clears
edit("include/rex/system/xmemory.h", [
    ("  BaseHeap* LookupHeapByType(bool physical, uint32_t page_size);\n",
     "  BaseHeap* LookupHeapByType(bool physical, uint32_t page_size);\n"
     "  // The physical parent heap (page size 4 KB, base 0): its page table says\n"
     "  // whether a physical page is allocated at all, whichever guest view the\n"
     "  // allocation came through. Read under the global critical region.\n"
     "  BaseHeap* physical_heap() { return &heaps_.physical; }\n"),
])

edit("src/graphics/d3d12/command_processor.cpp", [
    ("  if (!length || !source) return false;\n"
     "  uint8_t* destination = memory_->TranslatePhysical(address);\n"
     "  if (!destination) return false;\n"
     "  // The release path changes page state under the global critical region:\n"
     "  // holding it makes \"still committed\" and the copy one step. Held per\n"
     "  // 64 KB chunk, never across the whole copy - a 1-4 MB memcpy under it\n"
     "  // hundreds of times a second queued every guest kernel call behind it\n"
     "  // (22 fps in the market against 58-60 lock-free).\n"
     "  constexpr size_t kChunk = 64 * 1024;\n"
     "  size_t done = 0;\n"
     "  while (done < length) {\n"
     "    const size_t chunk = std::min(kChunk, size_t(length) - done);\n"
     "    auto global_lock = rex::thread::global_critical_region::AcquireDirect();\n"
     "    // The host's own answer for the physical view: every page committed\n"
     "    // and writable. (A heap's page table only knows what was allocated\n"
     "    // through that view; the view heap returned for physical memory\n"
     "    // skipped nearly every copy.)\n"
     "    const uint8_t* cursor = destination + done;\n"
     "    const uint8_t* end = cursor + chunk;\n"
     "    bool writable = true;\n"
     "    while (cursor < end) {\n"
     "      MEMORY_BASIC_INFORMATION mbi;\n"
     "      if (!VirtualQuery(cursor, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {\n"
     "        writable = false;\n"
     "        break;\n"
     "      }\n"
     "      const DWORD p = mbi.Protect & 0xFF;\n"
     "      if (p != PAGE_READWRITE && p != PAGE_EXECUTE_READWRITE && p != PAGE_WRITECOPY &&\n"
     "          p != PAGE_EXECUTE_WRITECOPY) {\n"
     "        writable = false;\n"
     "        break;\n"
     "      }\n"
     "      cursor = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;\n"
     "    }\n"
     "    if (!writable) {\n"
     "      // The game freed the target meanwhile (a save thumbnail read and\n"
     "      // freed before the GPU had finished it, 16:20 and 16:35): the rest is\n"
     "      // not landed.\n"
     "      g_guest_copy_faults.fetch_add(1, std::memory_order_relaxed);\n"
     "      return false;\n"
     "    }\n"
     "    std::memcpy(destination + done, static_cast<const uint8_t*>(source) + done, chunk);\n"
     "    done += chunk;\n"
     "  }\n"
     "  return true;\n",
     "  if (!length || !source) return false;\n"
     "  uint8_t* destination = memory_->TranslatePhysical(address);\n"
     "  if (!destination) return false;\n"
     "  // Still allocated? The physical parent heap's page table (an array\n"
     "  // lookup; the release path clears it under the global critical region).\n"
     "  // VirtualQuery was tried first: the guest views carry thousands of small\n"
     "  // regions from the per-page write watches, so one query per region span\n"
     "  // was hundreds of kernel calls per copy - 22 fps, then 4 fps chunked,\n"
     "  // against 58-60 without a check. The copy itself runs without the lock.\n"
     "  {\n"
     "    auto global_lock = rex::thread::global_critical_region::AcquireDirect();\n"
     "    memory::BaseHeap* heap = memory_->physical_heap();\n"
     "    const uint64_t end = uint64_t(address) + length;\n"
     "    uint64_t cursor = address;\n"
     "    bool allocated = heap != nullptr;\n"
     "    while (allocated && cursor < end) {\n"
     "      memory::HeapAllocationInfo info{};\n"
     "      if (!heap->QueryRegionInfo(uint32_t(cursor), &info) || !info.state) {\n"
     "        allocated = false;\n"
     "        break;\n"
     "      }\n"
     "      const uint64_t region_end = uint64_t(info.base_address) + info.region_size;\n"
     "      if (region_end <= cursor) {\n"
     "        allocated = false;\n"
     "        break;\n"
     "      }\n"
     "      cursor = std::min(region_end, end);\n"
     "    }\n"
     "    if (!allocated) {\n"
     "      // The game freed the target meanwhile (a save thumbnail read and\n"
     "      // freed before the GPU had finished it, 16:20 and 16:35).\n"
     "      g_guest_copy_faults.fetch_add(1, std::memory_order_relaxed);\n"
     "      return false;\n"
     "    }\n"
     "  }\n"
     "  std::memcpy(destination, source, length);\n"
     "  return true;\n"),
])
print("done")
