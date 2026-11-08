// dlcstrings.cpp - see dlcstrings.h and tools/dlc/merge_string_tables.py.
#include "dlcstrings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QPair>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

namespace {

constexpr int kIndexOffset = 0x800;
constexpr int kDataOffset = 0x4000;
constexpr int kSector = 0x800;
constexpr int kBpeBlock = 2048;  // Matches the original encoder's ratio
constexpr int kBpeMaxStack = 20; // Game decoder's fixed expansion stack
const QByteArray kTextSectionPrefix("56");
const char *const kDescriptionPrefixes[] = {"(Purchased", "(Default", "(Initial"};
// Bump when the output changes, so stamped tables are rebuilt.
constexpr int kFixVersion = 1;

quint32 rd32(const QByteArray &b, qsizetype off)
{
    quint32 v;
    std::memcpy(&v, b.constData() + off, 4);
    return qFromLittleEndian(v);
}

void put32(QByteArray &b, quint32 v)
{
    const quint32 le = qToLittleEndian(v);
    b.append(reinterpret_cast<const char *>(&le), 4);
}

void set32(QByteArray &b, qsizetype off, quint32 v)
{
    const quint32 le = qToLittleEndian(v);
    std::memcpy(b.data() + off, &le, 4);
}

// --- byte-pair encoding (Gage's format, little-endian block length) -----------

bool bpeExpand(const QByteArray &buf, quint32 dsize, QByteArray *out)
{
    out->clear();
    out->reserve(dsize);
    const auto *p = reinterpret_cast<const quint8 *>(buf.constData());
    const qsizetype n = buf.size();
    qsizetype i = 0;
    std::array<quint8, 256> left{}, right{};
    std::vector<quint8> stack;
    while (i < n && quint32(out->size()) < dsize) {
        for (int c = 0; c < 256; ++c)
            left[c] = quint8(c);
        int c = 0;
        for (;;) {
            if (i >= n)
                return false;
            int count = p[i++];
            if (count > 127) {
                c += count - 127;
                count = 0;
            }
            if (c == 256)
                break;
            for (int k = 0; k <= count; ++k) {
                if (c >= 256 || i >= n)
                    return false;
                left[c] = p[i++];
                if (left[c] != c) {
                    if (i >= n)
                        return false;
                    right[c] = p[i++];
                }
                ++c;
            }
            if (c == 256)
                break;
        }
        if (i + 2 > n)
            return false;
        int size = p[i] | (p[i + 1] << 8);
        i += 2;
        stack.clear();
        for (;;) {
            int code;
            if (!stack.empty()) {
                code = stack.back();
                stack.pop_back();
            } else {
                if (size == 0)
                    break;
                --size;
                if (i >= n)
                    return false;
                code = p[i++];
            }
            if (code == left[code]) {
                out->append(char(code));
            } else {
                stack.push_back(right[code]);
                stack.push_back(left[code]);
            }
        }
    }
    return quint32(out->size()) == dsize;
}

QByteArray bpeBlock(const QByteArray &block)
{
    std::vector<int> data(block.begin(), block.end());
    for (int &v : data)
        v &= 0xFF;
    std::array<int, 256> left{}, right{}, need{};
    std::array<bool, 256> used{};
    for (int c = 0; c < 256; ++c) {
        left[c] = c;
        need[c] = 1;
    }
    for (int v : data)
        used[v] = true;

    std::vector<int> count(65536), first(65536);
    for (;;) {
        int freeCode = -1;
        for (int c = 0; c < 256; ++c)
            if (!used[c]) {
                freeCode = c;
                break;
            }
        if (freeCode < 0)
            break;
        // Pair counts, and each pair's first position (the Python tool's
        // dict keeps first-occurrence order; ties go to the earliest pair).
        std::fill(count.begin(), count.end(), 0);
        std::vector<int> seen;
        for (size_t k = 0; k + 1 < data.size(); ++k) {
            const int key = (data[k] << 8) | data[k + 1];
            if (count[key]++ == 0) {
                first[key] = int(seen.size());
                seen.push_back(key);
            }
        }
        std::stable_sort(seen.begin(), seen.end(),
                         [&](int a, int b) { return count[a] > count[b]; });
        int best = -1;
        for (int key : seen) {
            if (count[key] < 3)
                break;
            const int a = key >> 8, b = key & 0xFF;
            if (std::max(1 + need[a], need[b]) <= kBpeMaxStack) {
                best = key;
                break;
            }
        }
        if (best < 0)
            break;
        const int a = best >> 8, b = best & 0xFF;
        need[freeCode] = std::max(1 + need[a], need[b]);
        std::vector<int> next;
        next.reserve(data.size());
        for (size_t k = 0; k < data.size();) {
            if (k + 1 < data.size() && data[k] == a && data[k + 1] == b) {
                next.push_back(freeCode);
                k += 2;
            } else {
                next.push_back(data[k]);
                ++k;
            }
        }
        data.swap(next);
        left[freeCode] = a;
        right[freeCode] = b;
        used[freeCode] = true;
    }

    QByteArray table;
    int c = 0;
    while (c < 256) {
        int skip = 0;
        while (c + skip < 256 && left[c + skip] == c + skip && skip < 128)
            ++skip;
        int run;
        if (skip) {
            table.append(char(127 + skip));
            c += skip;
            if (c == 256)
                break;
            run = 1;
        } else {
            run = 0;
            while (c + run < 256 && left[c + run] != c + run && run < 128)
                ++run;
            table.append(char(run - 1));
        }
        for (int code = c; code < c + run; ++code) {
            table.append(char(left[code]));
            if (left[code] != code)
                table.append(char(right[code]));
        }
        c += run;
    }
    table.append(char(data.size() & 0xFF));
    table.append(char((data.size() >> 8) & 0xFF));
    for (int v : data)
        table.append(char(v));
    return table;
}

QByteArray bpeCompress(const QByteArray &raw)
{
    QByteArray out;
    for (qsizetype i = 0; i < raw.size(); i += kBpeBlock)
        out += bpeBlock(raw.mid(i, kBpeBlock));
    return out;
}

// --- containers ----------------------------------------------------------------

struct Entry {
    quint32 id;
    quint32 flags;
    QByteArray text;
};

struct Section {
    QByteArray gid; // 4 chars
    quint32 count;
    QByteArray blob;
};

struct Table {
    QByteArray magic;
    quint32 slotCount = 0;
    QByteArray data;
    std::vector<Section> sections;
};

bool readTable(const QByteArray &data, Table *t, QString *error)
{
    if (data.size() < kDataOffset || !data.startsWith("EPAC")) {
        *error = QStringLiteral("not an EPAC archive");
        return false;
    }
    t->data = data;
    t->magic = data.mid(kIndexOffset, 4);
    if (t->magic != "SDB " && t->magic != "SDBP" && t->magic != "SDB2") {
        *error = QStringLiteral("unknown string database");
        return false;
    }
    t->slotCount = rd32(data, kIndexOffset + 4);
    struct Raw {
        QByteArray gid;
        quint32 sector, count;
    };
    std::vector<Raw> entries;
    qsizetype pos = kIndexOffset + 12;
    for (quint32 i = 0; i < t->slotCount; ++i) {
        if (pos + 12 > data.size()) {
            *error = QStringLiteral("truncated index");
            return false;
        }
        const QByteArray gid = data.mid(pos, 4);
        const quint32 sector = rd32(data, pos + 4), count = rd32(data, pos + 8);
        pos += 12;
        if (gid != QByteArray(4, '\0'))
            entries.push_back({gid, sector, count});
    }
    std::vector<quint32> starts;
    for (const auto &e : entries)
        starts.push_back(e.sector);
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    const quint32 endSector = quint32((data.size() - kDataOffset) / kSector);
    for (const auto &e : entries) {
        auto later = std::upper_bound(starts.begin(), starts.end(), e.sector);
        const quint32 stop = later != starts.end() ? *later : endSector;
        const qsizetype from = kDataOffset + qsizetype(e.sector) * kSector;
        const qsizetype to = kDataOffset + qsizetype(stop) * kSector;
        if (to > data.size() || from >= to) {
            *error = QStringLiteral("section outside the file");
            return false;
        }
        const QByteArray blob = data.mid(from, to - from);
        if (!blob.startsWith("PACH")) {
            *error = QStringLiteral("section is not PACH");
            return false;
        }
        t->sections.push_back({e.gid, e.count, blob});
    }
    return true;
}

std::vector<std::pair<quint32, QByteArray>> readPach(const QByteArray &blob)
{
    std::vector<std::pair<quint32, QByteArray>> subs;
    const quint32 n = rd32(blob, 4);
    const qsizetype header = 8 + 12 * qsizetype(n);
    for (quint32 i = 0; i < n; ++i) {
        const quint32 index = rd32(blob, 8 + 12 * i);
        const quint32 offset = rd32(blob, 8 + 12 * i + 4);
        const quint32 size = rd32(blob, 8 + 12 * i + 8);
        subs.emplace_back(index, blob.mid(header + offset, size));
    }
    return subs;
}

QByteArray writePach(const std::vector<std::pair<quint32, QByteArray>> &subs)
{
    QByteArray header("PACH");
    put32(header, quint32(subs.size()));
    QByteArray body;
    for (const auto &[index, sub] : subs) {
        put32(header, index);
        put32(header, quint32(body.size()));
        put32(header, quint32(sub.size()));
        body += sub;
        body.append(QByteArray((4 - body.size() % 4) % 4, '\0'));
    }
    QByteArray blob = header + body;
    blob.append(QByteArray((kSector - blob.size() % kSector) % kSector, '\0'));
    return blob;
}

bool decodeSub(const QByteArray &sub, QByteArray *raw, std::vector<Entry> *strings)
{
    if (!sub.startsWith("BPE ") || sub.size() < 16)
        return false;
    const quint32 csize = rd32(sub, 8), dsize = rd32(sub, 12);
    if (!bpeExpand(sub.mid(16, csize), dsize, raw) || raw->size() < 8)
        return false;
    const quint32 n = rd32(*raw, 4);
    strings->clear();
    for (quint32 i = 0; i < n; ++i) {
        const qsizetype e = 8 + 16 * qsizetype(i);
        if (e + 16 > raw->size())
            return false;
        const quint32 off = rd32(*raw, e), len = rd32(*raw, e + 4);
        strings->push_back({rd32(*raw, e + 8), rd32(*raw, e + 12), raw->mid(off, len)});
    }
    return true;
}

QByteArray encodeSub(const std::vector<Entry> &strings, QByteArray *rawOut)
{
    QByteArray table;
    put32(table, 0);
    put32(table, quint32(strings.size()));
    QByteArray text;
    const quint32 base = 8 + 16 * quint32(strings.size());
    QHash<QByteArray, quint32> seen; // Identical strings are stored once
    for (const Entry &e : strings) {
        quint32 offset;
        auto it = seen.constFind(e.text);
        if (it != seen.constEnd()) {
            offset = *it;
        } else {
            offset = base + quint32(text.size());
            seen.insert(e.text, offset);
            text += e.text;
        }
        put32(table, offset);
        put32(table, quint32(e.text.size()));
        put32(table, e.id);
        put32(table, e.flags);
    }
    *rawOut = table + text;
    const QByteArray packed = bpeCompress(*rawOut);
    QByteArray sub("BPE ");
    put32(sub, 0x100);
    put32(sub, quint32(packed.size()));
    put32(sub, quint32(rawOut->size()));
    return sub + packed;
}

QByteArray stripNul(const QByteArray &s)
{
    qsizetype end = s.size();
    while (end > 0 && s[end - 1] == '\0')
        --end;
    return s.left(end);
}

bool isEmptyText(const QByteArray &s)
{
    for (char ch : s)
        if (ch != '\0')
            return false;
    return true;
}

bool isPlaceholder(quint32 id, const QByteArray &s)
{
    return stripNul(s) == "Text" + QByteArray::number(id);
}

bool isDescription(const QByteArray &s)
{
    for (const char *prefix : kDescriptionPrefixes)
        if (s.startsWith(prefix))
            return true;
    return false;
}

using Key = QPair<char, quint32>; // (language digit, string id)

bool stringsById(const Table &t, const QSet<char> &languages, QHash<Key, QByteArray> *found)
{
    QByteArray raw;
    std::vector<Entry> strings;
    for (const Section &s : t.sections) {
        const char lang = s.gid[3];
        if (!languages.contains(lang))
            continue;
        for (const auto &[index, sub] : readPach(s.blob)) {
            if (!decodeSub(sub, &raw, &strings))
                return false;
            for (const Entry &e : strings)
                found->insert(Key(lang, e.id), e.text);
        }
    }
    return true;
}

// Shorten NUL-terminated UTF-8 text to maxChars code points and maxBytes
// bytes (both including the terminator), at a word boundary when possible.
QByteArray fitText(const QByteArray &text, int maxChars, int maxBytes)
{
    const QList<uint> body = QString::fromUtf8(stripNul(text)).toUcs4();
    auto utf8Len = [](const QList<uint> &cps) {
        return QString::fromUcs4(reinterpret_cast<const char32_t *>(cps.constData()),
                                 cps.size())
            .toUtf8()
            .size();
    };
    if (body.size() + 1 <= maxChars && utf8Len(body) + 1 <= maxBytes)
        return text;
    QList<uint> cut = body;
    while (!cut.isEmpty() && (cut.size() + 4 > maxChars || utf8Len(cut) + 4 > maxBytes))
        cut.removeLast();
    const qsizetype half = cut.size() / 2;
    qsizetype space = -1;
    for (qsizetype k = cut.size() - 1; k >= half; --k)
        if (cut[k] == ' ') {
            space = k;
            break;
        }
    if (space >= 0)
        cut = cut.mid(0, space);
    while (!cut.isEmpty() && (cut.last() == ' ' || cut.last() == ',' || cut.last() == ';'))
        cut.removeLast();
    return QString::fromUcs4(reinterpret_cast<const char32_t *>(cut.constData()), cut.size())
               .toUtf8() +
           QByteArray("...\0", 4);
}

} // namespace

namespace dlcstrings {

bool mergeTables(const QByteArray &oldData, const QByteArray &newData, QByteArray *out,
                 int *filled, QString *error)
{
    Table oldT, newT;
    QString why;
    if (!readTable(oldData, &oldT, &why) || !readTable(newData, &newT, &why)) {
        *error = why;
        return false;
    }
    if (oldT.magic != "SDB ") {
        *error = QStringLiteral("the base table must be an original 'SDB ' table");
        return false;
    }
    QSet<char> languages;
    for (const Section &s : newT.sections)
        languages.insert(s.gid[3]);
    QHash<Key, QByteArray> newer;
    if (!stringsById(newT, languages, &newer)) {
        *error = QStringLiteral("could not decode the newer table");
        return false;
    }

    // IDs with real text in the newer table.
    QSet<quint32> hasText;
    for (auto it = newer.constBegin(); it != newer.constEnd(); ++it)
        if (!isEmptyText(it.value()) && !it.value().startsWith("Text"))
            hasText.insert(it.key().second);

    // Placeholder IDs (any old section) and per-language description limits.
    QHash<Key, QByteArray> english;
    if (!stringsById(oldT, {'0'}, &english)) {
        *error = QStringLiteral("could not decode the old table");
        return false;
    }
    QSet<quint32> descriptionIds;
    for (auto it = english.constBegin(); it != english.constEnd(); ++it)
        if (isDescription(it.value()))
            descriptionIds.insert(it.key().second);

    QSet<quint32> wanted;
    QHash<char, QPair<int, int>> limits;
    QByteArray raw;
    std::vector<Entry> strings;
    for (const Section &s : oldT.sections) {
        const char lang = s.gid[3];
        for (const auto &[index, sub] : readPach(s.blob)) {
            if (!decodeSub(sub, &raw, &strings)) {
                *error = QStringLiteral("could not decode the old table");
                return false;
            }
            for (const Entry &e : strings) {
                if (isPlaceholder(e.id, e.text)) {
                    if (hasText.contains(e.id))
                        wanted.insert(e.id);
                } else if (descriptionIds.contains(e.id)) {
                    const int chars =
                        int(QString::fromUtf8(stripNul(e.text)).toUcs4().size()) + 1;
                    QPair<int, int> &lim = limits[lang];
                    lim.first = std::max(lim.first, chars);
                    lim.second = std::max(lim.second, int(e.text.size()));
                }
            }
        }
    }

    *filled = 0;
    std::vector<QByteArray> blobs;
    for (const Section &s : oldT.sections) {
        const char lang = s.gid[3];
        if (!languages.contains(lang) || !s.gid.startsWith(kTextSectionPrefix)) {
            blobs.push_back(s.blob);
            continue;
        }
        std::vector<std::pair<quint32, QByteArray>> subs;
        bool changed = false;
        for (auto [index, sub] : readPach(s.blob)) {
            if (!decodeSub(sub, &raw, &strings)) {
                *error = QStringLiteral("could not decode the old table");
                return false;
            }
            bool touched = false;
            for (Entry &e : strings) {
                const auto rep = newer.constFind(Key(lang, e.id));
                if (wanted.contains(e.id) &&
                    (isEmptyText(e.text) || isPlaceholder(e.id, e.text)) &&
                    rep != newer.constEnd() && !isEmptyText(rep.value())) {
                    const QPair<int, int> lim = limits.value(
                        lang, {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()});
                    e.text = fitText(rep.value(), lim.first, lim.second);
                    touched = true;
                    ++*filled;
                }
            }
            if (touched) {
                const quint32 originalSize = rd32(sub, 12);
                QByteArray newRaw, check;
                std::vector<Entry> checkStrings;
                sub = encodeSub(strings, &newRaw);
                if (!decodeSub(sub, &check, &checkStrings) || check != newRaw) {
                    *error = QStringLiteral("internal error: BPE round trip failed");
                    return false;
                }
                if (quint32(newRaw.size()) > originalSize) {
                    *error = QStringLiteral("filled text does not fit the game's buffer");
                    return false;
                }
                changed = true;
            }
            subs.emplace_back(index, sub);
        }
        if (changed) {
            QByteArray rebuilt = writePach(subs);
            if (rebuilt.size() > s.blob.size()) {
                *error = QStringLiteral("rebuilt section does not fit its original space");
                return false;
            }
            // Sections must keep their original sector positions.
            rebuilt.append(QByteArray(s.blob.size() - rebuilt.size(), '\0'));
            blobs.push_back(rebuilt);
        } else {
            blobs.push_back(s.blob);
        }
    }

    QByteArray result = oldT.data.left(kDataOffset);
    QByteArray index("SDB ");
    put32(index, oldT.slotCount);
    put32(index, 0);
    quint32 sector = 0;
    for (size_t k = 0; k < oldT.sections.size(); ++k) {
        index += oldT.sections[k].gid;
        put32(index, sector);
        put32(index, oldT.sections[k].count);
        result += blobs[k];
        sector += quint32(blobs[k].size() / kSector);
    }
    index.append(QByteArray(12 * qsizetype(oldT.slotCount - oldT.sections.size()), '\0'));
    std::memcpy(result.data() + kIndexOffset, index.constData(), index.size());
    set32(result, 8, quint32(result.size() - (kDataOffset + kSector)));
    *out = result;
    return true;
}

bool fixDlcStringTables(const QString &userDataRoot, const QString &titleId, Result *result,
                        QString *error)
{
    const QDir contentDir(QDir(userDataRoot).filePath(
        QStringLiteral("0000000000000000/%1/00000002").arg(titleId.toUpper())));
    if (!contentDir.exists())
        return true;

    struct Candidate {
        QString path;
        int catalogId;
    };
    std::vector<Candidate> newerTables;
    QStringList oldTables;
    for (const QString &pkg : contentDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString pkgDir = contentDir.filePath(pkg);
        const QString table = QDir(pkgDir).filePath(QStringLiteral("pac/string.pac"));
        const QString source = QFileInfo::exists(table + QStringLiteral(".orig"))
                                   ? table + QStringLiteral(".orig")
                                   : table;
        QFile f(source);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray head = f.read(kIndexOffset + 4);
        if (!head.startsWith("EPAC") || head.size() < kIndexOffset + 4)
            continue;
        const QByteArray magic = head.mid(kIndexOffset, 4);
        if (magic == "SDB ") {
            oldTables.append(table);
        } else if (magic == "SDBP" || magic == "SDB2") {
            // The catalog's byte 8 is the add-on number: newest wins.
            QFile cat(QDir(pkgDir).filePath(QStringLiteral("info/catalog.dlc")));
            int id = 0;
            if (cat.open(QIODevice::ReadOnly)) {
                const QByteArray c = cat.read(16);
                if (c.size() > 8 && c.startsWith("DLCC"))
                    id = quint8(c[8]);
            }
            newerTables.push_back({table, id});
        }
    }
    if (newerTables.empty() || oldTables.isEmpty()) {
        result->notes.append(QStringLiteral("DLC text: nothing to merge."));
        return true;
    }
    const Candidate newest = *std::max_element(
        newerTables.begin(), newerTables.end(),
        [](const Candidate &a, const Candidate &b) { return a.catalogId < b.catalogId; });
    result->sourceTable = newest.path;

    QFile nf(newest.path);
    if (!nf.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not read %1").arg(newest.path);
        return false;
    }
    const QByteArray newData = nf.readAll();
    const QFileInfo newInfo(newest.path);
    const QByteArray stamp = QStringLiteral("v%1 %2 %3 %4\n")
                                 .arg(kFixVersion)
                                 .arg(newest.path)
                                 .arg(newInfo.size())
                                 .arg(newInfo.lastModified().toSecsSinceEpoch())
                                 .toUtf8();

    for (const QString &table : oldTables) {
        const QString orig = table + QStringLiteral(".orig");
        const QString stampPath = table + QStringLiteral(".dlcfix");
        QFile st(stampPath);
        if (QFileInfo::exists(orig) && st.open(QIODevice::ReadOnly) && st.readAll() == stamp)
            continue; // Already merged from this exact source
        st.close();
        if (!QFileInfo::exists(orig) && !QFile::copy(table, orig)) {
            if (error)
                *error = QStringLiteral("Could not back up %1").arg(table);
            return false;
        }
        QFile of(orig);
        if (!of.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Could not read %1").arg(orig);
            return false;
        }
        QByteArray merged;
        int filled = 0;
        QString why;
        if (!mergeTables(of.readAll(), newData, &merged, &filled, &why)) {
            result->notes.append(QStringLiteral("DLC text: skipped %1 (%2)").arg(table, why));
            continue;
        }
        QSaveFile out(table);
        if (!out.open(QIODevice::WriteOnly) || out.write(merged) != merged.size() ||
            !out.commit()) {
            if (error)
                *error = QStringLiteral("Could not write %1").arg(table);
            return false;
        }
        QSaveFile stampOut(stampPath);
        if (stampOut.open(QIODevice::WriteOnly)) {
            stampOut.write(stamp);
            stampOut.commit();
        }
        ++result->tablesPatched;
        result->stringsFilled += filled;
        result->notes.append(
            QStringLiteral("DLC text: filled %1 names in %2").arg(filled).arg(table));
    }
    return true;
}

} // namespace dlcstrings
