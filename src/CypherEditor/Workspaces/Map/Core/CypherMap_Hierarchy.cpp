//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Hierarchy.cpp
//  Purpose: Implements entity attachment: the `parent` member of entity
//           records (CYMAP.md 6.2).
//  Details: Entity records live in the `entities` array of a layer; layer
//           stores have no dirty state, so editing a record in place is all
//           a save needs (the writer puts `parent` in its canonical place).
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Hierarchy.h"

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

struct entity_t {
    map_layer_store_t *pStore{ nullptr };
    key_value_t *pRecord{ nullptr };
    u64 id{ 0u };
};

// Every entity record; `visit` returns false to stop.
template <typename Visit>
void EachEntity( const map_document_t *map, Visit visit )
{
    for ( usize c = 0; map != nullptr && c < map->layerStores.nCount; ++c ) {
        map_layer_store_t *layerStore = map->layerStores.pData[c];
        if ( layerStore->store.pDocument == nullptr ) { continue; }
        const key_value_t *entities = KeyValue_Find( KeyValue_Root( layerStore->store.pDocument ), SV( "entities" ) );
        for ( usize i = 0; i < KeyValue_ChildCount( entities ); ++i ) {
            key_value_t *record = const_cast<key_value_t *>( KeyValue_ChildAt( entities, i ) );
            u64 id = 0;
            if ( KeyValue_Type( record ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( record, SV( "id" ) ), id ) ) { continue; }
            if ( !visit( entity_t{ layerStore, record, id } ) ) { return; }
        }
    }
}

entity_t FindEntity( const map_document_t *map, u64 id ) noexcept
{
    entity_t found{};
    if ( id == 0 ) { return found; }
    EachEntity( map, [&]( const entity_t &e ) { if ( e.id != id ) { return true; } found = e; return false; } );
    return found;
}

u64 ParentOf( const entity_t &e ) noexcept
{
    u64 parent = 0;
    return e.pRecord != nullptr && ReadId( KeyValue_Find( e.pRecord, SV( "parent" ) ), parent ) ? parent : 0u;
}

bool WriteParent( const entity_t &e, u64 parent ) noexcept
{
    key_value_document_t *document = e.pStore->store.pDocument;
    if ( key_value_t *old = const_cast<key_value_t *>( KeyValue_Find( e.pRecord, SV( "parent" ) ) ) ) {
        if ( parent != 0 ) { return KeyValue_SetU64( document, old, parent ) != CY_FALSE; }
        return KeyValue_Remove( document, e.pRecord, old ) != CY_FALSE;
    }
    if ( parent == 0 ) { return true; }
    key_value_t *slot = KeyValue_ObjectInsert( document, e.pRecord, SV( "parent" ), key_value_type_t::U64 );
    return slot != nullptr && KeyValue_SetU64( document, slot, parent );
}
} // namespace

bool MapHierarchy_IsEntity( const map_document_t *map, u64 id ) noexcept { return FindEntity( map, id ).pRecord != nullptr; }

u64 MapHierarchy_Parent( const map_document_t *map, u64 id ) noexcept { return ParentOf( FindEntity( map, id ) ); }

std::vector<std::pair<u64, u64>> MapHierarchy_Links( const map_document_t *map )
{
    std::vector<std::pair<u64, u64>> links;
    EachEntity( map, [&]( const entity_t &e ) {
        if ( const u64 parent = ParentOf( e ); parent != 0 ) { links.emplace_back( e.id, parent ); }
        return true;
    } );
    return links;
}

std::vector<u64> MapHierarchy_Children( const map_document_t *map, u64 id, bool bRecursive )
{
    std::vector<u64> children;
    const auto links = MapHierarchy_Links( map );
    if ( links.empty() ) { return children; }
    // Breadth first: `id`, then (recursive) each child found in turn. A loop
    // authored by hand stops at the first repeat.
    std::unordered_set<u64> seen{ id };
    std::vector<u64> open{ id };
    for ( usize i = 0; i < open.size(); ++i ) {
        for ( const auto &[child, parent] : links ) {
            if ( parent != open[i] || !seen.insert( child ).second ) { continue; }
            children.push_back( child );
            if ( bRecursive ) { open.push_back( child ); }
        }
    }
    return children;
}

map_status_t MapHierarchy_SetParent( map_document_t *map, span_t<const u64> ids, u64 parent ) noexcept
{
    if ( map == nullptr || !Span_IsValid( ids ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    try {
        std::vector<entity_t> targets;
        for ( usize i = 0; i < ids.nCount; ++i ) {
            const entity_t e = FindEntity( map, ids.pData[i] );
            if ( e.pRecord == nullptr || ( parent != 0 && e.id == parent ) ) { return map_status_t::INVALID_ARGUMENT; }
            targets.push_back( e );
        }
        if ( parent != 0 ) {
            if ( FindEntity( map, parent ).pRecord == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
            // The parent must not hang from one of them: walk up from it.
            std::unordered_map<u64, u64> up;
            for ( const auto &[child, p] : MapHierarchy_Links( map ) ) { up[child] = p; }
            std::unordered_set<u64> seen;
            for ( u64 at = parent; at != 0 && seen.insert( at ).second; ) {
                for ( const entity_t &e : targets ) { if ( e.id == at ) { return map_status_t::INVALID_ARGUMENT; } }
                const auto found = up.find( at );
                at = found != up.end() ? found->second : 0u;
            }
        }
        for ( const entity_t &e : targets ) {
            if ( !WriteParent( e, parent ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        return map_status_t::OK;
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
}

map_status_t MapHierarchy_Copied( map_document_t *map, const std::vector<std::pair<u64, u64>> &copies ) noexcept
{
    if ( map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    try {
        std::unordered_map<u64, u64> copyOf( copies.begin(), copies.end() );
        for ( const auto &[oldId, newId] : copies ) {
            const entity_t e = FindEntity( map, newId );
            const u64 parent = ParentOf( e );
            if ( parent == 0 ) { continue; }
            const auto copied = copyOf.find( parent );
            if ( copied != copyOf.end() && !WriteParent( e, copied->second ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        return map_status_t::OK;
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
}

std::vector<u64> MapHierarchy_Orphans( const map_document_t *map )
{
    std::unordered_set<u64> entities;
    EachEntity( map, [&]( const entity_t &e ) { entities.insert( e.id ); return true; } );
    std::vector<u64> orphans;
    for ( const auto &[child, parent] : MapHierarchy_Links( map ) ) {
        if ( entities.count( parent ) == 0 ) { orphans.push_back( child ); }
    }
    return orphans;
}

} // namespace cypher::editor::map
