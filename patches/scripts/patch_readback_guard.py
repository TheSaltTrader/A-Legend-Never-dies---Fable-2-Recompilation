"""Readback copies into guest memory survive the game freeing that memory.

Crash 16:20:36 (provider test run): the worker waited ~5 s in the sync path
of a first-seen resolve (a region load: pipeline creation), the game freed
the resolve target meanwhile (freed guest pages are no-access), and the
memcpy into the physical view faulted - a fault the runtime's handler never
handles (the physical view is exempt). The same hazard sits in every copy
into guest memory here. All of them now go through SafeGuestCopy: a plain-C
helper with a structured exception handler around the memcpy (the runtime's
vectored handler declines physical-view faults, so the frame handler gets
them), which reports the miss instead of taking the process down.
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

edit("src/graphics/d3d12/command_processor.cpp", [
    # the helper, next to the fence-wait stats (anonymous namespace)
    ("FenceWaitStat g_fence_waits[8];\n",
     "FenceWaitStat g_fence_waits[8];\n"
     "// [readback] A copy into guest memory that survives the game having freed\n"
     "// that memory meanwhile (freed pages are no-access; the runtime's vectored\n"
     "// handler declines faults in the physical view, so the frame handler here\n"
     "// gets them). Plain C: no objects to unwind past __try.\n"
     "std::atomic<uint32_t> g_guest_copy_faults{0};\n"
     "bool SafeGuestCopy(void* destination, const void* source, size_t length) {\n"
     "  __try {\n"
     "    std::memcpy(destination, source, length);\n"
     "    return true;\n"
     "  } __except (EXCEPTION_EXECUTE_HANDLER) {\n"
     "    g_guest_copy_faults.fetch_add(1, std::memory_order_relaxed);\n"
     "    return false;\n"
     "  }\n"
     "}\n"),
    ("    const uint32_t calls = g_provider_calls.exchange(0);\n",
     "    const uint32_t faults = g_guest_copy_faults.exchange(0);\n"
     "    if (faults) {\n"
     "      char b[96];\n"
     "      std::snprintf(b, sizeof(b), \"%sreadback copies into freed memory skipped %u\",\n"
     "                    line.empty() ? \"\" : \", \", faults);\n"
     "      line += b;\n"
     "    }\n"
     "    const uint32_t calls = g_provider_calls.exchange(0);\n"),
    # the sync path ("some", first-seen)
    ("      if (AwaitAllQueueOperationsCompletion() && rb.mapped_data[write_index]) {\n"
     "        if (uint8_t* destination = memory_->TranslatePhysical(written_address)) {\n"
     "          std::memcpy(destination, static_cast<const uint8_t*>(rb.mapped_data[write_index]),\n"
     "                      written_length);\n"
     "        }\n"
     "      }\n",
     "      if (AwaitAllQueueOperationsCompletion() && rb.mapped_data[write_index]) {\n"
     "        if (uint8_t* destination = memory_->TranslatePhysical(written_address)) {\n"
     "          SafeGuestCopy(destination, rb.mapped_data[write_index], written_length);\n"
     "        }\n"
     "      }\n"),
    # fast / full
    ("    uint8_t* destination = memory_->TranslatePhysical(written_address);\n"
     "    if (destination) {\n"
     "      std::memcpy(destination, static_cast<uint8_t*>(rb.mapped_data[read_index]), written_length);\n"
     "    }\n",
     "    uint8_t* destination = memory_->TranslatePhysical(written_address);\n"
     "    if (destination) {\n"
     "      SafeGuestCopy(destination, rb.mapped_data[read_index], written_length);\n"
     "    }\n"),
    # the frame-open drain
    ("        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          if (uint8_t* destination = memory_->TranslatePhysical(p.address)) {\n"
     "            std::memcpy(destination, static_cast<const uint8_t*>(rb.mapped_data[p.index]),\n"
     "                        p.length);\n"
     "          }\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "    pending_resolve_readbacks_.resize(kept);\n"
     "  }\n",
     "        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          if (uint8_t* destination = memory_->TranslatePhysical(p.address)) {\n"
     "            SafeGuestCopy(destination, rb.mapped_data[p.index], p.length);\n"
     "          }\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "    pending_resolve_readbacks_.resize(kept);\n"
     "  }\n"),
    # the on-demand landing
    ("        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          if (uint8_t* destination = memory_->TranslatePhysical(p.address)) {\n"
     "            std::memcpy(destination, static_cast<const uint8_t*>(rb.mapped_data[p.index]),\n"
     "                        p.length);\n"
     "          }\n"
     "        }\n"
     "      }\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n",
     "        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          if (uint8_t* destination = memory_->TranslatePhysical(p.address)) {\n"
     "            SafeGuestCopy(destination, rb.mapped_data[p.index], p.length);\n"
     "          }\n"
     "        }\n"
     "      }\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"),
])
print("done")
