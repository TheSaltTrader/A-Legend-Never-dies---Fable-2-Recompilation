"""Plugin: turning texture dumping on (or changing its folder) drops every
texture, so the scene in front of the player is written out - not only what
loads afterwards.

NG2 found that dumping switched on mid-stage "did nothing": the dump runs per
texture LOAD, and everything already resident had loaded before the switch,
so it was never seen. NG2 made dumping restart-required. The pack path already
has the better answer in this same block - a change drops every texture at
the end of the frame - so the dump settings get the same treatment, and the
switch captures the whole scene from where the player stands.
"""
import os, sys

C = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\texture_cache.cpp"

old = """      command_processor_.ClearCaches();
    }
  }
"""
new = """      command_processor_.ClearCaches();
    }
  }

  // [texpack] Dumping switched on, or pointed at another folder: drop every
  // texture too. The dump runs per texture LOAD, so without this only what
  // loads AFTER the switch is written and the scene already in memory is
  // silently missed - which on NG2 read as "dumping does nothing" and made
  // dumping a restart-required setting there. Dropping the cache makes every
  // resident texture load again, and the dump's own id+hash set skips what it
  // wrote before, so nothing is written twice. Switching dumping OFF changes
  // nothing that is drawn, so it is not worth a reload.
  {
    static std::string applied_dump;
    const std::string want_dump =
        REXCVAR_GET(texture_dump) ? rex::cvar::Query<std::string>("texture_dump_path")
                                  : std::string();
    if (want_dump != applied_dump) {
      const bool reload = !want_dump.empty();
      applied_dump = want_dump;
      if (reload) {
        REXLOG_INFO("[texpack] dumping to '{}' - reloading every texture so the scene in "
                    "memory is written too",
                    want_dump);
        command_processor_.ClearCaches();
      }
    }
  }
"""
s = open(C, encoding="utf-8").read()
# The anchor is the end of the pack-path block: make sure it is that one.
i = s.find("[texpack] pack path changed to '{}' - reloading every texture")
j = s.find(old, i)
if i < 0 or j < 0 or j - i > 1200:
    print("FAIL: anchor not found after the pack-path block"); sys.exit(1)
s = s[:j] + new + s[j + len(old):]
open(C, "w", encoding="utf-8", newline="").write(s)
print("dump trigger added after the pack-path block")
