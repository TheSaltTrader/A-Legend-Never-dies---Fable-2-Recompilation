import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def edit(rel, pairs):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    t = d.decode("utf-8").replace("\r\n", "\n")
    for old, new in pairs:
        if t.count(old) != 1:
            sys.exit("%s: %d matches for %r" % (rel, t.count(old), old[:70]))
        t = t.replace(old, new)
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", rel)

# --- header: the pending list and the per-frame counter --------------------------
edit("include/rex/graphics/d3d12/command_processor.h", [
    ("  std::unordered_map<uint64_t, ReadbackBuffer> readback_buffers_;\n",
     "  std::unordered_map<uint64_t, ReadbackBuffer> readback_buffers_;\n"
     "  // [readback] A resolve copy recorded without waiting for it: copied into\n"
     "  // guest memory once its submission has completed (see\n"
     "  // IssueCopy_ReadbackResolvePath and readback_resolve_sync_budget).\n"
     "  struct PendingResolveReadback {\n"
     "    uint64_t key;\n"
     "    uint64_t submission;\n"
     "    uint32_t index;\n"
     "    uint32_t address;\n"
     "    uint32_t length;\n"
     "  };\n"
     "  std::vector<PendingResolveReadback> pending_resolve_readbacks_;\n"
     "  uint32_t resolve_sync_misses_this_frame_ = 0;\n"),
])

# --- the command processor -----------------------------------------------------
edit("src/graphics/d3d12/command_processor.cpp", [
    # the switch
    ('REXCVAR_DEFINE_BOOL(d3d12_readback_resolve, false, "GPU/D3D12",\n'
     '                    "Read render-to-texture results on the CPU")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kHotReload);\n',
     'REXCVAR_DEFINE_BOOL(d3d12_readback_resolve, false, "GPU/D3D12",\n'
     '                    "Read render-to-texture results on the CPU")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kHotReload);\n'
     '\n'
     '// A resolve at an address the readback has not seen waits for the whole GPU\n'
     '// queue before its copy (readback_resolve fast/some). Fable II\'s lake and its\n'
     '// tree impostors resolve to about a thousand new addresses a second, and\n'
     '// those drains took 2.4 s of every 5 s (33 fps, 2026-09-13). This caps the\n'
     '// synchronous ones per frame; the rest are copied when their submission\n'
     '// completes, a frame or two later.\n'
     'REXCVAR_DEFINE_INT32(readback_resolve_sync_budget, 8, "GPU/D3D12",\n'
     '                     "Resolve readbacks per frame that may wait for the GPU before copying; "\n'
     '                     "the rest are copied when their submission completes (0 = never wait)")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kHotReload);\n'),
    # the miss path: budgeted
    ('  bool is_cache_miss = false;\n'
     '  if (use_delayed_sync && (!rb.buffers[read_index] || written_length > rb.sizes[read_index] ||\n'
     '                           !rb.mapped_data[read_index])) {\n'
     '    is_cache_miss = true;\n'
     '    read_index = write_index;\n'
     '    if (!AwaitAllQueueOperationsCompletion()) {\n'
     '      return true;\n'
     '    }\n'
     '  }\n',
     '  bool is_cache_miss = false;\n'
     '  if (use_delayed_sync && (!rb.buffers[read_index] || written_length > rb.sizes[read_index] ||\n'
     '                           !rb.mapped_data[read_index])) {\n'
     '    is_cache_miss = true;\n'
     '    read_index = write_index;\n'
     '    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);\n'
     '    if (budget < 0 || int32_t(resolve_sync_misses_this_frame_) < budget) {\n'
     '      ++resolve_sync_misses_this_frame_;\n'
     '      if (!AwaitAllQueueOperationsCompletion()) {\n'
     '        return true;\n'
     '      }\n'
     '    } else {\n'
     '      // Over budget: the copy is in the command list; hand it to guest\n'
     '      // memory when the submission completes (BeginSubmission), not now.\n'
     '      pending_resolve_readbacks_.push_back(\n'
     '          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n'
     '      rb.current_index = 1 - rb.current_index;\n'
     '      return true;\n'
     '    }\n'
     '  }\n'),
    # the drain, once the fence has been checked
    ('  CheckSubmissionFence(is_opening_frame ? closed_frame_submissions_[frame_current_ % kQueueFrames]\n'
     '                                        : 0);\n',
     '  CheckSubmissionFence(is_opening_frame ? closed_frame_submissions_[frame_current_ % kQueueFrames]\n'
     '                                        : 0);\n'
     '  // [readback] Deferred resolve copies whose submission has completed go to\n'
     '  // guest memory now; a buffer evicted meanwhile simply drops its copy.\n'
     '  if (!pending_resolve_readbacks_.empty()) {\n'
     '    const uint64_t completed = GetCompletedSubmission();\n'
     '    size_t kept = 0;\n'
     '    for (size_t i = 0; i < pending_resolve_readbacks_.size(); ++i) {\n'
     '      const PendingResolveReadback& p = pending_resolve_readbacks_[i];\n'
     '      if (p.submission > completed) {\n'
     '        pending_resolve_readbacks_[kept++] = p;\n'
     '        continue;\n'
     '      }\n'
     '      auto it = readback_buffers_.find(p.key);\n'
     '      if (it != readback_buffers_.end()) {\n'
     '        ReadbackBuffer& rb = it->second;\n'
     '        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n'
     '          if (uint8_t* destination = memory_->TranslatePhysical(p.address)) {\n'
     '            std::memcpy(destination, static_cast<const uint8_t*>(rb.mapped_data[p.index]),\n'
     '                        p.length);\n'
     '          }\n'
     '        }\n'
     '      }\n'
     '    }\n'
     '    pending_resolve_readbacks_.resize(kept);\n'
     '  }\n'),
    # the per-frame budget resets when a frame closes
    ('    closed_frame_submissions_[(frame_current_++) % kQueueFrames] = submission_current_ - 1;\n',
     '    closed_frame_submissions_[(frame_current_++) % kQueueFrames] = submission_current_ - 1;\n'
     '    resolve_sync_misses_this_frame_ = 0;  // [readback] a fresh budget per frame\n'),
])

# --- and the dump: a session writes at most 4,000 raw files (about 2 GB) -------
edit("src/graphics/d3d12/texture_cache.cpp", [
    ("        fresh = dumped_this_session < 20000 &&\n",
     "        // 4,000 a session (~2 GB): a 20,000 cap let one run write 11 GB.\n"
     "        fresh = dumped_this_session < 4000 &&\n"),
])
print("done")
