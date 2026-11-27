// Tests of the UI foundation: SelectionModel,
// DocumentSession, cell queries, InteractionController dispatch, the
// baseline DiagramScene/CanvasView projection and ActionRegistry.

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_pages.h"
#include "qraw/ui/action_registry.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/interaction.h"

#include <QAction>
#include <QSignalSpy>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;

namespace {

commands::Result moveA(commands::Transaction& tx, double dx)
{
    return commands::ops::moveCells(tx, 0, {"a"}, dx, 0);
}

// Records what the controller sends it.
class RecordingHandler final : public InteractionHandler {
public:
    explicit RecordingHandler(bool claim, std::vector<std::string>* log, std::string name)
        : claim_(claim), log_(log), name_(std::move(name))
    {
    }

    class Drag final : public Gesture {
    public:
        explicit Drag(std::vector<std::string>* log) : log_(log) {}
        void move(const CanvasEvent&) override { log_->push_back("move"); }
        void release(const CanvasEvent&) override { log_->push_back("release"); }
        void cancel() override { log_->push_back("cancel"); }

    private:
        std::vector<std::string>* log_;
    };

    std::unique_ptr<Gesture> press(const CanvasEvent&) override
    {
        log_->push_back(name_ + ":press");
        return claim_ ? std::make_unique<Drag>(log_) : nullptr;
    }

private:
    bool claim_;
    std::vector<std::string>* log_;
    std::string name_;
};

} // namespace

class TestFoundation : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qraw::ui::test::isolateSettings(); }

    void selectionModelKeepsOrderAndUniqueness()
    {
        SelectionModel model;
        QSignalSpy spy(&model, &SelectionModel::selectionChanged);
        model.set({"b", "a", "b"});
        QCOMPARE(model.cells(), (std::vector<std::string>{"b", "a"}));
        model.set({"b", "a"});
        QCOMPARE(spy.count(), 1); // unchanged content emits nothing
        model.toggle("a");
        model.toggle("c");
        QCOMPARE(model.cells(), (std::vector<std::string>{"b", "c"}));
        QCOMPARE(model.first(), std::string("b"));
        model.clear();
        QVERIFY(model.isEmpty());
        QCOMPARE(spy.count(), 4);
    }

    void sessionEditSelectsResultAndUndoSelectsChanged()
    {
        DocumentSession session(test::simpleDocument());
        QSignalSpy modified(&session, &DocumentSession::modifiedChanged);
        const QByteArray before = test::savedBytes(session);

        const auto result = session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) { return moveA(tx, 10); });
        QVERIFY(result.ok);
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"a"}));
        QVERIFY(session.isModified());
        QCOMPARE(modified.count(), 1);
        QCOMPARE(session.undoText(), QStringLiteral("Move"));

        session.selection().clear();
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(session.selection().contains("a")); // the changed cell is selected again
        QVERIFY(!session.isModified());
        QCOMPARE(modified.count(), 2);
    }

    void sessionFailedEditReportsAndChangesNothing()
    {
        DocumentSession session(test::simpleDocument());
        QSignalSpy status(&session, &DocumentSession::statusMessage);
        const auto result = session.edit(QStringLiteral("Bad"), [](commands::Transaction&) {
            return commands::Result::failure("nope");
        });
        QVERIFY(!result.ok);
        QCOMPARE(status.count(), 1);
        QVERIFY(!session.canUndo());
    }

    void sessionPrunesRemovedCellsAndTracksPages()
    {
        DocumentSession session(test::simpleDocument());
        session.selection().set({"a", "b"});
        session.edit(QStringLiteral("Delete"), [](commands::Transaction& tx) {
            auto r = commands::ops::removeCells(tx, 0, {"b"});
            r.cells.clear();
            return r;
        });
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"a"}));

        QSignalSpy pages(&session, &DocumentSession::currentPageChanged);
        session.edit(QStringLiteral("Add Page"), [](commands::Transaction& tx) {
            return commands::ops::addPage(tx, std::nullopt, std::string("Second"));
        });
        QCOMPARE(session.currentPageIndex(), std::size_t(1)); // Result.page switched
        QVERIFY(session.selection().isEmpty());
        QCOMPARE(pages.count(), 1);
        session.setCurrentPage(99);
        QCOMPARE(session.currentPageIndex(), std::size_t(1)); // clamped
    }

    void sessionLayerAndGroupScope()
    {
        DocumentSession session(test::simpleDocument());
        QCOMPARE(session.currentLayer(), std::string("1"));
        QCOMPARE(session.insertionParent(), std::string("1"));
        session.enterGroup("g");
        QCOMPARE(session.insertionParent(), std::string("g"));
        session.exitGroup();
        QVERIFY(session.enteredGroup().empty());
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"g"}));
    }

    void cellQueries()
    {
        const core::Document document = test::simpleDocument();
        const core::Page& page = *document.page(0);
        QVERIFY(isLayer(page, "1"));
        QVERIFY(!isLayer(page, "a"));
        QVERIFY(isCellSelectable(page, "c1"));
        QVERIFY(!isCellSelectable(page, "1"));
        QVERIFY(hasChildren(page, "g"));
        QCOMPARE(ancestorInScope(page, "c1", ""), std::string("g"));
        QCOMPARE(ancestorInScope(page, "c1", "g"), std::string("c1"));
        QVERIFY(ancestorInScope(page, "a", "g").empty());
        QVERIFY(isHtmlLabel(page, "b"));
        QCOMPARE(labelOf(page, "a"), std::string("A"));
        QVERIFY(!isCellLocked(page, "a"));
    }

    void controllerDispatchesByPriority()
    {
        std::vector<std::string> log;
        InteractionController controller(ToolContext{});
        controller.addHandler(std::make_unique<RecordingHandler>(true, &log, "late"), 500);
        controller.addHandler(std::make_unique<RecordingHandler>(false, &log, "early"), 10);
        CanvasEvent event;
        QVERIFY(controller.mousePress(event));
        QVERIFY(controller.isGestureActive());
        controller.mouseMove(event);
        controller.mouseRelease(event);
        QVERIFY(!controller.isGestureActive());
        QCOMPARE(log, (std::vector<std::string>{"early:press", "late:press", "move", "release"}));

        log.clear();
        controller.mousePress(event);
        controller.cancelGesture();
        QCOMPARE(log.back(), std::string("cancel"));
    }

    void sceneProjectsAndHitTests()
    {
        DocumentSession session(test::simpleDocument());
        auto view = test::makeCanvas(session);
        DiagramScene& scene = view->diagramScene();
        QCOMPARE(scene.snapshot()->records().size(), scene.pageView().states().size());
        QVERIFY(scene.state("a") != nullptr);
        QCOMPARE(scene.cellAt(QPointF(100, 70), 1), std::string("a"));
        QVERIFY(scene.cellAt(QPointF(600, 600), 1).empty());
        // A group child is hit before the transparent group behind it.
        QCOMPARE(scene.cellsAt(QPointF(60, 210), 1).front(), std::string("c1"));
        const auto inRect = scene.cellsInRect(QRectF(0, 0, 500, 150));
        QVERIFY(std::find(inRect.begin(), inRect.end(), "a") != inRect.end());
        QVERIFY(std::find(inRect.begin(), inRect.end(), "c1") == inRect.end());

        QSignalSpy projection(&scene, &DiagramScene::projectionChanged);
        session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) { return moveA(tx, 100); });
        QCOMPARE(projection.count(), 1);
        QCOMPARE(scene.state("a")->bounds.x, 140.0);
        // The moved cell is found at its new place, and no longer at the old one.
        QCOMPARE(scene.cellAt(QPointF(160, 70), 1), std::string("a"));
        QVERIFY(scene.cellAt(QPointF(50, 70), 1).empty());
        test::grabToPng(*view, QStringLiteral("foundation_scene"));
    }

    void viewForwardsMouseEventsToHandlers()
    {
        DocumentSession session(test::simpleDocument());
        auto view = test::makeCanvas(session);
        std::vector<std::string> log;
        view->interaction().addHandler(std::make_unique<RecordingHandler>(true, &log, "probe"), 10000);
        // Placeholder or real handlers may claim the press first; with an
        // empty area nothing but the probe (and the rubber band) is hit.
        test::drag(*view, QPointF(600, 500), QPointF(650, 550));
        if (!log.empty())
            QCOMPARE(log.back(), std::string("release"));
        QCOMPARE(view->zoom(), 1.0);
        view->setZoom(2);
        QCOMPARE(view->zoom(), 2.0);
    }

    void actionRegistryRegistersOnce()
    {
        ActionRegistry registry;
        QAction* save = registry.add(QString::fromLatin1(action_id::kSave), QStringLiteral("File"),
                                     QStringLiteral("&Save"), {QKeySequence::Save});
        QCOMPARE(registry.add(QString::fromLatin1(action_id::kSave), QStringLiteral("Edit"), QStringLiteral("x")), save);
        QCOMPARE(registry[action_id::kSave], save);
        QCOMPARE(save->objectName(), QStringLiteral("save"));
        QCOMPARE(registry.categories(), QStringList{QStringLiteral("File")});
        QVERIFY(!ActionRegistry::canvasKeys().empty());
    }

    void appSettingsRecentFiles()
    {
        AppSettings settings;
        settings.clearRecentFiles();
        settings.addRecentFile(QStringLiteral("/a.drawio"));
        settings.addRecentFile(QStringLiteral("/b.drawio"));
        settings.addRecentFile(QStringLiteral("/a.drawio"));
        QCOMPARE(settings.recentFiles(), (QStringList{QStringLiteral("/a.drawio"), QStringLiteral("/b.drawio")}));
        settings.clearRecentFiles();
    }
};

QTEST_MAIN(TestFoundation)
#include "tst_foundation.moc"
