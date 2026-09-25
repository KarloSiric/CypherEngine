//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_ReplacementIdentity.h
//  Purpose: Declares atomic source-identity planning for brush replacement.
//  Details: Structural brush edits prepare a complete replacement registry
//           privately, then publish it with an allocation-free ownership swap.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_REPLACEMENTIDENTITY_H
#define CYPHER_EDITOR_GEOMETRY_REPLACEMENTIDENTITY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"

namespace cypher::editor::geometry
{

// Validates the identity transition from currentBrush to replacementBrush.
// When membership must change, builds the complete resulting source-ID
// registry in pPreparedOut without changing the document. On success with no
// membership change, pPreparedOut remains canonical empty and
// *pHasRegistryChangesOut is false. Every failure leaves both outputs in that
// same default state.
CYPHER_NODISCARD geometry_status_t
GeometryReplacementIdentity_TryPrepare(
    const geometry_document_t *pDocument,
    geometry_source_id_t targetBrushId,
    const brush_solid_t *pCurrentBrush,
    const brush_solid_t *pReplacementBrush,
    geometry_source_id_registry_t *pPreparedOut,
    bool *pHasRegistryChangesOut ) noexcept;

// Publishes a prepared registry with an allocation-free ownership swap. The
// previous document registry moves into pPrepared and must subsequently be
// shut down by the caller. Both arguments must be initialized registries.
void GeometryReplacementIdentity_Publish(
    geometry_document_t *pDocument,
    geometry_source_id_registry_t *pPrepared ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_REPLACEMENTIDENTITY_H
