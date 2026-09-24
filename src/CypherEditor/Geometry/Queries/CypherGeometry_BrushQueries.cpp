//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushQueries.cpp
//  Purpose: Implements derived spatial and component queries on brushes.
//  Details: Bounds computation iterates boundary vertices once to build
//           the f64 AABB, then narrows to f32 via the checked conversion
//           in CypherMath. Centroid is a simple arithmetic mean. Component
//           lookups delegate to the boundary arrays with bounds checking.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushQueries.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

math::aabbd_t BrushQueries_ComputeBoundsd(
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( pBoundary == nullptr ) {
        return math::CY_AABBD_EMPTY;
    }

    const common::usize cVerts = BrushBoundary_VertexCount( pBoundary );
    if ( cVerts == 0u ) {
        return math::CY_AABBD_EMPTY;
    }

    math::aabbd_t bounds = math::CY_AABBD_EMPTY;
    for ( common::usize i = 0u; i < cVerts; ++i ) {
        bounds = math::Aabbd_ExpandPoint(
            bounds, pBoundary->vertices.pData[i] );
    }
    return bounds;
}

math::aabb_t BrushQueries_ComputeBounds(
    const brush_boundary_t *pBoundary ) noexcept
{
    const math::aabbd_t boundsD = BrushQueries_ComputeBoundsd( pBoundary );
    if ( math::Aabbd_IsEmpty( boundsD ) ) {
        return math::CY_AABB_EMPTY;
    }

    math::aabb_t result{};
    if ( !math::Aabbd_TryToAabb( boundsD, &result ) ) {
        return math::CY_AABB_EMPTY;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Component counts
// ---------------------------------------------------------------------------

common::usize BrushQueries_VertexCount(
    const brush_boundary_t *pBoundary ) noexcept
{
    return BrushBoundary_VertexCount( pBoundary );
}

common::usize BrushQueries_EdgeCount(
    const brush_boundary_t *pBoundary ) noexcept
{
    return BrushBoundary_EdgeCount( pBoundary );
}

common::usize BrushQueries_FaceCount(
    const brush_boundary_t *pBoundary ) noexcept
{
    return BrushBoundary_FaceCount( pBoundary );
}

// ---------------------------------------------------------------------------
// Vertex position lookup
// ---------------------------------------------------------------------------

geometry_status_t BrushQueries_TryGetVertex(
    const brush_boundary_t *pBoundary,
    common::usize iVertex,
    math::vec3d_t *pPositionOut ) noexcept
{
    if ( pPositionOut != nullptr ) {
        *pPositionOut = {};
    }
    if ( pBoundary == nullptr || pPositionOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( iVertex >= BrushBoundary_VertexCount( pBoundary ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    *pPositionOut = pBoundary->vertices.pData[iVertex];
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Edge lookup
// ---------------------------------------------------------------------------

geometry_status_t BrushQueries_TryGetEdge(
    const brush_boundary_t *pBoundary,
    common::usize iEdge,
    brush_boundary_edge_t *pEdgeOut ) noexcept
{
    if ( pEdgeOut != nullptr ) {
        *pEdgeOut = {};
    }
    if ( pBoundary == nullptr || pEdgeOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( iEdge >= BrushBoundary_EdgeCount( pBoundary ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    *pEdgeOut = pBoundary->edges.pData[iEdge];
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Centroid
// ---------------------------------------------------------------------------

math::vec3d_t BrushQueries_ComputeCentroid(
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( pBoundary == nullptr ) {
        return {};
    }
    const common::usize cVerts = BrushBoundary_VertexCount( pBoundary );
    if ( cVerts == 0u ) {
        return {};
    }
    math::vec3d_t sum{};
    for ( common::usize i = 0u; i < cVerts; ++i ) {
        const math::vec3d_t &v = pBoundary->vertices.pData[i];
        sum.x += v.x;
        sum.y += v.y;
        sum.z += v.z;
    }
    const common::f64 inv = 1.0 / static_cast<common::f64>( cVerts );
    return math::Vec3d_Make( sum.x * inv, sum.y * inv, sum.z * inv );
}

} // namespace cypher::editor::geometry
