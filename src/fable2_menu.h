// The settings menu, in its two surfaces.
//
// SetupScreen runs *before the guest boots*, from ReXApp::OnFinalizePaths -
// the one hook where the window and the ImGui drawer are already live but the
// runtime has not been constructed yet. That is what makes choosing the game
// data possible at all: the path it returns is the one the runtime mounts.
//
// SettingsOverlay is the same settings over a running game, on F10. Anything
// the presenter or the GPU plugin re-reads per frame is applied immediately;
// anything latched during startup is shown disabled with the reason, rather
// than accepted and silently ignored.

#pragma once

#include "fable2_textool.h"

#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

#include "fable2_disc.h"
#include "fable2_settings.h"

namespace rex::ui {
class Window;
}

namespace fable2 {

// The menu's own fonts, loaded from ReXApp::OnConfigureFonts. Both may be null
// - the SDK's built-in 13px bitmap font is the fallback - so every use goes
// through a push that tolerates it, and a missing font file costs looks
// rather than function.
struct MenuFonts {
  ImFont* body = nullptr;
  ImFont* title = nullptr;
  float body_size = 18.0f;
  float title_size = 30.0f;
};
MenuFonts& Fonts();

// Loads them into the atlas the SDK is about to build. Has to happen in
// OnConfigureFonts: the atlas is finalised straight afterwards.
void LoadMenuFonts(ImFontAtlas* atlas);

// The menu's look. Applied globally, so the SDK's own overlays inherit it too
// - which is the point: one application, one theme.
void ApplyMenuStyle(ImGuiStyle& style);

// Pushes the hot-reloadable settings into their cvars, and the window state
// onto the window. Safe to call before the GPU plugin exists: a cvar that is
// not registered yet is skipped and logged, not guessed at.
void ApplyLiveSettings(const Fable2Settings& settings, rex::ui::Window* window);

// A texture dump/upscale run. Its lifetime is the application's, not the
// settings menu's: the worker owns a child process tree and writes into the
// pack folder, so closing the menu must neither cancel the run nor orphan a
// python.exe. The App owns one of these; the setup screen and the overlay only
// borrow a pointer - they start, show and cancel the run, but never end it
// merely by being closed. Only an explicit Cancel or the app exiting stops it.
struct TextureJob {
  ExtractProgress progress;
  std::thread thread;
  TextureTools tools;
  bool tools_probed = false;

  // Presentation state for the run, kept with it so the bar and its clock
  // survive the menu being closed and reopened mid-run.
  double started_at = 0.0;   // for the time estimate, restarted per step
  int phase_seen = 0;        // the step the clock was last restarted for
  // Process everything again rather than only what is missing. Not a saved
  // setting: it is a decision about one run, taken after changing the scale,
  // the upscaler or its strength, and must not quietly apply to the next.
  bool redo_all = false;

  // Ask the run to stop and wait for it. The runner polls progress.cancel and
  // kills the whole child process tree within a moment, so this returns
  // promptly. Safe when nothing is running, and safe to call more than once.
  void CancelAndJoin() {
    if (thread.joinable()) {
      progress.cancel = true;
      thread.join();
    }
  }
  ~TextureJob() { CancelAndJoin(); }
};

class SetupScreen final : public rex::ui::ImGuiDialog {
 public:
  // on_done(true) = Play was pressed and `settings` holds the choices;
  // on_done(false) = the user quit. It is invoked from inside OnDraw, so the
  // app must defer any destruction of this object to the next UI tick.
  SetupScreen(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
              std::function<void(bool)> on_done,
              std::function<void()> on_advanced, TextureJob* tex_job);
  ~SetupScreen() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void DrawContent();
  void DrawInstaller();
  void DrawAbout();
  void DrawFooter(float column_width);

  void RefreshGame();
  void StartInstall();

  Fable2Settings* settings_;
  std::function<void(bool)> on_done_;
  std::function<void()> on_advanced_;
  TextureJob* tex_job_;
  bool finished_ = false;

  // Cached inspection of the configured game folder, refreshed only when the
  // path changes - it walks the folder, and this runs every frame.
  std::string game_path_inspected_;
  DiscInfo game_info_;

  // Installer state.
  std::filesystem::path iso_path_;
  DiscInfo iso_info_;
  std::filesystem::path install_dest_;
  std::string setup_save_message_;  // result of the save-folder pick, shown until cleared
  ExtractProgress progress_;
  std::thread install_thread_;
  bool install_started_ = false;
};

class SettingsOverlay final : public rex::ui::ImGuiDialog {
 public:
  SettingsOverlay(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
                  rex::ui::Window* window, std::function<void()> on_advanced,
                  std::function<void()> on_remap, TextureJob* tex_job);
  ~SettingsOverlay() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  Fable2Settings* settings_;
  rex::ui::Window* window_;
  std::function<void()> on_advanced_;
  std::function<void()> on_remap_;     // opens the Keyboard bindings screen
  // Borrowed from the App - see TextureJob. Never null while the overlay
  // exists.
  TextureJob* tex_job_;
  std::string status_;
};

}  // namespace fable2
