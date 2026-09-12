#include "fable2_saveimport.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xam/user_profile.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace fable2 {
namespace {

constexpr uint32_t kTitleId = 0x4D5307F1;

// Offsets into the STFS metadata block. These match tools/stfs_info.py, which
// is what read this project's packages correctly in the first place.
constexpr size_t kContentTypeOffset = 0x344;
constexpr size_t kTitleIdOffset = 0x360;

// The SaveInfo VersionNumber this build's game writes into its own saves
// (every save the port made on 2026-09-04 carries it). A console save with
// TU4 carries 393219 (0x00060003) and is refused as "more up-to-date".
constexpr uint32_t kGameSaveVersion = 805699586u;
constexpr int kSlotCount = 6;

// Size plus a hash of the first 64 KB: the STFS header, which holds the
// content id and the display name, changes whenever the package does.
std::string PackageFingerprint(const std::filesystem::path& package) {
  std::error_code ec;
  const uint64_t size = std::filesystem::file_size(package, ec);
  std::ifstream in(package, std::ios::binary);
  std::string head(64 * 1024, '\0');
  in.read(head.data(), std::streamsize(head.size()));
  head.resize(size_t(in.gcount()));
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%llu-%016llx", (unsigned long long)size,
                (unsigned long long)std::hash<std::string>{}(head));
  return buf;
}

// A slot "holds" a save when the game could load it: the world and the hero.
bool SlotHolds(const std::filesystem::path& slot_dir) {
  std::error_code ec;
  return std::filesystem::is_regular_file(slot_dir / "mainsave.bin", ec) &&
         std::filesystem::is_regular_file(slot_dir / "herosave.bin", ec);
}

std::filesystem::path ManifestPath(const std::filesystem::path& saves_dir) {
  return saves_dir / "imported_saves.txt";
}

// "<fingerprint> <slot> <package name>" per line.
std::string ManifestLookup(const std::filesystem::path& saves_dir,
                           const std::string& fingerprint) {
  std::ifstream in(ManifestPath(saves_dir));
  std::string line;
  while (std::getline(in, line)) {
    const size_t sp = line.find(' ');
    if (sp == std::string::npos) continue;
    if (line.substr(0, sp) != fingerprint) continue;
    const size_t sp2 = line.find(' ', sp + 1);
    return line.substr(sp + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp - 1);
  }
  return {};
}

void ManifestRecord(const std::filesystem::path& saves_dir, const std::string& fingerprint,
                    const std::string& slot, const std::string& package_name) {
  std::ofstream out(ManifestPath(saves_dir), std::ios::app);
  out << fingerprint << ' ' << slot << ' ' << package_name << '\n';
}

// The package's own slot name if it is free, else the first free HeroNNN.
// A slot that holds no loadable save (missing, or a leftover of an aborted
// save - the "corrupted" card) counts as free.
std::string FreeSlot(const std::filesystem::path& saves_dir, const std::string& preferred) {
  std::error_code ec;
  if (!SlotHolds(saves_dir / preferred)) return preferred;
  for (int i = 0; i < kSlotCount; ++i) {
    char slot[16];
    std::snprintf(slot, sizeof(slot), "Hero%03d", i);
    if (!SlotHolds(saves_dir / slot)) return slot;
  }
  return {};
}

// chaptersave.bin: a 4-byte big-endian length, then XML. If its VersionNumber
// is newer than this build's, rewrite it. Returns true when the file changed;
// `note` says what happened either way.
bool AdjustSaveVersion(const std::filesystem::path& chaptersave, std::string& note) {
  std::ifstream in(chaptersave, std::ios::binary);
  if (!in) { note = "no chaptersave.bin"; return false; }
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.size() < 8) { note = "chaptersave.bin too short"; return false; }
  const uint32_t len = (uint8_t(bytes[0]) << 24) | (uint8_t(bytes[1]) << 16) |
                       (uint8_t(bytes[2]) << 8) | uint8_t(bytes[3]);
  if (4 + size_t(len) > bytes.size()) { note = "chaptersave.bin length is wrong"; return false; }
  std::string xml = bytes.substr(4, len);
  const std::string open = "<VersionNumber type=\"uint\">";
  const size_t a = xml.find(open);
  const size_t b = a == std::string::npos ? a : xml.find("</VersionNumber>", a);
  if (a == std::string::npos || b == std::string::npos) { note = "no VersionNumber"; return false; }
  const std::string value = xml.substr(a + open.size(), b - a - open.size());
  const unsigned long long have = std::strtoull(value.c_str(), nullptr, 10);
  if (have == kGameSaveVersion) { note = "save version matches this build"; return false; }
  xml.replace(a + open.size(), value.size(), std::to_string(kGameSaveVersion));
  std::string out;
  out.push_back(char((xml.size() >> 24) & 0xFF));
  out.push_back(char((xml.size() >> 16) & 0xFF));
  out.push_back(char((xml.size() >> 8) & 0xFF));
  out.push_back(char(xml.size() & 0xFF));
  out += xml;
  out += bytes.substr(4 + len);
  std::ofstream o(chaptersave, std::ios::binary | std::ios::trunc);
  o.write(out.data(), std::streamsize(out.size()));
  note = "save version " + value + " -> " + std::to_string(kGameSaveVersion) +
         " (this build's) so the game accepts it";
  return true;
}
constexpr size_t kNameOffset = 0x411;   // display name, e.g. "Hero 1"
constexpr size_t kTitleNameOffset = 0x1691;  // title name, e.g. "Fable II"
constexpr size_t kNameSlot = 256;  // 128 UTF-16 characters
constexpr size_t kNameSlots = 9;   // one per locale

// Big-endian UTF-16 out of a buffer, stopping at the first NUL. The offset is
// odd, so reading it as aligned 16-bit words gives back mojibake that reads
// convincingly as a different byte order.
std::u16string ReadName(const std::vector<uint8_t>& d, size_t offset) {
  std::u16string out;
  for (size_t i = 0; i + 1 < kNameSlot; i += 2) {
    const size_t at = offset + i;
    if (at + 1 >= d.size())
      break;
    const char16_t c =
        static_cast<char16_t>((uint16_t(d[at]) << 8) | uint16_t(d[at + 1]));
    if (c == 0)
      break;
    out.push_back(c);
  }
  return out;
}

std::string ToUtf8(const std::u16string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    uint32_t c = in[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < in.size() &&
        in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (in[++i] - 0xDC00);
    }
    if (c < 0x80) {
      out.push_back(char(c));
    } else if (c < 0x800) {
      out.push_back(char(0xC0 | (c >> 6)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(char(0xE0 | (c >> 12)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (c >> 18)));
      out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

uint32_t ReadBe32(const std::vector<uint8_t>& d, size_t at) {
  if (at + 4 > d.size())
    return 0;
  return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) |
         (uint32_t(d[at + 2]) << 8) | uint32_t(d[at + 3]);
}

bool NameSaysFable(const std::string& title_name) {
  std::string lower = title_name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return lower.find("fable") != std::string::npos;
}

std::mutex g_queue_mutex;
std::vector<std::filesystem::path> g_queued;

}  // namespace

SaveInfo InspectSavePackage(const std::filesystem::path& path) {
  SaveInfo info;

  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    info.message = "That is not a file.";
    return info;
  }

  // Only the metadata block is needed, so this reads a bounded prefix rather
  // than the whole package - a Fable II save is about 5 MB.
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) {
    info.message = "Could not open that file.";
    return info;
  }
  std::vector<uint8_t> d(0x1800);
  const size_t got = std::fread(d.data(), 1, d.size(), f);
  std::fclose(f);
  d.resize(got);

  if (got < 0x1000 || (std::memcmp(d.data(), "CON ", 4) != 0 &&
                       std::memcmp(d.data(), "LIVE", 4) != 0 &&
                       std::memcmp(d.data(), "PIRS", 4) != 0)) {
    info.message = "Not an Xbox 360 content package.";
    return info;
  }

  info.ok = true;
  info.content_type = ReadBe32(d, kContentTypeOffset);
  info.title_id = ReadBe32(d, kTitleIdOffset);
  info.is_save = info.content_type == 0x00000001;
  info.title_name = ToUtf8(ReadName(d, kTitleNameOffset));

  // The title id is the right check when it is filled in. It is NOT always
  // filled in: every Fable II save to hand carries 00000000 there while naming
  // the title correctly at 0x1691. Rejecting those would refuse exactly the
  // saves this exists to import, so a zero id falls back to the title NAME.
  // A wrong non-zero id is still a wrong id and is still refused.
  info.right_game = info.title_id == kTitleId ||
                    (info.title_id == 0 && NameSaysFable(info.title_name));

  for (size_t slot = 0; slot < kNameSlots; ++slot) {
    info.display_name_u16 = ReadName(d, kNameOffset + slot * kNameSlot);
    if (!info.display_name_u16.empty())
      break;
  }
  info.display_name = ToUtf8(info.display_name_u16);

  if (!info.right_game) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "That save is for another game (title %08X %s).",
                  info.title_id, info.title_name.c_str());
    info.message = buf;
  } else if (!info.is_save) {
    info.message = "That is Fable II content, but not a saved game.";
  } else if (info.display_name.empty()) {
    info.message = path.filename().string();
  } else {
    info.message = info.display_name;
  }
  return info;
}

std::vector<std::filesystem::path> FindSavePackages(
    const std::filesystem::path& folder) {
  std::vector<std::filesystem::path> found;
  std::error_code ec;

  // A single file is accepted too. Someone who points this at one save rather
  // than at the folder holding it has said what they meant clearly enough.
  if (std::filesystem::is_regular_file(folder, ec)) {
    const SaveInfo info = InspectSavePackage(folder);
    if (info.ok && info.right_game && info.is_save)
      found.push_back(folder);
    return found;
  }

  if (!std::filesystem::is_directory(folder, ec))
    return found;

  for (auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
    if (ec)
      break;
    if (!entry.is_regular_file(ec))
      continue;
    const SaveInfo info = InspectSavePackage(entry.path());
    if (info.ok && info.right_game && info.is_save)
      found.push_back(entry.path());
  }
  std::sort(found.begin(), found.end());
  return found;
}

void QueueSaveImports(std::vector<std::filesystem::path> packages) {
  std::lock_guard<std::mutex> lock(g_queue_mutex);
  g_queued = std::move(packages);
}

std::vector<std::filesystem::path> TakeQueuedSaveImports() {
  std::lock_guard<std::mutex> lock(g_queue_mutex);
  std::vector<std::filesystem::path> out;
  out.swap(g_queued);
  return out;
}

bool ImportSave(const std::filesystem::path& package, std::string& message) {
  using rex::system::XContentType;
  using rex::system::xam::XCONTENT_AGGREGATE_DATA;

  const SaveInfo info = InspectSavePackage(package);
  if (!info.ok || !info.right_game || !info.is_save) {
    message = info.message.empty() ? "That file is not a Fable II save."
                                   : info.message;
    return false;
  }

  auto* kernel = REX_KERNEL_STATE();
  auto* content = kernel ? kernel->content_manager() : nullptr;
  if (content == nullptr) {
    message = "The runtime is not ready to take a save yet.";
    return false;
  }
  auto* profile = kernel->user_profile();
  const uint64_t xuid = profile ? profile->xuid() : 0;
  if (xuid == 0) {
    message = "There is no signed-in profile to import the save into.";
    return false;
  }

  // The package's own file name is the content's name. Renaming a save before
  // importing it would hand the game a save it does not recognise, so the name
  // is kept exactly as it arrived.
  const std::string name = package.filename().string();

  // Extract. This always lands in the downloadable-content slot - XUID 0,
  // content type 00000002 - because that is what InstallContent is for. The
  // move below is what turns it into a save.
  // The fingerprint is taken from the package itself, now; the manifest
  // and the slot choice need the profile's save folder, known only after
  // the content manager has answered once, below.
  const std::string fingerprint = PackageFingerprint(package);

  const auto rc = content->InstallContent(package);
  if (static_cast<uint32_t>(rc) != 0) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Could not read that save (0x%08X).",
                  static_cast<uint32_t>(rc));
    message = buf;
    return false;
  }

  XCONTENT_AGGREGATE_DATA data{};
  data.device_id = 1;
  data.content_type = XContentType::kSavedGame;
  data.set_display_name(info.display_name_u16);
  data.set_file_name(name);
  data.xuid = xuid;
  data.title_id = kTitleId;

  // Where the save has to end up. ContentManager has no accessor for its own
  // root, so the destination is asked for rather than assembled: CreateContent
  // makes the directory and GetOpenPackagePath names it. That path is also how
  // the root itself is found, four components up.
  const std::string root_name = "fable2import";
  content->CreateContent(root_name, xuid, data);
  std::filesystem::path dest = content->GetOpenPackagePath(root_name);
  if (dest.empty()) {
    // Already imported once: CreateContent refuses, but opening works.
    uint32_t license = 0;
    content->OpenContent(root_name, xuid, data, license);
    dest = content->GetOpenPackagePath(root_name);
  }
  content->CloseContent(root_name);
  if (dest.empty()) {
    message = "Could not work out where saves are kept.";
    return false;
  }

  // <root>/<xuid>/<title id>/<content type>/<name>
  const auto root =
      dest.parent_path().parent_path().parent_path().parent_path();
  char title_dir[16];
  std::snprintf(title_dir, sizeof(title_dir), "%08X", kTitleId);
  const auto src = root / "0000000000000000" / title_dir / "00000002" / name;

  std::error_code ec;
  if (!std::filesystem::is_directory(src, ec)) {
    message = "The save extracted, but not where it was expected.";
    REXLOG_WARN("Save import: nothing at {}", src.string());
    return false;
  }

  // The slot folders are siblings of dest (dest is <saves>/<name>). Once per
  // package: the manifest beside them remembers what was imported where, so
  // a launch does not overwrite the slot the player has since been saving
  // into, and a second package does not replace the first.
  const auto saves_dir = dest.parent_path();
  {
    const std::string already = ManifestLookup(saves_dir, fingerprint);
    if (!already.empty() && SlotHolds(saves_dir / already)) {
      std::filesystem::remove_all(src, ec);
      std::filesystem::remove(root / "0000000000000000" / title_dir / "Headers" /
                                  "00000002" / (name + ".header"),
                              ec);
      message = "already imported as " + already + " - not touched";
      return true;
    }
  }
  const std::string slot = FreeSlot(saves_dir, name);
  if (slot.empty()) {
    std::filesystem::remove_all(src, ec);
    message = "all six save slots are in use - delete one in the game, or move a "
              "HeroNNN folder out of " + saves_dir.string();
    return false;
  }
  if (slot != name) {
    // Re-point the content header and the destination at the free slot.
    data.set_file_name(slot);
    dest = saves_dir / slot;
    std::filesystem::remove_all(dest, ec);
  }

  std::filesystem::create_directories(dest, ec);
  size_t files = 0;
  for (auto& e : std::filesystem::recursive_directory_iterator(src, ec)) {
    const auto rel = std::filesystem::relative(e.path(), src, ec);
    const auto target = dest / rel;
    if (e.is_directory(ec)) {
      std::filesystem::create_directories(target, ec);
      continue;
    }
    std::filesystem::create_directories(target.parent_path(), ec);
    std::filesystem::copy_file(e.path(), target,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
      message = "Could not write the save into the profile.";
      REXLOG_WARN("Save import: copy {} -> {} failed: {}", e.path().string(),
                  target.string(), ec.message());
      return false;
    }
    ++files;
  }
  if (files == 0) {
    message = "That package is empty.";
    return false;
  }

  // A console save is usually from a title-updated game whose save version
  // is newer than this build's. The game refuses those outright ("created
  // with a more up-to-date version"), and the format is self-describing XML
  // plus a serialised world, so the only thing standing between the player
  // and the save is the number. Rewrite it to what this build writes.
  {
    std::string note;
    if (AdjustSaveVersion(dest / "chaptersave.bin", note))
      REXLOG_INFO("Save import: {} - {}", slot, note);
  }

  // The header is what XAM enumerates, so without it the save is invisible no
  // matter where its bytes are.
  content->WriteContentHeaderFile(xuid, data);
  ManifestRecord(saves_dir, fingerprint, slot, name);

  // Leave nothing behind in the downloadable-content slot: a save sitting
  // there would be offered to the game as DLC on the next boot.
  std::filesystem::remove_all(src, ec);
  std::filesystem::remove(root / "0000000000000000" / title_dir / "Headers" /
                              "00000002" / (name + ".header"),
                          ec);

  REXLOG_INFO("Save import: {} -> {} ({} file(s))", name, dest.string(), files);
  message = (info.display_name.empty() ? name : info.display_name) + " -> " + slot;
  return true;
}

}  // namespace fable2
