// realsteel - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>

#include "title_update.h"

class RealSteelApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<RealSteelApp>(new RealSteelApp(ctx, "realsteel",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  void OnPostSetup() override {
      // AMD/RADV fix: unclipped draw shaders cause blank screen during
      // gameplay on AMD GPUs. Execute them on CPU instead.
      // (Verified required for Real Steel: skipping it = black screens.)
      rex::cvar::SetFlagByName("execute_unclipped_draw_vs_on_cpu", "true");
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

#ifdef REALSTEEL_TITLE_UPDATE
  // Title update: find the installed TU and mount it as update:\\ (the
  // runtime applies update:\\default.xexp to game:\\default.xex).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    realsteel::ConfigureTitleUpdatePaths(paths);
  }
  static constexpr uint32_t kExpectedVersion = realsteel::kTitleUpdateVersion;
#else
  static constexpr uint32_t kExpectedVersion = realsteel::kBaseVersion;
#endif

  // The generated code is for one exact executable version: refuse to
  // start on anything else instead of crashing somewhere later.
  bool ConstructRuntime(const rex::PathConfig& paths) override {
#ifdef REALSTEEL_TITLE_UPDATE
    if (!realsteel::CheckTitleUpdatePresent(paths)) return false;
#endif
    if (!rex::ReXApp::ConstructRuntime(paths)) return false;
    return realsteel::CheckLoadedVersion(runtime(), kExpectedVersion);
  }
};
