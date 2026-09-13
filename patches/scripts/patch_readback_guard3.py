"""CopyToGuestMemory asks the HOST whether the physical-view pages are
writable (VirtualQuery), not a heap's bookkeeping: LookupHeapByType(true,
4096) is the 0xE0000000 view heap, whose page table only knows allocations
made through that view, so the previous check skipped ~1,500 copies a second
(7,295 per 5 s in prov3) - no readback at all. Freed guest memory is what
makes the physical view no-access, and that is exactly what the query sees.
Still under the global critical region, so a free cannot slip in between."""
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

edit("src/graphics/d3d12/command_processor.cpp", [
    ("  auto global_lock = rex::thread::global_critical_region::AcquireDirect();\n"
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
     "  if (!destination) {\n",
     "  auto global_lock = rex::thread::global_critical_region::AcquireDirect();\n"
     "  uint8_t* destination = memory_->TranslatePhysical(address);\n"
     "  // The host's own answer for the physical view: every page committed and\n"
     "  // writable. (A heap's page table only knows what was allocated through\n"
     "  // that view; the view heap returned for physical memory skipped nearly\n"
     "  // every copy.)\n"
     "  if (destination) {\n"
     "    const uint8_t* cursor = destination;\n"
     "    const uint8_t* end = destination + length;\n"
     "    while (cursor < end) {\n"
     "      MEMORY_BASIC_INFORMATION mbi;\n"
     "      if (!VirtualQuery(cursor, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {\n"
     "        destination = nullptr;\n"
     "        break;\n"
     "      }\n"
     "      const DWORD p = mbi.Protect & 0xFF;\n"
     "      if (p != PAGE_READWRITE && p != PAGE_EXECUTE_READWRITE && p != PAGE_WRITECOPY &&\n"
     "          p != PAGE_EXECUTE_WRITECOPY) {\n"
     "        destination = nullptr;\n"
     "        break;\n"
     "      }\n"
     "      cursor = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;\n"
     "    }\n"
     "  }\n"
     "  if (!destination) {\n"),
])
print("done")
