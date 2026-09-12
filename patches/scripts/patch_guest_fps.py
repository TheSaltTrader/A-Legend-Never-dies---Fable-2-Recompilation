"""The F8 counter showed the HOST present rate (170-200 on this machine) while
the game itself ran at 30-60. Publish the guest swap rate from the plugin as a
cvar and show THAT as FPS, with the host rate beside it, smaller."""
import os, sys

SDK = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
APP = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\fable2recomp"
CP = os.path.join(SDK, "src", "graphics", "d3d12", "command_processor.cpp")
HUD = os.path.join(APP, "src", "fable2_texnotify.cpp")

edits = []

edits.append((CP, """namespace {
void ReportGuestSwapRate() {
  using clock = std::chrono::steady_clock;
  static clock::time_point last_swap_at = clock::now();
  static clock::time_point window_start = last_swap_at;
  static float ms[1536] = {};
  static size_t count = 0;

  const auto now = clock::now();
  const float dt = std::chrono::duration<float, std::milli>(now - last_swap_at).count();
  last_swap_at = now;
  if (count < 1536)
    ms[count++] = dt;
  if (std::chrono::duration<double>(now - window_start).count() < 5.0)
    return;
""", """namespace {
void ReportGuestSwapRate() {
  using clock = std::chrono::steady_clock;
  static clock::time_point last_swap_at = clock::now();
  static clock::time_point window_start = last_swap_at;
  static float ms[1536] = {};
  static size_t count = 0;

  const auto now = clock::now();
  const float dt = std::chrono::duration<float, std::milli>(now - last_swap_at).count();
  last_swap_at = now;
  if (count < 1536)
    ms[count++] = dt;
  // The one-second rate, for an on-screen counter. The app's own counter
  // measures host presents, which run at the UI's pace (170-200 a second on
  // a 144 Hz panel) whether the game delivered a new frame or not; a counter
  // that says 190 while the game runs at 30 is worse than no counter.
  {
    static clock::time_point second_start = now;
    static uint32_t swaps = 0;
    ++swaps;
    const double secs = std::chrono::duration<double>(now - second_start).count();
    if (secs >= 1.0) {
      REXCVAR_SET(guest_fps_x10, int32_t(double(swaps) / secs * 10.0 + 0.5));
      second_start = now;
      swaps = 0;
    }
  }
  if (std::chrono::duration<double>(now - window_start).count() < 5.0)
    return;
"""))

# The cvar definition, next to the other GPU-published counters if there are
# any in this file; otherwise after the includes. Anchor on the include.
edits.append((CP, """#include <rex/cvar.h>
""", """#include <rex/cvar.h>

// Published for the app's on-screen counter: the game's OWN frame rate, from
// its swaps, over the last second, times ten. Read with
// REXCVAR_QUERY(int32_t, guest_fps_x10); 0 until the first second of swaps.
REXCVAR_DEFINE_INT32(guest_fps_x10, 0, "GPU",
                     "Guest frames per second x10 over the last second, from the game's "
                     "swaps (the game's own rate, not the host present rate)");
"""))

edits.append((HUD, """    if (s->hud_fps) {
      // Against 60, which is what the port targets with the 60 fps patch on
      // (30 without it - the colour is a hint, not a verdict).
      const ImVec4 c = p.fps >= 57.0f ? good : (p.fps >= 28.0f ? warn : bad);
      ImGui::TextColored(label, "FPS");
      ImGui::SameLine();
      ImGui::TextColored(c, "%5.1f", p.fps);
    }
""", """    if (s->hud_fps) {
      // The GAME's frame rate: swaps the guest completed, published by the GPU
      // plugin each second. The host presents 170-200 times a second on this
      // machine whether or not the game drew anything new, and a counter that
      // showed that while the game ran at 30 was read as "high fps but
      // laggy" (2026-09-11). Judged against 60, which is what the port targets
      // with the 60 fps patch on (30 without it - the colour is a hint, not a
      // verdict). The host rate stays beside it, smaller, because it is what
      // the V-Sync and frame-rate settings actually govern.
      const int32_t guest_x10 = REXCVAR_QUERY(int32_t, guest_fps_x10);
      const float game_fps = float(guest_x10) / 10.0f;
      const ImVec4 c = game_fps >= 57.0f ? good : (game_fps >= 28.0f ? warn : bad);
      ImGui::TextColored(label, "FPS");
      ImGui::SameLine();
      if (guest_x10 > 0) {
        ImGui::TextColored(c, "%5.1f", game_fps);
      } else {
        ImGui::TextColored(label, "  n/a");  // no swap in the last second
      }
      ImGui::SameLine();
      ImGui::TextColored(label, " host %.0f", p.fps);
    }
"""))

for path, old, new in edits:
    s = open(path, encoding="utf-8").read()
    n = s.count(old)
    if n != 1:
        print("FAIL: %d matches in %s for: %s" % (n, os.path.basename(path), old[:60].strip()))
        sys.exit(1)
    open(path, "w", encoding="utf-8", newline="").write(s.replace(old, new))
    print("ok  %s: %s" % (os.path.basename(path), old.strip().splitlines()[0][:60]))
print("all edits applied")
