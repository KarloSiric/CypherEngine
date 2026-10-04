//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Authored brush-face operations composed from the geometry backend.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_EDITOR_MAP_FACE_EDIT_H
#define CYPHER_EDITOR_MAP_FACE_EDIT_H
#pragma once

#include "CypherMap_Document.h"

namespace cypher::editor::map
{

// These functions edit a PRIVATE working document obtained with MapEdit_Clone.
// The host publishes it only on success and retains the original for undo.
// No file I/O occurs. Brush ID and persistent side ID address the face;
// positional side/face indices are not stable identities.
//
// A positive distance extends the existing convex brush along the selected
// face's outward normal. A negative distance contracts it. This offsets one
// authored plane; it does not duplicate the face or create another brush.
// Every side must still contribute to a closed convex volume after the edit.
// Collapse, redundant sides, unbounded geometry, and numerical overflow fail.
// Identities, UV records, material bindings, layer and owner are preserved.
CYPHER_NODISCARD map_status_t MapFaceEdit_PushPull( map_document_t *pMap,
    common::u64 brushId, common::u64 sideId, common::f64 distance ) noexcept;

// Assigns only the requested face. If its surface record is shared, a separate
// record is appended so other sides retain their material and projection.
// An empty material path clears the binding. Unknown material paths are valid
// authored references; asset availability is checked separately by the editor.
// Failure may leave an unused material reference in the PRIVATE map, so callers
// discard that working copy. Canonical brush publication itself is atomic.
CYPHER_NODISCARD map_status_t MapFaceEdit_SetMaterial( map_document_t *pMap,
    common::u64 brushId, common::u64 sideId, common::string_view_t material ) noexcept;

} // namespace cypher::editor::map
#endif
