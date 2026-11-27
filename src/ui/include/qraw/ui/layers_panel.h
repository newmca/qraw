#pragma once
// The Layers panel (LayersWindow, js/grapheditor/Dialogs.js:5298). Implemented in
// src/ui/layers_panel.cpp.

#include <QWidget>

#include <memory>
#include <string>

class QTreeWidget;

namespace qraw::ui {

class DocumentSession;

// One row per layer of the current page, topmost layer first (draw.io lists
// layers in reverse z-order). Each row: visibility checkbox (eye), lock
// checkbox, name; the current layer (DocumentSession::currentLayer) is
// highlighted; a row with selected cells shows a dot (draw.io marks it).
// Buttons below: Add (ops::addLayer, then make it current), Delete
// (ops::removeLayers; it adds a fresh default layer when none is left, as
// upstream), Duplicate
// (ops::duplicateLayer), Move Selection To (menu of layers ->
// ops::reparentCells of the selection's topmost cells into that layer),
// Rename (double click or button -> ops::renameLayer), Up/Down
// (ops::moveLayer). Visibility -> ops::setLayerVisible, lock ->
// ops::setLayerLocked. All through DocumentSession::edit. Rebuilds on
// documentChanged / currentPageChanged; follows selectionChanged.
class LayersPanel : public QWidget {
    Q_OBJECT

public:
    explicit LayersPanel(DocumentSession& session, QWidget* parent = nullptr);
    ~LayersPanel() override;

    [[nodiscard]] QTreeWidget* list() const;

    bool addLayer();
    bool removeLayer(const std::string& layerId);
    bool duplicateLayer(const std::string& layerId);
    bool renameLayer(const std::string& layerId, const QString& name);
    bool moveLayer(const std::string& layerId, std::size_t toIndex);
    bool setLayerVisible(const std::string& layerId, bool visible);
    bool setLayerLocked(const std::string& layerId, bool locked);
    bool moveSelectionToLayer(const std::string& layerId);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
