// The keyboard bindings screen: every controller action the runtime's
// keyboard driver knows, the keys bound to it, and a way to change them by
// pressing the key rather than typing its name.
//
// The runtime already does the work: 26 string cvars (keybind_a,
// keybind_lstick_up, ...) hold comma-separated alternatives with optional
// Shift+ / Ctrl+ / Alt+ prefixes, and its driver reads them on every poll, so
// a change here is live the moment it is applied. What was missing was a
// screen: the bindings sat on the SDK's F4 page as raw strings, and a player
// who does not know that ';' is "Semicolon" could not rebind anything.
//
// Capture takes the key from the window's input events, above ImGui, so the
// exact VirtualKey the driver will compare against is what gets written -
// not a guess translated from a text field.
#pragma once

#include <functional>
#include <string>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "fable2_settings.h"

namespace fable2 {

struct KeyAction {
  const char* cvar;      // the runtime's cvar, e.g. "keybind_a"
  const char* label;     // what the screen calls it
  const char* fallback;  // the binding this port ships when the player set none
};

// The actions, in the order the screen lists them. `count` receives how many.
const KeyAction* KeyActions(size_t* count);

// The binding in force for an action: the player's, or the port's default.
std::string KeyBinding(const Fable2Settings& s, const KeyAction& a);

class KeyRemapScreen final : public rex::ui::ImGuiDialog,
                             public rex::ui::WindowInputListener {
 public:
  // apply(cvar, value): push a binding to the runtime and save the settings.
  using ApplyFn = std::function<void(const std::string& cvar, const std::string& value)>;

  KeyRemapScreen(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
                 rex::ui::Window* window, ApplyFn apply);
  ~KeyRemapScreen() override;

  void Show() { shown_ = true; }
  void Hide();
  bool shown() const { return shown_; }

  // WindowInputListener - only acts while a capture is in progress.
  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void OnKeyUp(rex::ui::KeyEvent& e) override;
  void OnMouseDown(rex::ui::MouseEvent& e) override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Set(size_t index, const std::string& value);

  Fable2Settings* settings_;
  rex::ui::Window* window_;
  ApplyFn apply_;
  bool shown_ = false;
  int capturing_ = -1;    // index into KeyActions, or -1
  bool append_ = false;   // capture adds an alternative rather than replacing
  std::string status_;
};

}  // namespace fable2
