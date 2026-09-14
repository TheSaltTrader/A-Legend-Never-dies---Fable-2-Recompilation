"""s69 experiments - the drain (readback_resolve_drain_small_kb: a GPU wait
after every small resolve) cures the white impostor flashes; this splits
that cure to find WHICH of its effects matters, without the wait:
  readback_resolve_submit_small_kb=N  end the submission (no wait) after a
                                      deferred resolve of at most N KB
  readback_resolve_rebind_small_kb=N  only invalidate the command list's
                                      render-target binding after one
Both 0 (off) by default; set through FABLE2_TUNE for a test. Counts every
5 s are folded into the [gpu] fence-waits line as "resolve splits N".
APPLY ONCE (requires patch_readback_selfcopy.py)."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
s = open(P, encoding="utf-8").read()
assert "readback_resolve_submit_small_kb" not in s, "already applied"

def rep(old, new):
    global s
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    s = s.replace(old, new)

rep('''REXCVAR_DEFINE_INT32(readback_resolve_sync_budget, 8, "GPU/D3D12",''',
    '''REXCVAR_DEFINE_INT32(readback_resolve_submit_small_kb, 0, "GPU/D3D12",
                     "Experiment: end the submission (no wait) after every deferred resolve of at most this many KB (0 = never)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(readback_resolve_rebind_small_kb, 0, "GPU/D3D12",
                     "Experiment: re-bind the render targets after every deferred resolve of at most this many KB (0 = never)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
static std::atomic<uint32_t> g_resolve_splits{0};
REXCVAR_DEFINE_INT32(readback_resolve_sync_budget, 8, "GPU/D3D12",''')

rep('''      // The game's CPU may only touch this memory once the copy has landed:
      // no access until then, the provider lands it on demand (off by
      // default: the page protection alone costs two thirds of the frame
      // rate in a dense town).
      if (REXCVAR_GET(readback_resolve_on_demand))
        memory_->EnablePhysicalMemoryAccessCallbacks(written_address, written_length, false, true);
    }
    rb.current_index = 1 - rb.current_index;
    return true;
  }
''', '''      // The game's CPU may only touch this memory once the copy has landed:
      // no access until then, the provider lands it on demand (off by
      // default: the page protection alone costs two thirds of the frame
      // rate in a dense town).
      if (REXCVAR_GET(readback_resolve_on_demand))
        memory_->EnablePhysicalMemoryAccessCallbacks(written_address, written_length, false, true);
      // [experiment] The drain's cure without its wait: a submission
      // boundary, or only the render-target re-bind a boundary implies.
      const int32_t submit_kb = REXCVAR_GET(readback_resolve_submit_small_kb);
      const int32_t rebind_kb = REXCVAR_GET(readback_resolve_rebind_small_kb);
      if (submit_kb > 0 && written_length <= uint32_t(submit_kb) * 1024u) {
        if (submission_open_) {
          ++g_resolve_splits;
          EndSubmission(false);
        }
      } else if (rebind_kb > 0 && written_length <= uint32_t(rebind_kb) * 1024u) {
        ++g_resolve_splits;
        render_target_cache_->InvalidateCommandListRenderTargets();
      }
    }
    rb.current_index = 1 - rb.current_index;
    return true;
  }
''')

# the count on the fence-waits line
rep('''      std::snprintf(b, sizeof(b), "%sreadback copies landed quietly %u",
                    line.empty() ? "" : ", ", quiet);
      line += b;
    }
''', '''      std::snprintf(b, sizeof(b), "%sreadback copies landed quietly %u",
                    line.empty() ? "" : ", ", quiet);
      line += b;
    }
    const uint32_t splits = g_resolve_splits.exchange(0);
    if (splits) {
      char b[64];
      std::snprintf(b, sizeof(b), "%sresolve splits %u", line.empty() ? "" : ", ", splits);
      line += b;
    }
''')
open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: readback split experiments")
