# Commands and undo

The headers in `src/commands/include/qraw/commands/` are the contract. This document describes the design; `tests/commands/` covers it.

Upstream references are relative to `$DRAWIO_SRC/src/main/webapp/` (Apache-2.0, read only). Each operation's header comment cites the function it follows.

## Layers

```
UI ──▶ Editor ──edit(name, fn, mergeKey)──▶ Transaction ◀── ops::* (compose primitives)
         │                                      │
         ├── UndoStack (CommandGroup per step)  └── primitives (the only mutators of core)
         └── DocumentObserver::documentChanged(ChangeSet, origin)
```

| Header | Content |
|---|---|
| `result.h` | `Result` (ok/error, cells to select, page to show) |
| `change_set.h` | `ChangeSet`: per-page added/removed/changed/reordered, page added/removed/reset, page order, document header |
| `command.h` | `Command`: `apply` (checks preconditions, returns false and changes nothing), `revert`, `canMergeWith`/`mergeWith` |
| `command_group.h` | `CommandGroup`: ordered commands = one undo step; merges child-wise, all or nothing |
| `primitives.h` | `InsertCell`, `RemoveCell`, `ReplaceCell`, `ReorderCells`, `InsertPage`, `RemovePage`, `MovePage`, `ReplacePageAttributes`, `ReplaceDocumentHeader` |
| `transaction.h` | `Transaction`: apply-on-add, savepoints, rollback, commit |
| `queries.h` | read-only model queries ported from mxGraphModel/mxGraph, plus `removeSubtree`/`moveSubtree` |
| `undo_stack.h` | `UndoStack` |
| `editor.h` | `Editor`, `DocumentObserver`, `ChangeOrigin` |
| `ops_cells.h` | add, remove (draw.io deleteCells), move, resize, connect, disconnect, waypoints |
| `ops_layers.h` `removeLayers` | layer removal (built on removeCells) |
| `ops_structure.h` | z-order, edge-parent maintenance, reparent, removeFromGroup, group, ungroup |
| `ops_properties.h` | style keys/text/names, label, custom data (wrapping) |
| `ops_pages.h` | page add/remove/duplicate/rename/move/properties |
| `ops_layers.h` (rest) | layer add/duplicate/rename/move/visible/locked |

## Why primitives with whole-value snapshots

Undo followed by redo must give byte-identical saves. All mutations therefore go through nine primitives (ten with `ReplaceCells`, D-036). Each one stores whole values (a `core::Cell`, a `core::Page`, attribute lists) and exact entry positions. `revert()` puts the previous value back at the previous position, so the model is equal and saving is deterministic (ARCHITECTURE.md, lossless model rule 6). Operations never subclass `Command`. They only compose primitives inside a `Transaction`, so exactness is proven once, in `primitives.cpp`.

- **Positions are entry indices**, not z-indices among cells. `<root>` interleaves foreign `XmlNode`s with cells, and `<mxfile>` interleaves foreign nodes with pages. Restoring by cell z-index would move a cell to the other side of a foreign node. Two small core additions support this: `Page::entryIndex`/`insertCellAt` and `Document::pageEntryIndex`/`insertPageAt` (D-016).
- **Addressing.** Pages are addressed by index and cells by id within the page. Commands are applied and reverted strictly in sequence, so every index is resolved against the state it was computed on.
- **Capture on apply.** `RemoveCell`, `ReplaceCell` and the other primitives capture their "before" state in `apply()`. Operations only describe the "after" state.
- **Apply-on-add.** `Transaction::add` applies a command at once (draw.io's `beginUpdate`/`endUpdate` model), so later steps of an operation see the earlier ones.
- **ChangeSet per command.** The transaction keeps one ChangeSet per command. `rollbackTo(savepoint)` therefore restores the exact net ChangeSet, without spurious "changed" records.

## Transaction, nesting, rollback

- One `Transaction` is open per edit. `Editor::edit` nests by taking a `savepoint()`. A failing inner `fn` rolls back only its own part, and the outer `fn` decides what to do next. The outermost edit is one undo entry.
- A failed `Result` from `fn` or an exception means full rollback, no undo entry and no notification. A successful edit that changed nothing records nothing.
- An operation that fails after it has added commands does not have to clean up. The Editor or the caller rolls back to the savepoint. Helpers that are called from other operations (`removeSubtree`) roll back their own partial work, so they can be retried.

## UndoStack

- The default limit is 100 (mxUndoManager, `mxgraph/src/util/mxUndoManager.js:75`). When the limit is reached, the oldest entries are dropped and indices shift. A dropped clean entry makes `cleanIndex()` nullopt.
- `push` discards the redo tail first, as mxUndoManager's `trim()` does.
- **Merging (coalescing).** A push merges into the top entry when all of these hold:
  - the merge key is non-empty and equal to the top entry's key,
  - there is no redo tail and the top entry is not the clean state,
  - `mergeBreak()` was not called since the top entry was pushed,
  - the optional time window has not expired,
  - `CommandGroup::canMergeWith` agrees, that is, the groups have the same shape and each `ReplaceCell` targets the same cell.

  A drag is a sequence of `edit("Move", moveCells(...), "move:<ids>")` calls, one per mouse move. The UI calls `mergeBreak()` on release. Observers see every step, which gives the live preview, and the stack holds one entry.
- Undo and redo each produce a ChangeSet, which is delivered with `ChangeOrigin::Undo` or `ChangeOrigin::Redo`.

## Operation semantics (summary)

The details are in the header comments. These are the draw.io behaviours that matter most:

- **Delete** follows `Graph.deleteCells` → `mxGraph.removeCells(cells, includeEdges)`. Plain Delete keeps connected edges and disconnects them: the edge's end gets a terminal point and the terminal attribute is removed. Ctrl+Delete (`deleteAll`, `includeEdges`) deletes them. Descendants always go with their parent. A transparent parent whose children were all removed goes as well (Graph.js:38250). Without a view, terminal points use upstream's own geometry fallback (mxGraph.js:5279-5350).
- **Move** follows `mxGraph.moveCells`/`cellsMoved`/`translateCell`. It uses topmost cells only and drops relative edge labels whose terminal moves. Edges in the set whose terminal is not moved along are disconnected (disconnectOnMove). Relative children have their offset rounded the way JS `Math.round` rounds. Waypoints are translated.
- **Resize** follows `cellsResized`. Relative geometries change their offset. `extendParent` grows the parents.
- **Group** (Ctrl+G) follows `groupCells`. A single cell gets `container=1` instead. A new `group` vertex (connectable=0) is sized to the geometry bounding box. The children move into it and become relative to it. Edges are re-parented to the nearest common ancestor (`updateEdgeParent`).
- **Ungroup** follows `ungroupCells`. The children move to the group's parent with absolute positions kept. Only transparent groups are removed, together with the edges connected to them (Graph.removeCellsAfterUngroup). Remaining childless vertices get `container=0`.
- **Reparent** is `moveCells(..., target)` → `cellsAdded(absolute)`: coordinates are converted through `getOrigin`, and edge parents are maintained.
- **Z-order** follows `cellsOrdered`, with its index arithmetic. A cell moves together with its subtree block.
- **Custom data** follows Edit Data and `setAttributeForCell`. A plain cell is wrapped (`object` or `UserObject`), the label moves to the wrapper and new attributes go before `id`.
- **Layers** follow the Layers dialog. New layers are "Untitled Layer". Removal is `removeCells(layers, false)` plus a fresh default layer when none is left. Duplication creates "Copy of X" with new ids and terminals outside the copy dropped. Lock is `locked=1/0`.
- **Pages.** A new page is "Page-N" with draw.io's default model and a GUID id. Duplication creates "Copy of X". The last page cannot be deleted. `<mxfile pages="N">` is kept in sync when the file has it.

### Divergences from draw.io

These are deliberate and also recorded in the headers:

1. Removing or disconnecting an edge that has no geometry still clears the terminal. Upstream leaves a dangling id.
2. Disconnect-on-move and disconnect-on-delete place the end at the terminal's center. Upstream uses the rendered end point, which needs a view.
3. A duplicated page keeps its cell ids. Upstream regenerates them with a GUID prefix. Ids only need to be unique per page (D-009).
4. Transparency for ungroup and empty-parent removal is approximated from the cell's own style and the default stylesheet's `group` and `text` styles (`queries.h isTransparentCell`). Render's style resolution could be injected instead.
5. Not modelled yet: tables (row and column resizing on delete or move), `contract` parents, constrainChild, groupPadding, rotation in bounding boxes, transparentBounds groups. These are container work.

## Known limitations

- **Duplicate cell ids** on a page are addressed by the first occurrence (`Page::findCell`). `Page::validate()` already warns about them on load.
- **Performance (D-025).** `Page` keeps incremental indexes by id, parent, source and target, so `children`, `descendants` and `connectedEdges` cost O(result). `ReplaceCell` goes through `Page::replaceCell`, which relinks the cell when its parent, source or target changes. Hot loops use sets and the indexes: `addCells` tracks the entry range of each receiving parent as it inserts (`ParentRanges`), `allEdges` collects edges per terminal, `isWithin` tests set membership in O(depth). What remains O(n) per cell is moving or removing one entry (vector shift plus index shift), so group, ungroup and delete of k cells cost O(k·n); on 5000 cells that is about 0.5 s in release.

## Tests

| File | What |
|---|---|
| `tests/commands/tst_foundation.cpp` | ChangeSet folding, primitive exactness with interleaved foreign entries, ReorderCells landing rules, savepoints, merge, page primitives, queries, **primitive corpus script** |
| `tests/commands/corpus_harness.h` | `checkUndoRedoIdentity(path, script)`: save → steps → save; undo all == first save; redo all == second save; undo all again == first save |
| `tst_undo_stack.cpp`, `tst_editor.cpp`, `tst_editor_corpus.cpp` | stack invariants, merge rules, limit, clean, nesting, observers; corpus through Editor/UndoStack |
| `tst_ops_cells.cpp`, `tst_ops_cells_corpus.cpp` | each cell operation plus its corpus script |
| `tst_ops_structure.cpp`, `tst_ops_structure_corpus.cpp` | idem |
| `tst_ops_pages_layers.cpp`, `tst_ops_properties.cpp`, `tst_ops_pages_layers_corpus.cpp` | idem |

Every `tst_*.cpp` in `tests/commands/` is registered automatically as `tst_commands_<stem>` (D-014). The tests link `qraw_commands` and `qraw_format`, and `QRAW_CORPUS_DIRS` names the corpus folders.

**Corpus scripts** are deterministic per file. They pick their targets from the first page by kind (first vertex, first edge, first layer, and so on). A step that is not applicable is skipped, and a failing step is rolled back and counted. Each test asserts that `recorded > 0` holds for the corpus as a whole, so a script that never fires is noticed.
