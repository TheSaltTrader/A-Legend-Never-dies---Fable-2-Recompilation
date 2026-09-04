#include "fable2_menu.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/window.h>

#include "fable2_platform.h"

namespace fs = std::filesystem;

namespace fable2 {
namespace {

// --- colours -------------------------------------------------------------
//
// Fable II's own palette: aged gold on dark leather, which is what the game's
// menus are. (ng2recomp uses steel and blood for the same reason - the accent
// should come from the game, not from the framework.)
const ImVec4 kGood(0.42f, 0.80f, 0.47f, 1.0f);
const ImVec4 kBad(0.88f, 0.32f, 0.30f, 1.0f);
const ImVec4 kDim(0.58f, 0.58f, 0.62f, 1.0f);
const ImVec4 kAccent(0.82f, 0.65f, 0.28f, 1.0f);
const ImVec4 kHeading(0.86f, 0.86f, 0.88f, 1.0f);

// Wrapped, not clipped. Several of these lines are two sentences long and the
// overlay is only 620px wide; ImGui::Text would just run them off the edge,
// which is how the footer lost its last three words.
void Muted(const char* fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::TextWrapped("%s", buf);
  ImGui::PopStyleColor();
}

// Pushes the menu font if there is one. ImGui 1.92 takes an explicit size, and
// a null font means "keep the current one", so the fallback is free.
void PushMenuFont(ImFont* font, float size) {
  ImGui::PushFont(font, font != nullptr ? size : 0.0f);
}

// A label with its explanation behind a hover marker.
//
// The explanations were paragraphs under every row, which pushed the settings
// themselves below the fold - the one thing a settings screen must not do.
// They are worth keeping (a setting like "internal render size" is just a
// number otherwise), so they moved into the marker, and only a genuine
// surprise stays on the page as text.
void HelpMarker(const char* text) {
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::TextUnformatted("(?)");
  ImGui::PopStyleColor();
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void RowLabel(const char* label, const char* help) {
  ImGui::TextUnformatted(label);
  if (help && *help)
    HelpMarker(help);
}

void SectionHeader(const char* text, const char* help = nullptr) {
  ImGui::Dummy(ImVec2(0.0f, 10.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  if (help && *help)
    HelpMarker(help);
  // A short accent rule under the heading rather than a full-width separator:
  // the pages are one column of settings, and a line all the way across reads
  // as a page break instead of a group label.
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + 46.0f, p.y + 4.0f),
      ImGui::GetColorU32(kAccent));
  ImGui::Dummy(ImVec2(0.0f, 10.0f));
}

// A path shown in a read-only field: long paths have to be visible and
// selectable, and an ImGui::Text would just clip them.
void PathField(const char* id, const std::string& value) {
  std::string buffer = value;
  buffer.resize(std::max<size_t>(buffer.size() + 1, 512));
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText(id, buffer.data(), buffer.size(),
                   ImGuiInputTextFlags_ReadOnly);
}

// --- cvar helpers --------------------------------------------------------

bool CvarExists(const char* name) {
  return rex::cvar::GetFlagInfo(name) != nullptr;
}

void SetCvar(const char* name, const std::string& value) {
  if (!CvarExists(name)) {
    // Not a silent no-op: a setting whose cvar this build does not have is a
    // real finding, and the menu greys the row for the same reason.
    REXLOG_WARN("Settings: cvar '{}' is not registered; '{}' not applied", name,
                value);
    return;
  }
  if (!rex::cvar::SetFlagByName(name, value))
    REXLOG_WARN("Settings: cvar '{}' rejected value '{}'", name, value);
}

// The values the build itself declares for a string cvar, so a combo can never
// offer an option the presenter does not implement.
//
// `declared` separates the two ways a one-entry list happens: a build that
// really implements one filter, and a cvar that constrains nothing and left us
// falling back to the current value. They deserve different words on screen.
std::vector<std::string> AllowedValues(const char* name,
                                       const std::string& current,
                                       bool* declared) {
  const auto* info = rex::cvar::GetFlagInfo(name);
  if (info != nullptr && !info->constraints.allowed_values.empty()) {
    if (declared) *declared = true;
    return info->constraints.allowed_values;
  }
  if (declared) *declared = false;
  return {current};
}


// The runtime's shader cache, and how many files are in it.
//
// Worth a button rather than a documentation note: the maintainers of the
// unofficial Xenia fork for Fable II are explicit that stale cached shaders
// make its black-texture bug linger, and a cache built by an earlier, buggier
// build of this project is exactly that hazard.
//
// Deliberately narrow. The same root holds save games
// (B13EBABEBABEBABE/...), installed DLC and the player profile; only
// cache/shaders is ever touched, and the path is rebuilt from the user profile
// rather than taken from anything the UI could get wrong.
std::filesystem::path ShaderCachePath() {
  const char* home = std::getenv("USERPROFILE");
  if (!home || !*home) return {};
  return std::filesystem::path(home) / "Documents" / "fable2" / "cache" /
         "shaders";
}

int ShaderCacheFileCount() {
  const auto path = ShaderCachePath();
  std::error_code ec;
  if (path.empty() || !std::filesystem::is_directory(path, ec)) return 0;
  int n = 0;
  for (auto& e : std::filesystem::directory_iterator(path, ec))
    if (e.is_regular_file(ec)) ++n;
  return n;
}

// Returns a line to show the player, so a failure is visible rather than a
// button that appears to do nothing.
std::string ClearShaderCache() {
  const auto path = ShaderCachePath();
  if (path.empty()) return "Could not locate the cache folder.";
  std::error_code ec;
  if (!std::filesystem::is_directory(path, ec))
    return "No shader cache to clear.";
  const int before = ShaderCacheFileCount();
  std::filesystem::remove_all(path, ec);
  if (ec) return "Could not clear the cache: " + ec.message();
  return "Cleared " + std::to_string(before) +
         " cached shader file(s). They rebuild on the next launch.";
}

// --- shared pages --------------------------------------------------------

struct PageOptions {
  // False in the in-game overlay: the window and the guest video mode were
  // created during startup and cannot be rebuilt underneath a running game.
  bool restart_bound_editable = true;
};

// Marks a row that will not take effect until the next launch. Drawn after the
// control so it reads as a footnote on the value, not on the label.
void RestartTag() {
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 0.35f, 1.0f));
  ImGui::TextUnformatted("(restart)");
  ImGui::PopStyleColor();
}

// Settings are drawn as a two-column grid, label beside control. Stacking the
// control under its label doubled the height of every row and pushed half the
// list off a 720p window.
constexpr float kLabelWidth = 190.0f;
constexpr float kControlWidth = 250.0f;
constexpr ImGuiTableFlags kRowTableFlags = ImGuiTableFlags_SizingFixedFit;

// Settings rows are tighter than the rest of the UI. At the global spacing the
// list ran two rows past the bottom of a 720p window, and the choice was
// between dropping settings and reclaiming ~10px a row - which is invisible in
// a table of aligned controls, and costs nothing.
struct TightRows {
  TightRows() {
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(st.ItemSpacing.x, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(st.FramePadding.x, 4.0f));
  }
  ~TightRows() { ImGui::PopStyleVar(2); }
};

// Opens the next label/control row. The control that follows fills the second
// column, so every widget lines up without any of them naming a width.
void RowStart(const char* label, const char* help) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (help && *help)
    HelpMarker(help);
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

struct Resolution {
  int w, h;
};
constexpr Resolution kResolutions[] = {
    {1280, 720},
    {1920, 1080},
    {3840, 2160},
};
constexpr int kResolutionCount = IM_ARRAYSIZE(kResolutions);
// The trailing "Custom" entry is only ever *shown*; it cannot be picked. It
// exists so a hand-edited config with an unusual size is reported honestly
// instead of being silently snapped to 720p the first time the menu opens.
const char* const kResolutionNames[] = {
    "Original (1280 x 720)",
    "Full HD (1920 x 1080)",
    "4K (3840 x 2160)",
    "Custom",
};

int ResolutionIndex(const Fable2Settings& s) {
  for (int i = 0; i < kResolutionCount; ++i) {
    if (kResolutions[i].w == s.window_width && kResolutions[i].h == s.window_height)
      return i;
  }
  return kResolutionCount;  // Custom
}

// 30 and 60 are what the console offered. 120 and 144 are here because they
// were asked for; the row says plainly what they do to a game that paces
// itself off the display.
constexpr int kFpsValues[] = {30, 60, 120, 144};
const char* const kFpsNames[] = {"30 Hz", "60 Hz (as shipped)", "120 Hz", "144 Hz"};

int FpsIndex(int fps) {
  for (int i = 0; i < IM_ARRAYSIZE(kFpsValues); ++i)
    if (kFpsValues[i] == fps) return i;
  return 1;  // 60
}

// Quality presets: one row that moves the three that matter together.
//
// Taken from re:Blue, including the part that makes it work - "Custom" is a
// state the row can *display* but not select. Anything else and every preset
// silently lies about what the other rows say.
struct QualityPreset {
  const char* name;
  int scale;
  const char* aa;
  int aniso;
};
constexpr QualityPreset kPresets[] = {
    {"Performance", 1, "none", -1},
    {"Balanced", 1, "fxaa", 2},
    {"Quality", 2, "fxaa", 4},
    {"Maximum", 3, "fxaa_extreme", 4},
};
constexpr int kPresetCount = IM_ARRAYSIZE(kPresets);
const char* const kPresetNames[] = {"Performance", "Balanced", "Quality",
                                    "Maximum", "Custom"};

int PresetIndex(const Fable2Settings& s) {
  for (int i = 0; i < kPresetCount; ++i) {
    if (kPresets[i].scale == s.resolution_scale &&
        kPresets[i].aniso == s.anisotropic && s.antialias == kPresets[i].aa)
      return i;
  }
  return kPresetCount;  // Custom
}

void ApplyPreset(Fable2Settings& s, int index) {
  if (index < 0 || index >= kPresetCount)
    return;
  s.resolution_scale = kPresets[index].scale;
  s.antialias = kPresets[index].aa;
  s.anisotropic = kPresets[index].aniso;
}

// The cvar's own values are terse ("fxaa_extreme"); these are what a player
// should read. Anything the build declares that is not in this list is shown
// under its raw name rather than dropped.
const char* AaLabel(const std::string& value) {
  if (value == "none") return "Off";
  if (value == "fxaa") return "FXAA";
  if (value == "fxaa_extreme") return "FXAA (extreme)";
  return value.c_str();
}

// The whole settings list, in one page.
//
// It used to be four tabs. One page reads better here: there are barely twenty
// settings, tabs hid half of them behind a click, and the order Display ->
// Enhancements is the order somebody actually sets them in.
//
// Every row is a real setting on a cvar this build registers. Two obvious
// candidates are deliberately absent, because the runtime does not have them:
// an output/upscale filter (`present_effect` declares exactly one value,
// bilinear) and texture replacement (no such facility exists in the GPU
// plugin at all).
bool DrawSettings(Fable2Settings& s, const PageOptions& opts) {
  bool changed = false;
  const bool live = opts.restart_bound_editable;

  // --- Display -----------------------------------------------------------
  SectionHeader("Display");
  TightRows tight_display;
  if (ImGui::BeginTable("display", 2, kRowTableFlags)) {
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
    ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthFixed, kControlWidth);

    RowStart("Fullscreen", "Borderless fullscreen on the current monitor.");
    changed |= ImGui::Checkbox("##fullscreen", &s.fullscreen);

    ImGui::BeginDisabled(!live);
    RowStart("Resolution",
             "The guest is told the display is this size, so it is what the "
             "game renders for - not an upscale of a smaller image.");
    int res_index = ResolutionIndex(s);
    if (ImGui::Combo("##resolution", &res_index, kResolutionNames,
                     IM_ARRAYSIZE(kResolutionNames))) {
      if (res_index < kResolutionCount) {
        s.window_width = kResolutions[res_index].w;
        s.window_height = kResolutions[res_index].h;
        changed = true;
      }
    }
    if (!live) RestartTag();

    RowStart("Frame rate",
             "What the guest is told the display refresh is. 60 is what the "
             "console ran. Measured here: a 30 Hz and a 60 Hz run reach the "
             "same point in the boot sequence at the same wall-clock second, "
             "so this game does not appear to tie its pacing to the reported "
             "refresh the way some 360 titles do. That was measured over the "
             "boot sequence, not in combat.");
    int fps_index = FpsIndex(s.fps);
    if (ImGui::Combo("##fps", &fps_index, kFpsNames, IM_ARRAYSIZE(kFpsNames))) {
      s.fps = kFpsValues[fps_index];
      changed = true;
    }
    if (!live) RestartTag();
    // ng2recomp warns here that above 60 its game runs faster rather than
    // smoother, because Ninja Gaiden II paces its logic off the reported
    // refresh. That is NOT carried over as fact: measured on this title, 30 Hz
    // and 60 Hz runs stay in step, so the warning would be a borrowed claim.
    // Untested above 60, hence "untested" rather than a promise either way.
    if (s.fps > 60)
      Muted("Untested above 60. If the game speeds up rather than looking "
            "smoother, put this back to 60.");
    ImGui::EndDisabled();

    RowStart("V-Sync",
             "Caps presentation to the display. Turning it off usually just "
             "tears; on some 360 titles it also speeds the game up, which was "
             "not reproduced here.");
    changed |= ImGui::Checkbox("##vsync", &s.vsync);

    RowStart("Keep aspect ratio",
             "Letterbox instead of stretching the image to the window.");
    changed |= ImGui::Checkbox("##letterbox", &s.letterbox);

    RowStart("Hide the pointer after",
             "Seconds of mouse stillness over the window before the pointer "
             "disappears. 0 keeps it visible.");
    changed |= ImGui::SliderInt("##cursorhide", &s.cursor_hide_seconds, 0, 30,
                                s.cursor_hide_seconds == 0 ? "never" : "%d s");

    RowStart("Keyboard and mouse",
             "Drives the guest controller from the keyboard. The runtime has "
             "the driver but leaves it off, so without this only a real pad "
             "works. Bindings are on the F4 screen under Input / Keybinds - "
             "Enter is Start and Semicolon or Space is A.");
    changed |= ImGui::Checkbox("##mnk", &s.keyboard_control);

    // The mouse rows only mean anything once the keyboard driver is on, so
    // they follow its state rather than sitting there inert.
    ImGui::BeginDisabled(!s.keyboard_control);
    RowStart("Mouse look",
             "Points the right stick with the mouse. Fable II moves the camera "
             "with the right stick, so this is what makes mouse-look work at "
             "all. Without it the right stick is on the keyboard only.");
    changed |= ImGui::Checkbox("##mnkmouse", &s.mouse_look);

    RowStart("Mouse sensitivity", "How far the stick deflects per unit of "
                                  "mouse movement.");
    float sens = static_cast<float>(s.mouse_sensitivity);
    if (ImGui::SliderFloat("##mnksens", &sens, 0.1f, 5.0f, "%.2f")) {
      s.mouse_sensitivity = sens;
      changed = true;
    }
    ImGui::EndDisabled();

    ImGui::EndTable();
  }

  // --- Audio --------------------------------------------------------------
  SectionHeader("Audio");
  TightRows tight_audio;
  if (ImGui::BeginTable("audio", 2, kRowTableFlags)) {
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
    ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthFixed, kControlWidth);

    RowStart("Mute", "Silences the guest's audio output.");
    changed |= ImGui::Checkbox("##mute", &s.mute);

    RowStart("Audio buffering",
             "How many frames of audio the runtime queues ahead. Fewer frames "
             "means less delay between what happens and what you hear, and "
             "more risk of crackling if the machine cannot keep up. 8 is the "
             "runtime's own default.");
    changed |= ImGui::SliderInt("##aqframes", &s.audio_queue_frames, 4, 32);

    ImGui::EndTable();
  }

  // --- Enhancements ------------------------------------------------------
  SectionHeader("Enhancements");
  TightRows tight_enhance;
  if (ImGui::BeginTable("enhance", 2, kRowTableFlags)) {
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
    ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthFixed, kControlWidth);

    RowStart("Quality preset",
             "Sets supersampling, antialiasing and texture filtering together. "
             "Change any of them below and this reads Custom.");
    int preset = PresetIndex(s);
    if (ImGui::Combo("##preset", &preset, kPresetNames,
                     IM_ARRAYSIZE(kPresetNames))) {
      if (preset < kPresetCount) {
        ApplyPreset(s, preset);
        changed = true;
      }
    }

    ImGui::BeginDisabled(!live);
    RowStart("Supersampling",
             "Renders the game's own framebuffer at a multiple of its size and "
             "filters it back down. The sharpest setting here, and the most "
             "expensive: 2x is four times the pixels.");
    int scale_index = std::clamp(s.resolution_scale - 1, 0, 2);
    const char* scales[] = {"Off", "2x", "3x"};
    if (ImGui::Combo("##scale", &scale_index, scales, 3)) {
      s.resolution_scale = scale_index + 1;
      changed = true;
    }
    if (!live) RestartTag();
    ImGui::EndDisabled();

    // Not restart-bound: the plugin applies this post-process per swap, so it
    // changes with the next frame even in the overlay.
    RowStart("Antialiasing",
             "Post-process FXAA over the finished frame. Cheap, and it softens "
             "the image a little; supersampling is the higher-quality route if "
             "the GPU can afford it.");
    const auto aa_values = AllowedValues("swap_post_effect", s.antialias, nullptr);
    int aa_index = 0;
    for (size_t i = 0; i < aa_values.size(); ++i)
      if (aa_values[i] == s.antialias) aa_index = static_cast<int>(i);
    std::vector<const char*> aa_names;
    for (const auto& v : aa_values) aa_names.push_back(AaLabel(v));
    if (ImGui::Combo("##aa", &aa_index, aa_names.data(),
                     static_cast<int>(aa_names.size()))) {
      s.antialias = aa_values[aa_index];
      changed = true;
    }

    ImGui::BeginDisabled(!live);
    RowStart("Anisotropic filtering",
             "Forces a filtering level on every texture the game samples. "
             "\"Game default\" leaves the title's own sampler settings alone.");
    const char* aniso[] = {"Game default", "1x", "2x", "4x", "8x", "16x"};
    int aniso_index = std::clamp(s.anisotropic + 1, 0, 5);
    if (ImGui::Combo("##aniso", &aniso_index, aniso, 6)) {
      s.anisotropic = aniso_index - 1;
      changed = true;
    }
    if (!live) RestartTag();
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!live);
    RowStart("Black texture fix",
             "Fable II's best-known emulation bug: the hero's and the dog's "
             "textures turn black once the hero grows up. The fix is to read "
             "those textures back from the GPU. 'Some' reads back only what "
             "needs it, which is what the unofficial Xenia fork for this game "
             "does by hand; 'Full' reads everything back and is very "
             "expensive. Start at Some.");
    {
      bool declared = false;
      const auto values = AllowedValues("readback_resolve", s.readback, &declared);
      int index = 0;
      for (size_t i = 0; i < values.size(); ++i)
        if (values[i] == s.readback) index = static_cast<int>(i);
      std::vector<const char*> names;
      names.reserve(values.size());
      for (const auto& v : values) names.push_back(v.c_str());
      if (ImGui::Combo("##readback", &index, names.data(),
                       static_cast<int>(names.size()))) {
        s.readback = values[index];
        changed = true;
      }
    }
    if (!live) RestartTag();
    ImGui::EndDisabled();

    RowStart("Dither the output", "Hides colour banding on 8-bit displays.");
    changed |= ImGui::Checkbox("##dither", &s.present_dither);

    ImGui::EndTable();
  }

  // --- Community patches --------------------------------------------------
  //
  // Kept apart from the settings above, and off by default, because they are
  // not preferences: each one edits how the game itself behaves. They come
  // from Xenia Canary's patch file for this title, and every address was
  // checked against our own image - the disassembly is in
  // config/hooks/patches.toml.
  SectionHeader("Community patches",
                "From Xenia Canary's patch file for Fable II (Margen67, Guy). "
                "These change the game's own behaviour, so they are off by "
                "default. All of them are applied at startup.");
  TightRows tight_patches;
  if (ImGui::BeginTable("patches", 2, kRowTableFlags)) {
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
    ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthFixed, kControlWidth);

    ImGui::BeginDisabled(!live);

    RowStart("60 fps",
             "The game picks a frame divider at startup; this picks the one "
             "that runs at 60 instead of the 30 it shipped with.");
    changed |= ImGui::Checkbox("##p60", &s.patch_60fps);
    if (!live) RestartTag();

    RowStart("Render at 1280 wide",
             "Fable II renders 1120 pixels wide and scales up to the display. "
             "This makes it render 1280, so there is no upscale. Separate from "
             "supersampling above, and they stack.");
    changed |= ImGui::Checkbox("##p720", &s.patch_720p);
    if (!live) RestartTag();

    RowStart("Disable MSAA",
             "Turns off the game's own multisampling. Cheaper, and it frees "
             "EDRAM. Use the antialiasing setting above instead.");
    changed |= ImGui::Checkbox("##pmsaa", &s.patch_disable_msaa);
    if (!live) RestartTag();

    RowStart("30 Hz tick rate",
             "The simulation ticks at 15 Hz. This doubles it to 30, which its "
             "author says markedly improves the in-game UI's frame rate and "
             "input delay. Their own note warns of minor side effects.");
    changed |= ImGui::Checkbox("##ptick", &s.patch_high_tick_rate);
    if (!live) RestartTag();

    RowStart("Disable texture morphing",
             "The older workaround for the black hero and dog textures. Prefer "
             "the black texture fix above - the unofficial Xenia fork for this "
             "game dropped this patch once it had proper readback. Its author "
             "warns makeup stays broken and morphs can look strange.");
    changed |= ImGui::Checkbox("##pmorph", &s.patch_disable_texture_morph);
    if (!live) RestartTag();

    ImGui::EndDisabled();
    ImGui::EndTable();
  }

  return changed;
}


}  // namespace

// ---------------------------------------------------------------------------

MenuFonts& Fonts() {
  static MenuFonts fonts;
  return fonts;
}

void LoadMenuFonts(ImFontAtlas* atlas) {
  if (atlas == nullptr)
    return;
  // Segoe UI ships with every supported Windows version, so this is not a
  // bundled asset that can go missing from a release - and if it somehow is
  // missing, both pointers stay null and the SDK's default font is used.
  const char* kBody = "C:\\Windows\\Fonts\\segoeui.ttf";
  const char* kTitle = "C:\\Windows\\Fonts\\seguibl.ttf";  // Segoe UI Black
  std::error_code ec;
  if (std::filesystem::exists(kBody, ec)) {
    Fonts().body = atlas->AddFontFromFileTTF(kBody, Fonts().body_size);
  }
  const char* title_path =
      std::filesystem::exists(kTitle, ec) ? kTitle
      : std::filesystem::exists(kBody, ec) ? kBody
                                           : nullptr;
  if (title_path != nullptr) {
    Fonts().title = atlas->AddFontFromFileTTF(title_path, Fonts().title_size);
  }
  REXLOG_INFO("Menu fonts: body={} title={}", Fonts().body != nullptr,
              Fonts().title != nullptr);
}

void ApplyMenuStyle(ImGuiStyle& s) {
  // Start from a known base rather than patching whatever the SDK left, so the
  // result does not depend on the SDK's own theme changing under us.
  ImGui::StyleColorsDark(&s);

  s.WindowRounding = 6.0f;
  s.ChildRounding = 4.0f;
  s.FrameRounding = 4.0f;
  s.GrabRounding = 3.0f;
  s.TabRounding = 4.0f;
  s.PopupRounding = 4.0f;
  s.ScrollbarRounding = 6.0f;
  s.WindowBorderSize = 1.0f;
  s.FrameBorderSize = 0.0f;
  s.WindowPadding = ImVec2(22.0f, 18.0f);
  s.FramePadding = ImVec2(10.0f, 6.0f);
  s.ItemSpacing = ImVec2(10.0f, 7.0f);
  s.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
  s.ScrollbarSize = 13.0f;
  s.GrabMinSize = 12.0f;
  s.SeparatorTextBorderSize = 2.0f;

  ImVec4* c = s.Colors;
  const ImVec4 accent = kAccent;
  const ImVec4 accent_hot(0.90f, 0.22f, 0.24f, 1.00f);

  c[ImGuiCol_Text] = ImVec4(0.91f, 0.91f, 0.93f, 1.00f);
  c[ImGuiCol_TextDisabled] = ImVec4(0.44f, 0.44f, 0.48f, 1.00f);
  c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.075f, 0.085f, 0.97f);
  c[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.115f, 0.99f);
  c[ImGuiCol_Border] = ImVec4(0.24f, 0.24f, 0.27f, 0.85f);
  c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.27f, 0.27f, 0.30f, 1.00f);
  c[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.10f, 0.11f, 1.00f);
  c[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.09f, 0.10f, 0.60f);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.28f, 0.31f, 1.00f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.36f, 0.39f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive] = accent;
  c[ImGuiCol_CheckMark] = accent_hot;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accent_hot;
  c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
  c[ImGuiCol_ButtonHovered] = accent;
  c[ImGuiCol_ButtonActive] = accent_hot;
  c[ImGuiCol_Header] = ImVec4(0.24f, 0.16f, 0.17f, 1.00f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.18f, 0.19f, 1.00f);
  c[ImGuiCol_HeaderActive] = accent;
  c[ImGuiCol_Separator] = ImVec4(0.24f, 0.24f, 0.27f, 1.00f);
  c[ImGuiCol_Tab] = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
  c[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.18f, 0.19f, 1.00f);
  c[ImGuiCol_TabSelected] = ImVec4(0.28f, 0.13f, 0.14f, 1.00f);
  c[ImGuiCol_TabSelectedOverline] = accent_hot;
  c[ImGuiCol_TabDimmed] = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
  c[ImGuiCol_TabDimmedSelected] = ImVec4(0.20f, 0.12f, 0.13f, 1.00f);
  c[ImGuiCol_PlotHistogram] = accent;
  c[ImGuiCol_TextSelectedBg] = ImVec4(0.40f, 0.16f, 0.17f, 0.75f);
}

void ApplyLiveSettings(const Fable2Settings& s, rex::ui::Window* window) {
  // FXAA is a post-process the plugin applies per swap, so it changes with the
  // next frame - which is why the antialiasing row is not restart-bound in the
  // overlay even though it belongs to the GPU plugin.
  SetCvar("swap_post_effect", s.antialias);
  SetCvar("present_dither", s.present_dither ? "true" : "false");
  SetCvar("present_letterbox", s.letterbox ? "true" : "false");
  SetCvar("vsync", s.vsync ? "true" : "false");
  SetCvar("mnk_mode", s.keyboard_control ? "true" : "false");
  SetCvar("mnk_mouse", s.mouse_look ? "true" : "false");
  SetCvar("mnk_sensitivity", std::to_string(s.mouse_sensitivity));
  SetCvar("audio_mute", s.mute ? "true" : "false");
  // Deliberately NOT applied live: audio_maxqframes sizes a buffer the audio
  // system allocates at startup, and anisotropic_override /
  // draw_resolution_scale_* are read by the GPU plugin at init. Writing them
  // here would report success and do nothing - which is exactly the trap that
  // made internal scaling look impossible on ng2recomp. Both rows are shown
  // restart-bound in the overlay instead.
  if (window != nullptr) {
    window->SetFullscreen(s.fullscreen);
    // 0 means never hide, which the window spells as a zero delay meaning
    // "immediately" - so the two have to be told apart here.
    window->SetCursorAutoHideDelayMs(
        s.cursor_hide_seconds > 0
            ? static_cast<uint32_t>(s.cursor_hide_seconds) * 1000u
            : 0u);
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
  }
}

// --- SetupScreen -----------------------------------------------------------

SetupScreen::SetupScreen(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
                         std::function<void(bool)> on_done,
                         std::function<void()> on_advanced)
    : ImGuiDialog(drawer),
      settings_(settings),
      on_done_(std::move(on_done)),
      on_advanced_(std::move(on_advanced)) {
  install_dest_ = settings_->ResolvedGamePath();

  // Test seam, matching the NG2_* overrides the app already reads: preselect
  // the disc image and the destination so the installer can be driven by a
  // script. Without it the only way in is a native file dialog, which cannot
  // be automated without typing a path into a modal window.
  if (const char* iso = std::getenv("NG2_ISO"); iso && *iso) {
    iso_path_ = iso;
    iso_info_ = InspectDisc(iso_path_);
  }
  if (const char* dest = std::getenv("NG2_INSTALL_DEST"); dest && *dest)
    install_dest_ = dest;

  RefreshGame();
}

SetupScreen::~SetupScreen() {
  // The worker writes into progress_, which dies with this object.
  if (install_thread_.joinable()) {
    progress_.cancel = true;
    install_thread_.join();
  }
}

void SetupScreen::RefreshGame() {
  const auto path = settings_->ResolvedGamePath();
  game_path_inspected_ = path.string();
  game_info_ = InspectFolder(path);
}

void SetupScreen::StartInstall() {
  if (install_thread_.joinable())
    install_thread_.join();
  install_started_ = true;
  install_thread_ = ExtractDiscAsync(iso_path_, install_dest_, progress_);
}

void SetupScreen::OnDraw(ImGuiIO& io) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::Begin("fable2setup", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  PushMenuFont(Fonts().body, Fonts().body_size);

  // One centred column. Full-window rows would run a line of help text across
  // 1600 pixels, which is unreadable and looks like a log file rather than a
  // front end.
  constexpr float kColumnWidth = 1180.0f;
  const float avail = ImGui::GetContentRegionAvail().x;
  const float column = std::min(kColumnWidth, avail);
  const float indent = (avail - column) * 0.5f;
  if (indent > 0.0f)
    ImGui::Indent(indent);
  ImGui::BeginGroup();
  ImGui::PushItemWidth(column);

  PushMenuFont(Fonts().title, Fonts().title_size);
  ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
  ImGui::TextUnformatted("FABLE II");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  Muted("Game of the Year Edition. Static recompilation for PC.");
  ImGui::Dummy(ImVec2(0.0f, 6.0f));

  // Footer height, reserved so the page body scrolls instead of pushing the
  // buttons off the bottom on a small window.
  const float footer = ImGui::GetFrameHeightWithSpacing() * 2.6f;
  ImGui::BeginChild("body", ImVec2(column, -footer), ImGuiChildFlags_None);

  // Two columns, so the whole thing is visible at once on a 720p window:
  // settings on the left, because they are what somebody comes back for, and
  // content on the right, which is usually set once and never touched again.
  // Stacked in one column it ran to two screens and the settings were the half
  // that fell off the bottom.
  if (ImGui::BeginTable("layout", 2, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    DrawSettings(*settings_, PageOptions{});

    ImGui::TableSetColumnIndex(1);
    DrawContent();
    ImGui::EndTable();
  }

  ImGui::EndChild();

  ImGui::Separator();
  DrawFooter(column);

  ImGui::PopItemWidth();
  ImGui::EndGroup();
  if (indent > 0.0f)
    ImGui::Unindent(indent);
  ImGui::PopFont();
  ImGui::End();
  (void)io;
}

void SetupScreen::DrawContent() {
  SectionHeader("Game data",
                "The folder holding default.xex and the game's data files - "
                "the contents of your own disc.");
  PathField("##gamepath", game_path_inspected_);
  if (ImGui::Button("Choose folder...")) {
    if (auto picked = PickFolder("Select the folder containing default.xex",
                                 settings_->ResolvedGamePath())) {
      settings_->game_path = picked->string();
      RefreshGame();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Rescan"))
    RefreshGame();

  ImGui::TextColored(game_info_.Usable() ? kGood : kBad, "%s",
                     game_info_.message.c_str());
  if (game_info_.Usable() && !game_info_.IsFable2()) {
    ImGui::TextColored(kBad,
                       "This is not Fable II. The recompiled code is this "
                       "game's; another title will not run.");
  }

  DrawInstaller();
}

void SetupScreen::DrawInstaller() {
  SectionHeader("Install from a disc image",
                "Copies a .iso to a folder on disk. The runtime mounts a "
                "folder, not an image, so an ISO has to be extracted once "
                "before it can be played. Re-running skips files already "
                "there at the right size.");
  PathField("##isopath", iso_path_.string());
  if (ImGui::Button("Choose disc image...")) {
    if (auto picked = PickFile("Select an Xbox 360 disc image",
                               {{"Disc images", "*.iso;*.img;*.xex"},
                                {"All files", "*.*"}},
                               iso_path_)) {
      iso_path_ = *picked;
      iso_info_ = InspectDisc(iso_path_);
    }
  }
  if (!iso_path_.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(iso_info_.Usable() ? kGood : kBad, "%s",
                       iso_info_.message.c_str());
  }

  // Where it lands only matters once there is something to install, and it
  // defaults to the game folder. Showing it unconditionally cost three rows on
  // a screen that has to fit, for a question most people never answer.
  const bool busy = progress_.running.load();
  if (!iso_path_.empty()) {
    ImGui::Spacing();
    ImGui::TextUnformatted("Install to");
    PathField("##installdest", install_dest_.string());
    if (ImGui::Button("Choose destination...")) {
      if (auto picked = PickFolder("Where should the game data be installed?",
                                   install_dest_)) {
        install_dest_ = *picked;
      }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || !iso_info_.Usable() || install_dest_.empty());
    if (ImGui::Button("Install")) {
      StartInstall();
    }
    ImGui::EndDisabled();
  }

  if (busy || install_started_) {
    const uint64_t total = progress_.bytes_total.load();
    const uint64_t done = progress_.bytes_done.load();
    const float fraction =
        total > 0 ? static_cast<float>(double(done) / double(total)) : 0.0f;
    char overlay[128];
    std::snprintf(overlay, sizeof(overlay), "%llu / %llu files  -  %s",
                  static_cast<unsigned long long>(progress_.files_done.load()),
                  static_cast<unsigned long long>(progress_.files_total.load()),
                  FormatBytes(done).c_str());
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
    Muted("%s", progress_.CurrentFile().c_str());

    if (busy) {
      if (ImGui::Button("Cancel install"))
        progress_.cancel = true;
    } else if (progress_.complete.load()) {
      // One-shot: adopt the destination as the game folder so Play lights up
      // without the user having to point at what we just wrote.
      if (progress_.failed.load()) {
        ImGui::TextColored(kBad, "%s", progress_.Error().c_str());
      } else {
        ImGui::TextColored(kGood, "Install complete.");
        if (settings_->ResolvedGamePath() != install_dest_) {
          settings_->game_path = install_dest_.string();
          RefreshGame();
        }
      }
    }
  }
}

// No longer on the setup screen - the key list moved out when the workarounds
// needed the room, and both F4 and the in-game overlay list the binds anyway.
// Kept because the "where settings are kept" note is the only place that is
// written down in the app itself.
void SetupScreen::DrawAbout() {
  SectionHeader("Keys");
  ImGui::BulletText("F10  -  these settings, over the running game");
  ImGui::BulletText("F4   -  every runtime setting, unfiltered");
  ImGui::BulletText("F3   -  frame timing overlay");
  ImGui::BulletText("`    -  console");

  SectionHeader("Where settings are kept");
  Muted("fable2_settings.cfg holds the choices on this screen. cache/"
        "fable2_tuning.toml is rewritten from them on every launch and is not "
        "meant to be edited. fable2.toml is the runtime's own config, written by "
        "the F4 screen.");

  SectionHeader("Shader cache");
  {
    static std::string cache_status;
    const int n = ShaderCacheFileCount();
    Muted("The runtime caches translated shaders and pipelines between runs. "
          "Clear them if the picture starts misbehaving after a build change - "
          "a cache built by an older build is a known cause of Fable II's "
          "black-texture bug lingering. Saves and DLC are not touched.");
    if (ImGui::Button(n > 0 ? "Clear shader cache" : "Shader cache is empty")) {
      if (n > 0) cache_status = ClearShaderCache();
    }
    if (!cache_status.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(kGood, "%s", cache_status.c_str());
    }
  }

  SectionHeader("This screen");
  Muted("It opens on the first run, and any time Shift is held at launch. "
        "Once you press Play it stays out of the way.");

  SectionHeader("Downloadable content");
  Muted("There is no DLC page, on purpose. Knothole Island and See the Future "
        "are both already on the Game of the Year disc - their levels are in "
        "data/levels.bnk and the expansions' code is in the executable - so "
        "there is nothing to install. For a package that genuinely is not on "
        "the disc, pass --dlc_root <folder> or run tools/install_dlc.cmd.");
}

void SetupScreen::DrawFooter(float column_width) {
  const bool can_play = game_info_.Usable();

  // An install can run for minutes, and its own progress bar sits far enough
  // down the Content page to be below the fold - which read as a UI that had
  // simply stopped responding. The footer is always on screen, so the state
  // goes here too.
  if (progress_.running.load()) {
    const uint64_t total = progress_.bytes_total.load();
    const uint64_t done = progress_.bytes_done.load();
    const float fraction =
        total > 0 ? static_cast<float>(double(done) / double(total)) : 0.0f;
    char overlay[160];
    std::snprintf(overlay, sizeof(overlay), "Installing  %llu / %llu files  -  %s",
                  static_cast<unsigned long long>(progress_.files_done.load()),
                  static_cast<unsigned long long>(progress_.files_total.load()),
                  FormatBytes(done).c_str());
    ImGui::ProgressBar(fraction, ImVec2(column_width, 0.0f), overlay);
  } else if (!can_play) {
    ImGui::TextColored(kBad,
                       "Choose a folder containing default.xex, or install "
                       "from a disc image, before starting.");
  } else if (!game_info_.IsFable2()) {
    ImGui::TextColored(kBad, "The selected folder is not Fable II.");
  } else {
    Muted("Ready.");
  }

  if (on_advanced_ && ImGui::Button("Advanced settings...")) {
    on_advanced_();
  }

  // Right-align the two decisions inside the centred column, not the window:
  // GetContentRegionMax is obsolete in this ImGui and the window is wider than
  // the content anyway.
  // SameLine's offset is measured from the *indented* line start, so it must
  // not include the column's own left offset - adding that applied the indent
  // twice and pushed Play off the right of the column. Measured: asking for
  // 820 put the button at 1010, exactly one indent too far.
  const float button_w = 130.0f;
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::SameLine(column_width - button_w * 2.0f - style.ItemSpacing.x);
  if (ImGui::Button("Quit", ImVec2(button_w, 0)) && !finished_) {
    finished_ = true;
    on_done_(false);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_play || progress_.running.load());
  if (ImGui::Button("Play", ImVec2(button_w, 0)) && !finished_) {
    finished_ = true;
    settings_->configured = true;
    settings_->Clamp();
    settings_->Save();
    on_done_(true);
  }
  ImGui::EndDisabled();
}

// --- SettingsOverlay -------------------------------------------------------

SettingsOverlay::SettingsOverlay(rex::ui::ImGuiDrawer* drawer,
                                 Fable2Settings* settings, rex::ui::Window* window,
                                 std::function<void()> on_advanced)
    : ImGuiDialog(drawer),
      settings_(settings),
      window_(window),
      on_advanced_(std::move(on_advanced)) {}

SettingsOverlay::~SettingsOverlay() = default;

void SettingsOverlay::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowSize(ImVec2(680, 620), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  // Before Begin, so the title bar is drawn in the menu's font too. Pushed
  // after it, the window keeps the SDK's 13px bitmap face and the title reads
  // as a different application from its own contents.
  PushMenuFont(Fonts().body, Fonts().body_size);
  ImGui::Begin("Fable II - Settings", nullptr,
               ImGuiWindowFlags_NoSavedSettings);

  PageOptions opts;
  opts.restart_bound_editable = false;

  bool changed = false;
  ImGui::BeginChild("body", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f));
  changed |= DrawSettings(*settings_, opts);

  SectionHeader("Content");
  PathField("##gamepath", settings_->ResolvedGamePath().string());
  ImGui::Spacing();
  Muted("The game folder is chosen on the setup screen, which runs before the "
        "game is loaded. Hold Shift while launching to get it back.");
  ImGui::EndChild();

  if (changed) {
    // Everything the presenter re-reads takes effect on the next paint. The
    // rest is written to disk and waits for the next launch, which is what the
    // greyed rows above are telling the player.
    ApplyLiveSettings(*settings_, window_);
  }

  ImGui::Separator();
  Muted("Greyed settings are fixed for this session - the window and the "
        "guest video mode are built during startup.");

  if (on_advanced_ && ImGui::Button("Advanced settings...")) {
    on_advanced_();
  }
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    settings_->Clamp();
    settings_->Save();
    status_ = "Saved to fable2_settings.cfg";
  }
  if (!status_.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(kGood, "%s", status_.c_str());
  }

  ImGui::End();
  ImGui::PopFont();
}

}  // namespace fable2
