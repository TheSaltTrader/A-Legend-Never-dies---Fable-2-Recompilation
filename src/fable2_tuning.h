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
    // the whole of the antialiasing this runtime has. There is deliberately no
    // output-filter setting: present_effect declares one value, "bilinear".
    out.push_back({"swap_post_effect", s.antialias,
                   "post-process antialiasing: none, fxaa, fxaa_extreme"});

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
