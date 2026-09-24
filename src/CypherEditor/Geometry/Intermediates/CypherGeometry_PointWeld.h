//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PointWeld.h
//  Purpose: Declares explicit, deterministic point welding: a remap from
//           input points to cluster representatives.
//  Details: ARCHITECTURE.md: "Proximity never implies an automatic weld.
//           Welding changes topology and is an explicit, previewable
//           transaction." This helper is the explicit step. It never moves
//           a point: each cluster's representative is one of its members
//           (the lexicographically smallest), so welding cannot invent
//           coordinates that were not in the input.
//
//           Clustering is single-linkage within fDistance using the
//           Chebyshev (max-axis) metric, which is what a sort-and-sweep can
//           evaluate exactly. Chains can therefore span more than
//           fDistance end to end; PointWeld_Result reports the largest
//           member-to-representative distance so callers can reject that.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_POINT_WELD_H
#define CYPHER_EDITOR_GEOMETRY_POINT_WELD_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherCommon_Vector.h"
#include "CypherCommon_Span.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

struct point_weld_result_t {
    common::u32 cUnique{ 0u };           // number of clusters
    common::u32 cMerged{ 0u };           // input count - cUnique
    common::f64 fMaxDisplacement{ 0.0 }; // largest member -> representative distance
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Computes pRemapOut[i] = dense cluster index of point i, and
// pRepresentativeOut[c] = input index of cluster c's representative.
// Cluster indices are assigned in order of first appearance in the *input*
// (not sorted order), so welding an already-unique set is the identity.
//
// fDistance = 0 merges only bit-identical points. Both output vectors must
// be initialized; they are resized here. Non-finite points never merge
// with anything (each stays its own cluster). O(n log n + k) where k is
// the number of close pairs examined.
CYPHER_NODISCARD point_weld_result_t PointWeld_BuildRemap(
    common::span_t<const math::vec3d_t> points,
    common::f64 fDistance,
    const common::allocator_t *pScratchAllocator,
    common::vector_t<common::u32> *pRemapOut,
    common::vector_t<common::u32> *pRepresentativeOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_POINT_WELD_H
