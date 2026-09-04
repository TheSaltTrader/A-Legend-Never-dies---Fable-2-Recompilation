// fable2 - ReXGlue Recompiled Project
//
// Fable II (Lionhead / Microsoft, 2008), GOTY disc build 0.0.0.26,
// title 4D5307F1, media 716F0A0D.

#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include <filesystem>

#include "fable2_dlc.h"

// Where to look for Xbox 360 content packages. Empty (the default) means do
// nothing at all: the two Fable II expansions are already on the GOTY disc, so
// installing them is a gigabyte of wasted work. tools/run.cmd passes the
// project's DLC folder.
REXCVAR_DEFINE_STRING(dlc_root, "", "Content",
                      "Folder of Xbox 360 content packages to install (DLC)");

class Fable2App : public rex::ReXApp {
 public:
  // Fable II, from the XEX's own execution-info header.
  static constexpr uint32_t kTitleId = 0x4D5307F1;

  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fable2App>(new Fable2App(ctx, "fable2",
        PPCImageConfig));
  }

  // Select the Xenos GPU emulation plugin.
  //
  // This is a RuntimeConfig FIELD, not a cvar - there is no --gpu_plugin to
  // set instead. Without it the runtime comes up in "native rendering mode",
  // silently ignores every Vd* kernel call, and the guest never gets a ring
  // buffer: no error, just a black window. CMakeLists stages rexgpu-xenos.dll
  // next to the executable via GPU_PLUGINS.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";
  }

  // Boot progress markers. The runtime does not log a successful file open,
  // so during bring-up the only cheap signal that the guest is making
  // progress is where it gets to - and absence of logging is not absence of
  // behaviour.
  void OnPostLoadXexImage() override {
    REXLOG_INFO("fable2: XEX image loaded");
  }

  // Install any content packages once the runtime (and so the content
  // manager) exists, before the guest launches and enumerates its DLC.
  void OnPostSetup() override {
    const std::string root = REXCVAR_GET(dlc_root);
    if (root.empty()) return;
    fable2::InstallPackages(runtime()->kernel_state()->content_manager(),
                            std::filesystem::path(root), kTitleId);
  }

  void OnPreLaunchModule() override {
    REXLOG_INFO("fable2: launching guest module");
  }

  void OnGuestThreadExit(rex::system::XThread* thread) override {
    (void)thread;
    REXLOG_INFO("fable2: main guest thread exited");
  }
};
