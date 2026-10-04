//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Face edits exercise authored planes, independent materials, and persistence.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_Edit.h"
#include "CypherMap_FaceEdit.h"
#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_RaycastQueries.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{
struct fixture_t {
    fixture_t()
    {
        map_create_desc_t desc{};
        desc.name = StringView_FromCString( "Face edit" );
        desc.game = StringView_FromCString( "reap" );
        REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), desc ) == map_status_t::OK );
        REQUIRE( MapEdit_CreateBox( &map, { { -32.0, -16.0, -8.0 }, { 32.0, 16.0, 8.0 } },
                                  StringView_FromCString( "materials/original.cymat" ), {}, &brushId ) == map_status_t::OK );
        const auto *pBrush = geo::GeometryDocument_FindBrush( &map.geometry, { brushId } );
        REQUIRE( pBrush != nullptr );
        for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
            if ( pBrush->sides.pData[i].plane.normal.x == 1.0 ) { iFace = i; sideId = pBrush->sides.pData[i].sourceId.value; }
        }
        REQUIRE( sideId != 0u );
    }
    map_document_t map{};
    u64 brushId{};
    u64 sideId{};
    usize iFace{};
};

const geo::brush_solid_t *Brush( const map_document_t &map, u64 id )
{
    const auto *pBrush = geo::GeometryDocument_FindBrush( &map.geometry, { id } );
    REQUIRE( pBrush != nullptr );
    return pBrush;
}

geo::geometry_brush_side_attributes_t Attributes( const map_document_t &map, u64 id, usize iSide )
{
    const auto *pStore = geo::GeometryDocument_FindBrushAttributes( &map.geometry, { id } );
    REQUIRE( pStore != nullptr );
    geo::geometry_brush_side_attributes_t attributes{};
    REQUIRE( geo::BrushSideAttributeStore_TryGet( pStore, Brush( map, id )->sides.pData[iSide].iAttributeIndex, &attributes ) == geo::geometry_status_t::OK );
    return attributes;
}

struct saved_t {
    std::string root;
    std::map<std::string, std::string> chunks;
    map_save_sink_t Sink()
    {
        return { this,
            []( void *p, string_view_t path, string_view_t text ) noexcept -> bool_t {
                static_cast<saved_t *>( p )->chunks[std::string( path.pData, path.cchLength )].assign( text.pData, text.cchLength ); return CY_TRUE;
            },
            []( void *p, string_view_t path ) noexcept -> bool_t {
                static_cast<saved_t *>( p )->chunks.erase( std::string( path.pData, path.cchLength ) ); return CY_TRUE;
            },
            []( void *p, string_view_t text ) noexcept -> bool_t {
                static_cast<saved_t *>( p )->root.assign( text.pData, text.cchLength ); return CY_TRUE;
            } };
    }
};
} // namespace

TEST_CASE( "Brush face push pull preserves identity and extends the intended plane", "[map][face-edit]" )
{
    fixture_t f;
    const u64 revision = f.map.geometry.revision;
    map_document_t *pCopy = nullptr;
    REQUIRE( MapEdit_Clone( &f.map, &pCopy ) == map_status_t::OK );
    std::unique_ptr<map_document_t> copy( pCopy );
    REQUIRE( MapFaceEdit_PushPull( copy.get(), f.brushId, f.sideId, 16.0 ) == map_status_t::OK );
    const auto *pOriginal = Brush( f.map, f.brushId );
    const auto *pAfter = Brush( *copy, f.brushId );
    CHECK( pOriginal->sides.pData[f.iFace].plane.d == -32.0 );
    CHECK( pAfter->sides.pData[f.iFace].plane.d == -48.0 );
    CHECK( copy->geometry.revision == revision + 1u );
    CHECK( f.map.geometry.revision == revision );
    REQUIRE( pAfter->sides.nCount == pOriginal->sides.nCount );
    for ( usize i = 0u; i < pAfter->sides.nCount; ++i ) {
        CHECK( pAfter->sides.pData[i].sourceId.value == pOriginal->sides.pData[i].sourceId.value );
        CHECK( pAfter->sides.pData[i].iAttributeIndex == pOriginal->sides.pData[i].iAttributeIndex );
        CHECK( Attributes( *copy, f.brushId, i ).material.value == Attributes( f.map, f.brushId, i ).material.value );
    }
    CHECK( geo::BrushValidation_Deep( pAfter, copy->geometryPolicy, copy->pAllocator ).bWatertight );
}

TEST_CASE( "Invalid face contractions and coordinates do not publish a brush", "[map][face-edit]" )
{
    fixture_t f;
    const auto revision = f.map.geometry.revision;
    CHECK( MapFaceEdit_PushPull( &f.map, f.brushId, f.sideId, -64.0 ) != map_status_t::OK );
    CHECK( MapFaceEdit_PushPull( &f.map, f.brushId, f.sideId, 1.0e12 ) != map_status_t::OK );
    CHECK( MapFaceEdit_PushPull( &f.map, f.brushId, f.sideId, std::numeric_limits<double>::infinity() ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapFaceEdit_PushPull( &f.map, f.brushId, 999999u, 16.0 ) == map_status_t::UNKNOWN_OBJECT );
    CHECK( Brush( f.map, f.brushId )->sides.pData[f.iFace].plane.d == -32.0 );
    CHECK( f.map.geometry.revision == revision );
    REQUIRE( MapFaceEdit_PushPull( &f.map, f.brushId, f.sideId, -16.0 ) == map_status_t::OK );
    CHECK( Brush( f.map, f.brushId )->sides.pData[f.iFace].plane.d == -16.0 );
    f.map.bReadOnly = CY_TRUE;
    CHECK( MapFaceEdit_PushPull( &f.map, f.brushId, f.sideId, 16.0 ) == map_status_t::READ_ONLY );
    CHECK( MapFaceEdit_SetMaterial( &f.map, f.brushId, f.sideId, {} ) == map_status_t::READ_ONLY );
}

TEST_CASE( "Face material assignment isolates shared surface records and retains UV projection", "[map][face-edit]" )
{
    fixture_t f;
    auto *pBrush = geo::GeometryDocument_FindBrushMutable( &f.map.geometry, { f.brushId } );
    REQUIRE( pBrush != nullptr );
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) { pBrush->sides.pData[i].iAttributeIndex = 0u; }
    const auto original = Attributes( f.map, f.brushId, f.iFace );
    REQUIRE( MapFaceEdit_SetMaterial( &f.map, f.brushId, f.sideId, StringView_FromCString( "materials/replacement.cymat" ) ) == map_status_t::OK );
    const auto updated = Attributes( f.map, f.brushId, f.iFace );
    CHECK( StringView_Equals( MapMaterials_Path( &f.map.materials, updated.material.value ), StringView_FromCString( "materials/replacement.cymat" ) ) );
    CHECK( updated.uvProjection.uAxis.x == original.uvProjection.uAxis.x );
    CHECK( updated.uvProjection.vAxis.y == original.uvProjection.vAxis.y );
    pBrush = geo::GeometryDocument_FindBrushMutable( &f.map.geometry, { f.brushId } );
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        if ( i != f.iFace ) { CHECK( Attributes( f.map, f.brushId, i ).material.value == original.material.value ); }
    }
    const auto revision = f.map.geometry.revision;
    REQUIRE( MapFaceEdit_SetMaterial( &f.map, f.brushId, f.sideId, StringView_FromCString( "materials/replacement.cymat" ) ) == map_status_t::OK );
    CHECK( f.map.geometry.revision == revision );
    REQUIRE( MapFaceEdit_SetMaterial( &f.map, f.brushId, f.sideId, {} ) == map_status_t::OK );
    CHECK( Attributes( f.map, f.brushId, f.iFace ).material.value == 0u );
}

TEST_CASE( "Face identity and edits survive map serialization and exact ray hits", "[map][face-edit]" )
{
    fixture_t f;
    REQUIRE( MapFaceEdit_PushPull( &f.map, f.brushId, f.sideId, 16.0 ) == map_status_t::OK );
    REQUIRE( MapFaceEdit_SetMaterial( &f.map, f.brushId, f.sideId, StringView_FromCString( "materials/replacement.cymat" ) ) == map_status_t::OK );
    saved_t saved;
    REQUIRE( MapDocument_Save( &f.map, saved.Sink() ) == map_status_t::OK );
    std::vector<map_chunk_input_t> chunks;
    for ( const auto &[path, text] : saved.chunks ) { chunks.push_back( { { path.data(), path.size() }, { text.data(), text.size() } } ); }
    map_document_t loaded;
    REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), { saved.root.data(), saved.root.size() }, { chunks.data(), chunks.size() } ) == map_status_t::OK );
    const auto *pBrush = Brush( loaded, f.brushId );
    REQUIRE( pBrush->sides.pData[f.iFace].sourceId.value == f.sideId );
    CHECK( pBrush->sides.pData[f.iFace].plane.d == -48.0 );
    CHECK( StringView_Equals( MapMaterials_Path( &loaded.materials, Attributes( loaded, f.brushId, f.iFace ).material.value ), StringView_FromCString( "materials/replacement.cymat" ) ) );
    geo::brush_boundary_t boundary{};
    REQUIRE( geo::BrushBoundary_Init( &boundary, loaded.pAllocator ) == geo::geometry_status_t::OK );
    REQUIRE( geo::BrushBoundary_TryReconstruct( &boundary, pBrush, loaded.geometryPolicy ) == geo::geometry_status_t::OK );
    geo::brush_raycast_hit_t hit{};
    CHECK( geo::BrushQueries_TryRaycast( pBrush, &boundary, { { 100.0, 0.0, 0.0 }, { -1.0, 0.0, 0.0 } }, {}, loaded.geometryPolicy, &hit ) == geo::geometry_status_t::OK );
    CHECK( hit.bHit );
    CHECK( hit.sideSourceId.value == f.sideId );
    CHECK( hit.brushSourceId.value == f.brushId );
    CHECK( hit.position.x == 48.0 );
    geo::BrushBoundary_Shutdown( &boundary );
}
