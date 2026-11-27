// The palette data loader: resources/palettes/*.json (schema 1, ui.md
// section 8) -> Palette/PaletteEntry. QtCore only.
#include "qraw/ui/palette_library.h"

#include "qraw/core/number.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace qraw::ui {

namespace {

constexpr int kSchemaVersion = 1;

// Attribute-value escaping of XML 1.0 (what mxUtils.getXml writes).
std::string escapeAttribute(const QString& text)
{
    std::string out;
    const QByteArray utf8 = text.toUtf8();
    out.reserve(static_cast<std::size_t>(utf8.size()) + 8);
    for (const char c : utf8) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\n': out += "&#xa;"; break;
        case '\r': out += "&#xd;"; break;
        case '\t': out += "&#x9;"; break;
        default: out += c; break;
        }
    }
    return out;
}

// Sidebar.createVertexTemplateEntry / createEdgeTemplateEntry build one cell
// at the origin; this is that cell as a clipboard-style model.
std::string singleCellModel(const PaletteEntry& entry)
{
    const std::string w = core::formatNumber(entry.width);
    const std::string h = core::formatNumber(entry.height);
    std::string xml = R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="2" value=")";
    xml += escapeAttribute(entry.value);
    xml += R"(" style=")";
    xml += escapeAttribute(entry.style);
    if (entry.kind == PaletteEntry::Kind::Edge) {
        // createEdgeTemplate (Sidebar.js:7039): source (0, h), target (w, 0).
        xml += R"(" edge="1" parent="1"><mxGeometry width=")" + w + R"(" height=")" + h
            + R"(" relative="1" as="geometry"><mxPoint y=")" + h + R"(" as="sourcePoint"/><mxPoint x=")" + w
            + R"(" as="targetPoint"/></mxGeometry></mxCell>)";
    } else {
        xml += R"(" vertex="1" parent="1"><mxGeometry width=")" + w + R"(" height=")" + h
            + R"(" as="geometry"/></mxCell>)";
    }
    xml += "</root></mxGraphModel>";
    return xml;
}

// --- JSON -> model, tolerant: a bad entry is reported and skipped -------------

void addError(PaletteLoadReport* report, const QString& origin, const QString& message)
{
    if (report != nullptr)
        report->errors.push_back(origin + QStringLiteral(": ") + message);
}

QStringList stringList(const QJsonValue& value)
{
    QStringList list;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString())
            list.push_back(item.toString());
    }
    return list;
}

// Returns false (and fills `reason`) when the entry is unusable.
bool parseEntry(const QJsonObject& object, PaletteEntry& entry, QString& reason)
{
    entry.id = object.value(QStringLiteral("id")).toString();
    if (entry.id.isEmpty()) {
        reason = QStringLiteral("missing id");
        return false;
    }
    entry.title = object.value(QStringLiteral("title")).toString();
    const QString kind = object.value(QStringLiteral("kind")).toString();
    entry.width = object.value(QStringLiteral("w")).toDouble(0);
    entry.height = object.value(QStringLiteral("h")).toDouble(0);
    entry.tags = stringList(object.value(QStringLiteral("tags")));
    entry.showLabel = object.value(QStringLiteral("showLabel")).toBool(true);
    entry.value = object.value(QStringLiteral("value")).toString();

    if (kind == QLatin1String("vertex") || kind == QLatin1String("edge")) {
        entry.kind = kind == QLatin1String("edge") ? PaletteEntry::Kind::Edge : PaletteEntry::Kind::Vertex;
        if (!object.value(QStringLiteral("style")).isString()) {
            reason = QStringLiteral("%1 without a style string").arg(kind);
            return false;
        }
        entry.style = object.value(QStringLiteral("style")).toString();
        if (!object.value(QStringLiteral("w")).isDouble() || !object.value(QStringLiteral("h")).isDouble()) {
            reason = QStringLiteral("%1 without numeric w/h").arg(kind);
            return false;
        }
        return true;
    }
    if (kind == QLatin1String("cells")) {
        entry.kind = PaletteEntry::Kind::Cells;
        entry.xml = object.value(QStringLiteral("xml")).toString();
        if (entry.xml.isEmpty()) {
            reason = QStringLiteral("cells entry without xml");
            return false;
        }
        return true;
    }
    reason = QStringLiteral("unknown kind \"%1\"").arg(kind);
    return false;
}

} // namespace

std::string PaletteEntry::cellsXml() const
{
    if (kind == Kind::Cells)
        return xml.toStdString();
    return singleCellModel(*this);
}

const PaletteLibrary& PaletteLibrary::builtin()
{
    static const PaletteLibrary library = [] {
        PaletteLibrary loaded;
        loaded.loadDirectory(QStringLiteral(":/qraw/palettes"));
        return loaded;
    }();
    return library;
}

void PaletteLibrary::loadJson(const QByteArray& json, const QString& origin, PaletteLoadReport* report)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject()) {
        addError(report, origin, QStringLiteral("not a JSON object (%1)").arg(parseError.errorString()));
        return;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt(-1) != kSchemaVersion) {
        addError(report, origin,
                 QStringLiteral("unsupported schema version %1 (expected %2)")
                     .arg(root.value(QStringLiteral("schema")).toVariant().toString())
                     .arg(kSchemaVersion));
        return;
    }

    for (const QJsonValue& paletteValue : root.value(QStringLiteral("palettes")).toArray()) {
        const QJsonObject paletteObject = paletteValue.toObject();
        Palette palette;
        palette.id = paletteObject.value(QStringLiteral("id")).toString();
        if (palette.id.isEmpty()) {
            addError(report, origin, QStringLiteral("palette without id skipped"));
            continue;
        }
        if (this->palette(palette.id) != nullptr) {
            addError(report, origin, QStringLiteral("duplicate palette id \"%1\" skipped").arg(palette.id));
            continue;
        }
        palette.title = paletteObject.value(QStringLiteral("title")).toString(palette.id);
        palette.library = paletteObject.value(QStringLiteral("library")).toString();
        palette.order = paletteObject.value(QStringLiteral("order")).toInt(0);

        QSet<QString> seen;
        int index = 0;
        for (const QJsonValue& entryValue : paletteObject.value(QStringLiteral("entries")).toArray()) {
            PaletteEntry entry;
            QString reason;
            if (!parseEntry(entryValue.toObject(), entry, reason)) {
                addError(report, origin,
                         QStringLiteral("palette %1, entry #%2: %3").arg(palette.id).arg(index).arg(reason));
            } else if (seen.contains(entry.id)) {
                addError(report, origin,
                         QStringLiteral("palette %1: duplicate entry id \"%2\" skipped").arg(palette.id, entry.id));
            } else {
                seen.insert(entry.id);
                palette.entries.push_back(std::move(entry));
            }
            ++index;
        }
        if (report != nullptr) {
            ++report->palettes;
            report->entries += static_cast<int>(palette.entries.size());
        }
        palettes_.push_back(std::move(palette));
    }
    std::stable_sort(palettes_.begin(), palettes_.end(), [](const Palette& a, const Palette& b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
}

PaletteLoadReport PaletteLibrary::loadDirectory(const QString& directory)
{
    PaletteLoadReport report;
    const QDir dir(directory);
    const QStringList names = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (names.isEmpty())
        report.errors.push_back(directory + QStringLiteral(": no palette files found"));
    for (const QString& name : names) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            report.errors.push_back(file.fileName() + QStringLiteral(": cannot open"));
            continue;
        }
        loadJson(file.readAll(), name, &report);
    }
    return report;
}

const Palette* PaletteLibrary::palette(const QString& id) const
{
    const auto it = std::find_if(palettes_.begin(), palettes_.end(), [&](const Palette& p) { return p.id == id; });
    return it == palettes_.end() ? nullptr : &*it;
}

const PaletteEntry* PaletteLibrary::entry(const QString& key) const
{
    const qsizetype slash = key.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return nullptr;
    const Palette* owner = palette(key.left(slash));
    if (owner == nullptr)
        return nullptr;
    const QString entryId = key.mid(slash + 1);
    const auto it = std::find_if(owner->entries.begin(), owner->entries.end(),
                                 [&](const PaletteEntry& e) { return e.id == entryId; });
    return it == owner->entries.end() ? nullptr : &*it;
}

std::size_t PaletteLibrary::entryCount() const
{
    std::size_t count = 0;
    for (const Palette& p : palettes_)
        count += p.entries.size();
    return count;
}

namespace {

// Sidebar.searchEntries: a term matches a tag by prefix; we also accept a
// substring of the title so that "lambda" finds "AWS Lambda".
bool matchesTerm(const PaletteEntry& entry, const QString& lowerTitle, const QString& term)
{
    if (lowerTitle.contains(term))
        return true;
    return std::any_of(entry.tags.begin(), entry.tags.end(),
                       [&](const QString& tag) { return tag.startsWith(term, Qt::CaseInsensitive); });
}

// Derived palettes (Tables, Containers) copy entries of other palettes; a
// search should list such a shape once.
QString contentKey(const PaletteEntry& entry)
{
    return entry.title + QLatin1Char('\x1f') + entry.style + QLatin1Char('\x1f') + entry.xml + QLatin1Char('\x1f')
        + entry.value + QLatin1Char('\x1f') + QString::number(entry.width) + QLatin1Char('x')
        + QString::number(entry.height);
}

} // namespace

std::vector<PaletteLibrary::SearchHit> PaletteLibrary::search(const QString& query, std::size_t limit) const
{
    std::vector<SearchHit> hits;
    const QStringList terms = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (terms.isEmpty() || limit == 0)
        return hits;

    QSet<QString> seen;
    for (const Palette& p : palettes_) {
        for (const PaletteEntry& e : p.entries) {
            const QString lowerTitle = e.title.toLower();
            const bool all = std::all_of(terms.begin(), terms.end(),
                                         [&](const QString& term) { return matchesTerm(e, lowerTitle, term); });
            if (!all || seen.contains(contentKey(e)))
                continue;
            seen.insert(contentKey(e));
            hits.push_back({&p, &e});
            if (hits.size() >= limit)
                return hits;
        }
    }
    return hits;
}

Palette paletteFromLibrary(const format::Library& library, const QString& id, const QString& title)
{
    Palette palette;
    palette.id = id;
    palette.title = title;
    palette.library = QStringLiteral("user");
    int position = 0;
    for (const format::LibraryEntry& source : library.entries) {
        PaletteEntry entry;
        entry.id = QStringLiteral("e%1").arg(++position);
        entry.title = QString::fromStdString(source.title);
        entry.width = source.w;
        entry.height = source.h;
        entry.showLabel = false;
        if (!source.data.empty()) {
            // EditorUi.addLibraryEntries (js/diagramly/EditorUi.js:6466).
            entry.kind = PaletteEntry::Kind::Vertex;
            entry.style = QStringLiteral("shape=image;verticalLabelPosition=bottom;verticalAlign=top;imageAspect=0;");
            if (source.aspect == "fixed")
                entry.style += QStringLiteral("aspect=fixed;");
            entry.style += QStringLiteral("image=") + QString::fromStdString(format::convertDataUri(source.data)) + QLatin1Char(';');
        } else {
            entry.kind = PaletteEntry::Kind::Cells;
            entry.xml = QString::fromStdString(source.xml);
        }
        palette.entries.push_back(std::move(entry));
    }
    return palette;
}

} // namespace qraw::ui
