// Importing an Xbox 360 Fable II save package.
//
// Saves arrive as STFS containers ("CON " for a console-signed one). The SDK's
// ContentManager can already extract those - it is what installs the DLC - but
// it puts the result under content type 00000002 and XUID 0, which is where
// downloadable content lives. A save has to be under the signed-in profile's
// XUID and content type 00000001 or the game never sees it.
//
// So importing is: extract with the SDK, then move the result to the save slot.
//
// Ported from the Ninja Gaiden II port, with one difference that matters. NG2
// validates a package by its title id at 0x360. The Fable II saves to hand
// carry ZERO there while naming the title correctly at 0x1691 ("Fable II"), so
// a straight port refuses the very saves it exists to import. Both are
// accepted here, and the check is documented where it is made.
//
// This runs after the runtime exists (OnPostSetup) but before the guest starts,
// so a save chosen on the setup screen is in place by the time the game looks.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fable2 {

// What a package is, read from its own header. `ok` is false when the file is
// not an STFS container at all.
struct SaveInfo {
  bool ok = false;
  bool right_game = false;  // title id matches, or is 0 and the name says Fable
  bool is_save = false;     // content type is 00000001, a saved game
  uint32_t title_id = 0;
  uint32_t content_type = 0;
  std::string title_name;           // from 0x1691, e.g. "Fable II"
  std::string display_name;         // UTF-8, for showing: "Hero 1"
  std::u16string display_name_u16;  // what goes back into the content header
  std::string message;              // one line, ready to show
};

SaveInfo InspectSavePackage(const std::filesystem::path& path);

// Every Fable II save in a folder, searched recursively. Anything that is not
// one is skipped silently: a save folder is a folder of mixed files, and the
// useful answer is how many saves are in it, not a complaint per file.
std::vector<std::filesystem::path> FindSavePackages(
    const std::filesystem::path& folder);

// Remember packages to import when the runtime is up. The setup screen runs
// before there is a ContentManager to import with, so it queues instead.
// Queueing replaces whatever was queued before, so choosing a second folder
// does not silently import the first one as well.
void QueueSaveImports(std::vector<std::filesystem::path> packages);
std::vector<std::filesystem::path> TakeQueuedSaveImports();

// Extracts `package` and moves it into the profile's save slot. Returns false
// with `message` set on failure; both are safe to show.
bool ImportSave(const std::filesystem::path& package, std::string& message);

}  // namespace fable2
