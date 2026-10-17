//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Map edit ownership, persistent identity, transforms and save/load contracts.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_Edit.h"
#include "CypherMap_Hierarchy.h"
#include "CypherMap_Modifiers.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMap_Wireframe.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;
namespace
{
string_view_t SV( const std::string &s ) { return { s.data(), s.size() }; }
string_view_t SV( const char *s ) { return StringView_FromCString( s ); }
void Create( map_document_t &m )
{
    REQUIRE( MapDocument_Create( &m, Allocator_GetSystem(), { SV( "Edit contract" ), SV( "reap" ), {} } ) == map_status_t::OK );
}
u64 Box( map_document_t &m )
{
    u64 id = 0;
    REQUIRE( MapEdit_CreateBox( &m, { { -32, -16, 0 }, { 32, 16, 64 } }, SV( "materials/blockout/floor_tile.cymat" ), {}, &id ) == map_status_t::OK );
    REQUIRE( id != 0 ); return id;
}
map_bounds_t Bounds( map_document_t &m, u64 id )
{
    geo::brush_source_t b{};
    REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &m.geometry, { id }, m.pAllocator, &b ) == geo::geometry_status_t::OK );
    const auto bounds = MapGeometry_BrushBounds( b, m.geometryPolicy, m.pAllocator );
    geo::BrushSource_Shutdown( &b ); REQUIRE( bounds.bHas ); return bounds;
}
struct files_t {
    std::string root;
    map_status_t Save( map_document_t *pMap )
    {
        text_buffer_t text{};
        if ( !TextBuffer_Init( &text, Allocator_GetSystem() ) ) { return map_status_t::OUT_OF_MEMORY; }
        const map_status_t status = MapDocument_Save( pMap, &text );
        if ( status == map_status_t::OK ) { root.assign( TextBuffer_Data( &text ), TextBuffer_Length( &text ) ); }
        return status;
    }
};
void Load( map_document_t &m, const files_t &f )
{
    REQUIRE( MapDocument_Load( &m, Allocator_GetSystem(), SV( f.root ) ) == map_status_t::OK );
}
void Facility( map_document_t &m )
{
    const std::filesystem::path dir = CYPHER_MAP_EXAMPLE_DIR;
    const auto read = []( const std::filesystem::path &p ) { std::ifstream in( p ); return std::string( std::istreambuf_iterator<char>( in ), {} ); };
    files_t f; f.root = read( dir / "facility.cymap" );
    Load( m, f );
}
void UseUnweldedFacilityMirror( map_document_t &m )
{
    // Nonuniform root transforms preserve this axis-aligned mirror plane,
    // but cannot preserve a scalar distance-weld tolerance. Change only the
    // private test document; the authored facility stays welded.
    std::vector<geo::modifier_t> stages;
    REQUIRE( MapModifiers_Read( &m, 1200, stages ) == map_status_t::OK );
    REQUIRE( stages.size() == 1u ); REQUIRE( stages[0].kind == geo::modifier_kind_t::MIRROR );
    REQUIRE( stages[0].bWeld ); REQUIRE( stages[0].weldDistance > 0 );
    stages[0].bWeld = false;
    bool changed = false;
    REQUIRE( MapModifiers_Write( &m, 1200, stages, &changed ) == map_status_t::OK ); REQUIRE( changed );
}
void SetPrefabFixtureVector( map_document_t &map, const char *name, math::vec3d_t value )
{
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &map, 200, &store ); REQUIRE( record ); REQUIRE( store );
    if ( auto *old = KeyValue_Find( record, SV( name ) ) ) { REQUIRE( KeyValue_Remove( store->store.pDocument, record, old ) ); }
    auto *node = KeyValue_ObjectInsert( store->store.pDocument, record, SV( name ), key_value_type_t::ARRAY ); REQUIRE( node );
    for ( f64 part : { value.x, value.y, value.z } ) {
        auto *item = KeyValue_ArrayAppend( store->store.pDocument, node, key_value_type_t::F64 ); REQUIRE( item );
        REQUIRE( KeyValue_SetF64( store->store.pDocument, item, part ) );
    }
}
math::vec3d_t PrefabFixtureVector( map_document_t &map, const char *name )
{
    const auto *node = KeyValue_Find( MapDocument_FindObject( &map, 200, nullptr ), SV( name ) );
    math::vec3d_t value{}; REQUIRE( KeyValue_ChildCount( node ) == 3 );
    REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( node, 0 ), &value.x ) ); REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( node, 1 ), &value.y ) );
    REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( node, 2 ), &value.z ) ); return value;
}
math::vec3d_t PrefabFixturePoint( math::vec3d_t p, math::vec3d_t origin, math::vec3d_t angles, math::vec3d_t scale )
{
    // Independent point construction: scale, roll about X, pitch about Y,
    // then yaw about Z. The production admission uses a matrix conjugation.
    constexpr f64 rad = 3.14159265358979323846 / 180;
    p = { p.x * scale.x, p.y * scale.y, p.z * scale.z };
    const f64 roll = angles.z * rad, pitch = angles.x * rad, yaw = angles.y * rad;
    p = { p.x, std::cos( roll ) * p.y - std::sin( roll ) * p.z, std::sin( roll ) * p.y + std::cos( roll ) * p.z };
    p = { std::cos( pitch ) * p.x + std::sin( pitch ) * p.z, p.y, -std::sin( pitch ) * p.x + std::cos( pitch ) * p.z };
    p = { std::cos( yaw ) * p.x - std::sin( yaw ) * p.y, std::sin( yaw ) * p.x + std::cos( yaw ) * p.y, p.z };
    return { origin.x + p.x, origin.y + p.y, origin.z + p.z };
}
struct allocation_audit_t {
    usize calls{ 0 }, failAt{ CY_USIZE_MAX }, bytes{ 0 };
    allocator_t allocator{};
    allocation_audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *ctx, usize size, usize alignment ) noexcept -> void * {
            auto &audit = *static_cast<allocation_audit_t *>( ctx );
            if ( audit.calls++ == audit.failAt ) { return nullptr; }
            void *p = Allocator_Allocate( Allocator_GetSystem(), size, alignment );
            if ( p ) { audit.bytes += size; } return p;
        };
        allocator.pfnFree = []( void *ctx, void *p, usize size, usize alignment ) noexcept {
            if ( p ) { static_cast<allocation_audit_t *>( ctx )->bytes -= size; }
            Allocator_Free( Allocator_GetSystem(), p, size, alignment );
        };
    }
};
}

TEST_CASE( "Map duplicate requires an initialized result buffer before editing geometry", "[map][edit]" )
{
    map_document_t map; Create( map ); const u64 id = Box( map );
    const auto next = map.nextId, revision = map.geometry.revision;
    vector_t<u64> uninitialized{};
    CHECK( MapEdit_Duplicate( &map, { &id, 1 }, { 64, 0, 0 }, &uninitialized ) == map_status_t::INVALID_ARGUMENT );
    CHECK( map.geometry.brushes.nCount == 1 ); CHECK( map.nextId == next ); CHECK( map.geometry.revision == revision );
    CHECK( uninitialized.pData == nullptr ); CHECK( uninitialized.nCount == 0 );
    CHECK( geo::GeometryDocument_FindBrush( &map.geometry, { id } ) != nullptr );
    CHECK( Bounds( map, id ).box.minimum.x == Catch::Approx( -32 ) );
}

TEST_CASE( "Map edit working copies retain identity and authored state without mutating their source", "[map][edit]" )
{
    map_document_t original; Create( original ); const u64 id = Box( original );
    const u64 next = original.nextId; const auto revision = original.geometry.revision;
    map_document_t *raw = nullptr;
    REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK );
    std::unique_ptr<map_document_t> edited( raw );
    REQUIRE( MapEdit_Translate( edited.get(), { &id, 1 }, { 128, -64, 32 } ) == map_status_t::OK );
    CHECK( Bounds( original, id ).box.minimum.x == Catch::Approx( -32 ) );
    CHECK( Bounds( *edited, id ).box.minimum.x == Catch::Approx( 96 ) );
    CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision );
    CHECK( original.layerStores.nCount == 0 );
    CHECK( UniqueId_Equals( edited->mapId, original.mapId ) );
    CHECK( edited->geometry.sourceIds.allocator.next.value == original.geometry.sourceIds.allocator.next.value );
    CHECK( MapEdit_EstimateBytes( edited.get() ) > sizeof( map_document_t ) );
    const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &edited->geometry, { id } );
    REQUIRE( attributes );
    const auto *brush = geo::GeometryDocument_FindBrush( &edited->geometry, { id } ); REQUIRE( brush );
    for ( usize i = 0; i < brush->sides.nCount; ++i ) {
        const auto &uv = attributes->records.pData[brush->sides.pData[i].iAttributeIndex].uvProjection;
        const auto n = brush->sides.pData[i].plane.normal;
        CHECK( n.x * uv.normal.x + n.y * uv.normal.y + n.z * uv.normal.z == Catch::Approx( 1.0 ) );
        CHECK( uv.worldUnitsPerUv.x == Catch::Approx( 128.0 ) );
    }
    CHECK( MapMaterials_Path( &edited->materials, attributes->records.pData[0].material.value ).cchLength != 0 );
    files_t saved; REQUIRE( saved.Save( edited.get() ) == map_status_t::OK );
    map_document_t loaded; Load( loaded, saved );
    CHECK( Bounds( loaded, id ).box.minimum.x == Catch::Approx( 96 ) );
    CHECK( loaded.nextId == edited->nextId );
}

TEST_CASE( "Map affine edits compose rotation and scale around a world pivot", "[map][edit]" )
{
    map_document_t m; Create( m ); const u64 id = Box( m );
    REQUIRE( MapEdit_Scale( &m, { &id, 1 }, { 2, 3, 1 }, { 0, 0, 0 } ) == map_status_t::OK );
    CHECK( Bounds( m, id ).box.maximum.x == Catch::Approx( 64 ) );
    CHECK( Bounds( m, id ).box.maximum.y == Catch::Approx( 48 ) );
    REQUIRE( MapEdit_Rotate( &m, { &id, 1 }, { 0, 0, 90 }, {} ) == map_status_t::OK );
    CHECK( Bounds( m, id ).box.maximum.x == Catch::Approx( 48 ) );
    CHECK( Bounds( m, id ).box.maximum.y == Catch::Approx( 64 ) );
    const auto before = Bounds( m, id );
    CHECK( MapEdit_Scale( &m, { &id, 1 }, { 0, 1, 1 }, {} ) == map_status_t::INVALID_ARGUMENT );
    CHECK( Bounds( m, id ).box.maximum.y == before.box.maximum.y );
}

TEST_CASE( "Per-object resize calculations preserve each captured anchor and reject invalid travel", "[map][edit][resize-each][validation]" )
{
    const math::aabbd_t bounds{ { 10, 20, 30 }, { 110, 180, 230 } };
    const auto axisValue = []( math::vec3d_t value, int axis ) { return axis == 0 ? value.x : axis == 1 ? value.y : value.z; };
    const auto setAxis = []( math::vec3d_t &value, int axis, f64 amount ) { if ( axis == 0 ) { value.x = amount; } else if ( axis == 1 ) { value.y = amount; } else { value.z = amount; } };
    for ( int axis = 0; axis < 3; ++axis ) {
        for ( const f64 sign : { -1.0, 1.0 } ) {
            for ( const bool centered : { false, true } ) {
                CAPTURE( axis, sign, centered );
                map_bounds_resize_t resize{}; resize.bFromCenter = centered ? CY_TRUE : CY_FALSE;
                setAxis( resize.sides, axis, sign ); setAxis( resize.delta, axis, sign * 16 );
                math::vec3d_t factors{}, pivot{};
                REQUIRE( MapEdit_ResizeTransform( bounds, resize, &factors, &pivot ) == map_status_t::OK );
                for ( int other = 0; other < 3; ++other ) {
                    const f64 minimum = axisValue( bounds.minimum, other ), maximum = axisValue( bounds.maximum, other );
                    const f64 expectedPivot = other != axis || centered ? ( minimum + maximum ) * 0.5 : sign > 0 ? minimum : maximum;
                    CHECK( axisValue( pivot, other ) == Catch::Approx( expectedPivot ) );
                    CHECK( axisValue( factors, other ) == Catch::Approx( other == axis ? 1 + 16 * ( centered ? 2.0 : 1.0 ) / ( maximum - minimum ) : 1.0 ) );
                }
            }
        }
    }
    const map_bounds_resize_t valid{ { 1, 0, 0 }, { 16, 0, 0 }, CY_FALSE };
    const auto reject = [&]( math::aabbd_t box, map_bounds_resize_t resize ) {
        math::vec3d_t factors{ 7, 8, 9 }, pivot{ 4, 5, 6 };
        CHECK( MapEdit_ResizeTransform( box, resize, &factors, &pivot ) == map_status_t::INVALID_ARGUMENT );
        CHECK( factors.x == 7 ); CHECK( factors.y == 8 ); CHECK( factors.z == 9 );
        CHECK( pivot.x == 4 ); CHECK( pivot.y == 5 ); CHECK( pivot.z == 6 );
    };
    auto invalid = valid; invalid.sides = {}; reject( bounds, invalid );
    invalid = valid; invalid.sides.x = 0.5; reject( bounds, invalid );
    invalid = valid; invalid.delta.y = 1; reject( bounds, invalid );
    invalid = valid; invalid.delta.x = -100; reject( bounds, invalid );
    invalid = valid; invalid.delta.x = -51; invalid.bFromCenter = CY_TRUE; reject( bounds, invalid );
    invalid = valid; invalid.delta.x = std::numeric_limits<f64>::infinity(); reject( bounds, invalid );
    invalid = valid; invalid.sides.z = std::numeric_limits<f64>::quiet_NaN(); reject( bounds, invalid );
    auto flat = bounds; flat.maximum.x = flat.minimum.x; reject( flat, valid );
    auto inverted = bounds; inverted.maximum.z = inverted.minimum.z - 1; reject( inverted, valid );
    math::vec3d_t output{};
    CHECK( MapEdit_ResizeTransform( bounds, valid, nullptr, &output ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapEdit_ResizeTransform( bounds, valid, &output, &output ) == map_status_t::INVALID_ARGUMENT );
    invalid = valid; invalid.delta = {}; math::vec3d_t factors{}, pivot{};
    REQUIRE( MapEdit_ResizeTransform( bounds, invalid, &factors, &pivot ) == map_status_t::OK );
    CHECK( factors.x == 1 ); CHECK( factors.y == 1 ); CHECK( factors.z == 1 );
}

TEST_CASE( "Mixed selected roots extend by identical world travel without shifting their opposite sides", "[map][edit][resize-each][persistence]" )
{
    const auto axisValue = []( math::vec3d_t value, int axis ) { return axis == 0 ? value.x : axis == 1 ? value.y : value.z; };
    const auto setAxis = []( math::vec3d_t &value, int axis, f64 amount ) { if ( axis == 0 ) { value.x = amount; } else if ( axis == 1 ) { value.y = amount; } else { value.z = amount; } };
    for ( int axis = 0; axis < 3; ++axis ) {
        for ( const f64 sign : { -1.0, 1.0 } ) {
            for ( const bool centered : { false, true } ) {
                CAPTURE( axis, sign, centered );
                map_document_t original; Create( original );
                u64 ids[4]{};
                for ( usize i = 0; i < 3; ++i ) {
                    const f64 offset = static_cast<f64>( i ) * 256;
                    const f64 sizeOffset = static_cast<f64>( i ) * 32;
                    const math::aabbd_t box{ { offset + 16, offset + 32, offset + 48 }, { offset + 80 + sizeOffset, offset + 160 + sizeOffset, offset + 240 + sizeOffset } };
                    REQUIRE( MapEdit_CreateBox( &original, box, SV( "materials/blockout/floor_tile.cymat" ), {}, &ids[i] ) == map_status_t::OK );
                }
                map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = static_cast<u32>( ( axis + 1 ) % 3 );
                quad.bounds = { { -240, -160, -80 }, { -48, -16, 64 } }; quad.worldUnitsPerUv = 48;
                REQUIRE( MapEdit_CreatePrimitive( &original, quad, SV( "materials/dev/quad.cymat" ), {}, &ids[3] ) == map_status_t::OK );
                map_wireframe_t before; REQUIRE( MapWireframe_Init( &before, original.pAllocator ) ); REQUIRE( MapWireframe_Build( &before, original ) == map_status_t::OK );
                const auto originalRevision = original.geometry.revision, next = original.nextId;
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> edited( raw );
                // Repeated IDs must not double the side travel.
                const u64 selected[]{ ids[0], ids[1], ids[3], ids[2], ids[0], ids[3] };
                map_bounds_resize_t resize{}; resize.bFromCenter = centered ? CY_TRUE : CY_FALSE;
                setAxis( resize.sides, axis, sign ); setAxis( resize.delta, axis, sign * 64 );
                REQUIRE( MapEdit_Resize( edited.get(), { selected, 6 }, resize, false ) == map_status_t::OK );
                CHECK( edited->geometry.revision == originalRevision + 1 ); CHECK( original.geometry.revision == originalRevision ); CHECK( edited->nextId == next );
                map_wireframe_t after; REQUIRE( MapWireframe_Init( &after, edited->pAllocator ) ); REQUIRE( MapWireframe_Build( &after, *edited ) == map_status_t::OK );
                for ( const u64 id : ids ) {
                    const auto *source = MapWireframe_FindObject( before, id ), *target = MapWireframe_FindObject( after, id ); REQUIRE( source ); REQUIRE( target );
                    CHECK( target->owner == source->owner ); CHECK( target->iLayer == source->iLayer ); CHECK( target->nPoints == source->nPoints ); CHECK( target->nLines == source->nLines );
                    for ( int other = 0; other < 3; ++other ) {
                        const f64 lower = axisValue( source->bounds.box.minimum, other ) - ( other == axis && ( centered || sign < 0 ) ? 64 : 0 );
                        const f64 upper = axisValue( source->bounds.box.maximum, other ) + ( other == axis && ( centered || sign > 0 ) ? 64 : 0 );
                        CHECK( axisValue( target->bounds.box.minimum, other ) == Catch::Approx( lower ) ); CHECK( axisValue( target->bounds.box.maximum, other ) == Catch::Approx( upper ) );
                    }
                    if ( id == ids[3] ) { continue; }
                    const auto *sourceBrush = geo::GeometryDocument_FindBrush( &original.geometry, { id } ), *targetBrush = geo::GeometryDocument_FindBrush( &edited->geometry, { id } ); REQUIRE( sourceBrush ); REQUIRE( targetBrush );
                    const auto *sourceAttributes = geo::GeometryDocument_FindBrushAttributes( &original.geometry, { id } ), *targetAttributes = geo::GeometryDocument_FindBrushAttributes( &edited->geometry, { id } ); REQUIRE( sourceAttributes ); REQUIRE( targetAttributes );
                    REQUIRE( targetBrush->sides.nCount == sourceBrush->sides.nCount );
                    for ( usize side = 0; side < sourceBrush->sides.nCount; ++side ) {
                        CHECK( targetBrush->sides.pData[side].sourceId.value == sourceBrush->sides.pData[side].sourceId.value );
                        const auto &a = sourceAttributes->records.pData[sourceBrush->sides.pData[side].iAttributeIndex], &b = targetAttributes->records.pData[targetBrush->sides.pData[side].iAttributeIndex];
                        CHECK( b.material.value == a.material.value ); CHECK( b.uvProjection.worldUnitsPerUv.x == a.uvProjection.worldUnitsPerUv.x ); CHECK( b.uvProjection.worldUnitsPerUv.y == a.uvProjection.worldUnitsPerUv.y );
                    }
                }
                geo::mesh_source_description_t sourceMesh{}, targetMesh{};
                REQUIRE( geo::MeshSourceDescription_Init( &sourceMesh, original.pAllocator, { ids[3] } ) == geo::geometry_status_t::OK ); REQUIRE( geo::MeshSourceDescription_Init( &targetMesh, edited->pAllocator, { ids[3] } ) == geo::geometry_status_t::OK );
                REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &original.geometry, { ids[3] } ), &sourceMesh ) == geo::geometry_status_t::OK );
                REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &edited->geometry, { ids[3] } ), &targetMesh ) == geo::geometry_status_t::OK );
                REQUIRE( sourceMesh.vertices.nCount == targetMesh.vertices.nCount ); REQUIRE( sourceMesh.corners.nCount == targetMesh.corners.nCount );
                for ( usize vertex = 0; vertex < sourceMesh.vertices.nCount; ++vertex ) { CHECK( sourceMesh.vertices.pData[vertex].sourceId.value == targetMesh.vertices.pData[vertex].sourceId.value ); }
                for ( usize corner = 0; corner < sourceMesh.corners.nCount; ++corner ) { CHECK( sourceMesh.corners.pData[corner].attributes.uv0.x == targetMesh.corners.pData[corner].attributes.uv0.x ); CHECK( sourceMesh.corners.pData[corner].attributes.uv0.y == targetMesh.corners.pData[corner].attributes.uv0.y ); }
                geo::MeshSourceDescription_Shutdown( &sourceMesh ); geo::MeshSourceDescription_Shutdown( &targetMesh );
                files_t saved; REQUIRE( saved.Save( edited.get() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
                map_wireframe_t restored; REQUIRE( MapWireframe_Init( &restored, loaded.pAllocator ) ); REQUIRE( MapWireframe_Build( &restored, loaded ) == map_status_t::OK );
                for ( const u64 id : ids ) {
                    const auto *a = MapWireframe_FindObject( after, id ), *b = MapWireframe_FindObject( restored, id ); REQUIRE( a ); REQUIRE( b );
                    for ( int other = 0; other < 3; ++other ) { CHECK( axisValue( a->bounds.box.minimum, other ) == Catch::Approx( axisValue( b->bounds.box.minimum, other ) ) ); CHECK( axisValue( a->bounds.box.maximum, other ) == Catch::Approx( axisValue( b->bounds.box.maximum, other ) ) ); }
                }
            }
        }
    }
}

TEST_CASE( "Per-object resize preflights every root and keeps no-op revisions unchanged", "[map][edit][resize-each][validation]" )
{
    map_document_t map; Facility( map );
    const map_bounds_resize_t valid{ { 1, 0, 0 }, { 64, 0, 0 }, CY_FALSE };
    files_t baseline; REQUIRE( baseline.Save( &map ) == map_status_t::OK );
    const auto revision = map.geometry.revision, next = map.nextId, partNext = map.geometry.sourceIds.allocator.next.value;
    const auto reject = [&]( span_t<const u64> ids, map_bounds_resize_t resize, map_status_t expected ) {
        CHECK( MapEdit_Resize( &map, ids, resize ) == expected ); CHECK( map.geometry.revision == revision ); CHECK( map.nextId == next );
        CHECK( map.geometry.sourceIds.allocator.next.value == partNext );
        files_t actual; REQUIRE( actual.Save( &map ) == map_status_t::OK ); CHECK( actual.root == baseline.root );
    };
    const u64 unknown[]{ 1000, 987654321 }; reject( { unknown, 2 }, valid, map_status_t::UNKNOWN_OBJECT );
    const u64 entity[]{ 1000, 150 }; reject( { entity, 2 }, valid, map_status_t::INVALID_ARGUMENT );
    const u64 terrain[]{ 1000, 1100 }; reject( { terrain, 2 }, valid, map_status_t::INVALID_ARGUMENT );
    const u64 roots[]{ 1000, 1200, 1300, 151 };
    auto collapse = valid; collapse.delta.x = -1000000; reject( { roots, 4 }, collapse, map_status_t::INVALID_ARGUMENT );
    auto invalid = valid; invalid.delta.y = 1; reject( { roots, 4 }, invalid, map_status_t::INVALID_ARGUMENT );
    auto noop = valid; noop.delta = {};
    REQUIRE( MapEdit_Resize( &map, { roots, 4 }, noop ) == map_status_t::OK ); CHECK( map.geometry.revision == revision );
    CHECK( MapEdit_Resize( nullptr, { roots, 4 }, valid ) == map_status_t::INVALID_ARGUMENT ); CHECK( MapEdit_Resize( &map, {}, valid ) == map_status_t::INVALID_ARGUMENT );
    map.bReadOnly = CY_TRUE; CHECK( MapEdit_Resize( &map, { roots, 4 }, valid ) == map_status_t::READ_ONLY ); map.bReadOnly = CY_FALSE;
    map.geometry.revision = CY_U64_MAX; CHECK( MapEdit_Resize( &map, { roots, 4 }, valid ) == map_status_t::LIMIT_EXCEEDED ); CHECK( map.geometry.revision == CY_U64_MAX ); map.geometry.revision = revision;
    // Mesh 1200 has distance welding enabled: unequal axis scale cannot
    // preserve its tolerance, so the whole mixed-root batch must stay put.
    reject( { roots, 4 }, valid, map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED );
    // Supported curved patches use the same original control-net bounds as the
    // view, while originless entity helpers remain outside this operation.
    map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &map, &raw ) == map_status_t::OK );
    std::unique_ptr<map_document_t> compatible( raw ); UseUnweldedFacilityMirror( *compatible );
    const auto compatibleRevision = compatible->geometry.revision;
    REQUIRE( MapEdit_Resize( compatible.get(), { roots, 4 }, valid ) == map_status_t::OK ); CHECK( compatible->geometry.revision == compatibleRevision + 1 );
    CHECK( map.geometry.revision == revision ); CHECK( map.nextId == next ); CHECK( map.geometry.sourceIds.allocator.next.value == partNext );
    files_t unchanged; REQUIRE( unchanged.Save( &map ) == map_status_t::OK ); CHECK( unchanged.root == baseline.root );
    files_t resized; REQUIRE( resized.Save( compatible.get() ) == map_status_t::OK ); map_document_t restored; Load( restored, resized );
    map_wireframe_t wire; REQUIRE( MapWireframe_Init( &wire, restored.pAllocator ) ); REQUIRE( MapWireframe_Build( &wire, restored ) == map_status_t::OK );
    const auto *owned = MapWireframe_FindObject( wire, 151 ); REQUIRE( owned ); CHECK( owned->owner == 150 );
    const auto *retained = MapDocument_FindObject( &restored, 1200, nullptr ); REQUIRE( retained ); CHECK( KeyValue_Find( retained, SV( "modifiers" ) ) != nullptr );
}

TEST_CASE( "Per-object resize allocation failures release private preparations and preserve the source", "[map][edit][resize-each][allocation]" )
{
    allocation_audit_t audit;
    {
        map_document_t original; REQUIRE( MapDocument_Create( &original, &audit.allocator, { SV( "Resize allocation contract" ), SV( "reap" ), {} } ) == map_status_t::OK );
        const u64 brush = Box( original ); u64 mesh = 0;
        map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.bounds = { { 160, -80, 0 }, { 320, 80, 0 } };
        REQUIRE( MapEdit_CreatePrimitive( &original, quad, SV( "materials/dev/quad.cymat" ), {}, &mesh ) == map_status_t::OK );
        const usize baseline = audit.bytes; const auto revision = original.geometry.revision, next = original.nextId; const u64 selected[]{ brush, mesh };
        usize failed = 0; bool succeeded = false;
        for ( usize failIndex = 0; failIndex < 1024; ++failIndex ) {
            audit.failAt = CY_USIZE_MAX; map_document_t *copy = nullptr; REQUIRE( MapEdit_Clone( &original, &copy ) == map_status_t::OK );
            audit.calls = 0; audit.failAt = failIndex;
            const auto status = MapEdit_Resize( copy, { selected, 2 }, { { 1, 0, 0 }, { 64, 0, 0 }, CY_FALSE }, true );
            if ( status == map_status_t::OK ) { CHECK( copy->geometry.revision == revision + 1 ); succeeded = true; }
            else { CHECK( status == map_status_t::OUT_OF_MEMORY ); ++failed; }
            delete copy; CHECK( audit.bytes == baseline ); CHECK( original.geometry.revision == revision ); CHECK( original.nextId == next );
            if ( succeeded ) { break; }
        }
        audit.failAt = CY_USIZE_MAX; CHECK( succeeded ); CHECK( failed > 8 ); CHECK( Bounds( original, brush ).box.maximum.x == Catch::Approx( 32 ) );
    }
    CHECK( audit.bytes == 0 );
}

TEST_CASE( "Map duplication gives every part a fresh ID and keeps source surfaces", "[map][edit]" )
{
    map_document_t m; Create( m ); const u64 original = Box( m );
    vector_t<u64> copies; REQUIRE( Vector_Init( &copies, m.pAllocator ) );
    REQUIRE( MapEdit_Duplicate( &m, { &original, 1 }, { 64, 0, 0 }, &copies ) == map_status_t::OK );
    REQUIRE( copies.nCount == 1 ); const u64 duplicate = copies.pData[0];
    REQUIRE( duplicate > original ); CHECK( m.geometry.brushes.nCount == 2 );
    const auto *a = geo::GeometryDocument_FindBrush( &m.geometry, { original } );
    const auto *b = geo::GeometryDocument_FindBrush( &m.geometry, { duplicate } );
    REQUIRE( a ); REQUIRE( b );
    for ( usize i = 0; i < a->sides.nCount; ++i ) { CHECK( a->sides.pData[i].sourceId.value != b->sides.pData[i].sourceId.value ); }
    CHECK( Bounds( m, duplicate ).box.minimum.x == Catch::Approx( 32 ) );
    const auto *surface = geo::GeometryDocument_FindBrushAttributes( &m.geometry, { duplicate } ); REQUIRE( surface );
    CHECK( StringView_Equals( MapMaterials_Path( &m.materials, surface->records.pData[0].material.value ), SV( "materials/blockout/floor_tile.cymat" ) ) );
    REQUIRE( MapEdit_Delete( &m, { &duplicate, 1 } ) == map_status_t::OK );
    CHECK( !geo::GeometryDocument_FindBrush( &m.geometry, { duplicate } ) );
    u64 another = Box( m ); CHECK( another > duplicate );
}

TEST_CASE( "Map working copies retain chunk metadata and duplicate entity-owned brushes", "[map][edit]" )
{
    map_document_t original; Facility( original );
    files_t baseline; REQUIRE( baseline.Save( &original ) == map_status_t::OK );
    map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK );
    std::unique_ptr<map_document_t> copy( raw );
    files_t cloned; REQUIRE( cloned.Save( copy.get() ) == map_status_t::OK );
    CHECK( cloned.root == baseline.root );
    // Door entity owns brush #151; selection includes its child deliberately.
    const u64 selection[]{ 151, 150 };
    vector_t<u64> duplicates; REQUIRE( Vector_Init( &duplicates, copy->pAllocator ) );
    REQUIRE( MapEdit_Duplicate( copy.get(), { selection, 2 }, { 0, 128, 0 }, &duplicates ) == map_status_t::OK );
    REQUIRE( duplicates.nCount == 2 );
    REQUIRE( cloned.Save( copy.get() ) == map_status_t::OK );
    map_document_t reloaded; Load( reloaded, cloned );
    CHECK( MapDocument_FindObject( &reloaded, duplicates.pData[0], nullptr ) != nullptr );
    CHECK( MapDocument_FindObject( &reloaded, duplicates.pData[1], nullptr ) != nullptr );
    CHECK( original.geometry.brushes.nCount + 1 == copy->geometry.brushes.nCount );
}

TEST_CASE( "Map clone allocation failures release every pending allocation and preserve the live map", "[map][edit]" )
{
    allocation_audit_t audit;
    {
        map_document_t original;
        REQUIRE( MapDocument_Create( &original, &audit.allocator, { SV( "OOM contract" ), SV( "reap" ), {} } ) == map_status_t::OK );
        const u64 id = Box( original );
        u64 entity = 0;
        REQUIRE( MapDocument_AddEntity( &original, SV( "default" ), SV( "info_player_start" ), {}, &entity ) == map_status_t::OK );
        const usize baseline = audit.bytes;
        // Memory accounting includes the map structure (stack-owned here) and
        // every common-allocator allocation, including unused CYKV arena space.
        CHECK( MapEdit_EstimateBytes( &original ) == baseline + sizeof( original ) );
        usize failed = 0;
        bool succeeded = false;
        for ( usize failIndex = 0; failIndex < 256; ++failIndex ) {
            audit.calls = 0; audit.failAt = failIndex;
            map_document_t *copy = nullptr;
            const auto status = MapEdit_Clone( &original, &copy );
            if ( status == map_status_t::OK ) {
                REQUIRE( copy );
                CHECK( MapEdit_EstimateBytes( copy ) > sizeof( *copy ) );
                delete copy; succeeded = true;
            } else { CHECK( status == map_status_t::OUT_OF_MEMORY ); CHECK( copy == nullptr ); ++failed; }
            CHECK( audit.bytes == baseline );
            CHECK( original.geometry.brushes.nCount == 1 );
            CHECK( geo::GeometryDocument_FindBrush( &original.geometry, { id } ) != nullptr );
            CHECK( MapDocument_FindObject( &original, entity, nullptr ) != nullptr );
            if ( succeeded ) { break; }
        }
        CHECK( succeeded ); CHECK( failed > 8 );
        audit.failAt = CY_USIZE_MAX;
    }
    CHECK( audit.bytes == 0 );
}

TEST_CASE( "Map edits transform and duplicate open meshes and curved patches with surfacing intact", "[map][edit]" )
{
    map_document_t source; Facility( source );
    map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &source, &raw ) == map_status_t::OK );
    std::unique_ptr<map_document_t> copy( raw );
    const u64 ids[]{ 1200, 1300 };
    files_t welded; REQUIRE( welded.Save( copy.get() ) == map_status_t::OK );
    const auto weldedRevision = copy->geometry.revision, next = copy->nextId, partNext = copy->geometry.sourceIds.allocator.next.value;
    // Put the patch first: refusal of the later welded mesh must also leave
    // the preceding compatible target and all authored surface data intact.
    const u64 weldedIds[]{ 1300, 1200 };
    REQUIRE( MapEdit_Scale( copy.get(), { weldedIds, 2 }, { 2, 1, 1 }, {} ) == map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED );
    CHECK( copy->geometry.revision == weldedRevision ); CHECK( copy->nextId == next );
    CHECK( copy->geometry.sourceIds.allocator.next.value == partNext );
    files_t refused; REQUIRE( refused.Save( copy.get() ) == map_status_t::OK ); CHECK( refused.root == welded.root );
    UseUnweldedFacilityMirror( *copy );
    REQUIRE( MapEdit_Translate( copy.get(), { ids, 2 }, { 32, 64, 16 } ) == map_status_t::OK );
    REQUIRE( MapEdit_Scale( copy.get(), { ids, 2 }, { 2, 1, 1 }, {} ) == map_status_t::OK );
    REQUIRE( MapEdit_Rotate( copy.get(), { ids, 2 }, { 0, 0, 90 }, {} ) == map_status_t::OK );
    const auto *patch = geo::GeometryDocument_FindPatch( &copy->geometry, { 1300 } ); REQUIRE( patch );
    const auto *oldPatch = geo::GeometryDocument_FindPatch( &source.geometry, { 1300 } ); REQUIRE( oldPatch );
    CHECK( patch->controls.pData[0].position.x == Catch::Approx( -560 ) );
    CHECK( patch->controls.pData[0].position.y == Catch::Approx( -64 ) );
    CHECK( patch->controls.pData[0].position.z == Catch::Approx( 144 ) );
    CHECK( patch->controls.pData[0].sourceId.value == oldPatch->controls.pData[0].sourceId.value );
    CHECK( patch->controls.pData[0].uv.x == oldPatch->controls.pData[0].uv.x );
    geo::mesh_source_description_t description;
    REQUIRE( geo::MeshSourceDescription_Init( &description, copy->pAllocator, { 1200 } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &copy->geometry, { 1200 } ), &description ) == geo::geometry_status_t::OK );
    CHECK( description.vertices.pData[0].position.x == Catch::Approx( -320 ) );
    CHECK( description.vertices.pData[0].position.y == Catch::Approx( 576 ) );
    CHECK( description.vertices.pData[0].sourceId.value == 1201 );
    REQUIRE( description.edges.nCount == 1 ); CHECK( description.edges.pData[0].attributes.flags == geo::MESH_EDGE_FLAG_HARD );
    CHECK( description.corners.pData[1].attributes.uv0.x == Catch::Approx( 1 ) );
    vector_t<u64> duplicates; REQUIRE( Vector_Init( &duplicates, copy->pAllocator ) );
    REQUIRE( MapEdit_Duplicate( copy.get(), { ids, 2 }, { 16, 0, 0 }, &duplicates ) == map_status_t::OK );
    REQUIRE( duplicates.nCount == 2 );
    files_t saved; REQUIRE( saved.Save( copy.get() ) == map_status_t::OK );
    map_document_t reloaded; Load( reloaded, saved );
    const auto *meshRecord = MapDocument_FindObject( &reloaded, duplicates.pData[0], nullptr ); REQUIRE( meshRecord );
    CHECK( KeyValue_Find( meshRecord, SV( "modifiers" ) ) != nullptr );
    CHECK( geo::GeometryDocument_FindMesh( &reloaded.geometry, { duplicates.pData[0] } ) != nullptr );
    CHECK( geo::GeometryDocument_FindPatch( &reloaded.geometry, { duplicates.pData[1] } ) != nullptr );
    CHECK( source.geometry.meshes.nCount + 1 == reloaded.geometry.meshes.nCount );
    CHECK( source.geometry.patches.nCount + 1 == reloaded.geometry.patches.nCount );
}

TEST_CASE( "CYKV retained byte accounting includes removed nodes and replaced payloads", "[map][edit]" )
{
    allocation_audit_t audit;
    key_value_document_desc_t desc{}; desc.pAllocator = &audit.allocator;
    auto *doc = KeyValue_CreateDocument( desc ); REQUIRE( doc );
    REQUIRE( KeyValue_SetRootType( doc, key_value_type_t::OBJECT ) );
    auto *field = KeyValue_ObjectInsert( doc, KeyValue_Root( doc ), SV( "text" ), key_value_type_t::STRING ); REQUIRE( field );
    const std::string payload( 50000, 'x' );
    REQUIRE( KeyValue_SetString( doc, field, SV( payload ) ) );
    CHECK( KeyValue_OwnedBytes( doc ) == audit.bytes );
    const usize retained = KeyValue_OwnedBytes( doc );
    REQUIRE( KeyValue_Remove( doc, KeyValue_Root( doc ), field ) );
    CHECK( KeyValue_OwnedBytes( doc ) == retained );
    CHECK( KeyValue_OwnedBytes( doc ) == audit.bytes );
    KeyValue_DestroyDocument( doc ); CHECK( audit.bytes == 0 );
    CHECK( KeyValue_OwnedBytes( nullptr ) == 0 );
}

TEST_CASE( "Map primitive authoring creates complete layered solids with persistent surface projections", "[map][edit][primitive]" )
{
    map_document_t original; Create( original );
    REQUIRE( MapDocument_AddLayer( &original, SV( "detail" ), SV( "Detail geometry" ) ) == map_status_t::OK );
    map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK );
    std::unique_ptr<map_document_t> copy( raw );
    std::vector<u64> roots;
    std::set<u64> allocated;
    const auto material = SV( "materials/dev/sloped_stone.cymat" );
    const usize faces[]{ 6, 5, 18, 17, 240 };
    // This contract concerns the five closed brush primitives. Quad has the
    // separate open-mesh authoring and persistence contract below.
    for ( u32 i = 0; i < static_cast<u32>( map_primitive_kind_t::QUAD ); ++i ) {
        CAPTURE( i );
        map_primitive_desc_t desc{}; desc.kind = static_cast<map_primitive_kind_t>( i );
        const f64 x = static_cast<f64>( i ) * 256.0;
        desc.bounds = { { x - 48, 96, -24 }, { x + 48, 160, 104 } }; desc.worldUnitsPerUv = 64;
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( copy.get(), desc, material, SV( "detail" ), &id ) == map_status_t::OK );
        roots.push_back( id ); CHECK( allocated.insert( id ).second );
        const auto *brush = geo::GeometryDocument_FindBrush( &copy->geometry, { id } ); REQUIRE( brush );
        const auto *attrs = geo::GeometryDocument_FindBrushAttributes( &copy->geometry, { id } ); REQUIRE( attrs );
        REQUIRE( brush->sides.nCount == faces[i] ); REQUIRE( attrs->records.nCount == faces[i] );
        const auto validation = geo::BrushValidation_Deep( brush, copy->geometryPolicy, copy->pAllocator );
        REQUIRE( validation.status == geo::geometry_status_t::OK ); CHECK( validation.bWatertight ); CHECK( validation.eulerCharacteristic == 2 );
        for ( usize j = 0; j < brush->sides.nCount; ++j ) {
            const auto &side = brush->sides.pData[j]; CHECK( allocated.insert( side.sourceId.value ).second );
            const auto &record = attrs->records.pData[side.iAttributeIndex]; const auto &uv = record.uvProjection; const auto n = side.plane.normal;
            CHECK( StringView_Equals( MapMaterials_Path( &copy->materials, record.material.value ), material ) );
            CHECK( uv.worldUnitsPerUv.x == Catch::Approx( 64 ) ); CHECK( uv.worldUnitsPerUv.y == Catch::Approx( 64 ) );
            CHECK( n.x * uv.normal.x + n.y * uv.normal.y + n.z * uv.normal.z == Catch::Approx( 1.0 ) );
            CHECK( n.x * uv.uAxis.x + n.y * uv.uAxis.y + n.z * uv.uAxis.z == Catch::Approx( 0.0 ).margin( 1e-12 ) );
            CHECK( n.x * uv.vAxis.x + n.y * uv.vAxis.y + n.z * uv.vAxis.z == Catch::Approx( 0.0 ).margin( 1e-12 ) );
            math::vec2d_t projected{};
            const math::vec3d_t point{ uv.uAxis.x * 64, uv.uAxis.y * 64, uv.uAxis.z * 64 };
            REQUIRE( math::Uvd_TryProjectPlanarPoint( uv, point, 1e-8, &projected ) );
            CHECK( projected.x == Catch::Approx( 1 ) ); CHECK( projected.y == Catch::Approx( 0 ).margin( 1e-12 ) );
        }
        bool found = false;
        for ( usize j = 0; j < copy->geometryRecords.nCount; ++j ) {
            const auto &record = copy->geometryRecords.pData[j];
            if ( record.id == id ) { REQUIRE( record.iLayer < copy->layers.nCount ); CHECK( StringView_Equals( SV( copy->layers.pData[record.iLayer].id ), SV( "detail" ) ) ); CHECK( record.owner == 0 ); found = true; }
        }
        CHECK( found );
        const auto actual = Bounds( *copy, id ).box;
        CHECK( actual.minimum.x >= desc.bounds.minimum.x - 1e-6 ); CHECK( actual.maximum.x <= desc.bounds.maximum.x + 1e-6 );
        CHECK( actual.minimum.y >= desc.bounds.minimum.y - 1e-6 ); CHECK( actual.maximum.y <= desc.bounds.maximum.y + 1e-6 );
        CHECK( actual.minimum.z >= desc.bounds.minimum.z - 1e-6 ); CHECK( actual.maximum.z <= desc.bounds.maximum.z + 1e-6 );
    }
    CHECK( original.geometry.brushes.nCount == 0 ); CHECK( original.geometryRecords.nCount == 0 ); CHECK( original.materials.entries.nCount == 0 );
    const u64 next = copy->nextId;
    files_t saved; REQUIRE( saved.Save( copy.get() ) == map_status_t::OK );
    map_document_t loaded; Load( loaded, saved ); CHECK( loaded.nextId == next );
    for ( usize i = 0; i < roots.size(); ++i ) {
        const auto *brush = geo::GeometryDocument_FindBrush( &loaded.geometry, { roots[i] } ); REQUIRE( brush );
        const auto *before = geo::GeometryDocument_FindBrush( &copy->geometry, { roots[i] } ); REQUIRE( before );
        const auto *attrs = geo::GeometryDocument_FindBrushAttributes( &loaded.geometry, { roots[i] } ); REQUIRE( attrs );
        REQUIRE( brush->sides.nCount == faces[i] );
        for ( usize j = 0; j < brush->sides.nCount; ++j ) {
            CHECK( brush->sides.pData[j].sourceId.value == before->sides.pData[j].sourceId.value );
            const auto &record = attrs->records.pData[brush->sides.pData[j].iAttributeIndex];
            CHECK( StringView_Equals( MapMaterials_Path( &loaded.materials, record.material.value ), material ) );
            CHECK( record.uvProjection.worldUnitsPerUv.x == Catch::Approx( 64 ) );
        }
        bool found = false;
        for ( usize j = 0; j < loaded.geometryRecords.nCount; ++j ) {
            if ( loaded.geometryRecords.pData[j].id == roots[i] ) { const auto layerIndex = loaded.geometryRecords.pData[j].iLayer; REQUIRE( layerIndex < loaded.layers.nCount ); CHECK( StringView_Equals( SV( loaded.layers.pData[layerIndex].id ), SV( "detail" ) ) ); found = true; }
        }
        CHECK( found );
    }
}

TEST_CASE( "Round and wedge primitive options orient the authored planes and keep ellipsoidal construction frames", "[map][edit][primitive]" )
{
    const auto coordinate = []( math::vec3d_t p, u32 axis ) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; };
    for ( u32 axis = 0; axis < 3; ++axis ) {
        for ( auto kind : { map_primitive_kind_t::CYLINDER, map_primitive_kind_t::CONE } ) {
            for ( f64 ratio : { 0.0, 0.5, 1.0 } ) {
                CAPTURE( axis, kind, ratio );
                map_document_t m; Create( m ); map_primitive_desc_t desc{};
                desc.kind = kind; desc.axis = axis; desc.nSides = 8; desc.coneTopRadiusRatio = ratio;
                desc.bounds = { { 16, -48, 128 }, { 80, 80, 320 } };
                u64 id = 0; REQUIRE( MapEdit_CreatePrimitive( &m, desc, {}, {}, &id ) == map_status_t::OK );
                const auto *brush = geo::GeometryDocument_FindBrush( &m.geometry, { id } ); REQUIRE( brush );
                const bool apex = kind == map_primitive_kind_t::CONE && ratio == 0.0;
                REQUIRE( brush->sides.nCount == ( apex ? 9u : 10u ) );
                const auto actual = Bounds( m, id ).box;
                CHECK( coordinate( actual.minimum, axis ) == Catch::Approx( coordinate( desc.bounds.minimum, axis ) ) );
                CHECK( coordinate( actual.maximum, axis ) == Catch::Approx( coordinate( desc.bounds.maximum, axis ) ) );
                const auto &bottom = brush->sides.pData[apex ? 8 : 9].plane;
                CHECK( coordinate( bottom.normal, axis ) == Catch::Approx( -1 ) );
                CHECK( bottom.d == Catch::Approx( coordinate( desc.bounds.minimum, axis ) ) );
            }
        }
        for ( u32 slope = 0; slope < 3; ++slope ) {
            if ( axis == slope ) { continue; }
            map_document_t m; Create( m ); map_primitive_desc_t desc{}; desc.kind = map_primitive_kind_t::WEDGE;
            desc.wedgeCutAxis = axis; desc.wedgeSlopeAxis = slope; desc.bounds = { { -24, -48, -16 }, { 24, 48, 16 } };
            u64 id = 0; REQUIRE( MapEdit_CreatePrimitive( &m, desc, {}, {}, &id ) == map_status_t::OK );
            const auto *brush = geo::GeometryDocument_FindBrush( &m.geometry, { id } ); REQUIRE( brush );
            REQUIRE( brush->sides.nCount == 5 ); const auto normal = brush->sides.pData[4].plane.normal;
            CHECK( coordinate( normal, axis ) != 0 ); CHECK( coordinate( normal, slope ) != 0 );
        }
    }
    for ( u32 level = 0; level < 2; ++level ) {
        map_document_t m; Create( m ); map_primitive_desc_t desc{}; desc.kind = map_primitive_kind_t::SPHERE;
        desc.sphereSubdivisions = level; desc.bounds = { { 48, 80, -64 }, { 112, 208, 128 } };
        u64 id = 0; REQUIRE( MapEdit_CreatePrimitive( &m, desc, {}, {}, &id ) == map_status_t::OK );
        const auto *brush = geo::GeometryDocument_FindBrush( &m.geometry, { id } ); REQUIRE( brush );
        REQUIRE( brush->sides.nCount == ( level == 0 ? 128u : 240u ) );
        geo::brush_boundary_t boundary{};
        REQUIRE( geo::BrushBoundary_Init( &boundary, m.pAllocator ) == geo::geometry_status_t::OK );
        REQUIRE( geo::BrushBoundary_TryReconstruct( &boundary, brush, m.geometryPolicy ) == geo::geometry_status_t::OK );
        for ( usize i = 0; i < boundary.vertices.nCount; ++i ) {
            const auto p = boundary.vertices.pData[i]; const f64 x = ( p.x - 80 ) / 32, y = ( p.y - 144 ) / 64, z = ( p.z - 32 ) / 96;
            CHECK( x * x + y * y + z * z == Catch::Approx( 1 ).margin( 1e-6 ) );
        }
        geo::BrushBoundary_Shutdown( &boundary );
    }
}

TEST_CASE( "Block spheres have convex round silhouettes and editable latitude bands", "[map][edit][primitive][sphere-roundness]" )
{
    for ( u32 detail = 0; detail < 2; ++detail ) {
        CAPTURE( detail );
        map_document_t map; Create( map );
        map_primitive_desc_t desc{}; desc.kind = map_primitive_kind_t::SPHERE; desc.sphereSubdivisions = detail;
        desc.bounds = { { 48, 80, -64 }, { 112, 208, 128 } };
        const u32 sides = detail == 0 ? 16u : 24u, bands = detail == 0 ? 8u : 10u;
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( &map, desc, {}, {}, &id ) == map_status_t::OK );
        const auto *brush = geo::GeometryDocument_FindBrush( &map.geometry, { id } ); REQUIRE( brush );
        geo::brush_boundary_t boundary{};
        REQUIRE( geo::BrushBoundary_Init( &boundary, map.pAllocator ) == geo::geometry_status_t::OK );
        REQUIRE( geo::BrushBoundary_TryReconstruct( &boundary, brush, map.geometryPolicy ) == geo::geometry_status_t::OK );
        REQUIRE( boundary.vertices.nCount == sides * ( bands - 1u ) + 2u );
        REQUIRE( boundary.faces.nCount == sides * bands );
        std::vector<math::vec3d_t> unit;
        for ( usize i = 0; i < boundary.vertices.nCount; ++i ) {
            const auto p = boundary.vertices.pData[i];
            unit.push_back( { ( p.x - 80 ) / 32, ( p.y - 144 ) / 64, ( p.z - 32 ) / 96 } );
            const auto q = unit.back();
            // All vertices stay on the ellipsoid: neither spikes nor an
            // accidental box/grid projection may move them off its surface.
            CHECK( q.x * q.x + q.y * q.y + q.z * q.z == Catch::Approx( 1.0 ).margin( 1e-7 ) );
        }
        for ( usize i = 0; i < brush->sides.nCount; ++i ) {
            const auto plane = brush->sides.pData[i].plane;
            f64 maximumOutside = -std::numeric_limits<f64>::infinity();
            for ( usize v = 0; v < boundary.vertices.nCount; ++v ) {
                const auto p = boundary.vertices.pData[v];
                maximumOutside = std::max( maximumOutside, plane.normal.x * p.x + plane.normal.y * p.y + plane.normal.z * p.z + plane.d );
            }
            CHECK( maximumOutside <= 1e-7 );
            CHECK( plane.normal.x * 80 + plane.normal.y * 144 + plane.normal.z * 32 + plane.d < 0.0 );
        }
        usize triangles = 0, quads = 0;
        for ( usize i = 0; i < boundary.faces.nCount; ++i ) {
            const auto corners = boundary.faces.pData[i].cVertices;
            triangles += corners == 3u; quads += corners == 4u;
        }
        CHECK( triangles == 2u * sides ); CHECK( quads == sides * ( bands - 2u ) );
        // Sample support in every orthographic silhouette, between vertices
        // as well as through them. This catches a pointy low-detail fallback.
        for ( u32 axis = 0; axis < 3u; ++axis ) {
            f64 minimumSupport = 1.0;
            for ( u32 sample = 0; sample < 360u; ++sample ) {
                const f64 angle = static_cast<f64>( sample ) * 3.14159265358979323846 / 180.0;
                f64 support = -1.0;
                for ( const auto p : unit ) {
                    const f64 c[]{ p.x, p.y, p.z };
                    support = std::max( support, c[( axis + 1u ) % 3u] * std::cos( angle ) + c[( axis + 2u ) % 3u] * std::sin( angle ) );
                }
                minimumSupport = std::min( minimumSupport, support );
            }
            CHECK( minimumSupport >= ( detail == 0 ? 0.98 : 0.987 ) );
        }
        const auto actual = Bounds( map, id ).box;
        CHECK( actual.minimum.x == Catch::Approx( desc.bounds.minimum.x ) );
        CHECK( actual.minimum.y == Catch::Approx( desc.bounds.minimum.y ) );
        CHECK( actual.minimum.z == Catch::Approx( desc.bounds.minimum.z ) );
        CHECK( actual.maximum.x == Catch::Approx( desc.bounds.maximum.x ) );
        CHECK( actual.maximum.y == Catch::Approx( desc.bounds.maximum.y ) );
        CHECK( actual.maximum.z == Catch::Approx( desc.bounds.maximum.z ) );
        geo::BrushBoundary_Shutdown( &boundary );
    }
}

TEST_CASE( "Map primitive invalid requests do not allocate IDs or change authored state", "[map][edit][primitive]" )
{
    map_document_t m; Create( m );
    map_primitive_desc_t valid{}; valid.bounds = { { -32, -16, 0 }, { 32, 16, 64 } };
    const auto next = m.nextId, revision = m.geometry.revision;
    const auto check = [&]( const map_primitive_desc_t &desc, map_status_t expected, string_view_t material = {}, string_view_t layer = {} ) {
        u64 output = 987654321;
        CHECK( MapEdit_CreatePrimitive( &m, desc, material, layer, &output ) == expected );
        CHECK( output == 987654321 ); CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision );
        CHECK( m.geometry.brushes.nCount == 0 ); CHECK( m.geometryRecords.nCount == 0 ); CHECK( m.materials.entries.nCount == 0 );
    };
    auto desc = valid; desc.kind = map_primitive_kind_t::COUNT; check( desc, map_status_t::INVALID_ARGUMENT );
    desc.kind = static_cast<map_primitive_kind_t>( 255 ); check( desc, map_status_t::INVALID_ARGUMENT );
    desc = valid; desc.bounds.maximum.x = desc.bounds.minimum.x; check( desc, map_status_t::INVALID_ARGUMENT );
    desc = valid; desc.bounds.maximum.x = -64; check( desc, map_status_t::INVALID_ARGUMENT );
    desc = valid; desc.bounds.maximum.x = std::numeric_limits<f64>::infinity(); check( desc, map_status_t::INVALID_ARGUMENT );
    desc = valid; desc.bounds.maximum.x = geo::kMeshSourceCoordinateMax * 2; check( desc, map_status_t::INVALID_ARGUMENT );
    for ( f64 uv : { 0.0, -1.0, std::numeric_limits<f64>::quiet_NaN() } ) { desc = valid; desc.worldUnitsPerUv = uv; check( desc, map_status_t::INVALID_ARGUMENT ); }
    desc = valid; desc.kind = map_primitive_kind_t::WEDGE; desc.wedgeSlopeAxis = desc.wedgeCutAxis; check( desc, map_status_t::INVALID_ARGUMENT );
    desc.wedgeSlopeAxis = 3; check( desc, map_status_t::INVALID_ARGUMENT );
    desc = valid; desc.kind = map_primitive_kind_t::CYLINDER; desc.axis = 3; check( desc, map_status_t::INVALID_ARGUMENT );
    desc.axis = 2; desc.nSides = 2; check( desc, map_status_t::INVALID_ARGUMENT );
    desc.nSides = 129; check( desc, map_status_t::LIMIT_EXCEEDED );
    desc.nSides = 16; m.geometryPolicy.limits.cBrushSidesPerBrushMax = 17; check( desc, map_status_t::LIMIT_EXCEEDED );
    m.geometryPolicy.limits.cBrushSidesPerBrushMax = 256;
    desc = valid; desc.kind = map_primitive_kind_t::CONE;
    for ( f64 ratio : { -0.1, 1.1, std::numeric_limits<f64>::quiet_NaN() } ) { desc.coneTopRadiusRatio = ratio; check( desc, map_status_t::INVALID_ARGUMENT ); }
    desc = valid; desc.kind = map_primitive_kind_t::SPHERE; desc.sphereSubdivisions = 2; check( desc, map_status_t::LIMIT_EXCEEDED );
    desc.sphereSubdivisions = CY_U32_MAX; check( desc, map_status_t::LIMIT_EXCEEDED );
    check( valid, map_status_t::UNKNOWN_LAYER, {}, SV( "missing" ) );
    check( valid, map_status_t::INVALID_ARGUMENT, SV( "materials\\bad.cymat" ) );
    check( valid, map_status_t::INVALID_ARGUMENT, { nullptr, 5 } );
    check( valid, map_status_t::INVALID_ARGUMENT, {}, { nullptr, 5 } );
    const std::string longPath( MAP_MATERIAL_PATH_MAX + 1, 'a' ); check( valid, map_status_t::INVALID_ARGUMENT, SV( longPath ) );
    m.bReadOnly = CY_TRUE; check( valid, map_status_t::READ_ONLY ); m.bReadOnly = CY_FALSE;
    // A positive but numerically collapsed volume must fail complete solid
    // validation; it must not be adopted just because its planes are finite.
    desc = valid; desc.bounds.maximum = { -32 + 1e-10, -16 + 1e-10, 1e-10 };
    check( desc, map_status_t::GEOMETRY_FAILED );
}

TEST_CASE( "Quad authoring creates one planar authored mesh with material UV identity and exact persistence", "[map][edit][primitive][quad][persistence]" )
{
    const auto coordinate = []( math::vec3d_t p, u32 axis ) { return axis == 0u ? p.x : axis == 1u ? p.y : p.z; };
    for ( u32 axis = 0; axis < 3; ++axis ) {
        for ( const bool thickEnvelope : { false, true } ) {
            CAPTURE( axis, thickEnvelope );
            map_document_t map; Create( map );
            REQUIRE( MapDocument_AddLayer( &map, SV( "detail" ), SV( "Quad detail" ) ) == map_status_t::OK );
            map_primitive_desc_t primitive{}; primitive.kind = map_primitive_kind_t::QUAD; primitive.axis = axis;
            primitive.bounds = { { -13.5, 7.25, -31.125 }, { 82.5, 151.25, 16.875 } }; primitive.worldUnitsPerUv = 48;
            if ( !thickEnvelope ) {
                if ( axis == 0 ) { primitive.bounds.maximum.x = primitive.bounds.minimum.x; }
                else if ( axis == 1 ) { primitive.bounds.maximum.y = primitive.bounds.minimum.y; }
                else { primitive.bounds.maximum.z = primitive.bounds.minimum.z; }
            }
            const auto next = map.nextId, revision = map.geometry.revision;
            u64 id = 0;
            REQUIRE( MapEdit_CreatePrimitive( &map, primitive, SV( "materials/dev/quad.cymat" ), SV( "detail" ), &id ) == map_status_t::OK );
            CHECK( id == next ); CHECK( map.nextId == next + 6 ); CHECK( map.geometry.revision == revision + 1 );
            CHECK( map.geometry.brushes.nCount == 0 ); REQUIRE( map.geometry.meshes.nCount == 1 );
            const auto *mesh = geo::GeometryDocument_FindMesh( &map.geometry, { id } ); REQUIRE( mesh != nullptr );
            REQUIRE( map.geometryRecords.nCount == 1 );
            CHECK( map.geometryRecords.pData[0].id == id ); CHECK( map.geometryRecords.pData[0].owner == 0 );
            CHECK( StringView_Equals( SV( map.layers.pData[map.geometryRecords.pData[0].iLayer].id ), SV( "detail" ) ) );
            geo::mesh_source_description_t authored{};
            REQUIRE( geo::MeshSourceDescription_Init( &authored, map.pAllocator, { id } ) == geo::geometry_status_t::OK );
            REQUIRE( geo::MeshSource_TryDescribe( mesh, &authored ) == geo::geometry_status_t::OK );
            REQUIRE( authored.vertices.nCount == 4 ); REQUIRE( authored.faces.nCount == 1 ); REQUIRE( authored.corners.nCount == 4 );
            const auto &face = authored.faces.pData[0]; CHECK( face.cCorners == 4 ); CHECK( face.attributes.smoothingGroups == 0 );
            CHECK( StringView_Equals( MapMaterials_Path( &map.materials, face.attributes.material.value ), SV( "materials/dev/quad.cymat" ) ) );
            std::set<u64> identities{ id }; CHECK( identities.insert( face.sourceId.value ).second );
            for ( usize i = 0; i < authored.vertices.nCount; ++i ) {
                const auto &vertex = authored.vertices.pData[i];
                CHECK( identities.insert( vertex.sourceId.value ).second );
                CHECK( coordinate( vertex.position, axis ) == coordinate( primitive.bounds.minimum, axis ) );
                for ( u32 other = 0; other < 3; ++other ) {
                    if ( other == axis ) { continue; }
                    const f64 value = coordinate( vertex.position, other );
                    CHECK( ( value == coordinate( primitive.bounds.minimum, other ) || value == coordinate( primitive.bounds.maximum, other ) ) );
                }
            }
            CHECK( identities.size() == 6 ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
            const auto position = [&]( u32 corner ) { return authored.vertices.pData[authored.corners.pData[face.iFirstCorner + corner].iVertex].position; };
            const auto winding = math::Vec3d_Cross( math::Vec3d_Subtract( position( 1 ), position( 0 ) ), math::Vec3d_Subtract( position( 2 ), position( 0 ) ) );
            CHECK( coordinate( winding, axis ) > 0 );
            for ( u32 corner = 0; corner < 4; ++corner ) {
                const auto &a = authored.corners.pData[face.iFirstCorner + corner].attributes.uv0;
                const auto &b = authored.corners.pData[face.iFirstCorner + ( corner + 1u ) % 4u].attributes.uv0;
                const auto delta = math::Vec3d_Subtract( position( corner ), position( ( corner + 1u ) % 4u ) );
                const f64 worldSquared = math::Vec3d_Dot( delta, delta );
                CHECK( ( a.x - b.x ) * ( a.x - b.x ) + ( a.y - b.y ) * ( a.y - b.y ) == Catch::Approx( worldSquared / ( 48.0 * 48.0 ) ) );
            }
            const auto bounds = MapGeometry_MeshBounds( *mesh, map.pAllocator ); REQUIRE( bounds.bHas );
            CHECK( coordinate( bounds.box.minimum, axis ) == coordinate( primitive.bounds.minimum, axis ) );
            CHECK( coordinate( bounds.box.maximum, axis ) == coordinate( primitive.bounds.minimum, axis ) );
            files_t saved; REQUIRE( saved.Save( &map ) == map_status_t::OK );
            map_document_t loaded; Load( loaded, saved ); CHECK( loaded.nextId == map.nextId );
            const auto *savedMesh = geo::GeometryDocument_FindMesh( &loaded.geometry, { id } ); REQUIRE( savedMesh != nullptr );
            geo::mesh_source_description_t restored{};
            REQUIRE( geo::MeshSourceDescription_Init( &restored, loaded.pAllocator, { id } ) == geo::geometry_status_t::OK );
            REQUIRE( geo::MeshSource_TryDescribe( savedMesh, &restored ) == geo::geometry_status_t::OK );
            CHECK( geo::MeshSourceDescription_Equal( &authored, &restored ) );
            CHECK( StringView_Equals( MapMaterials_Path( &loaded.materials, restored.faces.pData[0].attributes.material.value ), SV( "materials/dev/quad.cymat" ) ) );
            geo::MeshSourceDescription_Shutdown( &restored ); geo::MeshSourceDescription_Shutdown( &authored );
        }
    }
}

TEST_CASE( "Quad rejects invalid planes and exhausted revisions without advancing identity", "[map][edit][primitive][quad][validation]" )
{
    map_document_t map; Create( map );
    map_primitive_desc_t valid{}; valid.kind = map_primitive_kind_t::QUAD; valid.bounds = { { -64, -32, 12 }, { 64, 32, 12 } };
    const auto next = map.nextId, revision = map.geometry.revision;
    const auto reject = [&]( map_primitive_desc_t desc, map_status_t expected ) {
        u64 id = 987654321;
        CHECK( MapEdit_CreatePrimitive( &map, desc, {}, {}, &id ) == expected ); CHECK( id == 987654321 );
        CHECK( map.nextId == next ); CHECK( map.geometry.brushes.nCount == 0 ); CHECK( map.geometry.meshes.nCount == 0 );
        CHECK( map.geometryRecords.nCount == 0 );
    };
    auto invalid = valid; invalid.axis = 3; reject( invalid, map_status_t::INVALID_ARGUMENT );
    invalid = valid; invalid.bounds.maximum.x = invalid.bounds.minimum.x; reject( invalid, map_status_t::INVALID_ARGUMENT );
    invalid = valid; invalid.bounds.maximum.z = invalid.bounds.minimum.z - 1; reject( invalid, map_status_t::INVALID_ARGUMENT );
    invalid = valid; invalid.bounds.minimum.y = std::numeric_limits<f64>::infinity(); reject( invalid, map_status_t::INVALID_ARGUMENT );
    map.bReadOnly = CY_TRUE; reject( valid, map_status_t::READ_ONLY ); map.bReadOnly = CY_FALSE;
    map.geometry.revision = CY_U64_MAX; reject( valid, map_status_t::LIMIT_EXCEEDED ); map.geometry.revision = revision;
    CHECK( map.geometry.revision == revision );
}

TEST_CASE( "Every primitive allocation failure discards private construction without leaking or publishing", "[map][edit][primitive][allocation]" )
{
    allocation_audit_t audit;
    {
        map_document_t original;
        REQUIRE( MapDocument_Create( &original, &audit.allocator, { SV( "Primitive failure contract" ), SV( "reap" ), {} } ) == map_status_t::OK );
        const u64 originalId = Box( original );
        const usize baseline = audit.bytes, materialCount = original.materials.entries.nCount;
        const auto next = original.nextId, revision = original.geometry.revision;
        for ( u32 i = 0; i <= static_cast<u32>( map_primitive_kind_t::COUNT ); ++i ) {
            const bool directSphere = i == static_cast<u32>( map_primitive_kind_t::COUNT );
            const auto create = directSphere ? &MapEdit_CreateSphereMesh : &MapEdit_CreatePrimitive;
            map_primitive_desc_t desc{}; desc.kind = directSphere ? map_primitive_kind_t::SPHERE : static_cast<map_primitive_kind_t>( i ); desc.sphereSubdivisions = 0;
            desc.bounds = { { 64, -32, 16 }, { 160, 32, 144 } }; desc.nSides = 5; desc.worldUnitsPerUv = 48;
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK );
            std::unique_ptr<map_document_t> successful( raw );
            audit.calls = 0;
            u64 id = 0;
            REQUIRE( create( successful.get(), desc, SV( "materials/new_primitive.cymat" ), {}, &id ) == map_status_t::OK );
            const usize nAllocations = audit.calls; REQUIRE( nAllocations > 8 ); successful.reset(); CHECK( audit.bytes == baseline );
            for ( usize failure = 0; failure < nAllocations; ++failure ) {
                CAPTURE( i, directSphere, failure, nAllocations );
                audit.failAt = CY_USIZE_MAX; raw = nullptr;
                REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK );
                std::unique_ptr<map_document_t> working( raw );
                audit.calls = 0; audit.failAt = failure; id = 987654321;
                CHECK( create( working.get(), desc, SV( "materials/new_primitive.cymat" ), {}, &id ) == map_status_t::OUT_OF_MEMORY );
                CHECK( id == 987654321 ); working.reset();
                CHECK( audit.bytes == baseline ); CHECK( original.geometry.brushes.nCount == 1 );
                CHECK( original.geometryRecords.nCount == 1 ); CHECK( original.materials.entries.nCount == materialCount );
                CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision );
                CHECK( geo::GeometryDocument_FindBrush( &original.geometry, { originalId } ) != nullptr );
            }
            audit.failAt = CY_USIZE_MAX;
        }
    }
    CHECK( audit.bytes == 0 );
}

TEST_CASE( "Affected-object collection includes owned geometry and descendants without mutating the map", "[map][edit][affected-object-query]" )
{
    map_document_t map; Create( map );
    const u64 owned = Box( map ), childOwned = Box( map ), loose = Box( map );
    u64 parent{}, child{}, grandchild{}, owner{};
    REQUIRE( MapDocument_AddEntity( &map, SV( map.layers.pData[0].id ), SV( "info_target" ), {}, &parent ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( &map, SV( map.layers.pData[0].id ), SV( "info_target" ), {}, &child ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( &map, SV( map.layers.pData[0].id ), SV( "info_target" ), {}, &grandchild ) == map_status_t::OK );
    REQUIRE( MapCreate_TieToEntity( &map, { &owned, 1 }, parent, SV( "func_detail" ), &owner ) == map_status_t::OK );
    REQUIRE( MapCreate_TieToEntity( &map, { &childOwned, 1 }, grandchild, SV( "func_detail" ), &owner ) == map_status_t::OK );
    REQUIRE( MapHierarchy_SetParent( &map, { &child, 1 }, parent ) == map_status_t::OK );
    REQUIRE( MapHierarchy_SetParent( &map, { &grandchild, 1 }, child ) == map_status_t::OK );
    files_t before; REQUIRE( before.Save( &map ) == map_status_t::OK );
    const auto nextId = map.nextId, revision = map.geometry.revision;
    map.bReadOnly = CY_TRUE;
    vector_t<u64> affected{}; REQUIRE( Vector_Init( &affected, Allocator_GetSystem() ) );
    const u64 roots[]{ parent, child, parent };
    REQUIRE( MapEdit_CollectAffectedObjects( &map, { roots, 3 }, &affected ) == map_status_t::OK );
    CHECK( std::set<u64>( affected.pData, affected.pData + affected.nCount ) == std::set<u64>{ parent, owned, child, grandchild, childOwned } );
    CHECK( affected.nCount == 5 );
    // Borrowing the output as input is supported, without releasing it early.
    REQUIRE( MapEdit_CollectAffectedObjects( &map, { affected.pData, affected.nCount }, &affected ) == map_status_t::OK );
    CHECK( affected.nCount == 5 );
    REQUIRE( MapEdit_CollectAffectedObjects( &map, { &child, 1 }, &affected ) == map_status_t::OK );
    CHECK( std::set<u64>( affected.pData, affected.pData + affected.nCount ) == std::set<u64>{ child, grandchild, childOwned } );
    auto *storage = affected.pData; const auto count = affected.nCount, capacity = affected.nCapacity;
    const u64 invalid = map.nextId + 100;
    CHECK( MapEdit_CollectAffectedObjects( &map, { &invalid, 1 }, &affected ) != map_status_t::OK );
    CHECK( affected.pData == storage ); CHECK( affected.nCount == count ); CHECK( affected.nCapacity == capacity );
    CHECK( MapEdit_CollectAffectedObjects( nullptr, { &loose, 1 }, &affected ) == map_status_t::INVALID_ARGUMENT );
    vector_t<u64> uninitialized{};
    CHECK( MapEdit_CollectAffectedObjects( &map, { &parent, 1 }, &uninitialized ) == map_status_t::INVALID_ARGUMENT );
    REQUIRE( MapEdit_CollectAffectedObjects( &map, {}, &affected ) == map_status_t::OK ); CHECK( affected.nCount == 0 );
    CHECK( map.bReadOnly ); CHECK( map.nextId == nextId ); CHECK( map.geometry.revision == revision );
    map.bReadOnly = CY_FALSE;
    files_t after; REQUIRE( after.Save( &map ) == map_status_t::OK ); CHECK( after.root == before.root );
}

TEST_CASE( "Affected-object query allocation failures preserve borrowed input and output", "[map][edit][affected-object-query][allocation]" )
{
    map_document_t map; Create( map );
    u64 parent{};
    REQUIRE( MapDocument_AddEntity( &map, SV( map.layers.pData[0].id ), SV( "info_target" ), {}, &parent ) == map_status_t::OK );
    // Force several result growth allocations while following a deep chain.
    u64 previous = parent;
    for ( usize i = 0; i < 20; ++i ) {
        u64 child{}; REQUIRE( MapDocument_AddEntity( &map, SV( map.layers.pData[0].id ), SV( "info_target" ), {}, &child ) == map_status_t::OK );
        REQUIRE( MapHierarchy_SetParent( &map, { &child, 1 }, previous ) == map_status_t::OK ); previous = child;
    }
    files_t before; REQUIRE( before.Save( &map ) == map_status_t::OK );
    allocation_audit_t audit;
    usize allocations{};
    {
        vector_t<u64> output{}; REQUIRE( Vector_Init( &output, &audit.allocator ) );
        REQUIRE( Vector_PushBack( &output, parent ) ); audit.calls = 0;
        REQUIRE( MapEdit_CollectAffectedObjects( &map, { output.pData, output.nCount }, &output ) == map_status_t::OK );
        allocations = audit.calls; REQUIRE( allocations > 1 ); REQUIRE( output.nCount == 21 );
    }
    CHECK( audit.bytes == 0 );
    for ( usize fail = 0; fail < allocations; ++fail ) {
        DYNAMIC_SECTION( "allocation=" << fail ) {
            vector_t<u64> output{}; REQUIRE( Vector_Init( &output, &audit.allocator ) );
            REQUIRE( Vector_PushBack( &output, parent ) );
            auto *storage = output.pData; const auto count = output.nCount, capacity = output.nCapacity, bytes = audit.bytes;
            audit.calls = 0; audit.failAt = fail;
            CHECK( MapEdit_CollectAffectedObjects( &map, { output.pData, output.nCount }, &output ) == map_status_t::OUT_OF_MEMORY );
            audit.failAt = CY_USIZE_MAX;
            CHECK( output.pData == storage ); CHECK( output.nCount == count ); CHECK( output.nCapacity == capacity );
            CHECK( output.pAllocator == &audit.allocator ); CHECK( output.pData[0] == parent ); CHECK( audit.bytes == bytes );
        }
        CHECK( audit.bytes == 0 );
    }
    files_t after; REQUIRE( after.Save( &map ) == map_status_t::OK ); CHECK( after.root == before.root );
}

TEST_CASE( "Linked prefab scaling preserves the placed surface for representable world factors", "[map][edit][prefab-scale]" )
{
    struct example_t { math::vec3d_t angles, factors; };
    const example_t examples[]{ { {}, { 2, 3, 4 } }, { { 0, 90, 0 }, { 2, 3, 4 } },
        { { 0, -90, 0 }, { 2, 3, 4 } }, { { 0, 45, 0 }, { 2, 2, 3 } },
        { { 90, 0, 0 }, { 2, 3, 4 } }, { { 23, 41, -19 }, { 2, 2, 2 } } };
    for ( const auto &example : examples ) {
        CAPTURE( example.angles.x, example.angles.y, example.angles.z );
        map_document_t map; Facility( map );
        const math::vec3d_t origin{ 17, -23, 31 }, oldScale{ 2, 3, 4 }, pivot{ -11, 7, 5 };
        SetPrefabFixtureVector( map, "origin", origin ); SetPrefabFixtureVector( map, "angles", example.angles );
        SetPrefabFixtureVector( map, "scale", oldScale );
        const auto revision = map.geometry.revision, next = map.nextId, partNext = map.geometry.sourceIds.allocator.next.value;
        const u64 id = 200;
        REQUIRE( MapEdit_Scale( &map, { &id, 1 }, example.factors, pivot ) == map_status_t::OK );
        CHECK( map.geometry.revision == revision + 1 ); CHECK( map.nextId == next ); CHECK( map.geometry.sourceIds.allocator.next.value == partNext );
        const auto angles = PrefabFixtureVector( map, "angles" ), scale = PrefabFixtureVector( map, "scale" ), translated = PrefabFixtureVector( map, "origin" );
        CHECK( math::Vec3d_EqualsExact( angles, example.angles ) );
        const auto checkSurface = [&]( math::vec3d_t actualOrigin, math::vec3d_t actualScale ) {
            for ( const math::vec3d_t p : { math::vec3d_t{ 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { -3, 5, 7 } } ) {
                const auto before = PrefabFixturePoint( p, origin, example.angles, oldScale );
                const math::vec3d_t expected{ pivot.x + example.factors.x * ( before.x - pivot.x ),
                    pivot.y + example.factors.y * ( before.y - pivot.y ), pivot.z + example.factors.z * ( before.z - pivot.z ) };
                const auto actual = PrefabFixturePoint( p, actualOrigin, example.angles, actualScale );
                CHECK( actual.x == Catch::Approx( expected.x ).margin( 1e-10 ) ); CHECK( actual.y == Catch::Approx( expected.y ).margin( 1e-10 ) );
                CHECK( actual.z == Catch::Approx( expected.z ).margin( 1e-10 ) );
            }
        };
        checkSurface( translated, scale );
        files_t saved; REQUIRE( saved.Save( &map ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
        checkSurface( PrefabFixtureVector( loaded, "origin" ), PrefabFixtureVector( loaded, "scale" ) );
        const auto *record = MapDocument_FindObject( &loaded, id, nullptr ); REQUIRE( record );
        string_view_t text{}; REQUIRE( KeyValue_GetString( KeyValue_Find( record, SV( "source" ) ), &text ) );
        CHECK( StringView_Equals( text, SV( "prefabs/security_door.cyprefab" ) ) );
        REQUIRE( KeyValue_GetString( KeyValue_Find( KeyValue_Find( record, SV( "overrides" ) ), SV( "door.name" ) ), &text ) );
        CHECK( StringView_Equals( text, SV( "yard_door" ) ) );
    }
}

TEST_CASE( "Linked prefab scale accepts numeric forms and refuses shear before any mixed root changes", "[map][edit][prefab-scale]" )
{
    for ( const auto type : { key_value_type_t::NULL_VALUE, key_value_type_t::F64, key_value_type_t::I64, key_value_type_t::U64 } ) {
        CAPTURE( static_cast<int>( type ) ); map_document_t map; Facility( map );
        map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &map, 200, &store ); REQUIRE( record ); REQUIRE( store );
        REQUIRE( KeyValue_Find( record, SV( "scale" ) ) == nullptr );
        if ( type != key_value_type_t::NULL_VALUE ) {
            auto *value = KeyValue_ObjectInsert( store->store.pDocument, record, SV( "scale" ), type ); REQUIRE( value );
            if ( type == key_value_type_t::F64 ) { REQUIRE( KeyValue_SetF64( store->store.pDocument, value, 2 ) ); }
            else if ( type == key_value_type_t::I64 ) { REQUIRE( KeyValue_SetI64( store->store.pDocument, value, 2 ) ); }
            else { REQUIRE( KeyValue_SetU64( store->store.pDocument, value, 2 ) ); }
        }
        const u64 id = 200; REQUIRE( MapEdit_Scale( &map, { &id, 1 }, { 3, 3, 3 }, {} ) == map_status_t::OK );
        f64 scale = 0; REQUIRE( KeyValue_GetF64( KeyValue_Find( record, SV( "scale" ) ), &scale ) );
        CHECK( scale == ( type == key_value_type_t::NULL_VALUE ? 3 : 6 ) );
    }
    for ( const int invalid : { 0, 1, 2, 3 } ) {
        CAPTURE( invalid ); map_document_t map; Facility( map );
        SetPrefabFixtureVector( map, "angles", { 0, 45, 0 } );
        SetPrefabFixtureVector( map, "scale", invalid == 1 ? math::vec3d_t{ -1, 1, 1 } : invalid == 2 ? math::vec3d_t{ 0, 1, 1 } : math::vec3d_t{ 1, 1, 1 } );
        if ( invalid == 3 ) {
            map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &map, 200, &store );
            auto *value = KeyValue_Find( record, SV( "scale" ) ); REQUIRE( KeyValue_SetString( store->store.pDocument, value, SV( "bad scale" ) ) );
        }
        files_t before; REQUIRE( before.Save( &map ) == map_status_t::OK );
        const auto revision = map.geometry.revision, next = map.nextId, partNext = map.geometry.sourceIds.allocator.next.value;
        const u64 ids[]{ 1000, 200 }; // Compatible brush first; no target may move on refusal.
        CHECK( MapEdit_Scale( &map, { ids, 2 }, { 2, 1, 1 }, { -11, 7, 5 } ) ==
            ( invalid == 0 ? map_status_t::PREFAB_TRANSFORM_UNSUPPORTED : map_status_t::INVALID_ARGUMENT ) );
        CHECK( map.geometry.revision == revision ); CHECK( map.nextId == next ); CHECK( map.geometry.sourceIds.allocator.next.value == partNext );
        files_t after; REQUIRE( after.Save( &map ) == map_status_t::OK ); CHECK( after.root == before.root );
    }
}

TEST_CASE( "Linked prefab scale distinguishes exact quarters from amplified near-axis shear", "[map][edit][prefab-scale]" )
{
    for ( const f64 yaw : { 1e-12, 90.0 + 1e-12 } ) {
        CAPTURE( yaw ); map_document_t map; Facility( map );
        SetPrefabFixtureVector( map, "origin", {} ); SetPrefabFixtureVector( map, "angles", { 0, yaw, 0 } );
        SetPrefabFixtureVector( map, "scale", { 1, 1, 1 } );
        // Independent placed-axis test: world scaling introduces a component
        // along the other rotated local axis. No diagonal local scale can do so.
        const auto x = PrefabFixturePoint( { 1, 0, 0 }, {}, { 0, yaw, 0 }, { 1, 1, 1 } );
        const auto y = PrefabFixturePoint( { 0, 1, 0 }, {}, { 0, yaw, 0 }, { 1, 1, 1 } );
        const f64 crossAxis = 1e16 * x.x * y.x + x.y * y.y + x.z * y.z;
        REQUIRE( std::fabs( crossAxis ) > 100 );
        files_t before; REQUIRE( before.Save( &map ) == map_status_t::OK );
        const auto revision = map.geometry.revision, next = map.nextId, partNext = map.geometry.sourceIds.allocator.next.value;
        const u64 ids[]{ 1000, 200 };
        CHECK( MapEdit_Scale( &map, { ids, 2 }, { 1e16, 1, 1 }, {} ) == map_status_t::PREFAB_TRANSFORM_UNSUPPORTED );
        CHECK( map.geometry.revision == revision ); CHECK( map.nextId == next ); CHECK( map.geometry.sourceIds.allocator.next.value == partNext );
        files_t after; REQUIRE( after.Save( &map ) == map_status_t::OK ); CHECK( after.root == before.root );
    }
    for ( const f64 yaw : { 90.0, -90.0, 450.0 } ) {
        CAPTURE( yaw ); map_document_t map; Facility( map );
        SetPrefabFixtureVector( map, "origin", {} ); SetPrefabFixtureVector( map, "angles", { 0, yaw, 0 } );
        SetPrefabFixtureVector( map, "scale", { 2, 3, 4 } );
        const u64 id = 200;
        REQUIRE( MapEdit_Scale( &map, { &id, 1 }, { 1e16, 1, 1 }, {} ) == map_status_t::OK );
        const auto scale = PrefabFixtureVector( map, "scale" );
        CHECK( scale.x == 2 ); CHECK( scale.y == 3e16 ); CHECK( scale.z == 4 );
        // Exact signed coordinate permutation, independent of trigonometry.
        const f64 sign = yaw == -90 ? -1 : 1;
        const math::vec3d_t actual{ sign * 3 * scale.y, sign * 2 * scale.x, 7 * scale.z };
        CHECK( actual.x == sign * 9e16 ); CHECK( actual.y == sign * 4 ); CHECK( actual.z == 28 );
        files_t saved; REQUIRE( saved.Save( &map ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
        CHECK( math::Vec3d_EqualsExact( PrefabFixtureVector( loaded, "scale" ), scale ) );
        CHECK( PrefabFixtureVector( loaded, "angles" ).y == yaw );
    }
}
