#include "user_library.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::UserLibrary)
};

bool writeLibrary(const QString& path, const format::Library& library, QString* error)
{
    QSaveFile file(path);
    const std::string bytes = format::serializeLibrary(library);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes.data(), static_cast<qint64>(bytes.size())) < 0
        || !file.commit()) {
        if (error != nullptr)
            *error = Text::tr("The library could not be saved: %1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace

UserLibrary::UserLibrary(QString path, format::Library library) : path_(std::move(path)), library_(std::move(library))
{
    palette_ = std::make_shared<const Palette>(paletteFromLibrary(library_, paletteId(), title()));
}

QString UserLibrary::title() const
{
    return QFileInfo(path_).fileName();
}

std::optional<UserLibrary> UserLibrary::open(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = Text::tr("The library could not be read: %1").arg(file.errorString());
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    std::vector<core::Diagnostic> diagnostics;
    std::optional<format::Library> library = format::parseLibrary(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), &diagnostics);
    if (!library) {
        if (error != nullptr) {
            const QString reason = diagnostics.empty() ? QString() : QString::fromStdString(diagnostics.front().message);
            *error = Text::tr("%1 is not a library file: %2").arg(QFileInfo(path).fileName(), reason);
        }
        return std::nullopt;
    }
    return UserLibrary(QFileInfo(path).absoluteFilePath(), std::move(*library));
}

std::optional<UserLibrary> UserLibrary::create(const QString& path, QString* error)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (!writeLibrary(absolute, {}, error))
        return std::nullopt;
    return UserLibrary(absolute, {});
}

bool UserLibrary::commit(format::Library next, QString* error)
{
    if (!writeLibrary(path_, next, error))
        return false;
    library_ = std::move(next);
    palette_ = std::make_shared<const Palette>(paletteFromLibrary(library_, paletteId(), title()));
    return true;
}

bool UserLibrary::addCells(const std::string& cellsXml, double w, double h, const QString& title, QString* error)
{
    format::Library next = library_;
    next.entries.push_back({title.toStdString(), cellsXml, {}, w, h, {}});
    return commit(std::move(next), error);
}

bool UserLibrary::renameEntry(std::size_t index, const QString& title, QString* error)
{
    if (index >= library_.entries.size())
        return false;
    format::Library next = library_;
    next.entries[index].title = title.toStdString();
    return commit(std::move(next), error);
}

bool UserLibrary::removeEntry(std::size_t index, QString* error)
{
    if (index >= library_.entries.size())
        return false;
    format::Library next = library_;
    next.entries.erase(next.entries.begin() + static_cast<std::ptrdiff_t>(index));
    return commit(std::move(next), error);
}

} // namespace qraw::ui
