"""A [hitch] line only for a frame that is long RELATIVE to the recent
median (over 25 ms and over 1.8x the last window's p50), so the 30 fps title
and loading screens (every frame 33 ms) no longer log ten lines a second."""
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
    ("uint32_t g_frame_draws_snapshot = 0;\n"
     "void ReportGuestSwapRate() {\n",
     "uint32_t g_frame_draws_snapshot = 0;\n"
     "float g_recent_p50_ms = 16.7f;  // the last 5 s window's median frame\n"
     "void ReportGuestSwapRate() {\n"),
    ("    if (dt > 25.0f && dt < 2000.0f) {\n",
     "    // Long in absolute terms AND against the recent median: the 30 fps\n"
     "    // title and loading screens are not hitches.\n"
     "    if (dt > 25.0f && dt < 2000.0f && dt > 1.8f * g_recent_p50_ms) {\n"),
    ("  const float p50 = sorted[count / 2];\n"
     "  const float p99 = sorted[(count * 99) / 100];\n"
     "  const float worst = sorted[count - 1];\n"
     "  int hitches = 0;\n"
     "  for (size_t i = 0; i < count; ++i)\n"
     "    if (ms[i] > p50 * 2.0f) ++hitches;\n"
     "  REXLOG_INFO(\"[swap] ",
     "  const float p50 = sorted[count / 2];\n"
     "  const float p99 = sorted[(count * 99) / 100];\n"
     "  const float worst = sorted[count - 1];\n"
     "  g_recent_p50_ms = p50;\n"
     "  int hitches = 0;\n"
     "  for (size_t i = 0; i < count; ++i)\n"
     "    if (ms[i] > p50 * 2.0f) ++hitches;\n"
     "  REXLOG_INFO(\"[swap] "),
])
print("done")
