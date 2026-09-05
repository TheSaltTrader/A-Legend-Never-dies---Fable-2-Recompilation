#include "fable2_textool.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace fable2 {
namespace {

// Where the scripts live. The port is run from its build folder during
// development and from an install folder afterwards, so both are tried rather
// than assuming one - a missing script must say so, not fail silently.
std::filesystem::path ToolsDir() {
  const auto exe = rex::filesystem::GetExecutableFolder();
  for (const auto& candidate : {exe / "tools",
                                exe.parent_path().parent_path().parent_path() / "tools"}) {
    std::error_code ec;
    if (std::filesystem::is_directory(candidate, ec)) {
      return candidate;
    }
  }
  return exe / "tools";
}

// Quoted because every one of these paths can contain a space, and this one
// reliably does: "Fable 2 Recompile Xbox".
std::string Quote(const std::filesystem::path& p) {
  return "\"" + p.string() + "\"";
}

// Run a command and feed each output line to `on_line`. _popen is enough here:
// the tools are chatty on stdout, short-lived, and take no input.
//
// 2>&1 is deliberate - a Python traceback goes to stderr, and losing it would
// turn "the upscaler failed" into "the upscaler did nothing".
bool RunCapturing(const std::string& command, ToolProgress& progress,
                  const std::function<void(const std::string&)>& on_line) {
  const std::string full = "cmd /c \"" + command + " 2>&1\"";
  FILE* pipe = _popen(full.c_str(), "r");
  if (pipe == nullptr) {
    progress.SetSummary("Could not start the tool.");
    return false;
  }
  std::array<char, 1024> buffer{};
  std::string last_line;
  while (std::fgets(buffer.data(), int(buffer.size()), pipe) != nullptr) {
    if (progress.cancel.load()) {
      break;
    }
    std::string line(buffer.data());
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    last_line = line;
    on_line(line);
  }
  const int rc = _pclose(pipe);
  if (progress.cancel.load()) {
    progress.SetSummary("Cancelled.");
    return false;
  }
  if (rc != 0) {
    // The tool's own last words are far more useful than the exit code.
    progress.SetSummary(last_line.empty() ? "The tool failed." : last_line);
    return false;
  }
  return true;
}

}  // namespace

namespace {

int CountWithExtension(const std::filesystem::path& dir, const char* ext) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return 0;
  }
  int n = 0;
  for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }
    if (e.is_regular_file(ec) && e.path().extension() == ext) {
      ++n;
    }
  }
  return n;
}

}  // namespace

int CountDumped(const std::filesystem::path& texture_dir) {
  return CountWithExtension(texture_dir / "dump", ".bin");
}

int CountPacked(const std::filesystem::path& texture_dir) {
  return CountWithExtension(texture_dir / "pack", ".tex");
}

std::filesystem::path UpscalerPath() {
  return ToolsDir() / "realesrgan" / "realesrgan-ncnn-vulkan.exe";
}

bool UpscalerInstalled() {
  std::error_code ec;
  return std::filesystem::is_regular_file(UpscalerPath(), ec);
}

std::thread DownloadUpscalerAsync(ToolProgress& progress) {
  progress.running = true;
  progress.complete = false;
  progress.failed = false;
  progress.cancel = false;
  progress.done = 0;
  progress.total = 0;
  progress.SetCurrent("Contacting GitHub...");
  progress.SetSummary("");

  return std::thread([&progress] {
    const auto script = ToolsDir() / "get_upscaler.py";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script, ec)) {
      progress.SetSummary("tools/get_upscaler.py is missing.");
      progress.failed = true;
      progress.running = false;
      return;
    }
    const bool ok = RunCapturing("python " + Quote(script), progress,
                                 [&progress](const std::string& line) {
                                   progress.SetCurrent(line);
                                 });
    if (ok) {
      progress.SetSummary(UpscalerInstalled()
                              ? "Real-ESRGAN installed."
                              : "Finished, but the executable is still not there.");
      progress.failed = !UpscalerInstalled();
    } else {
      progress.failed = true;
    }
    progress.complete = true;
    progress.running = false;
  });
}

std::thread BuildPackAsync(const std::filesystem::path& texture_dir, int scale,
                           bool ai, float ai_strength, ToolProgress& progress) {
  progress.running = true;
  progress.complete = false;
  progress.failed = false;
  progress.cancel = false;
  progress.done = 0;
  progress.total = 0;
  progress.SetCurrent("Starting...");
  progress.SetSummary("");

  return std::thread([texture_dir, scale, ai, ai_strength, &progress] {
    const auto script = ToolsDir() / "upscale_textures.py";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script, ec)) {
      progress.SetSummary("tools/upscale_textures.py is missing.");
      progress.failed = true;
      progress.running = false;
      return;
    }
    if (!std::filesystem::is_directory(texture_dir / "dump", ec)) {
      progress.SetSummary("No dump/ folder there - turn on texture dumping and play first.");
      progress.failed = true;
      progress.running = false;
      return;
    }

    std::ostringstream cmd;
    cmd << "python " << Quote(script) << " --dir " << Quote(texture_dir)
        << " --upscale --scale " << scale;
    if (ai) {
      cmd << " --ai --ai-strength " << ai_strength;
    }

    const bool ok = RunCapturing(
        cmd.str(), progress, [&progress](const std::string& line) {
          // "PROGRESS done total name" - the tool's own protocol.
          if (line.rfind("PROGRESS ", 0) == 0) {
            std::istringstream in(line.substr(9));
            uint64_t done = 0, total = 0;
            std::string name;
            in >> done >> total >> name;
            progress.done = done;
            progress.total = total;
            progress.SetCurrent(name);
          } else if (line.rfind("DONE ", 0) == 0) {
            progress.SetSummary(line.substr(5));
          } else if (line.rfind("  not packed", 0) == 0) {
            // Kept: the reasons are the interesting part of a pack that came
            // out smaller than expected.
            REXLOG_INFO("Texture pack: {}", line);
          }
        });
    progress.failed = !ok;
    if (ok && progress.Summary().empty()) {
      progress.SetSummary("Finished.");
    }
    progress.complete = true;
    progress.running = false;
  });
}

}  // namespace fable2
