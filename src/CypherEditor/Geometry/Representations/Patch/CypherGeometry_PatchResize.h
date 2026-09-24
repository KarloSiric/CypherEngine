//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PatchResize.h
//  Purpose: Declares control-grid shrinking for patches: merging two
//           neighbouring sub-patch columns (or rows) into one, keeping the
//           surface as close to its previous shape as a coarser grid allows.
//  Details: The counterpart of Patch_TryInsertColumn/Row, which split a
//           sub-patch exactly. Removing loses information, so each control
//           row (column) of the merged span is refitted: its end controls
//           stay put and its interior controls are the least-squares fit to
//           the two-piece curve it replaces, sampled with the two pieces
//           taking half the parameter range each. That makes removal the
//           exact inverse of an insert (a curve split at its midpoint is
//           recovered exactly) and otherwise the closest single segment.
//           UVs are refitted the same way, so the texture follows the new
//           shape.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PATCH_RESIZE_H
#define CYPHER_EDITOR_GEOMETRY_PATCH_RESIZE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Patch.h"

namespace cypher::editor::geometry
{

// Merges sub-patch columns iSubU and iSubU + 1 (removing `degree` control
// columns). The merged span keeps the IDs of its first sub-patch's controls
// and of the second's last column; the others are dropped. Needs at least
// two sub-patch columns; INVALID_ARGUMENT otherwise. Failure-atomic.
CYPHER_NODISCARD geometry_status_t Patch_TryRemoveColumn( patch_surface_t *pPatch, common::u32 iSubU ) noexcept;

// Row counterpart.
CYPHER_NODISCARD geometry_status_t Patch_TryRemoveRow( patch_surface_t *pPatch, common::u32 iSubV ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PATCH_RESIZE_H
