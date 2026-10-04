//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Portable authored clipboard, ownership, identity, and failed-publication tests.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_Clipboard.h"
#include "CypherMap_Edit.h"
#include "CypherMap_Wireframe.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
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
string_view_t SV( const std::string &s ) { return { s.data(), s.size() }; }
string_view_t SV( const char *s ) { return StringView_FromCString( s ); }
struct files_t {
    std::string root; std::map<std::string, std::string> chunks;
    map_save_sink_t Sink()
    {
        return { this,
            []( void *c, string_view_t p, string_view_t t ) noexcept -> bool_t { static_cast<files_t *>( c )->chunks[std::string( p.pData, p.cchLength )] = std::string( t.pData, t.cchLength ); return CY_TRUE; },
            []( void *c, string_view_t p ) noexcept -> bool_t { static_cast<files_t *>( c )->chunks.erase( std::string( p.pData, p.cchLength ) ); return CY_TRUE; },
            []( void *c, string_view_t t ) noexcept -> bool_t { static_cast<files_t *>( c )->root.assign( t.pData, t.cchLength ); return CY_TRUE; } };
    }
};
void Load( map_document_t &m, const files_t &f, const allocator_t *a = Allocator_GetSystem() )
{
    std::vector<map_chunk_input_t> inputs;
    for ( const auto &[p, t] : f.chunks ) { inputs.push_back( { SV( p ), SV( t ) } ); }
    REQUIRE( MapDocument_Load( &m, a, SV( f.root ), { inputs.data(), inputs.size() } ) == map_status_t::OK );
}
void Facility( map_document_t &m )
{
    const std::filesystem::path dir = CYPHER_MAP_EXAMPLE_DIR;
    const auto read = []( const std::filesystem::path &p ) { std::ifstream in( p ); return std::string( std::istreambuf_iterator<char>( in ), {} ); };
    files_t f; f.root = read( dir / "facility.cymap" );
    for ( const auto &e : std::filesystem::recursive_directory_iterator( dir / "facility" ) ) {
        if ( e.path().extension() == ".cymapchunk" ) { f.chunks[e.path().lexically_relative( dir / "facility" ).generic_string()] = read( e.path() ); }
    }
    Load( m, f );
}
void Create( map_document_t &m, const allocator_t *a = Allocator_GetSystem() )
{
    REQUIRE( MapDocument_Create( &m, a, { SV( "Clipboard" ), SV( "reap" ), {} } ) == map_status_t::OK );
}
u64 Box( map_document_t &m )
{
    u64 id = 0;
    REQUIRE( MapEdit_CreateBox( &m, { { -32, -16, 0 }, { 32, 16, 64 } }, SV( "materials/clipboard/source.cymat" ), {}, &id ) == map_status_t::OK );
    return id;
}
const map_geometry_record_t *Placement( const map_document_t &m, u64 id )
{
    for ( usize i = 0; i < m.geometryRecords.nCount; ++i ) { if ( m.geometryRecords.pData[i].id == id ) { return &m.geometryRecords.pData[i]; } } return nullptr;
}
const map_wire_object_t *WireObject( const map_wireframe_t &w, u64 id )
{
    for ( usize i = 0; i < w.objects.nCount; ++i ) { if ( w.objects.pData[i].id == id ) { return &w.objects.pData[i]; } } return nullptr;
}
void CheckTravel( const map_wireframe_t &before, u64 oldId, const map_wireframe_t &after, u64 newId, math::vec3d_t offset )
{
    const auto *a = WireObject( before, oldId ), *b = WireObject( after, newId ); REQUIRE( a ); REQUIRE( b );
    CHECK( b->bounds.box.minimum.x == Catch::Approx( a->bounds.box.minimum.x + offset.x ) );
    CHECK( b->bounds.box.minimum.y == Catch::Approx( a->bounds.box.minimum.y + offset.y ) );
    CHECK( b->bounds.box.minimum.z == Catch::Approx( a->bounds.box.minimum.z + offset.z ) );
    CHECK( b->bounds.box.maximum.x == Catch::Approx( a->bounds.box.maximum.x + offset.x ) );
    CHECK( b->bounds.box.maximum.y == Catch::Approx( a->bounds.box.maximum.y + offset.y ) );
    CHECK( b->bounds.box.maximum.z == Catch::Approx( a->bounds.box.maximum.z + offset.z ) );
}
void SetString( map_document_t &m, u64 id, const char *key, const char *value )
{
    map_chunk_t *chunk = nullptr; auto *record = MapDocument_FindObject( &m, id, &chunk ); REQUIRE( record ); REQUIRE( chunk );
    auto *node = KeyValue_ObjectInsert( chunk->store.pDocument, record, SV( key ), key_value_type_t::STRING ); REQUIRE( node );
    REQUIRE( KeyValue_SetString( chunk->store.pDocument, node, SV( value ) ) );
}
std::string Field( map_document_t &m, u64 id, const char *key )
{
    auto *record = MapDocument_FindObject( &m, id, nullptr ); REQUIRE( record ); string_view_t value{};
    REQUIRE( KeyValue_GetString( KeyValue_Find( record, SV( key ) ), &value ) ); return { value.pData, value.cchLength };
}
struct allocation_audit_t {
    usize calls{ 0 }, failAt{ CY_USIZE_MAX }, bytes{ 0 }; allocator_t allocator{};
    allocation_audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *c, usize size, usize alignment ) noexcept -> void * {
            auto &a = *static_cast<allocation_audit_t *>( c ); if ( a.calls++ == a.failAt ) { return nullptr; }
            void *p = Allocator_Allocate( Allocator_GetSystem(), size, alignment ); if ( p ) { a.bytes += size; } return p;
        };
        allocator.pfnFree = []( void *c, void *p, usize size, usize alignment ) noexcept {
            if ( p ) { static_cast<allocation_audit_t *>( c )->bytes -= size; } Allocator_Free( Allocator_GetSystem(), p, size, alignment );
        };
    }
};
const std::string minimal =
    "@cykv 1\n@schema \"cypher.map.clipboard\" 1\n"
    "{ selection = [1u] bounds = [[1.0,2.0,3.0],[1.0,2.0,3.0]] objects = ["
    "{ kind = \"entity\" layer = \"default\" owner = 0u record = { id = 1u class = \"light\" origin = [1.0,2.0,3.0] properties = { color = [1.0,0.5,0.1] } } } ] }\n";
}

TEST_CASE( "Map clipboard carries mixed geometry and owned entities across material and identity domains", "[map][clipboard]" )
{
    map_document_t source; Facility( source ); SetString( source, 150, "plugin_text", "retain source strings" );
    SetString( source, 151, "plugin_shape", "authored brush metadata" );
    const u64 ids[]{ 151, 150, 1200, 1300, 1100, 101, 151 };
    text_buffer_t clip{}; REQUIRE( TextBuffer_Init( &clip, source.pAllocator ) );
    source.bReadOnly = CY_TRUE;
    const auto sourceRevision = source.geometry.revision, sourceNext = source.nextId;
    REQUIRE( MapClipboard_WriteSelection( &source, { ids, 7 }, &clip ) == map_status_t::OK );
    CHECK( source.geometry.revision == sourceRevision ); CHECK( source.nextId == sourceNext );
    map_bounds_t bounds{}; REQUIRE( MapClipboard_ReadBounds( source.pAllocator, TextBuffer_View( &clip ), &bounds ) == map_status_t::OK ); CHECK( bounds.bHas );
    map_wireframe_t before{}; REQUIRE( MapWireframe_Init( &before, source.pAllocator ) ); REQUIRE( MapWireframe_Build( &before, source ) == map_status_t::OK );
    for ( const bool matchingLayers : { false, true } ) {
        map_document_t destination; Create( destination ); Box( destination );
        u64 ignored = 0; REQUIRE( MapMaterials_Intern( &destination.materials, SV( "materials/clipboard/unrelated.cymat" ), &ignored ) );
        if ( matchingLayers ) { for ( const char *s : { "gameplay", "structure", "detail" } ) { REQUIRE( MapDocument_AddLayer( &destination, SV( s ), {} ) == map_status_t::OK ); } }
        destination.nextId = 8000; destination.geometry.sourceIds.allocator.next.value = 9000;
        const auto revision = destination.geometry.revision;
        vector_t<u64> pasted{}; REQUIRE( Vector_Init( &pasted, destination.pAllocator ) );
        const math::vec3d_t offset{ 64, -128, 32 };
        REQUIRE( MapClipboard_Paste( &destination, TextBuffer_View( &clip ), offset, &pasted ) == map_status_t::OK );
        REQUIRE( pasted.nCount == 6 ); CHECK( destination.geometry.revision == revision + 1 );
        for ( usize i = 0; i < pasted.nCount; ++i ) { CHECK( pasted.pData[i] >= 9000 ); CHECK( pasted.pData[i] != ids[i] ); }
        REQUIRE( Placement( destination, pasted.pData[0] ) ); CHECK( Placement( destination, pasted.pData[0] )->owner == pasted.pData[1] );
        CHECK( Field( destination, pasted.pData[1], "name" ) == "arena_door" );
        CHECK( Field( destination, pasted.pData[1], "plugin_text" ) == "retain source strings" );
        CHECK( Field( destination, pasted.pData[0], "plugin_shape" ) == "authored brush metadata" );
        const auto *a = geo::GeometryDocument_FindBrush( &source.geometry, { 151 } );
        const auto *b = geo::GeometryDocument_FindBrush( &destination.geometry, { pasted.pData[0] } ); REQUIRE( a ); REQUIRE( b );
        const auto *as = geo::GeometryDocument_FindBrushAttributes( &source.geometry, { 151 } );
        const auto *bs = geo::GeometryDocument_FindBrushAttributes( &destination.geometry, { pasted.pData[0] } ); REQUIRE( as ); REQUIRE( bs );
        for ( usize i = 0; i < a->sides.nCount; ++i ) { CHECK( b->sides.pData[i].sourceId.value != a->sides.pData[i].sourceId.value ); }
        CHECK( StringView_Equals( MapMaterials_Path( &source.materials, as->records.pData[0].material.value ), MapMaterials_Path( &destination.materials, bs->records.pData[0].material.value ) ) );
        map_wireframe_t after{}; REQUIRE( MapWireframe_Init( &after, destination.pAllocator ) ); REQUIRE( MapWireframe_Build( &after, destination ) == map_status_t::OK );
        for ( usize i = 0; i < pasted.nCount; ++i ) { CheckTravel( before, ids[i], after, pasted.pData[i], offset ); }
        CHECK( StringView_Equals( SV( destination.layers.pData[Placement( destination, pasted.pData[2] )->iLayer].id ), SV( matchingLayers ? "detail" : "default" ) ) );
        files_t saved; REQUIRE( MapDocument_Save( &destination, saved.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, saved );
        REQUIRE( Placement( loaded, pasted.pData[0] ) ); CHECK( Placement( loaded, pasted.pData[0] )->owner == pasted.pData[1] );
        CHECK( Field( loaded, pasted.pData[0], "plugin_shape" ) == "authored brush metadata" );
        CHECK( Field( loaded, pasted.pData[1], "plugin_text" ) == "retain source strings" );
        CHECK( geo::GeometryDocument_FindMesh( &loaded.geometry, { pasted.pData[2] } ) != nullptr );
        CHECK( geo::GeometryDocument_FindPatch( &loaded.geometry, { pasted.pData[3] } ) != nullptr );
        CHECK( geo::GeometryDocument_FindHeightField( &loaded.geometry, { pasted.pData[4] } ) != nullptr );
    }
}

TEST_CASE( "Map clipboard detaches child-only selections and preserves originless owners", "[map][clipboard]" )
{
    map_document_t source; Facility( source );
    text_buffer_t clip{}; REQUIRE( TextBuffer_Init( &clip, source.pAllocator ) );
    const u64 child = 151;
    REQUIRE( MapClipboard_WriteSelection( &source, { &child, 1 }, &clip ) == map_status_t::OK );
    map_document_t destination; Create( destination ); vector_t<u64> pasted{}; REQUIRE( Vector_Init( &pasted, destination.pAllocator ) );
    REQUIRE( MapClipboard_Paste( &destination, TextBuffer_View( &clip ), {}, &pasted ) == map_status_t::OK );
    REQUIRE( pasted.nCount == 1 ); REQUIRE( Placement( destination, pasted.pData[0] ) ); CHECK( Placement( destination, pasted.pData[0] )->owner == 0 );
    map_chunk_t *chunk = nullptr; auto *owner = MapDocument_FindObject( &source, 150, &chunk ); REQUIRE( owner ); REQUIRE( chunk );
    REQUIRE( KeyValue_Remove( chunk->store.pDocument, owner, KeyValue_Find( owner, SV( "origin" ) ) ) );
    // The map reader accepts positive signed IDs from authored CYKV. Retained
    // owner and child records need not already use the canonical U64 kind.
    REQUIRE( KeyValue_SetI64( chunk->store.pDocument, KeyValue_Find( owner, SV( "id" ) ), 150 ) );
    auto *childRecord = MapDocument_FindObject( &source, child, nullptr ); REQUIRE( childRecord );
    REQUIRE( KeyValue_SetI64( chunk->store.pDocument, KeyValue_Find( childRecord, SV( "id" ) ), 151 ) );
    for ( const u64 id : { 150u, 170u } ) {
        REQUIRE( MapClipboard_WriteSelection( &source, { &id, 1 }, &clip ) == map_status_t::OK );
        REQUIRE( MapClipboard_Paste( &destination, TextBuffer_View( &clip ), { 128, 0, 0 }, &pasted ) == map_status_t::OK );
        REQUIRE( pasted.nCount == 1 ); auto *record = MapDocument_FindObject( &destination, pasted.pData[0], nullptr ); REQUIRE( record );
        CHECK( KeyValue_Find( record, SV( "origin" ) ) == nullptr );
        if ( id == 150 ) {
            const map_geometry_record_t *owned = nullptr;
            for ( usize k = 0; k < destination.geometryRecords.nCount; ++k ) { if ( destination.geometryRecords.pData[k].owner == pasted.pData[0] ) { owned = &destination.geometryRecords.pData[k]; } }
            REQUIRE( owned ); geo::brush_source_t brush{};
            REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &destination.geometry, { owned->id }, destination.pAllocator, &brush ) == geo::geometry_status_t::OK );
            const auto bounds = MapGeometry_BrushBounds( brush, destination.geometryPolicy, destination.pAllocator ); geo::BrushSource_Shutdown( &brush );
            CHECK( bounds.box.minimum.x == Catch::Approx( 64 ) ); CHECK( bounds.box.maximum.x == Catch::Approx( 192 ) );
        }
    }
    const auto before = std::string( TextBuffer_CStr( &clip ) );
    const u64 unsupported[]{ 190, 200, 180 };
    for ( const u64 id : unsupported ) { CHECK( MapClipboard_WriteSelection( &source, { &id, 1 }, &clip ) == map_status_t::UNKNOWN_OBJECT ); CHECK( TextBuffer_CStr( &clip ) == before ); }
    auto *nested = KeyValue_Find( owner, SV( "brushes" ) ); REQUIRE( nested );
    auto *unknown = KeyValue_ArrayAppend( chunk->store.pDocument, nested, key_value_type_t::OBJECT ); REQUIRE( unknown );
    auto *unknownId = KeyValue_ObjectInsert( chunk->store.pDocument, unknown, SV( "id" ), key_value_type_t::U64 ); REQUIRE( unknownId ); REQUIRE( KeyValue_SetU64( chunk->store.pDocument, unknownId, 60000 ) );
    const u64 ownerId = 150; CHECK( MapClipboard_WriteSelection( &source, { &ownerId, 1 }, &clip ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Map clipboard rejects malformed bounded input and protects caller outputs", "[map][clipboard]" )
{
    map_document_t destination; Create( destination ); vector_t<u64> out{}; REQUIRE( Vector_Init( &out, destination.pAllocator ) ); REQUIRE( Vector_PushBack( &out, u64{ 777 } ) );
    map_bounds_t bounds{ { { -9, -8, -7 }, { 9, 8, 7 } }, CY_TRUE };
    const auto checkBad = [&]( const std::string &text ) {
        CHECK( MapClipboard_ReadBounds( destination.pAllocator, SV( text ), &bounds ) != map_status_t::OK ); CHECK( bounds.box.minimum.x == -9 );
        CHECK( MapClipboard_Paste( &destination, SV( text ), {}, &out ) != map_status_t::OK ); CHECK( out.nCount == 1 ); CHECK( out.pData[0] == 777 );
    };
    checkBad( "unrelated clipboard text" ); checkBad( "" );
    for ( const auto &[from, to] : std::vector<std::pair<std::string, std::string>>{
        { "@schema \"cypher.map.clipboard\" 1", "@schema \"cypher.map.clipboard\" 2" }, { "selection = [1u]", "selection = [1u,1u]" },
        { "selection = [1u]", "selection = [2u]" }, { "owner = 0u", "owner = 5u" }, { "kind = \"entity\"", "kind = \"prefab\"" },
        { "id = 1u", "id = 0u" }, { "layer = \"default\"", "layer = \"../escape\"" },
        { "origin = [1.0,2.0,3.0]", "origin = [1e999,2.0,3.0]" }, { "class = \"light\"", "class = \"light\" class = \"other\"" },
        { "[1.0,2.0,3.0],[1.0,2.0,3.0]", "[2.0,2.0,3.0],[1.0,2.0,3.0]" },
        { "properties =", "brushes = [] properties =" } } ) {
        auto bad = minimal; const auto p = bad.find( from ); REQUIRE( p != std::string::npos ); bad.replace( p, from.size(), to ); checkBad( bad );
    }
    const std::string oversized( MAP_CLIPBOARD_TEXT_MAX + 1, ' ' );
    CHECK( MapClipboard_Paste( &destination, SV( oversized ), {}, &out ) == map_status_t::LIMIT_EXCEEDED );
    CHECK( MapClipboard_ReadBounds( destination.pAllocator, SV( oversized ), &bounds ) == map_status_t::LIMIT_EXCEEDED );
    CHECK( MapClipboard_Paste( &destination, SV( minimal ), { std::numeric_limits<f64>::quiet_NaN(), 0, 0 }, &out ) == map_status_t::INVALID_ARGUMENT );
    destination.bReadOnly = CY_TRUE; CHECK( MapClipboard_Paste( &destination, SV( minimal ), {}, &out ) == map_status_t::READ_ONLY ); destination.bReadOnly = CY_FALSE;
    destination.geometry.revision = CY_U64_MAX; CHECK( MapClipboard_Paste( &destination, SV( minimal ), {}, &out ) == map_status_t::LIMIT_EXCEEDED ); destination.geometry.revision = 0;
    destination.nextId = 0; CHECK( MapClipboard_Paste( &destination, SV( minimal ), {}, &out ) == map_status_t::LIMIT_EXCEEDED );
    destination.nextId = CY_U64_MAX; CHECK( MapClipboard_Paste( &destination, SV( minimal ), {}, &out ) == map_status_t::LIMIT_EXCEEDED );
    CHECK( out.nCount == 1 ); CHECK( out.pData[0] == 777 );
}

TEST_CASE( "Map retained geometry records use owned storage without consuming IDs", "[map][clipboard]" )
{
    map_document_t m; Create( m ); const u64 id = Box( m ), next = m.nextId, revision = m.geometry.revision;
    key_value_document_desc_t desc{}; desc.pAllocator = m.pAllocator; auto *d = KeyValue_CreateDocument( desc ); REQUIRE( d );
    REQUIRE( KeyValue_SetRootType( d, key_value_type_t::OBJECT ) ); auto *record = KeyValue_Root( d );
    auto *slot = KeyValue_ObjectInsert( d, record, SV( "id" ), key_value_type_t::U64 ); REQUIRE( slot ); REQUIRE( KeyValue_SetU64( d, slot, id ) );
    auto *name = KeyValue_ObjectInsert( d, record, SV( "name" ), key_value_type_t::STRING ); REQUIRE( name ); REQUIRE( KeyValue_SetString( d, name, SV( "retained" ) ) );
    REQUIRE( MapDocument_InsertGeometryRecord( &m, map_geometry_kind_t::BRUSH, record ) == map_status_t::OK );
    CHECK( m.nextId == next ); CHECK( m.geometry.revision == revision );
    CHECK( MapDocument_InsertGeometryRecord( &m, map_geometry_kind_t::BRUSH, record ) == map_status_t::INVALID_ARGUMENT );
    KeyValue_DestroyDocument( d ); CHECK( Field( m, id, "name" ) == "retained" );
    files_t f; REQUIRE( MapDocument_Save( &m, f.Sink() ) == map_status_t::OK ); map_document_t reloaded; Load( reloaded, f ); CHECK( Field( reloaded, id, "name" ) == "retained" );
}

TEST_CASE( "Map clipboard accepts authored long classes but never cuts malformed identities", "[map][clipboard]" )
{
    map_document_t source; Create( source ); u64 id = 0;
    REQUIRE( MapDocument_AddEntity( &source, SV( "default" ), SV( "light" ), {}, &id ) == map_status_t::OK );
    map_chunk_t *chunk = nullptr; auto *record = MapDocument_FindObject( &source, id, &chunk ); REQUIRE( record ); REQUIRE( chunk );
    const std::string longClass( MAP_NAME_MAX_LENGTH, 'a' );
    REQUIRE( KeyValue_SetString( chunk->store.pDocument, KeyValue_Find( record, SV( "class" ) ), SV( longClass ) ) );
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, source.pAllocator ) );
    REQUIRE( MapClipboard_WriteSelection( &source, { &id, 1 }, &text ) == map_status_t::OK );
    map_document_t destination; Create( destination ); vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, destination.pAllocator ) );
    REQUIRE( MapClipboard_Paste( &destination, TextBuffer_View( &text ), {}, &roots ) == map_status_t::OK ); REQUIRE( roots.nCount == 1 );
    CHECK( Field( destination, roots.pData[0], "class" ) == longClass );
    const auto validText = std::string( TextBuffer_CStr( &text ) );
    REQUIRE( KeyValue_SetString( chunk->store.pDocument, KeyValue_Find( record, SV( "class" ) ), SV( "" ) ) );
    CHECK( MapClipboard_WriteSelection( &source, { &id, 1 }, &text ) != map_status_t::OK ); CHECK( TextBuffer_CStr( &text ) == validText );
    auto large = minimal; const auto position = large.find( "origin = [1.0,2.0,3.0]" ); REQUIRE( position != std::string::npos );
    large.replace( position, std::string( "origin = [1.0,2.0,3.0]" ).size(), "origin = [1000001.0,2.0,3.0]" );
    CHECK( MapClipboard_Paste( &destination, SV( large ), {}, &roots ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Map clipboard canonicalizes integer origins and rejects unsafe source coordinates", "[map][clipboard]" )
{
    map_document_t source; Create( source ); u64 id = 0;
    REQUIRE( MapDocument_AddEntity( &source, SV( "default" ), SV( "light" ), { 1, 2, 3 }, &id ) == map_status_t::OK );
    map_chunk_t *chunk = nullptr; auto *record = MapDocument_FindObject( &source, id, &chunk ); REQUIRE( record ); REQUIRE( chunk );
    REQUIRE( KeyValue_SetI64( chunk->store.pDocument, KeyValue_Find( record, SV( "id" ) ), static_cast<i64>( id ) ) );
    auto *origin = KeyValue_Find( record, SV( "origin" ) ); REQUIRE( origin );
    for ( usize i = 0; i < 3; ++i ) { REQUIRE( KeyValue_SetI64( chunk->store.pDocument, KeyValue_ChildAt( origin, i ), static_cast<i64>( i + 1 ) ) ); }
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, source.pAllocator ) );
    REQUIRE( MapClipboard_WriteSelection( &source, { &id, 1 }, &text ) == map_status_t::OK );
    map_document_t destination; Create( destination ); vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, destination.pAllocator ) );
    REQUIRE( MapClipboard_Paste( &destination, TextBuffer_View( &text ), { 64, -32, 16 }, &roots ) == map_status_t::OK ); REQUIRE( roots.nCount == 1 );
    auto *pasted = MapDocument_FindObject( &destination, roots.pData[0], nullptr ); REQUIRE( pasted );
    const auto *placed = KeyValue_Find( pasted, SV( "origin" ) ); f64 component = 0;
    REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( placed, 0 ), &component ) ); CHECK( component == 65 );
    REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( placed, 1 ), &component ) ); CHECK( component == -30 );
    REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( placed, 2 ), &component ) ); CHECK( component == 19 );
    const auto validText = std::string( TextBuffer_CStr( &text ) );
    REQUIRE( KeyValue_SetF64( chunk->store.pDocument, KeyValue_ChildAt( origin, 0 ), source.geometryPolicy.numerical.fCoordinateMagnitudeLimit + 1 ) );
    CHECK( MapClipboard_WriteSelection( &source, { &id, 1 }, &text ) == map_status_t::INVALID_ARGUMENT ); CHECK( TextBuffer_CStr( &text ) == validText );
}

TEST_CASE( "Map clipboard allocation failures release private work and preserve text and roots", "[map][clipboard][allocation]" )
{
    allocation_audit_t audit;
    {
        map_document_t source; Create( source, &audit.allocator ); const u64 id = Box( source );
        text_buffer_t out{}; REQUIRE( TextBuffer_Init( &out, &audit.allocator ) ); REQUIRE( TextBuffer_Assign( &out, SV( "keep clipboard" ) ) );
        const usize before = audit.bytes; bool copied = false; usize copyFailures = 0;
        for ( usize fail = 0; fail < 2048; ++fail ) {
            audit.calls = 0; audit.failAt = fail; const auto status = MapClipboard_WriteSelection( &source, { &id, 1 }, &out );
            audit.failAt = CY_USIZE_MAX;
            if ( status == map_status_t::OK ) { copied = true; break; }
            REQUIRE( status == map_status_t::OUT_OF_MEMORY ); CHECK( TextBuffer_CStr( &out ) == std::string( "keep clipboard" ) ); CHECK( audit.bytes == before ); ++copyFailures;
        }
        REQUIRE( copied ); CHECK( copyFailures > 8 );
        map_document_t destination; Create( destination, &audit.allocator ); Box( destination );
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, &audit.allocator ) ); REQUIRE( Vector_PushBack( &roots, u64{ 777 } ) );
        const usize baseline = audit.bytes; bool pasted = false; usize pasteFailures = 0;
        const auto revision = destination.geometry.revision, next = destination.nextId;
        for ( usize fail = 0; fail < 4096; ++fail ) {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &destination, &raw ) == map_status_t::OK );
            audit.calls = 0; audit.failAt = fail; const auto status = MapClipboard_Paste( raw, TextBuffer_View( &out ), { 64, 0, 0 }, &roots ); audit.failAt = CY_USIZE_MAX;
            delete raw;
            if ( status == map_status_t::OK ) { pasted = true; break; }
            REQUIRE( status == map_status_t::OUT_OF_MEMORY ); CHECK( roots.nCount == 1 ); CHECK( roots.pData[0] == 777 ); CHECK( audit.bytes == baseline ); ++pasteFailures;
            CHECK( destination.geometry.revision == revision ); CHECK( destination.nextId == next );
        }
        REQUIRE( pasted ); CHECK( pasteFailures > 8 ); CHECK( destination.geometry.brushes.nCount == 1 );
    }
    CHECK( audit.bytes == 0 );
}
