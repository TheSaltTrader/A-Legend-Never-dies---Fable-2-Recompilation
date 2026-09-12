// fable2 - ReXGlue Recompiled Project
//
// Fable II (Lionhead / Microsoft, 2008), GOTY disc build 0.0.0.26,
// title 4D5307F1, media 716F0A0D.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/overlay/settings_overlay.h>
#include <rex/ui/window.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>

#include "fable2_disc.h"
#include "fable2_fptrap.h"
#include "fable2_dlc.h"
#include <rex/input/input_system.h>
#include <rex/input/device_assignment.h>
#include "fable2_saveimport.h"
#include "fable2_autoskip.h"
#include "fable2_hwdetect.h"
#include "fable2_perf.h"
#include "fable2_texnotify.h"
#include "fable2_diagnostics.h"
#include "fable2_stage.h"
#include "fable2_crashdump.h"
#include "fable2_profiler.h"
#include "fable2_menu.h"
#include "fable2_platform.h"
#include "fable2_settings.h"
#include "fable2_tuning.h"

// Where to look for Xbox 360 content packages. Empty (the default) means do
// nothing at all: the two Fable II expansions are already on the GOTY disc, so
// installing them is a gigabyte of wasted work. There is deliberately no DLC
// page in the settings menu for the same reason - see tools/install_dlc.cmd.
REXCVAR_DEFINE_STRING(dlc_root, "", "Content",
                      "Folder of Xbox 360 content packages to install (DLC)");

// Which graphics backend the Xenos plugin should construct.
//
// The plugin the SDK ships for Windows is D3D12 ONLY - rexgpu-xenos.dll has
// zero references to Vulkan - because REXGLUE_USE_VULKAN defaults OFF on
// Windows. A plugin rebuilt from source with -DREXGLUE_USE_VULKAN=ON has both,
// and the plugin's own selector tries D3D12 first for "any", so Vulkan has to
// be asked for by name.
//
// This exists because Fable II's scene renders flat blue through the D3D12
// render-target path while Xenia Canary - which our plugin is an older fork of
// - plays the same disc fine. Vulkan is the cheap test of whether that path is
// the fault.
REXCVAR_DEFINE_STRING(gpu_backend, "any", "GPU",
                      "Graphics backend for the Xenos plugin: any, d3d12, vulkan");

class Fable2App : public rex::ReXApp {
 public:
  // Fable II, from the XEX's own execution-info header.
  static constexpr uint32_t kTitleId = 0x4D5307F1;

  Fable2Settings settings_;

  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fable2App>(new Fable2App(ctx, "fable2",
        PPCImageConfig));
  }

  // Read the settings file and apply everything the WINDOW is built from.
  //
  // This has to be OnConfigurePaths, not OnPreSetup. SetupPresentation creates
  // the window from window_width/height/fullscreen, and it runs BEFORE
  // OnPreSetup. Setting them later looks like it works, because the window
  // ends up the right size anyway once the guest sets its video mode -
  // fullscreen has no such second chance, and on ng2recomp came back windowed
  // every launch until this was moved here.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    // First thing, before anything that could fault: a crash from here on
    // leaves a minidump under crashdumps\ instead of a log that just stops.
    fable2::InstallCrashDumps();
    fable2::SetAppUserModelId();  // before any window: the taskbar groups by it
    settings_.Load();
    // Choose the memory-hungry settings from the card, ONCE. The latch is
    // what makes it safe: a value the player edits afterwards is never
    // overwritten on a later launch.
    if (!settings_.hardware_detected) {
      const auto gpu = fable2::DetectGpu();
      if (gpu.valid) {
        const auto rec = fable2::RecommendSettings(gpu);
        settings_.texture_cache_mb = rec.texture_cache_mb;
        settings_.texture_scale = rec.texture_scale;
        settings_.hardware_detected = true;
        settings_.Save();
        REXLOG_INFO("Hardware: {}", rec.summary);
        if (!rec.caveat.empty()) {
          REXLOG_WARN("Hardware: {}", rec.caveat);
        }
      }
    }
    ApplyEnvironmentOverrides();
    settings_.Clamp();

    REXCVAR_SET(window_width, settings_.window_width);
    REXCVAR_SET(window_height, settings_.window_height);
    REXCVAR_SET(fullscreen, settings_.fullscreen);
    REXCVAR_SET(monitor, settings_.monitor);

    if (paths.game_data_root.empty())
      paths.game_data_root = settings_.ResolvedGamePath();
  }

  // The setup screen, on the one hook where the window and the ImGui drawer
  // are live but the runtime has not been built yet - so the game path it
  // returns is the one that actually gets mounted.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    const bool path_from_cli = !REXCVAR_GET(game_data_root).empty();
    const bool game_ok =
        fable2::InspectFolder(settings_.ResolvedGamePath()).Usable();

    // Three reasons to stop: never configured, the player asked for it with
    // Shift, or the configured folder has gone (a moved or unplugged drive
    // should offer the picker, not a fatal error).
    const bool show = !path_from_cli &&
                      (!settings_.configured || fable2::ShiftHeld() || !game_ok);
    if (!show) {
      rex::PathConfig paths = defaults;
      if (!path_from_cli)
        paths.game_data_root = settings_.ResolvedGamePath();
      return paths;
    }

    REXLOG_INFO("Setup screen: {}",
                !settings_.configured ? "first run"
                : fable2::ShiftHeld() ? "Shift held at launch"
                                      : "configured game folder is missing");
    StartUiPump();
    setup_screen_ = std::make_unique<fable2::SetupScreen>(
        imgui_drawer(), &settings_,
        [this, defaults, resume](bool play) {
          // Invoked from inside the dialog's own OnDraw: destroying it here
          // would delete the object being drawn, so hand the teardown to the
          // next UI tick.
          app_context().CallInUIThreadDeferred([this, defaults, resume, play] {
            StopUiPump();
            setup_screen_.reset();
            if (!play) {
              app_context().QuitFromUIThread();
              return;
            }
            // The window already exists, so a fullscreen choice made on this
            // screen cannot go through the cvar the window was built from.
            // Push it at the live objects instead.
            fable2::ApplyLiveSettings(settings_, window());
            rex::PathConfig paths = defaults;
            paths.game_data_root = settings_.ResolvedGamePath();
            resume(paths);
          });
        },
        [this] { ToggleAdvancedSettings(); }, &tex_job_);
    return std::nullopt;
  }

  // Select the Xenos GPU emulation plugin, and push the tuning.
  //
  // gpu_plugin is a RuntimeConfig FIELD, not a cvar - there is no --gpu_plugin
  // to set instead. Without it the runtime comes up in "native rendering
  // mode", silently ignores every Vd* kernel call, and the guest never gets a
  // ring buffer: no error, just a black window.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Before the runtime is built, so before any guest thread seeds MXCSR.
    // FABLE2_TRAP_FP=1 makes invalid floating-point operations in recompiled
    // code fault and be reported, instead of quietly producing the NaN that
    // ends up in the vertex constants.
    fable2::FPTrap::InstallIfRequested();
    // The settings file is the player's choice; the cvar is the override, so
    // a command line still wins for scripted runs.
    std::string backend = REXCVAR_GET(gpu_backend);
    if (backend == "any" && !settings_.gpu_backend.empty())
      backend = settings_.gpu_backend;
    if (backend != "any") {
      // Load the plugin ourselves so the backend can be named. Setting
      // config.graphics directly bypasses ReXApp's own load, which always
      // passes "any".
      config.graphics = rex::system::LoadGpuPlugin("xenos", backend);
      if (config.graphics) {
        REXLOG_INFO("GPU: requested backend '{}'", backend);
      } else {
        REXLOG_ERROR("GPU: backend '{}' unavailable - is this plugin built "
                     "with it? Falling back to the default.", backend);
        config.gpu_plugin = "xenos";
      }
    } else {
      config.gpu_plugin = "xenos";
    }
    ApplyDisplaySettings();
    ApplyTuning();
  }

  void OnPostLoadXexImage() override {
    REXLOG_INFO("fable2: XEX image loaded");
  }

  // FABLE2_DUMP_CVARS=<path> writes every registered cvar - name, category,
  // current value, default, and any declared allowed values or range.
  //
  // This is the only honest way to know what a build can actually do. The GPU
  // plugin does not export accessors for its flags, so they cannot be listed
  // from outside, and guessing from the SDK headers is exactly how ng2recomp's
  // settings menu ended up offering FSR and CAS that its presenter does not
  // implement. Design the menu from this file, not from a header.
  //
  // It has to run in OnPostSetup: rexgpu-xenos.dll registers its cvars when it
  // loads, which is after OnPreSetup.
  static void DumpCvars(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
      REXLOG_WARN("Cvar dump: cannot write {}", path.string());
      return;
    }
    auto names = rex::cvar::ListFlags();
    std::sort(names.begin(), names.end());
    out << "# " << names.size() << " registered cvars\n";
    for (const auto& name : names) {
      const auto* info = rex::cvar::GetFlagInfo(name);
      if (!info) continue;
      out << name << "\n  category  " << info->category
          << "\n  value     " << rex::cvar::GetFlagByName(name)
          << "\n  default   " << info->default_value << "\n";
      if (!info->constraints.allowed_values.empty()) {
        out << "  allowed  ";
        for (const auto& v : info->constraints.allowed_values) out << " " << v;
        out << "\n";
      }
      if (info->constraints.HasRangeConstraint()) {
        out << "  range     "
            << (info->constraints.min ? std::to_string(*info->constraints.min) : "-")
            << " .. "
            << (info->constraints.max ? std::to_string(*info->constraints.max) : "-")
            << "\n";
      }
    }
    REXLOG_INFO("Cvar dump: {} cvars -> {}", names.size(), path.string());
  }

  // Import whatever the setup screen queued. One bad file does not stop the
  // rest: a folder of saves is exactly where a stray or corrupt one turns up.
  void ImportQueuedSaves() {
    auto packages = fable2::TakeQueuedSaveImports();
    if (!packages.empty() || !settings_.save_import_path.empty()) {
      REXLOG_INFO("Save: queue {} entries, folder '{}'", packages.size(),
                  settings_.save_import_path);
    }
    // A queue is only primed by the menu's Scan button. A path that was SAVED
    // has already been chosen deliberately, so it acts on its own - otherwise
    // the setting would appear to do nothing until the button is pressed again
    // every launch. Importing the same save twice overwrites in place, so this
    // is idempotent rather than accumulating.
    if (packages.empty() && !settings_.save_import_path.empty()) {
      packages = fable2::FindSavePackages(settings_.save_import_path);
      if (!packages.empty()) {
        REXLOG_INFO("Save: {} package(s) found under '{}'", packages.size(),
                    settings_.save_import_path);
      }
    }
    if (packages.empty()) {
      return;
    }
    size_t done = 0;
    for (const auto& package : packages) {
      std::string message;
      if (fable2::ImportSave(package, message)) {
        ++done;
        REXLOG_INFO("Save: imported {} - {}", package.filename().string(), message);
      } else {
        REXLOG_WARN("Save: could not import {} - {}", package.filename().string(),
                    message);
      }
    }
    REXLOG_INFO("Save: {} of {} imported", done, packages.size());
  }

  // Every controller drives guest user 0.
  //
  // The runtime's default is SlotAssignment: device ordinal N feeds guest user
  // N. Fable II is single-player and polls user 0 only, so a pad that does not
  // enumerate FIRST lands on user 1 where nobody is listening - measured here
  // with an arcade stick on ordinal 0 and the Xbox pad on ordinal 1, which did
  // nothing at all. SharedAssignment is the SDK's own answer for the case.
  //
  // It asks for the concrete input system rather than assuming it, so a
  // different backend keeps the default instead of crashing.
  void UseOneGuestUser() {
    auto* runtime_ptr = runtime();
    if (runtime_ptr == nullptr) {
      return;
    }
    auto* input = dynamic_cast<rex::input::InputSystem*>(runtime_ptr->input_system());
    if (input == nullptr) {
      REXLOG_WARN("Input: not the SDL input system; controllers stay on the "
                  "default per-slot assignment");
      return;
    }
    input->SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>());

    // Added AFTER the assignment so it is picked up with everything else. It
    // reports nothing at all unless armed.
    fable2::SetAutoSkipEnabled(settings_.skip_intro);
    input->AddDriver(fable2::MakeAutoSkipDriver());
    if (settings_.skip_intro) {
      // The boot logos play immediately, so the arm goes in here rather than
      // waiting for a signal this port cannot see.
      fable2::ArmAutoSkip();
    }
    REXLOG_INFO("Input: every controller drives guest user 0{}",
                settings_.skip_intro ? ", intro auto-skip armed" : "");
  }

  void OnPostSetup() override {
    if (const char* dump = std::getenv("FABLE2_DUMP_CVARS"); dump && *dump)
      DumpCvars(dump);

    // Read back what the plugin actually took, now that its cvars exist. This
    // is the only honest check that the deferred config reached it - the
    // config file saying 2 proves nothing.
    REXLOG_INFO("GPU: internal scale {}x{}, swap_post_effect '{}', vsync {}, "
                "render target path '{}', readback '{}', "
                "allow_invalid_fetch_constants {}",
                rex::cvar::GetFlagByName("draw_resolution_scale_x"),
                rex::cvar::GetFlagByName("draw_resolution_scale_y"),
                rex::cvar::GetFlagByName("swap_post_effect"),
                rex::cvar::GetFlagByName("vsync"),
                rex::cvar::GetFlagByName("render_target_path_d3d12"),
                rex::cvar::GetFlagByName("readback_resolve"),
                rex::cvar::GetFlagByName("gpu_allow_invalid_fetch_constants"));

    // Saves queued on the setup screen. They cannot be imported there: that
    // screen runs before the runtime is built, so there is no ContentManager
    // and no signed-in profile to import into. Here both exist and the guest
    // has not started looking for saves yet.
    UseOneGuestUser();
    GateInputToForeground();
    fable2::StartPerfMonitor();
    ImportQueuedSaves();
    MaybeWriteDiagnostics();
    ArmQuitSeam();
    ArmTexpackStressSeam();
    DumpGuestImage();
    fable2::RaiseTimerResolution();
    fable2::StartProfiler();  // FABLE2_PROFILE=1: sample the guest threads
    // Per-region texture warming needs to know which region is loading, and
    // the game says so through the audio bank it opens for it. The path the
    // runtime mounted is the one to enumerate - a command-line root wins over
    // the settings file.
    {
      const std::string mounted = REXCVAR_GET(game_data_root);
      fable2::InstallStageObserver(mounted.empty() ? settings_.ResolvedGamePath()
                                                   : std::filesystem::path(mounted));
    }

    // The window exists by now, so the comfort settings go straight on it.
    fable2::ApplyLiveSettings(settings_, window());

    const std::string root = REXCVAR_GET(dlc_root);
    if (!root.empty()) {
      fable2::InstallPackages(runtime()->kernel_state()->content_manager(),
                              std::filesystem::path(root), kTitleId);
    }
  }

  // F10 opens the same settings over the running game. F4 - the SDK's own raw
  // cvar browser - is left alone; this menu links to it rather than replacing
  // it, because the two answer different questions. F4 enumerates the registry
  // and so cannot fall behind the build; this one explains what matters.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    // The window title is the SDK's by default - "fable2 [rexglue-v0.10.0]" -
    // which names the SDK's version and never changes. Put the port's own
    // version where it is visible without opening anything.
    if (auto* w = window()) {
      w->SetTitle(std::string("Fable II  -  v") + FABLE2_VERSION);
      fable2::ApplyWindowIcon(w->GetNativeWindowHandle());
    }

    // Overlays that live for the whole run and draw nothing until asked: the
    // F9 indicator, the texture-cache warming bar, and the F8 readouts (with
    // the sampler behind them, which the menu's own cost bars also need).
    tex_notify_ = std::make_unique<fable2::TextureNotifyOverlay>(drawer);
    warm_overlay_ = std::make_unique<fable2::WarmOverlay>(drawer);
    fable2::SetHudSettings(&settings_);
    perf_hud_ = std::make_unique<fable2::PerfHudOverlay>(drawer);

    // Escape quits. Settings are saved on the way out, so a change made in
    // the overlay and then quit is not lost - which is the whole reason this
    // goes through the same path as the window's close button rather than
    // ending the process where it stands.
    rex::ui::RegisterBind("bind_fable2_quit", "Escape", "Quit the game",
                          [this] { QuitFromEscape(); });

    // F8 shows or hides the readouts. A number in the corner is a tool, so it
    // is off until asked for - but reaching it must not need a menu, because
    // what it measures is what the menu being open changes. Which of the
    // three are shown stays as chosen; this only switches them on and off.
    rex::ui::RegisterBind(
        "bind_fable2_hud", "F8", "Show or hide the on-screen readouts", [this] {
          settings_.hud_enabled = !settings_.hud_enabled;
          REXLOG_INFO("F8: on-screen readouts {}",
                      settings_.hud_enabled ? "on" : "off");
          settings_.Save();
        });

    // F9 switches the texture pack during play. This is the only practical way
    // to judge a pack: through a settings screen the eye loses the detail it
    // was comparing before the overlay closes.
    rex::ui::RegisterBind(
        "bind_fable2_texpack", "F9", "Toggle the upscaled texture pack", [this] {
          if (settings_.texture_path.empty()) {
            REXLOG_WARN("F9: no texture folder is set - nothing to switch to");
            return;
          }
          // One switch at a time. Each press makes the plugin drop and reload
          // every texture, which takes a second or two of render-thread work;
          // a second press inside that window rewrote the pack-path setting
          // while the render thread was reading it for every reload, and the
          // game died (2026-09-11, two presses 1.03 s apart, nothing logged
          // after). The setting is a plain string the plugin reads by
          // reference, so the rule has to be here: no second switch until the
          // first has settled, and none while a region's cache is warming.
          static double last_switch = -1000.0;
          const double now = ImGui::GetTime();
          if (now - last_switch < 3.0) {
            REXLOG_INFO("F9: ignored - the last switch is still reloading textures");
            return;
          }
          if (fable2::GetWarmState().warming) {
            REXLOG_INFO("F9: ignored - wait for the texture cache to finish loading");
            return;
          }
          last_switch = now;
          settings_.texture_pack = !settings_.texture_pack;
          REXLOG_INFO("F9: texture pack {}", settings_.texture_pack ? "ON" : "OFF");
          fable2::ApplyLiveSettings(settings_, window());
          fable2::NotifyTexturePack(settings_.texture_pack);
          // Deliberately NOT saved: a half-finished comparison must not become
          // the stored preference. The checkbox is the decision.
        });

    rex::ui::RegisterBind(
        "bind_fable2_settings", "F10", "Toggle the Fable II settings menu",
        [this, drawer] {
          if (overlay_) {
            overlay_.reset();
            return;
          }
          overlay_ = std::make_unique<fable2::SettingsOverlay>(
              drawer, &settings_, window(), [this] { ToggleAdvancedSettings(); },
              &tex_job_);
        });
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    fable2::LoadMenuFonts(atlas);
  }

  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override {
    fable2::ApplyMenuStyle(imgui_style);
    (void)ui_style;
  }

  void OnPreLaunchModule() override {
    REXLOG_INFO("fable2: launching guest module");
  }

  void OnGuestThreadExit(rex::system::XThread* thread) override {
    (void)thread;
    REXLOG_INFO("fable2: main guest thread exited");
  }

  void OnShutdown() override {
    fable2::StopProfiler();  // before any guest thread it samples can go away
    fable2::FPTrap::Report();
    // A texture run still going is stopped here, not left to the destructor:
    // it owns a process tree writing into the pack, and the job object kills
    // that tree the moment the run is cancelled.
    tex_job_.CancelAndJoin();
    // The dialogs hold a raw pointer to the drawer, which the SDK tears down
    // after this hook. Drop them first.
    StopUiPump();
    setup_screen_.reset();
    overlay_.reset();
    advanced_.reset();
    tex_notify_.reset();
    warm_overlay_.reset();
    perf_hud_.reset();
    rex::ui::UnregisterBind("bind_fable2_settings");
  }

 private:
  // The SDK's cvar browser: the honest "everything else" surface, because it
  // enumerates the registry rather than a hand-written list.
  void ToggleAdvancedSettings() {
    if (advanced_) {
      advanced_.reset();
      return;
    }
    advanced_ = std::make_unique<rex::ui::SettingsDialog>(
        imgui_drawer(), rex::filesystem::GetExecutableFolder() / "fable2.toml");
  }

  // What the title is told the display is: a 16:9 display, whatever shape
  // the window is.
  //
  // Two things went wrong before this. The guest mode was a separate setting
  // that could be pointed at the window's own shape; and the runtime treats a
  // video mode equal to its default of 1280x720 as "not configured" and
  // substitutes the window size for it. Either way, on a 3840x1600 window the
  // guest reported a 2.4:1 display, the 16:9 frame "matched" it, and the
  // presenter - which pillarboxes only when the guest's display aspect
  // differs from the window's - never added the bars. "Keep aspect ratio"
  // was on and doing nothing. Measured here at a 2400x1000 window: the
  // character-select cards stretched edge to edge. Fixed on the NG2 port first
  // (its v1.0.1); this is the same fix.
  //
  // The largest 16:9 box that fits the window: a 16:9 window is told its own
  // size, an ultrawide is told a display of its own height.
  void ApplyDisplaySettings() {
    int guest_w = settings_.window_width;
    int guest_h = settings_.window_height;
    if (guest_w * 9 > guest_h * 16)
      guest_w = guest_h * 16 / 9;
    else if (guest_w * 9 < guest_h * 16)
      guest_h = guest_w * 9 / 16;
    guest_w &= ~1;
    guest_h &= ~1;
    REXCVAR_SET(video_mode_width, guest_w);
    REXCVAR_SET(video_mode_height, guest_h);
    // Ask the runtime to take the value as set even when it equals the
    // default. By name, so a runtime without the option simply ignores it.
    if (rex::cvar::GetFlagInfo("video_mode_explicit"))
      rex::cvar::SetFlagByName("video_mode_explicit", "true");
    REXCVAR_SET(video_mode_refresh_rate, double(settings_.fps));
    REXLOG_INFO("Display: window {}x{}, guest display {}x{} @ {} Hz, "
                "fullscreen={}, scale={}x",
                settings_.window_width, settings_.window_height,
                REXCVAR_GET(video_mode_width), REXCVAR_GET(video_mode_height),
                settings_.fps, settings_.fullscreen, settings_.resolution_scale);
  }

  // Input is held while a stage's textures are still being read, and while
  // we are not the foreground window.
  //
  // Held at the INPUT layer rather than by drawing a modal, because the
  // screen underneath is the game's own loading screen and covering it would
  // replace something informative with something less so. Pressing on through
  // the warming would land the player in the region exactly when the disk is
  // busiest - which is the stutter the warming exists to remove.
  void GateInputToForeground() {
    auto* runtime_ptr = runtime();
    auto* input = runtime_ptr ? dynamic_cast<rex::input::InputSystem*>(
                                    runtime_ptr->input_system())
                              : nullptr;
    if (input == nullptr)
      return;
    input->SetActiveCallback([] {
      if (fable2::GetWarmState().warming)
        return false;
      return fable2::ThisProcessIsForeground();
    });
    REXLOG_INFO("Input: held while the texture cache warms, and while we are "
                "not the foreground window");
  }

  // Escape's quit: save, release what we own, then take the close button's
  // path.
  //
  // It used to ask the runtime to quit gracefully (QuitFromUIThread), and
  // measured on 2026-09-11 that never comes back on this title either:
  // "Escape: quitting" was the last line the log ever wrote, and the process
  // was still alive thirty seconds later with nothing left to draw. NG2 hit
  // the same thing - its notes blame the graceful path waiting on a guest
  // thread that never reaches the termination check. The close button never
  // showed it because the SDK's close path terminates the title and hard-exits
  // ("Title terminated; hard-exiting process.", 0.2 s in every log here). So
  // Escape now asks the window to close, which is exactly that path, with a
  // watchdog behind it in case the request itself is ever swallowed.
  //
  // Reachable from a test seam as well as the key: FABLE2_QUIT_AFTER=<seconds>
  // fires this from a timer, so whether the quit really exits is something a
  // script measures rather than something anyone has to believe.
  void QuitFromEscape() {
    settings_.Save();
    REXLOG_INFO("Escape: quitting");
    tex_job_.CancelAndJoin();
    fable2::StopPerfMonitor();
    StartExitWatchdog();
    if (auto* w = window()) {
      w->RequestClose();
    } else {
      app_context().QuitFromUIThread();
    }
  }

  // Make sure the process actually goes. Our own teardown is done by the time
  // this starts - the settings are written, the texture run is stopped - so
  // what remains is the runtime's, and it is not entitled to hang.
  void StartExitWatchdog() {
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::seconds(3));
      REXLOG_WARN("Shutdown: the runtime did not finish in 3 s; exiting now");
      std::fflush(nullptr);
      rex::FlushLogging();
      std::_Exit(0);
    }).detach();
  }

  void ArmQuitSeam() {
    const int after = EnvInt("FABLE2_QUIT_AFTER", 0);
    if (after <= 0)
      return;
    REXLOG_INFO("Quit seam: Escape's path fires in {} s", after);
    std::thread([this, after] {
      std::this_thread::sleep_for(std::chrono::seconds(after));
      app_context().CallInUIThreadDeferred([this] { QuitFromEscape(); });
    }).detach();
  }

  // FABLE2_DUMP_IMAGE=<file>: write the guest's loaded executable image
  // (0x82000000 up, every committed page, as the runtime has it AFTER any
  // sibling default.xexp title update was applied) to a flat file. The
  // analysis tools decode the .xex themselves and cannot apply a title
  // update; this hands them the patched bytes instead. Runs before the guest
  // starts, so the old recompiled code never executes against patched data.
  void DumpGuestImage() {
    const char* path = std::getenv("FABLE2_DUMP_IMAGE");
    if (!path || !*path)
      return;
    auto* memory = REX_KERNEL_MEMORY();
    if (!memory) {
      REXLOG_WARN("Image dump: no guest memory yet");
      return;
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) {
      REXLOG_WARN("Image dump: cannot create {}", path);
      return;
    }
    const uint32_t base = 0x82000000u, end = 0x84000000u, step = 0x10000u;
    uint64_t written = 0, last_used = 0;
    for (uint32_t va = base; va < end; va += step) {
      const uint8_t* host = memory->TranslateVirtual<const uint8_t*>(va);
      if (host && fable2::PageCommitted(host)) {
        std::fwrite(host, 1, step, f);
        last_used = va - base + step;
      } else {
        // Keep the file position honest: a gap of zeros.
        static const uint8_t zeros[0x10000] = {};
        std::fwrite(zeros, 1, step, f);
      }
      written += step;
    }
    std::fclose(f);
    REXLOG_INFO("Image dump: {} bytes written to {} (last committed byte at +{:#x})", written,
                path, last_used);
  }

  // FABLE2_TEXPACK_STRESS=<seconds>: from then on, switch the texture pack
  // on and off twenty times, 300 ms apart, from the UI thread - the same
  // work F9 does, WITHOUT F9's debounce. This is the reproduction of the
  // 2026-09-11 crash (two presses one second apart during a reload): with
  // the plugin reading the pack path by reference it dies within a few
  // switches; with the locked copy it must survive all twenty.
  void ArmTexpackStressSeam() {
    const int after = EnvInt("FABLE2_TEXPACK_STRESS", 0);
    if (after <= 0)
      return;
    REXLOG_INFO("Texpack stress seam: 20 switches start in {} s", after);
    std::thread([this, after] {
      std::this_thread::sleep_for(std::chrono::seconds(after));
      // With the settings menu OPEN, as the player had it: that is where the
      // 2026-09-11 crash lived (a table the menu begins was not visible for
      // one frame after a switch re-applied fullscreen, and a row was drawn
      // into it anyway).
      app_context().CallInUIThreadDeferred([this] {
        if (!overlay_) {
          overlay_ = std::make_unique<fable2::SettingsOverlay>(
              imgui_drawer(), &settings_, window(), [this] { ToggleAdvancedSettings(); },
              &tex_job_);
          REXLOG_INFO("Texpack stress: settings menu opened");
        }
      });
      std::this_thread::sleep_for(std::chrono::seconds(2));
      for (int i = 0; i < 20; ++i) {
        app_context().CallInUIThreadDeferred([this, i] {
          settings_.texture_pack = !settings_.texture_pack;
          REXLOG_INFO("Texpack stress: switch {} -> {}", i + 1,
                      settings_.texture_pack ? "ON" : "OFF");
          fable2::ApplyLiveSettings(settings_, window());
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
      }
      app_context().CallInUIThreadDeferred(
          [] { REXLOG_INFO("Texpack stress: all 20 switches survived"); });
    }).detach();
  }

  // The same bundle the settings button writes, from the environment.
  //
  // The button is in a menu, and the reports worth having most are from people
  // whose game never reaches one. This runs during startup instead, so a
  // launch that dies later still leaves something to send.
  void MaybeWriteDiagnostics() {
    const char* want = std::getenv("FABLE2_DIAGNOSTICS");
    if (!want || !*want || *want == '0')
      return;
    const auto result = fable2::WriteDiagnostics(
        Fable2Settings::Path(), rex::filesystem::GetExecutableFolder() / "logs");
    if (result.ok)
      REXLOG_INFO("Diagnostics: FABLE2_DIAGNOSTICS wrote {}", result.file.string());
    else
      REXLOG_ERROR("Diagnostics: {}", result.error);
  }

  // Everything the GPU plugin owns, through the cvar config loader so the
  // values survive until the plugin registers its cvars. See fable2_tuning.h
  // for why setting them directly cannot work.
  void ApplyTuning() {
    auto entries = Fable2Tuning::Fixed();
    const auto mappings =
        rex::filesystem::GetExecutableFolder() / "gamecontrollerdb.txt";
    std::error_code ec;
    const bool have_mappings = std::filesystem::exists(mappings, ec);
    for (auto& e : Fable2Tuning::FromSettings(
             settings_, have_mappings ? mappings.string() : std::string())) {
      entries.push_back(std::move(e));
    }
    Fable2Tuning::Apply(
        rex::filesystem::GetExecutableFolder() / "cache" / "fable2_tuning.toml",
        entries);
  }

  static int EnvInt(const char* name, int fallback) {
    const char* e = std::getenv(name);
    if (!e || !*e) return fallback;
    const int v = std::atoi(e);
    return v > 0 ? v : fallback;
  }

  // Environment overrides beat the saved file, for scripted testing. Read once
  // so every later consumer sees the same values.
  void ApplyEnvironmentOverrides() {
    settings_.window_width = EnvInt("FABLE2_WIDTH", settings_.window_width);
    settings_.window_height = EnvInt("FABLE2_HEIGHT", settings_.window_height);
    settings_.fps = EnvInt("FABLE2_FPS", settings_.fps);
    settings_.resolution_scale =
        EnvInt("FABLE2_SCALE", settings_.resolution_scale);
    if (std::getenv("FABLE2_FULLSCREEN"))
      settings_.fullscreen = EnvInt("FABLE2_FULLSCREEN", 0) != 0;
    if (const char* game = std::getenv("FABLE2_GAME"); game && *game)
      settings_.game_path = game;
    // A scripted run must never stop at the setup screen.
    if (std::getenv("FABLE2_NO_SETUP"))
      settings_.configured = true;
  }

  // Nothing paints the UI before the guest runs.
  //
  // The setup screen draws once and freezes without this - a perfect
  // screenshot, completely dead to the mouse, because ImGui only consumes
  // queued input inside a draw. The pump has to come from a SECOND THREAD: a
  // UI-thread tick that re-enqueues itself starves SDL's event loop and kills
  // the window's close button.
  void StartUiPump() {
    if (ui_pump_thread_.joinable()) return;
    ui_pump_stop_.store(false, std::memory_order_release);
    ui_pump_pending_.store(false, std::memory_order_release);
    ui_pump_thread_ = std::thread([this] {
      while (!ui_pump_stop_.load(std::memory_order_acquire)) {
        // One repaint in flight at a time. A modal file dialog blocks the UI
        // thread for as long as it is open, and without this the whole time
        // would come back as a queue full of stale repaint requests.
        if (!ui_pump_pending_.exchange(true, std::memory_order_acq_rel)) {
          app_context().CallInUIThreadDeferred([this] {
            if (auto* w = window()) w->RequestPresenterUIPaintFromUIThread();
            ui_pump_pending_.store(false, std::memory_order_release);
          });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      }
    });
  }

  void StopUiPump() {
    ui_pump_stop_.store(true, std::memory_order_release);
    if (ui_pump_thread_.joinable()) ui_pump_thread_.join();
  }

  std::unique_ptr<fable2::SetupScreen> setup_screen_;
  std::unique_ptr<fable2::SettingsOverlay> overlay_;
  std::unique_ptr<rex::ui::SettingsDialog> advanced_;
  std::unique_ptr<fable2::TextureNotifyOverlay> tex_notify_;
  std::unique_ptr<fable2::WarmOverlay> warm_overlay_;
  std::unique_ptr<fable2::PerfHudOverlay> perf_hud_;
  // The texture dump/upscale run, owned here for the life of the process so
  // closing a settings screen never cancels it. See TextureJob.
  fable2::TextureJob tex_job_;
  std::thread ui_pump_thread_;
  std::atomic<bool> ui_pump_stop_{false};
  std::atomic<bool> ui_pump_pending_{false};
};
