// Fable II port - keeping the install current.
//
// At launch the port asks the project's GitHub releases page for the newest
// version (a 3-second cap, nothing shown when offline or current) and, when
// there is a newer one, offers it: nothing is downloaded and nothing is
// installed without a click, and the restart is a click of its own. The
// install replaces only what the release zip carries - the executable, the
// runtime, the tools - by moving the running files aside as .old (Windows
// allows renaming a file in use) and extracting with the tar.exe every
// Windows 10 and 11 ships. The disc rip, the DLC, the saves, the settings
// and the texture pack are never in a release zip, so they are never
// touched. See CleanupAfterUpdate for what the next start does with the
// leftovers.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

struct Fable2Settings;   // fable2_settings.h; global, like the rest of the port

namespace fable2 {

// "owner/name" of the repository whose releases carry the port.
extern const char* const kUpdateRepo;

struct UpdateInfo {
  std::string version;     // "0.2.1": the tag without its leading v
  std::string tag;         // "v0.2.1"
  std::string page_url;    // the release page, for the notes
  std::string asset_name;  // fable2recomp-v0.2.1-win-amd64.zip
  std::string asset_url;   // where that zip is
  uint64_t asset_size = 0;
};

// "0.2.1" against "v0.2.10": negative, zero or positive like strcmp, numerically
// per dot-separated part. A part that is not a number counts as zero.
int CompareVersions(const std::string& a, const std::string& b);

// The version this executable was built as (FABLE2_VERSION), unless the
// developer override FABLE2_UPDATE_PRETEND_VERSION is set, which exists so the
// "an update is available" path can be exercised against the real releases.
std::string CurrentVersion();

// The newest release of `repo`, within `timeout_ms`. False with `error` set
// on any failure, offline included; a repository with no releases yet is a
// failure too ("HTTP 404").
bool FetchLatestRelease(const std::string& repo, UpdateInfo& out, std::string& error,
                        int timeout_ms = 3000);

// Streams `url` into `path` (through path + ".part"). `progress(done, total)`
// is called from the calling thread as data lands; total is 0 when the
// server did not say. `cancel` stops it between chunks.
bool DownloadToFile(const std::string& url, const std::filesystem::path& path,
                    const std::function<void(uint64_t, uint64_t)>& progress,
                    const std::atomic<bool>* cancel, std::string& error);

// Installs a release zip over `install_dir`. The zip is extracted into
// install_dir/update/stage with tar.exe, then every file in it except the
// game/ and dlc/ placeholders is moved into place: a file already there is
// renamed to <name>.old first and listed in update/renamed.txt. On any
// failure the renamed files are put back. `log` gets one line per step.
bool InstallReleaseZip(const std::filesystem::path& zip, const std::filesystem::path& install_dir,
                       const std::function<void(const std::string&)>& log, std::string& error);

// At start-up: removes the .old files and the zip a previous update left,
// retrying briefly in the background because the process that installed
// them may still be closing. Never fails; what it could not remove waits
// for the next start.
void CleanupAfterUpdate(const std::filesystem::path& install_dir);

// Starts a fresh fable2.exe from `exe_dir` that waits for this process to
// exit before it opens its window, so the handover shows one window at a
// time. Returns false if it could not be started, in which case the caller
// must not quit.
bool RelaunchSelf(const std::filesystem::path& exe_dir);

// The other half of RelaunchSelf, for the new process: waits (up to 10 s)
// for the process named by FABLE2_WAIT_PID to exit. No-op when unset.
void WaitForPreviousInstance();

// The launch-time panel: "version X is available" with Update now / Not now /
// Skip this version, the download bar, and the restart offer. Draws nothing
// while idle, checking, or current. One instance, owned by the app; the
// settings page reaches it through GetUpdateOverlay().
class UpdateOverlay final : public rex::ui::ImGuiDialog {
 public:
  UpdateOverlay(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
                std::function<void()> on_restart);
  ~UpdateOverlay() override;

  // Asks the releases page on a background thread. A manual check (the
  // settings page's button) also reports "up to date" and ignores the
  // skipped version; the launch check stays silent unless there is
  // something to offer.
  void StartCheck(bool manual);

  // For the settings page: one line saying what the last check found, and
  // whether a check or a download is in flight.
  std::string StatusLine() const;
  bool Busy() const;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class State {
    kIdle,         // nothing to show
    kChecking,
    kUpToDate,     // manual check only: shown as a line, not a panel
    kAvailable,    // the offer
    kDownloading,
    kInstalling,
    kInstalled,    // restart offer
    kFailed,
  };

  void StartDownload();
  void JoinWorker();
  void SetState(State s);
  State GetState() const { return state_.load(std::memory_order_acquire); }

  Fable2Settings* settings_;
  std::function<void()> on_restart_;
  std::atomic<State> state_{State::kIdle};
  std::atomic<bool> cancel_{false};
  std::atomic<bool> manual_{false};
  std::atomic<uint64_t> done_{0}, total_{0};
  std::thread worker_;
  mutable std::mutex mutex_;   // guards info_, error_, status_
  UpdateInfo info_;
  std::string error_;
  std::string status_;
};

// Set by the app when the overlay exists; null otherwise.
void SetUpdateOverlay(UpdateOverlay* overlay);
UpdateOverlay* GetUpdateOverlay();

}  // namespace fable2
