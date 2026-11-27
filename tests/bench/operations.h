#pragma once
// The timed operations of qraw-bench. Every operation works on the bytes of
// one generated document and reports its run times in milliseconds.

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <iosfwd>
#include <set>
#include <string>
#include <vector>

namespace qraw::bench {

struct Options {
    int repeat = 3;
    // Operation groups to run (see operationGroups()); empty = all.
    std::set<std::string> groups;
    // Keep a DiagramScene alive on the session during the edit operations, so
    // that move, duplicate, delete and undo include updating the canvas.
    bool withScene = false;
    // Repetitions stop after a run slower than this (the first run still counts).
    double slowRunMs = 10000;
    // A copy of the progress, one line per finished operation; may be null.
    std::ostream* progress = nullptr;
};

struct Measurement {
    std::string name;
    std::vector<double> runsMs;
    bool ok = true;
    QString detail; // the failure reason, or a note on the operation (the export scale)

    [[nodiscard]] double minMs() const;
    [[nodiscard]] double medianMs() const;
};

// The group names accepted by Options::groups, in run order. The edit groups
// (move, duplicate, delete) each produce two measurements: the edit and its undo.
[[nodiscard]] const std::vector<std::string>& operationGroups();

// Runs the selected operations on `original` (the bytes of a generated
// document). Measurements keep the run order; an operation that cannot run
// because an earlier one failed is reported as failed too.
[[nodiscard]] std::vector<Measurement> runOperations(const QByteArray& original, const Options& options);

} // namespace qraw::bench
