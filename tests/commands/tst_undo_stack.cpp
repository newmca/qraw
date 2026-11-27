// UndoStack: history navigation, clean state, limit and merge rules, using a
// tiny counter command so that the stack logic is tested in isolation.
#include "qraw/commands/command_group.h"
#include "qraw/commands/undo_stack.h"
#include "qraw/core/document.h"

#include <QTest>

#include <chrono>

using namespace qraw;
using namespace qraw::commands;
using namespace std::chrono_literals;

namespace {

// Adds `delta` to `*target`; consecutive counters sum their deltas.
class CounterCommand final : public Command {
public:
    CounterCommand(int* target, int delta) : target_(target), delta_(delta) {}

    bool apply(core::Document&, ChangeSet&) override
    {
        *target_ += delta_;
        return true;
    }
    void revert(core::Document&, ChangeSet&) override { *target_ -= delta_; }
    bool canMergeWith(const Command& next) const override { return dynamic_cast<const CounterCommand*>(&next); }
    bool mergeWith(const Command& next) override
    {
        const auto* other = dynamic_cast<const CounterCommand*>(&next);
        if (!other)
            return false;
        delta_ += other->delta_;
        return true;
    }

private:
    int* target_;
    int delta_;
};

} // namespace

class TestUndoStack : public QObject {
    Q_OBJECT

    core::Document doc_;
    ChangeSet changes_;
    int value_ = 0;
    UndoStack::Clock::time_point now_{};

    // An already-applied group of `n` counters, each adding `delta`.
    std::unique_ptr<CommandGroup> applied(int delta, int n = 1)
    {
        auto group = std::make_unique<CommandGroup>();
        for (int i = 0; i < n; ++i) {
            auto command = std::make_unique<CounterCommand>(&value_, delta);
            [[maybe_unused]] const bool ok = command->apply(doc_, changes_);
            group->append(std::move(command));
        }
        return group;
    }

    bool pushDelta(UndoStack& stack, std::string name, int delta, const char* key = "", int n = 1)
    {
        return stack.push(std::move(name), applied(delta, n), key);
    }

private slots:
    void init()
    {
        value_ = 0;
        now_ = UndoStack::Clock::time_point{};
    }

    void pushUndoRedo()
    {
        UndoStack stack;
        QVERIFY(!stack.canUndo() && !stack.canRedo());
        pushDelta(stack, "one", 1);
        pushDelta(stack, "two", 10);
        QCOMPARE(stack.count(), 2u);
        QCOMPARE(stack.index(), 2u);
        QCOMPARE(stack.undoText(), std::string_view("two"));
        QCOMPARE(stack.redoText(), std::string_view(""));

        QVERIFY(stack.undo(doc_, changes_));
        QCOMPARE(value_, 1);
        QCOMPARE(stack.index(), 1u);
        QVERIFY(stack.canUndo() && stack.canRedo());
        QCOMPARE(stack.undoText(), std::string_view("one"));
        QCOMPARE(stack.redoText(), std::string_view("two"));

        QVERIFY(stack.undo(doc_, changes_));
        QVERIFY(!stack.undo(doc_, changes_));
        QCOMPARE(value_, 0);

        QVERIFY(stack.redo(doc_, changes_));
        QVERIFY(stack.redo(doc_, changes_));
        QVERIFY(!stack.redo(doc_, changes_));
        QCOMPARE(value_, 11);
        QCOMPARE(stack.text(5), std::string_view(""));
    }

    void pushAfterUndoDiscardsRedoTail()
    {
        UndoStack stack;
        pushDelta(stack, "one", 1);
        pushDelta(stack, "two", 10);
        stack.undo(doc_, changes_);
        pushDelta(stack, "three", 100);
        QCOMPARE(stack.count(), 2u);
        QVERIFY(!stack.canRedo());
        QCOMPARE(stack.undoText(), std::string_view("three"));
    }

    void emptyGroupIgnored()
    {
        UndoStack stack;
        QVERIFY(!stack.push("nothing", std::make_unique<CommandGroup>()));
        QVERIFY(!stack.push("null", nullptr));
        QCOMPARE(stack.count(), 0u);
    }

    void cleanState()
    {
        UndoStack stack;
        QVERIFY(stack.isClean());
        pushDelta(stack, "a", 1);
        QVERIFY(!stack.isClean());
        stack.setClean();
        QVERIFY(stack.isClean());
        stack.undo(doc_, changes_);
        QVERIFY(!stack.isClean());
        stack.redo(doc_, changes_);
        QVERIFY(stack.isClean());
    }

    void cleanIndexInDiscardedTailIsLost()
    {
        UndoStack stack;
        pushDelta(stack, "a", 1);
        pushDelta(stack, "b", 1);
        stack.setClean(); // at 2
        stack.undo(doc_, changes_);
        stack.undo(doc_, changes_);
        pushDelta(stack, "c", 1);
        QCOMPARE(stack.cleanIndex(), std::optional<std::size_t>());
        QVERIFY(!stack.isClean());
    }

    void limitDropsOldest()
    {
        UndoStack stack(3);
        for (int i = 1; i <= 5; ++i)
            pushDelta(stack, std::to_string(i), 1);
        QCOMPARE(stack.count(), 3u);
        QCOMPARE(stack.index(), 3u);
        QCOMPARE(stack.text(0), std::string_view("3"));
        QCOMPARE(stack.text(2), std::string_view("5"));
    }

    void limitShiftsAndDropsCleanIndex()
    {
        UndoStack stack(3);
        pushDelta(stack, "a", 1);
        stack.setClean(); // 1
        pushDelta(stack, "b", 1);
        pushDelta(stack, "c", 1);
        QCOMPARE(stack.cleanIndex(), std::optional<std::size_t>(1));
        pushDelta(stack, "d", 1); // drops "a": clean index 1 -> 0
        QCOMPARE(stack.cleanIndex(), std::optional<std::size_t>(0));
        pushDelta(stack, "e", 1); // drops "b": clean state is gone
        QCOMPARE(stack.cleanIndex(), std::optional<std::size_t>());
    }

    void lowerLimitWithEverythingUndoneDropsFromBack()
    {
        UndoStack stack;
        pushDelta(stack, "a", 1);
        pushDelta(stack, "b", 1);
        pushDelta(stack, "c", 1);
        stack.setClean(); // 3
        while (stack.undo(doc_, changes_)) {
        }
        stack.setLimit(1);
        QCOMPARE(stack.count(), 1u);
        QCOMPARE(stack.index(), 0u);
        QCOMPARE(stack.text(0), std::string_view("a"));
        QCOMPARE(stack.cleanIndex(), std::optional<std::size_t>());
    }

    void mergeSameKey()
    {
        UndoStack stack;
        QVERIFY(!pushDelta(stack, "first", 1, "k"));
        QVERIFY(pushDelta(stack, "second", 10, "k"));
        QCOMPARE(stack.count(), 1u);
        QCOMPARE(stack.undoText(), std::string_view("first"));
        QCOMPARE(value_, 11);
        stack.undo(doc_, changes_);
        QCOMPARE(value_, 0); // one undo reverts both
        stack.redo(doc_, changes_);
        QCOMPARE(value_, 11);
    }

    void mergeRefusedForDifferentKey()
    {
        UndoStack stack;
        pushDelta(stack, "a", 1, "k");
        QVERIFY(!pushDelta(stack, "b", 1, "other"));
        QCOMPARE(stack.count(), 2u);
    }

    void mergeRefusedForEmptyKey()
    {
        UndoStack stack;
        pushDelta(stack, "a", 1);
        QVERIFY(!pushDelta(stack, "b", 1));
        QCOMPARE(stack.count(), 2u);
    }

    void mergeRefusedAfterMergeBreak()
    {
        UndoStack stack;
        pushDelta(stack, "a", 1, "k");
        stack.mergeBreak();
        QVERIFY(!pushDelta(stack, "b", 1, "k"));
        QCOMPARE(stack.count(), 2u);
        QVERIFY(pushDelta(stack, "c", 1, "k")); // a new sequence starts after the push
        QCOMPARE(stack.count(), 2u);
    }

    void mergeRefusedWithRedoTail()
    {
        UndoStack stack;
        pushDelta(stack, "x", 1, "other");
        pushDelta(stack, "a", 1, "k");
        stack.undo(doc_, changes_);
        QVERIFY(!pushDelta(stack, "b", 1, "k")); // must not merge into the undone "a"
        QCOMPARE(stack.count(), 2u);
        QCOMPARE(stack.undoText(), std::string_view("b"));
    }

    void mergeRefusedAtCleanIndex()
    {
        UndoStack stack;
        pushDelta(stack, "a", 1, "k");
        stack.setClean();
        QVERIFY(!pushDelta(stack, "b", 1, "k"));
        QCOMPARE(stack.count(), 2u);
    }

    void mergeWindow()
    {
        UndoStack stack;
        stack.setClock([this] { return now_; });
        stack.setMergeWindow(100ms);
        pushDelta(stack, "a", 1, "k");
        now_ += 50ms;
        QVERIFY(pushDelta(stack, "b", 1, "k")); // within the window; refreshes the timestamp
        now_ += 90ms;
        QVERIFY(pushDelta(stack, "c", 1, "k"));
        now_ += 200ms;
        QVERIFY(!pushDelta(stack, "d", 1, "k"));
        QCOMPARE(stack.count(), 2u);
    }

    void mergeRefusedForDifferentShapes()
    {
        UndoStack stack;
        pushDelta(stack, "a", 1, "k", 1);
        QVERIFY(!pushDelta(stack, "b", 1, "k", 2)); // group sizes differ
        QCOMPARE(stack.count(), 2u);
    }

    void clearResetsState()
    {
        UndoStack stack;
        pushDelta(stack, "a", 1);
        stack.clear();
        QCOMPARE(stack.count(), 0u);
        QVERIFY(stack.isClean());
        QVERIFY(!stack.canUndo());
    }
};

QTEST_GUILESS_MAIN(TestUndoStack)
#include "tst_undo_stack.moc"
