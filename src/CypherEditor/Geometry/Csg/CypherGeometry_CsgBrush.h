//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgBrush.h
//  Purpose: Declares Booleans over sets of convex brushes that keep the
//           result as convex brushes - the form a brush-based map (and the
//           BSP-style compile after it) needs.
//  Details: Built on the convex primitives of BrushCSG:
//             DIFFERENCE   every A brush is carved by every B brush in
//                          turn (Hammer's carve), each cut splitting a
//                          piece into at most one fragment per B side;
//             INTERSECTION each pair's intersection, made disjoint from
//                          the pieces already produced, so overlapping A or
//                          B brushes never yield doubled volume;
//             UNION        A as it is plus the parts of B outside A
//                          (a union decomposed into convex pieces - there
//                          is no convex brush for a non-convex union).
//
//           Surfaces: every output side takes the record of the side it
//           came from - an A side keeps A's material and UVs, a cut side
//           from B carries B's (the carve wall), a side from an
//           intersection its operand's - so texturing survives.
//
//           Identity: every output brush and side gets a fresh ID. A piece
//           of A may appear in several fragments, and one source side may
//           bound several of them; reusing its ID would collide. Provenance
//           per output side names the source brush and side instead.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_BRUSH_H
#define CYPHER_EDITOR_GEOMETRY_CSG_BRUSH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgTypes.h"
#include "CypherGeometry_Fragment.h"

namespace cypher::editor::geometry
{

// Bound on output pieces (carving can multiply fragments).
inline constexpr common::usize kCsgBrushPiecesMax = 16384u;

struct csg_brush_side_provenance_t {
    geometry_source_id_t destinationBrushId{};
    geometry_source_id_t destinationSideId{};
    geometry_source_id_t sourceBrushId{};
    geometry_source_id_t sourceSideId{}; // invalid for sides BrushCSG created without a source side
    common::u32 iOperand{ kCsgOperandA };
};

// Appends the result pieces to *pOut (initialized) with fresh IDs from
// *pIdAllocator; *pProvenance (optional, initialized) gets one entry per
// output side. SYMMETRIC_DIFFERENCE and CLIP are UNSUPPORTED (use the mesh
// path). An empty result (e.g. A inside B for a difference) is OK with no
// pieces. Failure leaves *pOut, *pProvenance and the allocator unchanged.
CYPHER_NODISCARD geometry_status_t CsgBrush_TryEvaluate(
    csg_operator_t op,
    common::span_t<const brush_source_t *const> brushesA,
    common::span_t<const brush_source_t *const> brushesB,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    geometry_fragment_t *pOut,
    common::vector_t<csg_brush_side_provenance_t> *pProvenance ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_BRUSH_H
