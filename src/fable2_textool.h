// Running the texture tools from the settings screen.
//
// The upscaler is a Python script, not library code, and that is deliberate:
// it decodes formats the plugin never has to, it can be re-run offline against
// a dump collected weeks ago, and Real-ESRGAN arrives as a separate executable.
// So the app SPAWNS it and reads its progress rather than reimplementing it.
//
// Both scripts already print machine-readable progress - upscale_textures.py
// emits "PROGRESS done total name" per texture and a final "DONE ..." line -
// so nothing had to be added to them for this.
//
// This mirrors the ISO extractor's shape (ExtractProgress + a worker thread)
// because the settings screen already knows how to draw that.

#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace fable2 {

struct ToolProgress {
  std::atomic<uint64_t> done{0};
  std::atomic<uint64_t> total{0};
  std::atomic<bool> running{false};
  std::atomic<bool> complete{false};
  std::atomic<bool> failed{false};
  std::atomic<bool> cancel{false};

  std::mutex text_mutex;
  std::string current;   // what it is working on right now
  std::string summary;   // the tool's own last word, shown when it finishes

  void SetCurrent(const std::string& s) {
    std::lock_guard lock(text_mutex);
    current = s;
  }
  std::string Current() {
    std::lock_guard lock(text_mutex);
    return current;
  }
  void SetSummary(const std::string& s) {
    std::lock_guard lock(text_mutex);
    summary = s;
  }
  std::string Summary() {
    std::lock_guard lock(text_mutex);
    return summary;
  }
};

// How many textures are on disk. Shown beside the switches so the state
// is visible without opening a file manager - "0 in the pack" is the
// answer to most of why the pack appears to do nothing.
int CountDumped(const std::filesystem::path& texture_dir);
int CountPacked(const std::filesystem::path& texture_dir);

// Is the Real-ESRGAN executable present? The AI option stays disabled until it
// is, so the control is never present-but-dead.
bool UpscalerInstalled();

// Where it lives, for showing in the UI.
std::filesystem::path UpscalerPath();

// Fetch Real-ESRGAN (~43 MB). Off by default and not shipped with the port.
std::thread DownloadUpscalerAsync(ToolProgress& progress);

// Build the pack from an existing dump. `scale` is 1, 2 or 4; `ai` uses
// Real-ESRGAN with `ai_strength` of its detail laid over a plain resize.
std::thread BuildPackAsync(const std::filesystem::path& texture_dir, int scale,
                           bool ai, float ai_strength, ToolProgress& progress);

}  // namespace fable2
