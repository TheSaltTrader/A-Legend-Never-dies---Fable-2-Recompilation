// Draw distance. The per-object draw distances are values in the game's own
// data\globals\globals.gdb (MaxDrawDistance, MaxDrawDistanceOverride,
// BillboardDistance, LodFadeDistance), read ONCE while the game starts and
// copied into its object definitions: scaling them in the resident copy, in
// play or on the title screen, changes nothing, while a scaled FILE removes
// the whole far side of Bowerstone Market at x0.1 (2026-09-13). So the port
// writes a scaled copy and serves it in place of the original through the
// runtime's file system. The game folder is never touched.
#pragma once

#include <filesystem>

namespace rex::filesystem {
class VirtualFileSystem;
}

namespace fable2 {

// Writes `out` = `in` with the four draw-distance fields scaled by `factor`
// (values outside 0.5..20000 - sentinels, references - are left alone).
// Returns the number of values scaled; 0 means nothing was written, and the
// reason is in the log. The layout is checked end to end before anything is
// written, so a file this code does not understand is refused, not mangled.
int WriteScaledGlobals(const std::filesystem::path& in,
                       const std::filesystem::path& out, double factor);

// Serves the scaled copy when `percent` is not 100: mirrors the game's
// data\globals folder under `shadow_dir`/globals (the scaled globals.gdb plus
// hard links - copies across volumes - of the other files there) and
// redirects that folder to the mirror. The folder, not the file: the
// runtime's OpenFile resolves a path's directory, where links apply, and then
// takes the child by name. At 100 the mirror is removed and nothing is
// redirected. Call once, after the runtime has mounted the game and before
// the guest starts opening files.
void InstallDrawDistance(rex::filesystem::VirtualFileSystem* fs,
                         const std::filesystem::path& game_root,
                         const std::filesystem::path& shadow_dir, int percent);

}  // namespace fable2
