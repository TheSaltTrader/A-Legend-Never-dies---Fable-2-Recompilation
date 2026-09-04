// fable2 - ReXGlue Recompiled Project
//
// Fable II (Lionhead / Microsoft, 2008), GOTY disc build 0.0.0.26,
// title 4D5307F1, media 716F0A0D.

#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

class Fable2App : public rex::ReXApp {
 public:
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

  void OnPreLaunchModule() override {
    REXLOG_INFO("fable2: launching guest module");
  }

  void OnGuestThreadExit(rex::system::XThread* thread) override {
    (void)thread;
    REXLOG_INFO("fable2: main guest thread exited");
  }
};
