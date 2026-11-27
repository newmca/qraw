#include "operations.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/format/codec.h"
#include "qraw/format/semantic.h"
#include "qraw/render/export.h"
#include "qraw/render/page_view.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/snapping.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImageReader>

#include <algorithm>
#include <functional>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>

namespace qraw::bench {

namespace {

// PNG exports are scaled so the long side is at most this many pixels
// (KNOWN_ISSUES "Huge exports": a 10^8-unit page cannot be allocated).
constexpr double kMaxExportPixels = 4096;
// The hit-test group: points for cellsAt, rectangles for cellsInRect, moves for the snapper.
constexpr int kHitPoints = 10000;
constexpr int kHitRects = 200;
constexpr int kSnapMoves = 200;

double elapsedMs(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

class Runner {
public:
    Runner(const QByteArray& original, const Options& options) : original_(original), options_(options) {}

    std::vector<Measurement> run()
    {
        for (const std::string& group : operationGroups()) {
            if (!options_.groups.empty() && !options_.groups.contains(group))
                continue;
            runGroup(group);
        }
        return std::move(results_);
    }

private:
    using Failure = QString; // empty = success

    void runGroup(const std::string& group)
    {
        // Inputs that an earlier group would have produced are made here, not
        // inside the timed step.
        detail_.clear();
        if (group != "load" && group != "move" && group != "duplicate" && group != "delete" && group != "group"
            && group != "ungroup")
            loadedDocument();
        if (group == "semantic" && saved_.isEmpty())
            save();
        if (group == "load")
            record(timeRuns("load", [&] { return load(); }));
        else if (group == "save")
            record(timeRuns("save", [&] { return save(); }));
        else if (group == "semantic")
            record(timeRuns("semanticEqual", [&] { return semantic(); }));
        else if (group == "pageview")
            record(timeRuns("pageView", [&] { return pageView(); }));
        else if (group == "png")
            record(timeRuns("exportPng", [&] { return exportPng(); }));
        else if (group == "scene") {
            const auto session = makeSession();
            record(timeRuns("scene", [&] { return scene(session.get()); }));
        }
        else if (group == "hittest")
            runHitTests();
        else if (group == "group")
            runEdit("group", [](ui::DocumentSession& s) { return ui::actions::group(s); });
        else if (group == "ungroup")
            runUngroup();
        else if (group == "move")
            runEdit("move", [](ui::DocumentSession& s) {
                const std::vector<std::string> ids = s.selection().cells();
                return s.edit(QStringLiteral("Move"), [&](commands::Transaction& tx) {
                           return commands::ops::moveCells(tx, s.currentPageIndex(), ids, 10, 10);
                       }).ok;
            });
        else if (group == "duplicate")
            runEdit("duplicate", [](ui::DocumentSession& s) { return ui::actions::duplicateSelection(s); });
        else if (group == "delete")
            runEdit("delete", [](ui::DocumentSession& s) { return ui::actions::deleteSelection(s, true); });
    }

    // --- timing ------------------------------------------------------------
    // Runs `step` options_.repeat times (fewer after a very slow run); `step`
    // returns a failure reason or an empty string.
    Measurement timeRuns(const std::string& name, const std::function<Failure()>& step)
    {
        Measurement m;
        m.name = name;
        for (int i = 0; i < options_.repeat; ++i) {
            QElapsedTimer timer;
            timer.start();
            const Failure failure = step();
            const double ms = elapsedMs(timer);
            if (!failure.isEmpty()) {
                m.ok = false;
                m.detail = failure;
                return m;
            }
            m.runsMs.push_back(ms);
            if (ms > options_.slowRunMs)
                break;
        }
        m.detail = detail_;
        return m;
    }

    void record(Measurement m)
    {
        if (options_.progress != nullptr) {
            *options_.progress << "  " << m.name << ": ";
            if (m.ok)
                *options_.progress << m.medianMs() << " ms\n";
            else
                *options_.progress << "FAILED " << m.detail.toStdString() << "\n";
            options_.progress->flush();
        }
        results_.push_back(std::move(m));
    }

    // --- the operations ----------------------------------------------------
    Failure load()
    {
        format::ReadResult result = format::readDocument(original_);
        if (!result.ok())
            return QStringLiteral("readDocument failed");
        document_ = std::move(result.document);
        return {};
    }

    // A document for the operations after load, whether or not load was selected.
    const core::Document* loadedDocument()
    {
        if (!document_ && !load().isEmpty())
            return nullptr;
        return &*document_;
    }

    Failure save()
    {
        const core::Document* doc = loadedDocument();
        if (doc == nullptr)
            return QStringLiteral("no document");
        saved_ = format::writeDocument(*doc);
        // The generator writes with writeDocument: a round trip is the identity.
        return saved_ == original_ ? Failure() : QStringLiteral("saved bytes differ from the original");
    }

    Failure semantic()
    {
        if (saved_.isEmpty() && !save().isEmpty())
            return QStringLiteral("save failed");
        std::string difference;
        return format::semanticEqual(original_, saved_, &difference) ? Failure()
                                                                    : QString::fromStdString(difference);
    }

    Failure pageView()
    {
        const core::Document* doc = loadedDocument();
        if (doc == nullptr || doc->pages().empty())
            return QStringLiteral("no page");
        const render::PageView view = render::PageView::build(*doc->pages().front(), render::RenderContext::defaults());
        return view.states().empty() ? QStringLiteral("empty view") : Failure();
    }

    Failure exportPng()
    {
        const core::Document* doc = loadedDocument();
        if (doc == nullptr || doc->pages().empty())
            return QStringLiteral("no page");
        const core::Rect bounds = render::PageView::build(*doc->pages().front(), render::RenderContext::defaults()).bounds();
        render::ExportOptions options;
        options.format = render::ExportFormat::Png;
        options.scale = std::min(1.0, kMaxExportPixels / std::max({bounds.width, bounds.height, 1.0}));
        options.maxPixelSize = static_cast<int>(kMaxExportPixels);
        const render::ExportResult result = render::exportPage(*doc, 0, options);
        if (!result.ok())
            return QStringLiteral("exportPage failed");
        QBuffer buffer;
        buffer.setData(result.data);
        buffer.open(QIODevice::ReadOnly);
        const QSize size = QImageReader(&buffer, "png").size();
        detail_ = QStringLiteral("scale %1, %2x%3 px, %4 MB")
                      .arg(options.scale, 0, 'f', 3)
                      .arg(size.width())
                      .arg(size.height())
                      .arg(static_cast<double>(result.data.size()) / 1e6, 0, 'f', 1);
        return {};
    }

    // Builds the scene of `session`: the page view and one item per cell.
    Failure scene(ui::DocumentSession* session)
    {
        if (session == nullptr)
            return QStringLiteral("no document");
        ui::DiagramScene scene(*session);
        const std::size_t states = scene.pageView().states().size();
        const bool built = states > 0 && scene.snapshot()->records().size() == states;
        return built ? Failure() : QStringLiteral("scene has %1 states").arg(states);
    }

    // Select all, `edit`, then undo, each timed on its own; after the undo the
    // saved bytes must equal the original.
    void runEdit(const std::string& name, const std::function<bool(ui::DocumentSession&)>& edit)
    {
        Measurement forward;
        forward.name = name;
        Measurement undo;
        undo.name = name + ".undo";
        const auto session = makeSession();
        if (!session) {
            forward.ok = undo.ok = false;
            forward.detail = undo.detail = QStringLiteral("no document");
        }
        std::unique_ptr<ui::DiagramScene> scene;
        if (session && options_.withScene)
            scene = std::make_unique<ui::DiagramScene>(*session);
        for (int i = 0; session && i < options_.repeat; ++i) {
            ui::actions::selectAll(*session);
            QElapsedTimer timer;
            timer.start();
            const bool edited = edit(*session);
            const double editMs = elapsedMs(timer);
            if (!edited) {
                forward.ok = false;
                forward.detail = QStringLiteral("the edit failed");
                break;
            }
            forward.runsMs.push_back(editMs);
            timer.restart();
            const bool undone = session->undo();
            const double undoMs = elapsedMs(timer);
            if (!undone || format::writeDocument(session->document()) != original_) {
                undo.ok = false;
                undo.detail = QStringLiteral("undo did not restore the original bytes");
                break;
            }
            undo.runsMs.push_back(undoMs);
            if (std::max(editMs, undoMs) > options_.slowRunMs)
                break;
        }
        if (!forward.ok)
            undo.ok = false;
        if (!forward.ok && undo.detail.isEmpty())
            undo.detail = QStringLiteral("not run: %1 failed").arg(QString::fromStdString(name));
        record(std::move(forward));
        record(std::move(undo));
    }

    // Group everything (not timed), then time ungrouping the group and undoing
    // it; the grouped state is undone at the end and must equal the original.
    void runUngroup()
    {
        Measurement forward;
        forward.name = "ungroup";
        Measurement undo;
        undo.name = "ungroup.undo";
        const auto session = makeSession();
        std::unique_ptr<ui::DiagramScene> scene;
        if (session && options_.withScene)
            scene = std::make_unique<ui::DiagramScene>(*session);
        bool ready = session != nullptr;
        std::vector<std::string> groupSelection;
        if (ready) {
            ui::actions::selectAll(*session);
            ready = ui::actions::group(*session);
            groupSelection = session->selection().cells(); // the new group
        }
        if (!ready) {
            forward.ok = undo.ok = false;
            forward.detail = undo.detail = QStringLiteral("grouping failed");
        }
        for (int i = 0; ready && i < options_.repeat; ++i) {
            session->selection().set(groupSelection);
            QElapsedTimer timer;
            timer.start();
            const bool edited = ui::actions::ungroup(*session);
            const double editMs = elapsedMs(timer);
            if (!edited) {
                forward.ok = undo.ok = false;
                forward.detail = undo.detail = QStringLiteral("the ungroup failed");
                break;
            }
            forward.runsMs.push_back(editMs);
            timer.restart();
            const bool undone = session->undo();
            const double undoMs = elapsedMs(timer);
            if (!undone) {
                undo.ok = false;
                undo.detail = QStringLiteral("undo failed");
                break;
            }
            undo.runsMs.push_back(undoMs);
            if (std::max(editMs, undoMs) > options_.slowRunMs)
                break;
        }
        if (ready && forward.ok) {
            // Only the group edit is left on the stack.
            if (!session->undo() || format::writeDocument(session->document()) != original_) {
                undo.ok = false;
                undo.detail = QStringLiteral("undo did not restore the original bytes");
            }
        }
        record(std::move(forward));
        record(std::move(undo));
    }

    // Hit testing, rubber bands and snapping on a scene of the document: the
    // work of one mouse move, repeated. The detail carries a checksum of the
    // results, to compare two implementations.
    void runHitTests()
    {
        const auto session = makeSession();
        if (!session) {
            Measurement m;
            m.name = "hittest";
            m.ok = false;
            m.detail = QStringLiteral("no document");
            record(std::move(m));
            return;
        }
        ui::DiagramScene scene(*session);
        const QRectF area = scene.pageView().states().empty() ? QRectF(0, 0, 100, 100) : scene.pageArea();
        std::uint64_t seed = 0x9e3779b97f4a7c15ULL;
        const auto next = [&seed] {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return static_cast<double>(seed % 1000000) / 1e6;
        };
        std::vector<QPointF> points;
        std::vector<QRectF> rects;
        for (int i = 0; i < kHitPoints; ++i)
            points.emplace_back(area.left() + next() * area.width(), area.top() + next() * area.height());
        for (int i = 0; i < kHitRects; ++i)
            rects.emplace_back(area.left() + next() * area.width() * 0.8, area.top() + next() * area.height() * 0.8,
                               area.width() * (0.01 + next() * 0.2), area.height() * (0.01 + next() * 0.2));
        rects.emplace_back(area); // the whole page

        const auto mix = [](std::uint64_t& sum, const std::vector<std::string>& ids) {
            for (const std::string& id : ids)
                sum = sum * 1099511628211ULL + std::hash<std::string>{}(id);
            sum = sum * 31 + ids.size();
        };
        std::uint64_t sum = 0;
        record(timeRuns("hit.cellsAt", [&] {
            sum = 0;
            for (const QPointF& p : points)
                mix(sum, scene.cellsAt(p, 4));
            detail_ = QStringLiteral("%1 points, checksum %2").arg(kHitPoints).arg(sum, 0, 16);
            return Failure();
        }));
        record(timeRuns("hit.cellsInRect", [&] {
            sum = 0;
            for (const QRectF& r : rects)
                mix(sum, scene.cellsInRect(r));
            detail_ = QStringLiteral("%1 rects, checksum %2").arg(rects.size()).arg(sum, 0, 16);
            return Failure();
        }));
        record(timeRuns("hit.snapMove", [&] {
            const ui::Snapper snapper(scene, true);
            double total = 0;
            for (int i = 0; i < kSnapMoves; ++i) {
                const QPointF& p = points[static_cast<std::size_t>(i)];
                total += snapper.snapMove(QRectF(p, QSizeF(120, 60)), QPointF(3, 2), {}, Qt::NoModifier, 1).delta.x();
            }
            detail_ = QStringLiteral("%1 moves, checksum %2").arg(kSnapMoves).arg(total, 0, 'f', 3);
            return Failure();
        }));
    }

    // Reads the original into a fresh session (not timed).
    std::unique_ptr<ui::DocumentSession> makeSession()
    {
        format::ReadResult result = format::readDocument(original_);
        if (!result.ok())
            return nullptr;
        return std::make_unique<ui::DocumentSession>(std::move(*result.document));
    }

    const QByteArray& original_;
    const Options& options_;
    std::vector<Measurement> results_;
    std::optional<core::Document> document_;
    QByteArray saved_;
    QString detail_; // set by an operation, copied into its measurement
};

} // namespace

double Measurement::minMs() const
{
    return runsMs.empty() ? 0 : *std::min_element(runsMs.begin(), runsMs.end());
}

double Measurement::medianMs() const
{
    if (runsMs.empty())
        return 0;
    std::vector<double> sorted = runsMs;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t n = sorted.size();
    return n % 2 == 1 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
}

const std::vector<std::string>& operationGroups()
{
    static const std::vector<std::string> groups{"load", "save", "semantic", "pageview", "png",
                                                 "scene", "hittest", "move", "duplicate", "delete", "group", "ungroup"};
    return groups;
}

std::vector<Measurement> runOperations(const QByteArray& original, const Options& options)
{
    return Runner(original, options).run();
}

} // namespace qraw::bench
