//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshLoopSlide_Tests.cpp
//  Purpose: Contract tests for regular closed-loop traversal and source-level
//           proportional loop sliding.
//  Details: The fixture is an open three-ring quad tube. Its middle ring is
//           a regular closed edge loop (valence four); lower and upper rails
//           have unequal lengths so signed proportional motion is directly
//           observable. Tests cover direction symmetry, non-accumulating
//           preview, exact authored-data preservation, conservative topology
//           rejection, large loops, failure atomicity, and allocation faults.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshLoopSlide.h"
#include "CypherGeometry_MeshSourceModeling.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

geometry_source_id_t Id( common::u64 value ) noexcept
{
    return geometry_source_id_t{ value };
}

struct loop_slide_failure_allocator_state_t {
    common::usize cCalls{ 0u };
    common::usize iFailOnCall{ common::CY_USIZE_MAX };
    common::usize cRejected{ 0u };
    common::usize cSuccessful{ 0u };
    common::usize cFrees{ 0u };
};

void *LoopSlideFailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<loop_slide_failure_allocator_state_t *>( pUserData );
    const common::usize iCall = pState->cCalls++;
    if ( iCall == pState->iFailOnCall ) {
        ++pState->cRejected;
        return nullptr;
    }
    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessful; }
    return pMemory;
}

void LoopSlideFailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState =
        static_cast<loop_slide_failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) { ++pState->cFrees; }
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeLoopSlideFailureAllocator(
    loop_slide_failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        &LoopSlideFailureAllocate,
        nullptr,
        &LoopSlideFailureFree,
        pState
    };
}

struct scoped_description_t {
    mesh_source_description_t value{};

    explicit scoped_description_t(
        const common::allocator_t *pAllocator )
    {
        REQUIRE( MeshSourceDescription_Init(
                     &value,
                     pAllocator,
                     GEOMETRY_SOURCE_ID_INVALID ) ==
                 geometry_status_t::OK );
    }

    ~scoped_description_t()
    {
        MeshSourceDescription_Shutdown( &value );
    }

    void Capture( const mesh_source_t *pSource )
    {
        REQUIRE( MeshSource_TryDescribe( pSource, &value ) ==
                 geometry_status_t::OK );
    }
};

struct loop_slide_fixture_t {
    common::allocator_t allocator{};
    mesh_source_description_t description{};
    mesh_source_t source{};
    common::u32 cSegments{ 0u };

    explicit loop_slide_fixture_t(
        common::u32 segments = 4u,
        bool bTriangulateUpperSeedFace = false,
        const common::allocator_t &allocatorValue =
            *common::Allocator_GetSystem() )
        : allocator( allocatorValue ),
          cSegments( segments )
    {
        REQUIRE( segments >= 3u );
        REQUIRE( MeshSourceDescription_Init(
                     &description, &allocator, Id( 1u ) ) ==
                 geometry_status_t::OK );

        constexpr double levels[3] = { 0.0, 1.0, 3.0 };
        for ( common::u32 iRing = 0u; iRing < 3u; ++iRing ) {
            for ( common::u32 i = 0u; i < segments; ++i ) {
                const double angle =
                    2.0 * std::numbers::pi_v<double> *
                    static_cast<double>( i ) /
                    static_cast<double>( segments );
                const math::vec3d_t p = math::Vec3d_Make(
                    4.0 * std::cos( angle ),
                    4.0 * std::sin( angle ),
                    levels[iRing] );
                REQUIRE( MeshSourceDescription_TryAddVertex(
                             &description,
                             p,
                             VertexId( iRing, i ),
                             nullptr ) == geometry_status_t::OK );
            }
        }

        common::u64 nextFaceId = 10000u;
        for ( common::u32 iBand = 0u; iBand < 2u; ++iBand ) {
            for ( common::u32 i = 0u; i < segments; ++i ) {
                const common::u32 j = ( i + 1u ) % segments;
                const common::u32 a = iBand * segments + i;
                const common::u32 b = iBand * segments + j;
                const common::u32 c = ( iBand + 1u ) * segments + j;
                const common::u32 d = ( iBand + 1u ) * segments + i;
                if ( bTriangulateUpperSeedFace && iBand == 1u && i == 0u ) {
                    const common::u32 first[] = { a, b, c };
                    const common::u32 second[] = { a, c, d };
                    AddFace( first, 3u, nextFaceId++ );
                    AddFace( second, 3u, nextFaceId++ );
                } else {
                    const common::u32 quad[] = { a, b, c, d };
                    AddFace( quad, 4u, nextFaceId++ );
                }
            }
        }

        // Give every selected edge non-default surface data. A pure slide
        // must keep these and every corner/face attribute bit-identical.
        for ( common::u32 i = 0u; i < segments; ++i ) {
            const common::u32 j = ( i + 1u ) % segments;
            const common::u32 a = segments + i;
            const common::u32 b = segments + j;
            mesh_edge_attributes_t edgeAttributes{};
            edgeAttributes.flags = static_cast<common::u8>(
                MESH_EDGE_FLAG_HARD |
                ( ( i & 1u ) != 0u ? MESH_EDGE_FLAG_SEAM : 0u ) );
            REQUIRE( MeshSourceDescription_TrySetEdge(
                         &description,
                         std::min( a, b ),
                         std::max( a, b ),
                         edgeAttributes,
                         0.25 + 0.5 * static_cast<double>( i & 1u ) ) ==
                     geometry_status_t::OK );
        }

        REQUIRE( MeshSource_TryBuild(
                     &description, &allocator, &source ) ==
                 geometry_status_t::OK );
    }

    ~loop_slide_fixture_t()
    {
        MeshSource_Shutdown( &source );
        MeshSourceDescription_Shutdown( &description );
    }

    geometry_source_id_t VertexId(
        common::u32 iRing,
        common::u32 i ) const noexcept
    {
        return Id( 100u +
                   static_cast<common::u64>( iRing ) * cSegments + i );
    }

    geometry_source_id_t MiddleId( common::u32 i ) const noexcept
    {
        return VertexId( 1u, i % cSegments );
    }

    void AddFace(
        const common::u32 *pCorners,
        common::u32 cCorners,
        common::u64 faceId )
    {
        mesh_face_attributes_t faceAttributes{};
        faceAttributes.material.value = 50000u + faceId;
        faceAttributes.smoothingGroups =
            1u << static_cast<common::u32>( faceId % 8u );
        common::u32 iFace = common::CY_U32_MAX;
        REQUIRE( MeshSourceDescription_TryAddFace(
                     &description,
                     common::span_t<const common::u32>{ pCorners, cCorners },
                     Id( faceId ),
                     faceAttributes,
                     &iFace ) == geometry_status_t::OK );
        const mesh_source_face_t &face = description.faces.pData[iFace];
        for ( common::u32 k = 0u; k < cCorners; ++k ) {
            mesh_corner_attributes_t &corner =
                description.corners.pData[face.iFirstCorner + k].attributes;
            corner.uv0 = math::vec2d_t{
                static_cast<double>( faceId ),
                static_cast<double>( k ) + 0.125 };
            corner.uv1 = math::vec2d_t{
                static_cast<double>( k ) + 0.25,
                static_cast<double>( faceId ) + 0.5 };
            corner.colorRgba =
                0x10203040u + static_cast<common::u32>( faceId + k );
        }
    }

    math::vec3d_t Position( geometry_source_id_t id ) const
    {
        geometry_mesh_vertex_handle_t h{};
        REQUIRE( MeshSource_TryFindVertex( &source, id, &h ) );
        const mesh_vertex_record_t *pVertex =
            EditableMesh_GetVertex( &source.mesh, h );
        REQUIRE( pVertex != nullptr );
        return pVertex->position;
    }
};

void CheckDescriptionsDifferOnlyInPositions(
    const mesh_source_description_t &before,
    mesh_source_description_t *pAfter )
{
    REQUIRE( before.vertices.nCount == pAfter->vertices.nCount );
    for ( common::usize i = 0u; i < before.vertices.nCount; ++i ) {
        REQUIRE( before.vertices.pData[i].sourceId.value ==
                 pAfter->vertices.pData[i].sourceId.value );
        pAfter->vertices.pData[i].position =
            before.vertices.pData[i].position;
    }
    CHECK( MeshSourceDescription_Equal( &before, pAfter ) );
}

} // namespace

TEST_CASE(
    "Loop slide follows unequal rails and preserves all authored data",
    "[geometry][mesh][loop-slide]" )
{
    SECTION( "positive follows the directed seed's left rail" ) {
        loop_slide_fixture_t fixture{};
        scoped_description_t before{ &fixture.allocator };
        scoped_description_t after{ &fixture.allocator };
        before.Capture( &fixture.source );

        mesh_loop_slide_result_t result{};
        REQUIRE( MeshSourceEdit_TrySlideEdgeLoop(
                     &fixture.source,
                     fixture.MiddleId( 0u ),
                     fixture.MiddleId( 1u ),
                     0.5,
                     &result ) == geometry_status_t::OK );
        CHECK( result.bClosed );
        CHECK( result.cVerticesMoved == 4u );
        CHECK( result.cFacesUpdated == 8u );
        for ( common::u32 i = 0u; i < fixture.cSegments; ++i ) {
            CHECK( fixture.Position( fixture.MiddleId( i ) ).z ==
                   Catch::Approx( 2.0 ) );
        }
        CHECK( MeshSource_Validate(
                   &fixture.source, &fixture.allocator ).fault ==
               mesh_source_fault_t::NONE );
        after.Capture( &fixture.source );
        CheckDescriptionsDifferOnlyInPositions( before.value, &after.value );
    }

    SECTION( "negative follows the shorter right rail" ) {
        loop_slide_fixture_t fixture{};
        REQUIRE( MeshSourceEdit_TrySlideEdgeLoop(
                     &fixture.source,
                     fixture.MiddleId( 0u ),
                     fixture.MiddleId( 1u ),
                     -0.5,
                     nullptr ) == geometry_status_t::OK );
        for ( common::u32 i = 0u; i < fixture.cSegments; ++i ) {
            CHECK( fixture.Position( fixture.MiddleId( i ) ).z ==
                   Catch::Approx( 0.5 ) );
        }
    }
}

TEST_CASE(
    "Loop slide direction is symmetric under reversed seed and sign",
    "[geometry][mesh][loop-slide][determinism]" )
{
    loop_slide_fixture_t forward{};
    loop_slide_fixture_t reverse{};
    REQUIRE( MeshSourceEdit_TrySlideEdgeLoop(
                 &forward.source,
                 forward.MiddleId( 0u ),
                 forward.MiddleId( 1u ),
                 0.375,
                 nullptr ) == geometry_status_t::OK );
    REQUIRE( MeshSourceEdit_TrySlideEdgeLoop(
                 &reverse.source,
                 reverse.MiddleId( 1u ),
                 reverse.MiddleId( 0u ),
                 -0.375,
                 nullptr ) == geometry_status_t::OK );

    scoped_description_t forwardDescription{ &forward.allocator };
    scoped_description_t reverseDescription{ &reverse.allocator };
    forwardDescription.Capture( &forward.source );
    reverseDescription.Capture( &reverse.source );
    CHECK( MeshSourceDescription_Equal(
        &forwardDescription.value, &reverseDescription.value ) );
}

TEST_CASE(
    "Loop slide plan previews are absolute to one captured baseline",
    "[geometry][mesh][loop-slide][preview]" )
{
    loop_slide_fixture_t fixture{};
    mesh_loop_slide_plan_t plan{};
    REQUIRE( MeshLoopSlidePlan_Init( &plan, &fixture.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSourceLoopSlidePlan_TryBuild(
                 &plan,
                 &fixture.source,
                 fixture.MiddleId( 0u ),
                 fixture.MiddleId( 1u ) ) == geometry_status_t::OK );
    REQUIRE( plan.bClosed );
    REQUIRE( plan.samples.nCount == 4u );

    REQUIRE( MeshSourceLoopSlidePlan_TryApply(
                 &plan, &fixture.source, 0.25, nullptr ) ==
             geometry_status_t::OK );
    CHECK( fixture.Position( fixture.MiddleId( 0u ) ).z ==
           Catch::Approx( 1.5 ) );

    REQUIRE( MeshSourceLoopSlidePlan_TryApply(
                 &plan, &fixture.source, 0.5, nullptr ) ==
             geometry_status_t::OK );
    CHECK( fixture.Position( fixture.MiddleId( 0u ) ).z ==
           Catch::Approx( 2.0 ) );

    mesh_loop_slide_result_t restored{};
    REQUIRE( MeshSourceLoopSlidePlan_TryApply(
                 &plan, &fixture.source, 0.0, &restored ) ==
             geometry_status_t::OK );
    CHECK( restored.cVerticesMoved == 4u );
    CHECK( fixture.Position( fixture.MiddleId( 0u ) ).z ==
           Catch::Approx( 1.0 ) );

    mesh_loop_slide_result_t noChange{};
    REQUIRE( MeshSourceLoopSlidePlan_TryApply(
                 &plan, &fixture.source, 0.0, &noChange ) ==
             geometry_status_t::OK );
    CHECK( noChange.cVerticesMoved == 0u );
    CHECK( noChange.cFacesUpdated == 0u );
    CHECK( noChange.bClosed );
    MeshLoopSlidePlan_Shutdown( &plan );
}

TEST_CASE(
    "Loop slide rejects invalid parameters, folds, boundaries, and non-quads atomically",
    "[geometry][mesh][loop-slide][contract]" )
{
    SECTION( "numeric and range failures do not mutate" ) {
        loop_slide_fixture_t fixture{};
        scoped_description_t before{ &fixture.allocator };
        scoped_description_t after{ &fixture.allocator };
        before.Capture( &fixture.source );
        for ( const double factor : {
                  std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity(),
                  -std::numeric_limits<double>::infinity() } ) {
            CHECK( MeshSourceEdit_TrySlideEdgeLoop(
                       &fixture.source,
                       fixture.MiddleId( 0u ),
                       fixture.MiddleId( 1u ),
                       factor,
                       nullptr ) == geometry_status_t::NUMERIC_FAILURE );
        }
        for ( const double factor : { -1.0, 1.0, -2.0, 2.0 } ) {
            CHECK( MeshSourceEdit_TrySlideEdgeLoop(
                       &fixture.source,
                       fixture.MiddleId( 0u ),
                       fixture.MiddleId( 1u ),
                       factor,
                       nullptr ) == geometry_status_t::INVALID_ARGUMENT );
        }
        after.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal( &before.value, &after.value ) );
    }

    SECTION( "an invalid cached target is still rejected by the atomic move" ) {
        loop_slide_fixture_t fixture{};
        mesh_loop_slide_plan_t plan{};
        REQUIRE( MeshLoopSlidePlan_Init( &plan, &fixture.allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshSourceLoopSlidePlan_TryBuild(
                     &plan,
                     &fixture.source,
                     fixture.MiddleId( 0u ),
                     fixture.MiddleId( 1u ) ) == geometry_status_t::OK );
        scoped_description_t before{ &fixture.allocator };
        scoped_description_t after{ &fixture.allocator };
        before.Capture( &fixture.source );
        // Corrupt every cached baseline to coincide with its positive rail.
        // Applying factor zero would collapse the complete upper quad band;
        // the move layer must reject that deterministically before writing.
        for ( common::usize i = 0u; i < plan.samples.nCount; ++i ) {
            plan.samples.pData[i].baselinePosition =
                plan.samples.pData[i].positiveRailPosition;
        }
        const geometry_status_t status =
            MeshSourceLoopSlidePlan_TryApply(
                &plan,
                &fixture.source,
                0.0,
                nullptr );
        CAPTURE( static_cast<int>( status ) );
        CHECK( status == geometry_status_t::DEGENERATE );
        after.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal( &before.value, &after.value ) );

        // A rejected preview must not replace the last-published position
        // precondition. Restore the authored baseline and prove that the same
        // plan remains usable.
        for ( common::usize i = 0u; i < plan.samples.nCount; ++i ) {
            plan.samples.pData[i].baselinePosition =
                plan.targetPositions.pData[i];
        }
        CHECK( MeshSourceLoopSlidePlan_TryApply(
                   &plan, &fixture.source, 0.25, nullptr ) ==
               geometry_status_t::OK );
        MeshLoopSlidePlan_Shutdown( &plan );
    }

    SECTION( "moving a captured rail makes the plan stale" ) {
        loop_slide_fixture_t fixture{};
        mesh_loop_slide_plan_t plan{};
        REQUIRE( MeshLoopSlidePlan_Init( &plan, &fixture.allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshSourceLoopSlidePlan_TryBuild(
                     &plan,
                     &fixture.source,
                     fixture.MiddleId( 0u ),
                     fixture.MiddleId( 1u ) ) == geometry_status_t::OK );
        const geometry_source_id_t railId =
            plan.samples.pData[0u].positiveRailVertexId;
        math::vec3d_t movedRail = fixture.Position( railId );
        movedRail.z += 0.125;
        REQUIRE( MeshSourceEdit_TryMoveVertex(
                     &fixture.source, railId, movedRail ) ==
                 geometry_status_t::OK );
        scoped_description_t beforeApply{ &fixture.allocator };
        scoped_description_t afterApply{ &fixture.allocator };
        beforeApply.Capture( &fixture.source );
        CHECK( MeshSourceLoopSlidePlan_TryApply(
                   &plan, &fixture.source, 0.25, nullptr ) ==
               geometry_status_t::STALE_HANDLE );
        afterApply.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal(
            &beforeApply.value, &afterApply.value ) );
        MeshLoopSlidePlan_Shutdown( &plan );
    }

    SECTION( "moving a selected loop vertex makes the plan stale" ) {
        loop_slide_fixture_t fixture{};
        mesh_loop_slide_plan_t plan{};
        REQUIRE( MeshLoopSlidePlan_Init( &plan, &fixture.allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshSourceLoopSlidePlan_TryBuild(
                     &plan,
                     &fixture.source,
                     fixture.MiddleId( 0u ),
                     fixture.MiddleId( 1u ) ) == geometry_status_t::OK );
        const geometry_source_id_t movedId =
            plan.samples.pData[0u].vertexId;
        math::vec3d_t moved = fixture.Position( movedId );
        moved.z += 0.125;
        REQUIRE( MeshSourceEdit_TryMoveVertex(
                     &fixture.source, movedId, moved ) ==
                 geometry_status_t::OK );
        scoped_description_t beforeApply{ &fixture.allocator };
        scoped_description_t afterApply{ &fixture.allocator };
        beforeApply.Capture( &fixture.source );
        CHECK( MeshSourceLoopSlidePlan_TryApply(
                   &plan, &fixture.source, 0.25, nullptr ) ==
               geometry_status_t::STALE_HANDLE );
        afterApply.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal(
            &beforeApply.value, &afterApply.value ) );
        MeshLoopSlidePlan_Shutdown( &plan );
    }

    SECTION( "a boundary edge is outside the first slice" ) {
        loop_slide_fixture_t fixture{};
        scoped_description_t before{ &fixture.allocator };
        scoped_description_t after{ &fixture.allocator };
        before.Capture( &fixture.source );
        CHECK( MeshSourceEdit_TrySlideEdgeLoop(
                   &fixture.source,
                   fixture.VertexId( 0u, 0u ),
                   fixture.VertexId( 0u, 1u ),
                   0.25,
                   nullptr ) == geometry_status_t::UNSUPPORTED );
        after.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal( &before.value, &after.value ) );
    }

    SECTION( "a triangle incident on the seed is outside the first slice" ) {
        loop_slide_fixture_t fixture{ 4u, true };
        CHECK( MeshSourceEdit_TrySlideEdgeLoop(
                   &fixture.source,
                   fixture.MiddleId( 0u ),
                   fixture.MiddleId( 1u ),
                   0.25,
                   nullptr ) == geometry_status_t::UNSUPPORTED );
    }
}

TEST_CASE(
    "Regular loop traversal has no legacy 256-edge ceiling",
    "[geometry][mesh][loop-slide][large]" )
{
    constexpr common::u32 cSegments = 300u;
    loop_slide_fixture_t fixture{ cSegments };
    mesh_loop_slide_plan_t plan{};
    REQUIRE( MeshLoopSlidePlan_Init( &plan, &fixture.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSourceLoopSlidePlan_TryBuild(
                 &plan,
                 &fixture.source,
                 fixture.MiddleId( 0u ),
                 fixture.MiddleId( 1u ) ) == geometry_status_t::OK );
    CHECK( plan.samples.nCount == cSegments );
    CHECK( plan.moveHandles.nCount == cSegments );
    REQUIRE( MeshSourceLoopSlidePlan_TryApply(
                 &plan, &fixture.source, 0.25, nullptr ) ==
             geometry_status_t::OK );
    CHECK( fixture.Position( fixture.MiddleId( 299u ) ).z ==
           Catch::Approx( 1.5 ) );
    CHECK( MeshSource_Validate(
               &fixture.source, &fixture.allocator ).fault ==
           mesh_source_fault_t::NONE );
    MeshLoopSlidePlan_Shutdown( &plan );
}

TEST_CASE(
    "Every loop-slide allocation failure is leak-free and leaves the source exact",
    "[geometry][mesh][loop-slide][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        loop_slide_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeLoopSlideFailureAllocator( &state );
        {
            loop_slide_fixture_t fixture{ 4u, false, allocator };
            const common::usize iBegin = state.cCalls;
            REQUIRE( MeshSourceEdit_TrySlideEdgeLoop(
                         &fixture.source,
                         fixture.MiddleId( 0u ),
                         fixture.MiddleId( 1u ),
                         0.25,
                         nullptr ) == geometry_status_t::OK );
            cOperationAllocations = state.cCalls - iBegin;
            REQUIRE( cOperationAllocations > 0u );
        }
        REQUIRE( state.cSuccessful == state.cFrees );
    }

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        loop_slide_failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeLoopSlideFailureAllocator( &state );
        {
            loop_slide_fixture_t fixture{ 4u, false, allocator };
            scoped_description_t before{ &fixture.allocator };
            scoped_description_t after{ &fixture.allocator };
            before.Capture( &fixture.source );
            state.iFailOnCall = state.cCalls + iFailure;
            CHECK( MeshSourceEdit_TrySlideEdgeLoop(
                       &fixture.source,
                       fixture.MiddleId( 0u ),
                       fixture.MiddleId( 1u ),
                       0.25,
                       nullptr ) == geometry_status_t::ALLOCATION_FAILED );
            CHECK( state.cRejected == 1u );
            state.iFailOnCall = common::CY_USIZE_MAX;
            after.Capture( &fixture.source );
            CHECK( MeshSourceDescription_Equal(
                &before.value, &after.value ) );
        }
        CHECK( state.cSuccessful == state.cFrees );
    }
}

TEST_CASE(
    "A failed loop-slide rebuild preserves the prior usable plan",
    "[geometry][mesh][loop-slide][allocation][contract]" )
{
    loop_slide_failure_allocator_state_t state{};
    const common::allocator_t allocator =
        MakeLoopSlideFailureAllocator( &state );
    {
        loop_slide_fixture_t fixture{ 4u, false, allocator };
        mesh_loop_slide_plan_t plan{};
        REQUIRE( MeshLoopSlidePlan_Init( &plan, &fixture.allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshSourceLoopSlidePlan_TryBuild(
                     &plan,
                     &fixture.source,
                     fixture.MiddleId( 0u ),
                     fixture.MiddleId( 1u ) ) == geometry_status_t::OK );
        REQUIRE( plan.samples.nCount == 4u );
        const geometry_source_id_t oldSeedA = plan.directedSeedA;
        const geometry_source_id_t oldSeedB = plan.directedSeedB;
        const geometry_source_id_t oldFirstVertex =
            plan.samples.pData[0u].vertexId;

        // The traversal's first dynamically sized slot mask allocation.
        state.iFailOnCall = state.cCalls;
        CHECK( MeshSourceLoopSlidePlan_TryBuild(
                   &plan,
                   &fixture.source,
                   fixture.MiddleId( 1u ),
                   fixture.MiddleId( 0u ) ) ==
               geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cRejected == 1u );
        CHECK( plan.directedSeedA.value == oldSeedA.value );
        CHECK( plan.directedSeedB.value == oldSeedB.value );
        REQUIRE( plan.samples.nCount == 4u );
        CHECK( plan.samples.pData[0u].vertexId.value ==
               oldFirstVertex.value );

        state.iFailOnCall = common::CY_USIZE_MAX;
        REQUIRE( MeshSourceLoopSlidePlan_TryApply(
                     &plan, &fixture.source, 0.25, nullptr ) ==
                 geometry_status_t::OK );
        CHECK( fixture.Position( fixture.MiddleId( 0u ) ).z ==
               Catch::Approx( 1.5 ) );
        MeshLoopSlidePlan_Shutdown( &plan );
    }
    CHECK( state.cSuccessful == state.cFrees );
}

} // namespace cypher::editor::geometry
