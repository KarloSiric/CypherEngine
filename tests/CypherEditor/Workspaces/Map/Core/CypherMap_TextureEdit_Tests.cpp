//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_TextureEdit_Tests.cpp
//  Purpose: Contract tests for material alignment on brush sides and mesh
//           faces: what each alignment produces, and that only the targeted
//           faces change.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_TextureEdit.h"
#include "CypherMap_Hotspots.h"
#include "CypherMap_Edit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherMath_UV.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }

struct fixture_t {
    map_document_t map{};
    fixture_t() { REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), { SV( "Texture fixture" ), SV( "reap" ), {} } ) == map_status_t::OK ); }
    u64 Primitive( map_primitive_kind_t kind, math::aabbd_t bounds )
    {
        map_primitive_desc_t desc{}; desc.kind = kind; desc.bounds = bounds; desc.wedgeCutAxis = 0; desc.wedgeSlopeAxis = 2;
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( &map, desc, SV( "materials/source.cymat" ), {}, &id ) == map_status_t::OK );
        return id;
    }
};

geo::geometry_brush_side_attributes_t Side( const map_document_t &map, u64 brush, usize i )
{
    const auto *solid = geo::GeometryDocument_FindBrush( &map.geometry, { brush } ); REQUIRE( solid );
    const auto *store = geo::GeometryDocument_FindBrushAttributes( &map.geometry, { brush } ); REQUIRE( store );
    geo::geometry_brush_side_attributes_t out{};
    REQUIRE( geo::BrushSideAttributeStore_TryGet( store, solid->sides.pData[i].iAttributeIndex, &out ) == geo::geometry_status_t::OK );
    return out;
}
u64 SideId( const map_document_t &map, u64 brush, usize i ) { return geo::GeometryDocument_FindBrush( &map.geometry, { brush } )->sides.pData[i].sourceId.value; }

// The side's polygon projected through its mapping: UV bounds.
void SideUvBounds( const map_document_t &map, u64 brush, usize iSide, math::vec2d_t &low, math::vec2d_t &high )
{
    const auto *solid = geo::GeometryDocument_FindBrush( &map.geometry, { brush } ); REQUIRE( solid );
    geo::brush_boundary_t boundary{};
    REQUIRE( geo::BrushBoundary_Init( &boundary, Allocator_GetSystem() ) == geo::geometry_status_t::OK );
    REQUIRE( geo::BrushBoundary_TryReconstruct( &boundary, solid, map.geometryPolicy ) == geo::geometry_status_t::OK );
    const auto attributes = Side( map, brush, iSide );
    low = { 1e300, 1e300 }; high = { -1e300, -1e300 };
    for ( usize f = 0; f < boundary.faces.nCount; ++f ) {
        const auto &face = boundary.faces.pData[f];
        if ( face.iSide != iSide ) { continue; }
        for ( u32 k = 0; k < face.cVertices; ++k ) {
            math::vec2d_t uv{};
            REQUIRE( math::Uvd_TryProjectPlanarPoint( attributes.uvProjection, boundary.vertices.pData[boundary.faceVertexIndices.pData[face.iFirstIndex + k]], 0.0, &uv ) );
            low = { std::min( low.x, uv.x ), std::min( low.y, uv.y ) }; high = { std::max( high.x, uv.x ), std::max( high.y, uv.y ) };
        }
    }
    geo::BrushBoundary_Shutdown( &boundary );
}
} // namespace

TEST_CASE( "Fit and justify place the texture on one brush side only", "[map][texture-edit][brush]" )
{
    fixture_t f; const u64 box = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 200, 100, 64 } } );
    const map_surface_ref_t first{ box, SideId( f.map, box, 0 ) };
    const auto untouched = Side( f.map, box, 1 );
    REQUIRE( MapTextureEdit_Align( &f.map, { &first, 1 }, map_uv_align_t::FIT, 128.0 ) == map_status_t::OK );
    math::vec2d_t low{}, high{};
    SideUvBounds( f.map, box, 0, low, high );
    CHECK( high.x - low.x == Catch::Approx( 1.0 ) ); CHECK( high.y - low.y == Catch::Approx( 1.0 ) );
    // A record the other sides share was split, not edited in place.
    const auto other = Side( f.map, box, 1 );
    CHECK( other.uvProjection.worldUnitsPerUv.x == untouched.uvProjection.worldUnitsPerUv.x );
    CHECK( other.uvProjection.offset.x == untouched.uvProjection.offset.x );

    const map_surface_ref_t second{ box, SideId( f.map, box, 2 ) };
    REQUIRE( MapTextureEdit_Align( &f.map, { &second, 1 }, map_uv_align_t::JUSTIFY_LEFT, 128.0 ) == map_status_t::OK );
    SideUvBounds( f.map, box, 2, low, high );
    CHECK( low.x - std::floor( low.x + 1e-9 ) == Catch::Approx( 0.0 ).margin( 1e-9 ) );
    REQUIRE( MapTextureEdit_Align( &f.map, { &second, 1 }, map_uv_align_t::JUSTIFY_TOP, 128.0 ) == map_status_t::OK );
    SideUvBounds( f.map, box, 2, low, high );
    CHECK( std::ceil( high.y - 1e-9 ) - high.y == Catch::Approx( 0.0 ).margin( 1e-9 ) );
}

TEST_CASE( "Rotate, scale and shift adjust the projection about the face centre", "[map][texture-edit][brush]" )
{
    fixture_t f; const u64 box = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 128, 128, 128 } } );
    const map_surface_ref_t whole{ box, 0 };
    const auto before = Side( f.map, box, 0 );
    REQUIRE( MapTextureEdit_Rotate( &f.map, { &whole, 1 }, 90.0 ) == map_status_t::OK );
    CHECK( std::fabs( std::remainder( Side( f.map, box, 0 ).uvProjection.rotationRadians - before.uvProjection.rotationRadians - 1.5707963267948966, 6.283185307179586 ) ) < 1e-9 );
    REQUIRE( MapTextureEdit_Scale( &f.map, { &whole, 1 }, { 2.0, 2.0 } ) == map_status_t::OK );
    CHECK( std::fabs( Side( f.map, box, 3 ).uvProjection.worldUnitsPerUv.x ) == Catch::Approx( 2.0 * std::fabs( before.uvProjection.worldUnitsPerUv.x ) ) );
    const auto scaled = Side( f.map, box, 4 );
    REQUIRE( MapTextureEdit_Shift( &f.map, { &whole, 1 }, { 32.0, 0.0 }, 128.0 ) == map_status_t::OK );
    CHECK( Side( f.map, box, 4 ).uvProjection.offset.x - scaled.uvProjection.offset.x == Catch::Approx( 32.0 / scaled.uvProjection.worldUnitsPerUv.x ) );
    CHECK( MapTextureEdit_Scale( &f.map, { &whole, 1 }, { 0.0, 1.0 } ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "World alignment projects a slope along its dominant axis and face alignment along its normal", "[map][texture-edit][brush]" )
{
    fixture_t f; const u64 wedge = f.Primitive( map_primitive_kind_t::WEDGE, { { 0, 0, 0 }, { 128, 128, 64 } } );
    const auto *solid = geo::GeometryDocument_FindBrush( &f.map.geometry, { wedge } ); REQUIRE( solid );
    usize slope = solid->sides.nCount;
    for ( usize i = 0; i < solid->sides.nCount; ++i ) {
        const auto n = solid->sides.pData[i].plane.normal;
        const int axial = ( std::fabs( n.x ) > 0.999 ) + ( std::fabs( n.y ) > 0.999 ) + ( std::fabs( n.z ) > 0.999 );
        if ( axial == 0 ) { slope = i; }
    }
    REQUIRE( slope < solid->sides.nCount );
    const map_surface_ref_t ref{ wedge, solid->sides.pData[slope].sourceId.value };
    const auto normal = solid->sides.pData[slope].plane.normal;
    REQUIRE( MapTextureEdit_Align( &f.map, { &ref, 1 }, map_uv_align_t::WORLD, 128.0 ) == map_status_t::OK );
    const auto world = Side( f.map, wedge, slope ).uvProjection.normal;
    CHECK( std::fabs( world.x ) + std::fabs( world.y ) + std::fabs( world.z ) == Catch::Approx( 1.0 ) ); // One world axis.
    REQUIRE( MapTextureEdit_Align( &f.map, { &ref, 1 }, map_uv_align_t::FACE, 128.0 ) == map_status_t::OK );
    const auto face = Side( f.map, wedge, slope ).uvProjection.normal;
    CHECK( face.x * normal.x + face.y * normal.y + face.z * normal.z == Catch::Approx( 1.0 ) );
}

TEST_CASE( "Mesh face UVs fit, rotate and align through the mesh transaction", "[map][texture-edit][mesh]" )
{
    fixture_t f; const u64 id = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 256, 128, 64 } } );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &f.map, { &id, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
    const auto read = [&]( std::vector<math::vec2d_t> &uvs, u64 &face ) {
        geo::mesh_source_description_t d{};
        REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
        REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ), &d ) == geo::geometry_status_t::OK );
        face = d.faces.pData[0].sourceId.value; uvs.clear();
        for ( u32 k = 0; k < d.faces.pData[0].cCorners; ++k ) { uvs.push_back( d.corners.pData[d.faces.pData[0].iFirstCorner + k].attributes.uv0 ); }
        geo::MeshSourceDescription_Shutdown( &d );
    };
    std::vector<math::vec2d_t> uvs; u64 face = 0;
    read( uvs, face );
    const map_surface_ref_t ref{ id, face };
    REQUIRE( MapTextureEdit_Align( &f.map, { &ref, 1 }, map_uv_align_t::FIT, 128.0 ) == map_status_t::OK );
    read( uvs, face );
    f64 lowU = 1e9, highU = -1e9;
    for ( const auto &uv : uvs ) { lowU = std::min( lowU, uv.x ); highU = std::max( highU, uv.x ); }
    CHECK( lowU == Catch::Approx( 0.0 ).margin( 1e-9 ) ); CHECK( highU == Catch::Approx( 1.0 ) );
    const auto revision = f.map.geometry.revision;
    REQUIRE( MapTextureEdit_Rotate( &f.map, { &ref, 1 }, 90.0 ) == map_status_t::OK );
    CHECK( f.map.geometry.revision != revision );
    const map_surface_ref_t whole{ id, 0 };
    REQUIRE( MapTextureEdit_Align( &f.map, { &whole, 1 }, map_uv_align_t::WORLD, 64.0 ) == map_status_t::OK );
    read( uvs, face );
    for ( const auto &uv : uvs ) { CHECK( std::fabs( uv.x * 64.0 - std::round( uv.x * 64.0 ) ) < 1e-6 ); } // World units / 64.
}

TEST_CASE( "Texture edits reject what has no face textures and leave the map unchanged", "[map][texture-edit][validation]" )
{
    fixture_t f; const u64 box = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 64, 64, 64 } } );
    const auto revision = f.map.geometry.revision;
    const map_surface_ref_t missing{ 999999, 0 }, badSide{ box, 999999 }, none{ 0, 0 };
    CHECK( MapTextureEdit_Align( &f.map, { &missing, 1 }, map_uv_align_t::FIT, 128.0 ) == map_status_t::UNKNOWN_OBJECT );
    CHECK( MapTextureEdit_Align( &f.map, { &badSide, 1 }, map_uv_align_t::FIT, 128.0 ) != map_status_t::OK );
    CHECK( MapTextureEdit_Align( &f.map, { &none, 1 }, map_uv_align_t::FIT, 128.0 ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapTextureEdit_Align( &f.map, {}, map_uv_align_t::FIT, 128.0 ) == map_status_t::INVALID_ARGUMENT );
    f.map.bReadOnly = CY_TRUE;
    const map_surface_ref_t whole{ box, 0 };
    CHECK( MapTextureEdit_Rotate( &f.map, { &whole, 1 }, 90.0 ) == map_status_t::READ_ONLY );
    CHECK( f.map.geometry.revision == revision );
}

TEST_CASE( "Replace Materials swaps one material across brushes and meshes", "[map][texture-edit][replace]" )
{
    fixture_t f;
    const u64 brush = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 64, 64, 64 } } );
    const u64 mesh = f.Primitive( map_primitive_kind_t::BOX, { { 128, 0, 0 }, { 192, 64, 64 } } );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &f.map, { &mesh, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
    usize replaced = 0;
    REQUIRE( MapTextureEdit_ReplaceMaterial( &f.map, StringView_FromCString( "materials/source.cymat" ), StringView_FromCString( "materials/other.cymat" ), &replaced ) == map_status_t::OK );
    CHECK( replaced == 12 ); // Six brush sides and six mesh faces.
    CHECK( StringView_Equals( MapMaterials_Path( &f.map.materials, Side( f.map, brush, 0 ).material.value ), StringView_FromCString( "materials/other.cymat" ) ) );
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { mesh } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &f.map.geometry, { mesh } ), &d ) == geo::geometry_status_t::OK );
    for ( usize i = 0; i < d.faces.nCount; ++i ) {
        CHECK( StringView_Equals( MapMaterials_Path( &f.map.materials, d.faces.pData[i].attributes.material.value ), StringView_FromCString( "materials/other.cymat" ) ) );
    }
    geo::MeshSourceDescription_Shutdown( &d );
    const auto revision = f.map.geometry.revision;
    REQUIRE( MapTextureEdit_ReplaceMaterial( &f.map, StringView_FromCString( "materials/missing.cymat" ), StringView_FromCString( "materials/x.cymat" ), &replaced ) == map_status_t::OK );
    CHECK( replaced == 0 ); CHECK( f.map.geometry.revision == revision );
}

TEST_CASE( "Unwrap lays a mesh's faces out as islands inside the unit square", "[map][texture-edit][unwrap]" )
{
    fixture_t f; const u64 id = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 256, 128, 64 } } );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &f.map, { &id, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
    const map_surface_ref_t whole{ id, 0 };
    REQUIRE( MapTextureEdit_Unwrap( &f.map, { &whole, 1 } ) == map_status_t::OK );
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ), &d ) == geo::geometry_status_t::OK );
    f64 high = 0.0;
    for ( usize i = 0; i < d.corners.nCount; ++i ) {
        const auto uv = d.corners.pData[i].attributes.uv0;
        CHECK( uv.x >= -1e-9 ); CHECK( uv.y >= -1e-9 ); CHECK( uv.x <= 1.0 + 1e-9 ); CHECK( uv.y <= 1.0 + 1e-9 );
        high = std::max( { high, uv.x, uv.y } );
    }
    CHECK( high == Catch::Approx( 1.0 ) );
    geo::MeshSourceDescription_Shutdown( &d );
    const u64 brush = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 64, 64, 64 } } );
    const map_surface_ref_t brushWhole{ brush, 0 };
    CHECK( MapTextureEdit_Unwrap( &f.map, { &brushWhole, 1 } ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Fast Texture fits brush sides and mesh faces into one rectangle of the texture", "[map][texture-edit][fast-texture]" )
{
    fixture_t f;
    const map_uv_rect_t rect{ { 0.25, 0.5 }, { 0.5, 1.0 }, false, false };
    SECTION( "A brush side: its UV bounds become the rectangle; a flip mirrors it inside" )
    {
        const u64 box = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 200, 100, 64 } } );
        const map_surface_ref_t side{ box, SideId( f.map, box, 0 ) };
        const auto untouched = Side( f.map, box, 1 );
        REQUIRE( MapTextureEdit_FitToRect( &f.map, { &side, 1 }, rect, map_uv_fit_mode_t::EXISTING ) == map_status_t::OK );
        math::vec2d_t low{}, high{};
        SideUvBounds( f.map, box, 0, low, high );
        CHECK( low.x == Catch::Approx( 0.25 ) ); CHECK( high.x == Catch::Approx( 0.5 ) );
        CHECK( low.y == Catch::Approx( 0.5 ) ); CHECK( high.y == Catch::Approx( 1.0 ) );
        CHECK( Side( f.map, box, 1 ).uvProjection.offset.x == untouched.uvProjection.offset.x ); // Only that side.
        const auto before = Side( f.map, box, 0 ).uvProjection.worldUnitsPerUv;
        map_uv_rect_t flipped = rect; flipped.bFlipU = true;
        REQUIRE( MapTextureEdit_FitToRect( &f.map, { &side, 1 }, flipped, map_uv_fit_mode_t::EXISTING ) == map_status_t::OK );
        SideUvBounds( f.map, box, 0, low, high );
        CHECK( low.x == Catch::Approx( 0.25 ) ); CHECK( high.x == Catch::Approx( 0.5 ) );
        CHECK( Side( f.map, box, 0 ).uvProjection.worldUnitsPerUv.x * before.x < 0.0 ); // Mirrored.
    }
    SECTION( "Mesh faces: together (existing, planar) or each on its own (square)" )
    {
        const u64 id = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 256, 128, 64 } } );
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( &f.map, { &id, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
        const auto corners = [&]( usize face, std::vector<math::vec2d_t> &uvs ) {
            geo::mesh_source_description_t d{};
            REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
            REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ), &d ) == geo::geometry_status_t::OK );
            uvs.clear();
            for ( u32 k = 0; k < d.faces.pData[face].cCorners; ++k ) { uvs.push_back( d.corners.pData[d.faces.pData[face].iFirstCorner + k].attributes.uv0 ); }
            const u64 faceId = d.faces.pData[face].sourceId.value;
            geo::MeshSourceDescription_Shutdown( &d );
            return faceId;
        };
        std::vector<math::vec2d_t> uvs;
        const map_surface_ref_t faces[]{ { id, corners( 0, uvs ) }, { id, corners( 1, uvs ) } };
        for ( const auto mode : { map_uv_fit_mode_t::EXISTING, map_uv_fit_mode_t::PLANAR, map_uv_fit_mode_t::SQUARE } ) {
            CAPTURE( static_cast<int>( mode ) );
            REQUIRE( MapTextureEdit_FitToRect( &f.map, { faces, 2 }, rect, mode ) == map_status_t::OK );
            math::vec2d_t low{ 1e9, 1e9 }, high{ -1e9, -1e9 };
            for ( usize face = 0; face < 2; ++face ) {
                ( void )corners( face, uvs );
                math::vec2d_t faceLow{ 1e9, 1e9 }, faceHigh{ -1e9, -1e9 };
                for ( const auto &uv : uvs ) {
                    CHECK( uv.x >= 0.25 - 1e-9 ); CHECK( uv.x <= 0.5 + 1e-9 ); CHECK( uv.y >= 0.5 - 1e-9 ); CHECK( uv.y <= 1.0 + 1e-9 );
                    faceLow = { std::min( faceLow.x, uv.x ), std::min( faceLow.y, uv.y ) }; faceHigh = { std::max( faceHigh.x, uv.x ), std::max( faceHigh.y, uv.y ) };
                }
                if ( mode == map_uv_fit_mode_t::SQUARE ) { CHECK( faceLow.x == Catch::Approx( 0.25 ) ); CHECK( faceHigh.y == Catch::Approx( 1.0 ) ); } // Each fills it.
                low = { std::min( low.x, faceLow.x ), std::min( low.y, faceLow.y ) }; high = { std::max( high.x, faceHigh.x ), std::max( high.y, faceHigh.y ) };
            }
            CHECK( low.x == Catch::Approx( 0.25 ) ); CHECK( high.x == Catch::Approx( 0.5 ) ); // Together they span it exactly.
            CHECK( low.y == Catch::Approx( 0.5 ) ); CHECK( high.y == Catch::Approx( 1.0 ) );
        }
        CHECK( MapTextureEdit_FitToRect( &f.map, { faces, 2 }, { { 0.5, 0.5 }, { 0.5, 1.0 }, false, false }, map_uv_fit_mode_t::EXISTING ) == map_status_t::INVALID_ARGUMENT );
    }
}

TEST_CASE( "Hotspot rectangle files round-trip and faces take the region their size matches", "[map][texture-edit][hotspots]" )
{
    CHECK( MapHotspots_SidecarPath( SV( "materials/dev/trim.cymat" ) ) == "materials/dev/trim.cyrect" );
    CHECK( MapHotspots_SidecarPath( SV( "materials/dev.v2/trim" ) ) == "materials/dev.v2/trim.cyrect" );
    std::vector<geo::mesh_hotspot_rect_t> rects( 2 );
    rects[0].uvMin = { 0.0, 0.0 }; rects[0].uvMax = { 1.0, 0.25 }; rects[0].worldSize = { 256.0, 64.0 }; rects[0].bTileU = true; // A trim strip.
    rects[1].uvMin = { 0.0, 0.5 }; rects[1].uvMax = { 0.5, 1.0 }; rects[1].worldSize = { 128.0, 128.0 }; rects[1].bAllowRotation = false; // A panel.
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( MapHotspots_Write( rects, &text ) == map_status_t::OK );
    std::vector<geo::mesh_hotspot_rect_t> read;
    REQUIRE( MapHotspots_Parse( TextBuffer_View( &text ), read ) == map_status_t::OK );
    REQUIRE( read.size() == 2 );
    CHECK( read[0].bTileU ); CHECK_FALSE( read[1].bAllowRotation ); CHECK( read[1].uvMin.y == 0.5 ); CHECK( read[0].worldSize.x == 256.0 );
    CHECK( MapHotspots_Parse( SV( "@cykv 1\n@schema \"cypher.hotspots\" 1\n{ rects = [ { uv = [0.5, 0, 0.25, 1] size = [1, 1] } ] }" ), read ) == map_status_t::ROOT_INVALID );
    CHECK( MapHotspots_Parse( SV( "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"x\" }" ), read ) == map_status_t::ROOT_UNREADABLE );
    TextBuffer_Shutdown( &text );

    fixture_t f;
    const u64 box = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 128, 128, 64 } } );
    const auto *solid = geo::GeometryDocument_FindBrush( &f.map.geometry, { box } ); REQUIRE( solid );
    usize top = 0, side = 0;
    for ( usize i = 0; i < solid->sides.nCount; ++i ) {
        if ( solid->sides.pData[i].plane.normal.z > 0.99 ) { top = i; }
        if ( solid->sides.pData[i].plane.normal.x > 0.99 ) { side = i; }
    }
    const map_surface_ref_t whole{ box, 0 };
    REQUIRE( MapTextureEdit_ApplyHotspots( &f.map, { &whole, 1 }, rects ) == map_status_t::OK );
    math::vec2d_t low{}, high{};
    SideUvBounds( f.map, box, top, low, high ); // 128 x 128: the panel.
    CHECK( low.x == Catch::Approx( 0.0 ).margin( 1e-9 ) ); CHECK( high.x == Catch::Approx( 0.5 ) );
    CHECK( low.y == Catch::Approx( 0.5 ) ); CHECK( high.y == Catch::Approx( 1.0 ) );
    SideUvBounds( f.map, box, side, low, high ); // 128 x 64: the trim, half its tiling length.
    CHECK( std::min( high.x - low.x, high.y - low.y ) == Catch::Approx( 0.25 ) );
    CHECK( std::max( high.x - low.x, high.y - low.y ) == Catch::Approx( 0.5 ) );
    // Mesh faces go through the library's hotspot fit.
    const u64 mesh = f.Primitive( map_primitive_kind_t::BOX, { { 512, 0, 0 }, { 640, 128, 128 } } );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &f.map, { &mesh, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
    const map_surface_ref_t meshWhole{ mesh, 0 };
    REQUIRE( MapTextureEdit_ApplyHotspots( &f.map, { &meshWhole, 1 }, rects ) == map_status_t::OK );
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { mesh } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &f.map.geometry, { mesh } ), &d ) == geo::geometry_status_t::OK );
    for ( usize i = 0; i < d.corners.nCount; ++i ) { // Every face is a 128 cube face: the panel.
        CHECK( d.corners.pData[i].attributes.uv0.x <= 0.5 + 1e-9 ); CHECK( d.corners.pData[i].attributes.uv0.y >= 0.5 - 1e-9 );
    }
    geo::MeshSourceDescription_Shutdown( &d );
}
