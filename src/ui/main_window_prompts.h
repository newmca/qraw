#pragma once
// DocumentPrompts implementations used by MainWindow (private to the editor UI).

#include "qraw/ui/document_controller.h"

#include <QCoreApplication>
#include <QStringList>

class QWidget;

namespace qraw::ui {

// QMessageBox / QFileDialog prompts parented to the main window.
class DialogPrompts final : public DocumentPrompts {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::DialogPrompts)

public:
    explicit DialogPrompts(QWidget* parent) : parent_(parent) {}

    SaveChoice askSaveChanges(const QString& documentName) override;
    std::optional<QString> askOpenPath(const QString& filter) override;
    std::optional<QString> askSavePath(const QString& suggested, const QString& filter) override;
    void showError(const QString& title, const QString& message) override;
    void showWarnings(const QString& title, const QStringList& messages) override;

private:
    QWidget* parent_;
};

// What the controller talks to: the test prompts when set, else dialogs.
class ForwardingPrompts final : public DocumentPrompts {
public:
    explicit ForwardingPrompts(QWidget* parent) : dialogs_(parent) {}

    void setOverride(DocumentPrompts* prompts) { override_ = prompts; }

    SaveChoice askSaveChanges(const QString& documentName) override { return target().askSaveChanges(documentName); }
    std::optional<QString> askOpenPath(const QString& filter) override { return target().askOpenPath(filter); }
    std::optional<QString> askSavePath(const QString& suggested, const QString& filter) override
    {
        return target().askSavePath(suggested, filter);
    }
    void showError(const QString& title, const QString& message) override { target().showError(title, message); }
    void showWarnings(const QString& title, const QStringList& messages) override
    {
        target().showWarnings(title, messages);
    }

private:
    DocumentPrompts& target() { return override_ != nullptr ? *override_ : static_cast<DocumentPrompts&>(dialogs_); }

    DialogPrompts dialogs_;
    DocumentPrompts* override_ = nullptr;
};

// Never shows anything (screenshot harness): unsaved changes are discarded,
// file dialogs are cancelled and errors are collected for the caller.
class NonInteractivePrompts final : public DocumentPrompts {
public:
    SaveChoice askSaveChanges(const QString&) override { return SaveChoice::Discard; }
    std::optional<QString> askOpenPath(const QString&) override { return std::nullopt; }
    std::optional<QString> askSavePath(const QString&, const QString&) override { return std::nullopt; }
    void showError(const QString& title, const QString& message) override
    {
        errors_.append(title + QStringLiteral(": ") + message);
    }

    [[nodiscard]] const QStringList& errors() const { return errors_; }

private:
    QStringList errors_;
};

} // namespace qraw::ui
