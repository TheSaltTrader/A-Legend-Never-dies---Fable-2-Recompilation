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

class SetupScreen final : public rex::ui::ImGuiDialog {
 public:
  // on_done(true) = Play was pressed and `settings` holds the choices;
  // on_done(false) = the user quit. It is invoked from inside OnDraw, so the
  // app must defer any destruction of this object to the next UI tick.
  SetupScreen(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
              std::function<void(bool)> on_done,
              std::function<void()> on_advanced);
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
  bool finished_ = false;

  // Cached inspection of the configured game folder, refreshed only when the
  // path changes - it walks the folder, and this runs every frame.
  std::string game_path_inspected_;
  DiscInfo game_info_;

  // Installer state.
  std::filesystem::path iso_path_;
  DiscInfo iso_info_;
  std::filesystem::path install_dest_;
  ExtractProgress progress_;
  std::thread install_thread_;
  bool install_started_ = false;
};

class SettingsOverlay final : public rex::ui::ImGuiDialog {
 public:
  SettingsOverlay(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
                  rex::ui::Window* window, std::function<void()> on_advanced);
  ~SettingsOverlay() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  Fable2Settings* settings_;
  rex::ui::Window* window_;
  std::function<void()> on_advanced_;
  std::string status_;
};

}  // namespace fable2
