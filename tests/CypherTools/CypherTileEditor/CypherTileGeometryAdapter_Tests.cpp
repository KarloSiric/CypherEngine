//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileGeometryAdapter_Tests.cpp
//  Purpose: Verifies tile map box → brush solid conversion.
//  Details: Covers Gate 7 tile adapter acceptance: a tile-derived brush
//           can render (cook), pick (source mapping), serialize, load,
//           and re-cook without changing semantics.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileGeometryAdapter.h"
#include "CypherGeometry_CookSourceMapping.h"
#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_BrushBoundary.h"

#include "CypherTileMapGeometry.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using Catch::Approx;

namespace {

// Sets up a single tile map box and the geometry document to receive it.
struct TileAdapterFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    geometry_document_t document{};
    tools::tile_editor::tile_map_geometry_t tileGeom{};

    TileAdapterFixture()
    {
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        // Init the tile geometry's boxes vector directly to avoid
        // linking the full TileMapCore library at test time.
        REQUIRE( common::Vector_Init(
                     &tileGeom.boxes, &allocator ) );
        tileGeom.pAllocator = &allocator;
    }

    ~TileAdapterFixture()
    {
        common::Vector_Shutdown( &tileGeom.boxes );
        GeometryDocument_Shutdown( &document );
    }

    // Adds a floor box at the given grid position.
    void AddTileBox(
        float cx, float cy, float cz,
        float hx, float hy, float hz )
    {
        tools::tile_editor::tile_map_geometry_box_t box{};
        box.centerX = cx;
        box.centerY = cy;
        box.centerZ = cz;
        box.halfExtentX = hx;
        box.halfExtentY = hy;
        box.halfExtentZ = hz;
        box.kind =
            tools::tile_editor::tile_map_geometry_box_kind_t::FLOOR;
        REQUIRE( common::Vector_PushBack(
                     &tileGeom.boxes, box ) );
    }

    tile_adapter_result_t Convert()
    {
        return GeometryTileMapAdapter_ConvertBoxes(
            &tileGeom, &idAlloc, policy, &document );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Basic conversion
// ---------------------------------------------------------------------------

TEST_CASE( "TileAdapter: single box converts to one 6-plane brush",
           "[Gate7][TileAdapter]" )
{
    TileAdapterFixture f;
    f.AddTileBox( 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f );

    const tile_adapter_result_t result = f.Convert();
    REQUIRE( result.status == tile_adapter_status_t::OK );
    REQUIRE( result.cBrushesAdded == 1u );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 1u );

    const brush_solid_t *pBrush = f.document.brushes.pData[0];
    REQUIRE( pBrush != nullptr );
    REQUIRE( BrushSolid_SideCount( pBrush ) == 6u );
}

TEST_CASE( "TileAdapter: multiple boxes convert to matching brush count",
           "[Gate7][TileAdapter]" )
{
    TileAdapterFixture f;
    f.AddTileBox( 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f );
    f.AddTileBox( 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f );
    f.AddTileBox( 0.0f, 2.0f, 0.0f, 1.0f, 1.0f, 0.5f );

    const tile_adapter_result_t result = f.Convert();
    REQUIRE( result.status == tile_adapter_status_t::OK );
    REQUIRE( result.cBrushesAdded == 3u );
    REQUIRE( GeometryDocument_BrushCount( &f.document ) == 3u );
}

// ---------------------------------------------------------------------------
// Tile-derived brush can reconstruct boundary
// ---------------------------------------------------------------------------

TEST_CASE( "TileAdapter: converted brush reconstructs valid boundary",
           "[Gate7][TileAdapter]" )
{
    TileAdapterFixture f;
    f.AddTileBox( 1.5f, 2.5f, 0.25f, 0.5f, 0.5f, 0.25f );
    REQUIRE( f.Convert().status == tile_adapter_status_t::OK );

    const brush_solid_t *pBrush = f.document.brushes.pData[0];
    brush_boundary_t boundary{};
    REQUIRE( BrushBoundary_Init( &boundary, &f.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, pBrush, f.policy ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_VertexCount( &boundary ) == 8u );
    REQUIRE( BrushBoundary_FaceCount( &boundary ) == 6u );
    BrushBoundary_Shutdown( &boundary );
}

// ---------------------------------------------------------------------------
// Full round-trip: tile → brush → cook → serialize → load → re-cook
// ---------------------------------------------------------------------------

TEST_CASE( "TileAdapter: tile-derived brush survives full pipeline round-trip",
           "[Gate7][TileAdapter]" )
{
    TileAdapterFixture f;
    f.AddTileBox( 3.0f, 4.0f, 0.5f, 1.0f, 1.0f, 0.5f );
    REQUIRE( f.Convert().status == tile_adapter_status_t::OK );

    // Cook the document.
    geometry_cook_result_t cook1{};
    REQUIRE( GeometryCook_Init( &cook1, &f.allocator ) ==
             geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_TryCook(
                 &cook1, &f.document, f.policy ) ==
             geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_TriangleCount( &cook1 ) == 12u );
    const common::content_hash_t directHash =
        GeometryCook_ContentHash( &cook1 );

    // Serialize.
    common::text_buffer_t textBuf{};
    REQUIRE( common::TextBuffer_Init( &textBuf, &f.allocator ) );
    REQUIRE( GeometrySerialization_SaveToText(
                 &f.document, &textBuf ).status ==
             geometry_serialization_status_t::OK );

    // Load.
    geometry_document_t loaded{};
    REQUIRE( GeometrySerialization_LoadFromText(
                 common::TextBuffer_View( &textBuf ),
                 &f.allocator, f.policy, &loaded ).status ==
             geometry_serialization_status_t::OK );
    REQUIRE( GeometryDocument_BrushCount( &loaded ) == 1u );

    // Re-cook.
    geometry_cook_result_t cook2{};
    REQUIRE( GeometryCook_Init( &cook2, &f.allocator ) ==
             geometry_cook_status_t::OK );
    REQUIRE( GeometryCook_TryCook(
                 &cook2, &loaded, f.policy ) ==
             geometry_cook_status_t::OK );

    // Identical hash.
    REQUIRE( common::ContentHash_Equals(
                 directHash,
                 GeometryCook_ContentHash( &cook2 ) ) );

    GeometryCook_Shutdown( &cook2 );
    GeometryDocument_Shutdown( &loaded );
    GeometryCook_Shutdown( &cook1 );
}

// ---------------------------------------------------------------------------
// Error paths
// ---------------------------------------------------------------------------

TEST_CASE( "TileAdapter: null arguments rejected",
           "[Gate7][TileAdapter]" )
{
    REQUIRE( GeometryTileMapAdapter_ConvertBoxes(
                 nullptr, nullptr, {}, nullptr ).status ==
             tile_adapter_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "TileAdapter: StatusName returns non-null",
           "[Gate7][TileAdapter]" )
{
    REQUIRE( GeometryTileMapAdapter_StatusName(
                 tile_adapter_status_t::OK ) != nullptr );
    REQUIRE( GeometryTileMapAdapter_StatusName(
                 tile_adapter_status_t::DOCUMENT_ADD_FAILED ) !=
             nullptr );
}

} // namespace cypher::editor::geometry
