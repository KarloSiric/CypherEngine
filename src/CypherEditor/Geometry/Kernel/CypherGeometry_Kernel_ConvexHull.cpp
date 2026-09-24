//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_ConvexHull.cpp
//  Purpose: Implements bounded, failure-atomic 3D convex hull construction.
//  Details: Uses an incremental triangular hull. All temporary storage is
//           allocator-backed and bounded by the explicit geometry policy;
//           fixed working-array limits are deliberately avoided.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//  - Hardened for policy bounds and failure atomicity on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Kernel_ConvexHull.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace cypher::math;

namespace
{

struct hull_tri_t {
    u32 v[3]{};
    vec3d_t normal{};
    f64 dist{ 0.0 };
    bool alive{ false };
    u32 adj[3]{ CY_U32_MAX, CY_U32_MAX, CY_U32_MAX };
};

struct horizon_edge_t {
    u32 va{ 0u };
    u32 vb{ 0u };
    u32 iSurvivingTriangle{ CY_U32_MAX };
};

bool HullIsInitialized( const convex_hull_t *pHull ) noexcept
{
    return pHull != nullptr &&
           pHull->faces.pAllocator != nullptr &&
           Vector_IsValid( &pHull->faces );
}

bool HullIsCanonicalEmpty( const convex_hull_t &hull ) noexcept
{
    return hull.faces.pData == nullptr &&
           hull.faces.nCount == 0u &&
           hull.faces.nCapacity == 0u &&
           hull.faces.pAllocator == nullptr;
}

bool BrushIsCanonicalEmpty( const brush_solid_t &brush ) noexcept
{
    return brush.sides.pData == nullptr &&
           brush.sides.nCount == 0u &&
           brush.sides.nCapacity == 0u &&
           brush.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( brush.sourceId );
}

bool PointIsWithinPolicy(
    vec3d_t point,
    const geometry_numerical_policy_t &policy ) noexcept
{
    return std::fabs( point.x ) <= policy.fCoordinateMagnitudeLimit &&
           std::fabs( point.y ) <= policy.fCoordinateMagnitudeLimit &&
           std::fabs( point.z ) <= policy.fCoordinateMagnitudeLimit;
}

f64 PointPlaneDistance( const hull_tri_t &tri, vec3d_t point ) noexcept
{
    return Vec3d_Dot( tri.normal, point ) + tri.dist;
}

geometry_status_t ComputeTrianglePlane(
    hull_tri_t *pTriangle,
    const vec3d_t *pPoints,
    vec3d_t interiorPoint,
    const geometry_numerical_policy_t &policy ) noexcept
{
    if ( pTriangle == nullptr || pPoints == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const vec3d_t a = pPoints[pTriangle->v[0]];
    const vec3d_t b = pPoints[pTriangle->v[1]];
    const vec3d_t c = pPoints[pTriangle->v[2]];
    const vec3d_t edgeAB = Vec3d_Subtract( b, a );
    const vec3d_t edgeAC = Vec3d_Subtract( c, a );
    if ( !Vec3d_IsFinite( edgeAB ) || !Vec3d_IsFinite( edgeAC ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    // Normalize one edge before taking the cross product. This produces the
    // same normal direction while avoiding the avoidable overflow of
    // cross(edgeAB, edgeAC) for large but otherwise valid coordinates.
    vec3d_t unitAB{};
    f64 edgeLength = 0.0;
    if ( !Vec3d_TryNormalize(
             edgeAB, 0.0, &unitAB, &edgeLength ) ) {
        return geometry_status_t::DEGENERATE;
    }

    const vec3d_t perpendicular = Vec3d_Cross( unitAB, edgeAC );
    if ( !Vec3d_IsFinite( perpendicular ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    vec3d_t normal{};
    f64 altitude = 0.0;
    if ( !Vec3d_TryNormalize(
             perpendicular, 0.0, &normal, &altitude ) ) {
        return geometry_status_t::DEGENERATE;
    }

    // area = edgeLength * altitude / 2. Comparing in quotient form avoids
    // overflowing that product and still applies the policy in world units.
    const f64 minimumAltitude =
        ( policy.fMinimumFaceArea / edgeLength ) * 2.0;
    if ( !Scalar_IsFinite( minimumAltitude ) ||
         altitude < minimumAltitude ) {
        return geometry_status_t::DEGENERATE;
    }

    f64 distance = -Vec3d_Dot( normal, a );
    f64 interiorDistance =
        Vec3d_Dot( normal, interiorPoint ) + distance;
    if ( !Vec3d_IsFinite( normal ) ||
         !Scalar_IsFinite( distance ) ||
         !Scalar_IsFinite( interiorDistance ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    // Winding is a topological decision, so use the exact-sign predicate
    // rather than the rounded plane-distance sign. The metric plane remains
    // responsible for world-unit tolerances after orientation is established.
    const i32 interiorOrientation = Orient3D( a, b, c, interiorPoint );
    if ( interiorOrientation == 0 ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( interiorOrientation < 0 ) {
        normal = Vec3d_Negate( normal );
        distance = -distance;
        interiorDistance = -interiorDistance;

        // The face indices and stored normal form one contract. Reversing only
        // the normal would leave back-facing winding even though adjacency
        // happened to remain closed.
        const u32 temporary = pTriangle->v[1];
        pTriangle->v[1] = pTriangle->v[2];
        pTriangle->v[2] = temporary;
    }
    if ( interiorDistance > policy.fCoplanarDistanceTolerance ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    pTriangle->normal = normal;
    pTriangle->dist = distance;
    return geometry_status_t::OK;
}

geometry_status_t TryAppendTriangle(
    vector_t<hull_tri_t> *pTriangles,
    const vec3d_t *pPoints,
    vec3d_t interiorPoint,
    const geometry_policy_t &policy,
    u32 iA,
    u32 iB,
    u32 iC,
    u32 *pIndexOut ) noexcept
{
    if ( pTriangles == nullptr || pIndexOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pIndexOut = CY_U32_MAX;

    if ( pTriangles->nCount >= static_cast<usize>( CY_U32_MAX ) ||
         static_cast<u64>( pTriangles->nCount ) >=
             policy.limits.cIntersectionEventsMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    hull_tri_t triangle{};
    triangle.v[0] = iA;
    triangle.v[1] = iB;
    triangle.v[2] = iC;
    triangle.alive = true;
    const geometry_status_t planeStatus = ComputeTrianglePlane(
        &triangle, pPoints, interiorPoint, policy.numerical );
    if ( planeStatus != geometry_status_t::OK ) {
        return planeStatus;
    }
    if ( !Vector_PushBack( pTriangles, triangle ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    *pIndexOut = static_cast<u32>( pTriangles->nCount - 1u );
    return geometry_status_t::OK;
}

bool TryLinkTriangles(
    vector_t<hull_tri_t> *pTriangles,
    u32 iFrom,
    u32 iTo ) noexcept
{
    if ( pTriangles == nullptr ||
         static_cast<usize>( iFrom ) >= pTriangles->nCount ||
         static_cast<usize>( iTo ) >= pTriangles->nCount ||
         iFrom == iTo ) {
        return false;
    }

    hull_tri_t &from = pTriangles->pData[iFrom];
    hull_tri_t &to = pTriangles->pData[iTo];
    for ( u32 iEdgeFrom = 0u; iEdgeFrom < 3u; ++iEdgeFrom ) {
        const u32 iA = from.v[iEdgeFrom];
        const u32 iB = from.v[( iEdgeFrom + 1u ) % 3u];
        for ( u32 iEdgeTo = 0u; iEdgeTo < 3u; ++iEdgeTo ) {
            if ( to.v[iEdgeTo] == iB &&
                 to.v[( iEdgeTo + 1u ) % 3u] == iA ) {
                from.adj[iEdgeFrom] = iTo;
                to.adj[iEdgeTo] = iFrom;
                return true;
            }
        }
    }
    return false;
}

bool LiveAdjacencyIsClosed(
    const vector_t<hull_tri_t> &triangles ) noexcept
{
    for ( usize iTriangle = 0u;
          iTriangle < triangles.nCount;
          ++iTriangle ) {
        const hull_tri_t &triangle = triangles.pData[iTriangle];
        if ( !triangle.alive ) {
            continue;
        }
        for ( u32 iEdge = 0u; iEdge < 3u; ++iEdge ) {
            const u32 iNeighbor = triangle.adj[iEdge];
            if ( iNeighbor == CY_U32_MAX ||
                 static_cast<usize>( iNeighbor ) >= triangles.nCount ||
                 !triangles.pData[iNeighbor].alive ) {
                return false;
            }

            const hull_tri_t &neighbor = triangles.pData[iNeighbor];
            const u32 iA = triangle.v[iEdge];
            const u32 iB = triangle.v[( iEdge + 1u ) % 3u];
            bool bReciprocal = false;
            for ( u32 iNeighborEdge = 0u;
                  iNeighborEdge < 3u;
                  ++iNeighborEdge ) {
                if ( neighbor.v[iNeighborEdge] == iB &&
                     neighbor.v[( iNeighborEdge + 1u ) % 3u] == iA &&
                     neighbor.adj[iNeighborEdge] == iTriangle ) {
                    bReciprocal = true;
                    break;
                }
            }
            if ( !bReciprocal ) {
                return false;
            }
        }
    }
    return true;
}

geometry_status_t TryFindInitialTetrahedron(
    const vec3d_t *pPoints,
    usize cPoints,
    const geometry_numerical_policy_t &policy,
    u32 *pI0,
    u32 *pI1,
    u32 *pI2,
    u32 *pI3 ) noexcept
{
    *pI0 = 0u;
    *pI1 = 0u;
    *pI2 = 0u;
    *pI3 = 0u;

    f64 bestPointDistance = 0.0;
    for ( usize iPoint = 1u; iPoint < cPoints; ++iPoint ) {
        const vec3d_t displacement =
            Vec3d_Subtract( pPoints[iPoint], pPoints[0] );
        f64 distance = 0.0;
        if ( !Vec3d_IsFinite( displacement ) ||
             !Vec3d_TryLength( displacement, &distance ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( distance > bestPointDistance ) {
            bestPointDistance = distance;
            *pI1 = static_cast<u32>( iPoint );
        }
    }
    if ( bestPointDistance < policy.fMinimumEdgeLength ) {
        return geometry_status_t::DEGENERATE;
    }

    const vec3d_t lineDirection =
        Vec3d_Subtract( pPoints[*pI1], pPoints[*pI0] );
    vec3d_t unitLine{};
    f64 lineLength = 0.0;
    if ( !Vec3d_TryNormalize(
             lineDirection, 0.0, &unitLine, &lineLength ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    f64 bestLineDistance = 0.0;
    for ( usize iPoint = 0u; iPoint < cPoints; ++iPoint ) {
        if ( iPoint == *pI0 || iPoint == *pI1 ) {
            continue;
        }
        const vec3d_t relative =
            Vec3d_Subtract( pPoints[iPoint], pPoints[*pI0] );
        const vec3d_t perpendicular = Vec3d_Cross( relative, unitLine );
        f64 distance = 0.0;
        if ( !Vec3d_IsFinite( relative ) ||
             !Vec3d_IsFinite( perpendicular ) ||
             !Vec3d_TryLength( perpendicular, &distance ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( distance > bestLineDistance ) {
            bestLineDistance = distance;
            *pI2 = static_cast<u32>( iPoint );
        }
    }
    if ( bestLineDistance < policy.fMinimumEdgeLength ) {
        return geometry_status_t::DEGENERATE;
    }

    const f64 minimumAltitude =
        ( policy.fMinimumFaceArea / lineLength ) * 2.0;
    if ( !Scalar_IsFinite( minimumAltitude ) ||
         bestLineDistance < minimumAltitude ) {
        return geometry_status_t::DEGENERATE;
    }

    const vec3d_t planePerpendicular = Vec3d_Cross(
        unitLine,
        Vec3d_Subtract( pPoints[*pI2], pPoints[*pI0] ) );
    vec3d_t planeNormal{};
    if ( !Vec3d_TryNormalize(
             planePerpendicular, 0.0, &planeNormal, nullptr ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    const f64 planeDistance =
        -Vec3d_Dot( planeNormal, pPoints[*pI0] );

    f64 bestPlaneDistance = 0.0;
    bool bFoundNonCoplanarPoint = false;
    for ( usize iPoint = 0u; iPoint < cPoints; ++iPoint ) {
        if ( iPoint == *pI0 || iPoint == *pI1 || iPoint == *pI2 ) {
            continue;
        }
        if ( Orient3D(
                 pPoints[*pI0], pPoints[*pI1],
                 pPoints[*pI2], pPoints[iPoint] ) == 0 ) {
            continue;
        }
        const f64 distance = std::fabs(
            Vec3d_Dot( planeNormal, pPoints[iPoint] ) + planeDistance );
        if ( !Scalar_IsFinite( distance ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !bFoundNonCoplanarPoint || distance > bestPlaneDistance ) {
            bFoundNonCoplanarPoint = true;
            bestPlaneDistance = distance;
            *pI3 = static_cast<u32>( iPoint );
        }
    }
    if ( !bFoundNonCoplanarPoint ||
         bestPlaneDistance < policy.fPlanarityTolerance ) {
        return geometry_status_t::DEGENERATE;
    }

    return geometry_status_t::OK;
}

geometry_status_t TryCollectUniquePlanes(
    vector_t<planed_t> *pPlanes,
    const convex_hull_t &hull,
    const vec3d_t *pPoints,
    const geometry_policy_t &policy ) noexcept
{
    const f64 parallelThreshold =
        std::cos( policy.numerical.fAngularToleranceRadians );
    for ( usize iFace = 0u; iFace < hull.faces.nCount; ++iFace ) {
        const convex_hull_face_t &face = hull.faces.pData[iFace];
        const f64 distance = -Vec3d_Dot(
            face.normal, pPoints[face.indices[0]] );
        if ( !Scalar_IsFinite( distance ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }

        bool bMerged = false;
        for ( usize iPlane = 0u; iPlane < pPlanes->nCount; ++iPlane ) {
            const planed_t &plane = pPlanes->pData[iPlane];
            if ( Vec3d_Dot( face.normal, plane.normal ) >=
                     parallelThreshold &&
                 std::fabs( distance - plane.d ) <=
                     policy.numerical.fCoplanarDistanceTolerance ) {
                bMerged = true;
                break;
            }
        }
        if ( bMerged ) {
            continue;
        }

        if ( static_cast<u64>( pPlanes->nCount ) >=
             policy.limits.cBrushSidesPerBrushMax ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        if ( !Vector_PushBack(
                 pPlanes, Planed_Make( face.normal, distance ) ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    return pPlanes->nCount >= 4u
        ? geometry_status_t::OK
        : geometry_status_t::DEGENERATE;
}

} // namespace

geometry_status_t ConvexHull_Init(
    convex_hull_t *pHull,
    const allocator_t *pAllocator ) noexcept
{
    if ( pHull == nullptr || pAllocator == nullptr ||
         !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pHull->faces.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !HullIsCanonicalEmpty( *pHull ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return Vector_Init( &pHull->faces, pAllocator )
        ? geometry_status_t::OK
        : geometry_status_t::ALLOCATION_FAILED;
}

void ConvexHull_Shutdown( convex_hull_t *pHull ) noexcept
{
    if ( pHull == nullptr ) {
        return;
    }
    Vector_Shutdown( &pHull->faces );
}

usize ConvexHull_FaceCount( const convex_hull_t *pHull ) noexcept
{
    return HullIsInitialized( pHull )
        ? Vector_Count( &pHull->faces )
        : 0u;
}

geometry_status_t ConvexHull_TryBuild(
    convex_hull_t *pHull,
    const vec3d_t *pPoints,
    usize cPoints,
    const geometry_policy_t &policy ) noexcept
{
    if ( pHull == nullptr || pPoints == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !HullIsInitialized( pHull ) ) {
        return HullIsCanonicalEmpty( *pHull )
            ? geometry_status_t::NOT_INITIALIZED
            : geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( cPoints < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( cPoints > static_cast<usize>( CY_U32_MAX ) ||
         static_cast<u64>( cPoints ) > policy.limits.cVerticesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( policy.limits.cFacesMax < 4u ||
         policy.limits.cIntersectionEventsMax < 4u ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    for ( usize iPoint = 0u; iPoint < cPoints; ++iPoint ) {
        if ( !Vec3d_IsFinite( pPoints[iPoint] ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !PointIsWithinPolicy( pPoints[iPoint], policy.numerical ) ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    }

    u32 i0 = 0u;
    u32 i1 = 0u;
    u32 i2 = 0u;
    u32 i3 = 0u;
    geometry_status_t status = TryFindInitialTetrahedron(
        pPoints, cPoints, policy.numerical, &i0, &i1, &i2, &i3 );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const vec3d_t interiorPoint = Vec3d_Scale(
        Vec3d_Add(
            Vec3d_Add( pPoints[i0], pPoints[i1] ),
            Vec3d_Add( pPoints[i2], pPoints[i3] ) ),
        0.25 );
    if ( !Vec3d_IsFinite( interiorPoint ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    const allocator_t *pAllocator = pHull->faces.pAllocator;
    vector_t<hull_tri_t> triangles{};
    vector_t<u8> visible{};
    vector_t<horizon_edge_t> horizon{};
    vector_t<convex_hull_face_t> stagedFaces{};
    if ( !Vector_Init( &triangles, pAllocator, 4u ) ||
         !Vector_Init( &visible, pAllocator ) ||
         !Vector_Init( &horizon, pAllocator ) ||
         !Vector_Init( &stagedFaces, pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    u32 initialFaces[4]{};
    status = TryAppendTriangle(
        &triangles, pPoints, interiorPoint, policy,
        i0, i1, i2, &initialFaces[0] );
    if ( status == geometry_status_t::OK ) {
        status = TryAppendTriangle(
            &triangles, pPoints, interiorPoint, policy,
            i0, i2, i3, &initialFaces[1] );
    }
    if ( status == geometry_status_t::OK ) {
        status = TryAppendTriangle(
            &triangles, pPoints, interiorPoint, policy,
            i0, i3, i1, &initialFaces[2] );
    }
    if ( status == geometry_status_t::OK ) {
        status = TryAppendTriangle(
            &triangles, pPoints, interiorPoint, policy,
            i1, i3, i2, &initialFaces[3] );
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    for ( u32 iFace = 0u; iFace < 4u; ++iFace ) {
        for ( u32 iOther = iFace + 1u; iOther < 4u; ++iOther ) {
            (void)TryLinkTriangles(
                &triangles, initialFaces[iFace], initialFaces[iOther] );
        }
    }
    if ( !LiveAdjacencyIsClosed( triangles ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    for ( usize iPoint = 0u; iPoint < cPoints; ++iPoint ) {
        if ( iPoint == i0 || iPoint == i1 ||
             iPoint == i2 || iPoint == i3 ) {
            continue;
        }

        if ( !Vector_Resize( &visible, triangles.nCount ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }

        bool bAnyVisible = false;
        usize cVisible = 0u;
        for ( usize iTriangle = 0u;
              iTriangle < triangles.nCount;
              ++iTriangle ) {
            const hull_tri_t &triangle = triangles.pData[iTriangle];
            if ( !triangle.alive ) {
                visible.pData[iTriangle] = 0u;
                continue;
            }
            const f64 distance =
                PointPlaneDistance( triangle, pPoints[iPoint] );
            if ( !Scalar_IsFinite( distance ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            const i32 orientation = Orient3D(
                pPoints[triangle.v[0]],
                pPoints[triangle.v[1]],
                pPoints[triangle.v[2]],
                pPoints[iPoint] );
            const f64 tolerance =
                policy.numerical.fCoplanarDistanceTolerance;
            if ( ( distance > tolerance && orientation >= 0 ) ||
                 ( distance < -tolerance && orientation < 0 ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            const bool bVisible =
                orientation < 0 && distance > tolerance;
            visible.pData[iTriangle] = bVisible ? 1u : 0u;
            bAnyVisible = bAnyVisible || bVisible;
            cVisible += bVisible ? 1u : 0u;
        }
        if ( !bAnyVisible ) {
            continue;
        }

        Vector_Clear( &horizon );
        for ( usize iTriangle = 0u;
              iTriangle < triangles.nCount;
              ++iTriangle ) {
            const hull_tri_t &triangle = triangles.pData[iTriangle];
            if ( !triangle.alive || visible.pData[iTriangle] == 0u ) {
                continue;
            }
            for ( u32 iEdge = 0u; iEdge < 3u; ++iEdge ) {
                const u32 iNeighbor = triangle.adj[iEdge];
                if ( iNeighbor == CY_U32_MAX ||
                     static_cast<usize>( iNeighbor ) >= triangles.nCount ||
                     !triangles.pData[iNeighbor].alive ) {
                    return geometry_status_t::CORRUPT_STATE;
                }
                if ( visible.pData[iNeighbor] != 0u ) {
                    continue;
                }

                horizon_edge_t edge{};
                edge.va = triangle.v[iEdge];
                edge.vb = triangle.v[( iEdge + 1u ) % 3u];
                edge.iSurvivingTriangle = iNeighbor;
                if ( !Vector_PushBack( &horizon, edge ) ) {
                    return geometry_status_t::ALLOCATION_FAILED;
                }
            }
        }
        if ( horizon.nCount < 3u ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        usize cLive = 0u;
        for ( usize iTriangle = 0u;
              iTriangle < triangles.nCount;
              ++iTriangle ) {
            cLive += triangles.pData[iTriangle].alive ? 1u : 0u;
        }
        const usize cLiveAfter = cLive - cVisible + horizon.nCount;
        if ( static_cast<u64>( cLiveAfter ) > policy.limits.cFacesMax ||
             horizon.nCount > static_cast<usize>( CY_U32_MAX ) -
                 triangles.nCount ||
             static_cast<u64>( horizon.nCount ) >
                 policy.limits.cIntersectionEventsMax -
                 static_cast<u64>( triangles.nCount ) ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        if ( !Vector_Reserve(
                 &triangles, triangles.nCount + horizon.nCount ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }

        for ( usize iTriangle = 0u;
              iTriangle < triangles.nCount;
              ++iTriangle ) {
            if ( visible.pData[iTriangle] != 0u ) {
                triangles.pData[iTriangle].alive = false;
            }
        }

        const u32 iFirstNew = static_cast<u32>( triangles.nCount );
        for ( usize iEdge = 0u; iEdge < horizon.nCount; ++iEdge ) {
            u32 iNew = CY_U32_MAX;
            status = TryAppendTriangle(
                &triangles, pPoints, interiorPoint, policy,
                horizon.pData[iEdge].va,
                horizon.pData[iEdge].vb,
                static_cast<u32>( iPoint ),
                &iNew );
            if ( status != geometry_status_t::OK ) {
                return status;
            }
            if ( iNew != iFirstNew + static_cast<u32>( iEdge ) ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }

        for ( usize iEdge = 0u; iEdge < horizon.nCount; ++iEdge ) {
            const u32 iNew =
                iFirstNew + static_cast<u32>( iEdge );
            if ( !TryLinkTriangles(
                     &triangles, iNew,
                     horizon.pData[iEdge].iSurvivingTriangle ) ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
        for ( usize i = 0u; i < horizon.nCount; ++i ) {
            const u32 iTriangle = iFirstNew + static_cast<u32>( i );
            for ( usize j = i + 1u; j < horizon.nCount; ++j ) {
                const u32 iOther = iFirstNew + static_cast<u32>( j );
                (void)TryLinkTriangles(
                    &triangles, iTriangle, iOther );
            }
        }
        if ( !LiveAdjacencyIsClosed( triangles ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    usize cLiveFaces = 0u;
    for ( usize iTriangle = 0u;
          iTriangle < triangles.nCount;
          ++iTriangle ) {
        cLiveFaces += triangles.pData[iTriangle].alive ? 1u : 0u;
    }
    if ( cLiveFaces < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( static_cast<u64>( cLiveFaces ) > policy.limits.cFacesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !Vector_Reserve( &stagedFaces, cLiveFaces ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( usize iTriangle = 0u;
          iTriangle < triangles.nCount;
          ++iTriangle ) {
        const hull_tri_t &triangle = triangles.pData[iTriangle];
        if ( !triangle.alive ) {
            continue;
        }
        convex_hull_face_t face{};
        face.indices[0] = triangle.v[0];
        face.indices[1] = triangle.v[1];
        face.indices[2] = triangle.v[2];
        face.normal = triangle.normal;
        if ( !Vector_PushBack( &stagedFaces, face ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }

    Vector_Shutdown( &pHull->faces );
    Vector_Move( &pHull->faces, &stagedFaces );
    return geometry_status_t::OK;
}

geometry_status_t ConvexHull_TryBuildBrush(
    brush_solid_t *pSolid,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const vec3d_t *pPoints,
    usize cPoints ) noexcept
{
    if ( pSolid == nullptr || pAllocator == nullptr ||
         pIdAllocator == nullptr || pPoints == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pSolid->sides.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !BrushIsCanonicalEmpty( *pSolid ) ||
         !Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( cPoints < 4u ) {
        return geometry_status_t::DEGENERATE;
    }

    convex_hull_t hull{};
    geometry_status_t status = ConvexHull_Init( &hull, pAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ConvexHull_TryBuild( &hull, pPoints, cPoints, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    vector_t<planed_t> uniquePlanes{};
    if ( !Vector_Init( &uniquePlanes, pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    status = TryCollectUniquePlanes(
        &uniquePlanes, hull, pPoints, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    const geometry_source_id_result_t brushId =
        GeometrySourceIdAllocator_Allocate( &stagedIds );
    if ( brushId.status != geometry_status_t::OK ) {
        return brushId.status;
    }

    brush_solid_t stagedBrush{};
    status = BrushSolid_Init( &stagedBrush, pAllocator, brushId.id );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushSolid_TryReserve(
        &stagedBrush, policy.limits, uniquePlanes.nCount );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    for ( usize iPlane = 0u;
          iPlane < uniquePlanes.nCount;
          ++iPlane ) {
        const geometry_source_id_result_t sideId =
            GeometrySourceIdAllocator_Allocate( &stagedIds );
        if ( sideId.status != geometry_status_t::OK ) {
            return sideId.status;
        }

        brush_solid_side_t side{};
        side.plane = uniquePlanes.pData[iPlane];
        side.sourceId = sideId.id;
        side.iAttributeIndex = static_cast<u32>( iPlane );
        status = BrushSolid_TryAddSide(
            &stagedBrush, policy.limits, side, nullptr );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
    }

    pSolid->sourceId = stagedBrush.sourceId;
    Vector_Move( &pSolid->sides, &stagedBrush.sides );
    stagedBrush.sourceId = GEOMETRY_SOURCE_ID_INVALID;
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
