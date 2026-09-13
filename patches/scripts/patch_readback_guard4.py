"""CopyToGuestMemory checks and copies in 64 KB chunks, taking the global
critical region per chunk instead of across the whole copy. With the lock
held for a 1-4 MB memcpy hundreds of times a second, every guest kernel
call queued behind it: 22 fps in the market against 58-60 with the pair
that copied lock-free (gate1 vs ctrl2, page watches off in both)."""
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
    ("  if (!length || !source) return false;\n"
     "  // The release path changes page state under the global critical region:\n"
     "  // holding it here makes \"still committed\" and the copy one step.\n"
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
     "  if (!destination) {\n"
     "    // The game freed the target meanwhile (a save thumbnail read and freed\n"
     "    // before the GPU had finished it, 16:20 and 16:35): nothing to land.\n"
     "    g_guest_copy_faults.fetch_add(1, std::memory_order_relaxed);\n"
     "    return false;\n"
     "  }\n"
     "  std::memcpy(destination, source, length);\n"
     "  return true;\n",
     "  if (!length || !source) return false;\n"
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
     "  return true;\n"),
])
print("done")
