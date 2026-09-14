#include "fable2_update.h"

#include "fable2_menu.h"      // Fonts()
#include "fable2_platform.h"  // LaunchDetached
#include "fable2_settings.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#ifndef FABLE2_VERSION
#define FABLE2_VERSION "dev"
#endif

namespace fable2 {

const char* const kUpdateRepo = "TheSaltTrader/A-Legend-Never-dies---Fable-2-Recompilation";

namespace {

namespace fs = std::filesystem;

std::wstring Widen(const std::string& s) {
  if (s.empty())
    return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

std::string Narrow(const std::wstring& w) {
  if (w.empty())
    return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string WinHttpErrorText(DWORD code) {
  switch (code) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "no connection (name not resolved)";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "no connection";
    case ERROR_WINHTTP_TIMEOUT: return "timed out";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "connection dropped";
    case ERROR_WINHTTP_SECURE_FAILURE: return "TLS failure";
    default: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "WinHTTP error %lu", static_cast<unsigned long>(code));
      return buf;
    }
  }
}

struct HttpHandle {
  HINTERNET h = nullptr;
  ~HttpHandle() {
    if (h)
      WinHttpCloseHandle(h);
  }
};

// One GET. The body goes to `body` (capped at body_cap bytes) when sink is
// null, else to sink(data, size, total_so_far), which returns false to stop.
bool HttpGet(const std::string& url, int timeout_ms, bool github_api, std::string* body,
             size_t body_cap, const std::function<bool(const char*, DWORD)>& sink,
             uint64_t* content_length, std::string& error) {
  const std::wstring wurl = Widen(url);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
    error = "bad URL";
    return false;
  }
  const std::wstring full_path = std::wstring(path) + extra;

  HttpHandle session;
  session.h = WinHttpOpen(Widen(std::string("fable2recomp/") + FABLE2_VERSION).c_str(),
                          WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session.h) {
    error = WinHttpErrorText(GetLastError());
    return false;
  }
  WinHttpSetTimeouts(session.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  HttpHandle conn;
  conn.h = WinHttpConnect(session.h, host, uc.nPort, 0);
  if (!conn.h) {
    error = WinHttpErrorText(GetLastError());
    return false;
  }
  HttpHandle req;
  req.h = WinHttpOpenRequest(conn.h, L"GET", full_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  if (!req.h) {
    error = WinHttpErrorText(GetLastError());
    return false;
  }
  const wchar_t* headers =
      github_api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
                 : WINHTTP_NO_ADDITIONAL_HEADERS;
  if (!WinHttpSendRequest(req.h, headers, headers ? DWORD(-1) : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(req.h, nullptr)) {
    error = WinHttpErrorText(GetLastError());
    return false;
  }
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status != 200) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "HTTP %lu", static_cast<unsigned long>(status));
    error = buf;
    return false;
  }
  if (content_length) {
    DWORD length = 0;
    size = sizeof(length);
    *content_length = WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                          WINHTTP_HEADER_NAME_BY_INDEX, &length, &size,
                                          WINHTTP_NO_HEADER_INDEX)
                          ? length
                          : 0;
  }
  std::vector<char> buffer(64 * 1024);
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(req.h, &available)) {
      error = WinHttpErrorText(GetLastError());
      return false;
    }
    if (available == 0)
      break;
    while (available > 0) {
      const DWORD want = std::min<DWORD>(available, DWORD(buffer.size()));
      DWORD got = 0;
      if (!WinHttpReadData(req.h, buffer.data(), want, &got)) {
        error = WinHttpErrorText(GetLastError());
        return false;
      }
      if (got == 0)
        break;
      available -= got;
      if (sink) {
        if (!sink(buffer.data(), got)) {
          error = "cancelled";
          return false;
        }
      } else if (body) {
        if (body->size() + got > body_cap) {
          error = "answer too large";
          return false;
        }
        body->append(buffer.data(), got);
      }
    }
  }
  return true;
}

// Minimal JSON reading, enough for the fields of GitHub's release answer.
// A key's string value: the text between the quotes after "key":, with \" and
// \\ unescaped. Empty when absent.
std::string JsonString(const std::string& s, const std::string& key, size_t from, size_t* end_out) {
  const std::string needle = "\"" + key + "\"";
  size_t p = s.find(needle, from);
  if (p == std::string::npos)
    return {};
  p = s.find(':', p + needle.size());
  if (p == std::string::npos)
    return {};
  ++p;
  while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t'))
    ++p;
  if (p >= s.size() || s[p] != '"')
    return {};
  ++p;
  std::string out;
  while (p < s.size() && s[p] != '"') {
    if (s[p] == '\\' && p + 1 < s.size()) {
      ++p;
      if (s[p] == 'n') out += '\n';
      else if (s[p] == 't') out += '\t';
      else if (s[p] == 'u') { p += 4; out += '?'; }   // not needed for tags and URLs
      else out += s[p];
    } else {
      out += s[p];
    }
    ++p;
  }
  if (end_out)
    *end_out = p;
  return out;
}

uint64_t JsonNumber(const std::string& s, const std::string& key, size_t from) {
  const std::string needle = "\"" + key + "\"";
  size_t p = s.find(needle, from);
  if (p == std::string::npos)
    return 0;
  p = s.find(':', p + needle.size());
  if (p == std::string::npos)
    return 0;
  ++p;
  while (p < s.size() && s[p] == ' ')
    ++p;
  return std::strtoull(s.c_str() + p, nullptr, 10);
}

// The text of the "assets" array: from its '[' to the matching ']', string
// literals skipped so a bracket inside one does not end it early.
std::string AssetsArray(const std::string& s) {
  size_t p = s.find("\"assets\"");
  if (p == std::string::npos)
    return {};
  p = s.find('[', p);
  if (p == std::string::npos)
    return {};
  int depth = 0;
  bool in_string = false;
  for (size_t i = p; i < s.size(); ++i) {
    const char c = s[i];
    if (in_string) {
      if (c == '\\') ++i;
      else if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') in_string = true;
    else if (c == '[') ++depth;
    else if (c == ']' && --depth == 0)
      return s.substr(p, i - p + 1);
  }
  return {};
}

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Runs a command with no window, collecting its stdout. False only when it
// could not be started; its exit code comes back in `exit_code`.
bool RunCapture(const std::wstring& command_line, std::string& output, DWORD& exit_code) {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE read_end = nullptr, write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &sa, 0))
    return false;
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = write_end;
  si.hStdError = write_end;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  std::wstring cmd = command_line;   // CreateProcessW may write into it
  const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &si, &pi);
  CloseHandle(write_end);
  if (!ok) {
    CloseHandle(read_end);
    return false;
  }
  char buf[4096];
  DWORD got = 0;
  while (ReadFile(read_end, buf, sizeof(buf), &got, nullptr) && got > 0)
    output.append(buf, got);
  CloseHandle(read_end);
  WaitForSingleObject(pi.hProcess, INFINITE);
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

fs::path TarExe() {
  wchar_t sysdir[MAX_PATH] = {};
  GetSystemDirectoryW(sysdir, MAX_PATH);
  return fs::path(sysdir) / L"tar.exe";
}

std::string Quoted(const fs::path& p) { return "\"" + p.string() + "\""; }

UpdateOverlay* g_update_overlay = nullptr;

}  // namespace

int CompareVersions(const std::string& a, const std::string& b) {
  auto parts = [](const std::string& v) {
    std::vector<long> out;
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    while (i <= v.size()) {
      size_t j = v.find('.', i);
      if (j == std::string::npos)
        j = v.size();
      out.push_back(std::strtol(v.substr(i, j - i).c_str(), nullptr, 10));
      i = j + 1;
    }
    while (out.size() < 4)
      out.push_back(0);
    return out;
  };
  const auto pa = parts(a), pb = parts(b);
  for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
    const long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
    if (x != y)
      return x < y ? -1 : 1;
  }
  return 0;
}

std::string CurrentVersion() {
  if (const char* pretend = std::getenv("FABLE2_UPDATE_PRETEND_VERSION"); pretend && *pretend) {
    REXLOG_WARN("[update] FABLE2_UPDATE_PRETEND_VERSION={}: pretending this build is that version",
                pretend);
    return pretend;
  }
  return FABLE2_VERSION;
}

bool FetchLatestRelease(const std::string& repo, UpdateInfo& out, std::string& error, int timeout_ms) {
  std::string body;
  const std::string url = "https://api.github.com/repos/" + repo + "/releases/latest";
  if (!HttpGet(url, timeout_ms, true, &body, 1 << 20, nullptr, nullptr, error))
    return false;
  out = UpdateInfo{};
  out.tag = JsonString(body, "tag_name", 0, nullptr);
  if (out.tag.empty()) {
    error = "no tag_name in the answer";
    return false;
  }
  out.version = (out.tag[0] == 'v' || out.tag[0] == 'V') ? out.tag.substr(1) : out.tag;
  out.page_url = JsonString(body, "html_url", 0, nullptr);
  const std::string assets = AssetsArray(body);
  size_t pos = 0;
  for (;;) {
    size_t after = 0;
    const std::string name = JsonString(assets, "name", pos, &after);
    if (name.empty())
      break;
    const std::string url_here = JsonString(assets, "browser_download_url", after, nullptr);
    const uint64_t size = JsonNumber(assets, "size", after);
    if (EndsWith(name, "-win-amd64.zip") && name.rfind("fable2recomp-", 0) == 0) {
      out.asset_name = name;
      out.asset_url = url_here;
      out.asset_size = size;
      break;
    }
    pos = after;
  }
  if (out.asset_url.empty()) {
    error = "release " + out.tag + " has no win-amd64 zip";
    return false;
  }
  return true;
}

bool DownloadToFile(const std::string& url, const fs::path& path,
                    const std::function<void(uint64_t, uint64_t)>& progress,
                    const std::atomic<bool>* cancel, std::string& error) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const fs::path part = path.string() + ".part";
  std::ofstream out(part, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "cannot write " + part.string();
    return false;
  }
  uint64_t total = 0, done = 0;
  bool write_failed = false;
  // The total arrives with the headers, before the first chunk; HttpGet
  // fills it in as soon as it has it, so the bar knows its length.
  const bool ok = HttpGet(
      url, 15000, false, nullptr, 0,
      [&](const char* data, DWORD size) {
        if (cancel && cancel->load())
          return false;
        out.write(data, size);
        if (!out) {
          write_failed = true;
          return false;
        }
        done += size;
        if (progress)
          progress(done, total);
        return true;
      },
      &total, error);
  out.close();
  if (!ok) {
    if (write_failed)
      error = "disk write failed (out of space?)";
    fs::remove(part, ec);
    return false;
  }
  if (total && done != total) {
    error = "download incomplete";
    fs::remove(part, ec);
    return false;
  }
  fs::remove(path, ec);
  fs::rename(part, path, ec);
  if (ec) {
    error = "cannot place " + path.string();
    return false;
  }
  if (progress)
    progress(done, done);
  return true;
}

bool InstallReleaseZip(const fs::path& zip, const fs::path& install_dir,
                       const std::function<void(const std::string&)>& log, std::string& error) {
  std::error_code ec;
  const fs::path tar = TarExe();
  if (!fs::is_regular_file(tar, ec)) {
    error = "tar.exe is missing from Windows; unzip the release by hand";
    return false;
  }
  const fs::path update_dir = install_dir / "update";
  const fs::path stage = update_dir / "stage";
  fs::remove_all(stage, ec);
  fs::create_directories(stage, ec);

  std::string output;
  DWORD code = 1;
  const std::wstring cmd = Widen(Quoted(tar) + " -xf " + Quoted(zip) + " -C " + Quoted(stage));
  if (!RunCapture(cmd, output, code)) {
    error = "could not start tar.exe";
    return false;
  }
  if (code != 0) {
    error = "tar.exe failed (" + std::to_string(code) + "): " + output.substr(0, 200);
    fs::remove_all(stage, ec);
    return false;
  }
  if (!fs::is_regular_file(stage / "fable2.exe", ec)) {
    error = "the zip holds no fable2.exe";
    fs::remove_all(stage, ec);
    return false;
  }
  log("extracted " + zip.filename().string());

  // Move into place, the existing file aside first. The list is written
  // before the first move so a crash mid-way still leaves a record.
  std::vector<fs::path> renamed;   // relative paths whose .old exists
  std::vector<fs::path> placed;    // relative paths now holding new files
  auto rollback = [&]() {
    for (const auto& rel : placed)
      fs::remove(install_dir / rel, ec);
    for (const auto& rel : renamed) {
      const fs::path target = install_dir / rel;
      fs::remove(target, ec);
      fs::rename(fs::path(target.string() + ".old"), target, ec);
    }
    fs::remove(update_dir / "renamed.txt", ec);
    fs::remove_all(stage, ec);
  };

  std::vector<fs::path> files;
  for (fs::recursive_directory_iterator it(stage, ec), end; it != end; it.increment(ec)) {
    if (ec)
      break;
    if (!it->is_regular_file(ec))
      continue;
    const fs::path rel = fs::relative(it->path(), stage, ec);
    const std::string first = rel.begin() != rel.end() ? rel.begin()->string() : "";
    if (first == "game" || first == "dlc")
      continue;   // the placeholders for the player's own data
    files.push_back(rel);
  }
  {
    std::ofstream list(update_dir / "renamed.txt", std::ios::trunc);
    for (const auto& rel : files) {
      if (fs::exists(install_dir / rel, ec))
        list << rel.generic_string() << "\n";
    }
  }
  for (const auto& rel : files) {
    const fs::path target = install_dir / rel;
    fs::create_directories(target.parent_path(), ec);
    if (fs::exists(target, ec)) {
      const fs::path old = target.string() + ".old";
      fs::remove(old, ec);
      fs::rename(target, old, ec);
      if (ec) {
        error = "cannot move aside " + rel.generic_string() + ": " + ec.message();
        rollback();
        return false;
      }
      renamed.push_back(rel);
    }
    fs::rename(stage / rel, target, ec);
    if (ec) {
      error = "cannot place " + rel.generic_string() + ": " + ec.message();
      rollback();
      return false;
    }
    placed.push_back(rel);
  }
  fs::remove_all(stage, ec);
  log(std::to_string(placed.size()) + " files installed, " + std::to_string(renamed.size()) +
      " moved aside as .old");
  return true;
}

void CleanupAfterUpdate(const fs::path& install_dir) {
  const fs::path update_dir = install_dir / "update";
  std::error_code ec;
  if (!fs::is_directory(update_dir, ec))
    return;
  std::thread([update_dir, install_dir] {
    std::error_code ec2;
    std::vector<fs::path> leftovers;
    {
      std::ifstream list(update_dir / "renamed.txt");
      std::string line;
      while (std::getline(list, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
          line.pop_back();
        if (!line.empty())
          leftovers.push_back(install_dir / (line + ".old"));
      }
    }
    // The process that installed us is still closing when we start; its
    // exe and DLLs stay locked until it is gone. Ten tries over five seconds
    // outlast the exit watchdog.
    int remaining = 0;
    for (int attempt = 0; attempt < 10; ++attempt) {
      remaining = 0;
      for (const auto& p : leftovers) {
        if (fs::exists(p, ec2) && !fs::remove(p, ec2))
          ++remaining;
      }
      if (remaining == 0)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (remaining == 0)
      fs::remove(update_dir / "renamed.txt", ec2);
    int zips = 0;
    for (fs::directory_iterator it(update_dir, ec2), end; it != end; it.increment(ec2)) {
      if (ec2)
        break;
      const auto name = it->path().filename().string();
      if (EndsWith(name, ".zip") || EndsWith(name, ".part")) {
        if (fs::remove(it->path(), ec2))
          ++zips;
      }
    }
    fs::remove_all(update_dir / "stage", ec2);
    if (!leftovers.empty() || zips)
      REXLOG_INFO("[update] cleaned up after the last update: {} of {} .old files removed, {} zip(s)",
                  int(leftovers.size()) - remaining, int(leftovers.size()), zips);
    // An empty update folder is noise beside the game.
    if (fs::is_empty(update_dir, ec2))
      fs::remove(update_dir, ec2);
  }).detach();
}

bool LaunchDetached(const fs::path& exe, const std::string& args) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\"";
  if (!args.empty())
    cmd += L" " + Widen(args);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const std::wstring cwd = exe.parent_path().wstring();
  const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                 CREATE_NEW_PROCESS_GROUP | CREATE_DEFAULT_ERROR_MODE, nullptr,
                                 cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
  if (!ok) {
    REXLOG_ERROR("[update] could not start {}: error {}", exe.string(), GetLastError());
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

bool RelaunchSelf(const fs::path& exe_dir) {
  const fs::path exe = exe_dir / "fable2.exe";
  SetEnvironmentVariableA("FABLE2_WAIT_PID", std::to_string(GetCurrentProcessId()).c_str());
  const bool ok = LaunchDetached(exe, "");
  SetEnvironmentVariableA("FABLE2_WAIT_PID", nullptr);
  if (ok)
    REXLOG_INFO("[update] started the new {}; this process is quitting", exe.string());
  return ok;
}

void WaitForPreviousInstance() {
  const char* pid_text = std::getenv("FABLE2_WAIT_PID");
  if (!pid_text || !*pid_text)
    return;
  const DWORD pid = DWORD(std::strtoul(pid_text, nullptr, 10));
  SetEnvironmentVariableA("FABLE2_WAIT_PID", nullptr);   // not for our own children
  if (!pid || pid == GetCurrentProcessId())
    return;
  HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (!h)
    return;   // already gone
  const DWORD r = WaitForSingleObject(h, 10000);
  CloseHandle(h);
  REXLOG_INFO("[update] previous instance {} {}", pid,
              r == WAIT_OBJECT_0 ? "has exited" : "did not exit in 10 s; going on");
}

// ---------------------------------------------------------------------------

void SetUpdateOverlay(UpdateOverlay* overlay) { g_update_overlay = overlay; }
UpdateOverlay* GetUpdateOverlay() { return g_update_overlay; }

UpdateOverlay::UpdateOverlay(rex::ui::ImGuiDrawer* drawer, Fable2Settings* settings,
                             std::function<void()> on_restart)
    : rex::ui::ImGuiDialog(drawer), settings_(settings), on_restart_(std::move(on_restart)) {}

UpdateOverlay::~UpdateOverlay() {
  cancel_.store(true);
  JoinWorker();
}

void UpdateOverlay::JoinWorker() {
  if (worker_.joinable())
    worker_.join();
}

void UpdateOverlay::SetState(State s) { state_.store(s, std::memory_order_release); }

bool UpdateOverlay::Busy() const {
  const State s = GetState();
  return s == State::kChecking || s == State::kDownloading || s == State::kInstalling;
}

std::string UpdateOverlay::StatusLine() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

void UpdateOverlay::StartCheck(bool manual) {
  if (Busy())
    return;
  JoinWorker();
  cancel_.store(false);
  manual_.store(manual);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = "Checking...";
  }
  SetState(State::kChecking);
  const std::string skip = settings_ ? settings_->update_skip : std::string();
  worker_ = std::thread([this, manual, skip] {
    UpdateInfo info;
    std::string error;
    const std::string current = CurrentVersion();
    const bool ok = FetchLatestRelease(kUpdateRepo, info, error, 3000);
    std::string line;
    State next = State::kIdle;
    if (!ok) {
      REXLOG_INFO("[update] check skipped: {}", error);
      line = "Could not check: " + error;
      next = manual ? State::kUpToDate : State::kIdle;   // a line on the page, no panel
    } else if (CompareVersions(info.version, current) <= 0) {
      REXLOG_INFO("[update] up to date: this is v{}, the newest release is {}", current, info.tag);
      line = "Up to date (v" + current + ").";
      next = manual ? State::kUpToDate : State::kIdle;
    } else if (!manual && !skip.empty() && CompareVersions(info.version, skip) == 0) {
      REXLOG_INFO("[update] {} is available but skipped by the player", info.tag);
      line = "Version " + info.version + " is available (skipped).";
      next = State::kIdle;
    } else {
      REXLOG_INFO("[update] {} is available ({} bytes); this is v{}", info.tag, info.asset_size,
                  current);
      line = "Version " + info.version + " is available.";
      next = State::kAvailable;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      info_ = info;
      error_ = error;
      status_ = line;
    }
    SetState(next);
  });
}

void UpdateOverlay::StartDownload() {
  JoinWorker();
  cancel_.store(false);
  done_.store(0);
  total_.store(0);
  SetState(State::kDownloading);
  UpdateInfo info;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    info = info_;
  }
  worker_ = std::thread([this, info] {
    const fs::path install_dir = rex::filesystem::GetExecutableFolder();
    const fs::path zip = install_dir / "update" / info.asset_name;
    std::string error;
    REXLOG_INFO("[update] downloading {} to {}", info.asset_url, zip.string());
    total_.store(info.asset_size);
    const bool got = DownloadToFile(
        info.asset_url, zip,
        [this](uint64_t done, uint64_t total) {
          done_.store(done);
          if (total)
            total_.store(total);
        },
        &cancel_, error);
    if (!got) {
      REXLOG_WARN("[update] download failed: {}", error);
      std::lock_guard<std::mutex> lock(mutex_);
      error_ = cancel_.load() ? "Download cancelled." : "Download failed: " + error;
      status_ = error_;
      SetState(cancel_.load() ? State::kIdle : State::kFailed);
      return;
    }
    if (info.asset_size) {
      std::error_code ec;
      const auto size = fs::file_size(zip, ec);
      if (ec || size != info.asset_size) {
        REXLOG_WARN("[update] {} is {} bytes, the release says {}", zip.string(), size, info.asset_size);
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = "The download is not the size the release lists; not installing it.";
        status_ = error_;
        SetState(State::kFailed);
        return;
      }
    }
    SetState(State::kInstalling);
    const bool installed = InstallReleaseZip(
        zip, install_dir, [](const std::string& line) { REXLOG_INFO("[update] {}", line); }, error);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!installed) {
      REXLOG_WARN("[update] install failed: {}", error);
      error_ = "Install failed: " + error;
      status_ = error_;
      SetState(State::kFailed);
      return;
    }
    status_ = "Version " + info.version + " is installed; restart to use it.";
    SetState(State::kInstalled);
  });
}

void UpdateOverlay::OnDraw(ImGuiIO& io) {
  const State state = GetState();
  if (state == State::kIdle || state == State::kChecking || state == State::kUpToDate)
    return;
  UpdateInfo info;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    info = info_;
    error = error_;
  }
  const std::string current = CurrentVersion();

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.96f);
  if (ImFont* f = Fonts().body)
    ImGui::PushFont(f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_AlwaysAutoResize;
  if (ImGui::Begin("Fable II - Update", nullptr, flags)) {
    switch (state) {
      case State::kAvailable: {
        ImGui::TextWrapped("Version %s of the port is available. You have v%s.", info.version.c_str(),
                           current.c_str());
        ImGui::TextDisabled("%s, %.1f MB. Your game, saves, settings and texture pack are not touched.",
                            info.asset_name.c_str(), double(info.asset_size) / (1024.0 * 1024.0));
        ImGui::Spacing();
        if (ImGui::Button("Update now", ImVec2(130.0f, 0.0f)))
          StartDownload();
        ImGui::SameLine();
        if (ImGui::Button("Not now", ImVec2(110.0f, 0.0f)))
          SetState(State::kIdle);
        ImGui::SameLine();
        if (ImGui::Button("Skip this version", ImVec2(150.0f, 0.0f))) {
          if (settings_) {
            settings_->update_skip = info.version;
            settings_->Save();
          }
          SetState(State::kIdle);
        }
        if (!info.page_url.empty()) {
          ImGui::SameLine();
          if (ImGui::SmallButton("Release notes"))
            ShellExecuteW(nullptr, L"open", Widen(info.page_url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
      }
      case State::kDownloading: {
        const uint64_t done = done_.load(), total = total_.load();
        ImGui::TextUnformatted("Downloading the update...");
        char text[64];
        std::snprintf(text, sizeof(text), "%.1f / %.1f MB", double(done) / (1024.0 * 1024.0),
                      double(total) / (1024.0 * 1024.0));
        ImGui::ProgressBar(total ? float(double(done) / double(total)) : 0.0f, ImVec2(-1.0f, 0.0f),
                           total ? text : "...");
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
          cancel_.store(true);
        break;
      }
      case State::kInstalling:
        ImGui::TextUnformatted("Installing...");
        ImGui::TextDisabled("The running files are moved aside; the game keeps running until you restart.");
        break;
      case State::kInstalled:
        ImGui::TextWrapped("Version %s is installed. Restart the game to use it.", info.version.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Restart now", ImVec2(130.0f, 0.0f))) {
          if (on_restart_)
            on_restart_();
        }
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(110.0f, 0.0f)))
          SetState(State::kIdle);
        break;
      case State::kFailed:
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::TextDisabled("Nothing was changed. The release can also be downloaded by hand.");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(110.0f, 0.0f)))
          SetState(State::kIdle);
        if (!info.page_url.empty()) {
          ImGui::SameLine();
          if (ImGui::SmallButton("Open the release page"))
            ShellExecuteW(nullptr, L"open", Widen(info.page_url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
      default:
        break;
    }
  }
  ImGui::End();
  if (Fonts().body)
    ImGui::PopFont();
}

}  // namespace fable2
