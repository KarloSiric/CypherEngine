//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Entity property ownership, typed persistence, no-op and allocation contracts.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_EntityEdit.h"
#include "CypherMap_Edit.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace
{
string_view_t SV( const char *text ) { return StringView_FromCString( text ); }
string_view_t SV( const std::string &text ) { return { text.data(), text.size() }; }
void Create( map_document_t &map, const allocator_t *allocator = Allocator_GetSystem() )
{
    REQUIRE( MapDocument_Create( &map, allocator, { SV( "Entity properties" ), SV( "reap" ), {} } ) == map_status_t::OK );
}
u64 AddEntity( map_document_t &map, const char *className = "light" )
{
    u64 id{};
    REQUIRE( MapDocument_AddEntity( &map, SV( "default" ), SV( className ), {}, &id ) == map_status_t::OK );
    return id;
}
key_value_t *Property( map_document_t &map, u64 id, const char *key )
{
    return KeyValue_Find( KeyValue_Find( MapDocument_FindObject( &map, id, nullptr ), SV( "properties" ) ), SV( key ) );
}
struct value_t {
    std::unique_ptr<key_value_document_t, decltype( &KeyValue_DestroyDocument )> document;
    explicit value_t( const char *text ) : document( KeyValue_CreateDocument( { Allocator_GetSystem() } ), &KeyValue_DestroyDocument )
    {
        REQUIRE( document != nullptr );
        const std::string source = std::string( "@cykv 1\n@schema \"cypher.test\" 1\n{ value = " ) + text + " }";
        REQUIRE( KeyValue_ParseText( SV( source ), {}, document.get() ).status == key_value_parse_status_t::OK );
    }
    key_value_t *Get() { return KeyValue_Find( KeyValue_Root( document.get() ), SV( "value" ) ); }
};
struct files_t {
    std::string root; std::map<std::string, std::string> chunks;
    map_save_sink_t Sink()
    {
        return { this,
            []( void *ctx, string_view_t path, string_view_t text ) noexcept -> bool_t {
                static_cast<files_t *>( ctx )->chunks[std::string( path.pData, path.cchLength )] = std::string( text.pData, text.cchLength ); return CY_TRUE;
            },
            []( void *ctx, string_view_t path ) noexcept -> bool_t {
                static_cast<files_t *>( ctx )->chunks.erase( std::string( path.pData, path.cchLength ) ); return CY_TRUE;
            },
            []( void *ctx, string_view_t text ) noexcept -> bool_t {
                static_cast<files_t *>( ctx )->root.assign( text.pData, text.cchLength ); return CY_TRUE;
            } };
    }
};
void Load( map_document_t &map, const files_t &files )
{
    std::vector<map_chunk_input_t> chunks;
    for ( const auto &[path, text] : files.chunks ) { chunks.push_back( { SV( path ), SV( text ) } ); }
    REQUIRE( MapDocument_Load( &map, Allocator_GetSystem(), SV( files.root ), { chunks.data(), chunks.size() } ) == map_status_t::OK );
}
struct allocation_audit_t {
    usize calls{}, failAt{ CY_USIZE_MAX }, bytes{};
    allocator_t allocator{};
    allocation_audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *ctx, usize size, usize alignment ) noexcept -> void * {
            auto &audit = *static_cast<allocation_audit_t *>( ctx );
            if ( audit.calls++ == audit.failAt ) { return nullptr; }
            void *memory = Allocator_Allocate( Allocator_GetSystem(), size, alignment );
            if ( memory != nullptr ) { audit.bytes += size; } return memory;
        };
        allocator.pfnFree = []( void *ctx, void *memory, usize size, usize alignment ) noexcept {
            if ( memory != nullptr ) { static_cast<allocation_audit_t *>( ctx )->bytes -= size; }
            Allocator_Free( Allocator_GetSystem(), memory, size, alignment );
        };
    }
};
} // namespace

TEST_CASE( "Entity property owner resolution deduplicates entities and owned geometry and rejects world mixtures", "[map][entity-edit][ownership]" )
{
    map_document_t map; Create( map ); const u64 entity = AddEntity( map, "trigger_once" ), light = AddEntity( map );
    u64 brush{}, world{};
    REQUIRE( MapEdit_CreateBox( &map, { { 0, 0, 0 }, { 32, 32, 32 } }, {}, {}, &brush ) == map_status_t::OK );
    REQUIRE( MapEdit_CreateBox( &map, { { 64, 0, 0 }, { 96, 32, 32 } }, {}, {}, &world ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &map, brush, entity ) == map_status_t::OK );
    vector_t<u64> owners; REQUIRE( Vector_Init( &owners, map.pAllocator ) );
    const u64 selection[]{ brush, entity, light, brush };
    REQUIRE( MapEntityEdit_ResolveOwners( &map, { selection, 4 }, &owners ) == map_status_t::OK );
    REQUIRE( owners.nCount == 2 ); CHECK( owners.pData[0] == entity ); CHECK( owners.pData[1] == light );
    Vector_Clear( &owners );
    const u64 mixed[]{ entity, world };
    CHECK( MapEntityEdit_ResolveOwners( &map, { mixed, 2 }, &owners ) == map_status_t::INVALID_ARGUMENT ); CHECK( owners.nCount == 0 );
    const u64 missing[]{ entity, 999999 };
    CHECK( MapEntityEdit_ResolveOwners( &map, { missing, 2 }, &owners ) == map_status_t::UNKNOWN_OBJECT ); CHECK( owners.nCount == 0 );
    vector_t<u64> uninitialized;
    CHECK( MapEntityEdit_ResolveOwners( &map, { &entity, 1 }, &uninitialized ) == map_status_t::INVALID_ARGUMENT );
    map.bReadOnly = CY_TRUE;
    REQUIRE( MapEntityEdit_ResolveOwners( &map, { &entity, 1 }, &owners ) == map_status_t::OK );
    REQUIRE( owners.nCount == 1 ); CHECK( owners.pData[0] == entity );
}

TEST_CASE( "Typed entity property changes preserve nested data identity and ordered outputs through save reload", "[map][entity-edit][persistence]" )
{
    map_document_t map; Create( map ); const u64 ids[]{ AddEntity( map ), AddEntity( map ) };
    const auto revision = map.geometry.revision, nextId = map.nextId;
    value_t nested( "{ unsigned = 7u signed = -3 real = 1.25 enabled = true empty = null labels = [ \"one\", \"two\" ] future = { custom = \"retained\" } }" );
    bool changed{};
    REQUIRE( MapEntityEdit_SetProperty( &map, { ids, 2 }, SV( "light_settings" ), nested.Get(), &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( MapEntityEdit_ValuesEqual( Property( map, ids[0], "light_settings" ), nested.Get() ) );
    REQUIRE( MapEntityEdit_SetProperty( &map, { ids, 2 }, SV( "light_settings" ), nested.Get(), &changed ) == map_status_t::OK ); CHECK_FALSE( changed );
    value_t outputs( "[ { output = \"on_trigger\" target = \"door\" input = \"open\" parameter = \"\" delay = 0.0 times = -1 }, { output = \"on_trigger\" target = \"sound\" input = \"play\" parameter = \"\" delay = 0.5 times = 1 } ]" );
    map_chunk_t *chunk{}; auto *entity = MapDocument_FindObject( &map, ids[0], &chunk );
    auto *outputMember = KeyValue_ObjectInsert( chunk->store.pDocument, entity, SV( "outputs" ), key_value_type_t::ARRAY ); REQUIRE( outputMember );
    for ( usize i = 0; i < KeyValue_ChildCount( outputs.Get() ); ++i ) { REQUIRE( KeyValue_CloneInto( chunk->store.pDocument, outputMember, KeyValue_ChildAt( outputs.Get(), i ) ) ); }
    REQUIRE( MapEntityEdit_SetIdentityField( &map, { ids, 2 }, SV( "name" ), SV( "shared_light" ), &changed ) == map_status_t::OK ); CHECK( changed );
    REQUIRE( MapEntityEdit_SetIdentityField( &map, { ids, 2 }, SV( "class" ), SV( "light_spot" ), &changed ) == map_status_t::OK ); CHECK( changed );
    REQUIRE( MapEntityEdit_RenameProperty( &map, { ids, 2 }, SV( "light_settings" ), SV( "settings" ), &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( map.geometry.revision == revision ); CHECK( map.nextId == nextId );
    files_t files; REQUIRE( MapDocument_Save( &map, files.Sink() ) == map_status_t::OK );
    map_document_t reloaded; Load( reloaded, files );
    for ( const u64 id : ids ) {
        CHECK( Property( reloaded, id, "light_settings" ) == nullptr );
        CHECK( MapEntityEdit_ValuesEqual( Property( reloaded, id, "settings" ), nested.Get() ) );
        string_view_t name{}, className{}; auto *record = MapDocument_FindObject( &reloaded, id, nullptr );
        REQUIRE( KeyValue_GetString( KeyValue_Find( record, SV( "name" ) ), &name ) ); CHECK( StringView_Equals( name, SV( "shared_light" ) ) );
        REQUIRE( KeyValue_GetString( KeyValue_Find( record, SV( "class" ) ), &className ) ); CHECK( StringView_Equals( className, SV( "light_spot" ) ) );
    }
    CHECK( MapEntityEdit_ValuesEqual( KeyValue_Find( MapDocument_FindObject( &reloaded, ids[0], nullptr ), SV( "outputs" ) ), outputs.Get() ) );
    REQUIRE( MapEntityEdit_RemoveProperty( &reloaded, { ids, 2 }, SV( "settings" ), &changed ) == map_status_t::OK ); CHECK( changed );
    REQUIRE( MapEntityEdit_RemoveProperty( &reloaded, { ids, 2 }, SV( "settings" ), &changed ) == map_status_t::OK ); CHECK_FALSE( changed );
}

TEST_CASE( "Property setters replace one key and safely stage aliased nested values", "[map][entity-edit][aliasing]" )
{
    map_document_t map; Create( map ); const u64 id = AddEntity( map ); bool changed{};
    value_t value( "{ nested = { enabled = true } spare = 7u }" );
    REQUIRE( MapEntityEdit_SetProperty( &map, { &id, 1 }, SV( "settings" ), value.Get(), &changed ) == map_status_t::OK );
    const auto *aliased = KeyValue_Find( Property( map, id, "settings" ), SV( "nested" ) ); REQUIRE( aliased );
    REQUIRE( MapEntityEdit_SetProperty( &map, { &id, 1 }, SV( "settings" ), aliased, &changed ) == map_status_t::OK ); CHECK( changed );
    value_t expected( "{ enabled = true }" ); CHECK( MapEntityEdit_ValuesEqual( Property( map, id, "settings" ), expected.Get() ) );
    value_t string( "\"final\"" );
    for ( int repeat = 0; repeat < 4; ++repeat ) { REQUIRE( MapEntityEdit_SetProperty( &map, { &id, 1 }, SV( "settings" ), string.Get(), &changed ) == map_status_t::OK ); }
    const auto *properties = KeyValue_Find( MapDocument_FindObject( &map, id, nullptr ), SV( "properties" ) );
    REQUIRE( KeyValue_ChildCount( properties ) == 1 );
    files_t files; REQUIRE( MapDocument_Save( &map, files.Sink() ) == map_status_t::OK ); map_document_t loaded; Load( loaded, files );
    CHECK( MapEntityEdit_ValuesEqual( Property( loaded, id, "settings" ), string.Get() ) );
}

TEST_CASE( "Entity value equality keeps numeric kinds and array order and ignores object key order", "[map][entity-edit][equality]" )
{
    value_t first( "{ a = 1 b = [ true, false ] }" ), reordered( "{ b = [ true, false ] a = 1 }" ), arrayOrder( "{ a = 1 b = [ false, true ] }" );
    value_t integer( "1" ), unsignedInteger( "1u" ), real( "1.0" );
    CHECK( MapEntityEdit_ValuesEqual( first.Get(), reordered.Get() ) ); CHECK_FALSE( MapEntityEdit_ValuesEqual( first.Get(), arrayOrder.Get() ) );
    CHECK_FALSE( MapEntityEdit_ValuesEqual( integer.Get(), unsignedInteger.Get() ) ); CHECK_FALSE( MapEntityEdit_ValuesEqual( integer.Get(), real.Get() ) );
    CHECK( MapEntityEdit_ValuesEqual( nullptr, nullptr ) ); CHECK_FALSE( MapEntityEdit_ValuesEqual( first.Get(), nullptr ) );
}

TEST_CASE( "Rename collisions and invalid batches are rejected before any owner changes", "[map][entity-edit][preflight]" )
{
    map_document_t map; Create( map ); const u64 ids[]{ AddEntity( map ), AddEntity( map ) }; bool changed{}; value_t value( "123" );
    REQUIRE( MapEntityEdit_SetProperty( &map, { ids, 2 }, SV( "old" ), value.Get(), &changed ) == map_status_t::OK );
    REQUIRE( MapEntityEdit_SetProperty( &map, { ids + 1, 1 }, SV( "new" ), value.Get(), &changed ) == map_status_t::OK );
    CHECK( MapEntityEdit_RenameProperty( &map, { ids, 2 }, SV( "old" ), SV( "new" ), &changed ) == map_status_t::INVALID_ARGUMENT ); CHECK_FALSE( changed );
    CHECK( Property( map, ids[0], "old" ) != nullptr ); CHECK( Property( map, ids[0], "new" ) == nullptr ); CHECK( Property( map, ids[1], "old" ) != nullptr );
    REQUIRE( MapEntityEdit_RenameProperty( &map, { ids, 2 }, SV( "old" ), SV( "old" ), &changed ) == map_status_t::OK ); CHECK_FALSE( changed );
    const u64 invalid[]{ ids[0], 0u };
    CHECK( MapEntityEdit_RemoveProperty( &map, { invalid, 2 }, SV( "old" ), &changed ) == map_status_t::UNKNOWN_OBJECT ); CHECK( Property( map, ids[0], "old" ) );
    CHECK( MapEntityEdit_SetIdentityField( &map, { ids, 2 }, SV( "id" ), SV( "7" ), &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapEntityEdit_SetIdentityField( &map, { ids, 2 }, SV( "class" ), {}, &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapEntityEdit_SetProperty( &map, { ids, 2 }, {}, value.Get(), &changed ) == map_status_t::INVALID_ARGUMENT );
    std::string tooLong( MAP_ENTITY_PROPERTY_KEY_MAX + 1u, 'x' );
    CHECK( MapEntityEdit_SetProperty( &map, { ids, 2 }, SV( tooLong ), value.Get(), &changed ) == map_status_t::INVALID_ARGUMENT );
    map.bReadOnly = CY_TRUE;
    CHECK( MapEntityEdit_SetProperty( &map, { ids, 2 }, SV( "old" ), value.Get(), &changed ) == map_status_t::READ_ONLY ); CHECK_FALSE( changed );
}

TEST_CASE( "Malformed duplicate properties and excessive value depth cannot be edited into a map", "[map][entity-edit][validation]" )
{
    map_document_t map; Create( map ); const u64 id = AddEntity( map ); bool changed{}; value_t value( "true" );
    map_chunk_t *chunk{}; auto *entity = MapDocument_FindObject( &map, id, &chunk );
    auto *properties = KeyValue_ObjectInsert( chunk->store.pDocument, entity, SV( "properties" ), key_value_type_t::STRING ); REQUIRE( properties );
    CHECK( MapEntityEdit_SetProperty( &map, { &id, 1 }, SV( "enabled" ), value.Get(), &changed ) == map_status_t::INVALID_ARGUMENT ); CHECK_FALSE( changed );
    REQUIRE( KeyValue_SetContainerType( chunk->store.pDocument, properties, key_value_type_t::OBJECT ) );
    REQUIRE( KeyValue_ObjectInsert( chunk->store.pDocument, properties, SV( "duplicate" ), key_value_type_t::BOOL ) );
    REQUIRE( KeyValue_ObjectInsert( chunk->store.pDocument, properties, SV( "duplicate" ), key_value_type_t::BOOL ) );
    CHECK( MapEntityEdit_RemoveProperty( &map, { &id, 1 }, SV( "duplicate" ), &changed ) == map_status_t::INVALID_ARGUMENT ); CHECK( KeyValue_ChildCount( properties ) == 2 );
    REQUIRE( KeyValue_SetContainerType( chunk->store.pDocument, properties, key_value_type_t::OBJECT ) );
    auto *deep = value.Get(); REQUIRE( KeyValue_SetContainerType( value.document.get(), deep, key_value_type_t::ARRAY ) );
    for ( usize level = 0; level <= MAP_ENTITY_PROPERTY_VALUE_DEPTH_MAX; ++level ) { deep = KeyValue_ArrayAppend( value.document.get(), deep, key_value_type_t::ARRAY ); REQUIRE( deep ); }
    CHECK( MapEntityEdit_SetProperty( &map, { &id, 1 }, SV( "deep" ), value.Get(), &changed ) == map_status_t::LIMIT_EXCEEDED ); CHECK_FALSE( changed );
    CHECK( KeyValue_ChildCount( properties ) == 0 );
}

TEST_CASE( "Failed property allocations release private copies and preserve the original map", "[map][entity-edit][allocation][atomic]" )
{
    allocation_audit_t audit;
    {
        map_document_t original; Create( original, &audit.allocator ); const u64 ids[]{ AddEntity( original ), AddEntity( original ) };
        value_t large( "\"placeholder\"" ); std::string payload( 64u * CY_KIB, 'q' );
        REQUIRE( KeyValue_SetString( large.document.get(), large.Get(), SV( payload ) ) );
        const usize baseline = audit.bytes; const auto nextId = original.nextId, revision = original.geometry.revision;
        bool succeeded = false; usize failures{};
        for ( usize failAt = 0; failAt < 128u; ++failAt ) {
            audit.failAt = CY_USIZE_MAX;
            map_document_t *raw{}; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK );
            {
                std::unique_ptr<map_document_t> working( raw ); audit.calls = 0; audit.failAt = failAt; bool changed = true;
                const auto status = MapEntityEdit_SetProperty( working.get(), { ids, 2 }, SV( "script_data" ), large.Get(), &changed );
                if ( status == map_status_t::OK ) { CHECK( changed ); succeeded = true; }
                else { CHECK( status == map_status_t::OUT_OF_MEMORY ); CHECK_FALSE( changed ); ++failures; }
                audit.failAt = CY_USIZE_MAX;
            }
            CHECK( audit.bytes == baseline ); CHECK( original.nextId == nextId ); CHECK( original.geometry.revision == revision );
            CHECK( Property( original, ids[0], "script_data" ) == nullptr ); CHECK( Property( original, ids[1], "script_data" ) == nullptr );
            if ( succeeded ) { break; }
        }
        CHECK( succeeded ); CHECK( failures >= 3u );
    }
    CHECK( audit.bytes == 0u );
}

TEST_CASE( "Read-only child cursors preserve authored order for arrays objects roots and scalars", "[map][entity-edit][keyvalue][traversal]" )
{
    value_t object( "{ first = true second = [ 7u, -4 ] third = {} }" );
    CHECK( KeyValue_FirstChild( nullptr ) == nullptr ); CHECK( KeyValue_NextSibling( nullptr ) == nullptr );
    CHECK( KeyValue_NextSibling( KeyValue_Root( object.document.get() ) ) == nullptr );
    const auto *first = KeyValue_FirstChild( object.Get() ); REQUIRE( first );
    CHECK( StringView_Equals( KeyValue_Name( first ), SV( "first" ) ) ); CHECK( KeyValue_FirstChild( first ) == nullptr );
    const auto *second = KeyValue_NextSibling( first ); REQUIRE( second ); CHECK( StringView_Equals( KeyValue_Name( second ), SV( "second" ) ) );
    const auto *third = KeyValue_NextSibling( second ); REQUIRE( third ); CHECK( KeyValue_FirstChild( third ) == nullptr ); CHECK( KeyValue_NextSibling( third ) == nullptr );
    const auto *unsignedValue = KeyValue_FirstChild( second ); REQUIRE( unsignedValue ); CHECK( KeyValue_Type( unsignedValue ) == key_value_type_t::U64 );
    const auto *signedValue = KeyValue_NextSibling( unsignedValue ); REQUIRE( signedValue ); CHECK( KeyValue_Type( signedValue ) == key_value_type_t::I64 ); CHECK( KeyValue_NextSibling( signedValue ) == nullptr );
}

TEST_CASE( "Entity lookup traverses dense entity arrays and validates borrowed records on read-only maps", "[map][entity-edit][lookup]" )
{
    map_document_t map; Create( map ); const u64 first = AddEntity( map );
    map_chunk_t *chunk{}; REQUIRE( MapDocument_FindObject( &map, first, &chunk ) );
    auto *entities = KeyValue_Find( KeyValue_Root( chunk->store.pDocument ), SV( "entities" ) ); REQUIRE( entities );
    u64 last = first;
    // Append directly so setup does not exercise the old indexed map lookup.
    for ( usize i = 0; i < 8192u; ++i ) {
        auto *record = KeyValue_ArrayAppend( chunk->store.pDocument, entities, key_value_type_t::OBJECT ); REQUIRE( record );
        last = MapDocument_AllocateId( &map );
        auto *id = KeyValue_ObjectInsert( chunk->store.pDocument, record, SV( "id" ), key_value_type_t::U64 ); REQUIRE( id ); REQUIRE( KeyValue_SetU64( chunk->store.pDocument, id, last ) );
        auto *className = KeyValue_ObjectInsert( chunk->store.pDocument, record, SV( "class" ), key_value_type_t::STRING ); REQUIRE( className ); REQUIRE( KeyValue_SetString( chunk->store.pDocument, className, SV( "light" ) ) );
    }
    map.bReadOnly = CY_TRUE;
    const auto *record = MapEntityEdit_FindEntity( &map, last ); REQUIRE( record ); u64 id{};
    REQUIRE( KeyValue_GetU64( KeyValue_Find( record, SV( "id" ) ), &id ) ); CHECK( id == last );
    CHECK( MapEntityEdit_FindEntity( &map, last + 1u ) == nullptr ); CHECK( MapEntityEdit_FindEntity( nullptr, last ) == nullptr ); CHECK( MapEntityEdit_FindEntity( &map, 0u ) == nullptr );
    vector_t<u64> owners; REQUIRE( Vector_Init( &owners, map.pAllocator ) );
    REQUIRE( MapEntityEdit_ResolveOwners( &map, { &last, 1 }, &owners ) == map_status_t::OK ); REQUIRE( owners.nCount == 1u ); CHECK( owners.pData[0] == last );
}

TEST_CASE( "Entity record growth is rejected before adding properties or identity fields past its bound", "[map][entity-edit][limit][preflight]" )
{
    map_document_t map; Create( map ); const u64 ids[]{ AddEntity( map ), AddEntity( map ) };
    map_chunk_t *chunk{}; auto *full = MapDocument_FindObject( &map, ids[1], &chunk ); REQUIRE( full );
    while ( KeyValue_ChildCount( full ) < MAP_ENTITY_RECORD_MEMBERS_MAX ) {
        const std::string key = "future_" + std::to_string( KeyValue_ChildCount( full ) );
        REQUIRE( KeyValue_ObjectInsert( chunk->store.pDocument, full, SV( key ), key_value_type_t::NULL_VALUE ) );
    }
    REQUIRE( MapEntityEdit_FindEntity( &map, ids[1] ) ); bool changed{}; value_t value( "true" );
    CHECK( MapEntityEdit_SetProperty( &map, { ids, 2 }, SV( "enabled" ), value.Get(), &changed ) == map_status_t::LIMIT_EXCEEDED ); CHECK_FALSE( changed );
    CHECK( Property( map, ids[0], "enabled" ) == nullptr ); CHECK( KeyValue_ChildCount( full ) == MAP_ENTITY_RECORD_MEMBERS_MAX );
    CHECK( MapEntityEdit_SetIdentityField( &map, { ids, 2 }, SV( "name" ), SV( "shared" ), &changed ) == map_status_t::LIMIT_EXCEEDED ); CHECK_FALSE( changed );
    CHECK( KeyValue_Find( MapDocument_FindObject( &map, ids[0], nullptr ), SV( "name" ) ) == nullptr );
    REQUIRE( MapEntityEdit_SetIdentityField( &map, { ids, 2 }, SV( "class" ), SV( "light_spot" ), &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( MapEntityEdit_FindEntity( &map, ids[1] ) );
}

TEST_CASE( "Validated entity lookup returns its chunk only on successful read-only inspection", "[map][entity-edit][lookup][ownership]" )
{
    map_document_t map; Create( map ); const u64 id = AddEntity( map );
    map_chunk_t *expected{}; auto *entity = MapDocument_FindObject( &map, id, &expected ); REQUIRE( entity ); REQUIRE( expected );
    map.bReadOnly = CY_TRUE;
    map_chunk_t *chunk{}; CHECK( MapEntityEdit_FindEntity( &map, id, &chunk ) == entity ); CHECK( chunk == expected );
    CHECK( MapEntityEdit_FindEntity( &map, id + 1u, &chunk ) == nullptr ); CHECK( chunk == nullptr );
    chunk = expected; CHECK( MapEntityEdit_FindEntity( nullptr, id, &chunk ) == nullptr ); CHECK( chunk == nullptr );
    chunk = expected; CHECK( MapEntityEdit_FindEntity( &map, 0u, &chunk ) == nullptr ); CHECK( chunk == nullptr );
    // An unusable retained properties container must not leak a chunk as if the
    // entity had passed the validated lookup.
    REQUIRE( KeyValue_ObjectInsert( expected->store.pDocument, entity, SV( "properties" ), key_value_type_t::STRING ) );
    chunk = expected; CHECK( MapEntityEdit_FindEntity( &map, id, &chunk ) == nullptr ); CHECK( chunk == nullptr );
}

TEST_CASE( "Property rename validates the resulting aggregate byte budget before reporting success", "[map][entity-edit][limit][rename]" )
{
    map_document_t original; Create( original ); const u64 id = AddEntity( original ); value_t value( "\"placeholder\"" ); bool changed{};
    const usize nameBytes = SV( "properties" ).cchLength + SV( "a" ).cchLength;
    const std::string payload( MAP_ENTITY_PROPERTY_VALUE_BYTES_MAX - nameBytes, 'q' );
    REQUIRE( KeyValue_SetString( value.document.get(), value.Get(), SV( payload ) ) );
    REQUIRE( MapEntityEdit_SetProperty( &original, { &id, 1 }, SV( "a" ), value.Get(), &changed ) == map_status_t::OK );
    REQUIRE( MapEntityEdit_FindEntity( &original, id ) );
    map_document_t *raw{}; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK );
    {
        std::unique_ptr<map_document_t> working( raw ); const std::string longer( MAP_ENTITY_PROPERTY_KEY_MAX, 'n' );
        CHECK( MapEntityEdit_RenameProperty( working.get(), { &id, 1 }, SV( "a" ), SV( longer ), &changed ) == map_status_t::LIMIT_EXCEEDED ); CHECK_FALSE( changed );
    }
    CHECK( Property( original, id, "a" ) != nullptr ); CHECK( MapEntityEdit_FindEntity( &original, id ) );
    REQUIRE( MapEntityEdit_RenameProperty( &original, { &id, 1 }, SV( "a" ), SV( "b" ), &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( MapEntityEdit_FindEntity( &original, id ) );
}

TEST_CASE( "Borrowed property keys and identity strings survive a multi-owner batch", "[map][entity-edit][aliasing][batch]" )
{
    map_document_t map; Create( map ); const u64 ids[]{ AddEntity( map ), AddEntity( map ) }; bool changed{};
    value_t value( "{ key = \"settings\" nested = { enabled = true } }" );
    REQUIRE( MapEntityEdit_SetProperty( &map, { ids, 2 }, SV( "settings" ), value.Get(), &changed ) == map_status_t::OK );
    const auto *first = Property( map, ids[0], "settings" ); string_view_t borrowedKey{};
    REQUIRE( KeyValue_GetString( KeyValue_Find( first, SV( "key" ) ), &borrowedKey ) );
    const auto *borrowedValue = KeyValue_Find( first, SV( "nested" ) ); REQUIRE( borrowedValue );
    REQUIRE( MapEntityEdit_SetProperty( &map, { ids, 2 }, borrowedKey, borrowedValue, &changed ) == map_status_t::OK ); CHECK( changed );
    value_t expected( "{ enabled = true }" );
    CHECK( MapEntityEdit_ValuesEqual( Property( map, ids[0], "settings" ), expected.Get() ) ); CHECK( MapEntityEdit_ValuesEqual( Property( map, ids[1], "settings" ), expected.Get() ) );
    const auto borrowedOldKey = KeyValue_Name( Property( map, ids[0], "settings" ) );
    REQUIRE( MapEntityEdit_RenameProperty( &map, { ids, 2 }, borrowedOldKey, SV( "options" ), &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( Property( map, ids[0], "settings" ) == nullptr ); CHECK( Property( map, ids[1], "settings" ) == nullptr );
    REQUIRE( MapEntityEdit_SetIdentityField( &map, { ids, 2 }, SV( "name" ), SV( "prefix_name" ), &changed ) == map_status_t::OK );
    string_view_t borrowedName{}; REQUIRE( KeyValue_GetString( KeyValue_Find( MapEntityEdit_FindEntity( &map, ids[0] ), SV( "name" ) ), &borrowedName ) );
    const string_view_t suffix{ borrowedName.pData + 7, borrowedName.cchLength - 7 };
    REQUIRE( MapEntityEdit_SetIdentityField( &map, { ids, 2 }, SV( "name" ), suffix, &changed ) == map_status_t::OK ); CHECK( changed );
    for ( const auto id : ids ) {
        string_view_t name{}; REQUIRE( KeyValue_GetString( KeyValue_Find( MapEntityEdit_FindEntity( &map, id ), SV( "name" ) ), &name ) ); CHECK( StringView_Equals( name, SV( "name" ) ) );
    }
}
