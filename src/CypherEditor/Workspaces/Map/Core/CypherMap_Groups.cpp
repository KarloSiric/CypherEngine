//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Groups.cpp
//  Purpose: Implements group names, membership edits, pruning and copying.
//  Details: Group records live in the `groups` array of a layer (CYMAP.md
//           6.10); layer stores have no dirty state, so editing a record in
//           place is all a save needs.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Groups.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_HeightField.h"
#include "CypherGeometry_Patch.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace cypher::editor::map
{
using namespace common;

namespace
{
string_view_t SV( const char *text ) noexcept { return StringView_FromCString( text ); }

// IDs are u64; a hand-typed positive signed value is accepted (as the
// document reads them).
bool ReadId( const key_value_t *value, u64 &out ) noexcept
{
    i64 signedValue = 0;
    if ( KeyValue_GetU64( value, &out ) ) { return out != 0u; }
    if ( KeyValue_GetI64( value, &signedValue ) && signedValue > 0 ) { out = static_cast<u64>( signedValue ); return true; }
    return false;
}

struct group_t {
    map_layer_store_t *pStore{ nullptr };
    key_value_t *pArray{ nullptr };
    key_value_t *pRecord{ nullptr };
    u64 id{ 0u };
};

// Every group record; `visit` returns false to stop.
template <typename Visit>
void EachGroup( const map_document_t *map, Visit visit )
{
    for ( usize c = 0; map != nullptr && c < map->layerStores.nCount; ++c ) {
        map_layer_store_t *layerStore = map->layerStores.pData[c];
        if ( layerStore->store.pDocument == nullptr ) { continue; }
        key_value_t *groups = const_cast<key_value_t *>( KeyValue_Find( KeyValue_Root( layerStore->store.pDocument ), SV( "groups" ) ) );
        for ( usize i = 0; i < KeyValue_ChildCount( groups ); ++i ) {
            key_value_t *record = const_cast<key_value_t *>( KeyValue_ChildAt( groups, i ) );
            u64 id = 0;
            if ( !ReadId( KeyValue_Find( record, SV( "id" ) ), id ) ) { continue; }
            if ( !visit( group_t{ layerStore, groups, record, id } ) ) { return; }
        }
    }
}

group_t FindGroup( const map_document_t *map, u64 id ) noexcept
{
    group_t found{};
    EachGroup( map, [&]( const group_t &g ) { if ( g.id != id ) { return true; } found = g; return false; } );
    return found;
}

group_t DirectGroup( const map_document_t *map, u64 id ) noexcept
{
    group_t found{};
    if ( id == 0 ) { return found; }
    EachGroup( map, [&]( const group_t &g ) noexcept {
        for ( const auto *at = KeyValue_FirstChild( KeyValue_Find( g.pRecord, SV( "members" ) ) ); at; at = KeyValue_NextSibling( at ) ) {
            u64 member = 0;
            if ( ReadId( at, member ) && member == id ) { found = g; return false; }
        }
        return true;
    } );
    return found;
}

bool RootExists( map_document_t *map, u64 id ) noexcept
{
    return MapDocument_FindObject( map, id, nullptr ) != nullptr ||
        geometry::GeometryDocument_FindBrush( &map->geometry, { id } ) != nullptr ||
        geometry::GeometryDocument_FindMesh( &map->geometry, { id } ) != nullptr ||
        geometry::GeometryDocument_FindPatch( &map->geometry, { id } ) != nullptr ||
        geometry::GeometryDocument_FindHeightField( &map->geometry, { id } ) != nullptr;
}

std::vector<u64> ReadMembers( const key_value_t *record )
{
    std::vector<u64> members;
    const key_value_t *array = KeyValue_Find( record, SV( "members" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( array ); ++i ) {
        u64 id = 0;
        if ( ReadId( KeyValue_ChildAt( array, i ), id ) ) { members.push_back( id ); }
    }
    return members;
}

// Replaces the record's members, sorted (CYMAP.md 6.10).
bool WriteMembers( const group_t &group, std::vector<u64> members ) noexcept
{
    std::sort( members.begin(), members.end() );
    members.erase( std::unique( members.begin(), members.end() ), members.end() );
    key_value_document_t *document = group.pStore->store.pDocument;
    if ( key_value_t *old = const_cast<key_value_t *>( KeyValue_Find( group.pRecord, SV( "members" ) ) ); old != nullptr && !KeyValue_Remove( document, group.pRecord, old ) ) { return false; }
    key_value_t *array = KeyValue_ObjectInsert( document, group.pRecord, SV( "members" ), key_value_type_t::ARRAY );
    if ( array == nullptr ) { return false; }
    for ( u64 id : members ) {
        key_value_t *item = KeyValue_ArrayAppend( document, array, key_value_type_t::U64 );
        if ( item == nullptr || !KeyValue_SetU64( document, item, id ) ) { return false; }
    }
    return true;
}

// Removes groups with no members, and then their places in other groups,
// until none is left empty.
map_status_t Dissolve( map_document_t *map, bool &changed )
{
    for ( u32 round = 0; round < 64; ++round ) {
        std::vector<group_t> empty;
        EachGroup( map, [&]( const group_t &g ) { if ( ReadMembers( g.pRecord ).empty() ) { empty.push_back( g ); } return true; } );
        if ( empty.empty() ) { return map_status_t::OK; }
        std::unordered_set<u64> gone;
        for ( const auto &g : empty ) { gone.insert( g.id ); }
        for ( const auto &g : empty ) {
            if ( !KeyValue_Remove( g.pStore->store.pDocument, g.pArray, g.pRecord ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        changed = true;
        bool ok = true;
        EachGroup( map, [&]( const group_t &g ) {
            auto members = ReadMembers( g.pRecord );
            const usize before = members.size();
            members.erase( std::remove_if( members.begin(), members.end(), [&]( u64 id ) { return gone.count( id ) != 0; } ), members.end() );
            if ( members.size() != before ) { ok = WriteMembers( g, members ); }
            return ok;
        } );
        if ( !ok ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return map_status_t::OK;
}

// Every ID a member may name: records in the layer stores (entity-owned geometry
// included), geometry a tool made that has no record yet, and the groups.
std::unordered_set<u64> Existing( const map_document_t *map )
{
    std::unordered_set<u64> ids;
    const auto collect = [&]( const key_value_t *array, auto &self, u32 depth ) -> void {
        for ( usize i = 0; i < KeyValue_ChildCount( array ); ++i ) {
            const key_value_t *record = KeyValue_ChildAt( array, i );
            u64 id = 0;
            if ( KeyValue_Type( record ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( record, SV( "id" ) ), id ) ) { continue; }
            ids.insert( id );
            if ( depth > 0 ) { continue; }
            for ( const char *owned : { "brushes", "meshes", "patches", "terrains" } ) { self( KeyValue_Find( record, SV( owned ) ), self, depth + 1 ); }
        }
    };
    for ( usize c = 0; c < map->layerStores.nCount; ++c ) {
        const map_layer_store_t *layerStore = map->layerStores.pData[c];
        if ( layerStore->store.pDocument == nullptr ) { continue; }
        const key_value_t *root = KeyValue_Root( layerStore->store.pDocument );
        for ( usize s = 0; s < KeyValue_ChildCount( root ); ++s ) {
            const key_value_t *section = KeyValue_ChildAt( root, s );
            if ( KeyValue_Type( section ) == key_value_type_t::ARRAY ) { collect( section, collect, 0 ); }
        }
    }
    const auto &g = map->geometry;
    for ( usize i = 0; i < g.brushes.nCount; ++i ) { ids.insert( g.brushes.pData[i]->sourceId.value ); }
    for ( usize i = 0; i < g.meshes.nCount; ++i ) { ids.insert( g.meshes.pData[i]->sourceId.value ); }
    for ( usize i = 0; i < g.patches.nCount; ++i ) { ids.insert( g.patches.pData[i]->sourceId.value ); }
    for ( usize i = 0; i < g.heightFields.nCount; ++i ) { ids.insert( g.heightFields.pData[i]->sourceId.value ); }
    return ids;
}

map_status_t Writable( const map_document_t *map ) noexcept
{
    if ( map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    return map->bReadOnly ? map_status_t::READ_ONLY : map_status_t::OK;
}
} // namespace

std::vector<u64> MapGroups_All( const map_document_t *map )
{
    std::vector<u64> all;
    EachGroup( map, [&]( const group_t &g ) { all.push_back( g.id ); return true; } );
    return all;
}

u64 MapGroups_Parent( const map_document_t *map, u64 id ) noexcept
{
    return DirectGroup( map, id ).id;
}

map_status_t MapGroups_ReplaceMember( map_document_t *map, u64 original, span_t<const u64> replacements ) noexcept
{
    if ( map == nullptr || !Allocator_IsValid( map->pAllocator ) || original == 0 ||
         !Span_IsValid( replacements ) || replacements.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    const auto group = DirectGroup( map, original );
    if ( group.id == 0 ) { return map_status_t::OK; }
    const auto *before = KeyValue_Find( group.pRecord, SV( "members" ) );
    struct member_t { u64 id; const key_value_t *source; };
    vector_t<member_t> members{};
    const key_value_t *replaced = nullptr;
    if ( !Vector_Init( &members, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( const auto *at = KeyValue_FirstChild( before ); at; at = KeyValue_NextSibling( at ) ) {
        u64 id = 0;
        if ( !ReadId( at, id ) ) { return map_status_t::INVALID_ARGUMENT; }
        if ( id == original ) { replaced = at; }
        else if ( !Vector_PushBack( &members, member_t{ id, at } ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    u64 firstReplacement = CY_U64_MAX;
    for ( usize i = 0; i < replacements.nCount; ++i ) { firstReplacement = std::min( firstReplacement, replacements.pData[i] ); }
    for ( usize i = 0; i < replacements.nCount; ++i ) {
        const u64 id = replacements.pData[i];
        const auto parent = DirectGroup( map, id ).id;
        if ( id == 0 || id == original || !RootExists( map, id ) || FindGroup( map, id ).id != 0 || parent != 0 ) {
            return map_status_t::INVALID_ARGUMENT;
        }
        if ( !Vector_PushBack( &members, member_t{ id, id == firstReplacement ? replaced : nullptr } ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    std::sort( members.pData, members.pData + members.nCount, []( const member_t &a, const member_t &b ) noexcept { return a.id < b.id; } );
    key_value_document_desc_t desc{}; desc.pAllocator = map->pAllocator;
    struct prepared_t { key_value_document_t *document{}; ~prepared_t() { KeyValue_DestroyDocument( document ); } };
    prepared_t prepared{ KeyValue_CreateDocument( desc ) };
    if ( !prepared.document || !KeyValue_SetRootType( prepared.document, key_value_type_t::OBJECT ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *array = KeyValue_ObjectInsert( prepared.document, KeyValue_Root( prepared.document ), SV( "members" ), key_value_type_t::ARRAY );
    if ( !array || !KeyValue_SetComments( prepared.document, array, KeyValue_Comments( before ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < members.nCount; ++i ) {
        const auto &member = members.pData[i];
        if ( i != 0 && member.id == members.pData[i - 1].id ) { continue; }
        auto *item = member.source ? KeyValue_CloneInto( prepared.document, array, member.source ) : KeyValue_ArrayAppend( prepared.document, array, key_value_type_t::U64 );
        if ( !item || !KeyValue_SetU64( prepared.document, item, member.id ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    // Only the membership field changes. The surrounding group record,
    // its position in the file, comments, unknown fields and parent stay.
    // Clone appends a field; it does not replace an existing field with the
    // same name. Keep the old array until the whole replacement has copied.
    auto *document = group.pStore->store.pDocument;
    if ( !KeyValue_CloneInto( document, group.pRecord, array ) ) { return map_status_t::OUT_OF_MEMORY; }
    return KeyValue_Remove( document, group.pRecord, const_cast<key_value_t *>( before ) )
        ? map_status_t::OK : map_status_t::GEOMETRY_FAILED;
}

map_status_t MapGroups_Members( const map_document_t *map, u64 group, std::vector<u64> &out )
{
    out.clear();
    const auto g = FindGroup( map, group );
    if ( g.pRecord == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    out = ReadMembers( g.pRecord );
    return map_status_t::OK;
}

std::string MapGroups_Name( const map_document_t *map, u64 group )
{
    const auto g = FindGroup( map, group );
    string_view_t text{};
    if ( g.pRecord == nullptr || !KeyValue_GetString( KeyValue_Find( g.pRecord, SV( "name" ) ), &text ) ) { return {}; }
    return { text.pData, text.cchLength };
}

map_status_t MapGroups_Rename( map_document_t *map, u64 group, string_view_t name, bool *pChanged ) noexcept
{
    if ( pChanged == nullptr || !StringView_IsValid( name ) || name.cchLength > MAP_NAME_MAX_LENGTH ) { return map_status_t::INVALID_ARGUMENT; }
    *pChanged = false;
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    const auto g = FindGroup( map, group );
    if ( g.pRecord == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    key_value_document_t *document = g.pStore->store.pDocument;
    key_value_t *old = const_cast<key_value_t *>( KeyValue_Find( g.pRecord, SV( "name" ) ) );
    string_view_t current{};
    if ( old != nullptr && KeyValue_GetString( old, &current ) ? StringView_Equals( current, name ) : name.cchLength == 0 ) { return map_status_t::OK; }
    if ( old != nullptr && !KeyValue_Remove( document, g.pRecord, old ) ) { return map_status_t::OUT_OF_MEMORY; }
    if ( name.cchLength != 0 ) {
        key_value_t *node = KeyValue_ObjectInsert( document, g.pRecord, SV( "name" ), key_value_type_t::STRING );
        if ( node == nullptr || !KeyValue_SetString( document, node, name ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    *pChanged = true;
    return map_status_t::OK;
}

map_status_t MapGroups_Add( map_document_t *map, u64 group, span_t<const u64> ids, bool *pChanged )
{
    if ( pChanged == nullptr || !Span_IsValid( ids ) ) { return map_status_t::INVALID_ARGUMENT; }
    *pChanged = false;
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( FindGroup( map, group ).pRecord == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    // No cycles: neither the group nor any group that contains it may go in.
    std::unordered_set<u64> above;
    for ( u64 g = group, steps = 0; g != 0 && steps < 64; g = MapGroups_Parent( map, g ), ++steps ) { above.insert( g ); }
    std::vector<u64> moving;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const u64 id = ids.pData[i];
        if ( id == 0 || above.count( id ) != 0 ) { return map_status_t::INVALID_ARGUMENT; }
        if ( MapGroups_Parent( map, id ) != group ) { moving.push_back( id ); }
    }
    if ( moving.empty() ) { return map_status_t::OK; }
    bool ok = true;
    // Out of their old groups first (members of the target excepted).
    EachGroup( map, [&]( const group_t &g ) {
        if ( g.id == group ) { return true; }
        auto members = ReadMembers( g.pRecord );
        const usize before = members.size();
        members.erase( std::remove_if( members.begin(), members.end(), [&]( u64 id ) { return std::find( moving.begin(), moving.end(), id ) != moving.end(); } ), members.end() );
        if ( members.size() != before ) { ok = WriteMembers( g, members ); }
        return ok;
    } );
    const auto target = FindGroup( map, group );
    auto members = ReadMembers( target.pRecord );
    members.insert( members.end(), moving.begin(), moving.end() );
    if ( !ok || !WriteMembers( target, members ) ) { return map_status_t::OUT_OF_MEMORY; }
    *pChanged = true;
    bool dissolved = false;
    return Dissolve( map, dissolved );
}

map_status_t MapGroups_Remove( map_document_t *map, span_t<const u64> ids, bool *pChanged )
{
    if ( pChanged == nullptr || !Span_IsValid( ids ) ) { return map_status_t::INVALID_ARGUMENT; }
    *pChanged = false;
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    std::unordered_set<u64> leaving( ids.pData, ids.pData + ids.nCount );
    bool ok = true;
    EachGroup( map, [&]( const group_t &g ) {
        auto members = ReadMembers( g.pRecord );
        const usize before = members.size();
        members.erase( std::remove_if( members.begin(), members.end(), [&]( u64 id ) { return leaving.count( id ) != 0; } ), members.end() );
        if ( members.size() != before ) { ok = WriteMembers( g, members ); *pChanged = true; }
        return ok;
    } );
    if ( !ok ) { return map_status_t::OUT_OF_MEMORY; }
    return Dissolve( map, *pChanged );
}

map_status_t MapGroups_Prune( map_document_t *map, bool *pChanged )
{
    if ( pChanged == nullptr || map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    *pChanged = false;
    bool any = false;
    EachGroup( map, [&]( const group_t & ) { any = true; return false; } );
    if ( !any || map->bReadOnly ) { return map_status_t::OK; }
    const auto existing = Existing( map );
    bool ok = true;
    EachGroup( map, [&]( const group_t &g ) {
        auto members = ReadMembers( g.pRecord );
        const usize before = members.size();
        members.erase( std::remove_if( members.begin(), members.end(), [&]( u64 id ) { return existing.count( id ) == 0; } ), members.end() );
        if ( members.size() != before ) { ok = WriteMembers( g, members ); *pChanged = true; }
        return ok;
    } );
    if ( !ok ) { return map_status_t::OUT_OF_MEMORY; }
    return Dissolve( map, *pChanged );
}

map_status_t MapGroups_Copied( map_document_t *map, const std::vector<std::pair<u64, u64>> &copies, std::vector<u64> *pNewGroups )
{
    if ( pNewGroups != nullptr ) { pNewGroups->clear(); }
    auto result = Writable( map );
    if ( result != map_status_t::OK || copies.empty() ) { return result; }
    std::unordered_map<u64, u64> copyOf( copies.begin(), copies.end() );
    // The groups all of whose members are copied, innermost first.
    struct record_t { u64 id; std::vector<u64> members; };
    std::vector<record_t> groups;
    EachGroup( map, [&]( const group_t &g ) { groups.push_back( { g.id, ReadMembers( g.pRecord ) } ); return true; } );
    if ( groups.empty() ) { return map_status_t::OK; }
    std::vector<u64> copiedGroups;
    for ( bool grew = true; grew; ) {
        grew = false;
        for ( const auto &g : groups ) {
            if ( copyOf.count( g.id ) != 0 || g.members.empty() ) { continue; }
            if ( !std::all_of( g.members.begin(), g.members.end(), [&]( u64 id ) { return copyOf.count( id ) != 0; } ) ) { continue; }
            copyOf[g.id] = 0; // Placeholder: copied, ID below.
            copiedGroups.push_back( g.id );
            grew = true;
        }
    }
    // The copied groups' records, cloned beside the originals.
    for ( u64 original : copiedGroups ) {
        const auto g = FindGroup( map, original );
        const u64 id = MapDocument_AllocateId( map );
        key_value_document_t *document = g.pStore->store.pDocument;
        key_value_t *clone = id != 0 ? KeyValue_CloneInto( document, g.pArray, g.pRecord ) : nullptr;
        key_value_t *idNode = clone != nullptr ? const_cast<key_value_t *>( KeyValue_Find( clone, SV( "id" ) ) ) : nullptr;
        if ( idNode == nullptr || !KeyValue_SetU64( document, idNode, id ) ) { return id == 0 ? map_status_t::LIMIT_EXCEEDED : map_status_t::OUT_OF_MEMORY; }
        if ( !WriteMembers( group_t{ g.pStore, g.pArray, clone, id }, {} ) ) { return map_status_t::OUT_OF_MEMORY; }
        copyOf[original] = id;
        if ( pNewGroups != nullptr ) { pNewGroups->push_back( id ); }
    }
    // Every copy (object or group) joins its original's group's copy, or
    // that group itself.
    std::unordered_map<u64, std::vector<u64>> joining;
    for ( const auto &g : groups ) {
        for ( u64 member : g.members ) {
            const auto it = copyOf.find( member );
            if ( it == copyOf.end() || it->second == 0 ) { continue; }
            const auto parent = copyOf.find( g.id );
            joining[parent != copyOf.end() ? parent->second : g.id].push_back( it->second );
        }
    }
    for ( auto &[target, members] : joining ) {
        const auto g = FindGroup( map, target );
        if ( g.pRecord == nullptr ) { continue; }
        auto all = ReadMembers( g.pRecord );
        all.insert( all.end(), members.begin(), members.end() );
        if ( !WriteMembers( g, all ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return map_status_t::OK;
}

} // namespace cypher::editor::map
