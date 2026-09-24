//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCSG_Tests.cpp
//  Purpose: Verifies brush CSG classification, intersection, subtraction.
//  Details: Covers Gate 6 CSG acceptance: disjoint, contained, touching
//           (point/edge/face), shared-plane, rotated operands, sliver
//           detection, and fragment-limit enforcement. Every output side
//           has provenance (source ID inherited from its operand). Failure
//           leaves both operands unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCSG.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushTransform.h"
#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_Document.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Planed_Make;
using Catch::Approx;

namespace {

// Two-brush fixture with boundaries reconstructed and ready for CSG.
struct CSGFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brushA{};
    brush_solid_t brushB{};
    brush_boundary_t boundaryA{};
    brush_boundary_t boundaryB{};

    // Creates two boxes at the given centers with the given half-extents.
    void init(
        math::vec3d_t centerA, math::vec3d_t halfA,
        math::vec3d_t centerB, math::vec3d_t halfB )
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brushA, &allocator, policy, &idAlloc,
                     centerA, halfA ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brushB, &allocator, policy, &idAlloc,
                     centerB, halfB ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundaryA, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundaryB, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundaryA, &brushA, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundaryB, &brushB, policy ) ==
                 geometry_status_t::OK );
    }

    ~CSGFixture()
    {
        BrushBoundary_Shutdown( &boundaryB );
        BrushBoundary_Shutdown( &boundaryA );
        BrushSolid_Shutdown( &brushB );
        BrushSolid_Shutdown( &brushA );
    }
};

struct csg_failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *CSGFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<csg_failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) {
        return nullptr;
    }

    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    void *pMemory = pSystem->pfnAllocate(
        pSystem->pUserData, cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void CSGFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<csg_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }

    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree(
        pSystem->pUserData, pMemory, cbSize, nAlignment );
}

common::allocator_t MakeCSGFailureAllocator(
    csg_failure_allocator_state_t *pState ) noexcept
{
    return {
        &CSGFailureAllocate,
        nullptr,
        &CSGFailureFree,
        pState
    };
}

void RequireCanonicalBrushOutput( const brush_solid_t &brush )
{
    REQUIRE( brush.sides.pData == nullptr );
    REQUIRE( brush.sides.nCount == 0u );
    REQUIRE( brush.sides.nCapacity == 0u );
    REQUIRE( brush.sides.pAllocator == nullptr );
    REQUIRE_FALSE( GeometrySourceId_IsValid( brush.sourceId ) );
}

void RequireCanonicalSubtractOutput(
    const brush_csg_subtract_result_t &result )
{
    REQUIRE( result.fragments == nullptr );
    REQUIRE( result.cFragments == 0u );
    REQUIRE( result.cCapacity == 0u );
    REQUIRE( result.pAllocator == nullptr );
}

bool TestPlanesEquivalent(
    math::planed_t a,
    math::planed_t b,
    common::f64 tolerance = 1.0e-12 )
{
    return math::Scalar_Abs( a.normal.x - b.normal.x ) <= tolerance &&
           math::Scalar_Abs( a.normal.y - b.normal.y ) <= tolerance &&
           math::Scalar_Abs( a.normal.z - b.normal.z ) <= tolerance &&
           math::Scalar_Abs( a.d - b.d ) <= tolerance;
}

const brush_solid_side_t *FindSideById(
    const brush_solid_t &brush,
    geometry_source_id_t id )
{
    for ( common::usize iSide = 0u;
          iSide < brush.sides.nCount;
          ++iSide ) {
        if ( brush.sides.pData[iSide].sourceId.value == id.value ) {
            return &brush.sides.pData[iSide];
        }
    }
    return nullptr;
}

bool SubtractResultPublishesId(
    const brush_csg_subtract_result_t &result,
    common::u64 value )
{
    for ( common::usize iFragment = 0u;
          iFragment < result.cFragments;
          ++iFragment ) {
        const brush_solid_t &fragment = result.fragments[iFragment];
        if ( fragment.sourceId.value == value ) {
            return true;
        }
        for ( common::usize iSide = 0u;
              iSide < fragment.sides.nCount;
              ++iSide ) {
            if ( fragment.sides.pData[iSide].sourceId.value == value ) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

TEST_CASE( "CSG: classify disjoint brushes",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // Two boxes far apart — no overlap.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 10.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1e-6 ) ==
             brush_csg_classification_t::DISJOINT );
}

TEST_CASE( "CSG: classify overlapping brushes",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // Two boxes overlapping in the middle.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1e-6 ) ==
             brush_csg_classification_t::INTERSECTS );
}

TEST_CASE( "CSG: classify A inside B",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // Small box inside big box.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 0.5, 0.5, 0.5 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 2.0, 2.0 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1e-6 ) ==
             brush_csg_classification_t::A_INSIDE_B );
}

TEST_CASE( "CSG: classify B inside A",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // Big box around small box.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 2.0, 2.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 0.5, 0.5, 0.5 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1e-6 ) ==
             brush_csg_classification_t::B_INSIDE_A );
}

TEST_CASE( "CSG: classify face-touching brushes",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // Two unit boxes sharing the +X/-X face at x=1.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 2.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1e-6 ) ==
             brush_csg_classification_t::TOUCHING );
}

TEST_CASE( "CSG: classify edge-touching brushes",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // Two unit boxes touching at one edge (diagonal in XY).
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 2.0, 2.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1e-6 ) ==
             brush_csg_classification_t::TOUCHING );
}

TEST_CASE( "CSG: classify point-touching brushes",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // Two unit boxes touching at exactly one corner.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 2.0, 2.0, 2.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1e-6 ) ==
             brush_csg_classification_t::TOUCHING );
}

TEST_CASE( "CSG: classify null args returns INVALID",
           "[Gate6][CSG]" )
{
    REQUIRE( BrushCSG_Classify(
                 nullptr, nullptr, nullptr, nullptr, 1e-6 ) ==
             brush_csg_classification_t::INVALID );
}

TEST_CASE( "CSG: classify rejects a non-finite tolerance",
           "[Gate6][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB,
                 std::numeric_limits<common::f64>::quiet_NaN() ) ==
             brush_csg_classification_t::INVALID );
}

TEST_CASE( "CSG: classify rejects non-finite brush planes",
           "[Gate6][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    f.brushA.sides.pData[0].plane.d =
        std::numeric_limits<common::f64>::quiet_NaN();
    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1.0e-6 ) ==
             brush_csg_classification_t::INVALID );
}

TEST_CASE( "CSG: classify rejects valid boundaries paired with other brushes",
           "[Gate6][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( -0.5, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make(  0.5, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryB,
                 &f.brushB, &f.boundaryA, 1.0e-6 ) ==
             brush_csg_classification_t::INVALID );
    REQUIRE( BrushCSG_Classify(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB, 1.0e-6 ) ==
             brush_csg_classification_t::INTERSECTS );
}

// ---------------------------------------------------------------------------
// Intersection
// ---------------------------------------------------------------------------

TEST_CASE( "CSG: intersect overlapping boxes produces valid brush",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // Box A: (-1,-1,-1) to (1,1,1)
    // Box B: (0,-1,-1) to (2,1,1)
    // Intersection: (0,-1,-1) to (1,1,1)
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_solid_t result{};
    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::OK );

    // The intersection should be a valid brush with 6 faces.
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &f.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &result, f.policy ) ==
             geometry_status_t::OK );

    REQUIRE( boundary.vertices.nCount == 8u );
    REQUIRE( boundary.faces.nCount == 6u );
    REQUIRE( BrushSolid_SideCount( &result ) == 6u );

    // Verify bounds: should be [0,1] x [-1,1] x [-1,1].
    const math::aabbd_t bounds =
        BrushQueries_ComputeBoundsd( &boundary );
    REQUIRE( bounds.minimum.x == Approx( 0.0 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.x == Approx( 1.0 ).margin( 0.01 ) );
    REQUIRE( bounds.minimum.y == Approx( -1.0 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.y == Approx( 1.0 ).margin( 0.01 ) );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &result );
}

TEST_CASE( "CSG: intersect A contained in B returns copy of A",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 0.5, 0.5, 0.5 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 2.0, 2.0 ) );

    brush_solid_t result{};
    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::OK );

    // Result bounds should match A's bounds.
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &f.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &result, f.policy ) ==
             geometry_status_t::OK );

    const math::aabbd_t bounds =
        BrushQueries_ComputeBoundsd( &boundary );
    REQUIRE( bounds.minimum.x == Approx( -0.5 ).margin( 0.01 ) );
    REQUIRE( bounds.maximum.x == Approx( 0.5 ).margin( 0.01 ) );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &result );
}

TEST_CASE( "CSG: intersect disjoint boxes returns DEGENERATE",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 10.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    const geometry_source_id_t nextBefore = f.idAlloc.next;
    brush_solid_t result{};
    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::DEGENERATE );
    RequireCanonicalBrushOutput( result );
    REQUIRE( f.idAlloc.next.value == nextBefore.value );
}

TEST_CASE( "CSG: intersect preserves side provenance",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    // Collect source IDs from both operands.
    const common::usize cSidesA = BrushSolid_SideCount( &f.brushA );
    const common::usize cSidesB = BrushSolid_SideCount( &f.brushB );
    for ( common::usize i = 0u; i < cSidesA; ++i ) {
        f.brushA.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 100u + i );
    }
    for ( common::usize i = 0u; i < cSidesB; ++i ) {
        f.brushB.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 200u + i );
    }

    brush_solid_t result{};
    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::OK );

    // Every output side's source ID should match one from A or B.
    const common::usize cResultSides = BrushSolid_SideCount( &result );
    for ( common::usize i = 0u; i < cResultSides; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( &result, i, &side );

        bool bFoundInA = false;
        for ( common::usize j = 0u; j < cSidesA; ++j ) {
            brush_solid_side_t sideA{};
            (void)BrushSolid_TryGetSide( &f.brushA, j, &sideA );
            if ( side.sourceId.value == sideA.sourceId.value ) {
                bFoundInA = true;
                REQUIRE( side.iAttributeIndex == sideA.iAttributeIndex );
                break;
            }
        }

        bool bFoundInB = false;
        if ( !bFoundInA ) {
            for ( common::usize j = 0u; j < cSidesB; ++j ) {
                brush_solid_side_t sideB{};
                (void)BrushSolid_TryGetSide( &f.brushB, j, &sideB );
                if ( side.sourceId.value == sideB.sourceId.value ) {
                    bFoundInB = true;
                    REQUIRE( side.iAttributeIndex ==
                             sideB.iAttributeIndex );
                    break;
                }
            }
        }

        REQUIRE( ( bFoundInA || bFoundInB ) );
    }

    BrushSolid_Shutdown( &result );
}

TEST_CASE( "CSG: intersection identity side order and attributes are deterministic",
           "[Gate6][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 0.5, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );
    for ( common::usize i = 0u; i < f.brushA.sides.nCount; ++i ) {
        f.brushA.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 10u + i );
    }
    for ( common::usize i = 0u; i < f.brushB.sides.nCount; ++i ) {
        f.brushB.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 20u + i );
    }

    geometry_source_id_allocator_t idsA = f.idAlloc;
    geometry_source_id_allocator_t idsB = f.idAlloc;
    brush_solid_t resultA{};
    brush_solid_t resultB{};
    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB, &f.allocator,
                 &idsA, f.policy, &resultA ) == geometry_status_t::OK );
    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB, &f.allocator,
                 &idsB, f.policy, &resultB ) == geometry_status_t::OK );

    REQUIRE( idsA.next.value == idsB.next.value );
    REQUIRE( resultA.sourceId.value == resultB.sourceId.value );
    REQUIRE( resultA.sides.nCount == resultB.sides.nCount );
    for ( common::usize i = 0u; i < resultA.sides.nCount; ++i ) {
        const brush_solid_side_t &a = resultA.sides.pData[i];
        const brush_solid_side_t &b = resultB.sides.pData[i];
        REQUIRE( a.plane.normal.x == b.plane.normal.x );
        REQUIRE( a.plane.normal.y == b.plane.normal.y );
        REQUIRE( a.plane.normal.z == b.plane.normal.z );
        REQUIRE( a.plane.d == b.plane.d );
        REQUIRE( a.sourceId.value == b.sourceId.value );
        REQUIRE( a.iAttributeIndex == b.iAttributeIndex );
    }

    BrushSolid_Shutdown( &resultB );
    BrushSolid_Shutdown( &resultA );
}

TEST_CASE( "CSG: intersect rotated overlapping brushes",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 2.0, 2.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    // Rotate B by 45° about Z axis.
    const common::f64 c = std::cos( 3.14159265358979323846 / 4.0 );
    const common::f64 s = std::sin( 3.14159265358979323846 / 4.0 );
    const math::affine3d_t rot = math::Affine3d_FromColumns(
        Vec3d_Make( c, s, 0.0 ),
        Vec3d_Make( -s, c, 0.0 ),
        Vec3d_Make( 0.0, 0.0, 1.0 ),
        Vec3d_Make( 0.0, 0.0, 0.0 ) );

    REQUIRE( BrushTransform_TryRotate(
                 &f.brushB, Vec3d_Make( 0.0, 0.0, 0.0 ), rot ) ==
             geometry_status_t::OK );

    // Rebuild B boundary after rotation.
    REQUIRE( BrushBoundary_TryReconstruct(
                 &f.boundaryB, &f.brushB, f.policy ) ==
             geometry_status_t::OK );

    // Rotated B is still inside A, so intersection should succeed.
    brush_solid_t result{};
    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::OK );

    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &f.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &result, f.policy ) ==
             geometry_status_t::OK );

    // Rotated unit box has 8 verts, 12 edges, 6 faces.
    REQUIRE( boundary.vertices.nCount == 8u );
    REQUIRE( boundary.edges.nCount == 12u );
    REQUIRE( boundary.faces.nCount == 6u );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &result );
}

TEST_CASE( "CSG: intersect null args rejected",
           "[Gate6][CSG]" )
{
    REQUIRE( BrushCSG_TryIntersect(
                 nullptr, nullptr, nullptr, nullptr,
                 geometry_policy_t{}, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "CSG: intersect rejects a live output and preserves identities",
           "[Gate6][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 0.5, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_solid_t output{};
    REQUIRE( BrushSolid_DeepCopy(
                 &output, &f.brushA, &f.allocator,
                 f.policy.limits ) == geometry_status_t::OK );
    const geometry_source_id_t nextBefore = f.idAlloc.next;
    const common::usize cSidesBefore = BrushSolid_SideCount( &output );

    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc, f.policy,
                 &output ) == geometry_status_t::ALREADY_INITIALIZED );
    REQUIRE( f.idAlloc.next.value == nextBefore.value );
    REQUIRE( BrushSolid_SideCount( &output ) == cSidesBefore );
    BrushSolid_Shutdown( &output );
}

TEST_CASE( "CSG: intersect is allocation and identity atomic",
           "[Gate6][CSG][Allocation][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 0.5, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    csg_failure_allocator_state_t baselineState{};
    baselineState.iFailOnCall =
        std::numeric_limits<common::usize>::max();
    common::allocator_t baselineAllocator =
        MakeCSGFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds = f.idAlloc;
    brush_solid_t baselineResult{};
    REQUIRE( BrushCSG_TryIntersect(
                 &f.brushA, &f.brushB,
                 &baselineAllocator, &baselineIds, f.policy,
                 &baselineResult ) == geometry_status_t::OK );
    const common::usize cAllocationCalls = baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    BrushSolid_Shutdown( &baselineResult );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        DYNAMIC_SECTION( "allocation call " << iFail ) {
            csg_failure_allocator_state_t state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator =
                MakeCSGFailureAllocator( &state );
            geometry_source_id_allocator_t ids = f.idAlloc;
            const geometry_source_id_t nextBefore = ids.next;
            brush_solid_t result{};

            REQUIRE( BrushCSG_TryIntersect(
                         &f.brushA, &f.brushB,
                         &allocator, &ids, f.policy,
                         &result ) == geometry_status_t::ALLOCATION_FAILED );
            RequireCanonicalBrushOutput( result );
            REQUIRE( ids.next.value == nextBefore.value );
            REQUIRE( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

// ---------------------------------------------------------------------------
// Subtraction
// ---------------------------------------------------------------------------

TEST_CASE( "CSG: subtract overlapping box produces fragments",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // A: (-2,-1,-1) to (2,1,1). B: (-1,-1,-1) to (1,1,1).
    // B is centered inside A, so A\B should produce fragments on ±X sides.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::OK );

    // B has 6 sides, so up to 6 fragments. The ±Y and ±Z planes of B
    // are coplanar with A, so those produce degenerate fragments that get
    // pruned. Only the ±X planes produce valid fragments.
    REQUIRE( result.cFragments >= 1u );
    REQUIRE( result.cFragments <= 6u );

    // Every fragment should produce a valid boundary.
    for ( common::usize i = 0u; i < result.cFragments; ++i ) {
        brush_boundary_t boundary{};
        REQUIRE( BrushBoundary_Init( &boundary, &f.allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &result.fragments[i], f.policy ) ==
                 geometry_status_t::OK );
        REQUIRE( boundary.vertices.nCount >= 4u );
        BrushBoundary_Shutdown( &boundary );
    }

    BrushCSGSubtractResult_Shutdown( &result );
}

TEST_CASE( "CSG: subtract disjoint brush returns full copy of A",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 10.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::OK );

    // When B doesn't overlap A, all of A survives as fragments.
    // The remainder after clipping by all of B's planes should be empty
    // (it equals A∩B which is empty), and all 6 clip iterations should
    // produce fragments that together reconstruct A.
    REQUIRE( result.cFragments == 1u );
    REQUIRE( result.cCapacity == 1u );
    REQUIRE( result.pAllocator == &f.allocator );

    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &f.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &result.fragments[0], f.policy ) ==
             geometry_status_t::OK );
    const math::aabbd_t bounds = BrushQueries_ComputeBoundsd( &boundary );
    REQUIRE( bounds.minimum.x == Approx( -1.0 ) );
    REQUIRE( bounds.maximum.x == Approx( 1.0 ) );
    BrushBoundary_Shutdown( &boundary );

    BrushCSGSubtractResult_Shutdown( &result );
}

TEST_CASE( "CSG: subtract B contains A returns DEGENERATE",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // A is inside B — subtraction is empty.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 0.5, 0.5, 0.5 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 2.0, 2.0 ) );

    const geometry_source_id_t nextBefore = f.idAlloc.next;
    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::DEGENERATE );

    REQUIRE( result.cFragments == 0u );
    RequireCanonicalSubtractOutput( result );
    REQUIRE( f.idAlloc.next.value == nextBefore.value );
}

TEST_CASE( "CSG: subtract preserves operand A unchanged",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    // Record A's planes before subtraction.
    const common::usize cSidesA = BrushSolid_SideCount( &f.brushA );
    math::planed_t origPlanes[6];
    for ( common::usize i = 0u; i < cSidesA; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( &f.brushA, i, &side );
        origPlanes[i] = side.plane;
    }

    brush_csg_subtract_result_t result{};
    (void)BrushCSG_TrySubtract(
        &f.brushA, &f.brushB,
        &f.allocator, &f.idAlloc,
        f.policy, &result );

    // A must be unchanged.
    REQUIRE( BrushSolid_SideCount( &f.brushA ) == cSidesA );
    for ( common::usize i = 0u; i < cSidesA; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( &f.brushA, i, &side );
        REQUIRE( side.plane.normal.x == origPlanes[i].normal.x );
        REQUIRE( side.plane.normal.y == origPlanes[i].normal.y );
        REQUIRE( side.plane.normal.z == origPlanes[i].normal.z );
        REQUIRE( side.plane.d == origPlanes[i].d );
    }

    BrushCSGSubtractResult_Shutdown( &result );
}

TEST_CASE( "CSG: subtract fragments have distinct brush IDs",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::OK );

    // All fragment IDs must be unique and different from the operands.
    for ( common::usize i = 0u; i < result.cFragments; ++i ) {
        REQUIRE( result.fragments[i].sourceId.value !=
                 f.brushA.sourceId.value );
        REQUIRE( result.fragments[i].sourceId.value !=
                 f.brushB.sourceId.value );
        for ( common::usize j = i + 1u; j < result.cFragments; ++j ) {
            REQUIRE( result.fragments[i].sourceId.value !=
                     result.fragments[j].sourceId.value );
        }
    }

    BrushCSGSubtractResult_Shutdown( &result );
}

TEST_CASE( "CSG: subtraction fragment order identities and attributes are deterministic",
           "[Gate6][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 2.0, 2.0 ),
            Vec3d_Make( 0.5, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );
    for ( common::usize i = 0u; i < f.brushA.sides.nCount; ++i ) {
        f.brushA.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 30u + i );
    }
    for ( common::usize i = 0u; i < f.brushB.sides.nCount; ++i ) {
        f.brushB.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 40u + i );
    }

    geometry_source_id_allocator_t idsA = f.idAlloc;
    geometry_source_id_allocator_t idsB = f.idAlloc;
    const common::u64 firstFreshId = idsA.next.value;
    brush_csg_subtract_result_t resultA{};
    brush_csg_subtract_result_t resultB{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB, &f.allocator,
                 &idsA, f.policy, &resultA ) == geometry_status_t::OK );
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB, &f.allocator,
                 &idsB, f.policy, &resultB ) == geometry_status_t::OK );

    REQUIRE( idsA.next.value == idsB.next.value );
    REQUIRE( resultA.cFragments == resultB.cFragments );
    for ( common::usize iFragment = 0u;
          iFragment < resultA.cFragments;
          ++iFragment ) {
        const brush_solid_t &a = resultA.fragments[iFragment];
        const brush_solid_t &b = resultB.fragments[iFragment];
        REQUIRE( a.sourceId.value == b.sourceId.value );
        REQUIRE( a.sourceId.value >= firstFreshId );
        REQUIRE( a.sourceId.value < idsA.next.value );
        REQUIRE( a.sides.nCount == b.sides.nCount );
        for ( common::usize iSide = 0u;
              iSide < a.sides.nCount;
              ++iSide ) {
            const brush_solid_side_t &sideA = a.sides.pData[iSide];
            const brush_solid_side_t &sideB = b.sides.pData[iSide];
            REQUIRE( sideA.plane.normal.x == sideB.plane.normal.x );
            REQUIRE( sideA.plane.normal.y == sideB.plane.normal.y );
            REQUIRE( sideA.plane.normal.z == sideB.plane.normal.z );
            REQUIRE( sideA.plane.d == sideB.plane.d );
            REQUIRE( sideA.sourceId.value == sideB.sourceId.value );
            REQUIRE( sideA.iAttributeIndex ==
                     sideB.iAttributeIndex );

            const brush_solid_side_t *pInherited = FindSideById(
                f.brushA, sideA.sourceId );
            if ( pInherited != nullptr ) {
                REQUIRE( sideA.iAttributeIndex ==
                         pInherited->iAttributeIndex );
                REQUIRE( TestPlanesEquivalent(
                             sideA.plane, pInherited->plane ) );
                continue;
            }

            REQUIRE( sideA.sourceId.value >= firstFreshId );
            REQUIRE( sideA.sourceId.value < idsA.next.value );
            bool bMatchedClipSource = false;
            for ( common::usize iSource = 0u;
                  iSource < f.brushB.sides.nCount;
                  ++iSource ) {
                const brush_solid_side_t &source =
                    f.brushB.sides.pData[iSource];
                if ( TestPlanesEquivalent( sideA.plane, source.plane ) ||
                     TestPlanesEquivalent(
                         sideA.plane, math::Planed_Flip( source.plane ) ) ) {
                    REQUIRE( sideA.iAttributeIndex ==
                             source.iAttributeIndex );
                    bMatchedClipSource = true;
                    break;
                }
            }
            REQUIRE( bMatchedClipSource );
        }
    }

    // Staging may allocate identities for candidate/remainder sides that are
    // later pruned. A successful publication commits the monotonic allocator
    // position, so those deterministic unpublished identities remain gaps.
    bool bHasStagedIdentityGap = false;
    for ( common::u64 value = firstFreshId;
          value < idsA.next.value;
          ++value ) {
        if ( !SubtractResultPublishesId( resultA, value ) ) {
            bHasStagedIdentityGap = true;
            break;
        }
    }
    REQUIRE( bHasStagedIdentityGap );

    BrushCSGSubtractResult_Shutdown( &resultB );
    BrushCSGSubtractResult_Shutdown( &resultA );
}

TEST_CASE( "CSG: subtract null args rejected",
           "[Gate6][CSG]" )
{
    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TrySubtract(
                 nullptr, nullptr, nullptr, nullptr,
                 geometry_policy_t{}, &result ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "CSG: subtract rejects live and corrupt output records",
           "[Gate6][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_csg_subtract_result_t live{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc, f.policy,
                 &live ) == geometry_status_t::OK );
    const geometry_source_id_t nextBefore = f.idAlloc.next;
    brush_solid_t *const pFragmentsBefore = live.fragments;
    const common::usize cFragmentsBefore = live.cFragments;

    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc, f.policy,
                 &live ) == geometry_status_t::ALREADY_INITIALIZED );
    REQUIRE( live.fragments == pFragmentsBefore );
    REQUIRE( live.cFragments == cFragmentsBefore );
    REQUIRE( f.idAlloc.next.value == nextBefore.value );
    BrushCSGSubtractResult_Shutdown( &live );

    brush_csg_subtract_result_t corrupt{};
    corrupt.cFragments = 1u;
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc, f.policy,
                 &corrupt ) == geometry_status_t::CORRUPT_STATE );
    REQUIRE( corrupt.cFragments == 1u );
    corrupt.cFragments = 0u;
}

TEST_CASE( "CSG: subtract is allocation and identity atomic",
           "[Gate6][CSG][Allocation][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    csg_failure_allocator_state_t baselineState{};
    baselineState.iFailOnCall =
        std::numeric_limits<common::usize>::max();
    common::allocator_t baselineAllocator =
        MakeCSGFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds = f.idAlloc;
    brush_csg_subtract_result_t baselineResult{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &baselineAllocator, &baselineIds, f.policy,
                 &baselineResult ) == geometry_status_t::OK );
    const common::usize cAllocationCalls = baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    BrushCSGSubtractResult_Shutdown( &baselineResult );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        DYNAMIC_SECTION( "allocation call " << iFail ) {
            csg_failure_allocator_state_t state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator =
                MakeCSGFailureAllocator( &state );
            geometry_source_id_allocator_t ids = f.idAlloc;
            const geometry_source_id_t nextBefore = ids.next;
            brush_csg_subtract_result_t result{};

            REQUIRE( BrushCSG_TrySubtract(
                         &f.brushA, &f.brushB,
                         &allocator, &ids, f.policy,
                         &result ) == geometry_status_t::ALLOCATION_FAILED );
            RequireCanonicalSubtractOutput( result );
            REQUIRE( ids.next.value == nextBefore.value );
            REQUIRE( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

TEST_CASE( "CSG: subtract obeys scratch policy before publishing",
           "[Gate6][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    geometry_policy_t constrained = f.policy;
    constrained.limits.cbScratchMax = 1u;
    geometry_source_id_allocator_t ids = f.idAlloc;
    const geometry_source_id_t nextBefore = ids.next;
    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &ids, constrained,
                 &result ) == geometry_status_t::LIMIT_EXCEEDED );
    RequireCanonicalSubtractOutput( result );
    REQUIRE( ids.next.value == nextBefore.value );
}

TEST_CASE( "CSG: subtract sliver overlap produces bounded result",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    // A and B are near-coincident boxes offset by a tiny amount along X.
    // The subtraction creates sliver-thin fragments. The system must either
    // produce valid (though thin) fragments or reject them as degenerate —
    // it must not crash, produce NaN, or exhaust resources.
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 0.001, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_csg_subtract_result_t result{};
    const geometry_status_t status = BrushCSG_TrySubtract(
        &f.brushA, &f.brushB,
        &f.allocator, &f.idAlloc,
        f.policy, &result );

    // Either OK with bounded fragments, or DEGENERATE if the slivers
    // collapsed. Both are acceptable bounded diagnostics.
    REQUIRE( ( status == geometry_status_t::OK ||
               status == geometry_status_t::DEGENERATE ) );

    if ( status == geometry_status_t::OK ) {
        REQUIRE( result.cFragments >= 1u );
        REQUIRE( result.cFragments <= 6u );

        // Every surviving fragment must reconstruct cleanly.
        for ( common::usize i = 0u; i < result.cFragments; ++i ) {
            brush_boundary_t boundary{};
            REQUIRE( BrushBoundary_Init( &boundary, &f.allocator ) ==
                     geometry_status_t::OK );
            REQUIRE( BrushBoundary_TryReconstruct(
                         &boundary, &result.fragments[i], f.policy ) ==
                     geometry_status_t::OK );
            BrushBoundary_Shutdown( &boundary );
        }
    }

    BrushCSGSubtractResult_Shutdown( &result );
}

TEST_CASE( "CSG: subtract high-side-count brush stays within fragment limit",
           "[Gate6][CSG]" )
{
    // Subtract a 32-sided prism from a large box. The prism has 34 planes
    // (32 lateral + 2 caps), so up to 34 fragments. This exercises the
    // many-fragment path and verifies the result stays bounded by the
    // mathematical one-fragment-per-cutter-side upper bound.
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};

    brush_solid_t boxA{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &boxA, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 5.0, 5.0, 5.0 ) ) ==
             geometry_status_t::OK );

    brush_solid_t prismB{};
    REQUIRE( BrushGenerator_TryMakePrism(
                 &prismB, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 2.0, 3.0, 32u, 2u ) ==
             geometry_status_t::OK );

    brush_csg_subtract_result_t result{};
    const geometry_status_t status = BrushCSG_TrySubtract(
        &boxA, &prismB,
        &allocator, &idAlloc,
        policy, &result );

    REQUIRE( status == geometry_status_t::OK );
    REQUIRE( result.cFragments >= 1u );
    REQUIRE( result.cFragments <= BrushSolid_SideCount( &prismB ) );

    // Every fragment must reconstruct to a valid boundary.
    for ( common::usize i = 0u; i < result.cFragments; ++i ) {
        brush_boundary_t boundary{};
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &result.fragments[i], policy ) ==
                 geometry_status_t::OK );
        REQUIRE( boundary.vertices.nCount >= 4u );
        BrushBoundary_Shutdown( &boundary );
    }

    BrushCSGSubtractResult_Shutdown( &result );
    BrushSolid_Shutdown( &prismB );
    BrushSolid_Shutdown( &boxA );
}

TEST_CASE( "CSG: shutdown cleans up fragments",
           "[Gate6][CSG]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TrySubtract(
                 &f.brushA, &f.brushB,
                 &f.allocator, &f.idAlloc,
                 f.policy, &result ) ==
             geometry_status_t::OK );

    REQUIRE( result.cFragments > 0u );

    // Shutdown should clean up without crashing.
    BrushCSGSubtractResult_Shutdown( &result );
    RequireCanonicalSubtractOutput( result );

    // Calling shutdown again should be safe.
    BrushCSGSubtractResult_Shutdown( &result );
}

// ===========================================================================
// CSG Hollow (Gate 8)
// ===========================================================================

TEST_CASE( "CSG: hollow box produces 6 wall fragments",
           "[Gate8][CSG]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 4.0, 4.0, 4.0 ) ) ==
             geometry_status_t::OK );

    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TryHollow(
                 &brush, 1.0,
                 &allocator, &idAlloc, policy,
                 &result ) ==
             geometry_status_t::OK );

    // A box hollowed with uniform wall thickness produces exactly 6
    // slab fragments — one per face.
    REQUIRE( result.cFragments == 6u );

    bool bHasInteriorCutSurface = false;
    REQUIRE_FALSE( result.sideProvenance.nCount == 0u );
    for ( common::usize i = 0u;
          i < result.sideProvenance.nCount;
          ++i ) {
        const brush_csg_raw_side_provenance_t &provenance =
            result.sideProvenance.pData[i];
        CHECK( provenance.sourceBrushId.value == brush.sourceId.value );

        const brush_solid_side_t *pSourceSide = nullptr;
        for ( common::usize iSide = 0u;
              iSide < brush.sides.nCount;
              ++iSide ) {
            if ( brush.sides.pData[iSide].sourceId.value ==
                 provenance.sourceSideId.value ) {
                pSourceSide = &brush.sides.pData[iSide];
                break;
            }
        }
        REQUIRE( pSourceSide != nullptr );
        CHECK( provenance.iSourceAttribute ==
               pSourceSide->iAttributeIndex );
        bHasInteriorCutSurface = bHasInteriorCutSurface ||
            provenance.operand == brush_csg_operand_t::SUBTRAHEND_B;
    }
    CHECK( bHasInteriorCutSurface );

    // Each fragment should produce a valid boundary.
    for ( common::usize i = 0u; i < result.cFragments; ++i ) {
        brush_boundary_t fb{};
        REQUIRE( BrushBoundary_Init( &fb, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &fb, &result.fragments[i], policy ) ==
                 geometry_status_t::OK );

        // Euler check on each fragment.
        const auto cV = BrushBoundary_VertexCount( &fb );
        const auto cE = BrushBoundary_EdgeCount( &fb );
        const auto cF = BrushBoundary_FaceCount( &fb );
        REQUIRE( static_cast<common::i32>( cV ) -
                 static_cast<common::i32>( cE ) +
                 static_cast<common::i32>( cF ) == 2 );

        BrushBoundary_Shutdown( &fb );
    }

    BrushCSGSubtractResult_Shutdown( &result );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "CSG: hollow with excessive thickness returns DEGENERATE",
           "[Gate8][CSG]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    // Box with half-extents 2.0 → wall thickness 3.0 exceeds the interior.
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 2.0, 2.0, 2.0 ) ) ==
             geometry_status_t::OK );

    const geometry_source_id_t nextBefore = idAlloc.next;
    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TryHollow(
                 &brush, 3.0,
                 &allocator, &idAlloc, policy,
                 &result ) ==
             geometry_status_t::DEGENERATE );
    RequireCanonicalSubtractOutput( result );
    REQUIRE( idAlloc.next.value == nextBefore.value );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "CSG: hollow null args rejected",
           "[Gate8][CSG]" )
{
    REQUIRE( BrushCSG_TryHollow(
                 nullptr, 1.0,
                 nullptr, nullptr,
                 geometry_policy_t{}, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "CSG: hollow zero thickness returns DEGENERATE",
           "[Gate8][CSG]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    brush_solid_t brush{};

    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &allocator, policy, &idAlloc,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 4.0, 4.0, 4.0 ) ) ==
             geometry_status_t::OK );

    brush_csg_subtract_result_t result{};
    REQUIRE( BrushCSG_TryHollow(
                 &brush, 0.0,
                 &allocator, &idAlloc, policy,
                 &result ) ==
             geometry_status_t::DEGENERATE );

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "CSG: hollow is allocation and identity atomic",
           "[Gate8][CSG][Allocation][Contract]" )
{
    common::allocator_t sourceAllocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t sourceIds{};
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush, &sourceAllocator, policy, &sourceIds,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 4.0, 4.0, 4.0 ) ) ==
             geometry_status_t::OK );

    csg_failure_allocator_state_t baselineState{};
    baselineState.iFailOnCall =
        std::numeric_limits<common::usize>::max();
    common::allocator_t baselineAllocator =
        MakeCSGFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds = sourceIds;
    brush_csg_subtract_result_t baselineResult{};
    REQUIRE( BrushCSG_TryHollow(
                 &brush, 1.0, &baselineAllocator,
                 &baselineIds, policy, &baselineResult ) ==
             geometry_status_t::OK );
    const common::usize cAllocationCalls = baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    BrushCSGSubtractResult_Shutdown( &baselineResult );
    REQUIRE( baselineState.cSuccessfulAllocations == baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        DYNAMIC_SECTION( "allocation call " << iFail ) {
            csg_failure_allocator_state_t state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator =
                MakeCSGFailureAllocator( &state );
            geometry_source_id_allocator_t ids = sourceIds;
            const geometry_source_id_t nextBefore = ids.next;
            brush_csg_subtract_result_t result{};

            REQUIRE( BrushCSG_TryHollow(
                         &brush, 1.0, &allocator,
                         &ids, policy, &result ) ==
                     geometry_status_t::ALLOCATION_FAILED );
            RequireCanonicalSubtractOutput( result );
            REQUIRE( ids.next.value == nextBefore.value );
            REQUIRE( state.cSuccessfulAllocations == state.cFrees );
        }
    }

    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "CSG: exhausted identity allocation never publishes output",
           "[CSG][Contract]" )
{
    CSGFixture overlap;
    overlap.init(
        Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
        Vec3d_Make( 0.5, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    SECTION( "intersection" ) {
        geometry_source_id_allocator_t ids{};
        ids.next = GEOMETRY_SOURCE_ID_INVALID;
        brush_solid_t result{};
        REQUIRE( BrushCSG_TryIntersect(
                     &overlap.brushA, &overlap.brushB,
                     &overlap.allocator, &ids, overlap.policy,
                     &result ) ==
                 geometry_status_t::INSUFFICIENT_CAPACITY );
        RequireCanonicalBrushOutput( result );
        REQUIRE_FALSE( GeometrySourceId_IsValid( ids.next ) );
    }

    SECTION( "subtraction" ) {
        geometry_source_id_allocator_t ids{};
        ids.next = GEOMETRY_SOURCE_ID_INVALID;
        brush_csg_subtract_result_t result{};
        REQUIRE( BrushCSG_TrySubtract(
                     &overlap.brushA, &overlap.brushB,
                     &overlap.allocator, &ids, overlap.policy,
                     &result ) ==
                 geometry_status_t::INSUFFICIENT_CAPACITY );
        RequireCanonicalSubtractOutput( result );
        REQUIRE_FALSE( GeometrySourceId_IsValid( ids.next ) );
    }

    SECTION( "hollow" ) {
        geometry_source_id_allocator_t ids{};
        ids.next = GEOMETRY_SOURCE_ID_INVALID;
        brush_csg_subtract_result_t result{};
        REQUIRE( BrushCSG_TryHollow(
                     &overlap.brushA, 0.25,
                     &overlap.allocator, &ids, overlap.policy,
                     &result ) ==
                 geometry_status_t::INSUFFICIENT_CAPACITY );
        RequireCanonicalSubtractOutput( result );
        REQUIRE_FALSE( GeometrySourceId_IsValid( ids.next ) );
    }
}

// ---------------------------------------------------------------------------
// Merge (union)
// ---------------------------------------------------------------------------

TEST_CASE( "CSG: merge two adjacent boxes sharing a face",
           "[Gate11][CSG]" )
{
    // Two 2×2×2 boxes sharing a face along the X axis. Their union is a
    // 4×2×2 box, which is convex — merge should succeed.
    CSGFixture f;
    f.init( Vec3d_Make( -1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make(  1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_solid_t merged{};
    REQUIRE( BrushCSG_TryMerge(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB,
                 &f.allocator, &f.idAlloc, f.policy,
                 &merged ) ==
             geometry_status_t::OK );

    // Merged result should be a 6-sided box.
    REQUIRE( BrushSolid_SideCount( &merged ) >= 6u );

    // Reconstruct boundary and validate.
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &f.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &merged, f.policy ) ==
             geometry_status_t::OK );

    // Should have 8 vertices (box).
    REQUIRE( boundary.vertices.nCount == 8u );

    // Euler: v - e + f = 2.
    const auto v = static_cast<common::i32>( boundary.vertices.nCount );
    const auto e = static_cast<common::i32>( boundary.edges.nCount );
    const auto fc = static_cast<common::i32>( boundary.faces.nCount );
    REQUIRE( ( v - e + fc ) == 2 );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &merged );
}

TEST_CASE( "CSG: merge disjoint boxes rejected as non-convex",
           "[Gate11][CSG]" )
{
    // Two boxes far apart — their union is not convex.
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make( 10.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    brush_solid_t merged{};
    REQUIRE( BrushCSG_TryMerge(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB,
                 &f.allocator, &f.idAlloc, f.policy,
                 &merged ) ==
             geometry_status_t::DEGENERATE );
}

TEST_CASE( "CSG: merge overlapping boxes rejected as non-convex",
           "[Gate11][CSG]" )
{
    // Two overlapping boxes that don't form a convex union.
    CSGFixture f;
    f.init( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ),
            Vec3d_Make( 1.0, 1.0, 0.0 ), Vec3d_Make( 2.0, 1.0, 1.0 ) );

    const geometry_source_id_t nextBefore = f.idAlloc.next;
    brush_solid_t merged{};
    REQUIRE( BrushCSG_TryMerge(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB,
                 &f.allocator, &f.idAlloc, f.policy,
                 &merged ) ==
             geometry_status_t::DEGENERATE );
    RequireCanonicalBrushOutput( merged );
    REQUIRE( f.idAlloc.next.value == nextBefore.value );
}

TEST_CASE( "CSG: merge rejects valid boundaries paired with other brushes",
           "[Gate11][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( -1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make(  1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    const geometry_source_id_t nextBefore = f.idAlloc.next;
    brush_solid_t merged{};
    REQUIRE( BrushCSG_TryMerge(
                 &f.brushA, &f.boundaryB,
                 &f.brushB, &f.boundaryA,
                 &f.allocator, &f.idAlloc, f.policy,
                 &merged ) == geometry_status_t::INVALID_ARGUMENT );
    RequireCanonicalBrushOutput( merged );
    REQUIRE( f.idAlloc.next.value == nextBefore.value );
}

TEST_CASE( "CSG: merge deterministically inherits side IDs and attributes",
           "[Gate11][CSG][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( -1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make(  1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );
    for ( common::usize i = 0u; i < f.brushA.sides.nCount; ++i ) {
        f.brushA.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 50u + i );
    }
    for ( common::usize i = 0u; i < f.brushB.sides.nCount; ++i ) {
        f.brushB.sides.pData[i].iAttributeIndex =
            static_cast<common::u32>( 60u + i );
    }

    geometry_source_id_allocator_t idsA = f.idAlloc;
    geometry_source_id_allocator_t idsB = f.idAlloc;
    const common::u64 firstFreshId = idsA.next.value;
    brush_solid_t mergedA{};
    brush_solid_t mergedB{};
    REQUIRE( BrushCSG_TryMerge(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB,
                 &f.allocator, &idsA, f.policy,
                 &mergedA ) == geometry_status_t::OK );
    REQUIRE( BrushCSG_TryMerge(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB,
                 &f.allocator, &idsB, f.policy,
                 &mergedB ) == geometry_status_t::OK );

    REQUIRE( idsA.next.value == idsB.next.value );
    REQUIRE( mergedA.sourceId.value == firstFreshId );
    REQUIRE( mergedA.sourceId.value == mergedB.sourceId.value );
    REQUIRE( mergedA.sides.nCount == mergedB.sides.nCount );
    REQUIRE( idsA.next.value ==
             firstFreshId + mergedA.sides.nCount + 1u );

    for ( common::usize iSide = 0u;
          iSide < mergedA.sides.nCount;
          ++iSide ) {
        const brush_solid_side_t &sideA = mergedA.sides.pData[iSide];
        const brush_solid_side_t &sideB = mergedB.sides.pData[iSide];
        REQUIRE( TestPlanesEquivalent( sideA.plane, sideB.plane ) );
        REQUIRE( sideA.sourceId.value == sideB.sourceId.value );
        REQUIRE( sideA.iAttributeIndex == sideB.iAttributeIndex );

        const brush_solid_side_t *pSource =
            FindSideById( f.brushA, sideA.sourceId );
        if ( pSource == nullptr ) {
            pSource = FindSideById( f.brushB, sideA.sourceId );
        }
        REQUIRE( pSource != nullptr );
        REQUIRE( sideA.iAttributeIndex == pSource->iAttributeIndex );
        REQUIRE( TestPlanesEquivalent( sideA.plane, pSource->plane ) );

        // Convex-hull plane IDs are staged deterministically, then replaced
        // by operand provenance. They are committed gaps, never output IDs.
        REQUIRE( ( sideA.sourceId.value < firstFreshId ||
                   sideA.sourceId.value >= idsA.next.value ) );
    }

    BrushSolid_Shutdown( &mergedB );
    BrushSolid_Shutdown( &mergedA );
}

TEST_CASE( "CSG: merge null args rejected",
           "[Gate11][CSG]" )
{
    brush_solid_t result{};
    REQUIRE( BrushCSG_TryMerge(
                 nullptr, nullptr, nullptr, nullptr,
                 nullptr, nullptr, geometry_policy_t{},
                 &result ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "CSG: merge is failure-atomic for every allocation",
           "[Gate11][CSG][Allocation][Contract]" )
{
    CSGFixture f;
    f.init( Vec3d_Make( -1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ),
            Vec3d_Make(  1.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    csg_failure_allocator_state_t baselineState{};
    baselineState.iFailOnCall =
        std::numeric_limits<common::usize>::max();
    common::allocator_t baselineAllocator =
        MakeCSGFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds = f.idAlloc;

    brush_solid_t baselineResult{};
    REQUIRE( BrushCSG_TryMerge(
                 &f.brushA, &f.boundaryA,
                 &f.brushB, &f.boundaryB,
                 &baselineAllocator, &baselineIds, f.policy,
                 &baselineResult ) ==
             geometry_status_t::OK );
    const common::usize cAllocationCalls =
        baselineState.cAllocationCalls;
    REQUIRE( cAllocationCalls > 0u );
    BrushSolid_Shutdown( &baselineResult );
    REQUIRE( baselineState.cSuccessfulAllocations ==
             baselineState.cFrees );

    for ( common::usize iFail = 1u;
          iFail <= cAllocationCalls;
          ++iFail ) {
        DYNAMIC_SECTION( "allocation call " << iFail ) {
            csg_failure_allocator_state_t state{};
            state.iFailOnCall = iFail;
            common::allocator_t allocator =
                MakeCSGFailureAllocator( &state );
            geometry_source_id_allocator_t ids = f.idAlloc;
            const geometry_source_id_t nextBefore = ids.next;

            brush_solid_t result{};
            REQUIRE( BrushCSG_TryMerge(
                         &f.brushA, &f.boundaryA,
                         &f.brushB, &f.boundaryB,
                         &allocator, &ids, f.policy,
                         &result ) ==
                     geometry_status_t::ALLOCATION_FAILED );

            RequireCanonicalBrushOutput( result );
            REQUIRE( ids.next.value == nextBefore.value );
            REQUIRE( state.cSuccessfulAllocations == state.cFrees );
        }
    }
}

} // namespace cypher::editor::geometry
