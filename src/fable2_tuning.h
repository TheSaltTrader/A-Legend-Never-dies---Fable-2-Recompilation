// Getting settings into the GPU plugin's cvars, which is harder than it looks.
//
// HOW IT IS APPLIED, AND WHY IT HAS TO BE THIS WAY
//
// The GPU plugin's cvars (draw_resolution_scale_x, swap_post_effect,
// anisotropic_override, ...) do not exist when the app starts: rexgpu-xenos.dll
// registers them as it loads, which is after OnPreSetup and before OnPostSetup.
// So:
//
//   * SetFlagByName in OnPreSetup fails - the flag is not registered yet.
//   * SetFlagByName in OnPostSetup succeeds and is too late - the plugin
//     latched the value at GPU init. On ng2recomp this is exactly what made
//     internal resolution scaling look impossible: the app was writing 1 back
//     over the real value after the plugin had already read it.
//
// cvar::LoadConfig is the one path that survives the gap. It *defers* values
// for cvars that are not registered yet ("Config: '{}' deferred (cvar not yet
// registered)") and applies them at registration. So the tuning is written to
// a TOML under the cache root and loaded from OnPreSetup.
//
// Read it back in OnPostSetup rather than trusting it. That readback is the
// only honest confirmation the values reached the plugin.

#pragma once

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "fable2_settings.h"

struct Fable2Tuning {
  struct Entry {
    const char* name;
    std::string value;
    const char* why;
  };

  // Title-specific correctness settings.
  //
  // The bar for this list is a citation or a measurement, in the comment. It
  // is NOT a place to copy ng2recomp's entries: those come from Xenia's
  // game-compatibility record for Ninja Gaiden II (clear_memory_page_state,
  // protect_zero, render_target_path_d3d12=rov) and none of them is about this
  // game. Copying a neighbouring project's compatibility flags is how a
  // working build gets broken.
  static std::vector<Entry> Fixed() {
    return {
        // [community] The Fable II guides are consistent that this must be on
        // for this title: left at the runtime's default of false the texture
        // fetch validation is too strict for what the game emits, and textures
        // get dropped - the visible symptom is missing ground detail, "no
        // grass". Xenia exposes the same flag for the same reason.
        {"gpu_allow_invalid_fetch_constants", "true",
         "[community] Fable II emits fetch constants the strict path rejects; "
         "without this, textures drop out (missing grass)"},

        // RETRACTED. This once set render_target_path_d3d12 = "rov", on a
        // measurement that turned out to be worthless: the run it came from
        // never reached the state that fails. Xenia Canary plays this title
        // through that point on RTV, so ROV was never the answer, and the
        // flat-blue scene reproduces on ROV, RTV, the default, AND on Vulkan.
        // Left at the runtime's default deliberately.
    };
  }

  // Settings the player controls. Every one of these is a cvar owned by the
  // GPU plugin or the presenter, so it has to travel the same deferred route.
  static std::vector<Entry> FromSettings(const Fable2Settings& s,
                                         const std::string& mappings_file) {
    std::vector<Entry> out;

    // Three names for one setting. `resolution_scale` is the convenience the
    // command line takes; the plugin itself reads draw_resolution_scale_x/y,
    // and on ng2recomp setting only the convenience left the GPU reporting
    // "internal scale 1x1" while the config said 2.
    const std::string scale = std::to_string(s.resolution_scale);
    out.push_back({"resolution_scale", scale,
                   "convenience alias, 1-8"});
    out.push_back({"draw_resolution_scale_x", scale,
                   "what the plugin actually reads for horizontal scale"});
    out.push_back({"draw_resolution_scale_y", scale,
                   "what the plugin actually reads for vertical scale"});

    out.push_back({"vsync", s.vsync ? "true" : "false", "present pacing"});

    // swap_post_effect declares exactly none / fxaa / fxaa_extreme, which is
    // the whole of the antialiasing this runtime has.
    out.push_back({"swap_post_effect", s.antialias,
                   "post-process antialiasing: none, fxaa, fxaa_extreme"});

    // The upscaling filter for guest output -> window. A stock SDK build
    // advertises only "bilinear" here, because the FSR/CAS shaders - which are
    // committed to the SDK tree and need nothing external - are gated behind a
    // define that is only set when the whole FidelityFX SDK is fetched. Our SDK
    // build sets it directly (REXGLUE_FIDELITYFX_SPATIAL_ONLY), so cas and fsr
    // are live. Against a stock runtime this value is simply parsed back to
    // bilinear rather than failing, so it is always safe to send.
    out.push_back({"present_effect", s.present_effect,
                   "upscaling filter: bilinear, fsr (FSR 1.0), cas"});

    // Only the knob belonging to the selected effect is sent. Sending both
    // would put two settings in the generated file that cannot both apply, and
    // on a stock runtime it would log a deferred-cvar line for each.
    if (s.present_effect == "fsr") {
      out.push_back({"present_fsr_sharpness_reduction", ToString(s.fsr_sharpness),
                     "FSR RCAS sharpness reduction in stops, 0-2 (LOWER is sharper)"});
    } else if (s.present_effect == "cas") {
      out.push_back({"present_cas_additional_sharpness", ToString(s.cas_sharpness),
                     "additional CAS sharpness, 0-1"});
    }

    // Sent unconditionally, including 0, so turning the repair off in the menu
    // actually reaches the plugin instead of leaving the previous value set.
    out.push_back({"diag_vs_const_nan_fix", std::to_string(s.nan_constant_repair),
                   "repair NaN in vertex shader constants: 0 off, 1 zero, 2 identity row"});

    // Exact float24 depth. Both halves or neither - converting in the pixel
    // shader without the matching rounding is a half-applied change.
    out.push_back({"depth_float24_convert_in_pixel_shader",
                   s.accurate_depth ? "true" : "false",
                   "exact float24 depth: costs shader work, buys depth precision"});
    out.push_back({"depth_float24_round", s.accurate_depth ? "true" : "false",
                   "the other half of exact float24 depth"});
    out.push_back({"use_fuzzy_alpha_epsilon", s.fuzzy_alpha ? "true" : "false",
                   "approximate alpha test - the plugin's fix for alpha flicker"});

    // Texture cache limits. 0 means leave the plugin's own defaults alone,
    // rather than restating them as though they were a choice.
    if (s.texture_cache_mb > 0) {
      out.push_back({"texture_cache_memory_limit_soft",
                     std::to_string(s.texture_cache_mb / 2),
                     "host memory the GPU may hold textures in"});
      out.push_back({"texture_cache_memory_limit_hard",
                     std::to_string(s.texture_cache_mb),
                     "the point at which it must evict"});
    }

    // Texture pack. These are GPU PLUGIN cvars, so this generated file is the
    // ONLY way to deliver them - a plugin cvar passed on the command line
    // reaches nothing at all.
    if (s.texture_dump && !s.texture_path.empty()) {
      out.push_back({"texture_dump", "true",
                     "write every unique guest texture out for upscaling"});
      out.push_back({"texture_dump_path", s.texture_path + "/dump",
                     "where dumped textures go"});
    }
    // Replacement landed in the shared SDK, so this is load-bearing now: the
    // plugin sizes the resource from the .tex header and uploads it straight
    // in, skipping the format-conversion compute shader.
    if (s.texture_pack && !s.texture_path.empty()) {
      out.push_back({"texture_pack_path", s.texture_path + "/pack",
                     "upscaled textures to load instead of the game's own"});
    }

    out.push_back({"present_dither", s.present_dither ? "true" : "false",
                   "dither the 10bpc output down to 8bpc"});
    out.push_back({"present_letterbox", s.letterbox ? "true" : "false",
                   "keep the guest aspect ratio instead of stretching"});

    // -1 means "leave the game's own samplers alone", so it is only sent when
    // the player actually overrode it. The dump gives the range as -1..5.
    if (s.anisotropic >= 0) {
      out.push_back({"anisotropic_override", std::to_string(s.anisotropic),
                     "forced anisotropic filtering level"});
    }

    // The black-texture fix. "some" is the femtofork's selective readback;
    // "full" is the blunt instrument that costs a lot of performance.
    out.push_back({"readback_resolve", s.readback,
                   "readback for the hero/dog black-texture bug"});

    // Memexport readback OFF. The SDK's default is on, and on this title it
    // was the frame: the game exports from shaders about five times a frame,
    // and each export made the GPU thread drain the whole GPU queue before
    // copying the result back - measured 1.9 to 3.9 s of every 5 s spent in
    // that wait, 17-45 fps in town. The double-buffered "fast" path never
    // applied because the previous frame's copy is never complete when it
    // checks. Off: a locked 60 in the same places, nothing visibly wrong in
    // play (2026-09-12). If something does read exported data on the CPU,
    // the answer is a one-frame-late copy, not this drain.
    out.push_back({"readback_memexport", "false",
                   "no full-queue drain per shader memory export"});

    // The community patches, read by the midasm hooks in patch_hooks.cpp.
    out.push_back({"fable2_60fps", s.patch_60fps ? "true" : "false",
                   "[Xenia/Margen67] 60 fps"});
    out.push_back({"fable2_720p", s.patch_720p ? "true" : "false",
                   "[Xenia/Margen67] render 1280 wide instead of 1120"});
    out.push_back({"fable2_disable_msaa",
                   s.patch_disable_msaa ? "true" : "false",
                   "[Xenia/Margen67] disable MSAA"});
    out.push_back({"fable2_disable_texture_morph",
                   s.patch_disable_texture_morph ? "true" : "false",
                   "[Xenia/Guy] disable texture morphing"});
    // Ours: the boot-logo list hook (patch_hooks.cpp). Driven by the same
    // switch as the synthetic A presses, which never shortened the logos.
    out.push_back({"fable2_skip_boot_logos", s.skip_intro ? "true" : "false",
                   "start without the Microsoft and Lionhead logo videos"});
    out.push_back({"fable2_high_tick_rate",
                   s.patch_high_tick_rate ? "true" : "false",
                   "[Xenia/Guy] 15 Hz -> 30 Hz tick rate"});

    out.push_back({"audio_mute", s.mute ? "true" : "false", "mute all audio"});
    out.push_back({"audio_maxqframes", std::to_string(s.audio_queue_frames),
                   "queued audio frames; lower is less latency"});

    // Keyboard-as-pad. The driver is in the runtime and defaults to off, so a
    // PC port that never sets this only works with a real controller.
    out.push_back({"mnk_mode", s.keyboard_control ? "true" : "false",
                   "drive the guest pad from the keyboard"});
    out.push_back({"mnk_mouse", s.mouse_look ? "true" : "false",
                   "mouse drives the right stick"});
    out.push_back({"mnk_sensitivity", ToString(s.mouse_sensitivity),
                   "mouse sensitivity, 0.01-10"});

    // The d-pad on plain arrow keys. The runtime binds it to Shift+Arrow,
    // which is awkward to press and, measured on the NG2 port, does not reach
    // the game at all - a bare Shift+Down does not move a menu cursor while
    // the unmodified left-stick keys do. The arrow keys are free: the left
    // stick is on WASD.
    out.push_back({"keybind_dpad_up", "Up", "plain arrows, not Shift+Arrow"});
    out.push_back({"keybind_dpad_down", "Down", "plain arrows"});
    out.push_back({"keybind_dpad_left", "Left", "plain arrows"});
    out.push_back({"keybind_dpad_right", "Right", "plain arrows"});

    if (!mappings_file.empty()) {
      // The runtime resolves this against the working directory, so a shortcut
      // that starts the game from anywhere else silently loses every
      // controller mapping. Give it an absolute path.
      out.push_back({"hid_mappings_file", mappings_file,
                     "absolute path - the default is resolved against the CWD"});
    }
    return out;
  }

  // Writes the entries and loads them into the cvar registry. Returns false
  // only if the file could not be written; a parse failure comes back through
  // the SDK's own log line.
  static bool Apply(const std::filesystem::path& path,
                    const std::vector<Entry>& entries) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
      REXLOG_WARN("Tuning: cannot write {}", path.string());
      return false;
    }
    out << "# Fable II tuning - generated every launch, do not edit.\n"
        << "# Player-facing settings live in the settings menu (F10).\n\n";
    for (const auto& e : entries) {
      out << "# " << e.why << "\n" << e.name << " = " << Quote(e.value) << "\n\n";
    }
    out.close();

    rex::cvar::LoadConfig(path);
    return true;
  }

 private:
  // Fixed precision rather than shortest round-trip: these end up in a TOML a
  // human may read, and "0.200" is easier to recognise than
  // "0.20000000000000001".
  static std::string ToString(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
  }

  // Numbers and booleans go in bare; everything else is a TOML string.
  static std::string Quote(const std::string& v) {
    if (v == "true" || v == "false") return v;
    bool numeric = !v.empty();
    for (char c : v) {
      if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != '-')
        numeric = false;
    }
    if (numeric) return v;
    std::string escaped;
    for (char c : v) {
      if (c == '\\' || c == '"') escaped += '\\';
      escaped += c;
    }
    return "\"" + escaped + "\"";
  }
};
