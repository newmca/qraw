// Style, label and custom-data operations (ops_properties.h). Every case
// checks the emitted state and that rollback restores the saved bytes.
#include "qraw/commands/ops_properties.h"

#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"
#include "qraw/format/codec.h"

#include <QTest>

#include <string>
#include <vector>

using namespace qraw;
using namespace qraw::commands;

namespace {

// Layer "1" with vertex "2" (label "Hello", style "rounded=1;fillColor=#fff;").
core::Document sampleDocument()
{
    core::Page page;
    page.setId("p");
    page.appendEntry(core::Cell("0"));
    page.appendEntry(core::Cell::makeLayer("1", "0"));
    page.appendEntry(core::Cell::makeVertex("2", "1", "Hello", "rounded=1;fillColor=#fff;", {10, 10, 80, 40}));
    core::Document doc;
    doc.appendEntry(std::move(page));
    return doc;
}

const core::Cell& cell(const core::Document& doc, const char* id)
{
    return *doc.page(0)->findCell(id);
}

std::vector<std::string> wrapperAttributeNames(const core::Cell& c)
{
    std::vector<std::string> names;
    for (const core::Attribute& a : c.wrapper()->attributes)
        names.push_back(a.name);
    return names;
}

} // namespace

class TestOpsProperties : public QObject {
    Q_OBJECT

private slots:
    void setStyleEditsKeyInPlace()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        const Result r = ops::setStyle(tx, 0, {"2"}, "fillColor", std::string("#000"));
        QVERIFY(r.ok);
        QCOMPARE(r.cells, (std::vector<std::string>{"2"}));
        QCOMPARE(QString::fromStdString(cell(doc, "2").styleText()), QStringLiteral("rounded=1;fillColor=#000;"));
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void newKeyIsAppendedBeforeTrailingSemicolon()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        QVERIFY(ops::setStyle(tx, 0, {"2"}, "strokeColor", std::string("#f00")).ok);
        QCOMPARE(QString::fromStdString(cell(doc, "2").styleText()),
                 QStringLiteral("rounded=1;fillColor=#fff;strokeColor=#f00;"));
    }

    void removalWithNulloptAndEmptyValue()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        {
            Transaction tx(doc);
            QVERIFY(ops::setStyle(tx, 0, {"2"}, "rounded", std::nullopt).ok);
            QCOMPARE(QString::fromStdString(cell(doc, "2").styleText()), QStringLiteral("fillColor=#fff;"));
            tx.rollback();
        }
        QCOMPARE(format::writeDocument(doc), before);
        {
            Transaction tx(doc);
            QVERIFY(ops::setStyle(tx, 0, {"2"}, "fillColor", std::string()).ok);
            QCOMPARE(QString::fromStdString(cell(doc, "2").styleText()), QStringLiteral("rounded=1;"));
            tx.rollback();
        }
        QCOMPARE(format::writeDocument(doc), before);
    }

    void sameValueEmitsNoCommand()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        QVERIFY(ops::setStyle(tx, 0, {"2"}, "fillColor", std::string("#fff")).ok);
        QVERIFY(tx.empty());
        QVERIFY(ops::setStyle(tx, 0, {"2"}, "missing", std::nullopt).ok);
        QVERIFY(tx.empty());
    }

    void invalidStyleKeyFailsWithoutChanges()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        for (const char* key : {"", "a=b", "a;b"}) {
            const Result r = ops::setStyle(tx, 0, {"2"}, key, std::string("x"));
            QVERIFY(!r.ok);
            QCOMPARE(r.error, std::string("invalid style key"));
        }
        QVERIFY(tx.empty());
        QCOMPARE(format::writeDocument(doc), before);
    }

    void styleTextAndStyleNames()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        QVERIFY(ops::setStyleText(tx, 0, {"2"}, "ellipse;").ok);
        QCOMPARE(QString::fromStdString(cell(doc, "2").styleText()), QStringLiteral("ellipse;"));

        QVERIFY(ops::setStyleName(tx, 0, {"2"}, "shadow", true).ok);
        QVERIFY(cell(doc, "2").style().hasName("shadow"));
        QVERIFY(cell(doc, "2").style().hasName("ellipse"));

        const std::size_t steps = tx.savepoint();
        QVERIFY(ops::setStyleName(tx, 0, {"2"}, "ellipse", true).ok); // already present
        QCOMPARE(tx.savepoint(), steps);

        QVERIFY(ops::setStyleName(tx, 0, {"2"}, "ellipse", false).ok);
        QVERIFY(!cell(doc, "2").style().hasName("ellipse"));
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void labelOnPlainAndWrappedCells()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        {
            Transaction tx(doc);
            const Result r = ops::setLabel(tx, 0, "2", "Renamed");
            QVERIFY(r.ok);
            QCOMPARE(QString::fromStdString(cell(doc, "2").value()), QStringLiteral("Renamed"));
            tx.rollback();
        }
        QCOMPARE(format::writeDocument(doc), before);
        {
            Transaction tx(doc);
            QVERIFY(ops::setCellData(tx, 0, "2", "qraw", std::string("1")).ok);
            QVERIFY(ops::setLabel(tx, 0, "2", "Wrapped").ok);
            QVERIFY(cell(doc, "2").hasWrapper());
            QCOMPARE(QString::fromStdString(cell(doc, "2").wrapper()->attributes.value("label")),
                     QStringLiteral("Wrapped"));
            tx.rollback();
        }
        QCOMPARE(format::writeDocument(doc), before);
    }

    void setCellDataWrapsPlainCellInObject()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        QVERIFY(ops::setCellData(tx, 0, "2", "qraw", std::string("1")).ok);
        const core::Cell& wrapped = cell(doc, "2");
        QVERIFY(wrapped.hasWrapper());
        QCOMPARE(wrapped.wrapper()->tag, std::string("object"));
        QCOMPARE(wrapperAttributeNames(wrapped), (std::vector<std::string>{"label", "qraw", "id"}));
        QCOMPARE(QString::fromStdString(wrapped.wrapper()->attributes.value("label")), QStringLiteral("Hello"));
        QCOMPARE(QString::fromStdString(wrapped.wrapper()->attributes.value("qraw")), QStringLiteral("1"));
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void userObjectTagVariant()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        QVERIFY(ops::setCellData(tx, 0, "2", "qraw", std::string("1"), "UserObject").ok);
        QCOMPARE(cell(doc, "2").wrapper()->tag, std::string("UserObject"));
    }

    void removingLastAttributeKeepsWrapper()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        QVERIFY(ops::setCellData(tx, 0, "2", "qraw", std::string("1")).ok);
        QVERIFY(ops::setCellData(tx, 0, "2", "qraw", std::nullopt).ok);
        QVERIFY(cell(doc, "2").hasWrapper());
        QVERIFY(!cell(doc, "2").wrapper()->attributes.has("qraw"));
        QCOMPARE(QString::fromStdString(cell(doc, "2").wrapper()->attributes.value("label")),
                 QStringLiteral("Hello"));
    }

    void removingFromPlainCellEmitsNoCommand()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        QVERIFY(ops::setCellData(tx, 0, "2", "qraw", std::nullopt).ok);
        QVERIFY(tx.empty());
        QVERIFY(!cell(doc, "2").hasWrapper());
    }

    void invalidAttributeNamesFail()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        for (const char* name : {"id", "label", "", "1abc", "a b"}) {
            const Result r = ops::setCellData(tx, 0, "2", name, std::string("x"));
            QVERIFY2(!r.ok, name);
            QCOMPARE(r.error, std::string("invalid attribute name"));
        }
        QVERIFY(tx.empty());
    }

    void editCellDataIsOneCommandPerCellInOrder()
    {
        core::Document doc = sampleDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        const Result r = ops::editCellData(tx, 0, {"2"},
                                           {{"b", std::string("2")}, {"a", std::string("1")}, {"tags", std::string("x y")}});
        QVERIFY(r.ok);
        QCOMPARE(tx.savepoint(), std::size_t(1)); // one ReplaceCell for three properties
        // draw.io wraps in <object>: label first, the id last, new properties in the order given before it.
        QCOMPARE(wrapperAttributeNames(cell(doc, "2")), (std::vector<std::string>{"label", "b", "a", "tags", "id"}));
        QCOMPARE(QString::fromStdString(cell(doc, "2").value()), QStringLiteral("Hello"));
        QCOMPARE(QString::fromStdString(cell(doc, "2").id()), QStringLiteral("2"));
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before); // byte-identical after undo
    }

    void editCellDataEditsInPlaceAndRemoves()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        QVERIFY(ops::editCellData(tx, 0, {"2"}, {{"a", std::string("1")}, {"b", std::string("2")}}).ok);
        // "a" keeps its place, "b" goes and comes back at the end (draw.io's removeAttribute + setAttribute).
        QVERIFY(ops::editCellData(tx, 0, {"2"}, {{"a", std::string("changed")}, {"b", std::nullopt}, {"b", std::string("")}}).ok);
        QCOMPARE(wrapperAttributeNames(cell(doc, "2")), (std::vector<std::string>{"label", "a", "b", "id"}));
        QCOMPARE(QString::fromStdString(cell(doc, "2").wrapper()->attributes.value("a")), QStringLiteral("changed"));
        QVERIFY(cell(doc, "2").wrapper()->attributes.has("b")); // an empty value is a value
        // Removing everything keeps the wrapper, as draw.io does not unwrap.
        QVERIFY(ops::editCellData(tx, 0, {"2"}, {{"a", std::nullopt}, {"b", std::nullopt}}).ok);
        QVERIFY(cell(doc, "2").hasWrapper());
        QCOMPARE(wrapperAttributeNames(cell(doc, "2")), (std::vector<std::string>{"label", "id"}));
    }

    void editCellDataWithoutChangeEmitsNothing()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        QVERIFY(ops::editCellData(tx, 0, {"2"}, {{"gone", std::nullopt}}).ok); // a plain cell has nothing to remove
        QVERIFY(tx.empty());
        QVERIFY(ops::editCellData(tx, 0, {"2"}, {{"a", std::string("1")}}).ok);
        const std::size_t count = tx.savepoint();
        QVERIFY(ops::editCellData(tx, 0, {"2"}, {{"a", std::string("1")}}).ok);
        QCOMPARE(tx.savepoint(), count); // the same value again is no command
    }

    void editCellDataValidatesAllNamesFirst()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        const Result r = ops::editCellData(tx, 0, {"2"}, {{"fine", std::string("1")}, {"1bad", std::string("2")}});
        QVERIFY(!r.ok);
        QVERIFY(tx.empty());
        QVERIFY(!cell(doc, "2").hasWrapper());
    }

    void editCellDataOnSeveralCellsAndTheRoot()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        QVERIFY(ops::editCellData(tx, 0, {"1", "2", "0"}, {{"k", std::string("v")}}).ok);
        for (const char* id : {"0", "1", "2"})
            QVERIFY2(cell(doc, id).hasWrapper() && cell(doc, id).wrapper()->attributes.value("k") == "v", id);
        QCOMPARE(QString::fromStdString(cell(doc, "0").id()), QStringLiteral("0"));
    }

    void dataNameRules()
    {
        for (const char* ok : {"name", "_x", "a-b.c", "a1", "\xC3\xA4rger" /* a with diaeresis */, "\xE6\x97\xA5\xE6\x9C\xAC"})
            QVERIFY2(ops::isValidDataName(ok), ok);
        for (const char* bad : {"", "1a", "-a", ".a", "a b", "a:b", "a=b", "a\"b", "a<b", "label", "id", "placeholders",
                                "\xC3" /* truncated UTF-8 */, "\xC3\x97" /* the multiplication sign */})
            QVERIFY2(!ops::isValidDataName(bad), bad);
        // tooltip, link and tags are ordinary names.
        for (const char* ok : {"tooltip", "link", "tags", "linkTarget"})
            QVERIFY2(ops::isValidDataName(ok), ok);
        QVERIFY(ops::isReservedDataName("label") && ops::isReservedDataName("a:b") && !ops::isReservedDataName("tags"));
    }
};

QTEST_GUILESS_MAIN(TestOpsProperties)
#include "tst_ops_properties.moc"
