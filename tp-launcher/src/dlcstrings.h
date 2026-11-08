// dlcstrings.h - give Real Steel's later DLC parts their real names.
//
// Real Steel's DLC add-ons each ship a string table (pac/string.pac).
// The base game reads only the original "SDB " table format, so it
// takes its text from add-on 10/11.x; the parts sold by add-ons 14-21
// then show placeholders such as "Text10900" (their text lives only in
// those add-ons' newer "SDBP"/"SDB2" tables, which the game's title
// update reads).
//
// fixDlcStringTables() fills those placeholder IDs in every installed
// old-format table with the newest table's text, keeping each table's
// structure, IDs and section positions exactly as the game expects.
// The original of each patched table is kept next to it as
// string.pac.orig and is always the source, so running this again
// (every launch) is safe and picks up newly installed add-ons.
//
// This is a C++ port of tools/dlc/merge_string_tables.py, which holds
// the format notes and the reasoning; the two produce identical files.
// QtCore only.
#pragma once

#include <QString>
#include <QStringList>

namespace dlcstrings {

struct Result {
    int tablesPatched = 0;  // Old-format tables rewritten
    int stringsFilled = 0;  // Placeholder strings filled, all tables
    QString sourceTable;    // Newer table the text came from
    QStringList notes;      // Human-readable log lines
};

// Scan <userDataRoot>/0000000000000000/<titleId>/00000002/*/pac/string.pac.
// Returns false only on a write error; "nothing to do" is success.
bool fixDlcStringTables(const QString &userDataRoot, const QString &titleId,
                        Result *result, QString *error = nullptr);

// Rebuild one table (exposed for the command-line test harness).
bool mergeTables(const QByteArray &oldTable, const QByteArray &newTable,
                 QByteArray *out, int *filled, QString *error);

} // namespace dlcstrings
