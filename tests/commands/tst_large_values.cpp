// Edits and undo snapshots of a document with multi-megabyte style values:
// nothing copies the megabytes except the cell that is edited.
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"
#include "qraw/core/shared_value.h"
#include "qraw/format/codec.h"

#include <QTest>

#include <string>

using namespace qraw;
using namespace qraw::commands;

namespace {

std::string imageStyle(char fill)
{
    return "shape=image;image=data:image/png," + std::string(2 * 1024 * 1024, fill) + ";rounded=1;";
}

core::Document sampleDocument()
{
    core::Page page;
    page.setId("p");
    page.appendEntry(core::Cell("0"));
    page.appendEntry(core::Cell::makeLayer("1", "0"));
    for (int i = 0; i < 4; ++i) {
        page.appendEntry(core::Cell::makeVertex("img" + std::to_string(i), "1", "", imageStyle(static_cast<char>('a' + i)),
                                                {i * 100.0, 0, 90, 60}));
    }
    page.appendEntry(core::Cell::makeVertex("box", "1", "Hello", "rounded=1;fillColor=#fff;", {0, 200, 80, 40}));
    core::Document doc;
    doc.appendEntry(std::move(page));
    return doc;
}

const core::Cell& cell(const core::Document& doc, const char* id)
{
    return *doc.page(0)->findCell(id);
}

} // namespace

class TestLargeValues : public QObject {
    Q_OBJECT

private slots:
    void editingAnotherCellCopiesNoImage()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        core::resetLargeValueCopies();
        {
            Transaction tx(doc);
            QVERIFY(ops::setStyle(tx, 0, {"box"}, "fillColor", std::string("#000")).ok);
            QVERIFY(ops::setStyle(tx, 0, {"box"}, "strokeColor", std::string("#f00")).ok);
            tx.rollback();
        }
        QCOMPARE(core::largeValueCopies(), 0u);
        QCOMPARE(format::writeDocument(doc), before);
    }

    void undoSnapshotsOfTheEditedImageCellShareTheOldText()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        const core::SharedValue* original = cell(doc, "img0").attributes().findValue("style");
        QVERIFY(original && original->isShared());
        const core::SharedValue keep = *original; // what a snapshot holds
        core::resetLargeValueCopies();
        {
            Transaction tx(doc);
            QVERIFY(ops::setStyle(tx, 0, {"img0"}, "rounded", std::string("0")).ok);
            // The edited cell has a new text; the old one is still intact in the snapshot.
            QVERIFY(!cell(doc, "img0").attributes().findValue("style")->sameBuffer(keep));
            QVERIFY(keep.str().find("rounded=1") != std::string::npos);
            // One new text for the edited cell: the serialised style, and the copy
            // into its buffer. The other three images are untouched.
            QVERIFY2(core::largeValueCopies() <= 2, qPrintable(QString::number(core::largeValueCopies())));
            tx.rollback();
        }
        QCOMPARE(format::writeDocument(doc), before);
    }

    void copyingTheCellsOfAPageCopiesNothing()
    {
        const core::Document doc = sampleDocument();
        core::resetLargeValueCopies();
        std::vector<core::Cell> snapshots;
        for (const core::Cell* c : doc.page(0)->cells())
            snapshots.push_back(*c);
        QCOMPARE(core::largeValueCopies(), 0u);
        QCOMPARE(snapshots.size(), doc.page(0)->cells().size());
    }
};

QTEST_APPLESS_MAIN(TestLargeValues)
#include "tst_large_values.moc"
