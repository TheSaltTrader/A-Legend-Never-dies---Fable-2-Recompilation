// fable2 - installing Xbox 360 content packages (DLC)
//
// IMPORTANT, BEFORE YOU USE THIS: the two Fable II expansions do NOT need it.
// Knothole Island and See the Future are already on the Game of the Year disc,
// verified rather than assumed - see README.md, "Is the DLC already on the
// disc?". Installing the standalone packages for them costs about a gigabyte
// each and adds nothing.
//
// What it is for is any package that genuinely is not on the disc, such as the
// 12 KB "Collectors' Edition Content" token, and for anything that turns up
// later. Point --dlc_root at a folder of packages; each one is inspected and
// installed once.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rex::system::xam {
class ContentManager;
}

namespace fable2 {

// What an STFS package says about itself. Read straight out of the header,
// because DLC arrives as opaque 40-hex-character filenames.
struct PackageInfo {
  std::filesystem::path path;
  std::string magic;            // "CON ", "LIVE" or "PIRS"
  uint32_t title_id = 0;
  uint32_t content_type = 0;
  uint64_t content_size = 0;
  std::string display_name;     // UTF-8, from the header's UTF-16BE field
};

// Reads the STFS header. Returns false if the file is not a content package.
bool ReadPackageInfo(const std::filesystem::path& path, PackageInfo& out);

// Every package in `dir` (non-recursive), in filename order.
std::vector<PackageInfo> ScanPackages(const std::filesystem::path& dir);

// Installs the packages in `dir` that belong to `title_id` and are not already
// installed. Returns the number newly installed. Skips - loudly - anything
// belonging to another title, so a mixed folder cannot contaminate this game.
int InstallPackages(rex::system::xam::ContentManager* content_manager,
                    const std::filesystem::path& dir, uint32_t title_id);

}  // namespace fable2
