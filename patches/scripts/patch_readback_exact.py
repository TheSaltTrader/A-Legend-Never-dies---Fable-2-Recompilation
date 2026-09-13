"""readback_resolve "some" copies EVERY resolve to the CPU, exactly, without waiting.

Before (2026-09-13): "some" copied a resolve to guest memory only the first time
an address was seen. Anything re-rendered there later - the cullis-gate swirl,
the dog's coat, the hero's morph textures - stayed stale on the CPU side, and
whenever the CPU touched that page the stale copy was uploaded over the fresh
render: a white flash every few frames at the Crucible gate, a translucent
"ghost" dog. "fast" copied the previous resolve at the same address (wrong for
one-shot renders such as tree impostors), "full" waited for the GPU every time
(33 fps at the lake).

Now "some": the first resolve at an address is copied synchronously (within
the per-frame budget, so the CPU can read it this frame), every later one is
copied when its own submission has completed - at the next frame's opening
submission, so a copy always lands BEFORE that frame's resolves and never over
a fresher one. A newer resolve into the same readback slot supersedes an
older pending copy. Readback buffers that may still be in flight are released
only once their submission has completed (the old code released them at once
on size growth and eviction).
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

edit("include/rex/graphics/d3d12/command_processor.h", [
    ("  std::vector<PendingResolveReadback> pending_resolve_readbacks_;\n"
     "  uint32_t resolve_sync_misses_this_frame_ = 0;\n",
     "  std::vector<PendingResolveReadback> pending_resolve_readbacks_;\n"
     "  uint32_t resolve_sync_misses_this_frame_ = 0;\n"
     "  // [readback] Readback buffers replaced or evicted while a copy into them\n"
     "  // may still be in flight: released once that submission has completed.\n"
     "  std::vector<std::pair<uint64_t, ID3D12Resource*>> readback_buffers_to_release_;\n"
     "  void DropPendingResolveReadbacks(uint64_t key, uint32_t index);\n"),
])

edit("src/graphics/d3d12/command_processor.cpp", [
    # 1. Size growth: the old buffer is released when its submission completes,
    #    and any pending copy out of it is dropped (it would read the new one).
    ("    if (rb.buffers[write_index]) {\n"
     "      if (rb.mapped_data[write_index]) {\n"
     "        rb.buffers[write_index]->Unmap(0, nullptr);\n"
     "        rb.mapped_data[write_index] = nullptr;\n"
     "      }\n"
     "      rb.buffers[write_index]->Release();\n"
     "    }\n"
     "    rb.buffers[write_index] = buffer;\n",
     "    if (rb.buffers[write_index]) {\n"
     "      if (rb.mapped_data[write_index]) {\n"
     "        rb.buffers[write_index]->Unmap(0, nullptr);\n"
     "        rb.mapped_data[write_index] = nullptr;\n"
     "      }\n"
     "      // [readback] A copy into it may be in flight: release later.\n"
     "      DropPendingResolveReadbacks(resolve_key, write_index);\n"
     "      readback_buffers_to_release_.emplace_back(GetCurrentSubmission(),\n"
     "                                                rb.buffers[write_index]);\n"
     "    }\n"
     "    rb.buffers[write_index] = buffer;\n"),
    # 2. The mode logic.
    ("  ReadbackResolveMode readback_mode = GetReadbackResolveMode(REXCVAR_GET(d3d12_readback_resolve));\n"
     "  bool use_delayed_sync =\n"
     "      readback_mode == ReadbackResolveMode::kFast || readback_mode == ReadbackResolveMode::kSome;\n"
     "  uint32_t read_index = write_index;\n"
     "  if (use_delayed_sync) {\n"
     "    read_index = 1 - write_index;\n"
     "  } else if (!AwaitAllQueueOperationsCompletion()) {\n"
     "    return true;\n"
     "  }\n"
     "\n"
     "  bool is_cache_miss = false;\n"
     "  if (use_delayed_sync && (!rb.buffers[read_index] || written_length > rb.sizes[read_index] ||\n"
     "                           !rb.mapped_data[read_index])) {\n"
     "    is_cache_miss = true;\n"
     "    read_index = write_index;\n"
     "    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);\n"
     "    if (budget < 0 || int32_t(resolve_sync_misses_this_frame_) < budget) {\n"
     "      ++resolve_sync_misses_this_frame_;\n"
     "      if (!AwaitAllQueueOperationsCompletion()) {\n"
     "        return true;\n"
     "      }\n"
     "    } else {\n"
     "      // Over budget: the copy is in the command list; hand it to guest\n"
     "      // memory when the submission completes (BeginSubmission), not now.\n"
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      rb.current_index = 1 - rb.current_index;\n"
     "      return true;\n"
     "    }\n"
     "  }\n"
     "\n"
     "  bool should_copy = (readback_mode == ReadbackResolveMode::kSome) ? is_cache_miss : true;\n",
     "  ReadbackResolveMode readback_mode = GetReadbackResolveMode(REXCVAR_GET(d3d12_readback_resolve));\n"
     "  bool use_delayed_sync =\n"
     "      readback_mode == ReadbackResolveMode::kFast || readback_mode == ReadbackResolveMode::kSome;\n"
     "  uint32_t read_index = write_index;\n"
     "  if (use_delayed_sync) {\n"
     "    read_index = 1 - write_index;\n"
     "  } else if (!AwaitAllQueueOperationsCompletion()) {\n"
     "    return true;\n"
     "  }\n"
     "\n"
     "  bool is_cache_miss = false;\n"
     "  if (use_delayed_sync && (!rb.buffers[read_index] || written_length > rb.sizes[read_index] ||\n"
     "                           !rb.mapped_data[read_index])) {\n"
     "    is_cache_miss = true;\n"
     "    read_index = write_index;\n"
     "  }\n"
     "  if (readback_mode == ReadbackResolveMode::kSome) {\n"
     "    // [readback] \"some\" (2026-09-13): every resolve reaches guest memory,\n"
     "    // exactly - the first at an address synchronously (budgeted, so the\n"
     "    // CPU can read it this frame), later ones once their own submission\n"
     "    // has completed, at the next frame's opening submission, never\n"
     "    // waiting. It used to copy only the first resolve at an address, so a\n"
     "    // texture re-rendered there (the cullis-gate swirl, the dog's coat)\n"
     "    // stayed stale on the CPU and was uploaded over the fresh render\n"
     "    // whenever the CPU touched the page: a white flash every few frames.\n"
     "    DropPendingResolveReadbacks(resolve_key, write_index);  // superseded\n"
     "    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);\n"
     "    const bool sync_now =\n"
     "        is_cache_miss && (budget < 0 || int32_t(resolve_sync_misses_this_frame_) < budget);\n"
     "    if (sync_now) {\n"
     "      ++resolve_sync_misses_this_frame_;\n"
     "      if (AwaitAllQueueOperationsCompletion() && rb.mapped_data[write_index]) {\n"
     "        if (uint8_t* destination = memory_->TranslatePhysical(written_address)) {\n"
     "          std::memcpy(destination, static_cast<const uint8_t*>(rb.mapped_data[write_index]),\n"
     "                      written_length);\n"
     "        }\n"
     "      }\n"
     "    } else {\n"
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "    }\n"
     "    rb.current_index = 1 - rb.current_index;\n"
     "    return true;\n"
     "  }\n"
     "  if (is_cache_miss) {\n"
     "    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);\n"
     "    if (budget < 0 || int32_t(resolve_sync_misses_this_frame_) < budget) {\n"
     "      ++resolve_sync_misses_this_frame_;\n"
     "      if (!AwaitAllQueueOperationsCompletion()) {\n"
     "        return true;\n"
     "      }\n"
     "    } else {\n"
     "      // Over budget: the copy is in the command list; hand it to guest\n"
     "      // memory when the submission completes (BeginSubmission), not now.\n"
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      rb.current_index = 1 - rb.current_index;\n"
     "      return true;\n"
     "    }\n"
     "  }\n"
     "\n"
     "  bool should_copy = true;\n"),
    # 3. The drain: only at a frame's opening submission (so a landed copy
    #    precedes that frame's resolves), plus the deferred releases.
    ("  // [readback] Deferred resolve copies whose submission has completed go to\n"
     "  // guest memory now; a buffer evicted meanwhile simply drops its copy.\n"
     "  if (!pending_resolve_readbacks_.empty()) {\n"
     "    const uint64_t completed = GetCompletedSubmission();\n",
     "  // [readback] Buffers replaced or evicted while a copy may have been in\n"
     "  // flight are released once that submission has completed.\n"
     "  if (!readback_buffers_to_release_.empty()) {\n"
     "    const uint64_t completed = GetCompletedSubmission();\n"
     "    size_t kept = 0;\n"
     "    for (size_t i = 0; i < readback_buffers_to_release_.size(); ++i) {\n"
     "      auto& entry = readback_buffers_to_release_[i];\n"
     "      if (entry.first > completed) {\n"
     "        readback_buffers_to_release_[kept++] = entry;\n"
     "      } else if (entry.second) {\n"
     "        entry.second->Release();\n"
     "      }\n"
     "    }\n"
     "    readback_buffers_to_release_.resize(kept);\n"
     "  }\n"
     "  // [readback] Deferred resolve copies whose submission has completed go to\n"
     "  // guest memory now - at a frame's opening submission only, so a copy\n"
     "  // always lands before that frame's own resolves and never over a fresher\n"
     "  // one; a buffer evicted meanwhile simply drops its copy.\n"
     "  if (is_opening_frame && !pending_resolve_readbacks_.empty()) {\n"
     "    const uint64_t completed = GetCompletedSubmission();\n"),
    # 4. Eviction defers the release too.
    ("    for (uint32_t i = 0; i < 2; ++i) {\n"
     "      if (readback.buffers[i]) {\n"
     "        if (readback.mapped_data[i]) {\n"
     "          readback.buffers[i]->Unmap(0, nullptr);\n"
     "        }\n"
     "        readback.buffers[i]->Release();\n"
     "      }\n",
     "    for (uint32_t i = 0; i < 2; ++i) {\n"
     "      if (readback.buffers[i]) {\n"
     "        if (readback.mapped_data[i]) {\n"
     "          readback.buffers[i]->Unmap(0, nullptr);\n"
     "        }\n"
     "        // [readback] A copy into it may be in flight: release later.\n"
     "        DropPendingResolveReadbacks(it->first, i);\n"
     "        readback_buffers_to_release_.emplace_back(GetCurrentSubmission(), readback.buffers[i]);\n"
     "      }\n"),
    # 5. The helper.
    ("void D3D12CommandProcessor::EvictOldReadbackBuffers(\n",
     "void D3D12CommandProcessor::DropPendingResolveReadbacks(uint64_t key, uint32_t index) {\n"
     "  size_t kept = 0;\n"
     "  for (size_t i = 0; i < pending_resolve_readbacks_.size(); ++i) {\n"
     "    const PendingResolveReadback& p = pending_resolve_readbacks_[i];\n"
     "    if (p.key != key || p.index != index) pending_resolve_readbacks_[kept++] = p;\n"
     "  }\n"
     "  pending_resolve_readbacks_.resize(kept);\n"
     "}\n"
     "\n"
     "void D3D12CommandProcessor::EvictOldReadbackBuffers(\n"),
])
print("done")
