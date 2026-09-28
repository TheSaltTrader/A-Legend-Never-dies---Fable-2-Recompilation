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

#include "fable2_keyremap.h"
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

        // [measured] The per-frame page-state refresh re-uploaded every page the CPU had uploaded, 460-700 MB/s -
        // the released build turned it off (CHANGELOG: market 51-56 -> 57-59 fps, impostor flashes 35-48 -> 5,
        // uploads five-fold down). The engine fork this branch builds against defaults it to TRUE (rexglue-src
        // c94f5eb), so the native branch had silently run with it on. Native backend, town, interleaved A/B
        // 2026-09-26 (CM01/CM11/CM02/CM12): plugin GPU-thread CPU 13.2-14.5 vs 15.7-16.3 ms per frame, uploads
        // 1.3 vs 6.9-7.1 GB per 5 s. Ninja Gaiden II needs it ON; this title does not.
        {"clear_memory_page_state", "false",
         "[measured] per-frame page-state refresh re-uploads ~700 MB/s; off in the released build"},

        // RETRACTED. This once set render_target_path_d3d12 = "rov", on a
        // measurement that turned out to be worthless: the run it came from
        // never reached the state that fails. Xenia Canary plays this title
        // through that point on RTV, so ROV was never the answer, and the
        // flat-blue scene reproduces on ROV, RTV, the default, AND on Vulkan.
        // Left at the runtime's default deliberately.
    };
  }

  // The native renderer (1.1.0): EXACTLY the set every test leg of 2026-09-26/27
  // and the user's own test ran with, read from "Play Fable II (native).cmd"'s
  // FABLE2_TUNE line - not trimmed, not reordered - so the release is the tested
  // configuration and differs from it only in where the values come from.
  // Applied when renderer=native (the default); renderer=plugin leaves the
  // Xenos plugin drawing, as in 1.0.x. FABLE2_TUNE still overrides any of them.
  static std::vector<Entry> Native() {
    const char* why = "native renderer (1.1.0 tested set)";
    return {
        {"ngpu_census", "false", why},
        {"ngpu_census_report_secs", "10", why},
        {"ngpu_shadow", "true", why},
        {"ngpu_native_draws", "true", why},
        {"ngpu_dump_textures", "0", why},
        {"ngpu_use_sdk_untile", "true", why},
        {"ngpu_bridge", "true", why},
        {"ngpu_bridge_log", "true", why},
        {"ngpu_bridge_draws", "true", why},
        {"ngpu_hooked_draws", "false", why},
        {"ngpu_sdk_path", "true", why},
        {"ngpu_sdk_pairs", "*:*", why},
        {"ngpu_sdk_world_only", "false", why},
        {"ngpu_bridge_accumulate", "true", why},
        {"ngpu_present_post", "true", why},
        {"ngpu_post_raw", "true", why},
        {"ngpu_backend", "true", why},
        {"gpu_offload_to_native", "true", why},
        {"ngpu_guest_scene_probe", "0", why},
        {"ngpu_dump_rts", "0", why},
        {"ngpu_truth_keep", "false", why},
        {"ngpu_guest_scene_dump_frame", "0", why},
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
    // [internal resolution] the game's render size before the multiple: 0/0 = the game's own (1280x720 with the
    // 720p patch); 960x540 when the player picked a 540-based size (1920x1080, 2880x1620).
    const bool sub720 = s.world_height == 540 || s.world_height == 544;
    out.push_back({"fable2_world_width", sub720 ? "960" : "0", "game render width (0 = its own)"});
    out.push_back({"fable2_world_height", sub720 ? std::to_string(s.world_height) : "0", "game render height (0 = its own)"});

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

    // Exact float24 depth: ALWAYS off. With it on the plugin failed to create
    // 52 graphics pipelines in one session (six vertex shaders) - black bars
    // on the loading screen, a shadow smear following the hero (2026-09-13).
    // The setting is gone from the menu; a value left in an old settings
    // file is ignored here rather than honoured.
    (void)s.accurate_depth;
    out.push_back({"depth_float24_convert_in_pixel_shader", "false",
                   "exact float24 depth breaks this title's pipelines"});
    out.push_back({"depth_float24_round", "false",
                   "the other half of exact float24 depth, off with it"});
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
    // Ultrawide stretches the frame to the window (the projection hook makes
    // the picture right for it and letterboxes menus itself, live).
    out.push_back({"present_letterbox", (s.letterbox && !s.ultrawide) ? "true" : "false",
                   "keep the guest aspect ratio instead of stretching"});

    // -1 = the menu's "Default (4x)": nothing is sent, so the plugin's own
    // default (anisotropic_override = 3, forced 4x) applies - not the game's own
    // samplers, as this comment used to say (F10 audit 2026-09-27). Values
    // 0..5 are the cvar's own: 0 off, 1 1x, 2 2x, 3 4x, 4 8x, 5 16x.
    if (s.anisotropic >= 0) {
      out.push_back({"anisotropic_override", std::to_string(s.anisotropic),
                     "forced anisotropic filtering level"});
    }

    // The black-texture fix. "some" is the femtofork's selective readback;
    // "full" is the blunt instrument that costs a lot of performance.
    out.push_back({"readback_resolve", s.readback,
                   "readback for the hero/dog black-texture bug"});
    if (s.readback_drain_small_kb > 0) {
      // The flash experiment (settings file only): a GPU drain after small
      // resolves with "some", where "full" drains after every one.
      out.push_back({"readback_resolve_drain_small_kb", std::to_string(s.readback_drain_small_kb),
                     "flash experiment: GPU drain after small resolves"});
    }

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
    // 60 fps and 1280-wide are the proven, beneficial pair: FORCED ON here,
    // ignoring the settings field, so there is no way to turn them off (the
    // field stays only so an old settings file parses). This is the "no option
    // to remove the beneficial patches" the release wants; the black-texture
    // fix (readback, above) is on by default the same way.
    out.push_back({"fable2_60fps", "true", "[Xenia/Margen67] 60 fps (always on)"});
    out.push_back({"fable2_720p", "true",
                   "[Xenia/Margen67] render 1280 wide instead of 1120 (always on)"});
    // MSAA off, ALWAYS (user, 2026-09-27: "Picture looks 100% the same, I say we remove MSAA by default"). The game's
    // 2x MSAA frame does not fit the 10 MB EDRAM, so the console draws the scene in 3 predicated-tiling passes and the
    // renderer issues every scene draw 3 times; without it, 2 passes. Fairfax stand: 41 -> 60 fps (vsync cap), p99
    // 25.6 -> 18.9 ms, 14.4k -> 10.2k draws. At 2x internal scale the supersampling already smooths edges, and FXAA
    // stays available. The field stays only so an old settings file parses.
    out.push_back({"fable2_disable_msaa", "true", "[Xenia/Margen67] disable MSAA (always on)"});
    out.push_back({"fable2_disable_texture_morph",
                   s.patch_disable_texture_morph ? "true" : "false",
                   "[Xenia/Guy] disable texture morphing"});
    // Ours: the boot-logo list hook (patch_hooks.cpp), with its own switch
    // (the player asked to be able to keep the logos, 2026-09-12).
    out.push_back({"fable2_skip_boot_logos", s.skip_logos ? "true" : "false",
                   "start without the Microsoft and Lionhead logo videos"});
    // Ours: the projection-builder hook (patch_hooks.cpp); the menu slider
    // sets the same cvar live.
    out.push_back({"fable2_fov", std::to_string(s.fov),
                   "vertical field of view in degrees (60 = as shipped)"});
    out.push_back({"fable2_ultrawide", s.ultrawide ? "true" : "false",
                   "project the world at the window's aspect, shown edge to edge"});
    // Ours: the audio loader's 400 ms sleep per sound bank (patch_hooks.cpp).
    // Always on; FABLE2_TUNE=fable2_fast_bank_load=false is the A/B.
    out.push_back({"fable2_fast_bank_load", "true",
                   "shorten the 400 ms sleep after each sound bank load"});
    // Ours: the render thread's GPU progress poll yields (patch_hooks.cpp).
    // OFF: measured neutral (Bowerstone Market, profiled, 2026-09-12 - 57-59
    // fps either way, and SwitchToThread returns at once, so the thread
    // stayed on the CPU; the burn moved from no-ops into a syscall). Kept as
    // the documented negative; FABLE2_TUNE=fable2_gpu_wait_yield=true tries it.
    out.push_back({"fable2_gpu_wait_yield", "false",
                   "yield instead of spinning while waiting for the GPU"});
    // Plugin: pack uploads past a per-frame byte budget wait for later
    // frames. OFF: measured a loss (same build, save load into Bowerstone
    // Market, first 5 s in the world: 24 MB -> 52.6 fps, p99 70 ms, 15
    // hitches; off -> 54.5 fps, p99 45 ms, 8 hitches; 2026-09-13). The pack
    // copies were never the hitch. FABLE2_TUNE=texture_pack_upload_budget_mb=24
    // tries it again.
    out.push_back({"texture_pack_upload_budget_mb", "0",
                   "pack upload bytes per frame before the rest wait (0 = no limit)"});
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
    // Every action the keyboard driver binds: the player's binding from the
    // Keyboard bindings screen, or the port's default (fable2_keyremap.cpp;
    // the D-pad on plain arrows, as this port always had it).
    {
      size_t count = 0;
      const fable2::KeyAction* actions = fable2::KeyActions(&count);
      for (size_t i = 0; i < count; ++i)
        out.push_back({actions[i].cvar, fable2::KeyBinding(s, actions[i]), actions[i].label});
    }

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
