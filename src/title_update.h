// title_update.h - run Real Steel with its title update (TU 2, v0.0.2.5).
//
// Used by both executables: realsteel (base game, v0.0.0.5) only checks
// that no patch was applied; realsteel-tu (REALSTEEL_TITLE_UPDATE) finds
// and requires the title update.
//
// The code in generated/ was recompiled from default.xex with the title
// update's default.xexp applied, so at run time the same patch must be
// applied to the image the runtime loads (its data sections, strings and
// tables must match the code). The runtime applies game:\default.xexp, or
// failing that update:\default.xexp.
//
// ConfigureTitleUpdatePaths() points update:\ at an installed title
// update when none was given: the folder an Xbox 360 content import puts
// it in, <user data>/0000000000000000/584111E0/000B0000/<package>/, picking
// the package whose default.xexp produces exactly kTitleUpdateVersion.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

namespace realsteel {

// XEX version fields: major.minor.build.qfe = 4.4.16.8 bits.
inline constexpr uint32_t kBaseVersion = 0x00000005;         // 0.0.0.5
inline constexpr uint32_t kTitleUpdateVersion = 0x00000205;  // 0.0.2.5
inline constexpr const char* kTitleId = "584111E0";

inline std::string VersionString(uint32_t v) {
  return fmt::format("{}.{}.{}.{}", v >> 28, (v >> 24) & 0xF, (v >> 8) & 0xFFFF, v & 0xFF);
}

inline uint32_t ReadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Target and source version of an XEX delta patch (header 0x5FF), or
// nullopt if the file is not one.
struct PatchVersions {
  uint32_t target;
  uint32_t source;
};
inline std::optional<PatchVersions> ReadPatchVersions(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> head(0x1000);
  in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
  head.resize(size_t(in.gcount()));
  if (head.size() < 24 || std::string_view(reinterpret_cast<char*>(head.data()), 4) != "XEX2")
    return std::nullopt;
  uint32_t count = ReadBE32(&head[20]);
  for (uint32_t i = 0; i < count && 24 + i * 8 + 8 <= head.size(); ++i) {
    uint32_t key = ReadBE32(&head[24 + i * 8]);
    uint32_t off = ReadBE32(&head[28 + i * 8]);
    if (key == 0x000005FF && off + 12 <= head.size())
      return PatchVersions{ReadBE32(&head[off + 4]), ReadBE32(&head[off + 8])};
  }
  return std::nullopt;
}

inline bool PatchMatches(const std::filesystem::path& xexp) {
  auto v = ReadPatchVersions(xexp);
  return v && v->target == kTitleUpdateVersion && v->source == kBaseVersion;
}

inline void ConfigureTitleUpdatePaths(rex::PathConfig& paths) {
  if (!paths.update_data_root.empty()) return;  // --update_data_root given
  std::error_code ec;
  if (!paths.game_data_root.empty() &&
      std::filesystem::is_regular_file(paths.game_data_root / "default.xexp", ec))
    return;  // a patch next to default.xex is used as is
  auto dir = paths.user_data_root / "0000000000000000" / kTitleId / "000B0000";
  for (auto it = std::filesystem::directory_iterator(dir, ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    auto xexp = it->path() / "default.xexp";
    if (it->is_directory(ec) && PatchMatches(xexp)) {
      paths.update_data_root = it->path();
      REXLOG_INFO("Title update {}: {}", VersionString(kTitleUpdateVersion),
                  it->path().string());
      return;
    }
  }
}

inline std::string MissingTitleUpdateMessage(const std::string& detail) {
  return fmt::format(
      "Real Steel needs its title update (TU 2, version {}).\n\n{}\n\n"
      "Import the title update package (TU_...) with the launcher's "
      "\"Import DLC or title update...\" button, or copy its default.xexp "
      "next to default.xex.",
      VersionString(kTitleUpdateVersion), detail);
}

inline bool CheckTitleUpdatePresent(const rex::PathConfig& paths) {
  std::error_code ec;
  std::filesystem::path xexp;
  if (!paths.game_data_root.empty() &&
      std::filesystem::is_regular_file(paths.game_data_root / "default.xexp", ec))
    xexp = paths.game_data_root / "default.xexp";
  else if (!paths.update_data_root.empty())
    xexp = paths.update_data_root / "default.xexp";
  std::string detail;
  if (xexp.empty() || !std::filesystem::is_regular_file(xexp, ec)) {
    detail = "No title update was found.";
  } else if (auto v = ReadPatchVersions(xexp); !v) {
    detail = fmt::format("{} is not a title update patch.", xexp.string());
  } else if (v->target != kTitleUpdateVersion || v->source != kBaseVersion) {
    detail = fmt::format("{} is title update version {} (for {}).", xexp.string(),
                         VersionString(v->target), VersionString(v->source));
  } else {
    return true;
  }
  auto msg = MissingTitleUpdateMessage(detail);
  REXLOG_ERROR("{}", msg);
  rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
  return false;
}

// The generated code matches exactly one executable version; anything
// else would run the wrong code over the image's data.
inline bool CheckLoadedVersion(rex::Runtime* runtime, uint32_t expected) {
  auto module = runtime ? runtime->kernel_state()->GetExecutableModule() : nullptr;
  auto* info = module && module->xex_module() ? module->xex_module()->opt_execution_info()
                                              : nullptr;
  uint32_t version = info ? uint32_t(info->version_value) : 0;
  if (version == expected) return true;
  std::string msg;
  if (expected == kTitleUpdateVersion) {
    msg = MissingTitleUpdateMessage(fmt::format(
        "The loaded executable is version {}; the patch was not applied.",
        VersionString(version)));
  } else {
    msg = fmt::format(
        "This is the base-game build of Real Steel (version {}), but the loaded "
        "executable is version {}: a title update patch (default.xexp) was "
        "applied.\n\nStart realsteel-tu instead (the launcher does this when the "
        "title update is installed), or remove default.xexp from the game folder.",
        VersionString(expected), VersionString(version));
  }
  REXLOG_ERROR("{}", msg);
  rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
  return false;
}

}  // namespace realsteel
