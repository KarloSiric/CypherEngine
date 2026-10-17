//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_PatchEdit.h
//  Purpose: Declares NetRadiant's patch matrix operations on authored
//           patches: insert and remove control rows or columns, transpose,
//           and invert (flip which side faces out).
//  Details: Inserting splits the last sub-patch span at its midpoint, so
//           the surface keeps its shape and gains controls to bend; removing
//           merges the last two spans. Existing control IDs survive; new
//           controls take fresh IDs above the map's next ID.
//
//           PRIVATE working-copy contract as CypherMap_MeshEdit.h.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_PATCH_EDIT_H
#define CYPHER_EDITOR_MAP_PATCH_EDIT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

namespace cypher::editor::map
{

enum class map_patch_op_t : common::u8 {
    INSERT_ROW = 0u,  // Split the last span along v: more rows of controls.
    INSERT_COLUMN,    // Split the last span along u.
    REMOVE_ROW,       // Merge the last two spans along v (needs two).
    REMOVE_COLUMN,    // Merge the last two spans along u (needs two).
    TRANSPOSE,        // Swap rows and columns (the front face flips).
    INVERT,           // Reverse the columns: the other side faces out.
    COUNT
};

// Applies the operation to every listed patch; any other ID, or a patch the
// operation cannot apply to (one span left to remove), is rejected and the
// map is unchanged.
CYPHER_NODISCARD map_status_t MapPatchEdit_Apply( map_document_t *pMap, common::span_t<const common::u64> patches,
    map_patch_op_t op ) noexcept;

// True when the operation can apply to that patch.
CYPHER_NODISCARD bool MapPatchEdit_Can( const map_document_t *pMap, common::u64 patch, map_patch_op_t op ) noexcept;

// NetRadiant's Thicken: adds a copy of each patch `thickness` behind it,
// along the surface normal at every control, turned to face away, and with
// `seams` a three-row strip along each open border so the pair encloses a
// slab (closed directions such as a cylinder's seam, and collapsed borders
// such as a cone's tip, get none). The new patches share the source's
// layer and owner. pRootsOut (initialized) receives the sources and all new
// patches.
// Patches from a brush face (NetRadiant): flat Bezier patches covering the
// face exactly (one for a four-cornered face, one per corner otherwise),
// with the face's material; the brush stays. pIdsOut (initialized)
// receives the new patches.
CYPHER_NODISCARD map_status_t MapPatchEdit_FromBrushFace( map_document_t *pMap, common::u64 brush, common::u64 side,
    common::vector_t<common::u64> *pIdsOut ) noexcept;

CYPHER_NODISCARD map_status_t MapPatchEdit_Thicken( map_document_t *pMap, common::span_t<const common::u64> patches,
    common::f64 thickness, bool seams, common::vector_t<common::u64> *pRootsOut ) noexcept;

// NetRadiant's Cap: every border of a selected patch that closes on itself
// (a cylinder's or cone's open ends, a lathe's rims) gets a patch filling
// it, its rows running in from the border to the border's centre, facing
// away from the patch and with its material. pRootsOut (initialized)
// receives the patches and their caps; *pCapsOut (optional) counts the
// caps. A patch with no closed border is left alone.
CYPHER_NODISCARD map_status_t MapPatchEdit_Cap( map_document_t *pMap, common::span_t<const common::u64> patches,
    common::vector_t<common::u64> *pRootsOut, common::usize *pCapsOut ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_PATCH_EDIT_H
