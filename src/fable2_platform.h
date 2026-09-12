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
struct MonitorInfo {
  int index = 0;
  // The whole display - what a person calls its resolution, and what the menu
  // shows beside it.
  int full_width = 0;
  int full_height = 0;
  // What a window can actually occupy: the display minus the taskbar. Smaller
  // than the full size, and the reason a "4K" window opens 72 pixels short.
  int width = 0;
  int height = 0;
  float scale = 1.0f;   // 1.25 at 125%
  bool primary = false;
};

// Every attached display, ordered LEFT TO RIGHT by position. Not primary-first:
// that is what this did before, and it disagreed with the runtime's own display
// indices for every display that was not the primary. Empty if they cannot be
// enumerated, in which case nothing should be clamped - refusing to guess beats
// clamping to a number that came from nowhere.
// Whether this process owns the foreground window. Input is held while it
// does not, so an alt-tabbed game does not keep reading the keyboard the
// player is now typing into something else with.
bool ThisProcessIsForeground();

// Ask Windows for the finest timer resolution (0.5 ms) and opt out of the
// timer coalescing it applies to processes it considers background. The
// runtime sleeps in whole milliseconds all over its frame - the guest's
// KeDelayExecutionThread, the GPU thread's poll of the game's own wait
// packets, the vsync worker - and every one of those sleeps lasts at least
// one timer tick. Per-process since Windows 10 2004, so nobody else's request
// helps this process. Logs what was granted.
void RaiseTimerResolution();

// Whether the page holding `p` is committed and readable. For dumping the
// guest image range without touching a reserved-but-unmapped page.
bool PageCommitted(const void* p);

// The three places a Windows app's icon comes from, none of which implies the
// others: the executable's resource (Explorer, desktop shortcuts), the
// window's own icon (title bar, taskbar, Alt-Tab), and the AppUserModelID
// that the taskbar groups and pins by. The resource is linked in from
// resources/fable2.rc; these two do the rest.
void SetAppUserModelId();                 // before any window exists
void ApplyWindowIcon(void* native_window);  // HWND, once the window is up

std::vector<MonitorInfo> Monitors();

// The scaling Windows will ACTUALLY apply to this process's windows.
//
// Not the target monitor's: measured, the window comes out at the PRIMARY
// display's scale wherever it is put, which is what a System-DPI-aware process
// gets. Asking for a size converted with the 4K monitor's own 150% therefore
// produced a window a fifth too small on it.
float SystemScale();

// The PHYSICAL work area of one monitor, and its scaling. Falls back to the
// primary when the index is out of range. False if nothing was determined.
bool MonitorWorkArea(int index, int& width, int& height, float& scale);

// Starts another copy of this application and does NOT wait for it. Used by
// the game's own "Quit Game", which returns the player to the setup screen by
// relaunching - see Ng2App::ReturnToMenu for why it is a relaunch and not a
// teardown. Returns false if the process could not be started, in which case
// the caller should quit rather than pretend a menu is coming.
bool LaunchDetached(const std::filesystem::path& exe, const std::string& args);

// Human-readable byte count ("6.7 GB"), for the disc and progress readouts.

std::string FormatBytes(uint64_t bytes);

}  // namespace fable2
