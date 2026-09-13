"""On-demand readback behind a switch (off), plus an experiment knob.

Measured in Bowerstone Market, same save, same 150 s walk with camera turns:
tested pair 58-60 fps (GPU 65-74% busy), on-demand pair 21 fps (GPU 25%).
Protecting and releasing three guest views per deferred resolve is a
VirtualProtect storm on a 32-thread process. And the provider counted ZERO
CPU touches during four minutes of play there (60 at the save thumbnail
only): the game's CPU does not read impostor renders, so the mechanism
cannot be the flash fix. readback_resolve_on_demand (bool, false) keeps it
available; readback_resolve_drain_small_kb (int, 0) makes "some" wait for
the GPU after any resolve of at most N KB - an experiment for the flashes,
which "full" (a drain after EVERY resolve) does not show.
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
    ("REXCVAR_DEFINE_INT32(readback_resolve_sync_budget, 8, \"GPU/D3D12\",\n",
     "// Off: protecting and releasing three guest views per deferred resolve cost\n"
     "// two thirds of the frame rate in Bowerstone Market (21 vs 58-60 fps), and\n"
     "// the game's CPU touched none of those renders in four minutes there.\n"
     "REXCVAR_DEFINE_BOOL(readback_resolve_on_demand, false, \"GPU/D3D12\",\n"
     "                    \"Watch deferred resolve targets and land their copy the moment the CPU touches them\")\n"
     "    .lifecycle(rex::cvar::Lifecycle::kHotReload);\n"
     "// An experiment for the impostor flashes that only a GPU drain seems to cure.\n"
     "REXCVAR_DEFINE_INT32(readback_resolve_drain_small_kb, 0, \"GPU/D3D12\",\n"
     "                     \"With readback_resolve=some, wait for the GPU after every resolve of at most this many KB (0 = never)\")\n"
     "    .lifecycle(rex::cvar::Lifecycle::kHotReload);\n"
     "REXCVAR_DEFINE_INT32(readback_resolve_sync_budget, 8, \"GPU/D3D12\",\n"),
    # "some": sync also for small resolves when the experiment asks
    ("    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);\n"
     "    const bool sync_now =\n"
     "        is_cache_miss && (budget < 0 || int32_t(resolve_sync_misses_this_frame_) < budget);\n",
     "    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);\n"
     "    const int32_t drain_small_kb = REXCVAR_GET(readback_resolve_drain_small_kb);\n"
     "    const bool sync_now =\n"
     "        (is_cache_miss && (budget < 0 || int32_t(resolve_sync_misses_this_frame_) < budget)) ||\n"
     "        (drain_small_kb > 0 && written_length <= uint32_t(drain_small_kb) * 1024u);\n"),
    # gate the watch ("some")
    ("      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "      // The game's CPU may only touch this memory once the copy has landed:\n"
     "      // no access until then, the provider lands it on demand.\n"
     "      memory_->EnablePhysicalMemoryAccessCallbacks(written_address, written_length, false, true);\n",
     "      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "      // The game's CPU may only touch this memory once the copy has landed:\n"
     "      // no access until then, the provider lands it on demand (off by\n"
     "      // default: the page protection alone costs two thirds of the frame\n"
     "      // rate in a dense town).\n"
     "      if (REXCVAR_GET(readback_resolve_on_demand))\n"
     "        memory_->EnablePhysicalMemoryAccessCallbacks(written_address, written_length, false, true);\n"),
    # gate the watch (fast / full over-budget)
    ("      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "      memory_->EnablePhysicalMemoryAccessCallbacks(written_address, written_length, false, true);\n"
     "      rb.current_index = 1 - rb.current_index;\n",
     "      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "      if (REXCVAR_GET(readback_resolve_on_demand))\n"
     "        memory_->EnablePhysicalMemoryAccessCallbacks(written_address, written_length, false, true);\n"
     "      rb.current_index = 1 - rb.current_index;\n"),
    # gate the releases (each takes the global lock; nothing to release when off)
    ("      shared_memory_->UnprotectGpuRange(p.address, p.length);  // the copy lands now\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n",
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);  // the copy lands now\n"
     "      if (REXCVAR_GET(readback_resolve_on_demand))\n"
     "        memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"),
    ("    } else if (shared_memory_) {\n"
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"
     "    }\n",
     "    } else if (shared_memory_) {\n"
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);\n"
     "      if (REXCVAR_GET(readback_resolve_on_demand))\n"
     "        memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"
     "    }\n"),
])
print("done")
