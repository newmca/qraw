#include "qraw/ui/template_catalog.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>
#include <set>
#include <tuple>

namespace qraw::ui {
namespace {

// "flowchart_1" -> "Flowchart 1"
QString titleFromStem(const QString& stem)
{
    QString title = stem;
    title.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!title.isEmpty())
        title[0] = title.at(0).toUpper();
    return title;
}

// The directory of `filePath` relative to `root`, "" for files directly in it.
QString categoryOf(const QDir& root, const QString& filePath)
{
    return root.relativeFilePath(QFileInfo(filePath).absolutePath());
}

} // namespace

std::vector<TemplateInfo> TemplateCatalog::scan(const QString& root)
{
    std::vector<TemplateInfo> templates;
    const QDir rootDir(root);
    QDirIterator it(root, {QStringLiteral("*.xml")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo info(filePath);
        templates.push_back(TemplateInfo{categoryOf(rootDir, filePath), titleFromStem(info.completeBaseName()),
                                         filePath});
    }
    std::sort(templates.begin(), templates.end(), [](const TemplateInfo& a, const TemplateInfo& b) {
        return std::tie(a.category, a.title) < std::tie(b.category, b.title);
    });
    return templates;
}

std::vector<QString> TemplateCatalog::categories(const std::vector<TemplateInfo>& templates)
{
    std::vector<QString> result;
    std::set<QString> seen;
    for (const TemplateInfo& info : templates) {
        if (seen.insert(info.category).second)
            result.push_back(info.category);
    }
    return result;
}

} // namespace qraw::ui
