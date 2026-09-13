#include "fable2_gdb.h"

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace fable2 {
namespace {

// globals.gdb, big-endian throughout (decoded 2026-09-13, tools in the
// session notes; the walk below re-checks every assumption on the file it is
// given):
//
//   0x00  "GDB\0"
//   0x04  u32  size of the record section
//   0x08  u32  offset of the descriptor section, measured from 0x18
//   0x0C  u32  size of the descriptor section
//   0x10  u32  count (9993 in the shipped file), 0x14 u32 0
//   0x18  16 bytes the game rewrites at load (pointers), then
//   0x28  records ........ up to (0x08 word)
//   0x18+(0x08 word)  descriptors, (0x0C word) bytes
//
// A descriptor is [count << 8 | flag][count field ids, FNV-1 of the field
// name, ascending][count type words: type << 24 | C++ member index]. A record
// is [descriptor offset within the section][one 32-bit value per field, in
// the descriptor's id order]. Type 3 is a float. The game turns the record's
// descriptor offset into a pointer when it loads the file; the values stay
// where they are, which is why a scaled file works and a scaled resident copy
// does not (the definitions are filled in from it once, early).
constexpr uint32_t Fnv1(const char* s) {
  uint32_t h = 0x811c9dc5u;
  for (; *s; ++s) {
    h *= 0x01000193u;
    h ^= uint8_t(*s);
  }
  return h;
}
constexpr uint32_t kFields[] = {
    Fnv1("MaxDrawDistance"),          // 0x171e1806
    Fnv1("MaxDrawDistanceOverride"),  // 0xa9689dee
    Fnv1("BillboardDistance"),        // 0xbffc8ddb
    Fnv1("LodFadeDistance"),          // 0xe2687215
};
constexpr uint8_t kTypeFloat = 3;
constexpr size_t kHeaderSize = 0x18;
constexpr size_t kRecordsStart = 0x28;

uint32_t BE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void PutBE32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
float AsFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
uint32_t AsBits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }

}  // namespace

int WriteScaledGlobals(const std::filesystem::path& in, const std::filesystem::path& out,
                       double factor) {
  std::vector<uint8_t> d;
  {
    std::ifstream f(in, std::ios::binary);
    if (!f) {
      REXLOG_WARN("Draw distance: cannot read {}", in.string());
      return 0;
    }
    d.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  }
  if (d.size() < 0x40 || std::memcmp(d.data(), "GDB\0", 4) != 0) {
    REXLOG_WARN("Draw distance: {} is not a GDB file", in.string());
    return 0;
  }
  const size_t records_end = BE32(&d[0x08]);
  const size_t desc_start = kHeaderSize + records_end;
  const size_t desc_size = BE32(&d[0x0C]);
  if (desc_start + desc_size > d.size() || records_end < kRecordsStart) {
    REXLOG_WARN("Draw distance: {} has sections outside the file", in.string());
    return 0;
  }

  // Descriptors: offset within the section -> (field ids, type words).
  struct Desc { std::vector<uint32_t> ids, types; };
  std::vector<std::pair<size_t, Desc>> descs;  // sorted by offset
  size_t o = desc_start;
  while (o + 4 <= desc_start + desc_size) {
    const uint32_t hdr = BE32(&d[o]);
    const size_t cnt = hdr >> 8;
    if (hdr >> 24 || cnt > 4096 || o + 4 + 8 * cnt > desc_start + desc_size) break;
    Desc desc;
    for (size_t i = 0; i < cnt; ++i) desc.ids.push_back(BE32(&d[o + 4 + 4 * i]));
    for (size_t i = 0; i < cnt; ++i) desc.types.push_back(BE32(&d[o + 4 + 4 * cnt + 4 * i]));
    bool sorted = true;  // ids ascend (repeats allowed: array fields share a name)
    for (size_t i = 1; i < cnt; ++i) sorted &= desc.ids[i] >= desc.ids[i - 1];
    if (!sorted) break;
    descs.emplace_back(o - desc_start, std::move(desc));
    o += 4 + 8 * cnt;
  }
  if (o != desc_start + desc_size) {
    REXLOG_WARN("Draw distance: descriptor walk of {} stopped at {:#x}, expected {:#x} - "
                "layout not understood, file left alone", in.string(), o, desc_start + desc_size);
    return 0;
  }
  auto find_desc = [&](size_t off) -> const Desc* {
    auto it = std::lower_bound(descs.begin(), descs.end(), off,
                               [](const auto& e, size_t v) { return e.first < v; });
    return (it != descs.end() && it->first == off) ? &it->second : nullptr;
  };

  // Records: verify the whole chain first, then scale.
  std::vector<size_t> sites;
  o = kRecordsStart;
  while (o + 4 <= records_end) {
    const Desc* desc = find_desc(BE32(&d[o]));
    if (!desc) break;
    if (o + 4 + 4 * desc->ids.size() > records_end) break;
    for (size_t i = 0; i < desc->ids.size(); ++i) {
      if ((desc->types[i] >> 24) != kTypeFloat) continue;
      bool want = false;
      for (uint32_t id : kFields) want |= (id == desc->ids[i]);
      if (!want) continue;
      const float v = AsFloat(BE32(&d[o + 4 + 4 * i]));
      if (v > 0.5f && v < 20000.0f) sites.push_back(o + 4 + 4 * i);
    }
    o += 4 + 4 * desc->ids.size();
  }
  if (records_end - o > 16) {
    REXLOG_WARN("Draw distance: record walk of {} stopped at {:#x}, expected {:#x} - "
                "layout not understood, file left alone", in.string(), o, records_end);
    return 0;
  }
  for (size_t at : sites) PutBE32(&d[at], AsBits(float(AsFloat(BE32(&d[at])) * factor)));

  std::error_code ec;
  std::filesystem::create_directories(out.parent_path(), ec);
  const auto tmp = out.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(reinterpret_cast<const char*>(d.data()), d.size())) {
      REXLOG_WARN("Draw distance: cannot write {}", tmp);
      return 0;
    }
  }
  std::filesystem::rename(tmp, out, ec);
  if (ec) {
    REXLOG_WARN("Draw distance: cannot replace {}: {}", out.string(), ec.message());
    return 0;
  }
  return int(sites.size());
}

void InstallDrawDistance(rex::filesystem::VirtualFileSystem* fs,
                         const std::filesystem::path& game_root,
                         const std::filesystem::path& shadow_dir, int percent) {
  namespace sfs = std::filesystem;
  // The shadow folder mirrors the game's data\globals folder: the scaled
  // globals.gdb plus every other file in there, so the whole folder can be
  // redirected. The runtime's OpenFile resolves the DIRECTORY of a path
  // (that is where symbolic links apply) and then takes the child by name,
  // so a link on the one file is never consulted - it has to be the folder
  // (found the hard way: the first version linked the file, the log said
  // "served", and the game read its own copy).
  const auto mirror = shadow_dir / "globals";
  std::error_code ec;
  if (percent == 100 || percent <= 0) {
    // Nothing served: a stale mirror from an earlier setting is removed so
    // the folder cannot be mistaken for something in use. Removing hard
    // links leaves the game's own files untouched.
    sfs::remove_all(mirror, ec);
    return;
  }
  if (!fs) {
    REXLOG_WARN("Draw distance: no file system to redirect through");
    return;
  }
  const auto src_dir = game_root / "data" / "globals";
  const int n = WriteScaledGlobals(src_dir / "globals.gdb", mirror / "globals.gdb", percent / 100.0);
  if (n == 0) return;

  // The six siblings (sound banks, model and texture headers, the list):
  // hard links when the two folders share a volume - no space, always
  // current - and copies otherwise. Anything in the mirror that the game
  // folder no longer has is dropped.
  int linked = 0, copied = 0;
  for (const auto& e : sfs::directory_iterator(src_dir, ec)) {
    if (!e.is_regular_file(ec) || e.path().filename() == "globals.gdb") continue;
    const auto dst = mirror / e.path().filename();
    if (sfs::exists(dst, ec) && sfs::file_size(dst, ec) == e.file_size(ec) &&
        sfs::last_write_time(dst, ec) == e.last_write_time(ec))
      continue;  // a hard link, or a copy still current
    sfs::remove(dst, ec);
    sfs::create_hard_link(e.path(), dst, ec);
    if (!ec) { ++linked; continue; }
    ec.clear();
    if (sfs::copy_file(e.path(), dst, sfs::copy_options::overwrite_existing, ec)) { ++copied; continue; }
    REXLOG_WARN("Draw distance: cannot mirror {} ({}) - the game keeps its own folder",
                e.path().string(), ec.message());
    sfs::remove_all(mirror, ec);
    return;
  }
  for (const auto& e : sfs::directory_iterator(mirror, ec)) {
    if (!sfs::exists(src_dir / e.path().filename(), ec)) sfs::remove(e.path(), ec);
  }

  // game:\data\globals\... and d:\data\globals\... both become
  // \Device\Harddisk0\Partition1\data\globals\... through the runtime's own
  // links, and its resolver keeps following links until none matches, so a
  // link on that folder sends it to a device of its own. That device is
  // mounted outside \Device\Harddisk0 on purpose: the runtime's null device
  // claims everything under it that the game partition does not.
  constexpr const char* kMount = "\\Device\\Fable2Shadow";
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(kMount, mirror, true);
  if (!device->Initialize() || !fs->RegisterDevice(std::move(device))) {
    REXLOG_WARN("Draw distance: could not mount {} - the game keeps its own folder",
                mirror.string());
    return;
  }
  fs->RegisterSymbolicLink("\\Device\\Harddisk0\\Partition1\\data\\globals", kMount);
  REXLOG_INFO("Draw distance {}%: {} values scaled into {}; data\\globals served from there "
              "({} files hard-linked, {} copied)",
              percent, n, (mirror / "globals.gdb").string(), linked, copied);
}

}  // namespace fable2
