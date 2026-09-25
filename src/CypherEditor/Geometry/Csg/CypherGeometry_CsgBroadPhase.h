//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgBroadPhase.h
//  Purpose: Declares CSG candidate generation: the pairs (triangle of A,
//           triangle of B) whose bounding boxes touch or overlap.
//  Details: A sweep over x with a y/z box test. Boxes are closed (touching
//           counts), because a triangle of one operand lying exactly on a
//           face of the other is the coplanar case the pipeline must see.
//           Output is sorted by (A triangle, B triangle), so equal input
//           always gives the same pair list regardless of sweep internals.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_BROAD_PHASE_H
#define CYPHER_EDITOR_GEOMETRY_CSG_BROAD_PHASE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgTypes.h"

namespace cypher::editor::geometry
{

struct csg_pair_t {
    common::u32 iA{ 0u }; // A's triangle index
    common::u32 iB{ 0u }; // B's triangle index (operand-local)
};

// Appends the candidate pairs to *pPairsOut (initialized; cleared first).
// LIMIT_EXCEEDED beyond cPairsMax pairs (a guard against pathological
// overlap, e.g. two dense coincident meshes).
CYPHER_NODISCARD geometry_status_t CsgBroadPhase_TryCollect(
    const csg_operand_t *pA,
    const csg_operand_t *pB,
    common::usize cPairsMax,
    common::vector_t<csg_pair_t> *pPairsOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_BROAD_PHASE_H
