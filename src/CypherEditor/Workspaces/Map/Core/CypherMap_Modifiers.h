//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Modifiers.h
//  Purpose: Declares a mesh's modifier stack as the map keeps it:
//           non-destructive mirror, linear and radial arrays, bend, taper
//           and subdivision (Hammer 5's subdivision levels), evaluated by
//           the geometry library's ModifierStack for display and collapsed
//           into the mesh on Apply.
//  Details: The stack lives in the mesh's map record as `modifiers`, the
//           array of stage objects CYMAP.md 6.4 defines (`type`, `enabled`,
//           axes as "x"/"y"/"z", angles in degrees); the geometry library
//           only evaluates it. A mesh without a record yet (made since the
//           last save) gets a map-owned one. Stage types this editor does
//           not know are kept in the file and skipped.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_MODIFIERS_H
#define CYPHER_EDITOR_MAP_MODIFIERS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"
#include "CypherGeometry_ModifierStack.h"

#include <vector>

namespace cypher::editor::map
{

// The mesh's stages in order (none: an empty list). Stages a newer editor
// wrote with an unknown type are skipped.
CYPHER_NODISCARD map_status_t MapModifiers_Read( const map_document_t *pMap, common::u64 mesh, std::vector<geometry::modifier_t> &stagesOut );

// Conservative, allocation-free inspection of the retained member. Missing or
// empty arrays have no stack; any nonempty array or malformed member (including
// explicit null) does. Unlike Read, this includes disabled and unknown stages.
// It neither evaluates the stack nor decides whether its contents are supported.
CYPHER_NODISCARD bool MapModifiers_HasRetainedStack( const map_document_t *pMap, common::u64 mesh ) noexcept;

// Replaces the mesh's stack (each stage must validate). An empty list
// removes the member. *pChangedOut is false when the stack was already
// exactly this.
CYPHER_NODISCARD map_status_t MapModifiers_Write( map_document_t *pMap, common::u64 mesh, const std::vector<geometry::modifier_t> &stages,
    bool *pChangedOut );

// The surface the stack makes from the mesh (for display). NOT_FOUND-like
// UNKNOWN_OBJECT when the mesh has no enabled stage; *pResult initialized.
CYPHER_NODISCARD map_status_t MapModifiers_Evaluate( const map_document_t *pMap, common::u64 mesh, geometry::modifier_result_t *pResult ) noexcept;

// Applies the stack: the mesh becomes the evaluated surface (cage
// identities kept on the original copy, fresh ones for everything else)
// and the stack is removed.
CYPHER_NODISCARD map_status_t MapModifiers_Apply( map_document_t *pMap, common::u64 mesh ) noexcept;

// Hammer 5's subdivision level: the levels of the mesh's subdivide stage
// (0 when it has none). Setting 0 removes the stage; otherwise it is added
// last, or changed in place. Levels above geometry::kSubdivisionLevelsMax
// are rejected.
CYPHER_NODISCARD common::u32 MapModifiers_SubdivisionLevel( const map_document_t *pMap, common::u64 mesh ) noexcept;
CYPHER_NODISCARD map_status_t MapModifiers_SetSubdivisionLevel( map_document_t *pMap, common::u64 mesh, common::u32 levels, bool *pChangedOut );

// Read-only admission for a whole-object affine transform. Refuses combinations
// the retained modifier representation cannot express, before moving the cage.
// Known disabled stages follow the same policy so re-enabling them is safe;
// unknown stages and unsupported subdivision schemes refuse nonidentity edits.
// Does not bake or modify the document. Allocation failure is reported.
CYPHER_NODISCARD map_status_t MapModifiers_CheckTransform( const map_document_t *pMap, common::u64 mesh,
    const math::affine3d_t &affine ) noexcept;

// Updates the stack after an admitted whole-object transform on a PRIVATE
// working copy (discard on failure, as for MapEdit operations). Mirror planes
// must remain axis-aligned; bend's circular plane needs equal scale and signed
// axis mappings; taper's low-to-high direction cannot reverse. Radial arrays
// and distance-welded copies require a proper similarity. Unsupported pairs
// return MODIFIER_TRANSFORM_UNSUPPORTED rather than silently changing shape.
CYPHER_NODISCARD map_status_t MapModifiers_Transform( map_document_t *pMap, common::u64 mesh, const math::affine3d_t &affine ) noexcept;

// The stage type's name in the file ("mirror", ...).
CYPHER_NODISCARD const char *MapModifiers_TypeName( geometry::modifier_kind_t kind ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_MODIFIERS_H
