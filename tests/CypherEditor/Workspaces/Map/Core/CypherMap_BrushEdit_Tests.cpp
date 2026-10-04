//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Brush operation contracts: exact authored ancestry, identity and ownership.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_BrushEdit.h"
#include "CypherMap_Edit.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushClip.h"
#include "CypherGeometry_BrushCSG.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;
namespace
{

string_view_t SV( const char *p ) { return StringView_FromCString( p ); }
string_view_t SV( const std::string &s ) { return { s.data(), s.size() }; }
void Create( map_document_t &map, const allocator_t *pAllocator = Allocator_GetSystem() )
{
    REQUIRE( MapDocument_Create( &map, pAllocator, { SV( "Brush edits" ), SV( "reap" ), {} } ) == map_status_t::OK );
}
u64 Box( map_document_t &map, math::aabbd_t bounds = { { -64, -64, -64 }, { 64, 64, 64 } } )
{
    u64 id = 0u;
    REQUIRE( MapEdit_CreateBox( &map, bounds, SV( "materials/original.cymat" ), {}, &id ) == map_status_t::OK );
    return id;
}
const geo::brush_solid_t *Brush( const map_document_t &map, u64 id )
{
    const auto *pBrush = geo::GeometryDocument_FindBrush( &map.geometry, { id } );
    REQUIRE( pBrush ); return pBrush;
}
map_bounds_t Bounds( const map_document_t &map, u64 id )
{
    geo::brush_source_t source{};
    REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &map.geometry, { id }, map.pAllocator, &source ) == geo::geometry_status_t::OK );
    const auto bounds = MapGeometry_BrushBounds( source, map.geometryPolicy, map.pAllocator );
    geo::BrushSource_Shutdown( &source ); REQUIRE( bounds.bHas ); return bounds;
}
geo::geometry_brush_side_attributes_t Attributes( const map_document_t &map, u64 root, usize iSide )
{
    const auto *pAttributes = geo::GeometryDocument_FindBrushAttributes( &map.geometry, { root } ); REQUIRE( pAttributes );
    geo::geometry_brush_side_attributes_t value{};
    REQUIRE( geo::BrushSideAttributeStore_TryGet( pAttributes, Brush( map, root )->sides.pData[iSide].iAttributeIndex, &value ) == geo::geometry_status_t::OK );
    return value;
}
const map_geometry_record_t &Placement( const map_document_t &map, u64 id )
{
    for ( usize i = 0u; i < map.geometryRecords.nCount; ++i ) { if ( map.geometryRecords.pData[i].id == id ) { return map.geometryRecords.pData[i]; } }
    FAIL( "Missing brush placement" ); return map.geometryRecords.pData[0];
}
const key_value_t *Face( const key_value_t *pRecord, u64 id )
{
    const auto *pFaces = KeyValue_Find( pRecord, SV( "faces" ) );
    for ( usize i = 0u; i < KeyValue_ChildCount( pFaces ); ++i ) {
        const auto *pFace = KeyValue_ChildAt( pFaces, i ); u64 faceId = 0u;
        if ( KeyValue_GetU64( KeyValue_Find( pFace, SV( "id" ) ), &faceId ) && faceId == id ) { return pFace; }
    }
    return nullptr;
}
struct saved_t {
    std::string root;
    std::map<std::string, std::string> chunks;
    map_save_sink_t Sink()
    {
        return { this,
            []( void *ctx, string_view_t path, string_view_t text ) noexcept -> bool_t {
                static_cast<saved_t *>( ctx )->chunks[std::string( path.pData, path.cchLength )].assign( text.pData, text.cchLength ); return CY_TRUE;
            },
            []( void *ctx, string_view_t path ) noexcept -> bool_t {
                static_cast<saved_t *>( ctx )->chunks.erase( std::string( path.pData, path.cchLength ) ); return CY_TRUE;
            },
            []( void *ctx, string_view_t text ) noexcept -> bool_t {
                static_cast<saved_t *>( ctx )->root.assign( text.pData, text.cchLength ); return CY_TRUE;
            } };
    }
};
void Load( map_document_t &map, const saved_t &saved )
{
    std::vector<map_chunk_input_t> inputs;
    for ( const auto &[path, text] : saved.chunks ) { inputs.push_back( { SV( path ), SV( text ) } ); }
    REQUIRE( MapDocument_Load( &map, Allocator_GetSystem(), SV( saved.root ), { inputs.data(), inputs.size() } ) == map_status_t::OK );
}

// Add genuinely future per-face fields to a retained record, so a 1-to-many
// operation has to follow exact side ancestry rather than only copy materials.
void AddMetadata( map_document_t &map, u64 id )
{
    map_chunk_t *pChunk = nullptr; auto *pRecord = MapDocument_FindObject( &map, id, &pChunk ); REQUIRE( pRecord ); REQUIRE( pChunk );
    auto *pDoc = pChunk->store.pDocument;
    auto *pName = KeyValue_ObjectInsert( pDoc, pRecord, SV( "name" ), key_value_type_t::STRING );
    REQUIRE( pName ); REQUIRE( KeyValue_SetString( pDoc, pName, SV( "Named authored shell" ) ) );
    auto *pFuture = KeyValue_ObjectInsert( pDoc, pRecord, SV( "future_root" ), key_value_type_t::U64 );
    REQUIRE( pFuture ); REQUIRE( KeyValue_SetU64( pDoc, pFuture, 54321u ) );
    auto *pFaces = KeyValue_ObjectInsert( pDoc, pRecord, SV( "faces" ), key_value_type_t::ARRAY ); REQUIRE( pFaces );
    const auto *pBrush = Brush( map, id );
    auto *pAttributes = geo::GeometryDocument_FindBrushAttributesMutable( &map.geometry, { id } ); REQUIRE( pAttributes );
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        auto *pFace = KeyValue_ArrayAppend( pDoc, pFaces, key_value_type_t::OBJECT ); REQUIRE( pFace );
        auto *pFaceId = KeyValue_ObjectInsert( pDoc, pFace, SV( "id" ), key_value_type_t::U64 ); REQUIRE( pFaceId );
        REQUIRE( KeyValue_SetU64( pDoc, pFaceId, pBrush->sides.pData[i].sourceId.value ) );
        auto *pMarker = KeyValue_ObjectInsert( pDoc, pFace, SV( "future_side" ), key_value_type_t::U64 ); REQUIRE( pMarker );
        REQUIRE( KeyValue_SetU64( pDoc, pMarker, i + 1u ) );
        auto &attributes = pAttributes->records.pData[pBrush->sides.pData[i].iAttributeIndex];
        attributes.uvProjection.worldUnitsPerUv.x = 64.0 + 16.0 * static_cast<double>( i );
        attributes.uvProjection.offset.x = 0.125 * static_cast<double>( i );
    }
}

void CheckMetadata( map_document_t &map, u64 id, u64 owner, u32 layer )
{
    CHECK( Placement( map, id ).owner == owner ); CHECK( Placement( map, id ).iLayer == layer );
    const auto *pRecord = MapDocument_FindObject( &map, id, nullptr ); REQUIRE( pRecord );
    string_view_t name{}; REQUIRE( KeyValue_GetString( KeyValue_Find( pRecord, SV( "name" ) ), &name ) );
    CHECK( StringView_Equals( name, SV( "Named authored shell" ) ) );
    u64 rootMarker = 0u; REQUIRE( KeyValue_GetU64( KeyValue_Find( pRecord, SV( "future_root" ) ), &rootMarker ) ); CHECK( rootMarker == 54321u );
    const auto *pBrush = Brush( map, id );
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        const auto *pFace = Face( pRecord, pBrush->sides.pData[i].sourceId.value ); REQUIRE( pFace );
        u64 marker = 0u; REQUIRE( KeyValue_GetU64( KeyValue_Find( pFace, SV( "future_side" ) ), &marker ) );
        REQUIRE( marker >= 1u ); REQUIRE( marker <= 6u );
        const auto attributes = Attributes( map, id, i );
        CHECK( attributes.uvProjection.worldUnitsPerUv.x == Catch::Approx( 64.0 + 16.0 * static_cast<double>( marker - 1u ) ) );
        CHECK( attributes.uvProjection.offset.x == Catch::Approx( 0.125 * static_cast<double>( marker - 1u ) ) );
        CHECK( StringView_Equals( MapMaterials_Path( &map.materials, attributes.material.value ), SV( "materials/original.cymat" ) ) );
    }
}

struct audit_t {
    usize calls{}, bytes{}, failAt{ CY_USIZE_MAX };
    allocator_t allocator{};
    audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *ctx, usize size, usize alignment ) noexcept -> void * {
            auto &a = *static_cast<audit_t *>( ctx ); if ( a.calls++ == a.failAt ) { return nullptr; }
            void *p = Allocator_Allocate( Allocator_GetSystem(), size, alignment ); if ( p ) { a.bytes += size; } return p;
        };
        allocator.pfnFree = []( void *ctx, void *p, usize size, usize alignment ) noexcept {
            if ( p ) { static_cast<audit_t *>( ctx )->bytes -= size; } Allocator_Free( Allocator_GetSystem(), p, size, alignment );
        };
    }
};

void TagOperand( map_document_t &map, u64 id, const char *name, const char *material )
{
    map_chunk_t *pChunk = nullptr; auto *pRecord = MapDocument_FindObject( &map, id, &pChunk ); REQUIRE( pRecord ); REQUIRE( pChunk );
    auto *pAttributes = geo::GeometryDocument_FindBrushAttributesMutable( &map.geometry, { id } ); REQUIRE( pAttributes );
    u64 ref = 0; REQUIRE( MapDocument_MaterialRef( &map, SV( material ), &ref ) == map_status_t::OK );
    const auto *pBrush = Brush( map, id );
    for ( usize i = 0; i < pBrush->sides.nCount; ++i ) {
        auto *pFace = const_cast<key_value_t *>( Face( pRecord, pBrush->sides.pData[i].sourceId.value ) ); REQUIRE( pFace );
        auto *pTag = KeyValue_ObjectInsert( pChunk->store.pDocument, pFace, SV( "future_operand" ), key_value_type_t::STRING ); REQUIRE( pTag );
        REQUIRE( KeyValue_SetString( pChunk->store.pDocument, pTag, SV( name ) ) );
        pAttributes->records.pData[pBrush->sides.pData[i].iAttributeIndex].material.value = ref;
    }
}

void CheckSubtractionMetadata( map_document_t &map, const vector_t<u64> &roots, u64 owner, u32 layer )
{
    usize cutterFaces = 0, targetFaces = 0;
    for ( usize i = 0; i < roots.nCount; ++i ) {
        const u64 id = roots.pData[i]; CHECK( Placement( map, id ).owner == owner ); CHECK( Placement( map, id ).iLayer == layer );
        const auto *pRecord = MapDocument_FindObject( &map, id, nullptr ); REQUIRE( pRecord );
        string_view_t name{}; REQUIRE( KeyValue_GetString( KeyValue_Find( pRecord, SV( "name" ) ), &name ) );
        CHECK( StringView_Equals( name, SV( "Named authored shell" ) ) );
        u64 marker = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( pRecord, SV( "future_root" ) ), &marker ) ); CHECK( marker == 54321u );
        const auto *pBrush = Brush( map, id );
        for ( usize j = 0; j < pBrush->sides.nCount; ++j ) {
            const auto *pFace = Face( pRecord, pBrush->sides.pData[j].sourceId.value ); REQUIRE( pFace );
            string_view_t operand{}; REQUIRE( KeyValue_GetString( KeyValue_Find( pFace, SV( "future_operand" ) ), &operand ) );
            const bool cutter = StringView_Equals( operand, SV( "cutter" ) );
            CHECK( ( cutter || StringView_Equals( operand, SV( "target" ) ) ) );
            cutter ? ++cutterFaces : ++targetFaces;
            REQUIRE( KeyValue_GetU64( KeyValue_Find( pFace, SV( "future_side" ) ), &marker ) ); REQUIRE( marker >= 1u ); REQUIRE( marker <= 6u );
            const auto attributes = Attributes( map, id, j );
            CHECK( StringView_Equals( MapMaterials_Path( &map.materials, attributes.material.value ),
                SV( cutter ? "materials/cutter.cymat" : "materials/target.cymat" ) ) );
            CHECK( attributes.uvProjection.worldUnitsPerUv.x == Catch::Approx( 64.0 + 16.0 * static_cast<double>( marker - 1u ) ) );
            CHECK( attributes.uvProjection.offset.x == Catch::Approx( 0.125 * static_cast<double>( marker - 1u ) ) );
        }
    }
    CHECK( cutterFaces > 0 ); CHECK( targetFaces > 0 );
}

} // namespace

TEST_CASE( "Map brush hollow produces a valid convex shell with fresh unique identities", "[map][brush-edit]" )
{
    map_document_t original; Create( original ); const u64 id = Box( original );
    const auto revision = original.geometry.revision; const u64 next = original.nextId;
    map_document_t *pCopy = nullptr; REQUIRE( MapEdit_Clone( &original, &pCopy ) == map_status_t::OK );
    std::unique_ptr<map_document_t> copy( pCopy ); vector_t<u64> roots; REQUIRE( Vector_Init( &roots, copy->pAllocator ) );
    REQUIRE( MapBrushEdit_CanEdit( copy.get(), { &id, 1u } ) );
    REQUIRE( MapBrushEdit_Hollow( copy.get(), { &id, 1u }, 16.0, &roots ) == map_status_t::OK );
    REQUIRE( roots.nCount == 6u ); CHECK( copy->geometry.brushes.nCount == 6u );
    CHECK( !geo::GeometryDocument_FindBrush( &copy->geometry, { id } ) );
    CHECK( copy->geometry.revision == revision + 1u ); CHECK( original.geometry.revision == revision );
    CHECK( original.nextId == next ); CHECK( original.geometry.brushes.nCount == 1u );
    std::set<u64> identities;
    double totalVolume = 0.0;
    for ( usize i = 0u; i < roots.nCount; ++i ) {
        const auto *pBrush = Brush( *copy, roots.pData[i] ); REQUIRE( pBrush->sourceId.value >= next );
        CHECK( identities.insert( pBrush->sourceId.value ).second );
        CHECK( geo::BrushValidation_Deep( pBrush, copy->geometryPolicy, copy->pAllocator ).bWatertight );
        bool containsOrigin = true;
        for ( usize k = 0u; k < pBrush->sides.nCount; ++k ) {
            CHECK( identities.insert( pBrush->sides.pData[k].sourceId.value ).second );
            containsOrigin = containsOrigin && pBrush->sides.pData[k].plane.d <= 0.0;
            CHECK( StringView_Equals( MapMaterials_Path( &copy->materials, Attributes( *copy, roots.pData[i], k ).material.value ), SV( "materials/original.cymat" ) ) );
        }
        CHECK_FALSE( containsOrigin );
        const auto b = Bounds( *copy, roots.pData[i] ).box;
        totalVolume += ( b.maximum.x - b.minimum.x ) * ( b.maximum.y - b.minimum.y ) * ( b.maximum.z - b.minimum.z );
    }
    CHECK( totalVolume == Catch::Approx( 128.0 * 128.0 * 128.0 - 96.0 * 96.0 * 96.0 ) );
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &copy->geometry.sourceIds ) );
}

TEST_CASE( "Hollow fragments inherit layer ownership and retained face metadata through exact provenance", "[map][brush-edit]" )
{
    map_document_t original; Create( original ); const u64 id = Box( original ); u64 owner = 0u;
    REQUIRE( MapDocument_AddLayer( &original, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( &original, id, SV( "detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( &original, SV( "detail" ), SV( "func_shell" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &original, id, owner ) == map_status_t::OK );
    saved_t initial; REQUIRE( MapDocument_Save( &original, initial.Sink() ) == map_status_t::OK );
    AddMetadata( original, id ); const u32 layer = Placement( original, id ).iLayer;
    map_document_t *pCopy = nullptr; REQUIRE( MapEdit_Clone( &original, &pCopy ) == map_status_t::OK );
    std::unique_ptr<map_document_t> copy( pCopy ); vector_t<u64> roots; REQUIRE( Vector_Init( &roots, copy->pAllocator ) );
    REQUIRE( MapBrushEdit_Hollow( copy.get(), { &id, 1u }, 16.0, &roots ) == map_status_t::OK );
    for ( usize i = 0u; i < roots.nCount; ++i ) { CheckMetadata( *copy, roots.pData[i], owner, layer ); }
    CHECK( MapDocument_FindObject( copy.get(), id, nullptr ) == nullptr );
    CHECK( MapDocument_FindObject( &original, id, nullptr ) != nullptr );
    saved_t saved; REQUIRE( MapDocument_Save( copy.get(), saved.Sink() ) == map_status_t::OK );
    map_document_t loaded; Load( loaded, saved ); CHECK( loaded.geometry.brushes.nCount == 6u );
    for ( usize i = 0u; i < roots.nCount; ++i ) { CheckMetadata( loaded, roots.pData[i], owner, layer ); }
    REQUIRE( MapEdit_Delete( &loaded, { &owner, 1u } ) == map_status_t::OK ); CHECK( loaded.geometry.brushes.nCount == 0u );
}

TEST_CASE( "Plane clips keep original identities and surface records while assigning a new cut face", "[map][brush-edit]" )
{
    map_document_t map; Create( map ); const u64 id = Box( map );
    saved_t initial; REQUIRE( MapDocument_Save( &map, initial.Sink() ) == map_status_t::OK ); AddMetadata( map, id );
    const auto *pOriginal = Brush( map, id ); std::set<u64> oldIds;
    for ( usize i = 0u; i < pOriginal->sides.nCount; ++i ) { oldIds.insert( pOriginal->sides.pData[i].sourceId.value ); }
    const u64 revision = map.geometry.revision; vector_t<u64> roots; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
    REQUIRE( Vector_PushBack( &roots, u64{ 999u } ) ); bool_t changed = CY_FALSE;
    REQUIRE( MapBrushEdit_Clip( &map, { &id, 1u }, { { 1, 0, 0 }, 0 }, SV( "materials/cut.cymat" ), &roots, &changed ) == map_status_t::OK );
    CHECK( changed ); REQUIRE( roots.nCount == 1u ); CHECK( roots.pData[0] == id ); CHECK( map.geometry.revision == revision + 1u );
    CHECK( Bounds( map, id ).box.minimum.x == Catch::Approx( -64.0 ) ); CHECK( Bounds( map, id ).box.maximum.x == Catch::Approx( 0.0 ) );
    const auto *pBrush = Brush( map, id ); const auto *pRecord = MapDocument_FindObject( &map, id, nullptr ); REQUIRE( pRecord );
    usize newSides = 0u;
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        const auto &side = pBrush->sides.pData[i]; const auto attributes = Attributes( map, id, i );
        if ( oldIds.contains( side.sourceId.value ) ) {
            const auto *pFace = Face( pRecord, side.sourceId.value ); REQUIRE( pFace ); u64 marker = 0u;
            REQUIRE( KeyValue_GetU64( KeyValue_Find( pFace, SV( "future_side" ) ), &marker ) );
            CHECK( attributes.uvProjection.worldUnitsPerUv.x == Catch::Approx( 64.0 + 16.0 * static_cast<double>( marker - 1u ) ) );
            CHECK( StringView_Equals( MapMaterials_Path( &map.materials, attributes.material.value ), SV( "materials/original.cymat" ) ) );
        } else {
            ++newSides; CHECK( side.plane.normal.x == 1.0 ); CHECK( side.plane.d == 0.0 );
            CHECK( StringView_Equals( MapMaterials_Path( &map.materials, attributes.material.value ), SV( "materials/cut.cymat" ) ) );
            CHECK( attributes.uvProjection.normal.x == 1.0 ); CHECK( attributes.uvProjection.worldUnitsPerUv.x == 128.0 );
        }
    }
    CHECK( newSides == 1u ); CHECK( geo::BrushValidation_Deep( pBrush, map.geometryPolicy, map.pAllocator ).bWatertight );
    saved_t saved; REQUIRE( MapDocument_Save( &map, saved.Sink() ) == map_status_t::OK );
    map_document_t loaded; Load( loaded, saved ); pBrush = Brush( loaded, id ); pRecord = MapDocument_FindObject( &loaded, id, nullptr ); REQUIRE( pRecord );
    CHECK( KeyValue_ChildCount( KeyValue_Find( pRecord, SV( "faces" ) ) ) == 5u );
    CHECK( Bounds( loaded, id ).box.maximum.x == Catch::Approx( 0.0 ) );
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        if ( !oldIds.contains( pBrush->sides.pData[i].sourceId.value ) ) {
            CHECK( StringView_Equals( MapMaterials_Path( &loaded.materials, Attributes( loaded, id, i ).material.value ), SV( "materials/cut.cymat" ) ) );
        }
    }
}

TEST_CASE( "Brush operations reject unsupported selections and distinguish no-op clipping", "[map][brush-edit]" )
{
    map_document_t map; Create( map ); const u64 id = Box( map ); vector_t<u64> roots; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
    const auto next = map.nextId; const auto revision = map.geometry.revision; const auto materialCount = map.materials.entries.nCount;
    bool_t changed = CY_TRUE;
    REQUIRE( MapBrushEdit_Clip( &map, { &id, 1u }, { { 1, 0, 0 }, -128 }, SV( "materials/unused.cymat" ), &roots, &changed ) == map_status_t::OK );
    CHECK_FALSE( changed ); CHECK( map.nextId == next ); CHECK( map.geometry.revision == revision ); CHECK( map.materials.entries.nCount == materialCount );
    CHECK( MapBrushEdit_Clip( &map, { &id, 1u }, { { 1, 0, 0 }, 128 }, {}, &roots ) != map_status_t::OK );
    CHECK( MapBrushEdit_Clip( &map, { &id, 1u }, { { 2, 0, 0 }, 0 }, {}, &roots ) != map_status_t::OK );
    CHECK( MapBrushEdit_Hollow( &map, { &id, 1u }, 64.0, &roots ) != map_status_t::OK );
    CHECK( MapBrushEdit_Hollow( &map, { &id, 1u }, 0.0, &roots ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapBrushEdit_Hollow( &map, { &id, 1u }, std::numeric_limits<double>::quiet_NaN(), &roots ) == map_status_t::INVALID_ARGUMENT );
    const u64 duplicates[]{ id, id }; CHECK_FALSE( MapBrushEdit_CanEdit( &map, { duplicates, 2u } ) );
    u64 entity = 0u; REQUIRE( MapDocument_AddEntity( &map, SV( "default" ), SV( "info_null" ), {}, &entity ) == map_status_t::OK );
    CHECK_FALSE( MapBrushEdit_CanEdit( &map, { &entity, 1u } ) ); CHECK( MapBrushEdit_Hollow( &map, { &entity, 1u }, 16.0, &roots ) == map_status_t::UNKNOWN_OBJECT );
    map.bReadOnly = CY_TRUE; CHECK_FALSE( MapBrushEdit_CanEdit( &map, { &id, 1u } ) );
    CHECK( MapBrushEdit_Hollow( &map, { &id, 1u }, 16.0, &roots ) == map_status_t::READ_ONLY );
    CHECK( Bounds( map, id ).box.maximum.x == Catch::Approx( 64.0 ) );
}

TEST_CASE( "Failed mixed brush batches are discarded without changing the source or result selection", "[map][brush-edit]" )
{
    map_document_t original; Create( original ); const u64 large = Box( original ), small = Box( original, { { 128, 128, 128 }, { 144, 144, 144 } } );
    const u64 ids[]{ large, small }; const auto next = original.nextId; const auto revision = original.geometry.revision;
    map_document_t *pCopy = nullptr; REQUIRE( MapEdit_Clone( &original, &pCopy ) == map_status_t::OK );
    std::unique_ptr<map_document_t> copy( pCopy ); vector_t<u64> roots; REQUIRE( Vector_Init( &roots, copy->pAllocator ) ); REQUIRE( Vector_PushBack( &roots, large ) );
    CHECK( MapBrushEdit_Hollow( copy.get(), { ids, 2u }, 16.0, &roots ) != map_status_t::OK );
    REQUIRE( roots.nCount == 1u ); CHECK( roots.pData[0] == large ); copy.reset();
    CHECK( original.geometry.brushes.nCount == 2u ); CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision );
    CHECK( Bounds( original, large ).box.maximum.x == Catch::Approx( 64.0 ) );
    pCopy = nullptr; REQUIRE( MapEdit_Clone( &original, &pCopy ) == map_status_t::OK ); copy.reset( pCopy );
    bool_t changed = CY_FALSE;
    CHECK( MapBrushEdit_Clip( copy.get(), { ids, 2u }, { { 1, 0, 0 }, 0 }, {}, &roots, &changed ) != map_status_t::OK );
    CHECK_FALSE( changed ); copy.reset(); CHECK( Bounds( original, large ).box.maximum.x == Catch::Approx( 64.0 ) );
}

TEST_CASE( "Brush operation allocation failures release owned temporary geometry", "[map][brush-edit]" )
{
    audit_t audit;
    {
        map_document_t original; Create( original, &audit.allocator ); const u64 id = Box( original ); const auto baseline = audit.bytes;
        usize allocations = 0u;
        {
            map_document_t *pCopy = nullptr; REQUIRE( MapEdit_Clone( &original, &pCopy ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( pCopy );
            vector_t<u64> roots; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); const auto callsBefore = audit.calls;
            REQUIRE( MapBrushEdit_Hollow( copy.get(), { &id, 1u }, 16.0, &roots ) == map_status_t::OK ); allocations = audit.calls - callsBefore;
        }
        REQUIRE( audit.bytes == baseline ); REQUIRE( allocations > 16u );
        for ( usize failure : { usize{ 0u }, usize{ 1u }, usize{ 8u }, allocations / 4u, allocations / 2u, allocations - 1u } ) {
            CAPTURE( failure );
            {
                map_document_t *pCopy = nullptr; REQUIRE( MapEdit_Clone( &original, &pCopy ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( pCopy );
                vector_t<u64> roots; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); audit.failAt = audit.calls + failure;
                CHECK( MapBrushEdit_Hollow( copy.get(), { &id, 1u }, 16.0, &roots ) != map_status_t::OK ); audit.failAt = CY_USIZE_MAX;
            }
            CHECK( audit.bytes == baseline ); CHECK( original.geometry.brushes.nCount == 1u );
        }
    }
    CHECK( audit.bytes == 0u );
}

TEST_CASE( "Brush subtraction produces the exact remaining volume with fresh identities and keeps its cutter", "[map][brush-edit][subtract]" )
{
    map_document_t original; Create( original ); const u64 target = Box( original );
    const u64 cutter = Box( original, { { -32, -32, -32 }, { 32, 32, 32 } } );
    const auto next = original.nextId, revision = original.geometry.revision;
    std::set<u64> oldIds;
    for ( u64 id : { target, cutter } ) { oldIds.insert( id ); const auto *pBrush = Brush( original, id ); for ( usize i = 0; i < pBrush->sides.nCount; ++i ) { oldIds.insert( pBrush->sides.pData[i].sourceId.value ); } }
    map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, copy->pAllocator ) ); bool_t changed = CY_FALSE;
    REQUIRE( MapBrushEdit_Subtract( copy.get(), { &target, 1 }, cutter, &roots, &changed ) == map_status_t::OK );
    CHECK( changed ); REQUIRE( roots.nCount == 6 ); CHECK( copy->geometry.brushes.nCount == 7 );
    CHECK( !geo::GeometryDocument_FindBrush( &copy->geometry, { target } ) ); CHECK( copy->geometry.revision == revision + 1 );
    const auto *before = Brush( original, cutter ), *after = Brush( *copy, cutter );
    REQUIRE( before->sides.nCount == after->sides.nCount );
    for ( usize i = 0; i < before->sides.nCount; ++i ) { CHECK( before->sides.pData[i].sourceId.value == after->sides.pData[i].sourceId.value ); CHECK( before->sides.pData[i].plane.d == after->sides.pData[i].plane.d ); }
    std::set<u64> fresh; f64 totalVolume = 0;
    for ( usize i = 0; i < roots.nCount; ++i ) {
        const auto *pBrush = Brush( *copy, roots.pData[i] ); CHECK( roots.pData[i] >= next ); CHECK_FALSE( oldIds.contains( roots.pData[i] ) ); CHECK( fresh.insert( roots.pData[i] ).second );
        const auto valid = geo::BrushValidation_Deep( pBrush, copy->geometryPolicy, copy->pAllocator ); CHECK( valid.status == geo::geometry_status_t::OK ); CHECK( valid.bWatertight );
        for ( usize j = 0; j < pBrush->sides.nCount; ++j ) { const auto sideId = pBrush->sides.pData[j].sourceId.value; CHECK_FALSE( oldIds.contains( sideId ) ); CHECK( fresh.insert( sideId ).second ); }
        const auto b = Bounds( *copy, roots.pData[i] ).box; totalVolume += ( b.maximum.x - b.minimum.x ) * ( b.maximum.y - b.minimum.y ) * ( b.maximum.z - b.minimum.z );
    }
    CHECK( totalVolume == Catch::Approx( 128.0 * 128.0 * 128.0 - 64.0 * 64.0 * 64.0 ) );
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &copy->geometry.sourceIds ) );
    CHECK( original.geometry.brushes.nCount == 2 ); CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision );
    // A productive intersection at an outer boundary produces one clean
    // fragment, rather than the six walls of an internal subtraction.
    map_document_t edge; Create( edge ); const u64 edgeTarget = Box( edge ); const u64 edgeCutter = Box( edge, { { 0, -128, -128 }, { 128, 128, 128 } } );
    REQUIRE( MapBrushEdit_Subtract( &edge, { &edgeTarget, 1 }, edgeCutter, &roots ) == map_status_t::OK );
    REQUIRE( roots.nCount == 1 ); CHECK( Bounds( edge, roots.pData[0] ).box.maximum.x == Catch::Approx( 0 ) );
    CHECK( Bounds( edge, roots.pData[0] ).box.minimum.x == Catch::Approx( -64 ) ); CHECK( Brush( edge, edgeCutter ) != nullptr );
}

TEST_CASE( "Subtraction adopts target ownership and exact target or cutter surface ancestry through persistence", "[map][brush-edit][subtract]" )
{
    map_document_t original; Create( original ); const u64 target = Box( original ); const u64 cutter = Box( original, { { -32, -32, -32 }, { 32, 32, 32 } } ); u64 owner = 0;
    REQUIRE( MapDocument_AddLayer( &original, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( &original, target, SV( "detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( &original, SV( "detail" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &original, target, owner ) == map_status_t::OK );
    saved_t initial; REQUIRE( MapDocument_Save( &original, initial.Sink() ) == map_status_t::OK );
    AddMetadata( original, target ); AddMetadata( original, cutter ); TagOperand( original, target, "target", "materials/target.cymat" ); TagOperand( original, cutter, "cutter", "materials/cutter.cymat" );
    // Cutter root metadata must not leak onto the replacement target roots.
    map_chunk_t *cutterChunk = nullptr; auto *cutterRecord = MapDocument_FindObject( &original, cutter, &cutterChunk ); REQUIRE( cutterRecord ); REQUIRE( cutterChunk );
    REQUIRE( KeyValue_SetU64( cutterChunk->store.pDocument, KeyValue_Find( cutterRecord, SV( "future_root" ) ), 99999u ) );
    const u32 layer = Placement( original, target ).iLayer; map_document_t *raw = nullptr;
    REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, copy->pAllocator ) );
    REQUIRE( MapBrushEdit_Subtract( copy.get(), { &target, 1 }, cutter, &roots ) == map_status_t::OK );
    CheckSubtractionMetadata( *copy, roots, owner, layer ); CHECK( Placement( *copy, cutter ).owner == 0 );
    saved_t saved; REQUIRE( MapDocument_Save( copy.get(), saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
    CheckSubtractionMetadata( loaded, roots, owner, layer );
    u64 marker = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( MapDocument_FindObject( &loaded, cutter, nullptr ), SV( "future_root" ) ), &marker ) ); CHECK( marker == 99999u );
    REQUIRE( MapEdit_Delete( &loaded, { &owner, 1 } ) == map_status_t::OK );
    CHECK( loaded.geometry.brushes.nCount == 1 ); CHECK( Brush( loaded, cutter ) != nullptr );
}

TEST_CASE( "Subtraction preserves disjoint and touching targets and deletes completely covered targets", "[map][brush-edit][subtract]" )
{
    map_document_t m; Create( m ); const u64 cutter = Box( m );
    const u64 disjoint = Box( m, { { 256, -32, -32 }, { 320, 32, 32 } } );
    const u64 touching = Box( m, { { 64, -32, -32 }, { 128, 32, 32 } } );
    const u64 noops[]{ disjoint, touching }; const auto revision = m.geometry.revision, next = m.nextId;
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); bool_t changed = CY_TRUE;
    REQUIRE( MapBrushEdit_Subtract( &m, { noops, 2 }, cutter, &roots, &changed ) == map_status_t::OK );
    CHECK_FALSE( changed ); CHECK( m.geometry.revision == revision ); CHECK( m.nextId == next );
    REQUIRE( roots.nCount == 2 ); CHECK( roots.pData[0] == disjoint ); CHECK( roots.pData[1] == touching ); CHECK( m.geometry.brushes.nCount == 3 );
    const u64 inside = Box( m, { { -16, -16, -16 }, { 16, 16, 16 } } ); const u64 identical = Box( m );
    saved_t saved; REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK );
    const u64 covered[]{ inside, identical }; const auto beforeRemove = m.geometry.revision, nextBefore = m.nextId; changed = CY_FALSE;
    REQUIRE( MapBrushEdit_Subtract( &m, { covered, 2 }, cutter, &roots, &changed ) == map_status_t::OK );
    CHECK( changed ); CHECK( roots.nCount == 0 ); CHECK( m.geometry.brushes.nCount == 3 ); CHECK( m.nextId == nextBefore ); CHECK( m.geometry.revision == beforeRemove + 1 );
    CHECK( MapDocument_FindObject( &m, inside, nullptr ) == nullptr ); CHECK( MapDocument_FindObject( &m, identical, nullptr ) == nullptr );
    CHECK( Brush( m, cutter ) != nullptr );
    REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved ); CHECK( loaded.geometry.brushes.nCount == 3 );
    CHECK( !geo::GeometryDocument_FindBrush( &loaded.geometry, { inside } ) ); CHECK( !geo::GeometryDocument_FindBrush( &loaded.geometry, { identical } ) );
}

TEST_CASE( "Invalid subtraction preserves output selection and changed state", "[map][brush-edit][subtract]" )
{
    map_document_t m; Create( m ); const u64 target = Box( m ), cutter = Box( m, { { -32, -32, -32 }, { 32, 32, 32 } } );
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) ); bool_t changed = CY_TRUE;
    const auto check = [&]( span_t<const u64> ids, u64 cut, map_status_t expected ) {
        CHECK( MapBrushEdit_Subtract( &m, ids, cut, &roots, &changed ) == expected ); REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); CHECK( changed );
    };
    check( { &target, 1 }, target, map_status_t::INVALID_ARGUMENT ); check( {}, cutter, map_status_t::INVALID_ARGUMENT );
    const u64 duplicates[]{ target, target }; check( { duplicates, 2 }, cutter, map_status_t::INVALID_ARGUMENT );
    check( { &target, 1 }, 98765, map_status_t::UNKNOWN_OBJECT );
    u64 entity = 0; REQUIRE( MapDocument_AddEntity( &m, SV( "default" ), SV( "info_null" ), {}, &entity ) == map_status_t::OK );
    check( { &target, 1 }, entity, map_status_t::UNKNOWN_OBJECT ); check( { &entity, 1 }, cutter, map_status_t::UNKNOWN_OBJECT );
    m.bReadOnly = CY_TRUE; check( { &target, 1 }, cutter, map_status_t::READ_ONLY ); m.bReadOnly = CY_FALSE;
    vector_t<u64> uninitialized{}; CHECK( MapBrushEdit_Subtract( &m, { &target, 1 }, cutter, &uninitialized ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapBrushEdit_Hollow( &m, { &target, 1 }, 16.0, &uninitialized ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapBrushEdit_Clip( &m, { &target, 1 }, { { 1, 0, 0 }, 0 }, {}, &uninitialized ) == map_status_t::INVALID_ARGUMENT );
    CHECK( m.geometry.brushes.nCount == 2 );
}

TEST_CASE( "Subtraction allocation failures release adopted geometry and retained metadata without publishing a batch", "[map][brush-edit][subtract][allocation]" )
{
    audit_t audit;
    for ( bool unsavedTarget : { false, true } ) {
        CAPTURE( unsavedTarget );
        map_document_t original; Create( original, &audit.allocator ); u64 target = unsavedTarget ? 0 : Box( original );
        const u64 cutter = Box( original, { { 0, -128, -128 }, { 128, 128, 128 } } );
        const u64 covered = Box( original, { { 16, -16, -16 }, { 32, 16, 16 } } );
        saved_t saved; REQUIRE( MapDocument_Save( &original, saved.Sink() ) == map_status_t::OK );
        if ( unsavedTarget ) { target = Box( original ); } else { AddMetadata( original, target ); }
        AddMetadata( original, cutter );
        const u64 ids[]{ covered, target }; const usize baseline = audit.bytes; const auto next = original.nextId, revision = original.geometry.revision;
        usize allocations = 0;
        {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
            vector_t<u64> roots; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); audit.calls = 0;
            REQUIRE( MapBrushEdit_Subtract( copy.get(), { ids, 2 }, cutter, &roots ) == map_status_t::OK ); allocations = audit.calls;
        }
        REQUIRE( audit.bytes == baseline ); REQUIRE( allocations > 16 );
        for ( usize failure = 0; failure < allocations; ++failure ) {
            CAPTURE( failure, allocations );
            {
                audit.failAt = CY_USIZE_MAX; map_document_t *raw = nullptr;
                REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                vector_t<u64> roots; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) );
                audit.calls = 0; audit.failAt = failure; bool_t changed = CY_FALSE;
                CHECK( MapBrushEdit_Subtract( copy.get(), { ids, 2 }, cutter, &roots, &changed ) == map_status_t::OUT_OF_MEMORY );
                REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); CHECK_FALSE( changed );
                audit.failAt = CY_USIZE_MAX;
            }
            CHECK( audit.bytes == baseline ); CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision );
            CHECK( original.geometry.brushes.nCount == 3 ); CHECK( MapDocument_FindObject( &original, covered, nullptr ) != nullptr );
        }
    }
    CHECK( audit.bytes == 0 );
}

TEST_CASE( "Unsaved subtraction targets receive minimal residual records on their own layer or owner", "[map][brush-edit][subtract]" )
{
    for ( bool owned : { false, true } ) {
        CAPTURE( owned );
        map_document_t m; Create( m ); const u64 cutter = Box( m, { { 0, -128, -128 }, { 128, 128, 128 } } );
        REQUIRE( MapDocument_AddLayer( &m, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
        saved_t saved; REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK );
        AddMetadata( m, cutter ); TagOperand( m, cutter, "cutter", "materials/cutter.cymat" );
        u64 owner = 0;
        if ( owned ) { REQUIRE( MapDocument_AddEntity( &m, SV( "detail" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK ); }
        const u64 target = Box( m ); REQUIRE( MapDocument_SetGeometryLayer( &m, target, SV( "detail" ) ) == map_status_t::OK );
        if ( owned ) { REQUIRE( MapDocument_SetGeometryOwner( &m, target, owner ) == map_status_t::OK ); }
        REQUIRE( MapDocument_FindObject( &m, target, nullptr ) == nullptr );
        const u32 layer = Placement( m, target ).iLayer; const usize chunksBefore = m.chunks.nCount;
        vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) );
        REQUIRE( MapBrushEdit_Subtract( &m, { &target, 1 }, cutter, &roots ) == map_status_t::OK ); REQUIRE( roots.nCount == 1 );
        const u64 fragment = roots.pData[0]; map_chunk_t *pChunk = nullptr;
        const auto *pRecord = MapDocument_FindObject( &m, fragment, &pChunk ); REQUIRE( pRecord ); REQUIRE( pChunk );
        CHECK( Placement( m, fragment ).owner == owner ); CHECK( Placement( m, fragment ).iLayer == layer ); CHECK( StringView_Equals( SV( pChunk->layer ), SV( "detail" ) ) );
        CHECK( KeyValue_Find( pRecord, SV( "name" ) ) == nullptr ); CHECK( KeyValue_Find( pRecord, SV( "future_root" ) ) == nullptr );
        CHECK( m.chunks.nCount == chunksBefore + ( owned ? 0u : 1u ) );
        usize preserved = 0;
        const auto check = [&]( const map_document_t &map, const key_value_t *record ) {
            const auto *brush = Brush( map, fragment );
            for ( usize i = 0; i < brush->sides.nCount; ++i ) {
                const auto *face = Face( record, brush->sides.pData[i].sourceId.value );
                if ( !face ) { continue; }
                string_view_t tag{}; REQUIRE( KeyValue_GetString( KeyValue_Find( face, SV( "future_operand" ) ), &tag ) ); CHECK( StringView_Equals( tag, SV( "cutter" ) ) );
                u64 marker = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( face, SV( "future_side" ) ), &marker ) ); REQUIRE( marker >= 1 ); REQUIRE( marker <= 6 );
                const auto attributes = Attributes( map, fragment, i );
                CHECK( StringView_Equals( MapMaterials_Path( &map.materials, attributes.material.value ), SV( "materials/cutter.cymat" ) ) );
                CHECK( attributes.uvProjection.worldUnitsPerUv.x == Catch::Approx( 64.0 + 16.0 * static_cast<double>( marker - 1 ) ) ); ++preserved;
            }
        };
        check( m, pRecord ); REQUIRE( preserved > 0 );
        if ( owned ) {
            const auto *nested = KeyValue_Find( MapDocument_FindObject( &m, owner, nullptr ), SV( "brushes" ) );
            REQUIRE( KeyValue_ChildCount( nested ) == 1 ); CHECK( KeyValue_ChildAt( nested, 0 ) == pRecord );
        }
        REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
        pRecord = MapDocument_FindObject( &loaded, fragment, nullptr ); REQUIRE( pRecord ); check( loaded, pRecord );
        CHECK( Placement( loaded, fragment ).owner == owner ); CHECK( Placement( loaded, fragment ).iLayer == layer );
    }
    // A retained cutter with no unknown face data must not cause record or
    // working-chunk growth for an otherwise unsaved target.
    map_document_t m; Create( m ); const u64 cutter = Box( m, { { 0, -128, -128 }, { 128, 128, 128 } } );
    saved_t saved; REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK );
    const u64 target = Box( m ); const usize chunksBefore = m.chunks.nCount;
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) );
    REQUIRE( MapBrushEdit_Subtract( &m, { &target, 1 }, cutter, &roots ) == map_status_t::OK );
    REQUIRE( roots.nCount == 1 ); CHECK( MapDocument_FindObject( &m, roots.pData[0], nullptr ) == nullptr ); CHECK( m.chunks.nCount == chunksBefore );
}

TEST_CASE( "Brush Boolean adoption respects the map face limit under a permissive backend policy", "[map][brush-edit][subtract][limit]" )
{
    map_document_t m; Create( m ); m.geometryPolicy.limits.cBrushSidesPerBrushMax = 512; m.geometry.policy = m.geometryPolicy;
    geo::geometry_source_id_allocator_t allocator{ { m.nextId } }; geo::brush_solid_t solid{}; geo::brush_source_t source{};
    // A 254-sided prism has exactly the format's 256 faces. Cutting just its
    // extreme polygon corner adds a new side while retaining every old side,
    // so the valid backend result has 257 faces and cannot be serialized.
    REQUIRE( geo::BrushGenerator_TryMakePrism( &solid, m.pAllocator, m.geometryPolicy, &allocator, {}, 128.0, 64.0, 254, 2 ) == geo::geometry_status_t::OK );
    const u64 target = solid.sourceId.value;
    REQUIRE( geo::BrushSource_TryBuildDefault( &solid, m.pAllocator, m.geometryPolicy, &source ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddBrushSource( &m.geometry, &source ) == geo::geometry_status_t::OK ); m.nextId = allocator.next.value;
    REQUIRE( MapDocument_SetGeometryLayer( &m, target, SV( "default" ) ) == map_status_t::OK );
    geo::BrushSource_Shutdown( &source ); geo::BrushSolid_Shutdown( &solid );
    const u64 cutter = Box( m, { { 127.99, -256, -256 }, { 256, 256, 256 } } );
    const auto next = m.nextId, revision = m.geometry.revision;
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) ); bool_t changed = CY_FALSE;
    CHECK( MapBrushEdit_ClipMode( &m, { &target, 1 }, { { 1, 0, 0 }, -127.99 }, map_brush_clip_mode_t::BOTH, {}, &roots, &changed ) == map_status_t::LIMIT_EXCEEDED );
    REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); CHECK_FALSE( changed );
    CHECK( m.geometry.brushes.nCount == 2 ); CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision );
    CHECK( MapBrushEdit_Subtract( &m, { &target, 1 }, cutter, &roots, &changed ) == map_status_t::LIMIT_EXCEEDED );
    REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); CHECK_FALSE( changed );
    CHECK( m.geometry.brushes.nCount == 2 ); CHECK( Brush( m, target )->sides.nCount == MAP_BRUSH_FACES_MAX );
    CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision );
}

TEST_CASE( "Front and back clipping preserve authored identities and independent cut surfaces", "[map][brush-edit][clip-mode]" )
{
    for ( const auto mode : { map_brush_clip_mode_t::BACK, map_brush_clip_mode_t::FRONT } ) {
        CAPTURE( static_cast<int>( mode ) );
        map_document_t m; Create( m ); const u64 id = Box( m );
        saved_t saved; REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); AddMetadata( m, id );
        std::set<u64> inherited;
        for ( usize i = 0; i < Brush( m, id )->sides.nCount; ++i ) { inherited.insert( Brush( m, id )->sides.pData[i].sourceId.value ); }
        const u64 next = m.nextId, revision = m.geometry.revision;
        vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); bool_t changed = CY_FALSE;
        REQUIRE( MapBrushEdit_ClipMode( &m, { &id, 1 }, { { 1, 0, 0 }, 0 }, mode, SV( "materials/cut.cymat" ), &roots, &changed ) == map_status_t::OK );
        REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == id ); CHECK( changed ); CHECK( m.geometry.revision == revision + 1 );
        const bool front = mode == map_brush_clip_mode_t::FRONT;
        const auto b = Bounds( m, id ).box;
        CHECK( b.minimum.x == Catch::Approx( front ? 0.0 : -64.0 ) ); CHECK( b.maximum.x == Catch::Approx( front ? 64.0 : 0.0 ) );
        const auto checkSurfaces = [&]( map_document_t &map ) {
            const auto *brush = Brush( map, id ); const auto *record = MapDocument_FindObject( &map, id, nullptr ); REQUIRE( record );
            REQUIRE( brush->sides.nCount == 6 ); usize cuts = 0;
            for ( usize i = 0; i < brush->sides.nCount; ++i ) {
                const auto &side = brush->sides.pData[i]; const auto attributes = Attributes( map, id, i );
                if ( inherited.contains( side.sourceId.value ) ) {
                    const auto *face = Face( record, side.sourceId.value ); REQUIRE( face ); u64 marker = 0;
                    REQUIRE( KeyValue_GetU64( KeyValue_Find( face, SV( "future_side" ) ), &marker ) ); REQUIRE( marker >= 1 ); REQUIRE( marker <= 6 );
                    CHECK( attributes.uvProjection.worldUnitsPerUv.x == Catch::Approx( 64.0 + 16.0 * static_cast<double>( marker - 1 ) ) );
                    CHECK( attributes.uvProjection.offset.x == Catch::Approx( 0.125 * static_cast<double>( marker - 1 ) ) );
                    CHECK( StringView_Equals( MapMaterials_Path( &map.materials, attributes.material.value ), SV( "materials/original.cymat" ) ) );
                } else {
                    ++cuts; CHECK( side.sourceId.value >= next ); CHECK( side.plane.normal.x == ( front ? -1.0 : 1.0 ) );
                    CHECK( side.plane.d == 0.0 ); CHECK( attributes.uvProjection.normal.x == side.plane.normal.x );
                    CHECK( attributes.uvProjection.worldUnitsPerUv.x == 128.0 ); CHECK( Face( record, side.sourceId.value ) == nullptr );
                    CHECK( StringView_Equals( MapMaterials_Path( &map.materials, attributes.material.value ), SV( "materials/cut.cymat" ) ) );
                }
            }
            CHECK( cuts == 1 ); CHECK( geo::BrushValidation_Deep( brush, map.geometryPolicy, map.pAllocator ).bWatertight );
        };
        checkSurfaces( m ); REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved ); checkSurfaces( loaded );
        CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &loaded.geometry.sourceIds ) );
    }
}

TEST_CASE( "Split clipping gives both halves fresh identities with exact metadata and shared surface ancestry", "[map][brush-edit][clip-mode]" )
{
    for ( bool sharedAttributes : { false, true } ) {
        CAPTURE( sharedAttributes );
        map_document_t m; Create( m ); const u64 id = Box( m ); u64 owner = 0;
        REQUIRE( MapDocument_AddLayer( &m, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryLayer( &m, id, SV( "detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_AddEntity( &m, SV( "detail" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( &m, id, owner ) == map_status_t::OK );
        saved_t saved; REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); AddMetadata( m, id );
        const u32 layer = Placement( m, id ).iLayer; const u64 next = m.nextId, revision = m.geometry.revision;
        std::set<u64> oldIds{ id }; std::map<u64, math::planed_t> oldPlanes;
        auto *original = geo::GeometryDocument_FindBrushMutable( &m.geometry, { id } ); REQUIRE( original );
        for ( usize i = 0; i < original->sides.nCount; ++i ) {
            oldIds.insert( original->sides.pData[i].sourceId.value ); oldPlanes[i + 1] = original->sides.pData[i].plane;
            if ( sharedAttributes ) { original->sides.pData[i].iAttributeIndex = 0; }
        }
        vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); bool_t changed = CY_FALSE;
        REQUIRE( MapBrushEdit_ClipMode( &m, { &id, 1 }, { { 1, 0, 0 }, 0 }, map_brush_clip_mode_t::BOTH, SV( "materials/cut.cymat" ), &roots, &changed ) == map_status_t::OK );
        CHECK( changed ); REQUIRE( roots.nCount == 2 ); CHECK( m.geometry.brushes.nCount == 2 ); CHECK( m.geometry.revision == revision + 1 );
        CHECK( geo::GeometryDocument_FindBrush( &m.geometry, { id } ) == nullptr ); CHECK( MapDocument_FindObject( &m, id, nullptr ) == nullptr );
        const auto checkHalves = [&]( map_document_t &map ) {
            std::set<u64> identities; std::map<u64, usize> uses; usize cuts = 0; double volume = 0;
            for ( usize i = 0; i < roots.nCount; ++i ) {
                const u64 root = roots.pData[i]; CHECK( root >= next ); CHECK( identities.insert( root ).second ); CHECK_FALSE( oldIds.contains( root ) );
                CHECK( Placement( map, root ).owner == owner ); CHECK( Placement( map, root ).iLayer == layer );
                const auto *record = MapDocument_FindObject( &map, root, nullptr ); REQUIRE( record );
                string_view_t name{}; REQUIRE( KeyValue_GetString( KeyValue_Find( record, SV( "name" ) ), &name ) ); CHECK( StringView_Equals( name, SV( "Named authored shell" ) ) );
                u64 marker = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( record, SV( "future_root" ) ), &marker ) ); CHECK( marker == 54321 );
                const auto b = Bounds( map, root ).box;
                CHECK( b.minimum.x == Catch::Approx( i == 0 ? -64.0 : 0.0 ) ); CHECK( b.maximum.x == Catch::Approx( i == 0 ? 0.0 : 64.0 ) );
                volume += ( b.maximum.x - b.minimum.x ) * ( b.maximum.y - b.minimum.y ) * ( b.maximum.z - b.minimum.z );
                const auto *brush = Brush( map, root ); REQUIRE( brush->sides.nCount == 6 ); CHECK( geo::BrushValidation_Deep( brush, map.geometryPolicy, map.pAllocator ).bWatertight );
                const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &map.geometry, { root } ); REQUIRE( attributes ); CHECK( attributes->records.nCount == 6 );
                for ( usize j = 0; j < brush->sides.nCount; ++j ) {
                    const auto &side = brush->sides.pData[j]; CHECK( side.sourceId.value >= next ); CHECK( identities.insert( side.sourceId.value ).second ); CHECK_FALSE( oldIds.contains( side.sourceId.value ) );
                    const auto surface = Attributes( map, root, j ); const auto *face = Face( record, side.sourceId.value );
                    if ( face ) {
                        REQUIRE( KeyValue_GetU64( KeyValue_Find( face, SV( "future_side" ) ), &marker ) ); REQUIRE( marker >= 1 ); REQUIRE( marker <= 6 ); ++uses[marker];
                        const auto plane = oldPlanes.at( marker ); CHECK( math::Vec3d_EqualsExact( side.plane.normal, plane.normal ) ); CHECK( side.plane.d == plane.d );
                        const u64 attributeMarker = sharedAttributes ? 1 : marker;
                        CHECK( surface.uvProjection.worldUnitsPerUv.x == Catch::Approx( 64.0 + 16.0 * static_cast<double>( attributeMarker - 1 ) ) );
                        CHECK( surface.uvProjection.offset.x == Catch::Approx( 0.125 * static_cast<double>( attributeMarker - 1 ) ) );
                        CHECK( StringView_Equals( MapMaterials_Path( &map.materials, surface.material.value ), SV( "materials/original.cymat" ) ) );
                    } else {
                        ++cuts; CHECK( side.plane.normal.x == ( i == 0 ? 1.0 : -1.0 ) ); CHECK( side.plane.d == 0.0 );
                        CHECK( surface.uvProjection.normal.x == side.plane.normal.x ); CHECK( surface.uvProjection.worldUnitsPerUv.x == 128.0 );
                        CHECK( StringView_Equals( MapMaterials_Path( &map.materials, surface.material.value ), SV( "materials/cut.cymat" ) ) );
                    }
                }
            }
            CHECK( cuts == 2 ); CHECK( volume == Catch::Approx( 128.0 * 128.0 * 128.0 ) );
            for ( const auto &[marker, plane] : oldPlanes ) { CHECK( uses[marker] == ( plane.normal.x == 0.0 ? 2u : 1u ) ); }
            CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
        };
        checkHalves( m ); REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved ); checkHalves( loaded );
        const auto *owned = KeyValue_Find( MapDocument_FindObject( &loaded, owner, nullptr ), SV( "brushes" ) ); CHECK( KeyValue_ChildCount( owned ) == 2 );
    }
}

TEST_CASE( "Clip modes retain exact no-ops and delete only wholly discarded brushes", "[map][brush-edit][clip-mode]" )
{
    for ( const auto mode : { map_brush_clip_mode_t::BACK, map_brush_clip_mode_t::FRONT, map_brush_clip_mode_t::BOTH } ) {
        CAPTURE( static_cast<int>( mode ) );
        map_document_t m; Create( m ); const u64 id = Box( m ); const auto next = m.nextId, revision = m.geometry.revision; const usize materials = m.materials.entries.nCount;
        vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); bool_t changed = CY_TRUE;
        const double outside = mode == map_brush_clip_mode_t::FRONT ? 128.0 : -128.0;
        REQUIRE( MapBrushEdit_ClipMode( &m, { &id, 1 }, { { 1, 0, 0 }, outside }, mode, SV( "materials/unused.cymat" ), &roots, &changed ) == map_status_t::OK );
        REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == id ); CHECK_FALSE( changed );
        CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision ); CHECK( m.materials.entries.nCount == materials );
        // Touching the boundary still leaves a complete nonzero solid.
        REQUIRE( MapBrushEdit_ClipMode( &m, { &id, 1 }, { { 1, 0, 0 }, outside > 0 ? 64.0 : -64.0 }, mode, {}, &roots, &changed ) == map_status_t::OK );
        CHECK_FALSE( changed ); CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision );
        if ( mode == map_brush_clip_mode_t::BOTH ) { continue; }
        saved_t saved; REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK );
        REQUIRE( MapBrushEdit_ClipMode( &m, { &id, 1 }, { { 1, 0, 0 }, -outside }, mode, SV( "materials/unused.cymat" ), &roots, &changed ) == map_status_t::OK );
        CHECK( changed ); CHECK( roots.nCount == 0 ); CHECK( m.geometry.brushes.nCount == 0 ); CHECK( MapDocument_FindObject( &m, id, nullptr ) == nullptr );
        CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision + 1 ); CHECK( m.materials.entries.nCount == materials );
        REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved ); CHECK( loaded.geometry.brushes.nCount == 0 );
    }
    map_document_t m; Create( m ); const u64 left = Box( m, { { -192, -64, -64 }, { -128, 64, 64 } } ), center = Box( m ), right = Box( m, { { 128, -64, -64 }, { 192, 64, 64 } } );
    const u64 ids[]{ left, center, right }; const auto revision = m.geometry.revision;
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) );
    REQUIRE( MapBrushEdit_ClipMode( &m, { ids, 3 }, { { 1, 0, 0 }, 0 }, map_brush_clip_mode_t::BACK, {}, &roots ) == map_status_t::OK );
    REQUIRE( roots.nCount == 2 ); CHECK( roots.pData[0] == left ); CHECK( roots.pData[1] == center ); CHECK( m.geometry.brushes.nCount == 2 ); CHECK( m.geometry.revision == revision + 1 );
    CHECK( Bounds( m, center ).box.maximum.x == 0.0 ); CHECK( geo::GeometryDocument_FindBrush( &m.geometry, { right } ) == nullptr );
}

TEST_CASE( "Clip modes accept bare preview sources and reject invalid output contracts", "[map][brush-edit][clip-mode]" )
{
    map_document_t live; Create( live ); const u64 id = Box( live ); map_document_t preview; Create( preview );
    geo::brush_source_t source{};
    REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &live.geometry, { id }, preview.pAllocator, &source ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddBrushSource( &preview.geometry, &source ) == geo::geometry_status_t::OK ); geo::BrushSource_Shutdown( &source ); preview.nextId = live.nextId;
    REQUIRE( preview.geometryRecords.nCount == 0 ); const usize chunks = preview.chunks.nCount;
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, preview.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) ); bool_t changed = CY_FALSE;
    vector_t<u64> uninitialized{};
    CHECK( MapBrushEdit_ClipMode( &preview, { &id, 1 }, { { 1, 0, 0 }, 0 }, map_brush_clip_mode_t::BOTH, {}, &uninitialized, &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapBrushEdit_ClipMode( &preview, { &id, 1 }, { { 1, 0, 0 }, 0 }, static_cast<map_brush_clip_mode_t>( 255 ), {}, &roots, &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapBrushEdit_ClipMode( &preview, { &id, 1 }, { { 2, 0, 0 }, 0 }, map_brush_clip_mode_t::BOTH, {}, &roots, &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapBrushEdit_ClipMode( &preview, { &id, 1 }, { { 1, 0, 0 }, 0 }, map_brush_clip_mode_t::BOTH, SV( "bad\\material" ), &roots, &changed ) == map_status_t::INVALID_ARGUMENT );
    const u64 duplicates[]{ id, id };
    CHECK( MapBrushEdit_ClipMode( &preview, { duplicates, 2 }, { { 1, 0, 0 }, 0 }, map_brush_clip_mode_t::BOTH, {}, &roots, &changed ) == map_status_t::INVALID_ARGUMENT );
    preview.bReadOnly = CY_TRUE;
    CHECK( MapBrushEdit_ClipMode( &preview, { &id, 1 }, { { 1, 0, 0 }, 0 }, map_brush_clip_mode_t::BOTH, {}, &roots, &changed ) == map_status_t::READ_ONLY ); preview.bReadOnly = CY_FALSE;
    REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); CHECK_FALSE( changed ); CHECK( preview.geometry.brushes.nCount == 1 );
    REQUIRE( MapBrushEdit_ClipMode( &preview, { &id, 1 }, { { 1, 0, 0 }, 0 }, map_brush_clip_mode_t::BOTH, {}, &roots, &changed ) == map_status_t::OK );
    REQUIRE( roots.nCount == 2 ); CHECK( changed ); CHECK( preview.chunks.nCount == chunks );
    for ( usize i = 0; i < roots.nCount; ++i ) { CHECK( MapDocument_FindObject( &preview, roots.pData[i], nullptr ) == nullptr ); CHECK( Brush( preview, roots.pData[i] )->sides.nCount == 6 ); }
    // Same bare-source construction also permits complete removal.
    const u64 halves[]{ roots.pData[0], roots.pData[1] };
    REQUIRE( MapBrushEdit_ClipMode( &preview, { halves, 2 }, { { 1, 0, 0 }, 128 }, map_brush_clip_mode_t::BACK, {}, &roots ) == map_status_t::OK );
    CHECK( roots.nCount == 0 ); CHECK( preview.geometry.brushes.nCount == 0 ); CHECK( live.geometry.brushes.nCount == 1 );
}

TEST_CASE( "Every split clipping allocation failure preserves external results and releases owned data", "[map][brush-edit][clip-mode][oom]" )
{
    audit_t audit;
    {
        map_document_t original; Create( original, &audit.allocator );
        const u64 discarded = Box( original, { { 128, -64, -64 }, { 192, 64, 64 } } ), id = Box( original );
        saved_t saved; REQUIRE( MapDocument_Save( &original, saved.Sink() ) == map_status_t::OK ); AddMetadata( original, id );
        const usize baseline = audit.bytes; const auto next = original.nextId, revision = original.geometry.revision;
        for ( const auto mode : { map_brush_clip_mode_t::BACK, map_brush_clip_mode_t::FRONT, map_brush_clip_mode_t::BOTH } ) {
            CAPTURE( static_cast<int>( mode ) ); const u64 ids[]{ discarded, id }; usize allocations = 0;
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                vector_t<u64> roots; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); audit.calls = 0;
                REQUIRE( MapBrushEdit_ClipMode( copy.get(), { ids, 2 }, { { 1, 0, 0 }, 0 }, mode, SV( "materials/cut.cymat" ), &roots ) == map_status_t::OK ); allocations = audit.calls;
            }
            REQUIRE( allocations > 16 ); REQUIRE( audit.bytes == baseline );
            for ( usize failure = 0; failure < allocations; ++failure ) {
                CAPTURE( failure, allocations );
                {
                    audit.failAt = CY_USIZE_MAX; map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) ); bool_t changed = CY_FALSE;
                    audit.calls = 0; audit.failAt = failure;
                    CHECK( MapBrushEdit_ClipMode( copy.get(), { ids, 2 }, { { 1, 0, 0 }, 0 }, mode, SV( "materials/cut.cymat" ), &roots, &changed ) == map_status_t::OUT_OF_MEMORY );
                    REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); CHECK_FALSE( changed ); audit.failAt = CY_USIZE_MAX;
                }
                CHECK( audit.bytes == baseline ); CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision ); CHECK( original.geometry.brushes.nCount == 2 );
            }
        }
    }
    CHECK( audit.bytes == 0 );
}

TEST_CASE( "Convex merge handles touching overlapping contained and equal authored boxes", "[map][brush-edit][merge]" )
{
    const math::aabbd_t otherBounds[]{
        { { 64, -64, -64 }, { 192, 64, 64 } }, // Adjacent full face.
        { { 0, -64, -64 }, { 128, 64, 64 } }, // Aligned convex overlap.
        { { -32, -32, -32 }, { 32, 32, 32 } }, // Second contained in first.
        { { -128, -128, -128 }, { 128, 128, 128 } }, // First contained in second.
        { { -64, -64, -64 }, { 64, 64, 64 } }
    };
    for ( usize scenario = 0; scenario < 5; ++scenario ) {
        CAPTURE( scenario ); map_document_t m; Create( m ); const u64 ids[]{ Box( m ), Box( m, otherBounds[scenario] ) };
        const auto next = m.nextId, revision = m.geometry.revision;
        vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) );
        REQUIRE( MapBrushEdit_Merge( &m, { ids, 2 }, &roots ) == map_status_t::OK ); REQUIRE( roots.nCount == 1 );
        CHECK( m.geometry.brushes.nCount == 1 ); CHECK( m.geometry.revision == revision + 1 ); CHECK( m.nextId > next );
        CHECK_FALSE( geo::GeometryDocument_FindBrush( &m.geometry, { ids[0] } ) ); CHECK_FALSE( geo::GeometryDocument_FindBrush( &m.geometry, { ids[1] } ) );
        const auto *merged = Brush( m, roots.pData[0] ); REQUIRE( merged->sides.nCount == 6 );
        CHECK( geo::BrushValidation_Deep( merged, m.geometryPolicy, m.pAllocator ).bWatertight );
        std::set<u64> identities; CHECK( merged->sourceId.value >= next ); CHECK( identities.insert( merged->sourceId.value ).second );
        for ( usize i = 0; i < merged->sides.nCount; ++i ) {
            CHECK( merged->sides.pData[i].sourceId.value >= next ); CHECK( identities.insert( merged->sides.pData[i].sourceId.value ).second );
            CHECK( StringView_Equals( MapMaterials_Path( &m.materials, Attributes( m, roots.pData[0], i ).material.value ), SV( "materials/original.cymat" ) ) );
        }
        const auto bounds = Bounds( m, roots.pData[0] ).box;
        CHECK( bounds.minimum.x == Catch::Approx( std::min( -64.0, otherBounds[scenario].minimum.x ) ) );
        CHECK( bounds.maximum.x == Catch::Approx( std::max( 64.0, otherBounds[scenario].maximum.x ) ) );
        CHECK( bounds.minimum.y == Catch::Approx( std::min( -64.0, otherBounds[scenario].minimum.y ) ) );
        CHECK( bounds.maximum.z == Catch::Approx( std::max( 64.0, otherBounds[scenario].maximum.z ) ) );
    }
}

TEST_CASE( "Merge keeps first operand root metadata and exact surfaces from both through save and reload", "[map][brush-edit][merge][persistence]" )
{
    for ( usize scenario = 0; scenario < 4; ++scenario ) {
      for ( bool reverse : { false, true } ) {
        CAPTURE( scenario, reverse ); map_document_t m; Create( m ); const u64 a = Box( m );
        const u64 b = Box( m, scenario == 0 ? math::aabbd_t{ { 64, -64, -64 }, { 192, 64, 64 } }
            : scenario == 1 ? math::aabbd_t{ { -128, -128, -128 }, { 128, 128, 128 } }
            : scenario == 2 ? math::aabbd_t{ { -64, -64, -64 }, { 64, 64, 64 } }
            : math::aabbd_t{ { -32, -32, -32 }, { 32, 32, 32 } } );
        REQUIRE( MapDocument_AddLayer( &m, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
        u64 owner = 0; REQUIRE( MapDocument_AddEntity( &m, SV( "detail" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
        for ( const u64 id : { a, b } ) {
            REQUIRE( MapDocument_SetGeometryLayer( &m, id, SV( "detail" ) ) == map_status_t::OK );
            REQUIRE( MapDocument_SetGeometryOwner( &m, id, owner ) == map_status_t::OK );
        }
        saved_t saved; REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); AddMetadata( m, a ); AddMetadata( m, b );
        TagOperand( m, a, "a", "materials/merge_a.cymat" ); TagOperand( m, b, "b", "materials/merge_b.cymat" );
        auto *bRecord = MapDocument_FindObject( &m, b, nullptr ); map_chunk_t *bChunk = nullptr; REQUIRE( MapDocument_FindObject( &m, b, &bChunk ) );
        REQUIRE( KeyValue_SetString( bChunk->store.pDocument, KeyValue_Find( bRecord, SV( "name" ) ), SV( "Second root must not win" ) ) );
        REQUIRE( KeyValue_SetU64( bChunk->store.pDocument, KeyValue_Find( bRecord, SV( "future_root" ) ), 76543 ) );
        auto *bAttributes = geo::GeometryDocument_FindBrushAttributesMutable( &m.geometry, { b } ); REQUIRE( bAttributes );
        for ( usize i = 0; i < bAttributes->records.nCount; ++i ) { bAttributes->records.pData[i].uvProjection.offset.x += 7.0; }
        const u64 ids[]{ reverse ? b : a, reverse ? a : b }; const u32 layer = Placement( m, a ).iLayer;
        vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); REQUIRE( MapBrushEdit_Merge( &m, { ids, 2 }, &roots ) == map_status_t::OK ); REQUIRE( roots.nCount == 1 );
        const auto check = [&]( map_document_t &map ) {
            const u64 id = roots.pData[0]; const auto *record = MapDocument_FindObject( &map, id, nullptr ); REQUIRE( record );
            CHECK( Placement( map, id ).owner == owner ); CHECK( Placement( map, id ).iLayer == layer );
            string_view_t name{}; REQUIRE( KeyValue_GetString( KeyValue_Find( record, SV( "name" ) ), &name ) );
            CHECK( StringView_Equals( name, SV( reverse ? "Second root must not win" : "Named authored shell" ) ) );
            u64 marker = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( record, SV( "future_root" ) ), &marker ) ); CHECK( marker == ( reverse ? 76543 : 54321 ) );
            usize fromA = 0, fromB = 0; const auto *brush = Brush( map, id );
            for ( usize i = 0; i < brush->sides.nCount; ++i ) {
                const auto *face = Face( record, brush->sides.pData[i].sourceId.value ); REQUIRE( face );
                string_view_t operand{}; REQUIRE( KeyValue_GetString( KeyValue_Find( face, SV( "future_operand" ) ), &operand ) );
                const bool isB = StringView_Equals( operand, SV( "b" ) ); CHECK( ( isB || StringView_Equals( operand, SV( "a" ) ) ) );
                isB ? ++fromB : ++fromA; REQUIRE( KeyValue_GetU64( KeyValue_Find( face, SV( "future_side" ) ), &marker ) ); REQUIRE( marker >= 1 ); REQUIRE( marker <= 6 );
                const auto attributes = Attributes( map, id, i );
                CHECK( attributes.uvProjection.worldUnitsPerUv.x == Catch::Approx( 64.0 + 16.0 * static_cast<double>( marker - 1 ) ) );
                CHECK( attributes.uvProjection.offset.x == Catch::Approx( ( isB ? 7.0 : 0.0 ) + 0.125 * static_cast<double>( marker - 1 ) ) );
                CHECK( StringView_Equals( MapMaterials_Path( &map.materials, attributes.material.value ), SV( isB ? "materials/merge_b.cymat" : "materials/merge_a.cymat" ) ) );
            }
            if ( scenario == 0 ) { CHECK( fromA == ( reverse ? 1 : 5 ) ); CHECK( fromB == ( reverse ? 5 : 1 ) ); }
            if ( scenario == 1 ) { CHECK( fromA == 0 ); CHECK( fromB == 6 ); }
            if ( scenario == 2 ) { CHECK( fromA == ( reverse ? 6 : 0 ) ); CHECK( fromB == ( reverse ? 0 : 6 ) ); }
            if ( scenario == 3 ) { CHECK( fromA == 6 ); CHECK( fromB == 0 ); }
        };
        check( m ); REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved ); check( loaded );
        const auto *nested = KeyValue_Find( MapDocument_FindObject( &loaded, owner, nullptr ), SV( "brushes" ) ); REQUIRE( KeyValue_ChildCount( nested ) == 1 );
      }
    }
}

TEST_CASE( "Merge never fills a disjoint gap or a hull with an interior notch", "[map][brush-edit][merge]" )
{
    for ( usize scenario = 0; scenario < 6; ++scenario ) {
        CAPTURE( scenario ); map_document_t m; Create( m );
        u64 ids[2]{};
        if ( scenario < 5 ) {
            ids[0] = Box( m, { { 0, 0, 0 }, { 64, 64, 64 } } );
            const math::aabbd_t second[]{
                { { 96, 0, 0 }, { 160, 64, 64 } }, // Aligned disjoint supports.
                { { 64, 32, 0 }, { 128, 96, 64 } }, // Nonconvex face touch.
                { { 32, 32, 0 }, { 96, 96, 64 } }, // Nonconvex overlap.
                { { 64, 64, 0 }, { 128, 128, 64 } }, // Shared edge only.
                { { 64, 64, 64 }, { 128, 128, 128 } } // Shared corner only.
            };
            ids[1] = Box( m, second[scenario] );
        } else {
            ids[0] = Box( m, { { 0, 0, 0 }, { 64, 64, 64 } } ); ids[1] = Box( m, { { 0, 0, 0 }, { 64, 64, 64 } } );
            vector_t<u64> clipped; REQUIRE( Vector_Init( &clipped, m.pAllocator ) ); const double s = 1.0 / std::sqrt( 2.0 );
            REQUIRE( MapBrushEdit_Clip( &m, { &ids[0], 1 }, { { s, s, 0 }, -76.8 * s }, {}, &clipped ) == map_status_t::OK );
            REQUIRE( MapBrushEdit_Clip( &m, { &ids[1], 1 }, { { s, -s, 0 }, -12.8 * s }, {}, &clipped ) == map_status_t::OK );
            // Their cube hull uses only original support planes, but the point
            // (64,32,32) is in neither operand. Support matching is insufficient.
        }
        const auto next = m.nextId, revision = m.geometry.revision;
        vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) );
        CHECK( MapBrushEdit_Merge( &m, { ids, 2 }, &roots ) == map_status_t::GEOMETRY_FAILED );
        REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); CHECK( m.geometry.brushes.nCount == 2 );
        CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision ); CHECK( Brush( m, ids[0] ) ); CHECK( Brush( m, ids[1] ) );
    }
}

TEST_CASE( "Merge validates exact operands writable state explicit placement and identity capacity", "[map][brush-edit][merge]" )
{
    map_document_t m; Create( m ); const u64 ids[]{ Box( m ), Box( m, { { 64, -64, -64 }, { 192, 64, 64 } } ) };
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) );
    const auto check = [&]( span_t<const u64> selection, map_status_t expected ) {
        CHECK( MapBrushEdit_Merge( &m, selection, &roots ) == expected ); REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); CHECK( m.geometry.brushes.nCount == 2 );
    };
    check( {}, map_status_t::INVALID_ARGUMENT ); check( { ids, 1 }, map_status_t::INVALID_ARGUMENT );
    const u64 three[]{ ids[0], ids[1], ids[0] }; check( { three, 3 }, map_status_t::INVALID_ARGUMENT );
    const u64 duplicates[]{ ids[0], ids[0] }; check( { duplicates, 2 }, map_status_t::INVALID_ARGUMENT );
    const u64 unknown[]{ ids[0], 987654 }; check( { unknown, 2 }, map_status_t::UNKNOWN_OBJECT );
    u64 owner = 0; REQUIRE( MapDocument_AddEntity( &m, SV( "default" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
    const u64 entitySelection[]{ ids[0], owner }; check( { entitySelection, 2 }, map_status_t::UNKNOWN_OBJECT );
    m.bReadOnly = CY_TRUE; check( { ids, 2 }, map_status_t::READ_ONLY ); m.bReadOnly = CY_FALSE;
    vector_t<u64> empty{}; CHECK( MapBrushEdit_Merge( &m, { ids, 2 }, &empty ) == map_status_t::INVALID_ARGUMENT );
    REQUIRE( MapDocument_AddLayer( &m, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( &m, ids[1], SV( "detail" ) ) == map_status_t::OK ); check( { ids, 2 }, map_status_t::INVALID_ARGUMENT );
    REQUIRE( MapDocument_SetGeometryLayer( &m, ids[1], SV( "default" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &m, ids[1], owner ) == map_status_t::OK ); check( { ids, 2 }, map_status_t::INVALID_ARGUMENT );
    REQUIRE( MapDocument_SetGeometryOwner( &m, ids[1], 0 ) == map_status_t::OK );
    const usize placementIndex = m.geometryRecords.pData[0].id == ids[1] ? 0 : 1;
    const u32 layer = m.geometryRecords.pData[placementIndex].iLayer; m.geometryRecords.pData[placementIndex].iLayer = 999;
    check( { ids, 2 }, map_status_t::UNKNOWN_LAYER ); m.geometryRecords.pData[placementIndex].iLayer = layer;
    const u64 next = m.nextId; m.nextId = CY_U64_MAX - 3; check( { ids, 2 }, map_status_t::LIMIT_EXCEEDED ); m.nextId = 0;
    check( { ids, 2 }, map_status_t::LIMIT_EXCEEDED ); m.nextId = next;
    Vector_Erase( &m.geometryRecords, placementIndex ); check( { ids, 2 }, map_status_t::GEOMETRY_FAILED );
}

TEST_CASE( "Merge preserves a second operand's residual faces onto an unsaved first root without stealing its name", "[map][brush-edit][merge]" )
{
    map_document_t m; Create( m ); const u64 b = Box( m, { { 64, -64, -64 }, { 192, 64, 64 } } );
    saved_t saved; REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); AddMetadata( m, b ); TagOperand( m, b, "b", "materials/merge_b.cymat" );
    const u64 a = Box( m ), ids[]{ a, b }; REQUIRE( MapDocument_FindObject( &m, a, nullptr ) == nullptr );
    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); REQUIRE( MapBrushEdit_Merge( &m, { ids, 2 }, &roots ) == map_status_t::OK ); REQUIRE( roots.nCount == 1 );
    const auto check = [&]( map_document_t &map ) {
        const auto *record = MapDocument_FindObject( &map, roots.pData[0], nullptr ); REQUIRE( record );
        CHECK_FALSE( KeyValue_Find( record, SV( "name" ) ) ); CHECK_FALSE( KeyValue_Find( record, SV( "future_root" ) ) );
        usize inherited = 0; const auto *brush = Brush( map, roots.pData[0] );
        for ( usize i = 0; i < brush->sides.nCount; ++i ) {
            const auto *face = Face( record, brush->sides.pData[i].sourceId.value );
            if ( !face ) { continue; }
            string_view_t tag{}; REQUIRE( KeyValue_GetString( KeyValue_Find( face, SV( "future_operand" ) ), &tag ) ); CHECK( StringView_Equals( tag, SV( "b" ) ) ); ++inherited;
        }
        CHECK( inherited == 1 );
    };
    check( m ); REQUIRE( MapDocument_Save( &m, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved ); check( loaded );
}

TEST_CASE( "Merge checks the persisted face limit before removing either source", "[map][brush-edit][merge][limit]" )
{
    map_document_t m; Create( m ); m.geometryPolicy.limits.cBrushSidesPerBrushMax = 512; m.geometry.policy = m.geometryPolicy;
    geo::geometry_source_id_allocator_t allocator{ { m.nextId } }; geo::brush_solid_t solid{}; geo::brush_source_t source{};
    REQUIRE( geo::BrushGenerator_TryMakePrism( &solid, m.pAllocator, m.geometryPolicy, &allocator, {}, 128.0, 64.0, 255, 2 ) == geo::geometry_status_t::OK );
    const u64 prism = solid.sourceId.value; REQUIRE( solid.sides.nCount == MAP_BRUSH_FACES_MAX + 1 );
    REQUIRE( geo::BrushSource_TryBuildDefault( &solid, m.pAllocator, m.geometryPolicy, &source ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddBrushSource( &m.geometry, &source ) == geo::geometry_status_t::OK ); m.nextId = allocator.next.value;
    REQUIRE( MapDocument_SetGeometryLayer( &m, prism, SV( "default" ) ) == map_status_t::OK ); geo::BrushSource_Shutdown( &source ); geo::BrushSolid_Shutdown( &solid );
    const u64 inside = Box( m, { { -16, -16, -16 }, { 16, 16, 16 } } ), ids[]{ inside, prism };
    const auto next = m.nextId, revision = m.geometry.revision; vector_t<u64> roots; REQUIRE( Vector_Init( &roots, m.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) );
    CHECK( MapBrushEdit_Merge( &m, { ids, 2 }, &roots ) == map_status_t::LIMIT_EXCEEDED ); REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 );
    CHECK( m.geometry.brushes.nCount == 2 ); CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision );
}

TEST_CASE( "Every merge allocation failure preserves external results and releases private prepared data", "[map][brush-edit][merge][oom]" )
{
    audit_t audit;
    for ( bool unsavedFirst : { false, true } ) {
        CAPTURE( unsavedFirst );
        {
            map_document_t original; Create( original, &audit.allocator ); u64 a = unsavedFirst ? 0 : Box( original );
            const u64 b = Box( original, { { 64, -64, -64 }, { 192, 64, 64 } } );
            saved_t saved; REQUIRE( MapDocument_Save( &original, saved.Sink() ) == map_status_t::OK ); AddMetadata( original, b );
            if ( unsavedFirst ) { a = Box( original ); } else { AddMetadata( original, a ); }
            const u64 ids[]{ a, b }; const usize baseline = audit.bytes; const auto next = original.nextId, revision = original.geometry.revision; usize allocations = 0;
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                vector_t<u64> roots; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); audit.calls = 0;
                REQUIRE( MapBrushEdit_Merge( copy.get(), { ids, 2 }, &roots ) == map_status_t::OK ); allocations = audit.calls;
            }
            REQUIRE( allocations > 16 ); REQUIRE( audit.bytes == baseline );
            for ( usize failure = 0; failure < allocations; ++failure ) {
                CAPTURE( failure, allocations );
                {
                    audit.failAt = CY_USIZE_MAX; map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                    vector_t<u64> roots; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 123456 } ) );
                    audit.calls = 0; audit.failAt = failure;
                    CHECK( MapBrushEdit_Merge( copy.get(), { ids, 2 }, &roots ) == map_status_t::OUT_OF_MEMORY );
                    REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 123456 ); audit.failAt = CY_USIZE_MAX;
                }
                CHECK( audit.bytes == baseline ); CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision ); CHECK( original.geometry.brushes.nCount == 2 );
            }
        }
        CHECK( audit.bytes == 0 );
    }
}
