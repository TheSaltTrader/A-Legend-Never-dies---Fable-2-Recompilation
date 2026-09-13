#include "fable2_texnotify.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

#include <cstdlib>
#include <string>

#include "fable2_menu.h"  // Fonts()
#include "fable2_perf.h"
#include "fable2_settings.h"
#include "fable2_viewstate.h"

namespace fable2 {
namespace {

// How long the indicator stays up, and how much of that is spent fading. Long
// enough to read after pressing the key, short enough not to sit over the game.
constexpr double kHoldSeconds = 3.5;
constexpr double kFadeSeconds = 0.9;

std::atomic<double> g_shown_at{-1000.0};
std::atomic<bool> g_enabled{false};

double NowSeconds() {
  using clock = std::chrono::steady_clock;
  static const auto t0 = clock::now();
  return std::chrono::duration<double>(clock::now() - t0).count();
}

// Every overlay here is the same kind of window: no chrome, no input, never
// focused. NoInputs matters most - none of these may take a click away from
// the game, and they are drawn while the player is holding a controller.
constexpr ImGuiWindowFlags kOverlayFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;

}  // namespace

// The settings the HUD reads. Pointed at the app's own settings object so the
// readouts follow the checkboxes with no copying and no staleness.
static const Fable2Settings* g_hud_settings = nullptr;

// FABLE2_HUD=1 shows every readout for this process whatever the settings
// say, and saves nothing: scripted test runs share the settings file with
// the player's own sessions, and a run whose frames carried no numbers
// because the player had toggled F8 the night before was worthless
// (2026-09-12). Read once; an environment variable does not change.
static bool HudForced() {
  static const bool forced = [] {
    const char* e = std::getenv("FABLE2_HUD");
    return e && *e && *e != '0';
  }();
  return forced;
}
void SetHudSettings(const Fable2Settings* settings) { g_hud_settings = settings; }

// The F8 state. Session-only on purpose: the readouts are meant to be there
// at every launch, and a toggle that was saved once left a test run's frames
// without numbers (2026-09-12).
static std::atomic<bool> g_hud_hidden{false};
bool ToggleHudHidden() {
  const bool now = !g_hud_hidden.load(std::memory_order_acquire);
  g_hud_hidden.store(now, std::memory_order_release);
  return now;
}

// A thin bar under a readout, coloured like its number. ImGui's ProgressBar
// takes its fill colour from the style, so it is pushed around the call; an
// empty overlay string draws no text.
static void ReadoutBar(float fraction, const ImVec4& colour) {
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colour);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.20f, 0.19f, 0.85f));
  ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(160.0f, 6.0f), "");
  ImGui::PopStyleColor(2);
}

void NotifyTexturePack(bool enabled) {
  g_enabled.store(enabled, std::memory_order_release);
  g_shown_at.store(NowSeconds(), std::memory_order_release);
}

TextureNotifyOverlay::TextureNotifyOverlay(rex::ui::ImGuiDrawer* drawer)
    : rex::ui::ImGuiDialog(drawer) {}

TextureNotifyOverlay::~TextureNotifyOverlay() = default;

void TextureNotifyOverlay::OnDraw(ImGuiIO& io) {
  (void)io;
  const double age = NowSeconds() - g_shown_at.load(std::memory_order_acquire);
  if (age < 0.0 || age > kHoldSeconds)
    return;

  float alpha = 1.0f;
  if (age > kHoldSeconds - kFadeSeconds)
    alpha = float((kHoldSeconds - age) / kFadeSeconds);
  alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);

  const bool enabled = g_enabled.load(std::memory_order_acquire);

  // Read across the DLL boundary: these are defined by the GPU plugin, which is
  // not on this executable's link line, so the registry is the only route.
  const int32_t replaced = REXCVAR_QUERY(int32_t, texture_pack_replaced);
  const int32_t original = REXCVAR_QUERY(int32_t, texture_pack_original);

  char line1[128];
  char line2[160];
  std::snprintf(line1, sizeof(line1), "%s",
                enabled ? "ENHANCED TEXTURES" : "ORIGINAL TEXTURES");
  if (enabled) {
    std::snprintf(line2, sizeof(line2), "%d enhanced loaded, %d original (not in pack)",
                  replaced, original);
  } else {
    std::snprintf(line2, sizeof(line2), "%d original loaded - the pack is off", original);
  }

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 24.0f, vp->WorkPos.y + 22.0f));
  ImGui::SetNextWindowBgAlpha(0.55f * alpha);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
  if (ImGui::Begin("##fable2_texnotify", nullptr, kOverlayFlags)) {
    // Green when the pack is on, a muted grey-green when it is off, so the two
    // states are told apart at a glance and not only by reading the words.
    const ImVec4 on(0.30f, 0.95f, 0.42f, alpha);
    const ImVec4 off(0.62f, 0.72f, 0.64f, alpha);
    if (ImFont* f = Fonts().title)
      ImGui::PushFont(f);
    ImGui::TextColored(enabled ? on : off, "%s", line1);
    if (Fonts().title)
      ImGui::PopFont();
    ImGui::TextColored(ImVec4(0.85f, 0.90f, 0.86f, alpha * 0.92f), "%s", line2);
    ImGui::TextColored(ImVec4(0.70f, 0.76f, 0.71f, alpha * 0.75f), "F9 to switch");
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void PerfHudOverlay::OnDraw(ImGuiIO& io) {
  // The window's aspect, for the ultrawide projection (patch_hooks.cpp reads
  // fable2_display_aspect_x1000). Once per change, not per frame.
  {
    static int last_aspect = 0;
    const int aspect = io.DisplaySize.y > 0.0f
                           ? int(io.DisplaySize.x / io.DisplaySize.y * 1000.0f + 0.5f)
                           : 0;
    if (aspect > 0 && aspect != last_aspect) {
      last_aspect = aspect;
      rex::cvar::SetFlagByName("fable2_display_aspect_x1000", std::to_string(aspect));
    }
    // Ultrawide presentation, per frame: a frame with a WORLD camera behind
    // it (built within the last quarter second; its projection is already
    // made for this display) is stretched edge to edge; the title, the main
    // menus, the loading map and every other camera-less frame keep 16:9
    // with bars - the user's rule. Only while the switch is on; otherwise
    // the tuning and the menu own the cvar.
    {
      static int last_want = -1;
      const Fable2Settings* st = g_hud_settings;
      if (st && st->ultrawide && aspect > 1800) {
        // Edge to edge in the world scene, bars in the loading and menu
        // scenes (patch_hooks.cpp keeps the scene from the cameras it sees;
        // a still camera during a dialogue keeps its scene, so nothing
        // flips while the wide world is on screen). Each state is held a
        // quarter second: a switch is a presenter re-layout, and flapping
        // per frame once cost 12 fps.
        static std::chrono::steady_clock::time_point last_switch{};
        static std::chrono::steady_clock::time_point last_check{};
        const auto now = std::chrono::steady_clock::now();
        // Inside the world scene the presenter never switches: the save
        // screen and the treasure popup draw the world as a captured picture
        // with 2D art on top, and a draw-count gate (builds 52-54) squeezed
        // them to 16:9 - the user's rule is that nothing in the world is
        // resized. The plugin's per-frame draw counts are still logged
        // below, to look for a signature of the map and Start menus.
        static const bool have_draw_stat = rex::cvar::GetFlagInfo("gpu_frame_depth_draws") != nullptr;
        const int32_t depth_draws =
            have_draw_stat ? rex::cvar::Query<int32_t>("gpu_frame_depth_draws") : -1;
        {
          // [scene] the frame's draw counts, logged when either moves by a
          // quarter (at most ten a second), with the 3D share.
          static int32_t last_depth = -1000, last_all = -1000;
          static std::chrono::steady_clock::time_point bucket_sec{};
          static int bucket_lines = 0;
          const int32_t all_draws = have_draw_stat ? rex::cvar::Query<int32_t>("gpu_frame_draws") : -1;
          auto moved = [](int32_t a, int32_t b) {
            const int32_t m = std::max(std::abs(a), std::abs(b));
            return std::abs(a - b) * 4 > m;
          };
          if (moved(depth_draws, last_depth) || moved(all_draws, last_all)) {
            if (now - bucket_sec > std::chrono::seconds(1)) { bucket_sec = now; bucket_lines = 0; }
            if (bucket_lines++ < 10)
              REXLOG_INFO("[scene] frame depth-tested draws {} of {} ({}% 3D), world camera {}", depth_draws,
                          all_draws, all_draws > 0 ? depth_draws * 100 / all_draws : 0,
                          fable2::WorldCameraLive() ? "live" : "not live");
            last_depth = depth_draws;
            last_all = all_draws;
          }
        }
        const bool world = fable2::WorldCameraLive();
        const int want = world ? 0 : 1;
        const bool held = now - last_switch < std::chrono::milliseconds(250);
        if (want != last_want && !held) {
          last_want = want;
          last_switch = now;
          rex::cvar::SetFlagByName("present_letterbox", want ? "true" : "false");
          REXLOG_INFO("[ultrawide] presenter -> {} ({:.3f} s since a world camera build)",
                      want ? "16:9 with bars" : "edge to edge",
                      fable2::SecondsSinceWorldCameraBuild());
        } else if (last_want >= 0 && now - last_check > std::chrono::milliseconds(500)) {
          // Any other settings change re-applies the letterbox and would
          // undo this (seen with the black-texture fix): re-assert, but
          // not more than twice a second.
          last_check = now;
          const char* wanted = last_want ? "true" : "false";
          if (rex::cvar::GetFlagByName("present_letterbox") != wanted)
            rex::cvar::SetFlagByName("present_letterbox", wanted);
        }
      } else {
        last_want = -1;
      }
    }
  }
  // Counted here rather than on a timer: this runs once per PRESENTED frame, so
  // it measures frames the player actually saw.
  PerfFrameTick();

  const Fable2Settings* s = g_hud_settings;
  if (!s)
    return;
  const bool forced = HudForced();
  const bool show_fps = forced || s->hud_fps;
  const bool show_cpu = forced || s->hud_cpu;
  const bool show_cpu_bar = forced || s->hud_cpu_bar;
  const bool show_gpu = forced || s->hud_gpu;
  const bool show_vram = forced || s->hud_vram;
  const bool show_gpu_bar = forced || s->hud_gpu_bar;
  const bool show_vram_bar = forced || s->hud_vram_bar;
  const bool hidden = g_hud_hidden.load(std::memory_order_acquire);
  if (!(forced || (s->hud_enabled && !hidden)) ||
      (!show_fps && !show_cpu && !show_gpu && !show_vram))
    return;

  const PerfSample p = GetPerfSample();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + vp->WorkSize.x - 18.0f, vp->WorkPos.y + 18.0f),
      ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.42f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 7.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
  if (ImGui::Begin("##fable2_perfhud", nullptr, kOverlayFlags)) {
    const ImVec4 good(0.55f, 0.95f, 0.60f, 1.0f);
    const ImVec4 warn(0.98f, 0.82f, 0.35f, 1.0f);
    const ImVec4 bad(0.98f, 0.45f, 0.40f, 1.0f);
    const ImVec4 label(0.72f, 0.78f, 0.74f, 1.0f);

    if (show_fps) {
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
    if (show_cpu) {
      // This process across all cores (fable2_perf.h says why not the
      // machine), and the same figure in cores, because one game thread
      // flat out is 100% of a core and 3% of this box.
      static const float cores = float(std::max(1u, std::thread::hardware_concurrency()));
      const float busy_cores = p.cpu_percent / 100.0f * cores;
      const ImVec4 c = p.cpu_percent < 50.0f ? good : (p.cpu_percent < 80.0f ? warn : bad);
      ImGui::TextColored(label, "CPU");
      ImGui::SameLine();
      ImGui::TextColored(c, "%5.0f%%", p.cpu_percent);
      ImGui::SameLine();
      ImGui::TextColored(label, " %.1f of %.0f cores", busy_cores, cores);
      if (show_cpu_bar) ReadoutBar(p.cpu_percent / 100.0f, c);
    }
    if (show_gpu) {
      ImGui::TextColored(label, "GPU");
      ImGui::SameLine();
      if (p.gpu_valid) {
        const ImVec4 c = p.gpu_percent < 80.0f ? good : (p.gpu_percent < 95.0f ? warn : bad);
        ImGui::TextColored(c, "%5.0f%%", p.gpu_percent);
        if (show_gpu_bar) ReadoutBar(p.gpu_percent / 100.0f, c);
      } else {
        // Never a zero that looks like an idle GPU.
        ImGui::TextColored(label, "    n/a");
      }
    }
    if (show_vram) {
      ImGui::TextColored(label, "VRAM");
      ImGui::SameLine();
      if (p.vram_valid) {
        const float frac = p.vram_total_mb > 0.0f ? p.vram_mb / p.vram_total_mb : 0.0f;
        const ImVec4 c = frac < 0.7f ? good : (frac < 0.9f ? warn : bad);
        ImGui::TextColored(c, "%.1f / %.1f GB", p.vram_mb / 1024.0f,
                           p.vram_total_mb / 1024.0f);
        if (show_vram_bar) ReadoutBar(frac, c);
      } else {
        ImGui::TextColored(label, "n/a");
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

WarmState GetWarmState() {
  WarmState w;
  // Read across the DLL boundary: these are the GPU plugin's, and the registry
  // is the only route to them from here.
  w.total = REXCVAR_QUERY(int32_t, texture_warm_total);
  w.done = REXCVAR_QUERY(int32_t, texture_warm_done);
  w.warming = w.total > 0 && w.done < w.total;
  w.fraction = w.total > 0 ? float(w.done) / float(w.total) : 0.0f;
  if (w.fraction > 1.0f)
    w.fraction = 1.0f;
  return w;
}

WarmOverlay::WarmOverlay(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}
WarmOverlay::~WarmOverlay() = default;

void WarmOverlay::OnDraw(ImGuiIO& io) {
  (void)io;
  // Held briefly at 100% so the bar is SEEN to finish. Vanishing the instant
  // the last file lands makes a fast stage look like nothing happened, and
  // leaves the player wondering whether it ran at all.
  static double full_at = -1000.0;
  const WarmState w = GetWarmState();
  const double now = ImGui::GetTime();
  if (w.warming)
    full_at = now;
  const bool linger = (now - full_at) < 1.2;
  if (!w.warming && !linger)
    return;

  const float fraction = w.warming ? w.fraction : 1.0f;
  const bool complete = fraction >= 0.999f;

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 24.0f, vp->WorkPos.y + 22.0f));
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
  if (ImGui::Begin("##fable2_warm", nullptr, kOverlayFlags)) {
    if (ImFont* f = Fonts().title)
      ImGui::PushFont(f);
    ImGui::TextColored(complete ? ImVec4(0.30f, 0.95f, 0.42f, 1.0f)
                                : ImVec4(0.45f, 0.70f, 1.00f, 1.0f),
                       complete ? "TEXTURE CACHE READY" : "LOADING TEXTURE CACHE");
    if (Fonts().title)
      ImGui::PopFont();

    // Blue while filling, green when full - the colour is the state, so it
    // reads at a glance without anyone parsing the numbers.
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          complete ? ImVec4(0.24f, 0.80f, 0.35f, 1.0f)
                                   : ImVec4(0.26f, 0.55f, 0.95f, 1.0f));
    char overlay_text[48];
    std::snprintf(overlay_text, sizeof(overlay_text), "%d / %d  (%.0f%%)",
                  w.warming ? w.done : w.total, w.total ? w.total : w.done,
                  fraction * 100.0f);
    ImGui::ProgressBar(fraction, ImVec2(300.0f, 0.0f), overlay_text);
    ImGui::PopStyleColor();
    if (!complete)
      ImGui::TextColored(ImVec4(0.80f, 0.86f, 0.82f, 0.85f),
                         "Please wait - loading this region's textures.");
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

PerfHudOverlay::PerfHudOverlay(rex::ui::ImGuiDrawer* drawer)
    : rex::ui::ImGuiDialog(drawer) {}
PerfHudOverlay::~PerfHudOverlay() = default;

}  // namespace fable2
