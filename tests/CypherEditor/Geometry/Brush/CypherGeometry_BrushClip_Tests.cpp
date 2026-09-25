//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushClip_Tests.cpp
//  Purpose: Verifies canonical, identity-safe, failure-atomic brush clipping.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushClip.h"
#include "CypherGeometry_BrushGenerator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace cypher::editor::geometry
{

using Catch::Approx;
using cypher::math::Planed_Make;
using cypher::math::Vec3d_Make;

namespace
{

constexpr common::usize cSnapshotSides = 16u;

struct clip_fixture_t {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};

    clip_fixture_t()
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush,
                     &allocator,
                     policy,
                     &ids,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) ==
                 geometry_status_t::OK );
    }

    ~clip_fixture_t()
    {
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

struct brush_snapshot_t {
    brush_solid_side_t *pData{ nullptr };
    const common::allocator_t *pAllocator{ nullptr };
    common::usize cSides{ 0u };
    common::usize cCapacity{ 0u };
    geometry_source_id_t brushId{};
    std::array<brush_solid_side_t, cSnapshotSides> sides{};
};

brush_snapshot_t SnapshotBrush( const brush_solid_t &brush )
{
    REQUIRE( brush.sides.nCount <= cSnapshotSides );
    brush_snapshot_t snapshot{};
    snapshot.pData = brush.sides.pData;
    snapshot.pAllocator = brush.sides.pAllocator;
    snapshot.cSides = brush.sides.nCount;
    snapshot.cCapacity = brush.sides.nCapacity;
    snapshot.brushId = brush.sourceId;
    for ( common::usize i = 0u; i < brush.sides.nCount; ++i ) {
        snapshot.sides[i] = brush.sides.pData[i];
    }
    return snapshot;
}

void CheckBrushEqualsSnapshot(
    const brush_solid_t &brush,
    const brush_snapshot_t &snapshot )
{
    CHECK( brush.sides.pData == snapshot.pData );
    CHECK( brush.sides.pAllocator == snapshot.pAllocator );
    CHECK( brush.sides.nCount == snapshot.cSides );
    CHECK( brush.sides.nCapacity == snapshot.cCapacity );
    CHECK( brush.sourceId.value == snapshot.brushId.value );
    REQUIRE( brush.sides.nCount == snapshot.cSides );
    for ( common::usize i = 0u; i < brush.sides.nCount; ++i ) {
        const brush_solid_side_t &actual = brush.sides.pData[i];
        const brush_solid_side_t &expected = snapshot.sides[i];
        CHECK( actual.plane.normal.x == expected.plane.normal.x );
        CHECK( actual.plane.normal.y == expected.plane.normal.y );
        CHECK( actual.plane.normal.z == expected.plane.normal.z );
        CHECK( actual.plane.d == expected.plane.d );
        CHECK( actual.sourceId.value == expected.sourceId.value );
        CHECK( actual.iAttributeIndex == expected.iAttributeIndex );
    }
}

bool BrushIsCanonicalEmpty( const brush_solid_t &brush ) noexcept
{
    return brush.sides.pData == nullptr &&
           brush.sides.nCount == 0u &&
           brush.sides.nCapacity == 0u &&
           brush.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( brush.sourceId );
}

bool BrushContainsId(
    const brush_solid_t &brush,
    geometry_source_id_t id ) noexcept
{
    if ( brush.sourceId.value == id.value ) {
        return true;
    }
    for ( common::usize iSide = 0u;
          iSide < brush.sides.nCount;
          ++iSide ) {
        if ( brush.sides.pData[iSide].sourceId.value == id.value ) {
            return true;
        }
    }
    return false;
}

void CheckStrictBoundary(
    const brush_solid_t &brush,
    const geometry_policy_t &policy )
{
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init(
                 &boundary, brush.sides.pAllocator ) ==
             geometry_status_t::OK );
    CHECK( BrushBoundary_TryReconstruct(
               &boundary, &brush, policy ) ==
           geometry_status_t::OK );
    BrushBoundary_Shutdown( &boundary );
}

struct failure_allocator_state_t {
    common::usize cCalls{ 0u };
    common::usize iFailOnCall{ common::CY_USIZE_MAX };
    common::usize cSuccessful{ 0u };
    common::usize cFrees{ 0u };
};

void *FailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    ++pState->cCalls;
    if ( pState->cCalls == pState->iFailOnCall ) {
        return nullptr;
    }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    void *pMemory = pSystem->pfnAllocate(
        pSystem->pUserData, cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessful;
    }
    return pMemory;
}

void FailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }
    const common::allocator_t *pSystem = common::Allocator_GetSystem();
    pSystem->pfnFree(
        pSystem->pUserData, pMemory, cbSize, nAlignment );
}

common::allocator_t MakeFailureAllocator(
    failure_allocator_state_t *pState ) noexcept
{
    return {
        &FailureAllocate,
        nullptr,
        &FailureFree,
        pState
    };
}

math::planed_t IntersectingXPlane() noexcept
{
    return Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), 0.0 );
}

math::planed_t AllFrontXPlane() noexcept
{
    return Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), 5.0 );
}

math::planed_t AllBackXPlane() noexcept
{
    return Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -5.0 );
}

} // namespace

TEST_CASE( "Clip classification covers every geometric relation",
           "[Gate5][Clip][Classification]" )
{
    clip_fixture_t fixture;

    CHECK( BrushClip_Classify(
               &fixture.boundary,
               IntersectingXPlane(),
               fixture.policy.numerical ) ==
           brush_clip_classification_t::INTERSECTS );
    CHECK( BrushClip_Classify(
               &fixture.boundary,
               AllFrontXPlane(),
               fixture.policy.numerical ) ==
           brush_clip_classification_t::ALL_FRONT );
    CHECK( BrushClip_Classify(
               &fixture.boundary,
               AllBackXPlane(),
               fixture.policy.numerical ) ==
           brush_clip_classification_t::ALL_BACK );

    for ( common::usize iVertex = 0u;
          iVertex < fixture.boundary.vertices.nCount;
          ++iVertex ) {
        fixture.boundary.vertices.pData[iVertex].x = 0.0;
    }
    CHECK( BrushClip_Classify(
               &fixture.boundary,
               IntersectingXPlane(),
               fixture.policy.numerical ) ==
           brush_clip_classification_t::ON_PLANE );
}

TEST_CASE( "Clip classification treats tangent volume as retained",
           "[Gate5][Clip][Classification]" )
{
    clip_fixture_t fixture;
    const math::planed_t tangent =
        Planed_Make( Vec3d_Make( 1.0, 0.0, 0.0 ), -1.0 );
    CHECK( BrushClip_Classify(
               &fixture.boundary,
               tangent,
               fixture.policy.numerical ) ==
           brush_clip_classification_t::ALL_BACK );
}

TEST_CASE( "Clip classification rejects malformed numeric input",
           "[Gate5][Clip][Classification][Contract]" )
{
    clip_fixture_t fixture;
    CHECK( BrushClip_Classify(
               nullptr,
               IntersectingXPlane(),
               fixture.policy.numerical ) ==
           brush_clip_classification_t::INVALID );

    const math::planed_t nonFinite = Planed_Make(
        Vec3d_Make(
            std::numeric_limits<common::f64>::quiet_NaN(),
            0.0,
            0.0 ),
        0.0 );
    CHECK( BrushClip_Classify(
               &fixture.boundary,
               nonFinite,
               fixture.policy.numerical ) ==
           brush_clip_classification_t::INVALID );

    const math::planed_t nonUnit =
        Planed_Make( Vec3d_Make( 2.0, 0.0, 0.0 ), 0.0 );
    CHECK( BrushClip_Classify(
               &fixture.boundary,
               nonUnit,
               fixture.policy.numerical ) ==
           brush_clip_classification_t::INVALID );

    geometry_numerical_policy_t invalidPolicy = fixture.policy.numerical;
    invalidPolicy.fCoplanarDistanceTolerance = -1.0;
    CHECK( BrushClip_Classify(
               &fixture.boundary,
               IntersectingXPlane(),
               invalidPolicy ) ==
           brush_clip_classification_t::INVALID );

    fixture.boundary.vertices.pData[0].x =
        std::numeric_limits<common::f64>::infinity();
    CHECK( BrushClip_Classify(
               &fixture.boundary,
               IntersectingXPlane(),
               fixture.policy.numerical ) ==
           brush_clip_classification_t::INVALID );
}

TEST_CASE( "Clip replaces a hidden side and publishes a strict brush",
           "[Gate5][Clip]" )
{
    clip_fixture_t fixture;
    for ( common::usize iSide = 0u;
          iSide < fixture.brush.sides.nCount;
          ++iSide ) {
        fixture.brush.sides.pData[iSide].iAttributeIndex =
            static_cast<common::u32>( 100u + iSide );
    }
    const brush_snapshot_t before = SnapshotBrush( fixture.brush );
    const geometry_source_id_t expectedClipId = fixture.ids.next;
    const geometry_source_id_t brushId = fixture.brush.sourceId;

    REQUIRE( BrushClip_TryClip(
                 &fixture.brush,
                 IntersectingXPlane(),
                 &fixture.ids,
                 fixture.policy ) ==
             geometry_status_t::OK );

    CHECK( fixture.brush.sourceId.value == brushId.value );
    CHECK( fixture.brush.sides.nCount == 6u );
    CHECK( BrushContainsId( fixture.brush, expectedClipId ) );
    CHECK( fixture.ids.next.value == expectedClipId.value + 1u );

    common::usize cRetainedOldSides = 0u;
    for ( common::usize iSide = 0u;
          iSide < fixture.brush.sides.nCount;
          ++iSide ) {
        const brush_solid_side_t &actual = fixture.brush.sides.pData[iSide];
        if ( actual.sourceId.value == expectedClipId.value ) {
            CHECK( actual.iAttributeIndex == 0u );
            continue;
        }
        bool bFound = false;
        for ( common::usize iOld = 0u;
              iOld < before.cSides;
              ++iOld ) {
            const brush_solid_side_t &old = before.sides[iOld];
            if ( old.sourceId.value != actual.sourceId.value ) {
                continue;
            }
            bFound = true;
            ++cRetainedOldSides;
            CHECK( actual.iAttributeIndex == old.iAttributeIndex );
            CHECK( actual.plane.normal.x == old.plane.normal.x );
            CHECK( actual.plane.normal.y == old.plane.normal.y );
            CHECK( actual.plane.normal.z == old.plane.normal.z );
            CHECK( actual.plane.d == old.plane.d );
            break;
        }
        CHECK( bFound );
    }
    CHECK( cRetainedOldSides == 5u );
    CheckStrictBoundary( fixture.brush, fixture.policy );

    REQUIRE( BrushBoundary_TryReconstruct(
                 &fixture.boundary,
                 &fixture.brush,
                 fixture.policy ) ==
             geometry_status_t::OK );
    REQUIRE( fixture.boundary.vertices.nCount == 8u );
    for ( common::usize i = 0u;
          i < fixture.boundary.vertices.nCount;
          ++i ) {
        CHECK( fixture.boundary.vertices.pData[i].x <=
               Approx( 0.0 ).margin( 1.0e-7 ) );
    }
}

TEST_CASE( "Clip retains all old faces when cutting through a corner fan",
           "[Gate5][Clip]" )
{
    clip_fixture_t fixture;
    const common::f64 component = 1.0 / std::sqrt( 3.0 );
    const math::planed_t diagonal = Planed_Make(
        Vec3d_Make( component, component, component ), 0.0 );

    REQUIRE( BrushClip_TryClip(
                 &fixture.brush,
                 diagonal,
                 &fixture.ids,
                 fixture.policy ) ==
             geometry_status_t::OK );
    CHECK( fixture.brush.sides.nCount == 7u );
    CheckStrictBoundary( fixture.brush, fixture.policy );
}

TEST_CASE( "Clip no-op is exact and consumes no identity",
           "[Gate5][Clip][Contract]" )
{
    clip_fixture_t fixture;
    const brush_snapshot_t before = SnapshotBrush( fixture.brush );
    fixture.ids.next = GEOMETRY_SOURCE_ID_INVALID;
    const geometry_source_id_allocator_t idsBefore = fixture.ids;

    CHECK( BrushClip_TryClip(
               &fixture.brush,
               AllBackXPlane(),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::OK );
    CheckBrushEqualsSnapshot( fixture.brush, before );
    CHECK( fixture.ids.next.value == idsBefore.next.value );
}

TEST_CASE( "Clip destructive result is rejected atomically",
           "[Gate5][Clip][Contract]" )
{
    clip_fixture_t fixture;
    const brush_snapshot_t before = SnapshotBrush( fixture.brush );
    const geometry_source_id_allocator_t idsBefore = fixture.ids;

    CHECK( BrushClip_TryClip(
               &fixture.brush,
               AllFrontXPlane(),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::DEGENERATE );
    CheckBrushEqualsSnapshot( fixture.brush, before );
    CHECK( fixture.ids.next.value == idsBefore.next.value );
}

TEST_CASE( "Clip validates planes and full policy before mutation",
           "[Gate5][Clip][Contract]" )
{
    clip_fixture_t fixture;
    const brush_snapshot_t before = SnapshotBrush( fixture.brush );
    const geometry_source_id_allocator_t idsBefore = fixture.ids;

    const math::planed_t nonFinite = Planed_Make(
        Vec3d_Make(
            std::numeric_limits<common::f64>::quiet_NaN(),
            0.0,
            0.0 ),
        0.0 );
    CHECK( BrushClip_TryClip(
               &fixture.brush,
               nonFinite,
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::NUMERIC_FAILURE );

    CHECK( BrushClip_TryClip(
               &fixture.brush,
               Planed_Make( Vec3d_Make( 2.0, 0.0, 0.0 ), 0.0 ),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::DEGENERATE );

    CHECK( BrushClip_TryClip(
               &fixture.brush,
               Planed_Make(
                   Vec3d_Make( 1.0, 0.0, 0.0 ),
                   fixture.policy.numerical.fCoordinateMagnitudeLimit + 1.0 ),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::LIMIT_EXCEEDED );

    geometry_policy_t invalidPolicy = fixture.policy;
    invalidPolicy.numerical.fUnitNormalTolerance = 0.0;
    CHECK( BrushClip_TryClip(
               &fixture.brush,
               IntersectingXPlane(),
               &fixture.ids,
               invalidPolicy ) ==
           geometry_status_t::INVALID_ARGUMENT );

    CheckBrushEqualsSnapshot( fixture.brush, before );
    CHECK( fixture.ids.next.value == idsBefore.next.value );
}

TEST_CASE( "Clip rejects invalid brush state and identity collisions",
           "[Gate5][Clip][Contract][Identity]" )
{
    clip_fixture_t fixture;
    brush_solid_t empty{};
    CHECK( BrushClip_TryClip(
               &empty,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::NOT_INITIALIZED );
    CHECK( BrushClip_TryClip(
               nullptr,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::INVALID_ARGUMENT );

    const brush_snapshot_t before = SnapshotBrush( fixture.brush );
    fixture.ids.next = fixture.brush.sourceId;
    const geometry_source_id_allocator_t idsBefore = fixture.ids;
    CHECK( BrushClip_TryClip(
               &fixture.brush,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::IDENTITY_CONFLICT );
    CheckBrushEqualsSnapshot( fixture.brush, before );
    CHECK( fixture.ids.next.value == idsBefore.next.value );
}

TEST_CASE( "Clip rejects duplicate input identities before allocation",
           "[Gate5][Clip][Contract][Identity]" )
{
    clip_fixture_t fixture;
    fixture.brush.sides.pData[1].sourceId =
        fixture.brush.sides.pData[0].sourceId;
    const brush_snapshot_t before = SnapshotBrush( fixture.brush );
    const geometry_source_id_allocator_t idsBefore = fixture.ids;

    CHECK( BrushClip_TryClip(
               &fixture.brush,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::IDENTITY_CONFLICT );
    CheckBrushEqualsSnapshot( fixture.brush, before );
    CHECK( fixture.ids.next.value == idsBefore.next.value );
}

TEST_CASE( "Clip ID exhaustion is failure atomic",
           "[Gate5][Clip][Contract][Identity]" )
{
    clip_fixture_t fixture;
    fixture.ids.next = GEOMETRY_SOURCE_ID_INVALID;
    const brush_snapshot_t before = SnapshotBrush( fixture.brush );

    CHECK( BrushClip_TryClip(
               &fixture.brush,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CheckBrushEqualsSnapshot( fixture.brush, before );
    CHECK( fixture.ids.next.value == 0u );
}

TEST_CASE( "Clip is failure atomic for every allocation",
           "[Gate5][Clip][Allocation][Contract]" )
{
    const geometry_policy_t policy{};

    failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds{};
    brush_solid_t baselineBrush{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &baselineBrush,
                 &baselineAllocator,
                 policy,
                 &baselineIds,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    const common::usize cCallsBefore = baselineState.cCalls;
    REQUIRE( BrushClip_TryClip(
                 &baselineBrush,
                 IntersectingXPlane(),
                 &baselineIds,
                 policy ) ==
             geometry_status_t::OK );
    const common::usize cOperationCalls =
        baselineState.cCalls - cCallsBefore;
    REQUIRE( cOperationCalls > 0u );
    BrushSolid_Shutdown( &baselineBrush );
    REQUIRE( baselineState.cSuccessful == baselineState.cFrees );

    for ( common::usize iFailure = 1u;
          iFailure <= cOperationCalls;
          ++iFailure ) {
        DYNAMIC_SECTION( "allocation " << iFailure ) {
            failure_allocator_state_t state{};
            common::allocator_t allocator = MakeFailureAllocator( &state );
            geometry_source_id_allocator_t ids{};
            brush_solid_t brush{};
            REQUIRE( BrushGenerator_TryMakeBox(
                         &brush,
                         &allocator,
                         policy,
                         &ids,
                         Vec3d_Make( 0.0, 0.0, 0.0 ),
                         Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                     geometry_status_t::OK );

            const brush_snapshot_t before = SnapshotBrush( brush );
            const geometry_source_id_allocator_t idsBefore = ids;
            const common::usize cOutstandingBefore =
                state.cSuccessful - state.cFrees;
            state.iFailOnCall = state.cCalls + iFailure;

            CHECK( BrushClip_TryClip(
                       &brush,
                       IntersectingXPlane(),
                       &ids,
                       policy ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            CheckBrushEqualsSnapshot( brush, before );
            CHECK( ids.next.value == idsBefore.next.value );
            CHECK( state.cSuccessful - state.cFrees ==
                   cOutstandingBefore );

            BrushSolid_Shutdown( &brush );
            CHECK( state.cSuccessful == state.cFrees );
        }
    }
}

TEST_CASE( "Slice produces two strict volumetric halves",
           "[Gate5][Clip][Slice]" )
{
    clip_fixture_t fixture;
    const brush_snapshot_t sourceBefore = SnapshotBrush( fixture.brush );
    brush_solid_t back{};
    brush_solid_t front{};

    REQUIRE( BrushClip_TrySlice(
                 &fixture.brush,
                 &fixture.allocator,
                 IntersectingXPlane(),
                 &fixture.ids,
                 fixture.policy,
                 &back,
                 &front ) ==
             geometry_status_t::OK );

    CheckBrushEqualsSnapshot( fixture.brush, sourceBefore );
    CHECK( back.sides.nCount == 6u );
    CHECK( front.sides.nCount == 6u );
    CheckStrictBoundary( back, fixture.policy );
    CheckStrictBoundary( front, fixture.policy );

    brush_boundary_t backBoundary{};
    brush_boundary_t frontBoundary{};
    REQUIRE( BrushBoundary_Init( &backBoundary, &fixture.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &frontBoundary, &fixture.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &backBoundary, &back, fixture.policy ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &frontBoundary, &front, fixture.policy ) ==
             geometry_status_t::OK );
    for ( common::usize i = 0u;
          i < backBoundary.vertices.nCount;
          ++i ) {
        CHECK( backBoundary.vertices.pData[i].x <=
               Approx( 0.0 ).margin( 1.0e-7 ) );
    }
    for ( common::usize i = 0u;
          i < frontBoundary.vertices.nCount;
          ++i ) {
        CHECK( frontBoundary.vertices.pData[i].x >=
               Approx( 0.0 ).margin( 1.0e-7 ) );
    }

    BrushBoundary_Shutdown( &frontBoundary );
    BrushBoundary_Shutdown( &backBoundary );
    BrushSolid_Shutdown( &front );
    BrushSolid_Shutdown( &back );
}

TEST_CASE( "Slice remaps every published identity",
           "[Gate5][Clip][Slice][Identity]" )
{
    clip_fixture_t fixture;
    brush_solid_t back{};
    brush_solid_t front{};
    REQUIRE( BrushClip_TrySlice(
                 &fixture.brush,
                 &fixture.allocator,
                 IntersectingXPlane(),
                 &fixture.ids,
                 fixture.policy,
                 &back,
                 &front ) ==
             geometry_status_t::OK );

    CHECK_FALSE( BrushContainsId( fixture.brush, back.sourceId ) );
    CHECK_FALSE( BrushContainsId( fixture.brush, front.sourceId ) );
    CHECK_FALSE( BrushContainsId( back, front.sourceId ) );
    for ( common::usize iBack = 0u;
          iBack < back.sides.nCount;
          ++iBack ) {
        const geometry_source_id_t id = back.sides.pData[iBack].sourceId;
        CHECK_FALSE( BrushContainsId( fixture.brush, id ) );
        CHECK_FALSE( BrushContainsId( front, id ) );
        CHECK( id.value != back.sourceId.value );
    }
    for ( common::usize iFront = 0u;
          iFront < front.sides.nCount;
          ++iFront ) {
        const geometry_source_id_t id = front.sides.pData[iFront].sourceId;
        CHECK_FALSE( BrushContainsId( fixture.brush, id ) );
        CHECK( id.value != front.sourceId.value );
    }

    BrushSolid_Shutdown( &front );
    BrushSolid_Shutdown( &back );
}

TEST_CASE( "Slice rejects one-sided cuts without publishing",
           "[Gate5][Clip][Slice][Contract]" )
{
    clip_fixture_t fixture;
    brush_solid_t back{};
    brush_solid_t front{};
    const geometry_source_id_allocator_t idsBefore = fixture.ids;

    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               AllBackXPlane(),
               &fixture.ids,
               fixture.policy,
               &back,
               &front ) ==
           geometry_status_t::DEGENERATE );
    CHECK( BrushIsCanonicalEmpty( back ) );
    CHECK( BrushIsCanonicalEmpty( front ) );
    CHECK( fixture.ids.next.value == idsBefore.next.value );

    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               AllFrontXPlane(),
               &fixture.ids,
               fixture.policy,
               &back,
               &front ) ==
           geometry_status_t::DEGENERATE );
    CHECK( BrushIsCanonicalEmpty( back ) );
    CHECK( BrushIsCanonicalEmpty( front ) );
    CHECK( fixture.ids.next.value == idsBefore.next.value );
}

TEST_CASE( "Slice enforces canonical distinct destinations",
           "[Gate5][Clip][Slice][Contract]" )
{
    clip_fixture_t fixture;
    brush_solid_t back{};
    brush_solid_t front{};

    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy,
               &back,
               &back ) ==
           geometry_status_t::INVALID_ARGUMENT );

    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy,
               const_cast<brush_solid_t *>( &fixture.brush ),
               &front ) ==
           geometry_status_t::INVALID_ARGUMENT );

    REQUIRE( BrushSolid_Init(
                 &back,
                 &fixture.allocator,
                 geometry_source_id_t{ 10'000u } ) ==
             geometry_status_t::OK );
    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy,
               &back,
               &front ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    CHECK( BrushIsCanonicalEmpty( front ) );
    BrushSolid_Shutdown( &back );

    back.sourceId = geometry_source_id_t{ 42u };
    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy,
               &back,
               &front ) ==
           geometry_status_t::CORRUPT_STATE );
    back.sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

TEST_CASE( "Slice ID exhaustion leaves outputs and allocator untouched",
           "[Gate5][Clip][Slice][Identity][Contract]" )
{
    clip_fixture_t fixture;
    fixture.ids.next = geometry_source_id_t{ common::CY_U64_MAX - 2u };
    const geometry_source_id_allocator_t idsBefore = fixture.ids;
    brush_solid_t back{};
    brush_solid_t front{};

    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               IntersectingXPlane(),
               &fixture.ids,
               fixture.policy,
               &back,
               &front ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( BrushIsCanonicalEmpty( back ) );
    CHECK( BrushIsCanonicalEmpty( front ) );
    CHECK( fixture.ids.next.value == idsBefore.next.value );
}

TEST_CASE( "Slice validates numeric inputs without publication",
           "[Gate5][Clip][Slice][Contract]" )
{
    clip_fixture_t fixture;
    brush_solid_t back{};
    brush_solid_t front{};
    const geometry_source_id_allocator_t idsBefore = fixture.ids;

    const math::planed_t badPlane = Planed_Make(
        Vec3d_Make( 0.0, 0.0, 0.0 ), 0.0 );
    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               badPlane,
               &fixture.ids,
               fixture.policy,
               &back,
               &front ) ==
           geometry_status_t::DEGENERATE );

    geometry_policy_t invalidPolicy = fixture.policy;
    invalidPolicy.limits.cBrushSidesPerBrushMax = 0u;
    CHECK( BrushClip_TrySlice(
               &fixture.brush,
               &fixture.allocator,
               IntersectingXPlane(),
               &fixture.ids,
               invalidPolicy,
               &back,
               &front ) ==
           geometry_status_t::INVALID_ARGUMENT );

    CHECK( BrushIsCanonicalEmpty( back ) );
    CHECK( BrushIsCanonicalEmpty( front ) );
    CHECK( fixture.ids.next.value == idsBefore.next.value );
}

TEST_CASE( "Slice is failure atomic for every allocation",
           "[Gate5][Clip][Slice][Allocation][Contract]" )
{
    clip_fixture_t fixture;

    failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeFailureAllocator( &baselineState );
    geometry_source_id_allocator_t baselineIds = fixture.ids;
    brush_solid_t baselineBack{};
    brush_solid_t baselineFront{};
    REQUIRE( BrushClip_TrySlice(
                 &fixture.brush,
                 &baselineAllocator,
                 IntersectingXPlane(),
                 &baselineIds,
                 fixture.policy,
                 &baselineBack,
                 &baselineFront ) ==
             geometry_status_t::OK );
    const common::usize cOperationCalls = baselineState.cCalls;
    REQUIRE( cOperationCalls > 0u );
    BrushSolid_Shutdown( &baselineFront );
    BrushSolid_Shutdown( &baselineBack );
    REQUIRE( baselineState.cSuccessful == baselineState.cFrees );

    for ( common::usize iFailure = 1u;
          iFailure <= cOperationCalls;
          ++iFailure ) {
        DYNAMIC_SECTION( "allocation " << iFailure ) {
            failure_allocator_state_t state{};
            state.iFailOnCall = iFailure;
            common::allocator_t allocator = MakeFailureAllocator( &state );
            geometry_source_id_allocator_t ids = fixture.ids;
            const geometry_source_id_allocator_t idsBefore = ids;
            brush_solid_t back{};
            brush_solid_t front{};

            CHECK( BrushClip_TrySlice(
                       &fixture.brush,
                       &allocator,
                       IntersectingXPlane(),
                       &ids,
                       fixture.policy,
                       &back,
                       &front ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK( BrushIsCanonicalEmpty( back ) );
            CHECK( BrushIsCanonicalEmpty( front ) );
            CHECK( ids.next.value == idsBefore.next.value );
            CHECK( state.cSuccessful == state.cFrees );
        }
    }
}

} // namespace cypher::editor::geometry
