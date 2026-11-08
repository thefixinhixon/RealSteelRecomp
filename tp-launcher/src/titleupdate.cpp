// titleupdate.cpp - see titleupdate.h.
#include "titleupdate.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace titleupdate {

namespace {

quint32 be32(const QByteArray &d, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData()) + at;
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
}

} // namespace

QString versionString(quint32 v)
{
    return QStringLiteral("%1.%2.%3.%4")
        .arg(v >> 28)
        .arg((v >> 24) & 0xF)
        .arg((v >> 8) & 0xFFFF)
        .arg(v & 0xFF);
}

bool readPatchVersions(const QString &xexpPath, quint32 *target, quint32 *source)
{
    QFile f(xexpPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(0x1000);
    if (head.size() < 24 || !head.startsWith("XEX2"))
        return false;
    const quint32 count = be32(head, 20);
    for (quint32 i = 0; i < count && 24 + qsizetype(i) * 8 + 8 <= head.size(); ++i) {
        const quint32 key = be32(head, 24 + i * 8);
        const quint32 off = be32(head, 28 + i * 8);
        if (key == 0x000005FF && qsizetype(off) + 12 <= head.size()) {
            *target = be32(head, off + 4);
            *source = be32(head, off + 8);
            return true;
        }
    }
    return false;
}

Found find(const QString &gameDataRoot, const QString &userDataRoot, const QString &titleId,
           quint32 targetVersion, quint32 baseVersion)
{
    Found found;
    QStringList candidates;
    const QString besideXex =
        gameDataRoot.isEmpty() ? QString() : QDir(gameDataRoot).filePath(QStringLiteral("default.xexp"));
    if (!besideXex.isEmpty())
        candidates << besideXex;
    if (!userDataRoot.isEmpty()) {
        const QDir tuDir(QDir(userDataRoot).filePath(
            QStringLiteral("0000000000000000/%1/000B0000").arg(titleId)));
        for (const QString &pkg : tuDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            candidates << tuDir.filePath(pkg + QStringLiteral("/default.xexp"));
    }
    QStringList others;
    for (const QString &path : candidates) {
        if (!QFileInfo(path).isFile())
            continue;
        quint32 target = 0, source = 0;
        if (!readPatchVersions(path, &target, &source))
            continue;
        if (target == targetVersion && source == baseVersion) {
            found.xexpPath = path;
            break;
        }
        others << versionString(target);
        // A non-matching patch next to default.xex is still applied by the
        // runtime, so it decides the outcome: stop here.
        if (path == besideXex)
            break;
    }
    others.removeDuplicates();
    found.otherVersions = others.join(QStringLiteral(", "));
    return found;
}

} // namespace titleupdate
