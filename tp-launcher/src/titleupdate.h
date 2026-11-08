// titleupdate.h - find an installed Xbox 360 title update for a game.
//
// A title update ships default.xexp, a delta patch for default.xex. A
// recomp that has a build for the patched executable (GameProfile::
// titleUpdateExeName) needs exactly the patch it was recompiled from:
// one whose header says "turns <base> into <target>". It is looked for
// where the game finds it (src/title_update.h on the game side):
//   1. <game data>/default.xexp (next to default.xex), then
//   2. <user data>/0000000000000000/<title id>/000B0000/*/default.xexp,
//      the folder the launcher's package import installs a TU to.
// QtCore only.
#pragma once

#include <QString>

namespace titleupdate {

struct Found {
    QString xexpPath;      // The matching default.xexp ("" = none)
    QString otherVersions; // Patches found that do not match, for messages
};

// XEX version value -> "a.b.c.d" (major.minor.build.qfe, 4.4.16.8 bits).
QString versionString(quint32 version);

// Reads a delta patch's target/source versions; false if not a patch.
bool readPatchVersions(const QString &xexpPath, quint32 *target, quint32 *source);

Found find(const QString &gameDataRoot, const QString &userDataRoot, const QString &titleId,
           quint32 targetVersion, quint32 baseVersion);

} // namespace titleupdate
