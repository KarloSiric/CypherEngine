//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Map mesh conversion, retained ancestry, winding and allocation contracts.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_MeshEdit.h"
#include "CypherMap_Edit.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherMath_UV.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <map>
#include <memory>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;
namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }
string_view_t SV( const std::string &s ) { return { s.data(), s.size() }; }
void Create( map_document_t &map, const allocator_t *allocator = Allocator_GetSystem() )
{
    REQUIRE( MapDocument_Create( &map, allocator, { SV( "Mesh edit fixture" ), SV( "reap" ), {} } ) == map_status_t::OK );
}
u64 Primitive( map_document_t &map, map_primitive_kind_t kind = map_primitive_kind_t::BOX )
{
    map_primitive_desc_t desc{}; desc.kind = kind;
    desc.bounds = { { -64, -48, 16 }, { 96, 80, 112 } };
    desc.nSides = 12;
    u64 id = 0;
    REQUIRE( MapEdit_CreatePrimitive( &map, desc, SV( "materials/source.cymat" ), {}, &id ) == map_status_t::OK );
    return id;
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
struct description_t {
    geo::mesh_source_description_t value{};
    ~description_t() { geo::MeshSourceDescription_Shutdown( &value ); }
    void Read( const map_document_t &map, u64 id )
    {
        REQUIRE( geo::MeshSourceDescription_Init( &value, map.pAllocator, { id } ) == geo::geometry_status_t::OK );
        const auto *source = geo::GeometryDocument_FindMesh( &map.geometry, { id } ); REQUIRE( source );
        REQUIRE( geo::MeshSource_TryDescribe( source, &value ) == geo::geometry_status_t::OK );
    }
};
const key_value_t *Face( const key_value_t *record, u64 id )
{
    const auto *faces = KeyValue_Find( record, SV( "faces" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( faces ); ++i ) {
        const auto *face = KeyValue_ChildAt( faces, i ); u64 found = 0;
        if ( KeyValue_GetU64( KeyValue_Find( face, SV( "id" ) ), &found ) && found == id ) { return face; }
    }
    return nullptr;
}
void SetU64( key_value_document_t *doc, key_value_t *parent, const char *name, u64 value )
{
    auto *node = KeyValue_ObjectInsert( doc, parent, SV( name ), key_value_type_t::U64 ); REQUIRE( node );
    REQUIRE( KeyValue_SetU64( doc, node, value ) );
}
void Metadata( map_document_t &map, u64 id )
{
    map_chunk_t *chunk = nullptr; auto *record = MapDocument_FindObject( &map, id, &chunk ); REQUIRE( record ); REQUIRE( chunk );
    auto *doc = chunk->store.pDocument;
    SetU64( doc, record, "future_root", 87654 );
    auto *name = KeyValue_ObjectInsert( doc, record, SV( "name" ), key_value_type_t::STRING ); REQUIRE( name );
    REQUIRE( KeyValue_SetString( doc, name, SV( "Authored conversion" ) ) );
    auto *faces = KeyValue_ObjectInsert( doc, record, SV( "faces" ), key_value_type_t::ARRAY ); REQUIRE( faces );
    const auto *brush = geo::GeometryDocument_FindBrush( &map.geometry, { id } ); REQUIRE( brush );
    auto *attributes = geo::GeometryDocument_FindBrushAttributesMutable( &map.geometry, { id } ); REQUIRE( attributes );
    for ( usize i = 0; i < brush->sides.nCount; ++i ) {
        auto *face = KeyValue_ArrayAppend( doc, faces, key_value_type_t::OBJECT ); REQUIRE( face );
        SetU64( doc, face, "id", brush->sides.pData[i].sourceId.value );
        SetU64( doc, face, "future_face", i + 10 );
        SetU64( doc, face, "smoothing", u64{ 1 } << i );
        auto *lightmap = KeyValue_ObjectInsert( doc, face, SV( "lightmap_scale" ), key_value_type_t::F64 ); REQUIRE( lightmap );
        REQUIRE( KeyValue_SetF64( doc, lightmap, 8.0 + static_cast<f64>( i ) ) );
        auto &surface = attributes->records.pData[brush->sides.pData[i].iAttributeIndex];
        surface.uvProjection.worldUnitsPerUv = { 73.0 + static_cast<f64>( i ), -91.0 - static_cast<f64>( i ) };
        surface.uvProjection.rotationRadians = 0.12 * static_cast<f64>( i );
        surface.uvProjection.offset = { 0.25 * static_cast<f64>( i ), -0.125 * static_cast<f64>( i ) };
    }
}
const map_geometry_record_t &Placement( const map_document_t &map, u64 id )
{
    for ( usize i = 0; i < map.geometryRecords.nCount; ++i ) { if ( map.geometryRecords.pData[i].id == id ) { return map.geometryRecords.pData[i]; } }
    FAIL( "Missing placement" ); return map.geometryRecords.pData[0];
}
void CheckMetadata( map_document_t &map, u64 id, u64 owner, u32 layer )
{
    CHECK( Placement( map, id ).owner == owner ); CHECK( Placement( map, id ).iLayer == layer );
    const auto *record = MapDocument_FindObject( &map, id, nullptr ); REQUIRE( record );
    u64 root = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( record, SV( "future_root" ) ), &root ) ); CHECK( root == 87654 );
    string_view_t name{}; REQUIRE( KeyValue_GetString( KeyValue_Find( record, SV( "name" ) ), &name ) ); CHECK( StringView_Equals( name, SV( "Authored conversion" ) ) );
    description_t desc; desc.Read( map, id );
    for ( usize i = 0; i < desc.value.faces.nCount; ++i ) {
        const auto &face = desc.value.faces.pData[i]; const auto *extra = Face( record, face.sourceId.value ); REQUIRE( extra );
        u64 tag = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( extra, SV( "future_face" ) ), &tag ) ); REQUIRE( tag >= 10 ); REQUIRE( tag < 16 );
        CHECK( face.attributes.smoothingGroups == u32{ 1 } << ( tag - 10 ) );
        f64 lightmap = 0; REQUIRE( KeyValue_GetF64( KeyValue_Find( extra, SV( "lightmap_scale" ) ), &lightmap ) ); CHECK( lightmap == Catch::Approx( 8.0 + static_cast<f64>( tag - 10 ) ) );
    }
}
math::vec3d_t Normal( const geo::mesh_source_description_t &desc, const geo::mesh_source_face_t &face )
{
    math::vec3d_t sum{};
    for ( u32 k = 0; k < face.cCorners; ++k ) {
        const auto &a = desc.vertices.pData[desc.corners.pData[face.iFirstCorner + k].iVertex].position;
        const auto &b = desc.vertices.pData[desc.corners.pData[face.iFirstCorner + ( k + 1 ) % face.cCorners].iVertex].position;
        sum.x += ( a.y - b.y ) * ( a.z + b.z ); sum.y += ( a.z - b.z ) * ( a.x + b.x ); sum.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return sum;
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
} // namespace

TEST_CASE( "Brush conversion uses the actual boundary and retains authored face identities and projected UVs", "[map][mesh-edit][convert]" )
{
    for ( auto kind : { map_primitive_kind_t::BOX, map_primitive_kind_t::WEDGE, map_primitive_kind_t::CYLINDER, map_primitive_kind_t::CONE, map_primitive_kind_t::SPHERE } ) {
        CAPTURE( static_cast<int>( kind ) );
        map_document_t map; Create( map ); const u64 root = Primitive( map, kind );
        const auto *brush = geo::GeometryDocument_FindBrush( &map.geometry, { root } ); REQUIRE( brush );
        const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &map.geometry, { root } ); REQUIRE( attributes );
        geo::brush_boundary_t boundary{}; REQUIRE( geo::BrushBoundary_Init( &boundary, map.pAllocator ) == geo::geometry_status_t::OK );
        REQUIRE( geo::BrushBoundary_TryReconstruct( &boundary, brush, map.geometryPolicy ) == geo::geometry_status_t::OK );
        std::map<u64, geo::geometry_brush_side_attributes_t> surfaces;
        for ( usize i = 0; i < brush->sides.nCount; ++i ) {
            geo::geometry_brush_side_attributes_t value{};
            REQUIRE( geo::BrushSideAttributeStore_TryGet( attributes, brush->sides.pData[i].iAttributeIndex, &value ) == geo::geometry_status_t::OK );
            surfaces[brush->sides.pData[i].sourceId.value] = value;
        }
        const auto next = map.nextId, revision = map.geometry.revision;
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &root, 1 }, &roots ) == map_status_t::OK );
        REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == root ); CHECK( map.geometry.brushes.nCount == 0 );
        CHECK( map.geometry.revision == revision + 1 ); CHECK( map.nextId == next + boundary.vertices.nCount );
        description_t desc; desc.Read( map, root );
        CHECK( desc.value.vertices.nCount == boundary.vertices.nCount ); CHECK( desc.value.faces.nCount == boundary.faces.nCount );
        for ( usize i = 0; i < desc.value.vertices.nCount; ++i ) {
            const auto &vertex = desc.value.vertices.pData[i]; CHECK( vertex.sourceId.value >= next );
            bool found = false;
            for ( usize k = 0; k < boundary.vertices.nCount; ++k ) {
                const auto &p = boundary.vertices.pData[k];
                found |= vertex.position.x == p.x && vertex.position.y == p.y && vertex.position.z == p.z;
            }
            CHECK( found );
        }
        for ( usize i = 0; i < desc.value.faces.nCount; ++i ) {
            const auto &face = desc.value.faces.pData[i]; REQUIRE( surfaces.contains( face.sourceId.value ) );
            const auto &surface = surfaces.at( face.sourceId.value ); CHECK( face.attributes.material.value == surface.material.value ); CHECK( face.attributes.smoothingGroups == 0 );
            for ( u32 k = 0; k < face.cCorners; ++k ) {
                const auto &corner = desc.value.corners.pData[face.iFirstCorner + k]; math::vec2d_t expected{};
                REQUIRE( math::Uvd_TryProjectPlanarPoint( surface.uvProjection, desc.value.vertices.pData[corner.iVertex].position,
                    map.geometryPolicy.numerical.fAbsoluteDistanceTolerance, &expected ) );
                CHECK( corner.attributes.uv0.x == Catch::Approx( expected.x ) ); CHECK( corner.attributes.uv0.y == Catch::Approx( expected.y ) );
            }
        }
        CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
        saved_t saved; REQUIRE( MapDocument_Save( &map, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
        description_t roundTrip; roundTrip.Read( loaded, root ); CHECK( geo::MeshSourceDescription_Equal( &desc.value, &roundTrip.value ) );
        geo::BrushBoundary_Shutdown( &boundary ); Vector_Shutdown( &roots );
    }
}

TEST_CASE( "Owned brush conversion preserves placement and unknown root and face records through save reload and winding edits", "[map][mesh-edit][convert][metadata]" )
{
    map_document_t map; Create( map ); const u64 root = Primitive( map ); u64 owner = 0;
    REQUIRE( MapDocument_AddLayer( &map, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( &map, root, SV( "detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( &map, SV( "detail" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &map, root, owner ) == map_status_t::OK );
    saved_t initial; REQUIRE( MapDocument_Save( &map, initial.Sink() ) == map_status_t::OK ); Metadata( map, root );
    const u32 layer = Placement( map, root ).iLayer;
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &root, 1 }, &roots ) == map_status_t::OK );
    CheckMetadata( map, root, owner, layer );
    REQUIRE( MapMeshEdit_FlipNormals( &map, { &root, 1 } ) == map_status_t::OK ); CheckMetadata( map, root, owner, layer );
    saved_t saved; REQUIRE( MapDocument_Save( &map, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
    CHECK( loaded.geometry.brushes.nCount == 0 ); CHECK( loaded.geometry.meshes.nCount == 1 ); CheckMetadata( loaded, root, owner, layer );
    description_t before, after; before.Read( map, root ); after.Read( loaded, root ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &loaded.geometry.sourceIds ) ); Vector_Shutdown( &roots );
}

TEST_CASE( "Whole mesh normal flipping reverses winding while preserving each authored corner and is its own inverse", "[map][mesh-edit][flip]" )
{
    map_document_t map; Create( map ); const u64 ids[]{ Primitive( map ), Primitive( map, map_primitive_kind_t::WEDGE ) };
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &map, { ids, 2 }, &roots ) == map_status_t::OK );
    description_t baseline; baseline.Read( map, ids[0] ); const auto next = map.nextId, revision = map.geometry.revision;
    REQUIRE( MapMeshEdit_FlipNormals( &map, { ids, 2 } ) == map_status_t::OK ); CHECK( map.geometry.revision == revision + 1 ); CHECK( map.nextId == next );
    description_t flipped; flipped.Read( map, ids[0] );
    REQUIRE( baseline.value.faces.nCount == flipped.value.faces.nCount ); REQUIRE( baseline.value.vertices.nCount == flipped.value.vertices.nCount );
    for ( usize i = 0; i < baseline.value.vertices.nCount; ++i ) {
        const auto &a = baseline.value.vertices.pData[i], &b = flipped.value.vertices.pData[i];
        CHECK( a.sourceId.value == b.sourceId.value ); CHECK( a.position.x == b.position.x ); CHECK( a.position.y == b.position.y ); CHECK( a.position.z == b.position.z );
    }
    for ( usize i = 0; i < baseline.value.faces.nCount; ++i ) {
        const auto &a = baseline.value.faces.pData[i], &b = flipped.value.faces.pData[i];
        CHECK( a.sourceId.value == b.sourceId.value ); CHECK( a.attributes.material.value == b.attributes.material.value ); CHECK( a.attributes.smoothingGroups == b.attributes.smoothingGroups );
        const auto na = Normal( baseline.value, a ), nb = Normal( flipped.value, b );
        CHECK( nb.x == Catch::Approx( -na.x ) ); CHECK( nb.y == Catch::Approx( -na.y ) ); CHECK( nb.z == Catch::Approx( -na.z ) );
        for ( u32 k = 0; k < a.cCorners; ++k ) {
            const auto &cornerA = baseline.value.corners.pData[a.iFirstCorner + k]; bool found = false;
            for ( u32 j = 0; j < b.cCorners; ++j ) {
                const auto &cornerB = flipped.value.corners.pData[b.iFirstCorner + j];
                if ( cornerA.iVertex != cornerB.iVertex ) { continue; }
                found = true; CHECK( cornerA.attributes.uv0.x == cornerB.attributes.uv0.x ); CHECK( cornerA.attributes.uv0.y == cornerB.attributes.uv0.y );
                CHECK( cornerA.attributes.uv1.x == cornerB.attributes.uv1.x ); CHECK( cornerA.attributes.uv1.y == cornerB.attributes.uv1.y ); CHECK( cornerA.attributes.colorRgba == cornerB.attributes.colorRgba );
            }
            CHECK( found );
        }
    }
    REQUIRE( MapMeshEdit_FlipNormals( &map, { ids, 2 } ) == map_status_t::OK ); description_t restored; restored.Read( map, ids[0] );
    CHECK( geo::MeshSourceDescription_Equal( &baseline.value, &restored.value ) ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
    Vector_Shutdown( &roots );
}

TEST_CASE( "Mesh adapters reject invalid mixed and conflicting records without changing external results", "[map][mesh-edit][validation]" )
{
    map_document_t map; Create( map ); const u64 a = Primitive( map ), b = Primitive( map );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 555 } ) );
    const auto revision = map.geometry.revision; const u64 duplicate[]{ a, a };
    CHECK( MapMeshEdit_ConvertBrushes( &map, { duplicate, 2 }, &roots ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapMeshEdit_ConvertBrushes( &map, {}, &roots ) == map_status_t::INVALID_ARGUMENT );
    map.bReadOnly = CY_TRUE; CHECK( MapMeshEdit_ConvertBrushes( &map, { &a, 1 }, &roots ) == map_status_t::READ_ONLY ); map.bReadOnly = CY_FALSE;
    CHECK( MapMeshEdit_FlipNormals( &map, { &a, 1 } ) == map_status_t::UNKNOWN_OBJECT );
    CHECK( MapMeshEdit_Triangulate( &map, { &a, 1 } ) == map_status_t::UNKNOWN_OBJECT );
    REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 555 ); CHECK( map.geometry.revision == revision );
    REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &a, 1 }, &roots ) == map_status_t::OK ); const u64 mixed[]{ a, b };
    CHECK( MapMeshEdit_ConvertBrushes( &map, { mixed, 2 }, &roots ) == map_status_t::UNKNOWN_OBJECT ); CHECK( MapMeshEdit_FlipNormals( &map, { mixed, 2 } ) == map_status_t::UNKNOWN_OBJECT );
    CHECK( MapMeshEdit_Triangulate( &map, { duplicate, 2 } ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapMeshEdit_Triangulate( &map, { mixed, 2 } ) == map_status_t::UNKNOWN_OBJECT );
    map.bReadOnly = CY_TRUE; CHECK( MapMeshEdit_FlipNormals( &map, { &a, 1 } ) == map_status_t::READ_ONLY );
    CHECK( MapMeshEdit_Triangulate( &map, { &a, 1 } ) == map_status_t::READ_ONLY ); map.bReadOnly = CY_FALSE;
    saved_t saved; REQUIRE( MapDocument_Save( &map, saved.Sink() ) == map_status_t::OK ); Metadata( map, b );
    map_chunk_t *chunk = nullptr; auto *record = MapDocument_FindObject( &map, b, &chunk ); REQUIRE( record ); REQUIRE( chunk );
    SetU64( chunk->store.pDocument, record, "vertex_ids", 999 );
    CHECK( MapMeshEdit_ConvertBrushes( &map, { &b, 1 }, &roots ) == map_status_t::INVALID_ARGUMENT ); REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == a );
    REQUIRE( KeyValue_Remove( chunk->store.pDocument, record, KeyValue_Find( record, SV( "vertex_ids" ) ) ) );
    const auto *brush = geo::GeometryDocument_FindBrush( &map.geometry, { b } ); REQUIRE( brush );
    auto *face = const_cast<key_value_t *>( Face( record, brush->sides.pData[0].sourceId.value ) ); REQUIRE( face ); SetU64( chunk->store.pDocument, face, "uv", 444 );
    CHECK( MapMeshEdit_ConvertBrushes( &map, { &b, 1 }, &roots ) == map_status_t::INVALID_ARGUMENT );
    CHECK( geo::GeometryDocument_FindBrush( &map.geometry, { b } ) ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) ); Vector_Shutdown( &roots );
}

TEST_CASE( "Every mesh adapter allocation failure releases private data and preserves source map and conversion output", "[map][mesh-edit][oom]" )
{
    for ( int operation : { 0, 1, 2 } ) {
        CAPTURE( operation ); audit_t audit;
        {
            map_document_t original; Create( original, &audit.allocator ); const u64 id = Primitive( original );
            saved_t initial; REQUIRE( MapDocument_Save( &original, initial.Sink() ) == map_status_t::OK ); Metadata( original, id );
            vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, &audit.allocator ) );
            if ( operation != 0 ) { REQUIRE( MapMeshEdit_ConvertBrushes( &original, { &id, 1 }, &roots ) == map_status_t::OK ); }
            Vector_Shutdown( &roots ); const usize baselineBytes = audit.bytes; const auto next = original.nextId, revision = original.geometry.revision;
            usize allocations = 0;
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                vector_t<u64> output{}; REQUIRE( Vector_Init( &output, &audit.allocator ) ); audit.calls = 0;
                const auto result = operation == 0 ? MapMeshEdit_ConvertBrushes( copy.get(), { &id, 1 }, &output ) :
                    operation == 1 ? MapMeshEdit_FlipNormals( copy.get(), { &id, 1 } ) : MapMeshEdit_Triangulate( copy.get(), { &id, 1 } );
                REQUIRE( result == map_status_t::OK );
                allocations = audit.calls; Vector_Shutdown( &output );
            }
            CHECK( audit.bytes == baselineBytes ); REQUIRE( allocations > 16 );
            for ( usize failure = 0; failure < allocations; ++failure ) {
                CAPTURE( failure, allocations );
                {
                    audit.failAt = CY_USIZE_MAX; map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                    vector_t<u64> output{}; REQUIRE( Vector_Init( &output, &audit.allocator ) ); REQUIRE( Vector_PushBack( &output, u64{ 999 } ) );
                    audit.calls = 0; audit.failAt = failure;
                    const auto result = operation == 0 ? MapMeshEdit_ConvertBrushes( copy.get(), { &id, 1 }, &output ) :
                        operation == 1 ? MapMeshEdit_FlipNormals( copy.get(), { &id, 1 } ) : MapMeshEdit_Triangulate( copy.get(), { &id, 1 } );
                    CHECK( result != map_status_t::OK );
                    REQUIRE( output.nCount == 1 ); CHECK( output.pData[0] == 999 ); audit.failAt = CY_USIZE_MAX; Vector_Shutdown( &output );
                }
                CHECK( audit.bytes == baselineBytes ); CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision );
                CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &original.geometry.sourceIds ) );
            }
        }
        CHECK( audit.bytes == 0 );
    }
}

TEST_CASE( "Mesh triangulation preserves exact parent face data and corner UVs with fresh triangle identities and a true no op", "[map][mesh-edit][triangulate]" )
{
    map_document_t map; Create( map ); const u64 root = Primitive( map ); u64 owner = 0;
    REQUIRE( MapDocument_AddLayer( &map, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( &map, root, SV( "detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( &map, SV( "detail" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &map, root, owner ) == map_status_t::OK );
    saved_t initial; REQUIRE( MapDocument_Save( &map, initial.Sink() ) == map_status_t::OK ); Metadata( map, root );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &root, 1 }, &roots ) == map_status_t::OK );
    const auto next = map.nextId, revision = map.geometry.revision; const auto layer = Placement( map, root ).iLayer;
    description_t before; before.Read( map, root );
    std::map<u64, u64> parentTags;
    const auto *record = MapDocument_FindObject( &map, root, nullptr ); REQUIRE( record );
    for ( usize i = 0; i < before.value.faces.nCount; ++i ) {
        const auto id = before.value.faces.pData[i].sourceId.value; u64 tag = 0;
        REQUIRE( KeyValue_GetU64( KeyValue_Find( Face( record, id ), SV( "future_face" ) ), &tag ) ); parentTags[tag] = id;
    }
    REQUIRE( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::OK );
    CHECK( map.geometry.revision == revision + 1 ); CHECK( map.nextId == next + 6 );
    description_t after; after.Read( map, root ); REQUIRE( after.value.faces.nCount == 12 ); REQUIRE( after.value.vertices.nCount == before.value.vertices.nCount );
    std::set<u64> fresh, retained;
    record = MapDocument_FindObject( &map, root, nullptr ); REQUIRE( record );
    for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
        const auto &face = after.value.faces.pData[i]; CHECK( face.cCorners == 3 );
        if ( face.sourceId.value >= next ) { fresh.insert( face.sourceId.value ); } else { retained.insert( face.sourceId.value ); }
        const auto *extra = Face( record, face.sourceId.value ); REQUIRE( extra ); u64 tag = 0;
        REQUIRE( KeyValue_GetU64( KeyValue_Find( extra, SV( "future_face" ) ), &tag ) ); REQUIRE( parentTags.contains( tag ) );
        const geo::mesh_source_face_t *parent = nullptr;
        for ( usize k = 0; k < before.value.faces.nCount; ++k ) { if ( before.value.faces.pData[k].sourceId.value == parentTags.at( tag ) ) { parent = &before.value.faces.pData[k]; break; } }
        REQUIRE( parent ); CHECK( face.attributes.material.value == parent->attributes.material.value ); CHECK( face.attributes.smoothingGroups == parent->attributes.smoothingGroups );
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const auto &corner = after.value.corners.pData[face.iFirstCorner + k]; const auto vertexId = after.value.vertices.pData[corner.iVertex].sourceId.value; bool found = false;
            for ( u32 j = 0; j < parent->cCorners; ++j ) {
                const auto &original = before.value.corners.pData[parent->iFirstCorner + j];
                if ( before.value.vertices.pData[original.iVertex].sourceId.value != vertexId ) { continue; }
                found = true; CHECK( corner.attributes.uv0.x == original.attributes.uv0.x ); CHECK( corner.attributes.uv0.y == original.attributes.uv0.y );
            }
            CHECK( found );
        }
    }
    CHECK( fresh.size() == 6 ); CHECK( retained.size() == 6 ); CheckMetadata( map, root, owner, layer );
    const auto committedRevision = map.geometry.revision, committedNext = map.nextId;
    REQUIRE( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::OK ); CHECK( map.geometry.revision == committedRevision ); CHECK( map.nextId == committedNext );
    description_t noOp; noOp.Read( map, root ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &noOp.value ) );
    saved_t saved; REQUIRE( MapDocument_Save( &map, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
    CheckMetadata( loaded, root, owner, layer ); description_t roundTrip; roundTrip.Read( loaded, root ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &roundTrip.value ) );
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &loaded.geometry.sourceIds ) ); Vector_Shutdown( &roots );
}

TEST_CASE( "Mesh edits preserve attributed open surfaces and enforce face and identity budgets", "[map][mesh-edit][open][limits]" )
{
    map_document_t map; Create( map );
    const u64 root = MapDocument_AllocateId( &map ); description_t authored;
    REQUIRE( geo::MeshSourceDescription_Init( &authored.value, map.pAllocator, { root } ) == geo::geometry_status_t::OK );
    const math::vec3d_t positions[]{ { 0, 0, 16 }, { 64, 0, 16 }, { 64, 48, 16 }, { 0, 48, 16 } };
    for ( const auto &position : positions ) {
        REQUIRE( geo::MeshSourceDescription_TryAddVertex( &authored.value, position, { MapDocument_AllocateId( &map ) }, nullptr ) == geo::geometry_status_t::OK );
    }
    const u64 faceId = MapDocument_AllocateId( &map ); const u32 indices[]{ 0, 1, 2, 3 };
    geo::mesh_face_attributes_t surface{}; surface.smoothingGroups = 42;
    REQUIRE( MapDocument_MaterialRef( &map, SV( "materials/open.cymat" ), &surface.material.value ) == map_status_t::OK );
    REQUIRE( geo::MeshSourceDescription_TryAddFace( &authored.value, { indices, 4 }, { faceId }, surface, nullptr ) == geo::geometry_status_t::OK );
    for ( usize i = 0; i < authored.value.corners.nCount; ++i ) {
        auto &corner = authored.value.corners.pData[i]; corner.attributes.uv0 = { static_cast<f64>( i ), 0.5 * static_cast<f64>( i ) };
        corner.attributes.uv1 = { 0.125 * static_cast<f64>( i ), -0.25 * static_cast<f64>( i ) }; corner.attributes.colorRgba = 0x112233FFu + static_cast<u32>( i ) * 0x11000000u;
    }
    geo::mesh_edge_attributes_t edge{}; edge.flags = geo::MESH_EDGE_FLAG_HARD | geo::MESH_EDGE_FLAG_SEAM;
    REQUIRE( geo::MeshSourceDescription_TrySetEdge( &authored.value, 0, 1, edge, 0.75 ) == geo::geometry_status_t::OK );
    geo::mesh_source_t source{}; REQUIRE( geo::MeshSource_TryBuild( &authored.value, map.pAllocator, &source ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddMesh( &map.geometry, &source ) == geo::geometry_status_t::OK ); geo::MeshSource_Shutdown( &source );
    REQUIRE( MapDocument_SetGeometryLayer( &map, root, SV( "default" ) ) == map_status_t::OK );
    description_t baseline; baseline.Read( map, root );
    REQUIRE( MapMeshEdit_FlipNormals( &map, { &root, 1 } ) == map_status_t::OK ); REQUIRE( MapMeshEdit_FlipNormals( &map, { &root, 1 } ) == map_status_t::OK );
    description_t restored; restored.Read( map, root ); CHECK( geo::MeshSourceDescription_Equal( &baseline.value, &restored.value ) );
    const auto revision = map.geometry.revision, next = map.nextId;
    map.geometry.policy.limits.cFacesMax = 1;
    CHECK( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::LIMIT_EXCEEDED ); CHECK( map.geometry.revision == revision ); CHECK( map.nextId == next );
    map.geometry.policy = map.geometryPolicy;
    REQUIRE( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::OK ); description_t triangles; triangles.Read( map, root ); REQUIRE( triangles.value.faces.nCount == 2 );
    for ( usize i = 0; i < triangles.value.faces.nCount; ++i ) {
        const auto &face = triangles.value.faces.pData[i]; CHECK( face.attributes.smoothingGroups == 42 ); CHECK( face.attributes.material.value == surface.material.value );
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const auto &corner = triangles.value.corners.pData[face.iFirstCorner + k]; const u64 vertex = triangles.value.vertices.pData[corner.iVertex].sourceId.value; bool found = false;
            for ( usize j = 0; j < baseline.value.corners.nCount; ++j ) {
                const auto &old = baseline.value.corners.pData[j]; if ( baseline.value.vertices.pData[old.iVertex].sourceId.value != vertex ) { continue; }
                found = true; CHECK( corner.attributes.uv0.x == old.attributes.uv0.x ); CHECK( corner.attributes.uv0.y == old.attributes.uv0.y );
                CHECK( corner.attributes.uv1.x == old.attributes.uv1.x ); CHECK( corner.attributes.uv1.y == old.attributes.uv1.y ); CHECK( corner.attributes.colorRgba == old.attributes.colorRgba );
            }
            CHECK( found );
        }
    }
    REQUIRE( triangles.value.edges.nCount == 1 ); CHECK( triangles.value.edges.pData[0].attributes.flags == edge.flags ); CHECK( triangles.value.edges.pData[0].creaseWeight == 0.75 );
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
    map_document_t exhausted; Create( exhausted ); const u64 brush = Primitive( exhausted ); vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, exhausted.pAllocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 333 } ) );
    exhausted.nextId = CY_U64_MAX; CHECK( MapMeshEdit_ConvertBrushes( &exhausted, { &brush, 1 }, &roots ) == map_status_t::LIMIT_EXCEEDED );
    REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == 333 ); CHECK( geo::GeometryDocument_FindBrush( &exhausted.geometry, { brush } ) ); Vector_Shutdown( &roots );
}

TEST_CASE( "Triangulation never reuses live or retired map IDs when allocation cursors differ", "[map][mesh-edit][triangulate][identity]" )
{
    map_document_t map; Create( map ); const u64 root = Primitive( map );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &root, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
    const auto meshNext = map.nextId;
    std::set<u64> issued;
    for ( u32 i = 0; i < 8; ++i ) {
        u64 entity = 0; REQUIRE( MapDocument_AddEntity( &map, SV( "default" ), SV( "info_target" ), {}, &entity ) == map_status_t::OK );
        issued.insert( entity );
        if ( i % 2 == 1 ) { REQUIRE( MapDocument_RemoveObject( &map, entity ) == map_status_t::OK ); }
    }
    const auto mapNext = map.nextId; REQUIRE( mapNext == meshNext + 8 );
    CHECK( map.geometry.sourceIds.allocator.next.value == mapNext );
    saved_t saved; REQUIRE( MapDocument_Save( &map, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
    REQUIRE( loaded.nextId == mapNext ); CHECK( loaded.geometry.sourceIds.allocator.next.value == mapNext );

    u64 expectedFirst = mapNext;
    SECTION( "map high-water mark is ahead of geometry" ) {
        // Model a retained map reservation arriving with an older, valid
        // geometry registry. Deleted map-only IDs have no registry claims.
        loaded.geometry.sourceIds.allocator.next = { meshNext };
    }
    SECTION( "geometry high-water mark is ahead of map" ) {
        expectedFirst = mapNext + 32;
        REQUIRE( geo::GeometrySourceIdAllocator_AdvancePast( &loaded.geometry.sourceIds.allocator, { expectedFirst - 1 } ) == geo::geometry_status_t::OK );
    }
    REQUIRE( geo::GeometrySourceIdRegistry_ValidateDeep( &loaded.geometry.sourceIds ) );
    description_t before; before.Read( loaded, root );
    std::set<u64> originalFaces;
    for ( usize i = 0; i < before.value.faces.nCount; ++i ) { originalFaces.insert( before.value.faces.pData[i].sourceId.value ); }
    const auto revision = loaded.geometry.revision;
    REQUIRE( MapMeshEdit_Triangulate( &loaded, { &root, 1 } ) == map_status_t::OK );
    CHECK( loaded.geometry.revision == revision + 1 ); CHECK( loaded.nextId == expectedFirst + 6 );
    CHECK( loaded.geometry.sourceIds.allocator.next.value == loaded.nextId );
    description_t after; after.Read( loaded, root ); usize fresh = 0;
    for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
        const auto id = after.value.faces.pData[i].sourceId.value;
        CHECK_FALSE( issued.contains( id ) );
        if ( !originalFaces.contains( id ) ) { CHECK( id >= expectedFirst ); ++fresh; }
    }
    CHECK( fresh == 6 ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &loaded.geometry.sourceIds ) );
    saved_t committed; REQUIRE( MapDocument_Save( &loaded, committed.Sink() ) == map_status_t::OK ); map_document_t roundTrip; Load( roundTrip, committed );
    CHECK( roundTrip.nextId == expectedFirst + 6 ); CHECK( roundTrip.geometry.sourceIds.allocator.next.value == roundTrip.nextId );
    description_t persisted; persisted.Read( roundTrip, root ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
}

TEST_CASE( "Triangulation rejects exhausted namespaces and leaves exhausted triangle no ops intact", "[map][mesh-edit][triangulate][identity][limits]" )
{
    map_document_t map; Create( map ); const u64 root = Primitive( map );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &root, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
    description_t before; before.Read( map, root ); const auto revision = map.geometry.revision;
    SECTION( "map cursor is exhausted" ) {
        const auto registryNext = map.geometry.sourceIds.allocator.next.value;
        map.nextId = 0;
        CHECK( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::LIMIT_EXCEEDED );
        CHECK( map.nextId == 0 ); CHECK( map.geometry.sourceIds.allocator.next.value == registryNext );
    }
    SECTION( "geometry cursor is exhausted" ) {
        const auto mapNext = map.nextId;
        map.geometry.sourceIds.allocator.next = {};
        CHECK( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::LIMIT_EXCEEDED );
        CHECK( map.nextId == mapNext ); CHECK( map.geometry.sourceIds.allocator.next.value == 0 );
    }
    SECTION( "remaining identity budget cannot fit all six child faces" ) {
        map.nextId = CY_U64_MAX - 3;
        CHECK( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::LIMIT_EXCEEDED );
        CHECK( map.nextId == CY_U64_MAX - 3 );
    }
    SECTION( "triangles at exhausted identity and revision limits remain a no op" ) {
        REQUIRE( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::OK );
        description_t triangles; triangles.Read( map, root );
        map.nextId = 0; map.geometry.sourceIds.allocator.next = {}; map.geometry.revision = CY_U64_MAX;
        REQUIRE( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::OK );
        CHECK( map.nextId == 0 ); CHECK( map.geometry.sourceIds.allocator.next.value == 0 ); CHECK( map.geometry.revision == CY_U64_MAX );
        description_t after; after.Read( map, root ); CHECK( geo::MeshSourceDescription_Equal( &triangles.value, &after.value ) );
        return;
    }
    CHECK( map.geometry.revision == revision );
    description_t after; after.Read( map, root ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
}

namespace
{
map_status_t EditFace( map_document_t *map, int operation, u64 root, u64 face, f64 amount )
{
    return operation == 0 ? MapMeshEdit_ExtrudeFace( map, root, face, amount ) : MapMeshEdit_InsetFace( map, root, face, amount );
}
u64 ConvertBox( map_document_t &map )
{
    const u64 root = Primitive( map ); vector_t<u64> roots{};
    REQUIRE( Vector_Init( &roots, map.pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &root, 1 }, &roots ) == map_status_t::OK );
    Vector_Shutdown( &roots ); return root;
}
const geo::mesh_source_face_t *DescribedFace( const geo::mesh_source_description_t &description, u64 id )
{
    for ( usize i = 0; i < description.faces.nCount; ++i ) {
        if ( description.faces.pData[i].sourceId.value == id ) { return &description.faces.pData[i]; }
    }
    return nullptr;
}
const geo::mesh_source_vertex_t *DescribedVertex( const geo::mesh_source_description_t &description, u64 id )
{
    for ( usize i = 0; i < description.vertices.nCount; ++i ) {
        if ( description.vertices.pData[i].sourceId.value == id ) { return &description.vertices.pData[i]; }
    }
    return nullptr;
}
math::vec3d_t UnitNormal( const geo::mesh_source_description_t &description, const geo::mesh_source_face_t &face )
{
    auto n = Normal( description, face ); const auto length = std::sqrt( n.x * n.x + n.y * n.y + n.z * n.z ); REQUIRE( length > 0 );
    return { n.x / length, n.y / length, n.z / length };
}
u64 TopFace( const geo::mesh_source_description_t &description )
{
    for ( usize i = 0; i < description.faces.nCount; ++i ) {
        if ( UnitNormal( description, description.faces.pData[i] ).z > 0.99 ) { return description.faces.pData[i].sourceId.value; }
    }
    FAIL( "Fixture has no top face" ); return 0;
}
void CheckPoint( math::vec3d_t actual, math::vec3d_t expected )
{
    CHECK( actual.x == Catch::Approx( expected.x ).margin( 1e-9 ) );
    CHECK( actual.y == Catch::Approx( expected.y ).margin( 1e-9 ) );
    CHECK( actual.z == Catch::Approx( expected.z ).margin( 1e-9 ) );
}
math::vec3d_t Centroid( const geo::mesh_source_description_t &description, const geo::mesh_source_face_t &face )
{
    math::vec3d_t point{};
    for ( u32 k = 0; k < face.cCorners; ++k ) {
        const auto p = description.vertices.pData[description.corners.pData[face.iFirstCorner + k].iVertex].position;
        point.x += p.x; point.y += p.y; point.z += p.z;
    }
    return { point.x / face.cCorners, point.y / face.cCorners, point.z / face.cCorners };
}
} // namespace

TEST_CASE( "Single face modeling retains the selected identity and correctly creates geometry on every signed box side", "[map][mesh-edit][face-modeling]" )
{
    for ( int operation : { 0, 1 } ) {
        for ( usize side = 0; side < 6; ++side ) {
            CAPTURE( operation, side ); map_document_t map; Create( map ); const u64 root = ConvertBox( map );
            description_t before; before.Read( map, root ); const auto &original = before.value.faces.pData[side];
            const u64 faceId = original.sourceId.value, next = map.nextId, revision = map.geometry.revision;
            const f64 amount = operation == 0 ? 24.0 : 8.0;
            REQUIRE( EditFace( &map, operation, root, faceId, amount ) == map_status_t::OK );
            description_t after; after.Read( map, root ); const auto *edited = DescribedFace( after.value, faceId ); REQUIRE( edited );
            REQUIRE( edited->cCorners == original.cCorners ); CHECK( after.value.vertices.nCount == before.value.vertices.nCount + 4 );
            CHECK( after.value.faces.nCount == before.value.faces.nCount + 4 ); CHECK( map.nextId == next + 8 );
            CHECK( map.geometry.sourceIds.allocator.next.value == map.nextId ); CHECK( map.geometry.revision == revision + 1 );
            const auto normal = UnitNormal( before.value, original ); CheckPoint( UnitNormal( after.value, *edited ), normal );
            const auto center = Centroid( before.value, original );
            for ( u32 k = 0; k < original.cCorners; ++k ) {
                const auto p = before.value.vertices.pData[before.value.corners.pData[original.iFirstCorner + k].iVertex].position;
                auto expected = math::vec3d_t{ p.x + normal.x * amount, p.y + normal.y * amount, p.z + normal.z * amount };
                if ( operation == 1 ) {
                    const auto dx = center.x - p.x, dy = center.y - p.y, dz = center.z - p.z;
                    const auto distance = std::sqrt( dx * dx + dy * dy + dz * dz );
                    // The source operation's amount is radial travel toward
                    // the centroid, not a perpendicular border width.
                    expected = { p.x + amount * dx / distance, p.y + amount * dy / distance, p.z + amount * dz / distance };
                }
                bool found = false;
                for ( u32 j = 0; j < edited->cCorners; ++j ) {
                    const auto &vertex = after.value.vertices.pData[after.value.corners.pData[edited->iFirstCorner + j].iVertex];
                    if ( std::fabs( vertex.position.x - expected.x ) < 1e-9 && std::fabs( vertex.position.y - expected.y ) < 1e-9 &&
                         std::fabs( vertex.position.z - expected.z ) < 1e-9 ) { CHECK( vertex.sourceId.value >= next ); found = true; }
                }
                CHECK( found );
            }
            std::set<u64> fresh;
            for ( usize i = 0; i < after.value.vertices.nCount; ++i ) {
                const auto &vertex = after.value.vertices.pData[i]; const auto *old = DescribedVertex( before.value, vertex.sourceId.value );
                if ( old ) { CheckPoint( vertex.position, old->position ); } else { CHECK( vertex.sourceId.value >= next ); CHECK( fresh.insert( vertex.sourceId.value ).second ); }
            }
            for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
                const auto id = after.value.faces.pData[i].sourceId.value;
                if ( !DescribedFace( before.value, id ) ) { CHECK( id >= next ); CHECK( fresh.insert( id ).second ); }
            }
            CHECK( fresh.size() == 8 );
            for ( usize i = 0; i < before.value.faces.nCount; ++i ) { CHECK( DescribedFace( after.value, before.value.faces.pData[i].sourceId.value ) ); }
            CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
        }
    }
}

TEST_CASE( "Face modeling follows exact surface ancestry for materials UV sets colors and retained nested records across reload", "[map][mesh-edit][face-modeling][metadata]" )
{
    for ( int operation : { 0, 1 } ) {
        CAPTURE( operation ); map_document_t map; Create( map ); const u64 root = Primitive( map ); u64 owner = 0;
        REQUIRE( MapDocument_AddLayer( &map, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryLayer( &map, root, SV( "detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_AddEntity( &map, SV( "detail" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( &map, root, owner ) == map_status_t::OK );
        saved_t initial; REQUIRE( MapDocument_Save( &map, initial.Sink() ) == map_status_t::OK ); Metadata( map, root );
        std::map<u64, geo::geometry_brush_side_attributes_t> surfaces;
        std::map<u64, u64> parentTags;
        const auto *brush = geo::GeometryDocument_FindBrush( &map.geometry, { root } ); REQUIRE( brush );
        auto *attributes = geo::GeometryDocument_FindBrushAttributesMutable( &map.geometry, { root } ); REQUIRE( attributes );
        map_chunk_t *chunk = nullptr; auto *record = MapDocument_FindObject( &map, root, &chunk ); REQUIRE( record ); REQUIRE( chunk );
        for ( usize i = 0; i < brush->sides.nCount; ++i ) {
            const auto id = brush->sides.pData[i].sourceId.value; auto &surface = attributes->records.pData[brush->sides.pData[i].iAttributeIndex];
            const std::string path = "materials/face_" + std::to_string( i ) + ".cymat";
            REQUIRE( MapDocument_MaterialRef( &map, SV( path ), &surface.material.value ) == map_status_t::OK ); surfaces[id] = surface; parentTags[id] = i + 10;
            auto *face = const_cast<key_value_t *>( Face( record, id ) ); REQUIRE( face );
            auto *payload = KeyValue_ObjectInsert( chunk->store.pDocument, face, SV( "future_payload" ), key_value_type_t::OBJECT ); REQUIRE( payload );
            SetU64( chunk->store.pDocument, payload, "author", 100 + i );
            auto *note = KeyValue_ObjectInsert( chunk->store.pDocument, payload, SV( "note" ), key_value_type_t::STRING ); REQUIRE( note );
            REQUIRE( KeyValue_SetString( chunk->store.pDocument, note, SV( path ) ) );
        }
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &root, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
        description_t authored; authored.Read( map, root );
        for ( usize i = 0; i < authored.value.faces.nCount; ++i ) {
            const auto &face = authored.value.faces.pData[i];
            for ( u32 k = 0; k < face.cCorners; ++k ) {
                auto &corner = authored.value.corners.pData[face.iFirstCorner + k];
                corner.attributes.uv1 = { 2.0 * corner.attributes.uv0.x + 0.125, -3.0 * corner.attributes.uv0.y - 0.5 };
                corner.attributes.colorRgba = 0x223344FFu + static_cast<u32>( i ) * 0x11000000u;
            }
        }
        geo::mesh_source_t replacement{};
        REQUIRE( geo::MeshSource_TryBuild( &authored.value, map.pAllocator, &replacement ) == geo::geometry_status_t::OK );
        REQUIRE( geo::GeometryDocument_TryReplaceMesh( &map.geometry, &replacement ) == geo::geometry_status_t::OK ); geo::MeshSource_Shutdown( &replacement );
        const u64 selected = TopFace( authored.value ); const auto layer = Placement( map, root ).iLayer;
        REQUIRE( EditFace( &map, operation, root, selected, operation == 0 ? 24.0 : 8.0 ) == map_status_t::OK );
        description_t after; after.Read( map, root ); CheckMetadata( map, root, owner, layer );
        saved_t committed; REQUIRE( MapDocument_Save( &map, committed.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, committed );
        CheckMetadata( loaded, root, owner, layer ); description_t persisted; persisted.Read( loaded, root );
        REQUIRE( after.value.faces.nCount == persisted.value.faces.nCount );
        for ( usize i = 0; i < persisted.value.faces.nCount; ++i ) {
            auto &face = persisted.value.faces.pData[i]; const auto &expected = after.value.faces.pData[i];
            REQUIRE( face.sourceId.value == expected.sourceId.value );
            // Material refs index this document's intern table. Save writes
            // paths; load may assign different refs when unused entries are
            // absent or face encounter order differs. Compare the paths and
            // normalize only the test description's local ref before exact
            // comparison of every identity, position, UV, color and edge.
            const auto path = MapMaterials_Path( &loaded.materials, face.attributes.material.value );
            const auto expectedPath = MapMaterials_Path( &map.materials, expected.attributes.material.value );
            CHECK( StringView_Equals( path, expectedPath ) );
            face.attributes.material = expected.attributes.material;
        }
        CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) ); CHECK( loaded.nextId == map.nextId );
        for ( map_document_t *document : { &map, &loaded } ) {
            record = MapDocument_FindObject( document, root, nullptr ); REQUIRE( record );
            for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
                const auto &face = after.value.faces.pData[i]; const auto *parent = DescribedFace( authored.value, face.sourceId.value );
                if ( !parent ) {
                    const auto n = UnitNormal( after.value, face );
                    for ( usize k = 0; k < authored.value.faces.nCount; ++k ) {
                        const auto &candidate = authored.value.faces.pData[k]; const auto oldNormal = UnitNormal( authored.value, candidate );
                        if ( n.x * oldNormal.x + n.y * oldNormal.y + n.z * oldNormal.z > 0.99 ) { parent = &candidate; break; }
                    }
                }
                REQUIRE( parent ); CAPTURE( face.sourceId.value, parent->sourceId.value );
                CHECK( face.attributes.material.value == parent->attributes.material.value ); CHECK( face.attributes.smoothingGroups == parent->attributes.smoothingGroups );
                const auto *childExtra = Face( record, face.sourceId.value ); REQUIRE( childExtra );
                const auto *payload = KeyValue_Find( childExtra, SV( "future_payload" ) ); REQUIRE( payload );
                u64 tag = 0, author = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( childExtra, SV( "future_face" ) ), &tag ) );
                CHECK( tag == parentTags.at( parent->sourceId.value ) );
                REQUIRE( KeyValue_GetU64( KeyValue_Find( payload, SV( "author" ) ), &author ) ); CHECK( author == tag + 90 );
                string_view_t note{}; REQUIRE( KeyValue_GetString( KeyValue_Find( payload, SV( "note" ) ), &note ) );
                const auto expectedPath = "materials/face_" + std::to_string( tag - 10 ) + ".cymat"; CHECK( StringView_Equals( note, SV( expectedPath ) ) );
                CHECK( face.attributes.material.value == surfaces.at( parent->sourceId.value ).material.value );
                for ( u32 k = 0; k < face.cCorners; ++k ) {
                    const auto &corner = after.value.corners.pData[face.iFirstCorner + k]; math::vec2d_t expected{};
                    REQUIRE( math::Uvd_TryProjectPlanarPoint( surfaces.at( parent->sourceId.value ).uvProjection,
                        after.value.vertices.pData[corner.iVertex].position, map.geometryPolicy.numerical.fAbsoluteDistanceTolerance, &expected ) );
                    CHECK( corner.attributes.uv0.x == Catch::Approx( expected.x ).margin( 1e-9 ) ); CHECK( corner.attributes.uv0.y == Catch::Approx( expected.y ).margin( 1e-9 ) );
                    CHECK( corner.attributes.uv1.x == Catch::Approx( 2.0 * expected.x + 0.125 ).margin( 1e-9 ) );
                    CHECK( corner.attributes.uv1.y == Catch::Approx( -3.0 * expected.y - 0.5 ).margin( 1e-9 ) );
                    CHECK( corner.attributes.colorRgba == authored.value.corners.pData[parent->iFirstCorner].attributes.colorRgba );
                }
            }
            CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &document->geometry.sourceIds ) );
        }
    }
}

TEST_CASE( "Face modeling rejects invalid amounts read only maps and missing components without allocating or advancing identities", "[map][mesh-edit][face-modeling][validation]" )
{
    for ( int operation : { 0, 1 } ) {
        audit_t audit; CAPTURE( operation );
        {
            map_document_t map; Create( map, &audit.allocator ); const u64 root = ConvertBox( map ); const u64 brush = Primitive( map );
            description_t before; before.Read( map, root ); const u64 face = TopFace( before.value );
            const auto revision = map.geometry.revision, mapNext = map.nextId, geometryNext = map.geometry.sourceIds.allocator.next.value;
            for ( f64 amount : { 0.0, -1.0, std::numeric_limits<f64>::quiet_NaN(), std::numeric_limits<f64>::infinity() } ) {
                CAPTURE( amount ); audit.calls = 0;
                CHECK( EditFace( &map, operation, root, face, amount ) == map_status_t::INVALID_ARGUMENT ); CHECK( audit.calls == 0 );
            }
            CHECK( EditFace( nullptr, operation, root, face, 8.0 ) == map_status_t::INVALID_ARGUMENT );
            CHECK( EditFace( &map, operation, root, 0, 8.0 ) == map_status_t::INVALID_ARGUMENT );
            CHECK( EditFace( &map, operation, root, CY_U64_MAX, 8.0 ) == map_status_t::UNKNOWN_OBJECT );
            CHECK( EditFace( &map, operation, CY_U64_MAX, face, 8.0 ) == map_status_t::UNKNOWN_OBJECT );
            CHECK( EditFace( &map, operation, brush, face, 8.0 ) == map_status_t::UNKNOWN_OBJECT );
            map.bReadOnly = CY_TRUE; CHECK( EditFace( &map, operation, root, face, 8.0 ) == map_status_t::READ_ONLY ); map.bReadOnly = CY_FALSE;
            CHECK( map.geometry.revision == revision ); CHECK( map.nextId == mapNext ); CHECK( map.geometry.sourceIds.allocator.next.value == geometryNext );
            description_t after; after.Read( map, root ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
        }
        CHECK( audit.bytes == 0 );
    }
}

TEST_CASE( "Face modeling respects both exhausted identity cursors revision limits and document geometry budgets", "[map][mesh-edit][face-modeling][identity][limits]" )
{
    for ( int operation : { 0, 1 } ) {
        for ( int limit = 0; limit < 7; ++limit ) {
            CAPTURE( operation, limit ); map_document_t map; Create( map ); const u64 root = ConvertBox( map );
            description_t before; before.Read( map, root ); const u64 face = TopFace( before.value );
            if ( limit == 0 ) { map.nextId = 0; }
            if ( limit == 1 ) { map.geometry.sourceIds.allocator.next = {}; }
            if ( limit == 2 ) { map.nextId = CY_U64_MAX - 3; }
            if ( limit == 3 ) { map.geometry.sourceIds.allocator.next = { CY_U64_MAX - 3 }; }
            if ( limit == 4 ) { map.geometry.revision = CY_U64_MAX; }
            if ( limit == 5 ) { map.geometry.policy.limits.cFacesMax = 6; }
            if ( limit == 6 ) { map.geometry.policy.limits.cVerticesMax = 8; }
            const auto revision = map.geometry.revision, mapNext = map.nextId, geometryNext = map.geometry.sourceIds.allocator.next.value;
            CHECK( EditFace( &map, operation, root, face, 8.0 ) == map_status_t::LIMIT_EXCEEDED );
            CHECK( map.geometry.revision == revision ); CHECK( map.nextId == mapNext ); CHECK( map.geometry.sourceIds.allocator.next.value == geometryNext );
            description_t after; after.Read( map, root ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
            CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
        }
    }
}

TEST_CASE( "New face and vertex IDs never reuse map reservations when source allocation cursors differ", "[map][mesh-edit][face-modeling][identity]" )
{
    for ( int operation : { 0, 1 } ) {
        for ( int ahead : { 0, 1 } ) {
            CAPTURE( operation, ahead ); map_document_t map; Create( map ); const u64 root = ConvertBox( map ); const u64 meshNext = map.nextId;
            std::set<u64> issued;
            for ( u32 i = 0; i < 8; ++i ) {
                u64 id = 0; REQUIRE( MapDocument_AddEntity( &map, SV( "default" ), SV( "info_target" ), {}, &id ) == map_status_t::OK ); issued.insert( id );
                if ( i % 2 == 1 ) { REQUIRE( MapDocument_RemoveObject( &map, id ) == map_status_t::OK ); }
            }
            u64 first = map.nextId;
            if ( ahead == 0 ) { map.geometry.sourceIds.allocator.next = { meshNext }; }
            else { first += 32; REQUIRE( geo::GeometrySourceIdAllocator_AdvancePast( &map.geometry.sourceIds.allocator, { first - 1 } ) == geo::geometry_status_t::OK ); }
            description_t before; before.Read( map, root ); const u64 face = TopFace( before.value );
            REQUIRE( EditFace( &map, operation, root, face, 8.0 ) == map_status_t::OK );
            description_t after; after.Read( map, root ); usize fresh = 0;
            for ( usize i = 0; i < after.value.vertices.nCount; ++i ) {
                const auto id = after.value.vertices.pData[i].sourceId.value; CHECK_FALSE( issued.contains( id ) );
                if ( !DescribedVertex( before.value, id ) ) { CHECK( id >= first ); ++fresh; }
            }
            for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
                const auto id = after.value.faces.pData[i].sourceId.value; CHECK_FALSE( issued.contains( id ) );
                if ( !DescribedFace( before.value, id ) ) { CHECK( id >= first ); ++fresh; }
            }
            CHECK( fresh == 8 ); CHECK( map.nextId == first + 8 ); CHECK( map.geometry.sourceIds.allocator.next.value == map.nextId );
            CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
            saved_t saved; REQUIRE( MapDocument_Save( &map, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
            CHECK( loaded.nextId == first + 8 ); CHECK( loaded.geometry.sourceIds.allocator.next.value == loaded.nextId );
            description_t persisted; persisted.Read( loaded, root ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
        }
    }
}

TEST_CASE( "Every face modeling allocation failure releases the discarded private copy and a fresh retry preserves the source", "[map][mesh-edit][face-modeling][oom]" )
{
    for ( int operation : { 0, 1 } ) {
        CAPTURE( operation ); audit_t audit;
        {
            map_document_t original; Create( original, &audit.allocator ); const u64 root = Primitive( original );
            saved_t initial; REQUIRE( MapDocument_Save( &original, initial.Sink() ) == map_status_t::OK ); Metadata( original, root );
            vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, &audit.allocator ) );
            REQUIRE( MapMeshEdit_ConvertBrushes( &original, { &root, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
            description_t before; before.Read( original, root ); const u64 face = TopFace( before.value );
            const auto next = original.nextId, registryNext = original.geometry.sourceIds.allocator.next.value, revision = original.geometry.revision;
            const usize baselineBytes = audit.bytes; usize allocations = 0;
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                audit.calls = 0; REQUIRE( EditFace( copy.get(), operation, root, face, 8.0 ) == map_status_t::OK ); allocations = audit.calls;
            }
            REQUIRE( allocations > 16 ); CHECK( audit.bytes == baselineBytes );
            for ( usize failure = 0; failure < allocations; ++failure ) {
                CAPTURE( failure, allocations );
                {
                    map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                    audit.calls = 0; audit.failAt = failure;
                    CHECK( EditFace( copy.get(), operation, root, face, 8.0 ) == map_status_t::OUT_OF_MEMORY );
                    audit.failAt = CY_USIZE_MAX;
                    // Failure may occur after private geometry publication;
                    // the public contract requires discarding this copy.
                }
                CHECK( audit.bytes == baselineBytes ); CHECK( original.nextId == next ); CHECK( original.geometry.sourceIds.allocator.next.value == registryNext );
                CHECK( original.geometry.revision == revision ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &original.geometry.sourceIds ) );
            }
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> retry( raw );
                REQUIRE( EditFace( retry.get(), operation, root, face, 8.0 ) == map_status_t::OK );
                description_t result; result.Read( *retry, root ); CHECK( result.value.faces.nCount == 10 ); CHECK( result.value.vertices.nCount == 12 );
                CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &retry->geometry.sourceIds ) );
            }
            CHECK( audit.bytes == baselineBytes ); description_t unchanged; unchanged.Read( original, root );
            CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) );
        }
        CHECK( audit.bytes == 0 );
    }
}

TEST_CASE( "Quad slice retains authored shells face ancestry and both UV channels on every signed box side", "[map][mesh-edit][quad-slice]" )
{
    for ( usize side = 0; side < 6; ++side ) {
        CAPTURE( side ); map_document_t map; Create( map ); const u64 root = Primitive( map ); u64 owner = 0;
        REQUIRE( MapDocument_AddLayer( &map, SV( "detail" ), SV( "Detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryLayer( &map, root, SV( "detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_AddEntity( &map, SV( "detail" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( &map, root, owner ) == map_status_t::OK );
        saved_t initial; REQUIRE( MapDocument_Save( &map, initial.Sink() ) == map_status_t::OK ); Metadata( map, root );
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, map.pAllocator ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &root, 1 }, &roots ) == map_status_t::OK );
        description_t before; before.Read( map, root );
        for ( usize i = 0; i < before.value.faces.nCount; ++i ) {
            const auto &face = before.value.faces.pData[i];
            for ( u32 k = 0; k < face.cCorners; ++k ) {
                auto &corner = before.value.corners.pData[face.iFirstCorner + k]; const auto p = before.value.vertices.pData[corner.iVertex].position;
                corner.attributes.uv0 = { p.x + 2 * p.z, p.y - p.z };
                corner.attributes.uv1 = { 0.5 * p.x - p.y, 0.25 * p.z + p.y };
                corner.attributes.colorRgba = 0x223344FFu + static_cast<u32>( i ) * 0x11000000u;
            }
        }
        geo::mesh_source_t replacement{};
        REQUIRE( geo::MeshSource_TryBuild( &before.value, map.pAllocator, &replacement ) == geo::geometry_status_t::OK );
        REQUIRE( geo::GeometryDocument_TryReplaceMesh( &map.geometry, &replacement ) == geo::geometry_status_t::OK ); geo::MeshSource_Shutdown( &replacement );
        const auto selected = before.value.faces.pData[side].sourceId.value;
        const auto next = map.nextId, revision = map.geometry.revision; const auto layer = Placement( map, root ).iLayer;
        REQUIRE( MapMeshEdit_QuadSliceFace( &map, root, selected, 3, 2 ) == map_status_t::OK );
        description_t after; after.Read( map, root );
        CHECK( after.value.vertices.nCount == 16 ); CHECK( after.value.faces.nCount == 11 );
        CHECK( map.nextId == next + 13 ); CHECK( map.geometry.sourceIds.allocator.next.value == map.nextId ); CHECK( map.geometry.revision == revision + 1 );
        const auto *source = geo::GeometryDocument_FindMesh( &map.geometry, { root } ); REQUIRE( source );
        CHECK( geo::MeshBoundary_CountBoundaryEdges( &source->mesh ) == 0 ); CHECK( geo::EditableMesh_ShellCount( &source->mesh ) == 1 );
        usize freshVertices = 0, freshFaces = 0, neighbors = 0;
        for ( usize i = 0; i < after.value.vertices.nCount; ++i ) {
            const auto &vertex = after.value.vertices.pData[i];
            if ( const auto *old = DescribedVertex( before.value, vertex.sourceId.value ) ) { CheckPoint( vertex.position, old->position ); }
            else { CHECK( vertex.sourceId.value >= next ); ++freshVertices; }
        }
        for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
            const auto &face = after.value.faces.pData[i]; const auto *parent = DescribedFace( before.value, face.sourceId.value );
            if ( !parent ) { parent = DescribedFace( before.value, selected ); CHECK( face.sourceId.value >= next ); ++freshFaces; }
            REQUIRE( parent ); CHECK( face.attributes.material.value == parent->attributes.material.value ); CHECK( face.attributes.smoothingGroups == parent->attributes.smoothingGroups );
            CheckPoint( UnitNormal( after.value, face ), UnitNormal( before.value, *parent ) );
            if ( parent->sourceId.value == selected ) { CHECK( face.cCorners == 4 ); }
            else if ( face.cCorners > 4 ) { ++neighbors; }
            for ( u32 k = 0; k < face.cCorners; ++k ) {
                const auto &corner = after.value.corners.pData[face.iFirstCorner + k]; const auto p = after.value.vertices.pData[corner.iVertex].position;
                CHECK( corner.attributes.uv0.x == Catch::Approx( p.x + 2 * p.z ).margin( 1e-9 ) );
                CHECK( corner.attributes.uv0.y == Catch::Approx( p.y - p.z ).margin( 1e-9 ) );
                CHECK( corner.attributes.uv1.x == Catch::Approx( 0.5 * p.x - p.y ).margin( 1e-9 ) );
                CHECK( corner.attributes.uv1.y == Catch::Approx( 0.25 * p.z + p.y ).margin( 1e-9 ) );
                CHECK( corner.attributes.colorRgba == before.value.corners.pData[parent->iFirstCorner].attributes.colorRgba );
            }
        }
        CHECK( freshVertices == 8 ); CHECK( freshFaces == 5 ); CHECK( neighbors == 4 ); CheckMetadata( map, root, owner, layer );
        CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
        saved_t committed; REQUIRE( MapDocument_Save( &map, committed.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, committed );
        CheckMetadata( loaded, root, owner, layer ); description_t persisted; persisted.Read( loaded, root );
        CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) ); CHECK( loaded.nextId == map.nextId );
    }
}

TEST_CASE( "Quad slice authors open sheets and honors maximum cell counts with checked map identity high water marks", "[map][mesh-edit][quad-slice][identity]" )
{
    for ( bool geometryAhead : { false, true } ) {
        CAPTURE( geometryAhead ); map_document_t map; Create( map ); const u64 root = Primitive( map, map_primitive_kind_t::QUAD );
        const auto geometryNext = map.geometry.sourceIds.allocator.next.value; std::set<u64> issued;
        for ( u32 i = 0; i < 4; ++i ) {
            u64 id = 0; REQUIRE( MapDocument_AddEntity( &map, SV( "default" ), SV( "info_target" ), {}, &id ) == map_status_t::OK ); issued.insert( id );
            if ( i % 2 == 1 ) { REQUIRE( MapDocument_RemoveObject( &map, id ) == map_status_t::OK ); }
        }
        u64 first = map.nextId;
        if ( geometryAhead ) { first += 32; REQUIRE( geo::GeometrySourceIdAllocator_AdvancePast( &map.geometry.sourceIds.allocator, { first - 1 } ) == geo::geometry_status_t::OK ); }
        else { map.geometry.sourceIds.allocator.next = { geometryNext }; }
        description_t before; before.Read( map, root ); REQUIRE( before.value.faces.nCount == 1 ); const auto face = before.value.faces.pData[0].sourceId.value;
        REQUIRE( MapMeshEdit_QuadSliceFace( &map, root, face, 64, 1 ) == map_status_t::OK );
        description_t after; after.Read( map, root ); CHECK( after.value.faces.nCount == 64 ); CHECK( after.value.vertices.nCount == 130 );
        usize fresh = 0;
        for ( usize i = 0; i < after.value.vertices.nCount; ++i ) {
            const auto id = after.value.vertices.pData[i].sourceId.value; CHECK_FALSE( issued.contains( id ) );
            if ( !DescribedVertex( before.value, id ) ) { CHECK( id >= first ); ++fresh; }
        }
        for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
            const auto id = after.value.faces.pData[i].sourceId.value; CHECK_FALSE( issued.contains( id ) );
            if ( !DescribedFace( before.value, id ) ) { CHECK( id >= first ); ++fresh; }
        }
        CHECK( fresh == 189 ); CHECK( map.nextId == first + fresh ); CHECK( map.geometry.sourceIds.allocator.next.value == map.nextId );
        const auto *source = geo::GeometryDocument_FindMesh( &map.geometry, { root } ); REQUIRE( source );
        CHECK( geo::EditableMesh_ShellCount( &source->mesh ) == 1 ); CHECK( geo::MeshBoundary_CountBoundaryEdges( &source->mesh ) == 130 );
        CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &map.geometry.sourceIds ) );
        saved_t saved; REQUIRE( MapDocument_Save( &map, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
        description_t persisted; persisted.Read( loaded, root ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) ); CHECK( loaded.nextId == map.nextId );
    }
}

TEST_CASE( "Quad slice validates cell counts face shape limits and allocation free no ops before reserving identities", "[map][mesh-edit][quad-slice][validation]" )
{
    audit_t audit;
    {
        map_document_t map; Create( map, &audit.allocator ); const u64 root = ConvertBox( map ); const u64 brush = Primitive( map );
        description_t before; before.Read( map, root ); const auto face = TopFace( before.value );
        const auto next = map.nextId, revision = map.geometry.revision, registryNext = map.geometry.sourceIds.allocator.next.value;
        for ( const auto cells : { std::pair<u32, u32>{ 0, 1 }, { 1, 0 }, { 65, 1 }, { 1, 65 }, { CY_U32_MAX, CY_U32_MAX } } ) {
            CAPTURE( cells.first, cells.second ); audit.calls = 0;
            CHECK( MapMeshEdit_QuadSliceFace( &map, root, face, cells.first, cells.second ) == map_status_t::INVALID_ARGUMENT ); CHECK( audit.calls == 0 );
        }
        CHECK( MapMeshEdit_QuadSliceFace( nullptr, root, face, 2, 2 ) == map_status_t::INVALID_ARGUMENT );
        CHECK( MapMeshEdit_QuadSliceFace( &map, root, 0, 2, 2 ) == map_status_t::INVALID_ARGUMENT );
        CHECK( MapMeshEdit_QuadSliceFace( &map, root, CY_U64_MAX, 2, 2 ) == map_status_t::UNKNOWN_OBJECT );
        CHECK( MapMeshEdit_QuadSliceFace( &map, brush, face, 2, 2 ) == map_status_t::UNKNOWN_OBJECT );
        map.bReadOnly = CY_TRUE; CHECK( MapMeshEdit_QuadSliceFace( &map, root, face, 2, 2 ) == map_status_t::READ_ONLY ); map.bReadOnly = CY_FALSE;
        audit.calls = 0; CHECK( MapMeshEdit_QuadSliceFace( &map, root, face, 1, 1 ) == map_status_t::OK ); CHECK( audit.calls == 0 );
        CHECK( map.nextId == next ); CHECK( map.geometry.revision == revision ); CHECK( map.geometry.sourceIds.allocator.next.value == registryNext );
        for ( int limit = 0; limit < 7; ++limit ) {
            CAPTURE( limit ); map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &map, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
            if ( limit == 0 ) { copy->nextId = 0; }
            if ( limit == 1 ) { copy->geometry.sourceIds.allocator.next = {}; }
            if ( limit == 2 ) { copy->nextId = CY_U64_MAX - 3; }
            if ( limit == 3 ) { copy->geometry.sourceIds.allocator.next = { CY_U64_MAX - 3 }; }
            if ( limit == 4 ) { copy->geometry.revision = CY_U64_MAX; }
            if ( limit == 5 ) { copy->geometry.policy.limits.cFacesMax = 6; }
            if ( limit == 6 ) { copy->geometry.policy.limits.cVerticesMax = 8; }
            const auto mapNext = copy->nextId, geometryNext = copy->geometry.sourceIds.allocator.next.value, rev = copy->geometry.revision;
            audit.calls = 0; CHECK( MapMeshEdit_QuadSliceFace( copy.get(), root, face, 1, 1 ) == map_status_t::OK ); CHECK( audit.calls == 0 );
            CHECK( MapMeshEdit_QuadSliceFace( copy.get(), root, face, 3, 2 ) == map_status_t::LIMIT_EXCEEDED );
            CHECK( copy->nextId == mapNext ); CHECK( copy->geometry.sourceIds.allocator.next.value == geometryNext ); CHECK( copy->geometry.revision == rev );
            description_t after; after.Read( *copy, root ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
        }
        REQUIRE( MapMeshEdit_Triangulate( &map, { &root, 1 } ) == map_status_t::OK );
        description_t triangles; triangles.Read( map, root ); const auto triangle = triangles.value.faces.pData[0].sourceId.value;
        audit.calls = 0; CHECK( MapMeshEdit_QuadSliceFace( &map, root, triangle, 2, 2 ) == map_status_t::INVALID_ARGUMENT ); CHECK( audit.calls == 0 );
        CHECK( MapMeshEdit_QuadSliceFace( &map, root, triangle, 1, 1 ) == map_status_t::INVALID_ARGUMENT ); CHECK( audit.calls == 0 );
    }
    CHECK( audit.bytes == 0 );
}

TEST_CASE( "Every quad slice allocation failure releases its private copy and retry preserves the authored source", "[map][mesh-edit][quad-slice][oom]" )
{
    audit_t audit;
    {
        map_document_t original; Create( original, &audit.allocator ); const u64 root = Primitive( original );
        saved_t initial; REQUIRE( MapDocument_Save( &original, initial.Sink() ) == map_status_t::OK ); Metadata( original, root );
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, &audit.allocator ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( &original, { &root, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
        description_t before; before.Read( original, root ); const u64 face = TopFace( before.value );
        const auto next = original.nextId, registryNext = original.geometry.sourceIds.allocator.next.value, revision = original.geometry.revision;
        const usize baselineBytes = audit.bytes; usize allocations = 0;
        {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
            audit.calls = 0; REQUIRE( MapMeshEdit_QuadSliceFace( copy.get(), root, face, 3, 2 ) == map_status_t::OK ); allocations = audit.calls;
        }
        REQUIRE( allocations > 16 ); CHECK( audit.bytes == baselineBytes );
        for ( usize failure = 0; failure < allocations; ++failure ) {
            CAPTURE( failure, allocations );
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                audit.calls = 0; audit.failAt = failure;
                CHECK( MapMeshEdit_QuadSliceFace( copy.get(), root, face, 3, 2 ) == map_status_t::OUT_OF_MEMORY ); audit.failAt = CY_USIZE_MAX;
                // Publication failures still discard this private map;
                // geometry, snapshots and retained records never go live.
            }
            CHECK( audit.bytes == baselineBytes ); CHECK( original.nextId == next ); CHECK( original.geometry.sourceIds.allocator.next.value == registryNext );
            CHECK( original.geometry.revision == revision ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &original.geometry.sourceIds ) );
        }
        {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> retry( raw );
            REQUIRE( MapMeshEdit_QuadSliceFace( retry.get(), root, face, 3, 2 ) == map_status_t::OK );
            description_t result; result.Read( *retry, root ); CHECK( result.value.faces.nCount == 11 ); CHECK( result.value.vertices.nCount == 16 );
            CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &retry->geometry.sourceIds ) );
        }
        CHECK( audit.bytes == baselineBytes ); description_t unchanged; unchanged.Read( original, root );
        CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) );
    }
    CHECK( audit.bytes == 0 );
}
