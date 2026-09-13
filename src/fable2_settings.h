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
#include <map>
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

  // What the title is told the display is used to be two more fields here.
  // It is now derived: the largest 16:9 box inside the window, computed in
  // ApplyDisplaySettings. A separate setting could be pointed at the window's
  // own shape, and then the presenter had no aspect difference to pillarbox -
  // the whole picture stretched on any ultrawide, with "Keep aspect ratio" on
  // and doing nothing. The game renders 16:9 whatever it is told.
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
  std::string antialias = "none";  // none | fxaa | fxaa_extreme

  // --- Upscaling ------------------------------------------------------------
  // The filter used to scale the guest's output up to the window. This matters
  // for this title specifically: the guest renders 1120x720 (1280x720 with the
  // community patch), so on any modern display the image is ALWAYS being
  // upscaled - the only question is by what.
  //
  // For a long time this menu said no such choice existed, because the shipped
  // runtime advertises present_effect="bilinear" and nothing else. That turned
  // out to be a build-configuration artefact, not a limit of the runtime: the
  // FSR and CAS shaders are compiled and committed in the SDK tree, but the
  // cvar that selects them sits behind REX_HAS_FIDELITYFX_SDK, which is off
  // unless the whole FidelityFX SDK is fetched - even though the spatial
  // shaders need nothing from it. Our SDK build turns that define on by
  // itself (REXGLUE_FIDELITYFX_SPATIAL_ONLY), so these values are real here.
  //
  //   bilinear - the stock filter; soft, and what every build before this had
  //   fsr      - FidelityFX FSR 1.0 (EASU + RCAS). Edge-aware upscale, then a
  //              sharpening pass. The right default for a 720p guest on a
  //              1080p or 4K window.
  //   cas      - Contrast-Adaptive Sharpening. Sharpens without the upscale
  //              stage; better when the window is near the guest resolution.
  //
  // fsr2/fsr3 are deliberately NOT offered. They are a temporal upscaler that
  // needs real depth and motion vectors; the runtime synthesizes those, warns
  // that it does, and falls back to spatial FSR anyway when the ffx-api library
  // is absent - which it is in this build. Listing them would be three names
  // for one filter.
  //
  // Requires a restart: present_effect is declared kRequiresRestart because the
  // presenter builds its pipelines for the chosen effect once.
  std::string present_effect = "fsr";  // bilinear | fsr | cas

  // Extra CAS sharpening, 0..1. Only read when present_effect is "cas".
  double cas_sharpness = 0.0;

  // FSR's RCAS sharpness reduction in stops, 0..2. LOWER IS SHARPER - it is a
  // reduction, not an amount. Only read when present_effect is "fsr".
  double fsr_sharpness = 0.2;

  // Repair NaN in the vertex shader float constants before they reach the GPU.
  // 0 off, 1 substitute zero, 2 substitute an identity matrix row.
  //
  // DEFAULTS TO OFF, because the bug it worked around is fixed. The NaN came
  // from the codegen localising VMX128 registers v64-v127, so functions handed
  // a value in one read zero; regenerating with that fixed took NaN in these
  // constants from 6.28% to 0.79% and the flat-blue frames from 3-in-16 to
  // none.
  //
  // Leaving the repair ON after that fix is actively HARMFUL: with the real
  // NaN gone, substitution mostly overwrites values that were fine, and the
  // scene goes black. It stays here as a diagnostic - 1 substitutes zero,
  // 2 an identity matrix row - and because it is the measurement that proved
  // the NaN was causal in the first place.
  int nan_constant_repair = 0;
  // Texture pack and texture cache, ported from the NG2 port.
  //
  // texture_dump writes every unique guest texture out as raw guest bytes plus
  // its key; tools/upscale_textures.py untiles and decodes them offline. The
  // plugin converts textures ON THE GPU, so finished pixels never exist
  // CPU-side and dumping the raw bytes is the cheap way to get at them.
  //
  // texture_cache_mb of 0 means "leave the plugin's own limits alone", so the
  // setting is only sent when it has actually been chosen.
  // Folder holding Xbox 360 save packages to import at startup.
  // Imported in OnPostSetup, which is after the ContentManager and
  // profile exist and before the guest looks for saves.
  std::string save_import_path;
  // fuzzy_alpha is the plugin's own fix for alpha-test flicker.
  // accurate_depth is kept only so older settings files still parse: the
  // option was removed from the menu on 2026-09-13 (it broke pipeline
  // creation on this title) and the tuning ignores its value.
  bool accurate_depth = false;
  bool fuzzy_alpha = false;
  // Start without the Microsoft and Lionhead logo videos (the list hook in
  // patch_hooks.cpp). Its own switch, so the logos can be kept on purpose.
  bool skip_logos = true;
  // Press A through a chapter's cinematic for you. A synthetic pad, ORed
  // into the real one; any genuine input disarms it immediately.
  bool skip_intro = true;
  bool texture_dump = false;
  // OFF by default. The replacement path is new code in the shared plugin,
  // and with it on the game crashed at character select - a screen that is
  // stable for 8 straight frames with it off. Until that is understood this
  // stays opt-in.
  // On-screen readouts. ON at every launch - the player wants the numbers
  // there when the game comes up (2026-09-12); F8 hides them for the session
  // only and is not saved, so the next launch shows them again.
  bool hud_enabled = true;
  bool hud_fps = true;
  bool hud_cpu = true;       // this process's CPU, across all cores
  bool hud_cpu_bar = true;   // a bar under the CPU number
  bool hud_gpu = true;
  bool hud_gpu_bar = true;   // a bar under the GPU number
  bool hud_vram = true;
  bool hud_vram_bar = true;  // a bar under the VRAM number
  bool hud_menu_bars = true;   // the live cost bars in the Textures panel
  // A LATCH, not a preference: hardware detection runs once and records
  // that it ran, so a value changed by hand is never overwritten later.
  bool hardware_detected = false;
  bool texture_pack = false;
  // How far the packer enlarges. 2x is the recommendation and the
  // measured one: replacements are uncompressed, so 4x came to about
  // 6 GB against a 4 GB maximum soft cache and thrashed.
  int texture_scale = 2;
  // Real-ESRGAN, off until it is downloaded. Its DETAIL is laid over a
  // plain resize rather than replacing the image, so tone and colour
  // stay the game's.
  // ON by default: the upscaler ships with the port, so there is nothing to
  // opt into. Only the pack build uses it, and that is an explicit button.
  bool texture_ai = true;
  float texture_ai_strength = 0.75f;
  std::string texture_path;
  int texture_cache_mb = 0;
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
  // Key bindings the player changed on the Keyboard bindings screen, by the
  // runtime's cvar name ("keybind_a" -> "Semicolon,Space"). Only what was
  // changed is here; fable2_keyremap.cpp holds the defaults, and the tuning
  // emits every action from one or the other.
  std::map<std::string, std::string> keybinds;
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
    // EVERY key Apply() understands must be written here. They drifted apart
    // once already: Save() wrote 23 keys while Apply() read 30, so the
    // graphics engine, the black-texture readback fix and all five community
    // patches could be loaded but never written back. The effect was silent
    // and destructive - changing any setting in the menu rewrote the file
    // without them, wiping the player's patches. If you add a field, add it in
    // both places.
    out << "# Fable II recompilation settings\n"
        << "window_width=" << window_width << "\n"
        << "window_height=" << window_height << "\n"
        << "fullscreen=" << (fullscreen ? 1 : 0) << "\n"
        << "monitor=" << monitor << "\n"
        << "fps=" << fps << "\n"
        << "vsync=" << (vsync ? 1 : 0) << "\n"
        << "gpu_backend=" << gpu_backend << "\n"
        << "resolution_scale=" << resolution_scale << "\n"
        << "anisotropic=" << anisotropic << "\n"
        << "antialias=" << antialias << "\n"
        << "present_effect=" << present_effect << "\n"
        << "cas_sharpness=" << cas_sharpness << "\n"
        << "fsr_sharpness=" << fsr_sharpness << "\n"
        << "nan_constant_repair=" << nan_constant_repair << "\n"
        << "save_import_path=" << save_import_path << "\n"
        << "accurate_depth=" << (accurate_depth ? 1 : 0) << "\n"
        << "fuzzy_alpha=" << (fuzzy_alpha ? 1 : 0) << "\n"
        << "skip_logos=" << (skip_logos ? 1 : 0) << "\n"
        << "skip_intro=" << (skip_intro ? 1 : 0) << "\n"
        << "texture_dump=" << (texture_dump ? 1 : 0) << "\n"
        << "hud_enabled=" << (hud_enabled ? 1 : 0) << "\n"
        << "hud_fps=" << (hud_fps ? 1 : 0) << "\n"
        << "hud_cpu=" << (hud_cpu ? 1 : 0) << "\n"
        << "hud_cpu_bar=" << (hud_cpu_bar ? 1 : 0) << "\n"
        << "hud_gpu=" << (hud_gpu ? 1 : 0) << "\n"
        << "hud_vram=" << (hud_vram ? 1 : 0) << "\n"
        << "hud_gpu_bar=" << (hud_gpu_bar ? 1 : 0) << "\n"
        << "hud_vram_bar=" << (hud_vram_bar ? 1 : 0) << "\n"
        << "hud_menu_bars=" << (hud_menu_bars ? 1 : 0) << "\n"
        << "hardware_detected=" << (hardware_detected ? 1 : 0) << "\n"
        << "texture_pack=" << (texture_pack ? 1 : 0) << "\n"
        << "texture_scale=" << texture_scale << "\n"
        << "texture_ai=" << (texture_ai ? 1 : 0) << "\n"
        << "texture_ai_strength=" << texture_ai_strength << "\n"
        << "texture_path=" << texture_path << "\n"
        << "texture_cache_mb=" << texture_cache_mb << "\n"
        << "present_dither=" << (present_dither ? 1 : 0) << "\n"
        << "letterbox=" << (letterbox ? 1 : 0) << "\n"
        << "readback=" << readback << "\n"
        << "patch_60fps=" << (patch_60fps ? 1 : 0) << "\n"
        << "patch_720p=" << (patch_720p ? 1 : 0) << "\n"
        << "patch_disable_msaa=" << (patch_disable_msaa ? 1 : 0) << "\n"
        << "patch_disable_texture_morph="
        << (patch_disable_texture_morph ? 1 : 0) << "\n"
        << "patch_high_tick_rate=" << (patch_high_tick_rate ? 1 : 0) << "\n"
        << "cursor_hide_seconds=" << cursor_hide_seconds << "\n"
        << "mute=" << (mute ? 1 : 0) << "\n"
        << "audio_queue_frames=" << audio_queue_frames << "\n"
        << "keyboard_control=" << (keyboard_control ? 1 : 0) << "\n";
    for (const auto& kv : keybinds)
      out << kv.first << "=" << kv.second << "\n";
    out
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
    fps = std::clamp(fps, 24, 240);
    resolution_scale = std::clamp(resolution_scale, 1, 8);
    nan_constant_repair = std::clamp(nan_constant_repair, 0, 2);
    texture_cache_mb = std::clamp(texture_cache_mb, 0, 8192);
    if (texture_scale != 2 && texture_scale != 4 && texture_scale != 8) {
      texture_scale = 2;
    }
    texture_ai_strength = std::clamp(texture_ai_strength, 0.0f, 1.0f);
    anisotropic = std::clamp(anisotropic, -1, 5);
    if (antialias != "none" && antialias != "fxaa" && antialias != "fxaa_extreme")
      antialias = "none";
    // A build against a stock SDK only accepts "bilinear"; sending it "fsr"
    // there is harmless (the runtime's parser falls through to bilinear), so
    // the value is kept rather than silently rewritten.
    if (present_effect != "bilinear" && present_effect != "fsr" &&
        present_effect != "cas")
      present_effect = "bilinear";
    cas_sharpness = std::clamp(cas_sharpness, 0.0, 1.0);
    fsr_sharpness = std::clamp(fsr_sharpness, 0.0, 2.0);
    audio_queue_frames = std::clamp(audio_queue_frames, 4, 64);
    mouse_sensitivity = std::clamp(mouse_sensitivity, 0.01, 10.0);
    cursor_hide_seconds = std::clamp(cursor_hide_seconds, 0, 60);
    if (readback != "none" && readback != "fast" && readback != "some" &&
        readback != "full")
      readback = "none";
    if (gpu_backend != "vulkan" && gpu_backend != "d3d12")
      gpu_backend = "d3d12";
    // Dumping and loading together put the disk on the GPU thread and starve
    // the command stream. The menu makes the pair impossible to select; this
    // makes it impossible to arrive with, from an older config or a hand edit.
    if (texture_dump && texture_pack)
      texture_pack = false;
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
    // video_width / video_height from older files are read and dropped: the
    // guest display is derived from the window now (see the Display block).
    else if (k == "fps") fps = std::atoi(v.c_str());
    else if (k == "vsync") vsync = Truthy(v);
    else if (k == "gpu_backend") gpu_backend = v;
    else if (k == "resolution_scale") resolution_scale = std::atoi(v.c_str());
    else if (k == "anisotropic") anisotropic = std::atoi(v.c_str());
    else if (k == "antialias") antialias = v;
    else if (k == "present_effect") present_effect = v;
    else if (k == "cas_sharpness") cas_sharpness = std::atof(v.c_str());
    else if (k == "fsr_sharpness") fsr_sharpness = std::atof(v.c_str());
    else if (k == "nan_constant_repair") nan_constant_repair = std::atoi(v.c_str());
    else if (k == "save_import_path") save_import_path = v;
    else if (k == "accurate_depth") accurate_depth = Truthy(v);
    else if (k == "fuzzy_alpha") fuzzy_alpha = Truthy(v);
    else if (k == "skip_logos") skip_logos = Truthy(v);
    else if (k == "skip_intro") skip_intro = Truthy(v);
    else if (k == "texture_dump") texture_dump = Truthy(v);
    else if (k == "hud_enabled") hud_enabled = Truthy(v);
    else if (k == "hud_fps") hud_fps = Truthy(v);
    else if (k == "hud_cpu") hud_cpu = Truthy(v);
    else if (k == "hud_cpu_bar") hud_cpu_bar = Truthy(v);
    else if (k == "hud_gpu") hud_gpu = Truthy(v);
    else if (k == "hud_vram") hud_vram = Truthy(v);
    else if (k == "hud_gpu_bar") hud_gpu_bar = Truthy(v);
    else if (k == "hud_vram_bar") hud_vram_bar = Truthy(v);
    else if (k == "hud_menu_bars") hud_menu_bars = Truthy(v);
    else if (k == "hardware_detected") hardware_detected = Truthy(v);
    else if (k == "texture_pack") texture_pack = Truthy(v);
    else if (k == "texture_scale") texture_scale = std::atoi(v.c_str());
    else if (k == "texture_ai") texture_ai = Truthy(v);
    else if (k == "texture_ai_strength") texture_ai_strength = float(std::atof(v.c_str()));
    else if (k == "texture_path") texture_path = v;
    else if (k == "texture_cache_mb") texture_cache_mb = std::atoi(v.c_str());
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
    else if (k.rfind("keybind_", 0) == 0) keybinds[k] = v;
    else if (k == "mouse_look") mouse_look = Truthy(v);
    else if (k == "mouse_sensitivity") mouse_sensitivity = std::atof(v.c_str());
    else if (k == "game_path") game_path = v;
    else if (k == "configured") configured = Truthy(v);
  }
};
