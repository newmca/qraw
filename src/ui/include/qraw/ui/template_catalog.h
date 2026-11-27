#pragma once
// The bundled diagram templates (resources/templates, compiled in under
// :/qraw/templates/<category>/<name>.xml; see resources/templates/NOTICE.md).
// Implemented in src/ui/template_catalog.cpp. QtCore only.

#include <QString>

#include <vector>

namespace qraw::ui {

struct TemplateInfo {
    QString category; // directory name, e.g. "flowcharts", "cloud/aws" -> "cloud/aws"
    QString title;    // file stem with '_' -> ' ', first letter upper-cased ("Flowchart 1")
    QString path;     // ":/qraw/templates/flowcharts/flowchart_1.xml"
};

class TemplateCatalog {
public:
    // Scans `root` recursively for *.xml (default the compiled-in templates),
    // sorted by category then title. Licence/notice files are ignored.
    [[nodiscard]] static std::vector<TemplateInfo> scan(const QString& root = QStringLiteral(":/qraw/templates"));
    // Distinct categories in scan order.
    [[nodiscard]] static std::vector<QString> categories(const std::vector<TemplateInfo>& templates);
};

} // namespace qraw::ui
