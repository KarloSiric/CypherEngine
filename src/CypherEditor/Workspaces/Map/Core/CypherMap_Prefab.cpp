//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Prefab.cpp
//  Purpose: Implements prefab files over the map clipboard's validated
//           object serialization.
//  Details: Writing translates a private copy of the map so the prefab's
//           origin is the world origin, serializes the selection through the
//           clipboard (canonical records, every identity present) and regroups
//           it into the prefab's layer-like sections. Inserting regroups the
//           sections back into clipboard objects and pastes them, which remaps
//           every identity and places the copy.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Prefab.h"
#include "CypherMap_Clipboard.h"
#include "CypherMap_Edit.h"

#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"
#include "CypherCommon/Tier1/CypherCommon_UniqueId.h"

#include <algorithm>
#include <memory>

namespace cypher::editor::map
{
using namespace common;
namespace
{
string_view_t SV( const char *p ) noexcept { return StringView_FromCString( p ); }

struct document_t {
    key_value_document_t *pDocument{ nullptr };
    ~document_t() { KeyValue_DestroyDocument( pDocument ); }
    bool Create( const allocator_t *allocator ) noexcept
    {
        key_value_document_desc_t desc{}; desc.pAllocator = allocator;
        pDocument = KeyValue_CreateDocument( desc );
        return pDocument != nullptr && KeyValue_SetRootType( pDocument, key_value_type_t::OBJECT );
    }
    key_value_t *Root() const noexcept { return KeyValue_Root( pDocument ); }
};

bool Parse( string_view_t text, document_t &out, const char *schema, u32 version ) noexcept
{
    key_value_parse_options_t options{};
    options.cbMaxInput = options.cbMaxStringData = MAP_CLIPBOARD_TEXT_MAX; options.nMaxNodes = MAP_CLIPBOARD_VALUES_MAX;
    if ( KeyValue_ParseText( text, options, out.pDocument ).status != key_value_parse_status_t::OK ) { return false; }
    const auto header = KeyValue_DocumentHeader( out.pDocument );
    return header.nLanguageVersion == CYKV_LANGUAGE_VERSION_1 && StringView_Equals( header.schemaId, SV( schema ) ) && header.nSchemaVersion == version;
}

map_status_t Write( key_value_document_t *document, text_buffer_t *out ) noexcept
{
    key_value_write_options_t options{}; options.flags |= KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS; options.nLineWidth = MAP_LINE_WIDTH;
    const auto measured = KeyValue_WriteText( KeyValue_Root( document ), options, nullptr, 0 );
    if ( measured.status == key_value_write_status_t::OUT_OF_MEMORY ) { return map_status_t::OUT_OF_MEMORY; }
    if ( measured.status != key_value_write_status_t::OUTPUT_TRUNCATED && measured.status != key_value_write_status_t::OK ) { return map_status_t::ROOT_INVALID; }
    if ( measured.cchRequired > MAP_CLIPBOARD_TEXT_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    if ( !TextBuffer_Resize( out, measured.cchRequired ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto written = KeyValue_WriteText( KeyValue_Root( document ), options, TextBuffer_Data( out ), TextBuffer_Capacity( out ) + 1 );
    return written.status == key_value_write_status_t::OK ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

bool ReadVec( const key_value_t *node, math::vec3d_t &out ) noexcept
{
    return KeyValue_ChildCount( node ) == 3 && KeyValue_GetF64( KeyValue_ChildAt( node, 0 ), &out.x ) &&
        KeyValue_GetF64( KeyValue_ChildAt( node, 1 ), &out.y ) && KeyValue_GetF64( KeyValue_ChildAt( node, 2 ), &out.z );
}

bool SetU64( key_value_document_t *d, key_value_t *object, const char *name, u64 value ) noexcept
{
    auto *node = KeyValue_ObjectInsert( d, object, SV( name ), key_value_type_t::U64 );
    return node != nullptr && KeyValue_SetU64( d, node, value );
}
bool SetText( key_value_document_t *d, key_value_t *object, const char *name, string_view_t value ) noexcept
{
    auto *node = KeyValue_ObjectInsert( d, object, SV( name ), key_value_type_t::STRING );
    return node != nullptr && KeyValue_SetString( d, node, value );
}
key_value_t *Section( key_value_document_t *d, key_value_t *object, string_view_t name ) noexcept
{
    if ( auto *found = KeyValue_Find( object, name ) ) { return found; }
    return KeyValue_ObjectInsert( d, object, name, key_value_type_t::ARRAY );
}
u64 RecordId( const key_value_t *record ) noexcept { u64 id = 0; return KeyValue_GetU64( KeyValue_Find( record, SV( "id" ) ), &id ) ? id : 0; }

constexpr const char *kGeometrySections[]{ "brushes", "meshes", "patches", "terrains" };
} // namespace

map_status_t MapPrefab_Write( const map_document_t *map, span_t<const u64> ids, string_view_t name, text_buffer_t *pTextOut, math::vec3d_t *pOriginOut ) noexcept
{
    if ( map == nullptr || !Span_IsValid( ids ) || ids.nCount == 0 || pTextOut == nullptr || !TextBuffer_IsValid( pTextOut ) ||
         !StringView_IsValid( name ) || name.cchLength == 0 || name.cchLength > 128 ) { return map_status_t::INVALID_ARGUMENT; }
    // The selection's bounds, from a first serialization: the origin is
    // their bottom centre.
    text_buffer_t text{};
    if ( !TextBuffer_Init( &text, map->pAllocator, 0 ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto result = MapClipboard_WriteSelection( map, ids, &text );
    map_bounds_t bounds{};
    if ( result == map_status_t::OK ) { result = MapClipboard_ReadBounds( map->pAllocator, TextBuffer_View( &text ), &bounds ); }
    TextBuffer_Shutdown( &text );
    if ( result != map_status_t::OK ) { return result; }
    const math::vec3d_t origin{ ( bounds.box.minimum.x + bounds.box.maximum.x ) * 0.5, ( bounds.box.minimum.y + bounds.box.maximum.y ) * 0.5, bounds.box.minimum.z };
    if ( pOriginOut != nullptr ) { *pOriginOut = origin; }
    return MapPrefab_WriteAt( map, ids, name, origin, pTextOut );
}

map_status_t MapPrefab_WriteAt( const map_document_t *map, span_t<const u64> ids, string_view_t name, math::vec3d_t origin, text_buffer_t *pTextOut ) noexcept
{
    if ( map == nullptr || !Span_IsValid( ids ) || ids.nCount == 0 || pTextOut == nullptr || !TextBuffer_IsValid( pTextOut ) ||
         !StringView_IsValid( name ) || name.cchLength == 0 || name.cchLength > 128 ) { return map_status_t::INVALID_ARGUMENT; }
    text_buffer_t text{};
    if ( !TextBuffer_Init( &text, map->pAllocator, 0 ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto result = map_status_t::OK;
    // From a copy moved so the origin is at zero.
    map_document_t *copy = nullptr;
    if ( result == map_status_t::OK ) { result = MapEdit_Clone( map, &copy ); }
    if ( result == map_status_t::OK && ( origin.x != 0.0 || origin.y != 0.0 || origin.z != 0.0 ) ) {
        result = MapEdit_Translate( copy, ids, { -origin.x, -origin.y, -origin.z }, true );
    }
    if ( result == map_status_t::OK ) { result = MapClipboard_WriteSelection( copy, ids, &text ); }
    delete copy;
    document_t clipboard, prefab;
    if ( result == map_status_t::OK && ( !clipboard.Create( map->pAllocator ) || !prefab.Create( map->pAllocator ) ) ) { result = map_status_t::OUT_OF_MEMORY; }
    if ( result == map_status_t::OK && !Parse( TextBuffer_View( &text ), clipboard, MAP_CLIPBOARD_SCHEMA_ID, MAP_CLIPBOARD_SCHEMA_VERSION ) ) { result = map_status_t::ROOT_INVALID; }
    TextBuffer_Shutdown( &text );
    if ( result != map_status_t::OK ) { return result; }
    // Header and identity (6.12 members).
    auto *d = prefab.pDocument; auto *root = prefab.Root();
    unique_id_t uuid{}; char uuidText[CY_UNIQUE_ID_STRING_CAPACITY]{};
    if ( !KeyValue_SetDocumentHeader( d, { CYKV_LANGUAGE_VERSION_1, SV( MAP_PREFAB_SCHEMA_ID ), MAP_PREFAB_SCHEMA_VERSION } ) ||
         !UniqueId_CreateRandom( &uuid ) || UniqueId_ToString( uuid, uuidText, sizeof( uuidText ) ) == 0 ||
         !SetText( d, root, "prefab_id", SV( uuidText ) ) || !SetText( d, root, "name", name ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto *rootRecord = SettingsDocument_Root( &map->root );
    string_view_t game{};
    ( void )KeyValue_GetString( KeyValue_Find( rootRecord, SV( "game" ) ), &game );
    if ( !SetText( d, root, "game", game.cchLength != 0 ? game : SV( "reap" ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *next = KeyValue_ObjectInsert( d, root, SV( "next_id" ), key_value_type_t::U64 );
    if ( next == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    // Sections: entities first so owned geometry can nest in them.
    const auto *objects = KeyValue_Find( clipboard.Root(), SV( "objects" ) );
    u64 highest = 0;
    for ( int pass = 0; pass < 2; ++pass ) {
        for ( usize i = 0; i < KeyValue_ChildCount( objects ); ++i ) {
            const auto *item = KeyValue_ChildAt( objects, i );
            const auto *record = KeyValue_Find( item, SV( "record" ) );
            string_view_t kind{}; u64 owner = 0;
            if ( !KeyValue_GetString( KeyValue_Find( item, SV( "kind" ) ), &kind ) || !KeyValue_GetU64( KeyValue_Find( item, SV( "owner" ) ), &owner ) || record == nullptr ) {
                return map_status_t::ROOT_INVALID;
            }
            const bool entity = StringView_Equals( kind, SV( "entity" ) );
            if ( ( pass == 0 ) != entity ) { continue; }
            highest = std::max( highest, RecordId( record ) );
            key_value_t *target = nullptr;
            if ( entity ) { target = Section( d, root, SV( "entities" ) ); }
            else if ( owner == 0 ) { target = Section( d, root, kind ); }
            else {
                auto *entities = KeyValue_Find( root, SV( "entities" ) );
                for ( usize e = 0; e < KeyValue_ChildCount( entities ) && target == nullptr; ++e ) {
                    auto *candidate = KeyValue_ChildAt( entities, e );
                    if ( RecordId( candidate ) == owner ) { target = Section( d, candidate, kind ); }
                }
            }
            if ( target == nullptr || KeyValue_CloneInto( d, target, record ) == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
        }
    }
    if ( !KeyValue_SetU64( d, next, highest + 1 ) ) { return map_status_t::OUT_OF_MEMORY; }
    return Write( d, pTextOut );
}

map_status_t MapPrefab_Insert( map_document_t *map, string_view_t text, math::vec3d_t position, vector_t<u64> *pIdsOut ) noexcept
{
    if ( map == nullptr || pIdsOut == nullptr || !Vector_IsValid( pIdsOut ) || !StringView_IsValid( text ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    document_t prefab, clipboard;
    if ( !prefab.Create( map->pAllocator ) || !clipboard.Create( map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    if ( !Parse( text, prefab, MAP_PREFAB_SCHEMA_ID, MAP_PREFAB_SCHEMA_VERSION ) ) { return map_status_t::ROOT_UNREADABLE; }
    auto *d = clipboard.pDocument; auto *root = clipboard.Root();
    if ( !KeyValue_SetDocumentHeader( d, { CYKV_LANGUAGE_VERSION_1, SV( MAP_CLIPBOARD_SCHEMA_ID ), MAP_CLIPBOARD_SCHEMA_VERSION } ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *selection = KeyValue_ObjectInsert( d, root, SV( "selection" ), key_value_type_t::ARRAY );
    auto *bounds = KeyValue_ObjectInsert( d, root, SV( "bounds" ), key_value_type_t::ARRAY );
    auto *objects = KeyValue_ObjectInsert( d, root, SV( "objects" ), key_value_type_t::ARRAY );
    if ( selection == nullptr || bounds == nullptr || objects == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    // The paste places by `position`; the bounds here are informational.
    for ( int corner = 0; corner < 2; ++corner ) {
        auto *point = KeyValue_ArrayAppend( d, bounds, key_value_type_t::ARRAY );
        for ( int axis = 0; point != nullptr && axis < 3; ++axis ) {
            auto *value = KeyValue_ArrayAppend( d, point, key_value_type_t::F64 );
            if ( value == nullptr || !KeyValue_SetF64( d, value, 0.0 ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        if ( point == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    }
    const string_view_t layer = map->layers.nCount != 0 ? SV( map->layers.pData[0].id ) : SV( "default" );
    const auto add = [&]( const key_value_t *record, const char *kind, u64 owner, bool top, bool strip ) noexcept {
        auto *item = KeyValue_ArrayAppend( d, objects, key_value_type_t::OBJECT );
        if ( item == nullptr || !SetText( d, item, "kind", SV( kind ) ) || !SetText( d, item, "layer", layer ) || !SetU64( d, item, "owner", owner ) ) { return false; }
        auto *copy = KeyValue_ObjectInsert( d, item, SV( "record" ), key_value_type_t::OBJECT );
        if ( copy == nullptr ) { return false; }
        for ( usize i = 0; i < KeyValue_ChildCount( record ); ++i ) {
            const auto *child = KeyValue_ChildAt( record, i );
            bool nested = false;
            for ( const char *section : kGeometrySections ) { nested |= strip && StringView_Equals( KeyValue_Name( child ), SV( section ) ); }
            if ( !nested && KeyValue_CloneInto( d, copy, child ) == nullptr ) { return false; }
        }
        if ( !top ) { return true; }
        auto *id = KeyValue_ArrayAppend( d, selection, key_value_type_t::U64 );
        return id != nullptr && KeyValue_SetU64( d, id, RecordId( record ) );
    };
    const auto *entities = KeyValue_Find( prefab.Root(), SV( "entities" ) );
    for ( usize e = 0; e < KeyValue_ChildCount( entities ); ++e ) {
        const auto *entity = KeyValue_ChildAt( entities, e );
        if ( !add( entity, "entity", 0, true, true ) ) { return map_status_t::OUT_OF_MEMORY; }
        for ( const char *section : kGeometrySections ) {
            const auto *owned = KeyValue_Find( entity, SV( section ) );
            for ( usize k = 0; k < KeyValue_ChildCount( owned ); ++k ) {
                if ( !add( KeyValue_ChildAt( owned, k ), section, RecordId( entity ), false, false ) ) { return map_status_t::OUT_OF_MEMORY; }
            }
        }
    }
    for ( const char *section : kGeometrySections ) {
        const auto *world = KeyValue_Find( prefab.Root(), SV( section ) );
        for ( usize k = 0; k < KeyValue_ChildCount( world ); ++k ) {
            if ( !add( KeyValue_ChildAt( world, k ), section, 0, true, false ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
    }
    if ( KeyValue_ChildCount( selection ) == 0 ) { return map_status_t::ROOT_INVALID; } // An empty prefab.
    text_buffer_t staged{};
    if ( !TextBuffer_Init( &staged, map->pAllocator, 0 ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto result = Write( d, &staged );
    if ( result == map_status_t::OK ) { result = MapClipboard_Paste( map, TextBuffer_View( &staged ), position, pIdsOut ); }
    TextBuffer_Shutdown( &staged );
    return result;
}

} // namespace cypher::editor::map
