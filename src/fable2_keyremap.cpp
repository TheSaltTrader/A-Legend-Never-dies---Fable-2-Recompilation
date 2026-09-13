#include "fable2_keyremap.h"

#include <cstring>

#include <imgui.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>

#include "fable2_menu.h"  // Fonts()

namespace fable2 {

namespace {

// The runtime's own defaults, except the D-pad: this port has always put the
// D-pad on the plain arrows (the menu navigates with them) where the runtime
// ships Shift+arrows. The right stick shares the arrows, as before.
const KeyAction kActions[] = {
    {"keybind_a", "A", "Semicolon,Space"},
    {"keybind_b", "B", "Quote,Backspace"},
    {"keybind_x", "X", "L"},
    {"keybind_y", "Y", "P"},
    {"keybind_start", "Start", "X,Return"},
    {"keybind_back", "Back", "Z,Tab"},
    {"keybind_left_trigger", "Left trigger", "Q,I"},
    {"keybind_right_trigger", "Right trigger", "E,O"},
    {"keybind_left_shoulder", "Left shoulder", "1"},
    {"keybind_right_shoulder", "Right shoulder", "3"},
    {"keybind_lstick_up", "Left stick up", "W"},
    {"keybind_lstick_down", "Left stick down", "S"},
    {"keybind_lstick_left", "Left stick left", "A"},
    {"keybind_lstick_right", "Left stick right", "D"},
    {"keybind_lstick_press", "Left stick press", "F"},
    {"keybind_rstick_up", "Right stick up", "Up"},
    {"keybind_rstick_down", "Right stick down", "Down"},
    {"keybind_rstick_left", "Right stick left", "Left"},
    {"keybind_rstick_right", "Right stick right", "Right"},
    {"keybind_rstick_press", "Right stick press", "K"},
    {"keybind_dpad_up", "D-pad up", "Up"},
    {"keybind_dpad_down", "D-pad down", "Down"},
    {"keybind_dpad_left", "D-pad left", "Left"},
    {"keybind_dpad_right", "D-pad right", "Right"},
    {"keybind_guide", "Guide", ""},
};

// Every input listener is asked from the highest z down and a handled event
// stops there; the drawer that feeds ImGui sits far below this.
constexpr size_t kCaptureZ = size_t(1) << 40;

bool IsModifierOnly(rex::ui::VirtualKey vk) {
  using K = rex::ui::VirtualKey;
  return vk == K::kShift || vk == K::kControl || vk == K::kMenu || vk == K::kLShift ||
         vk == K::kRShift || vk == K::kLControl || vk == K::kRControl || vk == K::kLMenu ||
         vk == K::kRMenu;
}

}  // namespace

const KeyAction* KeyActions(size_t* count) {
  *count = sizeof(kActions) / sizeof(kActions[0]);
  return kActions;
}

std::string KeyBinding(const Fable2Settings& s, const KeyAction& a) {
  auto it = s.keybinds.find(a.cvar);
  return it == s.keybinds.end() ? std::string(a.fallback) : it->second;
}

KeyRemapScreen::KeyRemapScreen(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
                               rex::ui::Window* window, ApplyFn apply)
    : rex::ui::ImGuiDialog(drawer), settings_(settings), window_(window),
      apply_(std::move(apply)) {
  if (window_) window_->AddInputListener(this, kCaptureZ);
}

KeyRemapScreen::~KeyRemapScreen() {
  if (window_) window_->RemoveInputListener(this);
}

void KeyRemapScreen::Hide() {
  shown_ = false;
  capturing_ = -1;
}

void KeyRemapScreen::Set(size_t index, const std::string& value) {
  const KeyAction& a = kActions[index];
  settings_->keybinds[a.cvar] = value;
  if (apply_) apply_(a.cvar, value);
  REXLOG_INFO("Keys: {} = '{}'", a.label, value);
}

void KeyRemapScreen::OnKeyDown(rex::ui::KeyEvent& e) {
  if (capturing_ < 0) return;
  const rex::ui::VirtualKey vk = e.virtual_key();
  if (IsModifierOnly(vk)) {
    e.set_handled(true);  // wait for the key the modifier goes with
    return;
  }
  if (vk == rex::ui::VirtualKey::kEscape) {
    capturing_ = -1;
    status_ = "Cancelled.";
    e.set_handled(true);
    return;
  }
  const std::string name = rex::ui::VirtualKeyToString(vk);
  if (name.empty()) {
    status_ = "That key has no name the driver knows - try another.";
    e.set_handled(true);
    return;
  }
  std::string token;
  if (e.is_shift_pressed()) token += "Shift+";
  if (e.is_ctrl_pressed()) token += "Ctrl+";
  if (e.is_alt_pressed()) token += "Alt+";
  token += name;
  const size_t index = size_t(capturing_);
  std::string value = token;
  if (append_) {
    const std::string current = KeyBinding(*settings_, kActions[index]);
    if (!current.empty()) value = current + "," + token;
  }
  Set(index, value);
  status_ = std::string(kActions[index].label) + " = " + value;
  capturing_ = -1;
  e.set_handled(true);
}

void KeyRemapScreen::OnKeyUp(rex::ui::KeyEvent& e) {
  if (capturing_ >= 0) e.set_handled(true);
}

void KeyRemapScreen::OnDraw(ImGuiIO& io) {
  if (!shown_) return;
  ImGui::SetNextWindowSize(ImVec2(620, 640), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  ImGui::PushFont(Fonts().body, Fonts().body != nullptr ? Fonts().body_size : 0.0f);
  bool open = true;
  const bool visible = ImGui::Begin("Fable II - Keyboard bindings", &open,
                                    ImGuiWindowFlags_NoSavedSettings);
  if (visible) {
    ImGui::TextWrapped(
        "Each action lists the keys that press it, separated by commas. Set "
        "replaces them with the next key you press, Add keeps them and adds "
        "one, Clear empties the action. Hold Shift, Ctrl or Alt while pressing "
        "to bind the combination. Escape cancels a capture. Changes apply at "
        "once and are saved.");
    ImGui::Spacing();
    if (!settings_->keyboard_control) {
      ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.0f),
                         "Keyboard and mouse is off in the settings - these bindings "
                         "take effect when it is on.");
      ImGui::Spacing();
    }
    if (ImGui::BeginTable("##keys", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.30f);
      ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch, 0.42f);
      ImGui::TableSetupColumn("##buttons", ImGuiTableColumnFlags_WidthStretch, 0.28f);
      ImGui::TableHeadersRow();
      size_t count = 0;
      const KeyAction* actions = KeyActions(&count);
      for (size_t i = 0; i < count; ++i) {
        const KeyAction& a = actions[i];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(a.label);
        ImGui::TableSetColumnIndex(1);
        if (capturing_ == int(i)) {
          ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.60f, 1.0f),
                             append_ ? "press a key to add..." : "press a key...");
        } else {
          const std::string b = KeyBinding(*settings_, a);
          if (b.empty())
            ImGui::TextDisabled("(none)");
          else
            ImGui::TextUnformatted(b.c_str());
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::PushID(int(i));
        if (ImGui::SmallButton("Set")) {
          capturing_ = int(i);
          append_ = false;
          status_.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Add")) {
          capturing_ = int(i);
          append_ = true;
          status_.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
          Set(i, "");
          status_ = std::string(a.label) + " cleared";
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Defaults")) {
      size_t count = 0;
      const KeyAction* actions = KeyActions(&count);
      settings_->keybinds.clear();
      for (size_t i = 0; i < count; ++i)
        if (apply_) apply_(actions[i].cvar, actions[i].fallback);
      status_ = "Defaults restored.";
      capturing_ = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) open = false;
    if (!status_.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", status_.c_str());
    }
  }
  ImGui::End();
  ImGui::PopFont();
  if (!open) Hide();
}

}  // namespace fable2
