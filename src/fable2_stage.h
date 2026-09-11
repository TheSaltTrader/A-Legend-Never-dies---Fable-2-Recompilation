// Which region the game is loading, for per-stage texture warming.
//
// The GPU plugin records which pack textures each stage uses and reads that
// stage's files back into the page cache when the stage returns, so the first
// draw needing one is a warm read rather than a cold one. It is keyed by an
// integer stage id the app publishes in the `texture_pack_chapter` cvar; the
// plugin knows nothing about what a stage is.
//
// NG2 gets its id from the chapter file the game opens. Fable II streams its
// world out of one bank and opens no per-level file - but it does keep a
// per-region AUDIO bank on the disc (data\audio\region_specific_<name>.bnk,
// plus atmos_<name>.bnk), and the runtime now reports every guest file open
// (rex::kernel::xboxkrnl::SetFileOpenObserver). Whether the game opens those
// banks when a region loads, rather than all at once at boot, is what the
// first run with this in decides: every open is logged at debug level for
// exactly that reason.
//
// 0 means no region has loaded yet - boot, the menus, the character select.

#pragma once

#include <filesystem>
#include <string>

namespace fable2 {

// Enumerates the region banks under <game_root>/data/audio to give each a
// stable id, and installs the file-open observer. Call once, after the
// runtime exists (OnPostSetup); safe to call when the folder is missing, in
// which case nothing is ever published and warming simply never engages.
void InstallStageObserver(const std::filesystem::path& game_root);

int CurrentStage();               // 0 until a region has been seen
std::string CurrentStageName();   // "" until then, else e.g. "bowerstone_market"

}  // namespace fable2
