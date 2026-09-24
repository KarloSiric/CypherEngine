//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_IndoorWorkflow_Tests.cpp
//  Purpose: Checks architectural openings across CSG, picking, and persistence.
//  Details: Analytical box differences provide an independent occupancy and
//           volume oracle for doorway/window fragments and their cooked output.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCSG.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_CookSourceMapping.h"
#include "CypherGeometry_RaycastQueries.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {
namespace {

struct indoor_workflow_fixture_t {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{};
    brush_solid_t wall{};
    brush_solid_t cutter{};
    brush_csg_subtract_result_t difference{};
    geometry_document_t document{};
    geometry_document_t loaded{};
    geometry_cook_result_t cooked{};
    geometry_cook_result_t recooked{};
    brush_boundary_t boundary{};
    common::text_buffer_t text{};
    common::text_buffer_t rewritten{};

    ~indoor_workflow_fixture_t()
    {
        BrushBoundary_Shutdown( &boundary );
        GeometryCook_Shutdown( &recooked );
        GeometryCook_Shutdown( &cooked );
        GeometryDocument_Shutdown( &loaded );
        GeometryDocument_Shutdown( &document );
        BrushCSGSubtractResult_Shutdown( &difference );
        BrushSolid_Shutdown( &cutter );
        BrushSolid_Shutdown( &wall );
    }
};

bool ContainsPoint( const brush_solid_t &brush, math::vec3d_t point )
{
    for ( common::usize i = 0; i < brush.sides.nCount; ++i ) {
        if ( math::Planed_SignedDistance( brush.sides.pData[i].plane, point ) > 1e-7 ) {
            return false;
        }
    }
    return true;
}

void CheckOpening( bool window, math::vec3d_t offset )
{
    CAPTURE( window, offset.x, offset.y, offset.z );
    indoor_workflow_fixture_t f;
    const double sill = window ? 1.0 : 0.0;
    // Wall: x [-5, 5], y [-0.25, 0.25], z [0, 4].
    // Cutter: x [-1, 1], y [-1, 1], z [sill, 3].
    // A doorway shares the floor plane; a window retains a sill.
    REQUIRE( BrushGenerator_TryMakeBox(
        &f.wall, &f.allocator, f.policy, &f.ids,
        math::Vec3d_Add( offset, { 0, 0, 2 } ), { 5, 0.25, 2 } ) == geometry_status_t::OK );
    REQUIRE( BrushGenerator_TryMakeBox(
        &f.cutter, &f.allocator, f.policy, &f.ids,
        math::Vec3d_Add( offset, { 0, 0, (3.0 + sill) * 0.5 } ),
        { 1, 1, (3.0 - sill) * 0.5 } ) == geometry_status_t::OK );
    REQUIRE( BrushCSG_TrySubtract(
        &f.wall, &f.cutter, &f.allocator, &f.ids, f.policy,
        &f.difference ) == geometry_status_t::OK );
    REQUIRE( f.difference.cFragments > 0u );
    REQUIRE( f.difference.cFragments <= f.cutter.sides.nCount );
    REQUIRE( BrushBoundary_Init( &f.boundary, &f.allocator ) == geometry_status_t::OK );

    bool lintelHit = false;
    bool jambHit = false;
    for ( common::usize i = 0; i < f.difference.cFragments; ++i ) {
        const brush_solid_t &part = f.difference.fragments[i];
        REQUIRE( BrushBoundary_TryReconstruct( &f.boundary, &part, f.policy ) == geometry_status_t::OK );
        for ( unsigned probe = 0; probe < 3; ++probe ) {
            // The opening is clear; the lintel and right jamb remain pickable.
            const math::vec3d_t localOrigin = probe == 0 ? math::vec3d_t{ 0, -2, 2 }
                : probe == 1 ? math::vec3d_t{ 0, -2, 3.5 } : math::vec3d_t{ 3, -2, 2 };
            brush_raycast_hit_t hit{};
            REQUIRE( BrushQueries_TryRaycast(
                &part, &f.boundary,
                { math::Vec3d_Add( offset, localOrigin ), { 0, 1, 0 } },
                {}, f.policy, &hit ) == geometry_status_t::OK );
            if ( probe == 0 ) { CHECK_FALSE( hit.bHit ); }
            if ( probe == 1 ) { lintelHit = lintelHit || hit.bHit; }
            if ( probe == 2 ) { jambHit = jambHit || hit.bHit; }
            if ( hit.bHit ) {
                CHECK( hit.fDistance == Catch::Approx( 1.75 ).margin( 1e-7 ) );
            }
        }
    }
    CHECK( lintelHit );
    CHECK( jambHit );

    // Test cells away from all cutter/partition planes. Exactly one fragment
    // must own each point in the analytical difference; no overlap is allowed.
    for ( unsigned ix = 0; ix < 19; ++ix ) {
        for ( unsigned iy = 0; iy < 3; ++iy ) {
            for ( unsigned iz = 0; iz < 9; ++iz ) {
                const math::vec3d_t local{ -5.4 + ix * 0.61, -0.51 + iy * 0.47, -0.3 + iz * 0.59 };
                const bool inWall = std::abs( local.x ) < 5 && std::abs( local.y ) < 0.25 && local.z > 0 && local.z < 4;
                const bool inCut = std::abs( local.x ) < 1 && std::abs( local.y ) < 1 && local.z > sill && local.z < 3;
                unsigned owners = 0;
                for ( common::usize i = 0; i < f.difference.cFragments; ++i ) {
                    owners += ContainsPoint( f.difference.fragments[i], math::Vec3d_Add( offset, local ) );
                }
                CAPTURE( ix, iy, iz );
                REQUIRE( owners == (inWall && !inCut ? 1u : 0u) );
            }
        }
    }

    // Raw CSG fragments retain operand side IDs as provenance. They currently
    // cannot coexist in one document until a document adoption operation assigns
    // unique destination IDs and records the source mapping. Exercise each
    // fragment independently; the separate admission test protects that boundary.
    REQUIRE( common::TextBuffer_Init( &f.text, &f.allocator ) );
    REQUIRE( common::TextBuffer_Init( &f.rewritten, &f.allocator ) );
    double volume = 0.0;
    for ( common::usize iPart = 0; iPart < f.difference.cFragments; ++iPart ) {
        CAPTURE( iPart );
        REQUIRE( GeometryDocument_Init( &f.document, &f.allocator, f.policy ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrush( &f.document, &f.difference.fragments[iPart] ) == geometry_status_t::OK );
        REQUIRE( GeometryCook_Init( &f.cooked, &f.allocator ) == geometry_cook_status_t::OK );
        REQUIRE( GeometryCook_TryCook( &f.cooked, &f.document, f.policy ) == geometry_cook_status_t::OK );
        REQUIRE( f.cooked.triangles.nCount > 0u );
        for ( common::usize i = 0; i < f.cooked.triangles.nCount; ++i ) {
            const cook_triangle_t &triangle = f.cooked.triangles.pData[i];
            REQUIRE( triangle.iVertex0 < f.cooked.vertices.nCount );
            REQUIRE( triangle.iVertex1 < f.cooked.vertices.nCount );
            REQUIRE( triangle.iVertex2 < f.cooked.vertices.nCount );
            const auto a = math::Vec3d_Subtract( f.cooked.vertices.pData[triangle.iVertex0], offset );
            const auto b = math::Vec3d_Subtract( f.cooked.vertices.pData[triangle.iVertex1], offset );
            const auto c = math::Vec3d_Subtract( f.cooked.vertices.pData[triangle.iVertex2], offset );
            volume += math::Vec3d_Dot( a, math::Vec3d_Cross( b, c ) ) / 6.0;
            const brush_solid_t *source = GeometryDocument_FindBrush( &f.document, triangle.source.brushSourceId );
            REQUIRE( source != nullptr );
            REQUIRE( triangle.source.iSideIndex < source->sides.nCount );
            CHECK( source->sides.pData[triangle.source.iSideIndex].sourceId.value == triangle.source.sideSourceId.value );
            // Cook winding agrees with the authored outward side normal.
            CHECK( math::Vec3d_Dot( math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, a ) ),
                source->sides.pData[triangle.source.iSideIndex].plane.normal ) > 0.0 );
        }
        REQUIRE( GeometrySerialization_SaveToText( &f.document, &f.text ).status == geometry_serialization_status_t::OK );
        REQUIRE( GeometrySerialization_LoadFromText( common::TextBuffer_View( &f.text ), &f.allocator,
            f.policy, &f.loaded ).status == geometry_serialization_status_t::OK );
        REQUIRE( GeometrySerialization_SaveToText( &f.loaded, &f.rewritten ).status == geometry_serialization_status_t::OK );
        CHECK( common::StringView_Equals( common::TextBuffer_View( &f.text ), common::TextBuffer_View( &f.rewritten ) ) );
        REQUIRE( GeometryCook_Init( &f.recooked, &f.allocator ) == geometry_cook_status_t::OK );
        REQUIRE( GeometryCook_TryCook( &f.recooked, &f.loaded, f.policy ) == geometry_cook_status_t::OK );
        CHECK( common::ContentHash_Equals( f.cooked.contentHash, f.recooked.contentHash ) );
        CHECK( f.cooked.vertices.nCount == f.recooked.vertices.nCount );
        CHECK( f.cooked.triangles.nCount == f.recooked.triangles.nCount );
        GeometryCook_Shutdown( &f.recooked );
        GeometryCook_Shutdown( &f.cooked );
        GeometryDocument_Shutdown( &f.loaded );
        GeometryDocument_Shutdown( &f.document );
    }
    CHECK( volume == Catch::Approx( 20.0 - (3.0 - sill) ).margin( 1e-7 ) );
}

} // namespace

TEST_CASE( "Indoor: doorway remains open through CSG picking and per-fragment cook round-trip", "[Indoor][CSG][Integration]" )
{
    CheckOpening( false, { 0, 0, 0 } );
    CheckOpening( false, { 1024, -512, 256 } );
}

TEST_CASE( "Indoor: window preserves sill and nonoverlapping solid volume", "[Indoor][CSG][Integration]" )
{
    CheckOpening( true, { 0, 0, 0 } );
    CheckOpening( true, { -1024, 512, -256 } );
}

TEST_CASE( "Indoor: raw CSG provenance collisions reject document admission atomically", "[Indoor][CSG][Integration][Identity]" )
{
    indoor_workflow_fixture_t f;
    REQUIRE( BrushGenerator_TryMakeBox( &f.wall, &f.allocator, f.policy, &f.ids,
        { 0, 0, 2 }, { 5, 0.25, 2 } ) == geometry_status_t::OK );
    REQUIRE( BrushGenerator_TryMakeBox( &f.cutter, &f.allocator, f.policy, &f.ids,
        { 0, 0, 1.5 }, { 1, 1, 1.5 } ) == geometry_status_t::OK );
    REQUIRE( BrushCSG_TrySubtract( &f.wall, &f.cutter, &f.allocator, &f.ids,
        f.policy, &f.difference ) == geometry_status_t::OK );
    REQUIRE( f.difference.cFragments >= 2u );
    const auto &first = f.difference.fragments[0];
    const auto &second = f.difference.fragments[1];
    bool sharedSourceSide = false;
    for ( common::usize i = 0; i < first.sides.nCount; ++i ) {
        for ( common::usize j = 0; j < second.sides.nCount; ++j ) {
            sharedSourceSide = sharedSourceSide ||
                first.sides.pData[i].sourceId.value == second.sides.pData[j].sourceId.value;
        }
    }
    REQUIRE( sharedSourceSide );
    REQUIRE( GeometryDocument_Init( &f.document, &f.allocator, f.policy ) == geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &f.document, &first ) == geometry_status_t::OK );
    REQUIRE( common::TextBuffer_Init( &f.text, &f.allocator ) );
    REQUIRE( common::TextBuffer_Init( &f.rewritten, &f.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText( &f.document, &f.text ).status == geometry_serialization_status_t::OK );
    const auto revision = GeometryDocument_GetRevision( &f.document );
    const auto nextId = f.document.sourceIds.allocator.next;
    const bool loadRegistrationOpen = f.document.sourceIds.bLoadRegistrationOpen;
    const auto checkRegistry = [&]() {
        // This fresh document has only the first brush and its sides. Matching
        // counts plus membership proves neither live nor retired claims leaked.
        REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &f.document.sourceIds ) );
        CHECK( GeometrySourceIdRegistry_Count( &f.document.sourceIds ) == first.sides.nCount + 1u );
        CHECK( GeometrySourceIdRegistry_ClaimedCount( &f.document.sourceIds ) == first.sides.nCount + 1u );
        CHECK( GeometrySourceIdRegistry_Contains( &f.document.sourceIds, first.sourceId ) );
        for ( common::usize i = 0; i < first.sides.nCount; ++i ) {
            CHECK( GeometrySourceIdRegistry_Contains( &f.document.sourceIds, first.sides.pData[i].sourceId ) );
        }
    };
    checkRegistry();
    CHECK( GeometryDocument_TryAddBrush( &f.document, &second ) == geometry_status_t::IDENTITY_CONFLICT );
    checkRegistry();
    CHECK( f.document.sourceIds.allocator.next.value == nextId.value );
    CHECK( f.document.sourceIds.bLoadRegistrationOpen == loadRegistrationOpen );
    CHECK( GeometryDocument_BrushCount( &f.document ) == 1u );
    CHECK( GeometryDocument_GetRevision( &f.document ) == revision );
    REQUIRE( GeometrySerialization_SaveToText( &f.document, &f.rewritten ).status == geometry_serialization_status_t::OK );
    CHECK( common::StringView_Equals( common::TextBuffer_View( &f.text ), common::TextBuffer_View( &f.rewritten ) ) );
}

} // namespace cypher::editor::geometry
