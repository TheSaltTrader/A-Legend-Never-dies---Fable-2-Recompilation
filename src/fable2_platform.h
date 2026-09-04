// Host OS bits the settings menu needs: native file pickers and the
// launch-time modifier check.
//
// The SDK ships SDL3, which has SDL_ShowOpenFileDialog, but SDL is linked
// *statically into rexruntime.dll* and none of its symbols are exported (
// checked: `SDL_ShowFileDialogWithProperties` does not appear in
// rexruntime.lib). Linking SDL3-static.lib into the executable as well would
// put a second, uninitialised copy of SDL in the process. This title is
// Windows-only, so the picker talks to IFileOpenDialog directly instead.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fable2 {

// Win32-style filter: a display name and a ';'-separated pattern list.
struct FileFilter {
  const char* name;
  const char* pattern;  // e.g. "*.iso;*.img"
};

// Modal open dialogs. Both return nullopt when the user cancels or the dialog
// cannot be shown; a cancel is not an error and is not logged.
std::optional<std::filesystem::path> PickFile(
    const std::string& title, const std::vector<FileFilter>& filters,
    const std::filesystem::path& start_in);

std::optional<std::filesystem::path> PickFolder(
    const std::string& title, const std::filesystem::path& start_in);

// Whether either Shift key is down. Read once at startup to decide whether to
// force the setup screen open on a launch that would otherwise skip it.
bool ShiftHeld();

// Human-readable byte count ("6.7 GB"), for the disc and progress readouts.
std::string FormatBytes(uint64_t bytes);

}  // namespace fable2
