//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgCleanup.h
//  Purpose: Declares CSG cleanup: removing constructed vertices that no
//           longer shape anything, and the final structural validation of a
//           result before it is published.
//  Details: A cut leaves intersection points on the outline of faces that
//           survive whole. Where such a point ends up between exactly two
//           edges on a straight line, it carries no shape and only adds
//           density, so it goes - but only constructed points: an authored
//           vertex keeps its identity even when it looks redundant, since
//           something else may refer to it.
//
//           Validation: each edge on at most two faces, each directed edge
//           once (consistent winding), and for a solid result every edge on
//           exactly two faces. Failure reports NON_MANIFOLD / OPEN_VOLUME
//           with the offending edge's midpoint as witness.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_CLEANUP_H
#define CYPHER_EDITOR_GEOMETRY_CSG_CLEANUP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgReconstruct.h"

namespace cypher::editor::geometry
{

// Collinearity tolerance relative to the two edge lengths.
inline constexpr common::f64 kCsgCollinearRelative = 1e-12;

// *pcRemovedOut (optional) counts removed vertices.
CYPHER_NODISCARD geometry_status_t CsgCleanup_TryRemoveRedundantVertices( csg_mesh_result_t *pResult, common::usize *pcRemovedOut ) noexcept;

CYPHER_NODISCARD geometry_status_t CsgCleanup_TryValidate( const csg_mesh_result_t *pResult, bool bExpectClosed, csg_diagnostics_t *pDiag ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_CLEANUP_H
