#include "fable2_stage.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/kernel/xboxkrnl/io.h>
#include <rex/logging.h>

namespace fs = std::filesystem;

namespace fable2 {
namespace {

std::mutex g_mutex;
std::map<std::string, int> g_ids;   // region name -> stage id, 1-based
std::vector<std::string> g_names;   // stage id - 1 -> region name
std::atomic<int> g_current{0};
std::atomic<unsigned> g_open_count{0};

std::string Lower(std::string s) {
  for (auto& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// The region name out of a path, or "" if the path is not a region bank.
//
// Two bank families name a region: region_specific_<name>.bnk is the primary
// signal (sounds for that region only), atmos_<name>.bnk the ambience, which
// some regions share (the caves and tombs use generic ones). Only the first
// is used to publish a stage; the second is logged so the pattern can be read
// off a run. Separators are the game's: a backslash path with lowercase
// names, but the comparison is case-blind because nothing here is worth
// getting wrong over a capital letter.
std::string RegionFromPath(const std::string& lowered) {
  const size_t slash = lowered.find_last_of("\\/");
  const std::string file = slash == std::string::npos ? lowered : lowered.substr(slash + 1);
  static const std::string kPrefix = "region_specific_";
  static const std::string kSuffix = ".bnk";
  if (file.size() <= kPrefix.size() + kSuffix.size())
    return {};
  if (file.compare(0, kPrefix.size(), kPrefix) != 0)
    return {};
  if (file.compare(file.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
    return {};
  return file.substr(kPrefix.size(), file.size() - kPrefix.size() - kSuffix.size());
}

void OnFileOpen(const std::string& path, bool opened) {
  const unsigned n = ++g_open_count;
  // Every open, at debug: this is the record that says whether region banks
  // are opened per region or all at boot, and it is cheap to keep.
  REXLOG_DEBUG("[stage] open #{}: {}{}", n, path, opened ? "" : " (failed)");
  if (!opened)
    return;
  const std::string lowered = Lower(path);
  const std::string region = RegionFromPath(lowered);
  if (region.empty())
    return;

  int id = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_ids.find(region);
    if (it == g_ids.end()) {
      // A bank the disc walk did not see - a DLC region, or a name that
      // arrived through a different path. Give it the next id rather than
      // dropping it: an unknown region warming under a fresh id is strictly
      // better than one that never warms.
      g_names.push_back(region);
      id = int(g_names.size());
      g_ids.emplace(region, id);
    } else {
      id = it->second;
    }
  }
  const int previous = g_current.exchange(id);
  if (previous == id)
    return;
  REXLOG_INFO("[stage] region '{}' is loading -> stage {} (was {})", region, id, previous);
  // Published by name: the cvar is the GPU plugin's, which is not on this
  // executable's link line. It is hot-reloadable, so the plugin sees the
  // change on its next submission and starts warming that stage's list.
  if (rex::cvar::GetFlagInfo("texture_pack_chapter"))
    rex::cvar::SetFlagByName("texture_pack_chapter", std::to_string(id));
}

}  // namespace

void InstallStageObserver(const fs::path& game_root) {
  std::vector<std::string> names;
  std::error_code ec;
  const fs::path audio = game_root / "data" / "audio";
  if (fs::is_directory(audio, ec)) {
    for (fs::directory_iterator it(audio, ec), end; it != end; it.increment(ec)) {
      if (ec)
        break;
      if (!it->is_regular_file(ec))
        continue;
      const std::string region = RegionFromPath(Lower(it->path().filename().string()));
      if (!region.empty())
        names.push_back(region);
    }
  }
  // Sorted, so the ids are the same on every launch and on every install of
  // the same disc: the stage lists the plugin writes (pack/stages/chNN.txt)
  // are named by these numbers and must keep meaning the same region.
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_names = names;
    g_ids.clear();
    for (size_t i = 0; i < g_names.size(); ++i)
      g_ids.emplace(g_names[i], int(i + 1));
  }
  if (names.empty()) {
    REXLOG_WARN("[stage] no region banks under {} - per-region texture warming "
                "will not engage", audio.string());
  } else {
    REXLOG_INFO("[stage] {} regions numbered from {} (1 = '{}', {} = '{}')", names.size(),
                audio.string(), names.front(), names.size(), names.back());
  }
  rex::kernel::xboxkrnl::SetFileOpenObserver(OnFileOpen);
}

int CurrentStage() { return g_current.load(std::memory_order_relaxed); }

std::string CurrentStageName() {
  const int id = CurrentStage();
  std::lock_guard<std::mutex> lock(g_mutex);
  if (id <= 0 || size_t(id) > g_names.size())
    return {};
  return g_names[size_t(id) - 1];
}

}  // namespace fable2
