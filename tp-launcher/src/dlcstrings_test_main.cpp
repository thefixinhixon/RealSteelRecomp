// dlcstrings_test - merge two Real Steel string tables like the launcher does.
//   dlcstrings_test <old SDB table> <newer table> <output>
//   dlcstrings_test --fix <user data root> [title id]
//   dlcstrings_test --import <package|archive|folder> <user data root>
//     (what the launcher's Import DLC button does)
#include "dlcimport.h"
#include "dlcstrings.h"
#include "titleupdate.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout), err(stderr);
    const QStringList args = app.arguments();
    if (args.size() >= 3 && args[1] == QStringLiteral("--fix")) {
        dlcstrings::Result result;
        QString error;
        const bool ok = dlcstrings::fixDlcStringTables(
            args[2], args.size() > 3 ? args[3] : QStringLiteral("584111E0"), &result, &error);
        for (const QString &line : result.notes)
            out << line << "\n";
        if (!ok)
            err << error << "\n";
        return ok ? 0 : 1;
    }
    if (args.size() == 4 && args[1] == QStringLiteral("--tu")) {
        // --tu <game data root> <user data root>: Real Steel TU 2 lookup.
        const titleupdate::Found tu = titleupdate::find(
            args[2], args[3], QStringLiteral("584111E0"), 0x205u, 0x5u);
        out << "match: " << (tu.xexpPath.isEmpty() ? QStringLiteral("-") : tu.xexpPath) << "\n"
            << "other: " << (tu.otherVersions.isEmpty() ? QStringLiteral("-") : tu.otherVersions)
            << "\n";
        return tu.xexpPath.isEmpty() ? 1 : 0;
    }
    if (args.size() == 4 && args[1] == QStringLiteral("--import")) {
        dlcimport::Outcome outcome;
        QString error;
        dlcimport::importDlc(args[2], args[3], QStringLiteral("584111E0"),
                             [&](const QString &name) { out << "installing " << name << "\n"; out.flush(); },
                             &outcome, &error);
        out << "installed " << outcome.installed << "\n";
        for (const QString &s : outcome.skipped)
            out << "skipped " << s << "\n";
        if (outcome.installed == 0) {
            err << error << "\n";
            return 1;
        }
        dlcstrings::Result result;
        if (!dlcstrings::fixDlcStringTables(args[3], QStringLiteral("584111E0"), &result, &error)) {
            err << error << "\n";
            return 1;
        }
        for (const QString &line : result.notes)
            out << line << "\n";
        return 0;
    }
    if (args.size() != 4) {
        err << "usage: dlcstrings_test <old> <newer> <out> | --fix <root> [title] | --tu <gamedata> <userdata>\n";
        return 2;
    }
    QFile a(args[1]), b(args[2]);
    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) {
        err << "cannot read inputs\n";
        return 1;
    }
    QByteArray merged;
    int filled = 0;
    QString error;
    if (!dlcstrings::mergeTables(a.readAll(), b.readAll(), &merged, &filled, &error)) {
        err << error << "\n";
        return 1;
    }
    QFile o(args[3]);
    if (!o.open(QIODevice::WriteOnly) || o.write(merged) != merged.size()) {
        err << "cannot write output\n";
        return 1;
    }
    out << "filled " << filled << " strings, " << merged.size() << " bytes\n";
    return 0;
}
