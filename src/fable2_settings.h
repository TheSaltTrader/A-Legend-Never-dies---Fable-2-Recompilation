// Persistent PC settings for the Fable II recompilation.
//
// A flat key=value file next to the executable, so it can be edited by hand or
// written by the settings menu. Fields marked "restart" only take effect next
// launch, because the window and the guest video mode are created during
// startup.
//
// Every setting here maps to a cvar that was confirmed to exist by dumping the
// live registry (FABLE2_DUMP_CVARS). That matters: ng2recomp's menu once
// offered FSR and CAS sharpening because they looked plausible in the SDK
// headers, and this presenter implements neither.
//
// Two other config files exist and are deliberately separate:
//
//   cache/fable2_tuning.toml  derived, rewritten every launch from these
//                             values. Never edit it - it is the transport into
//                             the GPU plugin's cvars, not somewhere to keep
//                             anything (see fable2_tuning.h).
//   fable2.toml               the SDK's own cvar config, written by the raw
//                             cvar browser on F4.

#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <rex/filesystem.h>
#include <rex/logging.h>

struct Fable2Settings {
  // --- Display (restart) --------------------------------------------------
  // 0 means "let the app decide", which is what the window_width/height cvars
  // themselves default to.
  int window_width = 1280;
  int window_height = 720;
  bool fullscreen = false;
  int monitor = 0;                 // 0..16, 0 = default

  // What the title is told the display is. This, not the window size, is what
  // changes the rendered resolution and the rate the game targets.
  int video_width = 1280;          // cvar range 640..4095
  int video_height = 720;          // cvar range 480..4095
  int fps = 60;                    // video_mode_refresh_rate, 24..240

  bool vsync = true;

  // --- Graphics -----------------------------------------------------------
  // Which graphics API the Xenos plugin renders through.
  //
  // MEASURED: d3d12 is the better default here. Vulkan was built and tried
  // specifically to see whether the flat-blue scene was a D3D12
  // render-target-path fault; it is not. On Vulkan the blue is still there AND
  // character meshes stop drawing entirely, so it is strictly worse.
  //
  // That result is worth more than it cost: the same bug on BOTH backends
  // means the defect is in the SHARED GPU code, not the D3D12 path.
  //
  // The choice is kept because it is how that was established, and because the
  // Vulkan path may improve. It needs a plugin built from source with
  // -DREXGLUE_USE_VULKAN=ON (the SDK's stock Windows plugin is D3D12-only);
  // with a stock plugin the app logs the miss and falls back rather than
  // failing to start.
  std::string gpu_backend = "d3d12";   // vulkan | d3d12

  // True internal supersampling: the guest's framebuffer is rendered at this
  // multiple and downsampled. The cvar's own range is 1..8.
  int resolution_scale = 1;

  // -1 = leave the game's own sampler settings alone, 0..5 = force a level.
  // Range read off anisotropic_override, which is -1..5 here (ng2recomp's
  // notes say -1..4; do not trust that, trust the dump).
  int anisotropic = -1;

  // Post-process antialiasing applied to the swap image. The only real AA knob
  // the runtime has - swap_post_effect declares exactly these three values.
  // present_effect, by contrast, declares "bilinear" and nothing else, so
  // there is no upscaling filter to choose and the menu does not pretend
  // otherwise.
  std::string antialias = "none";  // none | fxaa | fxaa_extreme

  bool present_dither = false;
  bool letterbox = true;

  // The fix for Fable II's black-texture bug - the hero's and the dog's
  // textures turning black once the hero reaches adulthood. The unofficial
  // Xenia femtofork for this game solves it by reading back only the textures
  // that need it; this runtime already exposes that as a graduated setting
  // (readback_resolve: none / fast / some / full), so "some" is that fix
  // rather than the all-or-nothing readback that cripples performance.
  std::string readback = "none";   // none | fast | some | full

  // --- Community patches ---------------------------------------------------
  // Xenia Canary's patch file for 4D5307F1 (Margen67, Guy). Each is verified
  // against our own image in config/hooks/patches.toml. All off by default -
  // they change how the game shipped.
  bool patch_60fps = false;
  bool patch_720p = false;
  bool patch_disable_msaa = false;
  bool patch_disable_texture_morph = false;
  bool patch_high_tick_rate = false;

  // --- Audio --------------------------------------------------------------
  bool mute = false;
  int audio_queue_frames = 8;      // audio_maxqframes; lower = less latency

  // --- Comfort ------------------------------------------------------------
  // Seconds of mouse stillness over the window before the pointer hides.
  // 0 = never hide. Borrowed from re:Blue, which does the same thing.
  int cursor_hide_seconds = 5;

  // Drive the guest pad from keyboard and mouse. Off by default in the
  // runtime, which is a surprising default for a PC port.
  bool keyboard_control = false;
  bool mouse_look = false;         // mnk_mouse: mouse drives the right stick
  double mouse_sensitivity = 1.0;  // 0.01..10

  // --- Content ------------------------------------------------------------
  std::string game_path;           // folder containing default.xex; empty = game/

  // Set once Play has been pressed, so the setup screen only interrupts the
  // first run. Hold Shift at launch to get it back.
  bool configured = false;

  static std::filesystem::path Path() {
    return rex::filesystem::GetExecutableFolder() / "fable2_settings.cfg";
  }

  std::filesystem::path ResolvedGamePath() const {
    if (!game_path.empty()) return std::filesystem::path(game_path);
    return rex::filesystem::GetExecutableFolder() / "game";
  }

  void Load() {
    std::ifstream in(Path());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') continue;
      const auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string v = line.substr(eq + 1);
      while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
      Apply(line.substr(0, eq), v);
    }
    REXLOG_INFO("Settings: loaded {}", Path().string());
  }

  void Save() const {
    std::ofstream out(Path(), std::ios::trunc);
    if (!out) {
      REXLOG_WARN("Settings: cannot write {}", Path().string());
      return;
    }
    out << "# Fable II recompilation settings\n"
        << "window_width=" << window_width << "\n"
        << "window_height=" << window_height << "\n"
        << "fullscreen=" << (fullscreen ? 1 : 0) << "\n"
        << "monitor=" << monitor << "\n"
        << "video_width=" << video_width << "\n"
        << "video_height=" << video_height << "\n"
        << "fps=" << fps << "\n"
        << "vsync=" << (vsync ? 1 : 0) << "\n"
        << "resolution_scale=" << resolution_scale << "\n"
        << "anisotropic=" << anisotropic << "\n"
        << "antialias=" << antialias << "\n"
        << "present_dither=" << (present_dither ? 1 : 0) << "\n"
        << "letterbox=" << (letterbox ? 1 : 0) << "\n"
        << "mute=" << (mute ? 1 : 0) << "\n"
        << "audio_queue_frames=" << audio_queue_frames << "\n"
        << "keyboard_control=" << (keyboard_control ? 1 : 0) << "\n"
        << "mouse_look=" << (mouse_look ? 1 : 0) << "\n"
        << "mouse_sensitivity=" << mouse_sensitivity << "\n"
        << "game_path=" << game_path << "\n"
        << "configured=" << (configured ? 1 : 0) << "\n";
    REXLOG_INFO("Settings: saved {}", Path().string());
  }

  // Ranges come from the cvar dump, not from guesswork. Clamping here means
  // the menu cannot offer a value the runtime will refuse.
  void Clamp() {
    window_width = std::clamp(window_width, 640, 7680);
    window_height = std::clamp(window_height, 480, 4320);
    monitor = std::clamp(monitor, 0, 16);
    video_width = std::clamp(video_width, 640, 4095);
    video_height = std::clamp(video_height, 480, 4095);
    fps = std::clamp(fps, 24, 240);
    resolution_scale = std::clamp(resolution_scale, 1, 8);
    anisotropic = std::clamp(anisotropic, -1, 5);
    if (antialias != "none" && antialias != "fxaa" && antialias != "fxaa_extreme")
      antialias = "none";
    audio_queue_frames = std::clamp(audio_queue_frames, 4, 64);
    mouse_sensitivity = std::clamp(mouse_sensitivity, 0.01, 10.0);
    cursor_hide_seconds = std::clamp(cursor_hide_seconds, 0, 60);
    if (readback != "none" && readback != "fast" && readback != "some" &&
        readback != "full")
      readback = "none";
    if (gpu_backend != "vulkan" && gpu_backend != "d3d12")
      gpu_backend = "d3d12";
  }

 private:
  static bool Truthy(const std::string& v) {
    return v == "1" || v == "true" || v == "yes";
  }

  void Apply(const std::string& k, const std::string& v) {
    if (k == "window_width") window_width = std::atoi(v.c_str());
    else if (k == "window_height") window_height = std::atoi(v.c_str());
    else if (k == "fullscreen") fullscreen = Truthy(v);
    else if (k == "monitor") monitor = std::atoi(v.c_str());
    else if (k == "video_width") video_width = std::atoi(v.c_str());
    else if (k == "video_height") video_height = std::atoi(v.c_str());
    else if (k == "fps") fps = std::atoi(v.c_str());
    else if (k == "vsync") vsync = Truthy(v);
    else if (k == "gpu_backend") gpu_backend = v;
    else if (k == "resolution_scale") resolution_scale = std::atoi(v.c_str());
    else if (k == "anisotropic") anisotropic = std::atoi(v.c_str());
    else if (k == "antialias") antialias = v;
    else if (k == "present_dither") present_dither = Truthy(v);
    else if (k == "letterbox") letterbox = Truthy(v);
    else if (k == "readback") readback = v;
    else if (k == "patch_60fps") patch_60fps = Truthy(v);
    else if (k == "patch_720p") patch_720p = Truthy(v);
    else if (k == "patch_disable_msaa") patch_disable_msaa = Truthy(v);
    else if (k == "patch_disable_texture_morph") patch_disable_texture_morph = Truthy(v);
    else if (k == "patch_high_tick_rate") patch_high_tick_rate = Truthy(v);
    else if (k == "mute") mute = Truthy(v);
    else if (k == "audio_queue_frames") audio_queue_frames = std::atoi(v.c_str());
    else if (k == "cursor_hide_seconds") cursor_hide_seconds = std::atoi(v.c_str());
    else if (k == "keyboard_control") keyboard_control = Truthy(v);
    else if (k == "mouse_look") mouse_look = Truthy(v);
    else if (k == "mouse_sensitivity") mouse_sensitivity = std::atof(v.c_str());
    else if (k == "game_path") game_path = v;
    else if (k == "configured") configured = Truthy(v);
  }
};
