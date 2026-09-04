// fable2 - installing Xbox 360 content packages (DLC). See fable2_dlc.h.

#include "fable2_dlc.h"

#include <rex/logging.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>

namespace fable2 {

namespace {

// Offsets into an STFS header, past the 0x228-byte signature block.
constexpr size_t kHeaderRead = 0x1800;
constexpr size_t kOffContentType = 0x344;
constexpr size_t kOffContentSize = 0x34C;
constexpr size_t kOffTitleId = 0x360;
constexpr size_t kOffDisplayName = 0x411;

uint32_t ReadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint64_t ReadBE64(const uint8_t* p) {
  return (uint64_t(ReadBE32(p)) << 32) | ReadBE32(p + 4);
}

// The display name is UTF-16 big-endian. Only the ASCII range matters for
// identifying a package in a log line, so anything else becomes '?' rather
// than dragging in a converter.
std::string DisplayName(const uint8_t* p, size_t max_chars) {
  std::string out;
  for (size_t i = 0; i < max_chars; ++i) {
    uint16_t ch = (uint16_t(p[i * 2]) << 8) | p[i * 2 + 1];
    if (!ch) break;
    out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

}  // namespace

bool ReadPackageInfo(const std::filesystem::path& path, PackageInfo& out) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return false;

  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::vector<uint8_t> head(kHeaderRead);
  file.read(reinterpret_cast<char*>(head.data()), kHeaderRead);
  if (static_cast<size_t>(file.gcount()) < kHeaderRead) return false;

  const std::string magic(reinterpret_cast<const char*>(head.data()), 4);
  if (magic != "CON " && magic != "LIVE" && magic != "PIRS") return false;

  out.path = path;
  out.magic = magic;
  out.content_type = ReadBE32(head.data() + kOffContentType);
  out.content_size = ReadBE64(head.data() + kOffContentSize);
  out.title_id = ReadBE32(head.data() + kOffTitleId);
  out.display_name = DisplayName(head.data() + kOffDisplayName, 128);
  return true;
}

std::vector<PackageInfo> ScanPackages(const std::filesystem::path& dir) {
  std::vector<PackageInfo> found;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return found;

  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file(ec)) files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());

  for (const auto& path : files) {
    PackageInfo info;
    if (ReadPackageInfo(path, info)) found.push_back(std::move(info));
  }
  return found;
}

int InstallPackages(rex::system::xam::ContentManager* content_manager,
                    const std::filesystem::path& dir, uint32_t title_id) {
  if (!content_manager) {
    REXLOG_WARN("fable2: no content manager; not installing DLC");
    return 0;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    REXLOG_INFO("fable2: DLC folder '{}' does not exist; nothing to install",
                dir.string());
    return 0;
  }

  // What the runtime already has. InstallContent extracts a package into a
  // directory named after the package file, so matching on file name is how
  // a second launch avoids re-extracting a gigabyte.
  std::set<std::string> installed;
  for (const auto& content : content_manager->ListContent(
           1, 0, rex::system::XContentType::kMarketplaceContent, title_id)) {
    installed.insert(content.file_name());
  }

  auto packages = ScanPackages(dir);
  REXLOG_INFO("fable2: {} content package(s) in '{}', {} already installed",
              packages.size(), dir.string(), installed.size());

  // Two copies of the same expansion, differing only in their signature
  // bytes, is a normal thing to find in a DLC folder. Installing both wastes
  // a gigabyte and leaves a duplicate in the game's own DLC list.
  std::set<std::string> seen_names;
  int count = 0;

  for (const auto& pkg : packages) {
    const std::string name = pkg.path.filename().string();
    const auto label = pkg.display_name.empty() ? name : pkg.display_name;

    if (pkg.title_id != title_id) {
      REXLOG_WARN("fable2: skipping '{}' ({}): title {:08X}, not {:08X}",
                  name, label, pkg.title_id, title_id);
      continue;
    }
    if (pkg.content_type !=
        static_cast<uint32_t>(rex::system::XContentType::kMarketplaceContent)) {
      REXLOG_INFO("fable2: skipping '{}' ({}): content type {:08X} is not DLC",
                  name, label, pkg.content_type);
      continue;
    }
    // Record the name BEFORE the already-installed check, not after. Getting
    // this the wrong way round means a package that is already installed
    // returns early without registering its name, so a second copy of it
    // sails through the duplicate check and installs anyway - which is how a
    // spare copy of a 557 MB expansion gets extracted twice.
    if (!pkg.display_name.empty() && !seen_names.insert(pkg.display_name).second) {
      REXLOG_INFO("fable2: skipping '{}': duplicate of '{}', already handled "
                  "this pass", name, label);
      continue;
    }
    if (installed.count(name)) {
      REXLOG_INFO("fable2: '{}' already installed", label);
      continue;
    }

    REXLOG_INFO("fable2: installing '{}' ({}, {:.1f} MB) - this extracts the "
                "package and can take a while",
                label, name, pkg.content_size / (1024.0 * 1024.0));
    const auto result = content_manager->InstallContent(pkg.path);
    if (result != 0) {
      REXLOG_ERROR("fable2: installing '{}' failed: {:08X}", label, result);
      continue;
    }
    REXLOG_INFO("fable2: installed '{}'", label);
    ++count;
  }

  if (count) {
    REXLOG_INFO("fable2: {} package(s) newly installed", count);
  }
  return count;
}

}  // namespace fable2
