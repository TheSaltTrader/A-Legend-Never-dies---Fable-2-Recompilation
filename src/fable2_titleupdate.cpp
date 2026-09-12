#include "fable2_titleupdate.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xam/user_profile.h>

#pragma comment(lib, "bcrypt.lib")

namespace fable2 {
namespace {

constexpr uint32_t kTitleId = 0x4D5307F1;
constexpr uint32_t kKeyExecutionInfo = 0x00040006;
constexpr uint32_t kKeyDeltaPatch = 0x000005FF;

uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool ReadHead(const std::filesystem::path& path, std::vector<uint8_t>& out, size_t max_bytes) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.resize(max_bytes);
  in.read(reinterpret_cast<char*>(out.data()), std::streamsize(max_bytes));
  out.resize(size_t(in.gcount()));
  return out.size() >= 32;
}

// The XEX2 optional header table: key -> value/offset.
bool OptHeader(const std::vector<uint8_t>& h, uint32_t key, uint32_t& value) {
  if (h.size() < 24 || std::memcmp(h.data(), "XEX2", 4) != 0) return false;
  const uint32_t count = be32(&h[20]);
  for (uint32_t i = 0; i < count; ++i) {
    const size_t at = 24 + size_t(i) * 8;
    if (at + 8 > h.size()) return false;
    if (be32(&h[at]) == key) {
      value = be32(&h[at + 4]);
      return true;
    }
  }
  return false;
}

bool Sha1(const uint8_t* data, size_t size, uint8_t out[20]) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = false;
  if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0))) {
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
      if (BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(data), ULONG(size), 0)) &&
          BCRYPT_SUCCESS(BCryptFinishHash(hash, out, 20, 0)))
        ok = true;
      BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
  }
  return ok;
}

std::filesystem::path g_queued_package;
std::mutex g_queue_mutex;

}  // namespace

std::string VersionText(uint32_t v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", v >> 28, (v >> 24) & 0xF, (v >> 8) & 0xFFFF,
                v & 0xFF);
  return buf;
}

std::filesystem::path StagedPatchPath() {
  return rex::filesystem::GetExecutableFolder() / "titleupdate" / "default.xexp";
}

TitleUpdateStatus InspectTitleUpdate(const std::filesystem::path& game_dir) {
  TitleUpdateStatus s;
#if defined(FABLE2_COMPILED_WITH_PATCH)
  s.compiled_with_patch = true;
#endif
  std::vector<uint8_t> xex;
  const auto xex_path = game_dir / "default.xex";
  if (!ReadHead(xex_path, xex, 64 * 1024)) {
    s.xex_error = "no readable default.xex in " + game_dir.string();
    return s;
  }
  uint32_t exec_off = 0;
  if (!OptHeader(xex, kKeyExecutionInfo, exec_off) || exec_off + 16 > xex.size()) {
    s.xex_error = "default.xex has no execution info header";
    return s;
  }
  s.media_id = be32(&xex[exec_off]);
  s.version = be32(&xex[exec_off + 4]);
  s.xex_ok = true;

  // The patch names the executable it was built against by the SHA-1 of
  // that executable's RSA signature (0x100 bytes at security info + 8).
  uint8_t digest[20] = {};
  const uint32_t sec = be32(&xex[16]);
  const bool have_digest = sec + 8 + 0x100 <= xex.size() && Sha1(&xex[sec + 8], 0x100, digest);

  std::error_code ec;
  for (const auto& candidate : {game_dir / "default.xexp", StagedPatchPath()}) {
    if (!std::filesystem::is_regular_file(candidate, ec)) continue;
    std::vector<uint8_t> p;
    if (!ReadHead(candidate, p, 64 * 1024)) continue;
    s.patch_found = true;
    s.patch_path = candidate;
    uint32_t desc = 0;
    if (!OptHeader(p, kKeyDeltaPatch, desc) || desc + 32 > p.size()) {
      s.patch_note = "not a title update patch (no delta descriptor)";
      break;
    }
    s.patch_target_version = be32(&p[desc + 4]);
    s.patch_source_version = be32(&p[desc + 8]);
    s.patch_matches = have_digest && std::memcmp(&p[desc + 12], digest, 20) == 0;
    char buf[200];
    if (s.patch_matches) {
      std::snprintf(buf, sizeof(buf), "fits this disc: %s -> %s",
                    VersionText(s.patch_source_version).c_str(),
                    VersionText(s.patch_target_version).c_str());
    } else {
      std::snprintf(buf, sizeof(buf),
                    "made for another pressing of the disc (its source is version %s, "
                    "signature differs)",
                    VersionText(s.patch_source_version).c_str());
    }
    s.patch_note = buf;
    break;
  }
  return s;
}

bool ChooseTitleUpdateFile(const std::filesystem::path& file, std::string& message) {
  std::vector<uint8_t> head;
  if (!ReadHead(file, head, 64)) {
    message = "Could not read that file.";
    return false;
  }
  std::error_code ec;
  if (std::memcmp(head.data(), "XEX2", 4) == 0) {
    const auto dest = StagedPatchPath();
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::filesystem::copy_file(file, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      message = "Could not copy it to " + dest.string() + ": " + ec.message();
      return false;
    }
    message = "Kept as " + dest.string();
    return true;
  }
  if (std::memcmp(head.data(), "LIVE", 4) == 0 || std::memcmp(head.data(), "CON ", 4) == 0 ||
      std::memcmp(head.data(), "PIRS", 4) == 0) {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    g_queued_package = file;
    message = "Title update package queued - unpacked when the game starts.";
    return true;
  }
  message = "Not a title update: expected a LIVE/CON package or a default.xexp.";
  return false;
}

void InstallQueuedTitleUpdate() {
  std::filesystem::path package;
  {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    package.swap(g_queued_package);
  }
  if (package.empty()) return;
  using rex::system::XContentType;
  using rex::system::xam::XCONTENT_AGGREGATE_DATA;
  auto* kernel = REX_KERNEL_STATE();
  auto* content = kernel ? kernel->content_manager() : nullptr;
  if (!content) {
    REXLOG_WARN("Title update: runtime not ready to unpack {}", package.string());
    return;
  }
  const std::string name = package.filename().string();
  const auto rc = content->InstallContent(package);
  if (static_cast<uint32_t>(rc) != 0) {
    REXLOG_WARN("Title update: could not unpack {} ({:#x})", package.string(),
                static_cast<uint32_t>(rc));
    return;
  }
  // Where InstallContent put it: the content root is learned the way the
  // save importer learns it, by asking for a package path.
  auto* profile = kernel->user_profile();
  XCONTENT_AGGREGATE_DATA data{};
  data.device_id = 1;
  data.content_type = XContentType::kSavedGame;
  data.set_file_name("fable2tu");
  data.xuid = profile ? profile->xuid() : 0;
  data.title_id = kTitleId;
  content->CreateContent("fable2tu", data.xuid, data);
  std::filesystem::path probe = content->GetOpenPackagePath("fable2tu");
  content->CloseContent("fable2tu");
  std::error_code ec;
  if (probe.empty()) {
    REXLOG_WARN("Title update: could not work out the content root");
    return;
  }
  std::filesystem::remove_all(probe, ec);
  const auto root = probe.parent_path().parent_path().parent_path().parent_path();
  char title_dir[16];
  std::snprintf(title_dir, sizeof(title_dir), "%08X", kTitleId);
  const auto extracted = root / "0000000000000000" / title_dir / "000B0000" / name;
  const auto xexp = extracted / "default.xexp";
  if (!std::filesystem::is_regular_file(xexp, ec)) {
    REXLOG_WARN("Title update: {} holds no default.xexp (looked in {})", name,
                extracted.string());
    return;
  }
  const auto dest = StagedPatchPath();
  std::filesystem::create_directories(dest.parent_path(), ec);
  std::filesystem::copy_file(xexp, dest, std::filesystem::copy_options::overwrite_existing, ec);
  // The data bank rides along, for the day a build compiled with the patch
  // mounts it as update:\data.
  const auto bank = extracted / "data" / "tu1_data.bnk";
  if (std::filesystem::is_regular_file(bank, ec)) {
    std::filesystem::create_directories(dest.parent_path() / "data", ec);
    std::filesystem::copy_file(bank, dest.parent_path() / "data" / "tu1_data.bnk",
                               std::filesystem::copy_options::overwrite_existing, ec);
  }
  std::filesystem::remove_all(extracted, ec);
  REXLOG_INFO("Title update: {} unpacked, patch kept as {}", name, dest.string());
}

}  // namespace fable2
