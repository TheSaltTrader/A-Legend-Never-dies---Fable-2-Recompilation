// The game's title update, and whether the one at hand fits this disc.
//
// A console that played Fable II had the disc's title update installed, and
// its saves carry that version. This port is compiled from the disc's own
// executable, so those saves are refused, and even with their version number
// adjusted they die loading (see CHANGELOG 0.0.16). Loading them takes a
// build compiled WITH the update: the update is a delta patch (default.xexp)
// the runtime applies when it sits beside default.xex, and the recompiler
// loads the executable through the same runtime, so the same file patches
// the compile. Title updates are per disc: the patch names the executable it
// was made for by a hash of its signature, and one for another pressing does
// not apply.
//
// What this module does: read the executable's identity (media id, version),
// find a patch beside it or in the staging folder, check it against the
// executable, and say all of that in words the setup screen can show. It
// does NOT apply the patch to a build that was not compiled for it - that
// would run the old code against patched data.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace fable2 {

struct TitleUpdateStatus {
  // The executable in the game folder.
  bool xex_ok = false;
  uint32_t media_id = 0;
  uint32_t version = 0;        // e.g. 0x1A = "0.0.0.26"
  std::string xex_error;
  // A patch found beside it or staged.
  bool patch_found = false;
  std::filesystem::path patch_path;
  bool patch_matches = false;  // its source digest is this executable's
  uint32_t patch_source_version = 0;
  uint32_t patch_target_version = 0;
  std::string patch_note;      // one line: fits / does not fit / unreadable
  // Whether THIS build was compiled with the patch applied.
  bool compiled_with_patch = false;
};

// "0.0.1.26" for 0x11A - the runtime's own rendering of an XEX version.
std::string VersionText(uint32_t version);

// Where a chosen patch is kept: <exe folder>/titleupdate/default.xexp.
std::filesystem::path StagedPatchPath();

// Read everything. `game_dir` holds default.xex.
TitleUpdateStatus InspectTitleUpdate(const std::filesystem::path& game_dir);

// A file the player chose: a bare default.xexp is copied to the staging
// folder at once; a LIVE/CON package is queued and unpacked when the runtime
// is up (InstallQueuedTitleUpdate), the way saves are. Returns false with a
// message when the file is neither.
bool ChooseTitleUpdateFile(const std::filesystem::path& file, std::string& message);
void InstallQueuedTitleUpdate();

// Is `file` a plausible title update at all - a default.xexp (XEX2 with a delta
// descriptor) or a LIVE/CON/PIRS package? A light check for the setup screen's
// Install gate; the full "fits THIS disc" test needs the extracted executable
// and is done by InspectTitleUpdate afterwards. `note` gets a one-line reason.
bool IsTitleUpdateFile(const std::filesystem::path& file, std::string* note);

// Install the chosen title update INTO a game folder, beside default.xex, so
// the runtime applies game:\default.xexp and mounts game:\update at play time.
// A loose default.xexp is copied in and its tu1_data.bnk (beside it, or under
// data/) brought along to update/data/; a LIVE/CON package is queued for the
// runtime to unpack there at first start. Returns false with a message on
// failure. This is what makes "install the game and the patch together" true.
bool StageTitleUpdateInto(const std::filesystem::path& tu_file,
                          const std::filesystem::path& game_dir, std::string& message);

}  // namespace fable2
