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
        [this] { ToggleAdvancedSettings(); });
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
    fable2::StartPerfMonitor();
    ImportQueuedSaves();

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
    // F8 shows or hides the readouts. A number in the corner is a tool, so it
    // is off until asked for - but reaching it must not need a menu, because
    // what it measures is what the menu being open changes.
    rex::ui::RegisterBind(
        "bind_fable2_hud", "F8", "Show or hide the on-screen readouts", [this] {
          settings_.hud_enabled = !settings_.hud_enabled;
          REXLOG_INFO("F8: on-screen readouts {}",
                      settings_.hud_enabled ? "on" : "off");
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
          settings_.texture_pack = !settings_.texture_pack;
          REXLOG_INFO("F9: texture pack {}", settings_.texture_pack ? "ON" : "OFF");
          fable2::ApplyLiveSettings(settings_, window());
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
              drawer, &settings_, window(), [this] { ToggleAdvancedSettings(); });
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
    fable2::FPTrap::Report();
    // The dialogs hold a raw pointer to the drawer, which the SDK tears down
    // after this hook. Drop them first.
    StopUiPump();
    setup_screen_.reset();
    overlay_.reset();
    advanced_.reset();
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

  // What the title is told the display is. This is separate from the host
  // window: video_mode_* is what actually changes the rendered resolution and
  // the rate the game targets.
  void ApplyDisplaySettings() {
    REXCVAR_SET(video_mode_width, settings_.video_width);
    REXCVAR_SET(video_mode_height, settings_.video_height);
    REXCVAR_SET(video_mode_refresh_rate, double(settings_.fps));
    REXLOG_INFO("Display: window {}x{}, guest {}x{} @ {} Hz, fullscreen={}, "
                "scale={}x",
                settings_.window_width, settings_.window_height,
                settings_.video_width, settings_.video_height, settings_.fps,
                settings_.fullscreen, settings_.resolution_scale);
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
  std::thread ui_pump_thread_;
  std::atomic<bool> ui_pump_stop_{false};
  std::atomic<bool> ui_pump_pending_{false};
};
