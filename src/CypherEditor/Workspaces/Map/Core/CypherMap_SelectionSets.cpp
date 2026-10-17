//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_SelectionSets.cpp
//  Purpose: Implements selection set edits on the map root document.
//  Details: A set is `{ id name members }` in the root's `selection_sets`
//           array (CYMAP.md 4). Members are kept sorted without repeats,
//           as the save writes them; a member that no longer exists stays
//           in the set (Hammer keeps it; selecting skips it).
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_SelectionSets.h"

#include <algorithm>

namespace cypher::editor::map
{
using namespace common;

namespace
{
string_view_t SV( const char *text ) noexcept { return StringView_FromCString( text ); }

bool ReadId( const key_value_t *value, u64 &out ) noexcept
{
    i64 signedValue = 0;
    if ( KeyValue_GetU64( value, &out ) ) { return out != 0u; }
    if ( KeyValue_GetI64( value, &signedValue ) && signedValue > 0 ) { out = static_cast<u64>( signedValue ); return true; }
    return false;
}

key_value_t *SetsArray( const map_document_t *map ) noexcept
{
    if ( map == nullptr || map->root.pDocument == nullptr ) { return nullptr; }
    auto *sets = const_cast<key_value_t *>( KeyValue_Find( KeyValue_Root( map->root.pDocument ), SV( "selection_sets" ) ) );
    return KeyValue_Type( sets ) == key_value_type_t::ARRAY ? sets : nullptr;
}

key_value_t *FindSet( const map_document_t *map, u64 id ) noexcept
{
    key_value_t *sets = SetsArray( map );
    for ( usize i = 0; id != 0u && i < KeyValue_ChildCount( sets ); ++i ) {
        auto *set = const_cast<key_value_t *>( KeyValue_ChildAt( sets, i ) );
        u64 setId = 0;
        if ( ReadId( KeyValue_Find( set, SV( "id" ) ), setId ) && setId == id ) { return set; }
    }
    return nullptr;
}

std::vector<u64> ReadMembers( const key_value_t *set )
{
    std::vector<u64> members;
    const key_value_t *array = KeyValue_Find( set, SV( "members" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( array ); ++i ) {
        u64 id = 0;
        if ( ReadId( KeyValue_ChildAt( array, i ), id ) ) { members.push_back( id ); }
    }
    return members;
}

bool WriteMembers( key_value_document_t *document, key_value_t *set, std::vector<u64> members ) noexcept
{
    std::sort( members.begin(), members.end() );
    members.erase( std::unique( members.begin(), members.end() ), members.end() );
    if ( auto *old = const_cast<key_value_t *>( KeyValue_Find( set, SV( "members" ) ) ); old != nullptr && !KeyValue_Remove( document, set, old ) ) { return false; }
    key_value_t *array = KeyValue_ObjectInsert( document, set, SV( "members" ), key_value_type_t::ARRAY );
    if ( array == nullptr ) { return false; }
    for ( u64 id : members ) {
        key_value_t *item = KeyValue_ArrayAppend( document, array, key_value_type_t::U64 );
        if ( item == nullptr || !KeyValue_SetU64( document, item, id ) ) { return false; }
    }
    return true;
}

bool WriteName( key_value_document_t *document, key_value_t *set, string_view_t name ) noexcept
{
    if ( auto *old = const_cast<key_value_t *>( KeyValue_Find( set, SV( "name" ) ) ); old != nullptr && !KeyValue_Remove( document, set, old ) ) { return false; }
    key_value_t *value = KeyValue_ObjectInsert( document, set, SV( "name" ), key_value_type_t::STRING );
    return value != nullptr && KeyValue_SetString( document, value, name );
}

bool ValidName( string_view_t name ) noexcept { return StringView_IsValid( name ) && name.cchLength != 0u && name.cchLength <= MAP_NAME_MAX_LENGTH; }

map_status_t Writable( const map_document_t *map ) noexcept
{
    if ( map == nullptr || map->root.pDocument == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    return map->bReadOnly ? map_status_t::READ_ONLY : map_status_t::OK;
}
} // namespace

std::vector<map_selection_set_t> MapSelectionSets_All( const map_document_t *map )
{
    std::vector<map_selection_set_t> all;
    const key_value_t *sets = SetsArray( map );
    for ( usize i = 0; i < KeyValue_ChildCount( sets ); ++i ) {
        const key_value_t *set = KeyValue_ChildAt( sets, i );
        map_selection_set_t out{};
        if ( !ReadId( KeyValue_Find( set, SV( "id" ) ), out.id ) ) { continue; }
        string_view_t name{};
        if ( KeyValue_GetString( KeyValue_Find( set, SV( "name" ) ), &name ) ) { out.name.assign( name.pData, name.cchLength ); }
        out.members = ReadMembers( set );
        all.push_back( std::move( out ) );
    }
    return all;
}

map_status_t MapSelectionSets_New( map_document_t *map, string_view_t name, span_t<const u64> members, u64 *pIdOut ) noexcept
{
    if ( pIdOut == nullptr || !ValidName( name ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( const auto status = Writable( map ); status != map_status_t::OK ) { return status; }
    *pIdOut = 0u;
    key_value_document_t *document = map->root.pDocument;
    key_value_t *root = KeyValue_Root( document );
    key_value_t *sets = SetsArray( map );
    if ( sets == nullptr ) {
        if ( KeyValue_Find( root, SV( "selection_sets" ) ) != nullptr ) { return map_status_t::INVALID_ARGUMENT; }
        sets = KeyValue_ObjectInsert( document, root, SV( "selection_sets" ), key_value_type_t::ARRAY );
        if ( sets == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( KeyValue_ChildCount( sets ) >= MAP_SELECTION_SETS_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    const u64 id = MapDocument_AllocateId( map );
    key_value_t *set = KeyValue_ArrayAppend( document, sets, key_value_type_t::OBJECT );
    key_value_t *idValue = set != nullptr ? KeyValue_ObjectInsert( document, set, SV( "id" ), key_value_type_t::U64 ) : nullptr;
    if ( id == 0u || idValue == nullptr || !KeyValue_SetU64( document, idValue, id ) || !WriteName( document, set, name ) ) { return map_status_t::OUT_OF_MEMORY; }
    try {
        if ( !WriteMembers( document, set, std::vector<u64>( members.pData, members.pData + members.nCount ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
    *pIdOut = id;
    return map_status_t::OK;
}

map_status_t MapSelectionSets_Edit( map_document_t *map, u64 id, span_t<const u64> add, span_t<const u64> remove, bool *pChangedOut ) noexcept
{
    if ( pChangedOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( const auto status = Writable( map ); status != map_status_t::OK ) { return status; }
    key_value_t *set = FindSet( map, id );
    if ( set == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    try {
        auto members = ReadMembers( set );
        std::sort( members.begin(), members.end() );
        members.erase( std::unique( members.begin(), members.end() ), members.end() );
        auto edited = members;
        edited.insert( edited.end(), add.pData, add.pData + add.nCount );
        edited.erase( std::remove_if( edited.begin(), edited.end(), [&]( u64 member ) {
            return std::find( remove.pData, remove.pData + remove.nCount, member ) != remove.pData + remove.nCount;
        } ), edited.end() );
        std::sort( edited.begin(), edited.end() );
        edited.erase( std::unique( edited.begin(), edited.end() ), edited.end() );
        if ( edited == members ) { return map_status_t::OK; }
        if ( !WriteMembers( map->root.pDocument, set, std::move( edited ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
    *pChangedOut = true;
    return map_status_t::OK;
}

map_status_t MapSelectionSets_Rename( map_document_t *map, u64 id, string_view_t name, bool *pChangedOut ) noexcept
{
    if ( pChangedOut == nullptr || !ValidName( name ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( const auto status = Writable( map ); status != map_status_t::OK ) { return status; }
    key_value_t *set = FindSet( map, id );
    if ( set == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    string_view_t old{};
    if ( KeyValue_GetString( KeyValue_Find( set, SV( "name" ) ), &old ) && StringView_Equals( old, name ) ) { return map_status_t::OK; }
    if ( !WriteName( map->root.pDocument, set, name ) ) { return map_status_t::OUT_OF_MEMORY; }
    *pChangedOut = true;
    return map_status_t::OK;
}

map_status_t MapSelectionSets_Delete( map_document_t *map, u64 id ) noexcept
{
    if ( const auto status = Writable( map ); status != map_status_t::OK ) { return status; }
    key_value_t *set = FindSet( map, id );
    if ( set == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    return KeyValue_Remove( map->root.pDocument, SetsArray( map ), set ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

} // namespace cypher::editor::map
