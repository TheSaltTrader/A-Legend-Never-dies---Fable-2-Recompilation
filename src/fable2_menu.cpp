#include "fable2_menu.h"

#include "fable2_textool.h"
#include "fable2_diagnostics.h"
#include "fable2_perf.h"

#include "fable2_saveimport.h"
#include "fable2_titleupdate.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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
  // The App's texture run, borrowed by whichever screen is drawing the
  // Textures section. Null hides the section's controls.
  TextureJob* tex_job = nullptr;
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

// The upscaling filters, named for a player rather than for the API.
const char* UpscaleLabel(const std::string& value) {
  if (value == "bilinear") return "Bilinear (soft)";
  if (value == "fsr") return "FSR 1.0 (sharp)";
  if (value == "cas") return "CAS (sharpen only)";
  return value.c_str();
}

// present_effect's allowed values, minus the ones that would be a lie.
//
// A runtime built with the full FidelityFX SDK also declares fsr2 and fsr3.
// Those are a TEMPORAL upscaler needing real depth and motion vectors, which
// this runtime does not have - it synthesizes them, says so in a warning, and
// falls back to spatial FSR when the ffx-api library is absent. Offering them
// would put three names for one filter in the list, so they are dropped.
// Anything else the runtime declares is passed through untouched.
std::vector<std::string> UpscaleValues(const std::string& current) {
  std::vector<std::string> out;
  for (const auto& v : AllowedValues("present_effect", current, nullptr)) {
    if (v == "fsr2" || v == "fsr3") continue;
    out.push_back(v);
  }
  if (out.empty()) out.push_back(current);
  return out;
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
// The texture pack: extract, enlarge, load back. Laid out like the NG2 port's
// so the two read the same.
void DrawTexturesSection(Fable2Settings& s, const PageOptions& opts, bool& changed) {
  SectionHeader("Textures",
                "Extract the game's textures, enlarge them with an AI "
                "upscaler, and load the results back in. Dumping costs a file "
                "write the first time each texture is seen; leave it off once "
                "a pack is made.");

  // The run is the App's (see TextureJob): both screens borrow it, so a build
  // started here keeps going when this screen closes.
  fable2::TextureJob* job = opts.tex_job;
  if (job == nullptr) {
    Muted("The texture tools are not available on this screen.");
    return;
  }
  if (!job->tools_probed) {
    job->tools = fable2::FindTextureTools();
    job->tools_probed = true;
  }

  const bool have_path = !s.texture_path.empty();
  const std::filesystem::path dir =
      have_path ? std::filesystem::path(s.texture_path) : std::filesystem::path();
  // Counted OFF the UI thread, and not every frame.
  //
  // These walk the dump and pack folders. That is fine while a pack is a few
  // hundred files and fatal once it is not: on the NG2 port, at 10,669 dumped
  // and 4,125 packed, this scanned ~14,800 directory entries EVERY FRAME the
  // section was open, produced a 2,318 ms frame during a video, and the GPU
  // driver reset for the missed deadline. The counts are advisory: a value a
  // few seconds stale is worth a great deal more than a stalled frame.
  //
  // The numbers are the ones that matter to the player: textures that CAN be
  // enhanced, how many of those are in the pack, how many are waiting. Not
  // "files dumped" - that count included the HUD, fonts, normal maps and video
  // frames the tool never packs, and read as hundreds of textures missing when
  // nothing was.
  struct TextureCounts {
    std::atomic<int> dumped{0};      // enhanceable textures in the dump
    std::atomic<int> packed{0};      // of those, in the pack
    std::atomic<int> waiting{0};     // of those, not yet in the pack
    std::atomic<int> excluded{0};    // dumped but never packed, by design
    std::atomic<int> tex_files{0};   // .tex files in the pack
    // What the pack says it was made with (pack/pack.txt). 0 = no record.
    std::atomic<int> manifest{0};
    std::atomic<int> pack_scale{0};
    std::atomic<int> pack_upscaler{0};        // 1 Lanczos, 2 Real-ESRGAN
    std::atomic<int> pack_strength_x100{0};
    std::atomic<bool> pack_complete{true};
    std::atomic<bool> counting{false};
    std::atomic<bool> have{false};
    double last = -1.0e9;
    std::string path;
  };
  static TextureCounts counts;
  if (have_path) {
    const std::string dir_str = dir.string();
    const double now = ImGui::GetTime();
    // Re-scan on a path change, otherwise at most every few seconds - and
    // again once a run finishes, so the button's count is not stale.
    const bool stale = (counts.path != dir_str) || (now - counts.last > 5.0);
    if (stale && !counts.counting.exchange(true)) {
      counts.path = dir_str;
      counts.last = now;
      std::thread([dir_str] {
        const fable2::PackCensus c = fable2::CountPack(std::filesystem::path(dir_str));
        counts.dumped.store(c.candidates);
        counts.packed.store(c.packed);
        counts.waiting.store(c.waiting);
        counts.excluded.store(c.excluded);
        counts.tex_files.store(c.tex_files);
        counts.manifest.store(c.manifest ? 1 : 0);
        counts.pack_scale.store(c.pack_scale);
        counts.pack_upscaler.store(c.pack_upscaler == "realesrgan" ? 2
                                   : c.pack_upscaler == "lanczos" ? 1 : 0);
        counts.pack_strength_x100.store(int(c.pack_strength * 100.0f + 0.5f));
        counts.pack_complete.store(c.pack_complete);
        counts.have.store(true);
        counts.counting.store(false);
      }).detach();
    }
  }
  const int dumped = have_path ? counts.dumped.load() : 0;
  const int packed = have_path ? counts.tex_files.load() : 0;
  const int in_pack = have_path ? counts.packed.load() : 0;
  const int waiting = have_path ? counts.waiting.load() : 0;
  const int excluded = have_path ? counts.excluded.load() : 0;
  const bool have_manifest = have_path && counts.manifest.load() != 0;
  const int pack_scale = counts.pack_scale.load();
  const int pack_upscaler = counts.pack_upscaler.load();
  const float pack_strength = float(counts.pack_strength_x100.load()) / 100.0f;
  const bool pack_complete = counts.pack_complete.load();
  const bool busy = job->progress.running.load();

  {
    TightRows tight;
    // GUARDED. BeginTable returns false when its window is collapsed or not
    // visible this frame - which a fullscreen re-apply from ApplyLiveSettings
    // (every F9) can produce - and a RowStart after that calls
    // ImGui::TableNextRow on a null table. That was the 2026-09-11 crash, in a
    // minidump: rexruntime!ImGui::TableNextRow+0x1f reading address 0x237,
    // called from this section with the settings menu open. NG2's sweep for
    // RowStart-outside-a-table could not see it: lexically this call sits
    // inside the table; only at run time is there no table.
    if (!ImGui::BeginTable("texrows", 2, ImGuiTableFlags_SizingStretchProp)) {
      static bool said = false;
      if (!said) {
        said = true;
        REXLOG_INFO("Textures section: table not visible this frame - rows skipped");
      }
    } else {

    RowStart("Folder",
             "Where dumped and upscaled textures are kept. Needs room: the raw "
             "dump is roughly the size of the game's texture data.");
    {
      char buf[512];
      std::snprintf(buf, sizeof(buf), "%s", s.texture_path.c_str());
      // Leave room for a Browse button on the same line. Typing a path still
      // works - the field is the fallback for a path pasted from elsewhere -
      // but a picker is what most people expect, and every other folder on
      // the setup screen already has one.
      const float browse_w = 96.0f;
      ImGui::SetNextItemWidth(-(browse_w + ImGui::GetStyle().ItemSpacing.x));
      if (ImGui::InputText("##texpath", buf, sizeof(buf))) {
        s.texture_path = buf;
        changed = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Browse...##tex", ImVec2(browse_w, 0.0f))) {
        const std::filesystem::path start =
            s.texture_path.empty() ? std::filesystem::path()
                                   : std::filesystem::path(s.texture_path);
        if (auto picked = PickFolder("Select a folder for textures", start)) {
          s.texture_path = picked->string();
          changed = true;
        }
      }
    }

    RowStart("Dump while playing",
             "Writes every texture the game loads. Play the regions you care "
             "about with this on - a run that only reaches the menu collects "
             "video frames and little else.\n\n"
             "Dumping and the pack are one or the other, never both: together "
             "they put a file write AND a file read on the GPU thread for every "
             "new texture, which starves the command stream.");
    // One or the other, never both.
    //
    // Dumping writes a file and stats a folder on the GPU thread for every
    // texture the decoder creates; the pack reads one for every texture it
    // replaces. Together they put the disk in the middle of the render thread
    // and starve the command stream - measured on the NG2 port, and previously
    // misdiagnosed there as a video-mode fault. Making them mutually exclusive
    // is the difference between a documented warning nobody reads and a state
    // that cannot happen. Clamp() enforces the same rule on a loaded file.
    ImGui::BeginDisabled(!have_path || s.texture_pack);
    if (ImGui::Checkbox("##texdump", &s.texture_dump)) {
      if (s.texture_dump)
        s.texture_pack = false;
      changed = true;
    }
    ImGui::EndDisabled();
    if (s.texture_pack)
      Muted("Turn the pack off first - dumping and loading together stall the "
            "command stream.");

    RowStart("Use the upscaled textures",
             "Loads the finished pack instead of the game's own textures. "
             "Nothing happens until a pack has been made.\n\n"
             "F9 toggles this during play, without opening this screen - which "
             "is the only practical way to compare the pack against the "
             "original, since the difference is in detail the eye loses while a "
             "menu is in the way.\n\n"
             "F9 does NOT change this setting. It is a look, not a decision, so "
             "leaving a comparison half-finished cannot quietly turn the pack "
             "off for the next launch. This checkbox is what persists.");
    ImGui::BeginDisabled(!have_path || packed == 0 || s.texture_dump);
    if (ImGui::Checkbox("##texuse", &s.texture_pack)) {
      if (s.texture_pack)
        s.texture_dump = false;
      changed = true;
    }
    ImGui::EndDisabled();
    if (s.texture_dump)
      Muted("Turn dumping off first - see above.");
    else if (have_path && packed == 0)
      Muted("No pack yet. Dump some textures and press Process textures.");

    ImGui::EndTable();
    }
  }

  // Say the shortcut in the panel itself, not only in a tooltip: a key nobody
  // is told about is a key nobody presses.
  if (have_path && counts.have.load()) {
    if (dumped == 0)
      Muted("No textures dumped yet.");
    else if (waiting == 0)
      Muted("%d textures can be enhanced - all %d are in the pack.  Press F9 in "
            "game to switch the pack on and off and see the difference.",
            dumped, in_pack);
    else
      Muted("%d textures can be enhanced: %d in the pack, %d waiting to be "
            "processed.", dumped, in_pack, waiting);
    if (excluded > 0)
      Muted("(%d more were dumped but are never enhanced - HUD, fonts, normal "
            "maps, video frames and other non-art - so they are not counted.)",
            excluded);
  }
  // Whether the enhanced textures are in use RIGHT NOW, from the renderer
  // itself rather than from the checkbox: the checkbox is what persists, F9
  // is what is live, and the two can differ mid-comparison. The counters are
  // the plugin's own, read across the DLL boundary by name.
  if (have_path && packed > 0) {
    const bool live = !rex::cvar::Query<std::string>("texture_pack_path").empty();
    const int32_t replaced = rex::cvar::Query<int32_t>("texture_pack_replaced");
    // Deliberately NOT a live count. The plugin's replaced/original counters
    // are per-scene - they count only the textures resident for what is on
    // screen this frame - so a number here ticks constantly and, worse, reads
    // as "only 57 textures were ever enhanced" when the pack holds thousands.
    // The stable, true figure is the census line above. All that is worth
    // saying live is whether the pack is actually reaching the screen.
    if (!live)
      Muted("Enhanced textures: OFF - the game is showing its original textures.  "
            "Tick 'Use the upscaled textures' or press F9.");
    else if (replaced > 0)
      Muted("Enhanced textures: ON and in use - the pack is replacing textures "
            "on screen now.  F9 switches.");
    else
      Muted("Enhanced textures: ON - nothing on this screen is in the pack yet, "
            "so it looks unchanged here.  F9 switches.");
  }
  if (s.texture_dump)
    Muted("Dumping starts as soon as it is ticked - the pack is switched off for "
          "it and every texture on screen is written out.");

  // --- the run -----------------------------------------------------------
  if (busy) {
    // The run has steps - decode everything, then upscale the art - and the
    // script reports each as its own bar. A new step restarts the bar AND the
    // clock: with one clock the estimate for the upscaling step included the
    // whole of the decoding step's time and quoted 228 minutes for a
    // 26-minute job, right after showing 100%.
    const int phase = job->progress.phase.load();
    const int phases = job->progress.phases.load();
    if (phase != job->phase_seen) {
      job->phase_seen = phase;
      job->started_at = ImGui::GetTime();
    }

    const double done = double(job->progress.files_done.load());
    const double total = double(job->progress.files_total.load());
    float fraction = total > 0.0 ? float(done / total) : 0.0f;
    fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);

    char step[40] = "";
    if (phases > 0)
      std::snprintf(step, sizeof(step), "Step %d of %d  -  ", phase, phases);

    // Time remaining, from the rate so far in THIS step. Shown only once there
    // is enough done to mean anything - an estimate off the first file is a
    // guess with a number on it, which is worse than no number.
    char overlay[128];
    const double elapsed = ImGui::GetTime() - job->started_at;
    if (done >= 3.0 && elapsed > 1.0 && total > done) {
      const double remain = (elapsed / done) * (total - done);
      if (remain >= 90.0)
        std::snprintf(overlay, sizeof(overlay), "%s%.0f%%  -  about %.0f min left",
                      step, fraction * 100.0f, remain / 60.0);
      else
        std::snprintf(overlay, sizeof(overlay), "%s%.0f%%  -  about %.0f s left",
                      step, fraction * 100.0f, remain);
    } else {
      std::snprintf(overlay, sizeof(overlay), "%s%.0f%%", step, fraction * 100.0f);
    }
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
    const std::string phase_label = job->progress.PhaseLabel();
    if (!phase_label.empty())
      Muted("%s", phase_label.c_str());
    Muted("%s", job->progress.CurrentFile().c_str());
    // The runner polls this and kills the whole process tree - the Python
    // launcher, the interpreter and the upscaler - within a moment.
    if (job->progress.cancel.load()) {
      Muted("Stopping...");
    } else if (ImGui::Button("Cancel")) {
      job->progress.cancel = true;
    }
    return;
  }

  if (job->thread.joinable()) {
    job->thread.join();
    counts.last = -1.0e9;  // the pack just changed: count it again now
  }

  // How far to enlarge. The warning next to 4x and 8x is the measured cost,
  // not a hedge: replacements are uncompressed, so a 4x pack of this size
  // came to 5.9 GB against a 4 GB maximum cache and thrashed hard enough to
  // produce two-second hitches.
  static const int kScales[] = {2, 4, 8};
  static const char* const kScaleNames[] = {
      "2x  (about 1.5 GB - recommended)",
      "4x  (about 6 GB - needs the 8 GB cache, may still hitch)",
      "8x  (about 24 GB - for future hardware)"};
  int scale_index = 0;
  for (int i = 0; i < IM_ARRAYSIZE(kScales); ++i)
    if (kScales[i] == s.texture_scale) scale_index = i;
  // Plain labels, NOT RowStart: this is outside the table that ended above,
  // and RowStart calls ImGui::TableNextRow, which dereferences the current
  // table without checking. Called with no table open it reads through a
  // null pointer and takes the process with it - which is exactly what it
  // did on the NG2 port, as an access violation in rexruntime.
  ImGui::TextUnformatted("Upscale");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(330.0f);
  if (ImGui::Combo("##texscale", &scale_index, kScaleNames, IM_ARRAYSIZE(kScaleNames))) {
    s.texture_scale = kScales[scale_index];
    changed = true;
  }

  // Live cost, beside the switches that cause it.
  //
  // The texture pack's price is video memory and the copies that go with it,
  // and until this was here the only way to find that out was to play until
  // it stuttered. Toggling the pack with F9 while watching these shows the
  // difference immediately, which is the whole reason they sit in THIS
  // section rather than on a diagnostics page.
  if (s.hud_menu_bars) {
    const fable2::PerfSample p = fable2::GetPerfSample();
    ImGui::TextUnformatted("Live cost");
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(150.0f);
    char cpu_text[32];
    std::snprintf(cpu_text, sizeof(cpu_text), "CPU %.0f%%", p.cpu_percent);
    ImGui::ProgressBar(p.cpu_percent / 100.0f, ImVec2(150.0f, 0.0f), cpu_text);
    ImGui::SameLine();
    char gpu_text[32];
    if (p.gpu_valid)
      std::snprintf(gpu_text, sizeof(gpu_text), "GPU %.0f%%", p.gpu_percent);
    else
      std::snprintf(gpu_text, sizeof(gpu_text), "GPU n/a");
    ImGui::ProgressBar(p.gpu_valid ? p.gpu_percent / 100.0f : 0.0f,
                       ImVec2(150.0f, 0.0f), gpu_text);
    if (p.vram_valid) {
      ImGui::TextUnformatted("Video memory");
      ImGui::SameLine(140.0f);
      char vram_text[64];
      std::snprintf(vram_text, sizeof(vram_text), "%.1f / %.1f GB",
                    p.vram_mb / 1024.0f, p.vram_total_mb / 1024.0f);
      ImGui::ProgressBar(p.vram_total_mb > 0.0f ? p.vram_mb / p.vram_total_mb : 0.0f,
                         ImVec2(306.0f, 0.0f), vram_text);
      Muted("This process only - other applications on the GPU are not counted.");
    }
    ImGui::Spacing();
  }

  // The method, chosen rather than implied. Naming Lanczos makes it visible,
  // where "AI off" reads as though there were no method at all - unticking
  // the old box did not disable enhancement, it selected Lanczos, which is a
  // perfectly good result.
  //
  // Measured on four 512x512 textures: the AI output carries 13-32% more
  // high-frequency detail than the plain resize, at roughly ten times the
  // cost. That is why Lanczos stays first-class rather than a fallback.
  //
  // This port ships the upscaler under tools/upscaler, so the AI entry is
  // normally live; a per-folder copy (Download) is the fallback for an
  // install that lost it.
  const bool ai_ready = fable2::UpscalerInstalled(dir);
  ImGui::TextUnformatted("Method");
  HelpMarker("Lanczos is a high-quality resample and needs nothing extra. "
             "Real-ESRGAN is a trained model that adds detail, and needs a "
             "Vulkan-capable GPU and the upscaler executable.");
  ImGui::SameLine();
  static const char* const kMethodNames[] = {
      "Lanczos  (fast, plain resize)",
      "Real-ESRGAN AI  (slower, adds detail)"};
  int method = s.texture_ai ? 1 : 0;
  ImGui::SetNextItemWidth(330.0f);
  if (ImGui::Combo("##texmethod", &method, kMethodNames, 2)) {
    s.texture_ai = (method == 1);
    changed = true;
  }
  if (s.texture_ai && ai_ready) {
    ImGui::SetNextItemWidth(240.0f);
    changed |= ImGui::SliderFloat("Detail strength", &s.texture_ai_strength,
                                  0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "How much of the model's fine detail is laid over the original.\n"
          "Tone and colour always stay the game's - only the detail is\n"
          "borrowed, so a higher value sharpens rather than repaints.\n"
          "A straight swap would not do that: the model is photo-trained\n"
          "and denoises hard, which on a smoke texture cost 40%% of its\n"
          "brightness.");
    }
  } else if (s.texture_ai && !ai_ready) {
    Muted("The upscaler is missing from tools/upscaler, so a run would fall back "
          "to Lanczos. Download a copy into the texture folder instead:");
    const bool downloading = job->progress.running.load();
    ImGui::BeginDisabled(downloading || job->tools.python.empty() || !have_path);
    if (ImGui::Button("Download AI upscaler (43 MB)")) {
      job->started_at = ImGui::GetTime();
      job->phase_seen = 0;
      job->thread = fable2::DownloadUpscalerAsync(job->tools, dir, job->progress);
    }
    ImGui::EndDisabled();
    if (job->tools.python.empty())
      Muted("Python is needed to download it.");
    else if (!have_path)
      Muted("Set a texture folder first.");
  }
  ImGui::Spacing();

  // What the pack was made with, against what is selected now. A pack made
  // with other settings cannot be topped up - the new textures would not
  // match the old - so a difference forces a full run, and is said here
  // rather than discovered after half an hour.
  const bool want_ai = s.texture_ai && ai_ready;
  const int want_upscaler = want_ai ? 2 : 1;
  const bool settings_differ =
      have_manifest &&
      (pack_scale != s.texture_scale || pack_upscaler != want_upscaler ||
       (want_ai && std::fabs(pack_strength - s.texture_ai_strength) > 0.005f));
  const bool must_redo =
      packed > 0 && (!have_manifest || settings_differ || !pack_complete);
  if (have_manifest) {
    char made[80];
    if (pack_upscaler == 2)
      std::snprintf(made, sizeof(made), "%dx, Real-ESRGAN, detail %.2f",
                    pack_scale, pack_strength);
    else
      std::snprintf(made, sizeof(made), "%dx, Lanczos", pack_scale);
    if (!pack_complete)
      Muted("The pack's last run was stopped halfway (%s) - the next run redoes "
            "every texture.", made);
    else if (settings_differ)
      Muted("The pack was made at %s, which differs from the settings above - "
            "the next run redoes every texture.", made);
    else
      Muted("The pack was made at %s - matches the settings above.", made);
  } else if (packed > 0) {
    Muted("The pack does not say what it was made with (made before 0.0.11) - "
          "the next run redoes every texture and records it.");
  }

  // Only what is missing, unless asked - or forced - to redo. A pack of
  // thousands takes half an hour with the AI; the textures dumped since take
  // minutes, and redoing everything to get them was the only option before.
  bool redo_box = must_redo || job->redo_all;
  ImGui::BeginDisabled(must_redo);
  if (ImGui::Checkbox("Redo textures already in the pack", &redo_box) && !must_redo)
    job->redo_all = redo_box;
  ImGui::EndDisabled();
  HelpMarker("Off: only textures not yet in the pack are processed, which is "
             "quick. On: every texture is done again - use it after changing "
             "the upscale factor, the upscaler or the detail strength. Ticked "
             "and greyed means the pack no longer matches the settings, so the "
             "full run is required.");
  const bool redo = must_redo || job->redo_all;

  const bool nothing_to_do = !redo && waiting == 0;
  const bool can_run =
      have_path && dumped > 0 && !job->tools.python.empty() && !nothing_to_do;
  // The button says how much work it is: "Process 128 waiting textures" is
  // a different decision from "Process all 5,907 textures".
  char run_label[80];
  if (redo)
    std::snprintf(run_label, sizeof(run_label), "Process all %d textures##texrun", dumped);
  else
    std::snprintf(run_label, sizeof(run_label), "Process %d waiting texture%s##texrun",
                  waiting, waiting == 1 ? "" : "s");
  // Exactly what the run will do, stated before the button is pressed.
  if (can_run) {
    char detail[40] = "";
    if (want_ai)
      std::snprintf(detail, sizeof(detail), ", detail %.2f", s.texture_ai_strength);
    Muted("Will process %s at %dx with %s%s.",
          redo ? "every texture that can be enhanced" : "only the waiting textures",
          s.texture_scale, want_ai ? "Real-ESRGAN" : "Lanczos", detail);
  }
  ImGui::BeginDisabled(!can_run);
  if (ImGui::Button(run_label)) {
    job->started_at = ImGui::GetTime();
    job->phase_seen = 0;
    job->thread = fable2::UpscaleTexturesAsync(job->tools, dir, /*upscale=*/true,
                                               s.texture_scale, want_ai,
                                               s.texture_ai_strength,
                                               /*only_missing=*/!redo,
                                               job->progress);
  }
  ImGui::EndDisabled();

  // Say what is missing rather than leaving a dead button. A greyed control
  // with no reason reads as a broken app.
  if (!have_path)
    Muted("Set a folder first.");
  else if (dumped == 0)
    Muted("Nothing dumped yet - turn dumping on and play.");
  else if (job->tools.python.empty())
    Muted("Python was not found, and the upscaler needs it.");
  else if (nothing_to_do)
    Muted("Nothing is waiting - every texture that can be enhanced is in the "
          "pack. Tick Redo to rebuild it.");

  if (job->progress.failed.load())
    Muted("%s", job->progress.Error().c_str());
  else if (job->progress.complete.load())
    Muted("Finished. Tick 'Use the upscaled textures' or press F9 to see it.");
}

// "Copy diagnostics to a file": the log, the settings and what the machine is,
// in one text file, with its path on the clipboard and the folder opened. A
// report needs three files from two folders, and asking for that is asking
// for a report with none of them.
void DrawDiagnosticsButton() {
  SectionHeader("Diagnostics",
                "For a bug report: this session's log, the settings, and what "
                "the machine is, in one text file. It contains no game data.");
  // Held across frames so the result stays readable after the click.
  static std::string status;
  static bool status_ok = false;
  if (ImGui::Button("Copy diagnostics to a file")) {
    const auto result = fable2::WriteDiagnostics(
        Fable2Settings::Path(), rex::filesystem::GetExecutableFolder() / "logs");
    status_ok = result.ok;
    if (result.ok) {
      const bool copied = fable2::CopyToClipboard(result.file.string());
      fable2::RevealInExplorer(result.file);
      status = result.file.filename().string();
      status += copied ? " - written, path copied, folder opened"
                       : " - written, folder opened";
    } else {
      status = result.error;
    }
  }
  if (!status.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(status_ok ? kGood : kBad, "%s", status.c_str());
  }
  Muted("Written to diagnostics\\ beside the game. Have a look before you post "
        "it - it names your folders.");
}

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

    ImGui::BeginDisabled(false);  // editable in game; RestartTag says when it applies
    RowStart("Monitor",
             "Which display to open on. The size beside each one is the "
             "display's own resolution.");
    {
      // Ordered LEFT TO RIGHT by position, not primary-first: the runtime's
      // own display indices are positional, and primary-first disagreed with
      // them for every display that was not the primary.
      const auto monitors = fable2::Monitors();
      if (monitors.empty()) {
        Muted("No displays could be enumerated.");
      } else {
        std::vector<std::string> labels;
        std::vector<const char*> items;
        labels.reserve(monitors.size());
        for (const auto& m : monitors) {
          char buf[64];
          std::snprintf(buf, sizeof(buf), "%d:  %d x %d%s", m.index + 1,
                        m.full_width, m.full_height, m.primary ? "   (main)" : "");
          labels.emplace_back(buf);
        }
        for (const auto& label : labels) {
          items.push_back(label.c_str());
        }
        int monitor_index = s.monitor;
        if (monitor_index < 0 || monitor_index >= int(items.size())) {
          monitor_index = 0;
        }
        if (ImGui::Combo("##monitor", &monitor_index, items.data(),
                         int(items.size()))) {
          s.monitor = monitor_index;
          changed = true;
        }
      }
      if (!live) RestartTag();
    }

    RowStart("Resolution",
             "The window size. The game renders 16:9 whatever the window is, "
             "and is told a 16:9 display of the window's height - so an "
             "ultrawide picture is pillarboxed with Keep aspect ratio on, and "
             "stretched with it off.");
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

    ImGui::BeginDisabled(false);  // editable in game; RestartTag says when it applies
    RowStart("Supersampling",
             "Renders the game's own framebuffer at a multiple of its size and "
             "filters it back down. The sharpest setting here, and by far the "
             "most expensive - the cost is the SQUARE of the number, so 2x is "
             "four times the pixels and 8x is sixty-four. The runtime's own "
             "range is 1-8 and all of it is offered; the high end is there for "
             "a card that can afford it, not as a recommendation.");
    // The cvar range really is 1..8 (draw_resolution_scale_x, read from the
    // live dump). This used to offer 1..3 while Clamp() allowed 1..8, so a
    // config with 4 displayed as "3x" and was silently written back as 3.
    int scale_index = std::clamp(s.resolution_scale - 1, 0, 7);
    const char* scales[] = {"Off", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
    if (ImGui::Combo("##scale", &scale_index, scales, IM_ARRAYSIZE(scales))) {
      s.resolution_scale = scale_index + 1;
      changed = true;
    }
    // Said once, next to the control, rather than left for the player to
    // discover: at the guest's 1280x720 this is 1280*scale by 720*scale.
    if (s.resolution_scale >= 4) {
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 0.35f, 1.0f));
      ImGui::Text("(%dx%d, %dx the pixels)", 1280 * s.resolution_scale,
                  720 * s.resolution_scale,
                  s.resolution_scale * s.resolution_scale);
      ImGui::PopStyleColor();
    }
    if (!live) RestartTag();
    ImGui::EndDisabled();

    ImGui::BeginDisabled(false);  // editable in game; RestartTag says when it applies
    RowStart("Import Xbox 360 saves",
             "Point this at a folder of Xbox 360 Fable II save packages (or at "
             "a single one) and they are imported into the profile on the next "
             "launch, so Continue can see them. Each package is imported once, "
             "into a free save slot, and its version number is adjusted to this "
             "build's - a console save from an updated game is otherwise refused "
             "as \"more up-to-date\". Packages that are not Fable II saves are "
             "skipped silently - a save folder usually holds other things too.");
    {
      char buf[512];
      std::snprintf(buf, sizeof(buf), "%s", s.save_import_path.c_str());
      if (ImGui::InputText("##savepath", buf, sizeof(buf))) {
        s.save_import_path = buf;
        changed = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Browse...##savebrowse")) {
        if (auto picked = PickFolder("Folder holding Xbox 360 Fable II save packages",
                                     s.save_import_path.empty()
                                         ? s.ResolvedGamePath()
                                         : std::filesystem::path(s.save_import_path))) {
          s.save_import_path = picked->string();
          changed = true;
        }
      }
      ImGui::SameLine();
      // Local to the row: it is a transient result line, not state the
      // settings own, and it must survive across frames to stay readable.
      static std::string save_scan_message_;
      if (ImGui::Button("Scan##savescan")) {
        const auto found = fable2::FindSavePackages(s.save_import_path);
        fable2::QueueSaveImports(found);
        save_scan_message_ = found.empty()
                                 ? std::string("No Fable II saves found there.")
                                 : (std::to_string(found.size()) +
                                    " save(s) queued - they import on next launch.");
      }
      if (!save_scan_message_.empty()) {
        ImGui::TextWrapped("%s", save_scan_message_.c_str());
      }
      if (!live) RestartTag();
    }

    RowStart("Skip publisher logos",
             "Starts the game without the Microsoft and Lionhead logo videos, "
             "17 seconds that no button shortens. The game keeps its boot "
             "movies in a list and plays until the end marker; one hook makes "
             "the first entry read as the end marker, and the game takes the "
             "path it already has for an empty list. Off: the logos play as "
             "they did on the console. The video FILES are never hidden: this "
             "game treats a video that fails to open as a bad disc and stops.");
    {
      if (ImGui::Checkbox("##skiplogos", &s.skip_logos)) changed = true;
      if (!live) RestartTag();
    }

    RowStart("Skip intro videos",
             "Presses A through the cinematic after a chapter load, by adding a "
             "synthetic controller whose presses are merged with your own; ANY "
             "genuine input disarms it at once, so a cinematic you want to "
             "watch is one stick nudge away from being left alone.");
    {
      if (ImGui::Checkbox("##skipintro", &s.skip_intro)) changed = true;
      if (!live) RestartTag();
    }

    RowStart("On-screen readouts",
             "Frame rate, CPU, GPU and video memory in the corner, with a bar "
             "under the CPU, GPU and VRAM numbers. Shown at every launch; F8 "
             "hides them for the session only and is not remembered.\n\n"
             "CPU is this process across all cores, with the same figure in "
             "cores beside it: a game thread flat out on one core reads as "
             "a small percentage of a big machine, and the core count says so.\n\n"
             "FPS is the GAME's own rate - frames it finished - with the host's "
             "present rate beside it, smaller. The two differ: the window "
             "repaints far more often than the game draws, and the big number "
             "is the one that says whether the game is keeping up.\n\n"
             "Video memory is this process only; other applications on the GPU "
             "are not counted.");
    {
      changed |= ImGui::Checkbox("##hud", &s.hud_enabled);
      if (s.hud_enabled) {
        ImGui::SameLine();
        changed |= ImGui::Checkbox("fps##hudfps", &s.hud_fps);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("CPU##hudcpu", &s.hud_cpu);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("GPU##hudgpu", &s.hud_gpu);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("VRAM##hudvram", &s.hud_vram);
        // Second line: the cell clips whatever sits past VRAM on the first
        // (seen in play, 2026-09-12).
        changed |= ImGui::Checkbox("CPU bar##hudcpubar", &s.hud_cpu_bar);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("GPU bar##hudgpubar", &s.hud_gpu_bar);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("VRAM bar##hudvrambar", &s.hud_vram_bar);
      }
    }

    RowStart("Accurate depth",
             "Converts depth to the Xbox 360's float24 format exactly, in the "
             "pixel shader, instead of approximating it. Costs shader work and "
             "buys depth precision - the thing it fixes is z-fighting and "
             "shadow acne on distant geometry.");
    {
      if (ImGui::Checkbox("##accdepth", &s.accurate_depth)) changed = true;
      if (!live) RestartTag();
    }

    RowStart("Fuzzy alpha test",
             "The plugin's own workaround for alpha-test flicker, where a pixel "
             "right on the test threshold flips between frames. Approximates the "
             "comparison with a small epsilon.");
    {
      if (ImGui::Checkbox("##fuzzyalpha", &s.fuzzy_alpha)) changed = true;
      if (!live) RestartTag();
    }

    RowStart("Texture cache",
             "How much host memory the GPU plugin may hold textures in. "
             "Leave at Default to use the plugin's own limits - raising it "
             "helps a large texture pack stay resident instead of being "
             "evicted and re-uploaded.");
    {
      static const int kMb[] = {0, 512, 1024, 2048, 4096, 8192};
      // 8 GB is the plugin's ceiling (soft 4096 / hard 8192) and it is what a
      // 4x pack needs: replacements are uncompressed, so a 4x texture costs
      // about 128x a DXT1 original.
      const char* kNames[] = {"Default", "512 MB", "1 GB", "2 GB", "4 GB", "8 GB"};
      int idx = 0;
      for (int i = 0; i < 6; ++i)
        if (kMb[i] == s.texture_cache_mb) idx = i;
      if (ImGui::Combo("##texcache", &idx, kNames, 6)) {
        s.texture_cache_mb = kMb[idx];
        changed = true;
      }
      if (!live) RestartTag();
    }

    RowStart("NaN constant repair",
             "A DIAGNOSTIC, and it should stay Off. It substitutes for NaN in "
             "the vertex shader constants before they reach the GPU. That used "
             "to be needed: the 3D scene drew as flat blue while the UI kept "
             "drawing on top. The actual cause was in the recompiler - VMX128 "
             "registers v64-v127 became zero-initialised locals, so a function "
             "handed a value in one read zero - and with that fixed the blue is "
             "gone. Turning this on NOW mostly overwrites good values and takes "
             "the scene to black. It is kept because it is the measurement that "
             "proved the NaN was the cause.");
    {
      int repair_index = std::clamp(s.nan_constant_repair, 0, 2);
      const char* repair_names[] = {"Off (recommended)", "Zero", "Identity row"};
      if (ImGui::Combo("##nanrepair", &repair_index, repair_names, 3)) {
        s.nan_constant_repair = repair_index;
        changed = true;
      }
      if (!live) RestartTag();
    }

    RowStart("Upscaling",
             "How the game's picture is scaled up to fill the window. Fable II "
             "renders at 1120x720 (1280x720 with the resolution patch), so on "
             "a modern display it is ALWAYS being upscaled - this only chooses "
             "by what. FSR 1.0 reconstructs edges and then sharpens, and is "
             "the one to want at 1080p or 4K. CAS sharpens without the "
             "upscaling stage, which suits a window near the game's own size. "
             "Bilinear is the stock filter and the softest.");
    {
      const auto up_values = UpscaleValues(s.present_effect);
      int up_index = 0;
      for (size_t i = 0; i < up_values.size(); ++i)
        if (up_values[i] == s.present_effect) up_index = static_cast<int>(i);
      std::vector<const char*> up_names;
      for (const auto& v : up_values) up_names.push_back(UpscaleLabel(v));
      if (ImGui::Combo("##upscale", &up_index, up_names.data(),
                       static_cast<int>(up_names.size()))) {
        s.present_effect = up_values[up_index];
        changed = true;
      }
      if (!live) RestartTag();

      // Only one filter has a knob at a time, and neither knob means anything
      // for bilinear, so the row appears and disappears with the choice rather
      // than sitting there greyed out.
      if (s.present_effect == "fsr") {
        RowStart("FSR sharpness",
                 "How hard FSR's RCAS pass sharpens after upscaling. The "
                 "runtime's own cvar is a sharpness REDUCTION in stops, where "
                 "lower means sharper; this slider is inverted so that right "
                 "is sharper, which is the way round a person expects.");
        // Stored as the reduction the cvar wants; shown as sharpness.
        float sharpness = 2.0f - static_cast<float>(s.fsr_sharpness);
        if (ImGui::SliderFloat("##fsrsharp", &sharpness, 0.0f, 2.0f, "%.2f")) {
          s.fsr_sharpness = 2.0 - static_cast<double>(sharpness);
          changed = true;
        }
        if (!live) RestartTag();
      } else if (s.present_effect == "cas") {
        RowStart("CAS sharpness",
                 "Extra contrast-adaptive sharpening on top of the default. "
                 "0 is the runtime's own default; higher is sharper.");
        float sharpness = static_cast<float>(s.cas_sharpness);
        if (ImGui::SliderFloat("##cassharp", &sharpness, 0.0f, 1.0f, "%.2f")) {
          s.cas_sharpness = static_cast<double>(sharpness);
          changed = true;
        }
        if (!live) RestartTag();
      }
    }
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

    ImGui::BeginDisabled(false);  // editable in game; RestartTag says when it applies
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

    ImGui::BeginDisabled(false);  // editable in game; RestartTag says when it applies
    RowStart("Graphics engine",
             "Which graphics API the game renders through. DirectX 12 is the "
             "default because it is measurably better here: on Vulkan the "
             "scene still turns blue AND character meshes stop drawing "
             "altogether. Vulkan needs a plugin built from source with it "
             "enabled - the SDK's stock Windows plugin is DirectX 12 only, "
             "and the app logs the miss and falls back rather than failing.");
    {
      const char* backends[] = {"Vulkan", "DirectX 12"};
      int idx = s.gpu_backend == "d3d12" ? 1 : 0;
      if (ImGui::Combo("##gpubackend", &idx, backends, 2)) {
        s.gpu_backend = idx == 1 ? "d3d12" : "vulkan";
        changed = true;
      }
    }
    if (!live) RestartTag();

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
  DrawTexturesSection(s, opts, changed);

  SectionHeader("Community patches",
                "From Xenia Canary's patch file for Fable II (Margen67, Guy). "
                "These change the game's own behaviour, so they are off by "
                "default. All of them are applied at startup.");
  TightRows tight_patches;
  if (ImGui::BeginTable("patches", 2, kRowTableFlags)) {
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
    ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthFixed, kControlWidth);

    ImGui::BeginDisabled(false);  // editable in game; RestartTag says when it applies

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
  // The pack path, so F9 and the checkbox both take effect without a
  // restart. Empty string means "use the game's own textures", which is
  // how switching it OFF is expressed - the plugin reloads either way.
  // Dumping, live as well (the plugin's dump cvars are hot-reloadable since the
  // content-hash change, 2026-09-11). Ticking "dump" turns the pack off, and
  // that path change drops every texture, so the scene in front of the player
  // is dumped there and then instead of at the next launch. Set before the
  // pack path so the reload it triggers already sees dump=on.
  const bool dump_on = s.texture_dump && !s.texture_path.empty();
  SetCvar("texture_dump", dump_on ? "true" : "false");
  SetCvar("texture_dump_path", dump_on ? (s.texture_path + "/dump") : std::string());
  SetCvar("texture_pack_path",
          (s.texture_pack && !s.texture_path.empty()) ? (s.texture_path + "/pack")
                                                     : std::string());
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
    // The window hides the pointer only while its visibility is in the
    // auto-hide MODE; setting the delay and leaving the mode at "always
    // visible" made the delay dead configuration - the pointer never hid.
    // So the mode is switched with the delay: auto-hidden when a delay is
    // chosen, plain visible for 0 (which the window would otherwise read
    // as a zero delay meaning "immediately").
    if (s.cursor_hide_seconds > 0) {
      window->SetCursorAutoHideDelayMs(
          static_cast<uint32_t>(s.cursor_hide_seconds) * 1000u);
      window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kAutoHidden);
    } else {
      window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
    }
  }
}

// --- SetupScreen -----------------------------------------------------------

SetupScreen::SetupScreen(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
                         std::function<void(bool)> on_done,
                         std::function<void()> on_advanced, TextureJob* tex_job)
    : ImGuiDialog(drawer),
      settings_(settings),
      on_done_(std::move(on_done)),
      on_advanced_(std::move(on_advanced)),
      tex_job_(tex_job) {
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
    {
      PageOptions setup_opts;
      setup_opts.tex_job = tex_job_;
      DrawSettings(*settings_, setup_opts);
    }

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

  // Xbox 360 saves, here as well as in the settings: a player with a save
  // from the console wants it in before the first launch, not after a menu
  // they have not seen yet. The folder is remembered in the settings; the
  // packages are imported at launch, each once, into a free slot, with the
  // save's version number adjusted to this build's - see fable2_saveimport.
  {
    ImGui::Spacing();
    ImGui::TextUnformatted("Import Xbox 360 saves (optional)");
    PathField("##setupsavepath", settings_->save_import_path);
    if (ImGui::Button("Choose save folder...")) {
      if (auto picked = PickFolder("Folder holding Xbox 360 Fable II save packages",
                                   settings_->save_import_path.empty()
                                       ? install_dest_
                                       : std::filesystem::path(settings_->save_import_path))) {
        settings_->save_import_path = picked->string();
        const auto found = fable2::FindSavePackages(settings_->save_import_path);
        fable2::QueueSaveImports(found);
        setup_save_message_ = found.empty()
                                  ? std::string("No Fable II saves found there.")
                                  : (std::to_string(found.size()) +
                                     " save(s) found - imported when the game starts, "
                                     "each once, into a free slot.");
      }
    }
    if (!settings_->save_import_path.empty()) {
      ImGui::SameLine();
      if (ImGui::Button("Clear##setupsave")) {
        settings_->save_import_path.clear();
        fable2::QueueSaveImports({});
        setup_save_message_.clear();
      }
    }
    if (!setup_save_message_.empty()) {
      ImGui::TextWrapped("%s", setup_save_message_.c_str());
    }
  }

  // The title update. Saves from a console need the game at the version the
  // console had, and this build was compiled from the disc's executable; the
  // section says which version that is, which update the disc takes, and
  // whether a file at hand is that update. A build compiled with the update
  // is what loads those saves - see fable2_titleupdate.h.
  {
    static fable2::TitleUpdateStatus tu;
    static double tu_read_at = -1.0e9;
    static std::string tu_message;
    const double now = ImGui::GetTime();
    if (now - tu_read_at > 5.0) {
      tu = fable2::InspectTitleUpdate(install_dest_.empty() ? settings_->ResolvedGamePath()
                                                            : install_dest_);
      tu_read_at = now;
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Title update");
    if (!tu.xex_ok) {
      ImGui::TextWrapped("Executable not inspected: %s", tu.xex_error.c_str());
    } else {
      ImGui::TextWrapped(
          "This build was compiled from game version %s (media ID %08X). Saves made on "
          "a console need the disc's title update - version %s for this pressing - and "
          "a build compiled with it.",
          fable2::VersionText(tu.version).c_str(), tu.media_id,
          tu.patch_found && tu.patch_matches
              ? fable2::VersionText(tu.patch_target_version).c_str()
              : "the next one up");
      if (tu.compiled_with_patch) {
        ImGui::TextWrapped("This build WAS compiled with the title update applied.");
      } else if (tu.patch_found) {
        ImGui::TextWrapped("Title update file: %s - %s.%s", tu.patch_path.string().c_str(),
                           tu.patch_note.c_str(),
                           tu.patch_matches ? " This build was compiled without it; it is "
                                              "staged for the build that will be."
                                            : "");
      } else {
        ImGui::TextWrapped("No title update file found. Choose the disc's title update "
                           "(a LIVE package, or its default.xexp) to stage it.");
      }
      if (ImGui::Button("Choose title update file...")) {
        if (auto picked = PickFile("The disc's title update (LIVE package or default.xexp)",
                                   {{"Title update", "*.xexp;*.*"}},
                                   settings_->ResolvedGamePath())) {
          fable2::ChooseTitleUpdateFile(*picked, tu_message);
          tu_read_at = -1.0e9;  // re-read on the next frame
        }
      }
      if (!tu_message.empty()) ImGui::TextWrapped("%s", tu_message.c_str());
    }
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

  DrawDiagnosticsButton();
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
                                 std::function<void()> on_advanced,
                                 TextureJob* tex_job)
    : ImGuiDialog(drawer),
      settings_(settings),
      window_(window),
      on_advanced_(std::move(on_advanced)),
      tex_job_(tex_job) {}

SettingsOverlay::~SettingsOverlay() {
  // Deliberately does NOT touch the texture run: it is owned by the App
  // (tex_job_), not by this overlay, so closing the settings menu leaves an
  // upscale running. Only an explicit Cancel or the app exiting stops it -
  // which is the whole point of moving it out of here.
}

void SettingsOverlay::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowSize(ImVec2(680, 620), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  // Before Begin, so the title bar is drawn in the menu's font too. Pushed
  // after it, the window keeps the SDK's 13px bitmap face and the title reads
  // as a different application from its own contents.
  PushMenuFont(Fonts().body, Fonts().body_size);
  // Begin's result matters: false means the window is collapsed or not
  // visible this frame, and everything drawn into it is skipped - so a table
  // begun inside it never opens, and a RowStart would read a null table. End
  // is still owed either way.
  const bool visible = ImGui::Begin("Fable II - Settings", nullptr,
                                    ImGuiWindowFlags_NoSavedSettings);
  if (visible) {
    PageOptions opts;
    opts.restart_bound_editable = false;
    opts.tex_job = tex_job_;

    bool changed = false;
    // Same rule for the child: draw its contents only when it is open;
    // EndChild is owed regardless.
    if (ImGui::BeginChild("body", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f))) {
      changed |= DrawSettings(*settings_, opts);

      SectionHeader("Content");
      PathField("##gamepath", settings_->ResolvedGamePath().string());
      ImGui::Spacing();
      Muted("The game folder is chosen on the setup screen, which runs before the "
            "game is loaded. Hold Shift while launching to get it back.");
      DrawDiagnosticsButton();
    }
    ImGui::EndChild();

    if (changed) {
      // Everything the presenter re-reads takes effect on the next paint. The
      // rest is written to disk and waits for the next launch, which is what
      // the greyed rows above are telling the player.
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
  }

  ImGui::End();
  ImGui::PopFont();
}

}  // namespace fable2
