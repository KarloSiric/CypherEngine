//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_History.h
//  Purpose: Declares a bounded, linear geometry-local undo/redo stack.
//  Details: The history owns committed change records and replays them
//           inverse (undo) or forward (redo). It is geometry-local: the
//           editor-wide history that interleaves geometry with entities,
//           materials, and settings belongs to the host, which can use
//           change records directly instead of this stack.
//
//           Linearity is enforced, not assumed. The history remembers the
//           revision its last operation produced; undo, redo, and push
//           fail with STALE_REVISION if the document has moved since,
//           because replaying a delta against a state it was not recorded
//           against could silently resurrect or destroy the wrong data.
//
//           Undo and redo publish NEW revisions. Revisions only ever grow;
//           what undo restores is content (the exact prior values), not
//           the revision number.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_HISTORY_H
#define CYPHER_EDITOR_GEOMETRY_HISTORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_ChangeRecord.h"

namespace cypher::editor::geometry
{

struct geometry_history_t {
    geometry_history_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( geometry_history_t );
    ~geometry_history_t() noexcept;

    geometry_document_t *pDocument{ nullptr };
    // records[0, cUndoable) can be undone, newest last;
    // records[cUndoable, count) can be redone, next redo first.
    common::vector_t<geometry_change_record_t *> records{};
    common::usize cUndoable{ 0u };
    common::usize cDepthMax{ 0u };
    // Revision produced by the last push, undo, or redo (or the document's
    // revision at Init). Every operation requires the document to still be
    // at this revision.
    common::u64 expectedRevision{ 0u };
    common::bool_t bInitialized{ false };
};

// Binds the history to a document at its current revision. cDepthMax is
// the maximum number of retained records (undoable plus redoable) and must
// be nonzero; pushing beyond it discards the oldest record.
CYPHER_NODISCARD geometry_status_t GeometryHistory_Init(
    geometry_history_t *pHistory,
    geometry_document_t *pDocument,
    common::usize cDepthMax ) noexcept;

// Destroys every record. Safe on a default or already-shut-down history.
void GeometryHistory_Shutdown( geometry_history_t *pHistory ) noexcept;

CYPHER_NODISCARD common::usize GeometryHistory_UndoCount(
    const geometry_history_t *pHistory ) noexcept;
CYPHER_NODISCARD common::usize GeometryHistory_RedoCount(
    const geometry_history_t *pHistory ) noexcept;

// Takes ownership of a freshly committed record and discards the redo
// tail. The record must come from the bound document and must have been
// committed against expectedRevision (STALE_REVISION otherwise). On any
// failure ownership is NOT taken and the history is unchanged.
CYPHER_NODISCARD geometry_status_t GeometryHistory_TryPush(
    geometry_history_t *pHistory,
    geometry_change_record_t *pRecord ) noexcept;

// Applies the newest undoable record inverse. INVALID_ARGUMENT when there
// is nothing to undo; STALE_REVISION when the document moved. On success
// *ppRecordOut (optional) borrows the record that was undone, which the
// host uses to refresh selection and caches.
CYPHER_NODISCARD geometry_status_t GeometryHistory_TryUndo(
    geometry_history_t *pHistory,
    const geometry_change_record_t **ppRecordOut ) noexcept;

// Applies the next redoable record forward. Statuses as for undo.
CYPHER_NODISCARD geometry_status_t GeometryHistory_TryRedo(
    geometry_history_t *pHistory,
    const geometry_change_record_t **ppRecordOut ) noexcept;

// Destroys every record and rebinds to the document's current revision.
// Used after an external change the history cannot represent.
void GeometryHistory_Clear( geometry_history_t *pHistory ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_HISTORY_H
