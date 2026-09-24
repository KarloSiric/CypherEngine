//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_ConvexHull_Tests.cpp
//  Purpose: Contract tests for 3D convex hull construction.
//  Details: Verifies hull construction from known point sets (box corners,
//           tetrahedron, interior points, degenerate inputs) and the
//           convenience brush builder.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Kernel_ConvexHull.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Vec3d_Dot;
using Catch::Approx;

namespace {

struct HullFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    convex_hull_t hull{};

    HullFixture() {
        REQUIRE( ConvexHull_Init( &hull, &allocator ) ==
                 geometry_status_t::OK );
    }
    ~HullFixture() {
        ConvexHull_Shutdown( &hull );
    }
};

// 8 corners of a unit cube [0,1]^3.
constexpr math::vec3d_t kBoxPoints[8] = {
    { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 },
    { 1.0, 1.0, 0.0 }, { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 1.0 },
    { 1.0, 1.0, 1.0 }, { 0.0, 1.0, 1.0 }
};

// Regular tetrahedron vertices.
constexpr math::vec3d_t kTetraPoints[4] = {
    { 1.0, 1.0, 1.0 },
    { 1.0, -1.0, -1.0 },
    { -1.0, 1.0, -1.0 },
    { -1.0, -1.0, 1.0 }
};

bool FacesExactlyEqual(
    const convex_hull_face_t &a,
    const convex_hull_face_t &b ) noexcept
{
    return a.indices[0] == b.indices[0] &&
           a.indices[1] == b.indices[1] &&
           a.indices[2] == b.indices[2] &&
           a.normal.x == b.normal.x &&
           a.normal.y == b.normal.y &&
           a.normal.z == b.normal.z;
}

std::vector<math::vec3d_t> MakeFibonacciSphere(
    common::usize cPoints )
{
    std::vector<math::vec3d_t> points;
    points.reserve( cPoints );
    const common::f64 goldenAngle =
        std::numbers::pi_v<common::f64> *
        ( 3.0 - std::sqrt( 5.0 ) );

    for ( common::usize iPoint = 0u;
          iPoint < cPoints;
          ++iPoint ) {
        const common::f64 fraction =
            ( static_cast<common::f64>( iPoint ) + 0.5 ) /
            static_cast<common::f64>( cPoints );
        const common::f64 z = 1.0 - 2.0 * fraction;
        const common::f64 radius =
            std::sqrt( std::max( 0.0, 1.0 - z * z ) );
        const common::f64 angle =
            goldenAngle * static_cast<common::f64>( iPoint );
        points.push_back( Vec3d_Make(
            radius * std::cos( angle ),
            radius * std::sin( angle ),
            z ) );
    }
    return points;
}

struct edge_incidence_t {
    common::usize cFaces{ 0u };
    common::i32 orientationBalance{ 0 };
};

void CheckHullTopologyAndGeometry(
    const convex_hull_t &hull,
    const math::vec3d_t *pPoints,
    common::usize cPoints,
    common::f64 outsideTolerance )
{
    REQUIRE( pPoints != nullptr );
    REQUIRE( cPoints >= 4u );

    std::map<std::pair<common::u32, common::u32>, edge_incidence_t>
        edges;
    std::map<std::array<common::u32, 3u>, common::usize> faces;
    std::vector<bool> usedVertices( cPoints, false );

    REQUIRE( hull.faces.nCount >= 4u );
    for ( common::usize iFace = 0u;
          iFace < hull.faces.nCount;
          ++iFace ) {
        const convex_hull_face_t &face = hull.faces.pData[iFace];
        REQUIRE( face.indices[0] < cPoints );
        REQUIRE( face.indices[1] < cPoints );
        REQUIRE( face.indices[2] < cPoints );
        REQUIRE( face.indices[0] != face.indices[1] );
        REQUIRE( face.indices[1] != face.indices[2] );
        REQUIRE( face.indices[2] != face.indices[0] );

        usedVertices[face.indices[0]] = true;
        usedVertices[face.indices[1]] = true;
        usedVertices[face.indices[2]] = true;

        const math::vec3d_t a = pPoints[face.indices[0]];
        const math::vec3d_t b = pPoints[face.indices[1]];
        const math::vec3d_t c = pPoints[face.indices[2]];
        math::vec3d_t windingNormal{};
        REQUIRE( math::Vec3d_TryNormalize(
                     math::Vec3d_Cross(
                         math::Vec3d_Subtract( b, a ),
                         math::Vec3d_Subtract( c, a ) ),
                     0.0,
                     &windingNormal,
                     nullptr ) );
        CHECK( Vec3d_Dot( windingNormal, face.normal ) ==
               Approx( 1.0 ).margin( 1.0e-10 ) );
        CHECK( math::Vec3d_IsUnitLength(
                   face.normal, 1.0e-10 ) );

        const common::f64 planeDistance =
            -Vec3d_Dot( face.normal, a );
        for ( common::usize iPoint = 0u;
              iPoint < cPoints;
              ++iPoint ) {
            const common::f64 signedDistance =
                Vec3d_Dot( face.normal, pPoints[iPoint] ) +
                planeDistance;
            CHECK( signedDistance <= outsideTolerance );
        }

        std::array<common::u32, 3u> canonicalFace{
            face.indices[0], face.indices[1], face.indices[2]
        };
        std::sort( canonicalFace.begin(), canonicalFace.end() );
        CHECK( ++faces[canonicalFace] == 1u );

        for ( common::u32 iEdge = 0u; iEdge < 3u; ++iEdge ) {
            const common::u32 iA = face.indices[iEdge];
            const common::u32 iB = face.indices[( iEdge + 1u ) % 3u];
            const auto key = std::minmax( iA, iB );
            edge_incidence_t &incidence = edges[{ key.first, key.second }];
            ++incidence.cFaces;
            incidence.orientationBalance += iA < iB ? 1 : -1;
        }
    }

    for ( const auto &[edge, incidence] : edges ) {
        CAPTURE( edge.first, edge.second );
        CHECK( incidence.cFaces == 2u );
        CHECK( incidence.orientationBalance == 0 );
    }
    CHECK( static_cast<common::usize>( std::count(
               usedVertices.begin(), usedVertices.end(), true ) ) ==
           cPoints );
}

struct hull_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *HullFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<hull_failure_allocator_state_t *>(
        pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) {
        return nullptr;
    }

    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void HullFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<hull_failure_allocator_state_t *>(
        pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeHullFailureAllocator(
    hull_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        HullFailureAllocate,
        nullptr,
        HullFailureFree,
        pState
    };
}

common::usize OutstandingAllocations(
    const hull_failure_allocator_state_t &state ) noexcept
{
    return state.cSuccessfulAllocations - state.cFrees;
}

bool BrushIsCanonicalEmpty( const brush_solid_t &brush ) noexcept
{
    return brush.sides.pData == nullptr &&
           brush.sides.nCount == 0u &&
           brush.sides.nCapacity == 0u &&
           brush.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( brush.sourceId );
}

} // namespace

// ---------------------------------------------------------------------------
// Raw hull construction
// ---------------------------------------------------------------------------

TEST_CASE( "ConvexHull: tetrahedron produces 4 triangular faces",
           "[Gate10][ConvexHull]" )
{
    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, kTetraPoints, 4u, f.policy ) ==
             geometry_status_t::OK );

    REQUIRE( ConvexHull_FaceCount( &f.hull ) == 4u );
}

TEST_CASE( "ConvexHull: box corners produce 12 triangular faces",
           "[Gate10][ConvexHull]" )
{
    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, kBoxPoints, 8u, f.policy ) ==
             geometry_status_t::OK );

    // A cube has 6 quad faces → 12 triangles.
    REQUIRE( ConvexHull_FaceCount( &f.hull ) == 12u );
    CheckHullTopologyAndGeometry(
        f.hull, kBoxPoints, 8u,
        f.policy.numerical.fCoplanarDistanceTolerance );
}

TEST_CASE( "ConvexHull: all normals point outward from centroid",
           "[Gate10][ConvexHull]" )
{
    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, kBoxPoints, 8u, f.policy ) ==
             geometry_status_t::OK );

    // Centroid of the box.
    const math::vec3d_t centroid = Vec3d_Make( 0.5, 0.5, 0.5 );

    const common::usize count = ConvexHull_FaceCount( &f.hull );
    const convex_hull_face_t *pFaces =
        common::Vector_Data( &f.hull.faces );
    for ( common::usize i = 0u; i < count; ++i ) {
        // The face's plane should put the centroid on the negative side.
        const math::vec3d_t faceVert = kBoxPoints[pFaces[i].indices[0]];
        const common::f64 d = -Vec3d_Dot( pFaces[i].normal, faceVert );
        const common::f64 dist =
            Vec3d_Dot( pFaces[i].normal, centroid ) + d;
        REQUIRE( dist < 0.001 );
    }
}

TEST_CASE( "ConvexHull: interior points are ignored",
           "[Gate10][ConvexHull]" )
{
    // Box corners plus center point.
    math::vec3d_t points[9];
    for ( int i = 0; i < 8; ++i ) { points[i] = kBoxPoints[i]; }
    points[8] = Vec3d_Make( 0.5, 0.5, 0.5 );

    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, points, 9u, f.policy ) ==
             geometry_status_t::OK );

    // Still 12 triangles — interior point doesn't add faces.
    REQUIRE( ConvexHull_FaceCount( &f.hull ) == 12u );
}

TEST_CASE( "ConvexHull: fewer than 4 points rejected",
           "[Gate10][ConvexHull]" )
{
    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, kTetraPoints, 3u, f.policy ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "ConvexHull: coplanar points rejected",
           "[Gate10][ConvexHull]" )
{
    const math::vec3d_t coplanar[4] = {
        { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 },
        { 1.0, 1.0, 0.0 }, { 0.0, 1.0, 0.0 }
    };
    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, coplanar, 4u, f.policy ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "ConvexHull: null args rejected",
           "[Gate10][ConvexHull]" )
{
    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 nullptr, kBoxPoints, 8u, f.policy ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, nullptr, 8u, f.policy ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "ConvexHull: lifecycle distinguishes absent and corrupt storage",
           "[Gate10][ConvexHull][Lifecycle]" )
{
    convex_hull_t hull{};
    CHECK( ConvexHull_FaceCount( &hull ) == 0u );
    CHECK( ConvexHull_TryBuild(
               &hull, kTetraPoints, 4u, geometry_policy_t{} ) ==
           geometry_status_t::NOT_INITIALIZED );

    hull.faces.nCount = 1u;
    CHECK( ConvexHull_FaceCount( &hull ) == 0u );
    CHECK( ConvexHull_TryBuild(
               &hull, kTetraPoints, 4u, geometry_policy_t{} ) ==
           geometry_status_t::CORRUPT_STATE );
    hull.faces.nCount = 0u;

    common::allocator_t invalidAllocator{};
    CHECK( ConvexHull_Init( &hull, &invalidAllocator ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( ConvexHull_Init( &hull, common::Allocator_GetSystem() ) ==
           geometry_status_t::OK );
    CHECK( ConvexHull_Init( &hull, common::Allocator_GetSystem() ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    ConvexHull_Shutdown( &hull );
    ConvexHull_Shutdown( &hull );
}

TEST_CASE( "ConvexHull: more than 512 faces remain closed and outward",
           "[Gate10][ConvexHull][Large]" )
{
    constexpr common::usize cPoints = 300u;
    const std::vector<math::vec3d_t> points =
        MakeFibonacciSphere( cPoints );

    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, points.data(), points.size(), f.policy ) ==
             geometry_status_t::OK );

    // Every point lies on the strictly convex unit sphere and is therefore a
    // hull vertex. Euler's relation for a triangulated convex polyhedron gives
    // F = 2V - 4.
    REQUIRE( ConvexHull_FaceCount( &f.hull ) == 2u * cPoints - 4u );
    REQUIRE( ConvexHull_FaceCount( &f.hull ) > 512u );
    CheckHullTopologyAndGeometry(
        f.hull, points.data(), points.size(),
        f.policy.numerical.fCoplanarDistanceTolerance * 2.0 );
}

TEST_CASE( "ConvexHull: validation and numerical failures preserve output",
           "[Gate10][ConvexHull][Atomicity]" )
{
    HullFixture f;
    REQUIRE( ConvexHull_TryBuild(
                 &f.hull, kTetraPoints, 4u, f.policy ) ==
             geometry_status_t::OK );
    REQUIRE( f.hull.faces.nCount == 4u );

    const convex_hull_face_t *const pOriginalFaces = f.hull.faces.pData;
    const common::usize originalCapacity = f.hull.faces.nCapacity;
    convex_hull_face_t originalFaces[4]{};
    for ( common::usize iFace = 0u; iFace < 4u; ++iFace ) {
        originalFaces[iFace] = f.hull.faces.pData[iFace];
    }

    geometry_policy_t invalidPolicy = f.policy;
    invalidPolicy.numerical.fAbsoluteDistanceTolerance = 0.0;
    CHECK( ConvexHull_TryBuild(
               &f.hull, kBoxPoints, 8u, invalidPolicy ) ==
           geometry_status_t::INVALID_ARGUMENT );

    math::vec3d_t nonFinite[4]{
        kTetraPoints[0], kTetraPoints[1],
        kTetraPoints[2], kTetraPoints[3]
    };
    nonFinite[2].x = std::numeric_limits<common::f64>::infinity();
    CHECK( ConvexHull_TryBuild(
               &f.hull, nonFinite, 4u, f.policy ) ==
           geometry_status_t::NUMERIC_FAILURE );

    math::vec3d_t outsideLimit[4]{
        kTetraPoints[0], kTetraPoints[1],
        kTetraPoints[2], kTetraPoints[3]
    };
    outsideLimit[1].x =
        f.policy.numerical.fCoordinateMagnitudeLimit + 1.0;
    CHECK( ConvexHull_TryBuild(
               &f.hull, outsideLimit, 4u, f.policy ) ==
           geometry_status_t::LIMIT_EXCEEDED );

    geometry_policy_t faceLimited = f.policy;
    faceLimited.limits.cFacesMax = 3u;
    faceLimited.limits.cLoopsMax = 3u;
    REQUIRE( GeometryPolicy_IsValid( faceLimited ) );
    CHECK( ConvexHull_TryBuild(
               &f.hull, kBoxPoints, 8u, faceLimited ) ==
           geometry_status_t::LIMIT_EXCEEDED );

    CHECK( f.hull.faces.pData == pOriginalFaces );
    CHECK( f.hull.faces.nCount == 4u );
    CHECK( f.hull.faces.nCapacity == originalCapacity );
    for ( common::usize iFace = 0u; iFace < 4u; ++iFace ) {
        CHECK( FacesExactlyEqual(
            f.hull.faces.pData[iFace], originalFaces[iFace] ) );
    }
}

TEST_CASE( "ConvexHull: repeated construction is byte-for-byte deterministic",
           "[Gate10][ConvexHull][Determinism]" )
{
    const std::vector<math::vec3d_t> points =
        MakeFibonacciSphere( 48u );
    HullFixture first;
    HullFixture second;
    REQUIRE( ConvexHull_TryBuild(
                 &first.hull, points.data(), points.size(), first.policy ) ==
             geometry_status_t::OK );
    REQUIRE( ConvexHull_TryBuild(
                 &second.hull, points.data(), points.size(), second.policy ) ==
             geometry_status_t::OK );
    REQUIRE( first.hull.faces.nCount == second.hull.faces.nCount );

    for ( common::usize iFace = 0u;
          iFace < first.hull.faces.nCount;
          ++iFace ) {
        CHECK( FacesExactlyEqual(
            first.hull.faces.pData[iFace],
            second.hull.faces.pData[iFace] ) );
    }
}

TEST_CASE( "ConvexHull: explicit limits reject work before publication",
           "[Gate10][ConvexHull][Limits]" )
{
    HullFixture f;

    geometry_policy_t vertexLimited = f.policy;
    vertexLimited.limits.cVerticesMax = 3u;
    REQUIRE( GeometryPolicy_IsValid( vertexLimited ) );
    CHECK( ConvexHull_TryBuild(
               &f.hull, kTetraPoints, 4u, vertexLimited ) ==
           geometry_status_t::LIMIT_EXCEEDED );

    geometry_policy_t faceLimited = f.policy;
    faceLimited.limits.cFacesMax = 3u;
    faceLimited.limits.cLoopsMax = 3u;
    REQUIRE( GeometryPolicy_IsValid( faceLimited ) );
    CHECK( ConvexHull_TryBuild(
               &f.hull, kTetraPoints, 4u, faceLimited ) ==
           geometry_status_t::LIMIT_EXCEEDED );

    geometry_policy_t eventLimited = f.policy;
    eventLimited.limits.cIntersectionEventsMax = 3u;
    REQUIRE( GeometryPolicy_IsValid( eventLimited ) );
    CHECK( ConvexHull_TryBuild(
               &f.hull, kTetraPoints, 4u, eventLimited ) ==
           geometry_status_t::LIMIT_EXCEEDED );
    if constexpr ( common::CY_USIZE_MAX > common::CY_U32_MAX ) {
        CHECK( ConvexHull_TryBuild(
                   &f.hull,
                   kTetraPoints,
                   static_cast<common::usize>( common::CY_U32_MAX ) + 1u,
                   f.policy ) == geometry_status_t::LIMIT_EXCEEDED );
    }
    CHECK( ConvexHull_FaceCount( &f.hull ) == 0u );
}

TEST_CASE( "ConvexHull: every allocation failure preserves the prior hull",
           "[Gate10][ConvexHull][Atomicity][Allocation]" )
{
    const std::vector<math::vec3d_t> replacementPoints =
        MakeFibonacciSphere( 32u );

    hull_failure_allocator_state_t probeState{};
    common::allocator_t probeAllocator =
        MakeHullFailureAllocator( &probeState );
    convex_hull_t probe{};
    REQUIRE( ConvexHull_Init( &probe, &probeAllocator ) ==
             geometry_status_t::OK );
    REQUIRE( ConvexHull_TryBuild(
                 &probe, kTetraPoints, 4u, geometry_policy_t{} ) ==
             geometry_status_t::OK );
    const common::usize iFirstReplacementAllocation =
        probeState.cAllocationCalls + 1u;
    REQUIRE( ConvexHull_TryBuild(
                 &probe,
                 replacementPoints.data(),
                 replacementPoints.size(),
                 geometry_policy_t{} ) == geometry_status_t::OK );
    const common::usize cReplacementAllocations =
        probeState.cAllocationCalls - iFirstReplacementAllocation + 1u;
    REQUIRE( cReplacementAllocations > 0u );
    ConvexHull_Shutdown( &probe );
    REQUIRE( OutstandingAllocations( probeState ) == 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cReplacementAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cReplacementAllocations );
        hull_failure_allocator_state_t state{};
        common::allocator_t allocator = MakeHullFailureAllocator( &state );
        convex_hull_t hull{};
        REQUIRE( ConvexHull_Init( &hull, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( ConvexHull_TryBuild(
                     &hull, kTetraPoints, 4u, geometry_policy_t{} ) ==
                 geometry_status_t::OK );
        REQUIRE( hull.faces.nCount == 4u );

        const convex_hull_face_t *const pOriginalFaces = hull.faces.pData;
        const common::usize originalCapacity = hull.faces.nCapacity;
        const common::usize cOriginalOutstanding =
            OutstandingAllocations( state );
        convex_hull_face_t originalFaces[4]{};
        for ( common::usize iFace = 0u; iFace < 4u; ++iFace ) {
            originalFaces[iFace] = hull.faces.pData[iFace];
        }

        state.iFailOnCall = state.cAllocationCalls + iFailure + 1u;
        CHECK( ConvexHull_TryBuild(
                   &hull,
                   replacementPoints.data(),
                   replacementPoints.size(),
                   geometry_policy_t{} ) ==
               geometry_status_t::ALLOCATION_FAILED );
        CHECK( hull.faces.pData == pOriginalFaces );
        CHECK( hull.faces.nCount == 4u );
        CHECK( hull.faces.nCapacity == originalCapacity );
        CHECK( OutstandingAllocations( state ) ==
               cOriginalOutstanding );
        for ( common::usize iFace = 0u; iFace < 4u; ++iFace ) {
            CHECK( FacesExactlyEqual(
                hull.faces.pData[iFace], originalFaces[iFace] ) );
        }

        ConvexHull_Shutdown( &hull );
        CHECK( OutstandingAllocations( state ) == 0u );
    }
}

// ---------------------------------------------------------------------------
// Brush builder
// ---------------------------------------------------------------------------

TEST_CASE( "ConvexHull: TryBuildBrush from box produces 6-sided brush",
           "[Gate10][ConvexHull]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};

    REQUIRE( ConvexHull_TryBuildBrush(
                 &brush, &allocator, policy, &idAllocator,
                 kBoxPoints, 8u ) == geometry_status_t::OK );

    // 12 hull triangles merge into 6 unique planes.
    REQUIRE( BrushSolid_SideCount( &brush ) == 6u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "ConvexHull: TryBuildBrush produces valid boundary",
           "[Gate10][ConvexHull]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};

    REQUIRE( ConvexHull_TryBuildBrush(
                 &brush, &allocator, policy, &idAllocator,
                 kBoxPoints, 8u ) == geometry_status_t::OK );

    // Verify the brush can reconstruct a valid boundary.
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &boundary, &brush, policy ) ==
             geometry_status_t::OK );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "ConvexHull: TryBuildBrush tetrahedron produces 4-sided brush",
           "[Gate10][ConvexHull]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};

    REQUIRE( ConvexHull_TryBuildBrush(
                 &brush, &allocator, policy, &idAllocator,
                 kTetraPoints, 4u ) == geometry_status_t::OK );

    REQUIRE( BrushSolid_SideCount( &brush ) == 4u );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "ConvexHull: TryBuildBrush null args rejected",
           "[Gate10][ConvexHull]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};

    REQUIRE( ConvexHull_TryBuildBrush(
                 nullptr, &allocator, policy, &idAllocator,
                 kBoxPoints, 8u ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( ConvexHull_TryBuildBrush(
                 &brush, &allocator, policy, &idAllocator,
                 nullptr, 8u ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "ConvexHull: TryBuildBrush rejects initialized output atomically",
           "[Gate10][ConvexHull][Lifecycle][Brush]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_solid_t brush{};
    REQUIRE( BrushSolid_Init(
                 &brush, &allocator, geometry_source_id_t{ 99u } ) ==
             geometry_status_t::OK );
    const geometry_source_id_t originalBrushId = brush.sourceId;
    geometry_source_id_allocator_t ids{};
    ids.next = geometry_source_id_t{ 500u };
    const geometry_source_id_t originalNext = ids.next;

    CHECK( ConvexHull_TryBuildBrush(
               &brush, &allocator, geometry_policy_t{},
               &ids, kBoxPoints, 8u ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    CHECK( brush.sourceId.value == originalBrushId.value );
    CHECK( BrushSolid_SideCount( &brush ) == 0u );
    CHECK( ids.next.value == originalNext.value );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "ConvexHull: TryBuildBrush validates policy and allocator atomically",
           "[Gate10][ConvexHull][Policy][Brush]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    common::allocator_t invalidAllocator{};
    geometry_source_id_allocator_t ids{};
    ids.next = geometry_source_id_t{ 321u };
    const geometry_source_id_t originalNext = ids.next;
    brush_solid_t brush{};

    geometry_policy_t invalidPolicy{};
    invalidPolicy.numerical.fPlanarityTolerance = 0.0;
    CHECK( ConvexHull_TryBuildBrush(
               &brush, &allocator, invalidPolicy,
               &ids, kBoxPoints, 8u ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushIsCanonicalEmpty( brush ) );
    CHECK( ids.next.value == originalNext.value );

    CHECK( ConvexHull_TryBuildBrush(
               &brush, &invalidAllocator, geometry_policy_t{},
               &ids, kBoxPoints, 8u ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushIsCanonicalEmpty( brush ) );
    CHECK( ids.next.value == originalNext.value );
}

TEST_CASE( "ConvexHull: brush construction has no hidden 256-plane cap",
           "[Gate10][ConvexHull][Large][Brush]" )
{
    constexpr common::usize cPoints = 140u;
    const std::vector<math::vec3d_t> points =
        MakeFibonacciSphere( cPoints );

    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    policy.limits.cBrushSidesPerBrushMax = 1024u;
    REQUIRE( GeometryPolicy_IsValid( policy ) );
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};

    REQUIRE( ConvexHull_TryBuildBrush(
                 &brush, &allocator, policy, &idAllocator,
                 points.data(), points.size() ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSolid_SideCount( &brush ) == 2u * cPoints - 4u );
    REQUIRE( BrushSolid_SideCount( &brush ) > 256u );
    CHECK( idAllocator.next.value ==
           2u + BrushSolid_SideCount( &brush ) );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "ConvexHull: every brush allocation failure is fully atomic",
           "[Gate10][ConvexHull][Atomicity][Allocation][Brush]" )
{
    hull_failure_allocator_state_t probeState{};
    common::allocator_t probeAllocator =
        MakeHullFailureAllocator( &probeState );
    geometry_source_id_allocator_t probeIds{};
    brush_solid_t probeBrush{};
    REQUIRE( ConvexHull_TryBuildBrush(
                 &probeBrush, &probeAllocator, geometry_policy_t{},
                 &probeIds, kBoxPoints, 8u ) ==
             geometry_status_t::OK );
    const common::usize cAllocationCalls = probeState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    BrushSolid_Shutdown( &probeBrush );
    REQUIRE( OutstandingAllocations( probeState ) == 0u );

    for ( common::usize iFailOnCall = 1u;
          iFailOnCall <= cAllocationCalls;
          ++iFailOnCall ) {
        CAPTURE( iFailOnCall, cAllocationCalls );
        hull_failure_allocator_state_t state{};
        state.iFailOnCall = iFailOnCall;
        common::allocator_t allocator = MakeHullFailureAllocator( &state );
        geometry_source_id_allocator_t ids{};
        ids.next = geometry_source_id_t{ 700u };
        const geometry_source_id_t originalNext = ids.next;
        brush_solid_t brush{};

        CHECK( ConvexHull_TryBuildBrush(
                   &brush, &allocator, geometry_policy_t{},
                   &ids, kBoxPoints, 8u ) ==
               geometry_status_t::ALLOCATION_FAILED );
        CHECK( BrushIsCanonicalEmpty( brush ) );
        CHECK( ids.next.value == originalNext.value );
        CHECK( OutstandingAllocations( state ) == 0u );
    }
}

TEST_CASE( "ConvexHull: source-ID exhaustion leaves brush and allocator exact",
           "[Gate10][ConvexHull][Atomicity][Identity]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{};
    ids.next = geometry_source_id_t{ common::CY_U64_MAX - 1u };
    const geometry_source_id_t originalNext = ids.next;
    brush_solid_t brush{};

    CHECK( ConvexHull_TryBuildBrush(
               &brush, &allocator, geometry_policy_t{},
               &ids, kBoxPoints, 8u ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( BrushIsCanonicalEmpty( brush ) );
    CHECK( ids.next.value == originalNext.value );
}

} // namespace cypher::editor::geometry
