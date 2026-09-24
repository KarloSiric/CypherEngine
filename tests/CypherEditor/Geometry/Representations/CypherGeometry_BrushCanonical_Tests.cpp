//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCanonical_Tests.cpp
//  Purpose: Verifies canonical traversal and failure atomicity of brush
//           boundary reconstruction, plus the Gate 2 identity and
//           malformed-input closeout cases.
//  Details: Permutation cases are exhaustive over every side order of the
//           fixture brushes. Allocation failure is swept over every
//           allocation reconstruction performs, so each early-return path
//           is exercised rather than sampled.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using cypher::math::f64;
using cypher::math::planed_t;
using cypher::math::vec3d_t;

namespace {

struct failing_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *FailingAllocate(
    void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) {
        return nullptr;
    }
    return common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
}

void FailingFree(
    void *, void *pMemory, common::usize cbSize, common::usize nAlignment ) noexcept
{
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeFailingAllocator( failing_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{ FailingAllocate, nullptr, FailingFree, pState };
}

planed_t UnitPlane( f64 nx, f64 ny, f64 nz, f64 offset ) noexcept
{
    // Plane n·p + d = 0 with n normalized; `offset` is the signed distance
    // of the plane from the origin along n, so d = -offset.
    const f64 length = std::sqrt( nx * nx + ny * ny + nz * nz );
    return cypher::math::Planed_Make(
        cypher::math::Vec3d_Make( nx / length, ny / length, nz / length ),
        -offset );
}

// Axis-aligned [-1, 1]^3 box planes in generator order.
std::vector<planed_t> BoxPlanes()
{
    return {
        UnitPlane( 1.0, 0.0, 0.0, 1.0 ), UnitPlane( -1.0, 0.0, 0.0, 1.0 ),
        UnitPlane( 0.0, 1.0, 0.0, 1.0 ), UnitPlane( 0.0, -1.0, 0.0, 1.0 ),
        UnitPlane( 0.0, 0.0, 1.0, 1.0 ), UnitPlane( 0.0, 0.0, -1.0, 1.0 ),
    };
}

// Box with the (+,+,+) corner cut by x + y + z = 2.5: 10 vertices,
// 15 edges, 7 faces, including one triangle and three pentagons.
std::vector<planed_t> ChamferedBoxPlanes()
{
    std::vector<planed_t> planes = BoxPlanes();
    planes.push_back( UnitPlane( 1.0, 1.0, 1.0, 2.5 / std::sqrt( 3.0 ) ) );
    return planes;
}

// Regular-ish tetrahedron: the minimum closed convex brush.
std::vector<planed_t> TetrahedronPlanes()
{
    return {
        UnitPlane( 1.0, 1.0, 1.0, 1.0 ),
        UnitPlane( -1.0, -1.0, 1.0, 1.0 ),
        UnitPlane( -1.0, 1.0, -1.0, 1.0 ),
        UnitPlane( 1.0, -1.0, -1.0, 1.0 ),
    };
}

struct brush_holder_t {
    brush_solid_t brush{};
    ~brush_holder_t() { BrushSolid_Shutdown( &brush ); }
};

struct boundary_holder_t {
    brush_boundary_t boundary{};
    ~boundary_holder_t() { BrushBoundary_Shutdown( &boundary ); }
};

// Builds a brush whose side i has plane planes[order[i]] and source ID
// 100 + order[i], so a side keeps its identity across permutations.
void BuildBrush(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    const std::vector<planed_t> &planes,
    const std::vector<common::usize> &order )
{
    REQUIRE( BrushSolid_Init( pBrush, pAllocator, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    for ( common::usize i = 0u; i < order.size(); ++i ) {
        brush_solid_side_t side{};
        side.plane = planes[order[i]];
        side.sourceId = geometry_source_id_t{ 100u + order[i] };
        side.iAttributeIndex = static_cast<common::u32>( order[i] );
        REQUIRE( BrushSolid_TryAddSide( pBrush, policy.limits, side, nullptr ) ==
                 geometry_status_t::OK );
    }
}

std::vector<common::usize> IdentityOrder( common::usize count )
{
    std::vector<common::usize> order( count );
    for ( common::usize i = 0u; i < count; ++i ) {
        order[i] = i;
    }
    return order;
}

std::vector<common::u32> FaceRing( const brush_boundary_t &boundary, common::usize iFace )
{
    std::vector<common::u32> ring( boundary.faces.pData[iFace].cVertices );
    common::usize count = 0u;
    REQUIRE( BrushBoundary_TryGetFaceVertexIndices(
                 &boundary, iFace, ring.data(), ring.size(), &count ) ==
             geometry_status_t::OK );
    return ring;
}

// Structural canonical-form checks that hold for any successful boundary.
void RequireCanonicalForm( const brush_boundary_t &boundary )
{
    const common::usize cVertices = BrushBoundary_VertexCount( &boundary );
    for ( common::usize i = 1u; i < cVertices; ++i ) {
        const vec3d_t a = boundary.vertices.pData[i - 1u];
        const vec3d_t b = boundary.vertices.pData[i];
        const bool bAscending =
            a.x < b.x || ( a.x == b.x && ( a.y < b.y || ( a.y == b.y && a.z < b.z ) ) );
        REQUIRE( bAscending );
    }

    const common::usize cEdges = BrushBoundary_EdgeCount( &boundary );
    for ( common::usize i = 0u; i < cEdges; ++i ) {
        const brush_boundary_edge_t e = boundary.edges.pData[i];
        REQUIRE( e.iVertex0 < e.iVertex1 );
        if ( i > 0u ) {
            const brush_boundary_edge_t p = boundary.edges.pData[i - 1u];
            REQUIRE( ( p.iVertex0 < e.iVertex0 ||
                       ( p.iVertex0 == e.iVertex0 && p.iVertex1 < e.iVertex1 ) ) );
        }
    }

    const common::usize cFaces = BrushBoundary_FaceCount( &boundary );
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const std::vector<common::u32> ring = FaceRing( boundary, iFace );
        REQUIRE( ring.front() == *std::min_element( ring.begin(), ring.end() ) );
        if ( iFace > 0u ) {
            REQUIRE( boundary.faces.pData[iFace - 1u].iSide <
                     boundary.faces.pData[iFace].iSide );
        }
    }
}

// Reconstructs the brush for every permutation of the plane set and
// requires identical vertex positions, identical edges, and identical face
// rings for each side identity.
void RequirePermutationInvariant( const std::vector<planed_t> &planes )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};

    brush_holder_t reference{};
    BuildBrush( &reference.brush, &allocator, policy, planes,
                IdentityOrder( planes.size() ) );
    boundary_holder_t refBoundary{};
    REQUIRE( BrushBoundary_Init( &refBoundary.boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &refBoundary.boundary, &reference.brush, policy ) ==
             geometry_status_t::OK );
    RequireCanonicalForm( refBoundary.boundary );

    boundary_holder_t permBoundary{};
    REQUIRE( BrushBoundary_Init( &permBoundary.boundary, &allocator ) ==
             geometry_status_t::OK );

    std::vector<common::usize> order = IdentityOrder( planes.size() );
    common::usize cPermutations = 0u;
    do {
        brush_holder_t permuted{};
        BuildBrush( &permuted.brush, &allocator, policy, planes, order );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &permBoundary.boundary, &permuted.brush, policy ) ==
                 geometry_status_t::OK );

        const brush_boundary_t &a = refBoundary.boundary;
        const brush_boundary_t &b = permBoundary.boundary;
        REQUIRE( BrushBoundary_VertexCount( &a ) == BrushBoundary_VertexCount( &b ) );
        REQUIRE( BrushBoundary_EdgeCount( &a ) == BrushBoundary_EdgeCount( &b ) );
        REQUIRE( BrushBoundary_FaceCount( &a ) == BrushBoundary_FaceCount( &b ) );

        for ( common::usize i = 0u; i < BrushBoundary_VertexCount( &a ); ++i ) {
            REQUIRE( b.vertices.pData[i].x == Approx( a.vertices.pData[i].x ).margin( 1e-12 ) );
            REQUIRE( b.vertices.pData[i].y == Approx( a.vertices.pData[i].y ).margin( 1e-12 ) );
            REQUIRE( b.vertices.pData[i].z == Approx( a.vertices.pData[i].z ).margin( 1e-12 ) );
        }
        for ( common::usize i = 0u; i < BrushBoundary_EdgeCount( &a ); ++i ) {
            REQUIRE( b.edges.pData[i].iVertex0 == a.edges.pData[i].iVertex0 );
            REQUIRE( b.edges.pData[i].iVertex1 == a.edges.pData[i].iVertex1 );
        }

        // Match faces by side identity. Side i of the permuted brush holds
        // original side order[i].
        for ( common::usize iSide = 0u; iSide < order.size(); ++iSide ) {
            common::usize iPermFace = 0u;
            common::usize iRefFace = 0u;
            REQUIRE( BrushBoundary_TryFindFaceForSide( &b, iSide, &iPermFace ) ==
                     geometry_status_t::OK );
            REQUIRE( BrushBoundary_TryFindFaceForSide( &a, order[iSide], &iRefFace ) ==
                     geometry_status_t::OK );
            REQUIRE( FaceRing( b, iPermFace ) == FaceRing( a, iRefFace ) );
        }
        ++cPermutations;
    } while ( std::next_permutation( order.begin(), order.end() ) );

    common::usize cExpected = 1u;
    for ( common::usize i = 2u; i <= planes.size(); ++i ) {
        cExpected *= i;
    }
    REQUIRE( cPermutations == cExpected );
}

} // namespace

//==========================================================================
// Canonical traversal
//==========================================================================

TEST_CASE( "box boundary is canonical under every side permutation",
           "[editor][geometry][brush][canonical]" ) {
    RequirePermutationInvariant( BoxPlanes() );
}

TEST_CASE( "chamfered box boundary is canonical under every side permutation",
           "[editor][geometry][brush][canonical]" ) {
    RequirePermutationInvariant( ChamferedBoxPlanes() );
}

TEST_CASE( "tetrahedron boundary is canonical under every side permutation",
           "[editor][geometry][brush][canonical]" ) {
    RequirePermutationInvariant( TetrahedronPlanes() );
}

TEST_CASE( "chamfered box has the expected topology and passes deep validation",
           "[editor][geometry][brush][canonical]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    const std::vector<planed_t> planes = ChamferedBoxPlanes();
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );

    const brush_validation_result_t result =
        BrushValidation_Deep( &holder.brush, policy, &allocator );
    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.cVertices == 10u );
    REQUIRE( result.cEdges == 15u );
    REQUIRE( result.cFaces == 7u );
    REQUIRE( result.bWatertight );
}

TEST_CASE( "tetrahedron is the minimum valid brush",
           "[editor][geometry][brush][canonical]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    const std::vector<planed_t> planes = TetrahedronPlanes();
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );

    const brush_validation_result_t result =
        BrushValidation_Deep( &holder.brush, policy, &allocator );
    REQUIRE( result.status == geometry_status_t::OK );
    REQUIRE( result.cVertices == 4u );
    REQUIRE( result.cEdges == 6u );
    REQUIRE( result.cFaces == 4u );
}

//==========================================================================
// Queries
//==========================================================================

TEST_CASE( "boundary element queries validate indices and zero outputs",
           "[editor][geometry][brush][queries]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    const std::vector<planed_t> planes = BoxPlanes();
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );
    boundary_holder_t b{};
    REQUIRE( BrushBoundary_Init( &b.boundary, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy ) ==
             geometry_status_t::OK );

    vec3d_t vertex{ 7.0, 7.0, 7.0 };
    REQUIRE( BrushBoundary_TryGetVertex( &b.boundary, 0u, &vertex ) ==
             geometry_status_t::OK );
    REQUIRE( vertex.x == -1.0 );
    REQUIRE( vertex.y == -1.0 );
    REQUIRE( vertex.z == -1.0 );
    REQUIRE( BrushBoundary_TryGetVertex( &b.boundary, 8u, &vertex ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( vertex.x == 0.0 );

    brush_boundary_edge_t edge{ 9u, 9u };
    REQUIRE( BrushBoundary_TryGetEdge( &b.boundary, 12u, &edge ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( edge.iVertex0 == 0u );
    REQUIRE( edge.iVertex1 == 0u );

    brush_boundary_face_t face{};
    REQUIRE( BrushBoundary_TryGetFace( &b.boundary, 5u, &face ) == geometry_status_t::OK );
    REQUIRE( face.iSide == 5u );
    REQUIRE( face.cVertices == 4u );
    REQUIRE( BrushBoundary_TryGetFace( &b.boundary, 6u, &face ) ==
             geometry_status_t::INVALID_ARGUMENT );

    common::usize iFace = 99u;
    REQUIRE( BrushBoundary_TryFindFaceForSide( &b.boundary, 3u, &iFace ) ==
             geometry_status_t::OK );
    REQUIRE( iFace == 3u );
    REQUIRE( BrushBoundary_TryFindFaceForSide( &b.boundary, 6u, &iFace ) ==
             geometry_status_t::INVALID_ARGUMENT );

    cypher::math::aabbd_t bounds{};
    REQUIRE( BrushBoundary_TryGetBounds( &b.boundary, &bounds ) == geometry_status_t::OK );
    REQUIRE( bounds.minimum.x == -1.0 );
    REQUIRE( bounds.minimum.y == -1.0 );
    REQUIRE( bounds.minimum.z == -1.0 );
    REQUIRE( bounds.maximum.x == 1.0 );
    REQUIRE( bounds.maximum.y == 1.0 );
    REQUIRE( bounds.maximum.z == 1.0 );

    brush_boundary_t uninitialized{};
    REQUIRE( BrushBoundary_TryGetBounds( &uninitialized, &bounds ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( BrushBoundary_TryGetVertex( nullptr, 0u, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

//==========================================================================
// Failure atomicity
//==========================================================================

TEST_CASE( "failed reconstruction clears a previously valid boundary",
           "[editor][geometry][brush][atomicity]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    const std::vector<planed_t> planes = BoxPlanes();
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );
    boundary_holder_t b{};
    REQUIRE( BrushBoundary_Init( &b.boundary, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_VertexCount( &b.boundary ) == 8u );

    // Move +X behind -X: the half-spaces no longer intersect.
    REQUIRE( BrushSolid_TrySetSidePlane(
                 &holder.brush, 0u, UnitPlane( 1.0, 0.0, 0.0, -2.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy ) ==
             geometry_status_t::DEGENERATE );
    REQUIRE( BrushBoundary_VertexCount( &b.boundary ) == 0u );
    REQUIRE( BrushBoundary_EdgeCount( &b.boundary ) == 0u );
    REQUIRE( BrushBoundary_FaceCount( &b.boundary ) == 0u );
    REQUIRE( b.boundary.faceVertexIndices.nCount == 0u );

    cypher::math::aabbd_t bounds{};
    REQUIRE( BrushBoundary_TryGetBounds( &b.boundary, &bounds ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "reconstruction under every allocation failure is atomic and leak-free",
           "[editor][geometry][brush][atomicity]" ) {
    const geometry_policy_t policy{};
    const std::vector<planed_t> planes = ChamferedBoxPlanes();

    common::allocator_t system{ *common::Allocator_GetSystem() };
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &system, policy, planes, IdentityOrder( planes.size() ) );

    boundary_holder_t reference{};
    REQUIRE( BrushBoundary_Init( &reference.boundary, &system ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &reference.boundary, &holder.brush, policy ) ==
             geometry_status_t::OK );

    bool bSucceeded = false;
    common::usize cFailuresInjected = 0u;
    for ( common::usize iFailure = 0u; iFailure < 64u && !bSucceeded; ++iFailure ) {
        failing_allocator_state_t state{};
        common::allocator_t allocator = MakeFailingAllocator( &state );
        boundary_holder_t b{};
        REQUIRE( BrushBoundary_Init( &b.boundary, &allocator ) == geometry_status_t::OK );
        state.iFailure = state.cAllocationCalls + iFailure;

        const geometry_status_t status =
            BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy );
        if ( status == geometry_status_t::OK ) {
            bSucceeded = true;
            REQUIRE( BrushBoundary_VertexCount( &b.boundary ) ==
                     BrushBoundary_VertexCount( &reference.boundary ) );
            REQUIRE( BrushBoundary_EdgeCount( &b.boundary ) ==
                     BrushBoundary_EdgeCount( &reference.boundary ) );
            REQUIRE( BrushBoundary_FaceCount( &b.boundary ) ==
                     BrushBoundary_FaceCount( &reference.boundary ) );
        } else {
            ++cFailuresInjected;
            REQUIRE( status == geometry_status_t::ALLOCATION_FAILED );
            REQUIRE( BrushBoundary_VertexCount( &b.boundary ) == 0u );
            REQUIRE( BrushBoundary_EdgeCount( &b.boundary ) == 0u );
            REQUIRE( BrushBoundary_FaceCount( &b.boundary ) == 0u );
            REQUIRE( b.boundary.faceVertexIndices.nCount == 0u );
        }
    }
    REQUIRE( bSucceeded );
    REQUIRE( cFailuresInjected > 0u );
}

//==========================================================================
// Malformed plane sets
//==========================================================================

TEST_CASE( "open plane sets fail reconstruction or deep validation",
           "[editor][geometry][brush][malformed]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};

    SECTION( "box missing its bottom side" ) {
        std::vector<planed_t> planes = BoxPlanes();
        planes.pop_back();
        brush_holder_t holder{};
        BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );
        const brush_validation_result_t result =
            BrushValidation_Deep( &holder.brush, policy, &allocator );
        REQUIRE( result.status != geometry_status_t::OK );
        REQUIRE_FALSE( result.bWatertight );
    }

    SECTION( "four-sided pyramid open underneath" ) {
        const std::vector<planed_t> planes = {
            UnitPlane( 1.0, 0.0, 1.0, 1.0 ), UnitPlane( -1.0, 0.0, 1.0, 1.0 ),
            UnitPlane( 0.0, 1.0, 1.0, 1.0 ), UnitPlane( 0.0, -1.0, 1.0, 1.0 ),
        };
        brush_holder_t holder{};
        BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );
        boundary_holder_t b{};
        REQUIRE( BrushBoundary_Init( &b.boundary, &allocator ) == geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy ) ==
                 geometry_status_t::DEGENERATE );
        REQUIRE( BrushBoundary_VertexCount( &b.boundary ) == 0u );
    }

    SECTION( "infinite square prism without caps" ) {
        std::vector<planed_t> planes = BoxPlanes();
        planes.resize( 4u );
        brush_holder_t holder{};
        BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );
        const brush_validation_result_t result =
            BrushValidation_Deep( &holder.brush, policy, &allocator );
        REQUIRE( result.status == geometry_status_t::DEGENERATE );
    }
}

TEST_CASE( "a redundant side reconstructs but fails deep validation",
           "[editor][geometry][brush][malformed]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    std::vector<planed_t> planes = BoxPlanes();
    planes.push_back( UnitPlane( 1.0, 0.0, 0.0, 5.0 ) ); // x <= 5 never touches the box
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );

    boundary_holder_t b{};
    REQUIRE( BrushBoundary_Init( &b.boundary, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_FaceCount( &b.boundary ) == 6u );
    common::usize iFace = 0u;
    REQUIRE( BrushBoundary_TryFindFaceForSide( &b.boundary, 6u, &iFace ) ==
             geometry_status_t::INVALID_ARGUMENT );

    const brush_validation_result_t result =
        BrushValidation_Deep( &holder.brush, policy, &allocator );
    REQUIRE( result.status == geometry_status_t::DEGENERATE );
    REQUIRE_FALSE( result.bWatertight );
}

TEST_CASE( "a vertex beyond the coordinate limit fails with LIMIT_EXCEEDED",
           "[editor][geometry][brush][malformed]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    policy.numerical.fCoordinateMagnitudeLimit = 10.0;

    // A thin wedge whose tip lands at x = 15 even though every plane
    // distance is within the limit.
    const std::vector<planed_t> planes = {
        UnitPlane( -1.0, 0.0, 0.0, 1.0 ),                    // x >= -1
        UnitPlane( 0.0, 1.0, 0.0, 1.0 ),                     // y <= 1
        UnitPlane( 0.0, 0.0, 1.0, 1.0 ),                     // z <= 1
        UnitPlane( 0.0, 0.0, -1.0, 1.0 ),                    // z >= -1
        UnitPlane( 1.0, -8.0, 0.0, 7.0 / std::sqrt( 65.0 ) ) // through (-1,-1) and (15,1)
    };
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );
    boundary_holder_t b{};
    REQUIRE( BrushBoundary_Init( &b.boundary, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( BrushBoundary_VertexCount( &b.boundary ) == 0u );

    policy.numerical.fCoordinateMagnitudeLimit = 100.0;
    REQUIRE( BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_VertexCount( &b.boundary ) == 6u );
}

TEST_CASE( "reconstruction rejects side counts above the policy limit",
           "[editor][geometry][brush][malformed]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    const std::vector<planed_t> planes = ChamferedBoxPlanes();
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );

    policy.limits.cBrushSidesPerBrushMax = 6u;
    boundary_holder_t b{};
    REQUIRE( BrushBoundary_Init( &b.boundary, &allocator ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &b.boundary, &holder.brush, policy ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( BrushValidation_Quick( &holder.brush, policy ) ==
             geometry_status_t::LIMIT_EXCEEDED );
}

//==========================================================================
// Identity
//==========================================================================

TEST_CASE( "brush sides reject duplicate and brush-aliasing identities",
           "[editor][geometry][brush][identity]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    brush_holder_t holder{};
    REQUIRE( BrushSolid_Init( &holder.brush, &allocator, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );

    brush_solid_side_t side{};
    side.plane = UnitPlane( 1.0, 0.0, 0.0, 1.0 );
    side.sourceId = geometry_source_id_t{ 2u };
    REQUIRE( BrushSolid_TryAddSide( &holder.brush, policy.limits, side, nullptr ) ==
             geometry_status_t::OK );

    REQUIRE( BrushSolid_TryAddSide( &holder.brush, policy.limits, side, nullptr ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    side.sourceId = geometry_source_id_t{ 1u };
    REQUIRE( BrushSolid_TryAddSide( &holder.brush, policy.limits, side, nullptr ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( BrushSolid_SideCount( &holder.brush ) == 1u );
}

TEST_CASE( "quick validation reports identity corruption after plane checks",
           "[editor][geometry][brush][identity]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    const std::vector<planed_t> planes = BoxPlanes();
    brush_holder_t holder{};
    BuildBrush( &holder.brush, &allocator, policy, planes, IdentityOrder( planes.size() ) );
    REQUIRE( BrushValidation_Quick( &holder.brush, policy ) == geometry_status_t::OK );

    // Bypass storage checks the way a corrupt deserializer would.
    holder.brush.sides.pData[4].sourceId = holder.brush.sides.pData[1].sourceId;
    REQUIRE( BrushValidation_Quick( &holder.brush, policy ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( BrushValidation_Deep( &holder.brush, policy, &allocator ).status ==
             geometry_status_t::IDENTITY_CONFLICT );

    // A geometric defect is reported ahead of the identity defect.
    holder.brush.sides.pData[0].plane = holder.brush.sides.pData[2].plane;
    REQUIRE( BrushValidation_Quick( &holder.brush, policy ) ==
             geometry_status_t::DEGENERATE );

    holder.brush.sides.pData[0].plane = planes[0];
    holder.brush.sides.pData[4].sourceId = holder.brush.sourceId;
    REQUIRE( BrushValidation_Quick( &holder.brush, policy ) ==
             geometry_status_t::IDENTITY_CONFLICT );
}

TEST_CASE( "box generator leaves the ID allocator untouched on exhaustion",
           "[editor][geometry][brush][identity]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    const geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    REQUIRE( GeometrySourceIdAllocator_Reset(
                 &ids, geometry_source_id_t{ common::CY_U64_MAX - 3u } ) ==
             geometry_status_t::OK );

    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &ids,
                 cypher::math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                 cypher::math::Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( ids.next.value == common::CY_U64_MAX - 3u );
    REQUIRE( brush.sides.pAllocator == nullptr );
    REQUIRE_FALSE( GeometrySourceId_IsValid( brush.sourceId ) );
}

TEST_CASE( "box generator leaves the ID allocator untouched on allocation failure",
           "[editor][geometry][brush][identity]" ) {
    const geometry_policy_t policy{};
    failing_allocator_state_t state{};
    common::allocator_t allocator = MakeFailingAllocator( &state );
    state.iFailure = 0u;

    geometry_source_id_allocator_t ids{};
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &ids,
                 cypher::math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                 cypher::math::Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::ALLOCATION_FAILED );
    REQUIRE( ids.next.value == 1u );
    REQUIRE( brush.sides.pAllocator == nullptr );

    state.iFailure = common::CY_USIZE_MAX;
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &ids,
                 cypher::math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                 cypher::math::Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( ids.next.value == 8u );
    BrushSolid_Shutdown( &brush );
}

} // namespace cypher::editor::geometry
