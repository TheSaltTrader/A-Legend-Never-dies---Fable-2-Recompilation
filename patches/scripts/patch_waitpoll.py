"""WAIT_REG_MEM: poll finely for the first 2 ms of a wait, regardless of vsync.

Measured on Fable II (2026-09-12, sampler on the GPU Commands thread): with
vsync on, the one-millisecond sleeps in this loop plus the fence waits were
25-35% of the thread's time and the game held 40-45 fps in town; with vsync
off (which takes the yield branch here AND raises the vblank rate to 1 kHz)
the same scenes ran 60-73. The vblank rate must stay at the refresh - the
game paces on it - so only the poll changes: yield for the first 2 ms of any
wait, then sleep as before. Waits inside a frame are short; the long ones
(idle at a menu, a load) still sleep and do not pin the core.
"""
import os, sys

C = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\command_processor.cpp"

edits = []
edits.append((C, """  bool matched = false;
  do {
    uint32_t value = 0;
    if (is_memory) {
      value =
          *reinterpret_cast<uint32_t*>(memory_->TranslatePhysical(poll_reg_addr & ~uint32_t(0x3)));""",
"""  bool matched = false;
  // When this wait began: the first two milliseconds are polled with a yield,
  // the rest with the packet's own sleep interval. See the block below.
  const auto wait_started = std::chrono::steady_clock::now();
  do {
    uint32_t value = 0;
    if (is_memory) {
      value =
          *reinterpret_cast<uint32_t*>(memory_->TranslatePhysical(poll_reg_addr & ~uint32_t(0x3)));"""))

edits.append((C, """    if (!matched) {
      // Wait.
      if (wait >= 0x100) {
        PrepareForWait();
        if (!REXCVAR_GET(vsync)) {
          // User wants it fast and dangerous.
          rex::thread::MaybeYield();
        } else {
          rex::thread::Sleep(std::chrono::milliseconds(wait / 0x100));
        }
        rex::thread::SyncMemory();
        ReturnFromWait();""",
"""    if (!matched) {
      // Wait.
      if (wait >= 0x100) {
        PrepareForWait();
        // A sleep here lasts at least a timer tick, and the game issues these
        // waits inside its frame - for its own fences, the swap, coherency.
        // Measured on Fable II with the in-process sampler: the millisecond
        // sleeps in this branch plus the fence waits were a third of the GPU
        // thread's time with vsync on, and the game held 40-45 fps in town;
        // the yield branch (vsync off) ran the same scenes at 60 and above.
        // So the first two milliseconds of every wait are polled with a yield
        // whatever vsync says, and only a wait that outlives them sleeps for
        // the packet's interval - a load screen or an idle menu, not a frame.
        const bool fresh = std::chrono::steady_clock::now() - wait_started <
                           std::chrono::milliseconds(2);
        if (fresh || !REXCVAR_GET(vsync)) {
          rex::thread::MaybeYield();
        } else {
          rex::thread::Sleep(std::chrono::milliseconds(wait / 0x100));
        }
        rex::thread::SyncMemory();
        ReturnFromWait();"""))

for path, old, new in edits:
    s = open(path, encoding="utf-8").read()
    n = s.count(old)
    if n != 1:
        print("FAIL: %d matches for: %s" % (n, old[:60].strip()))
        sys.exit(1)
    open(path, "w", encoding="utf-8", newline="").write(s.replace(old, new))
    print("ok:", old.strip().splitlines()[0][:60])
if "#include <chrono>" not in open(C, encoding="utf-8").read():
    s = open(C, encoding="utf-8").read()
    s = s.replace("#include <rex/cvar.h>\n", "#include <chrono>\n\n#include <rex/cvar.h>\n", 1)
    open(C, "w", encoding="utf-8", newline="").write(s)
    print("added <chrono>")
print("all edits applied")
