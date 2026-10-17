//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Hierarchy.cpp
//  Purpose: Implements Hammer 5's hierarchy commands for entity attachment.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Hierarchy.h"

#include "CypherMapGui_Notice.h"
#include "CypherMap_Hierarchy.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace cypher::editor::map
{
using namespace common;

namespace
{
// Per workspace: the selected IDs in the order they were picked.
std::unordered_map<const map_workspace_t *, std::vector<u64>> &Picked()
{
    static std::unordered_map<const map_workspace_t *, std::vector<u64>> picked;
    return picked;
}

map_workspace_t *Ws( void *pContext ) noexcept { return static_cast<map_workspace_t *>( pContext ); }

bool Writable( const map_workspace_t *ws ) noexcept { return ws != nullptr && ws->pDocument != nullptr && !ws->pDocument->bReadOnly; }

// The entity an object stands for: itself, or the entity owning its geometry.
u64 EntityOf( const map_workspace_t *ws, u64 id ) noexcept
{
    if ( MapWireframe_FindEntity( ws->wire, id ) != nullptr ) { return id; }
    const auto *object = MapWireframe_FindObject( ws->wire, id );
    return object != nullptr && object->owner != 0 && MapWireframe_FindEntity( ws->wire, object->owner ) != nullptr ? object->owner : 0u;
}

QString Label( const map_workspace_t *ws, u64 id )
{
    const auto *entity = MapWireframe_FindEntity( ws->wire, id );
    if ( entity == nullptr ) { return QStringLiteral( "#%1" ).arg( id ); }
    const QString name = QString::fromUtf8( entity->name );
    return name.isEmpty() ? QString::fromUtf8( entity->className ) : QStringLiteral( "%1 \"%2\"" ).arg( QString::fromUtf8( entity->className ), name );
}

QString Count( usize n, const char *one, const char *many ) { return QStringLiteral( "%1 %2" ).arg( n ).arg( QString::fromUtf8( n == 1 ? one : many ) ); }

struct parent_job_t {
    std::vector<u64> children;
    u64 parent{ 0u };
    std::vector<u64> keep; // The selection stays as it was.
};

map_status_t SetParents( void *pContext, map_document_t *pCopy, vector_t<u64> *pSelectionOut, bool *pChanged ) noexcept
{
    const auto *job = static_cast<const parent_job_t *>( pContext );
    *pChanged = std::any_of( job->children.begin(), job->children.end(), [&]( u64 child ) { return MapHierarchy_Parent( pCopy, child ) != job->parent; } );
    if ( !*pChanged ) { return map_status_t::OK; }
    const map_status_t status = MapHierarchy_SetParent( pCopy, { job->children.data(), job->children.size() }, job->parent );
    for ( usize i = 0; status == map_status_t::OK && i < job->keep.size(); ++i ) {
        if ( !Vector_PushBack( pSelectionOut, job->keep[i] ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return status;
}

u32 ReparentState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return Writable( ws ) && MapHierarchy_SelectedEntities( ws ).size() >= 2u ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

// Hammer 5's Reparent Selection: the entity picked last is the parent.
command_result_t Reparent( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ReparentState( pContext ) == COMMAND_STATE_NONE ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Parent to Last Selected: select the entities to attach, then the parent last (two entities at least)" ) );
        return command_result_t::DISABLED;
    }
    try {
        parent_job_t job;
        std::vector<u64> entities = MapHierarchy_SelectedEntities( ws );
        job.parent = entities.back();
        entities.pop_back();
        job.children = std::move( entities );
        // A child cannot also be an ancestor of its new parent.
        for ( u64 child : job.children ) {
            const auto below = MapHierarchy_Children( ws->pDocument, child, true );
            if ( std::find( below.begin(), below.end(), job.parent ) != below.end() ) {
                MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Parent to Last Selected: %1 already hangs from %2; attaching would make a loop" ).arg( Label( ws, job.parent ), Label( ws, child ) ) );
                return command_result_t::FAILED;
            }
        }
        job.keep.assign( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
        if ( !MapWorkspace_ApplyMetadataEdit( ws, "Parent to Last Selected", &SetParents, &job ) ) {
            if ( ws->lastEdit.outcome == map_edit_outcome_t::UNCHANGED ) {
                MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Parent to Last Selected: they already hang from %1" ).arg( Label( ws, job.parent ) ) );
                return command_result_t::OK;
            }
            return command_result_t::FAILED;
        }
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "%1 now %2 from %3; they move with it" )
            .arg( Count( job.children.size(), "entity", "entities" ), job.children.size() == 1 ? QStringLiteral( "hangs" ) : QStringLiteral( "hang" ), Label( ws, job.parent ) ) );
        return command_result_t::OK;
    } catch ( ... ) { return command_result_t::FAILED; }
}

// The selected entities that hang from something.
std::vector<u64> Attached( const map_workspace_t *ws )
{
    std::vector<u64> attached;
    for ( u64 id : MapHierarchy_SelectedEntities( ws ) ) { if ( MapHierarchy_Parent( ws->pDocument, id ) != 0 ) { attached.push_back( id ); } }
    return attached;
}

u32 AttachedState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pDocument == nullptr ) { return COMMAND_STATE_NONE; }
    try { return !Attached( ws ).empty() ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE; } catch ( ... ) { return COMMAND_STATE_NONE; }
}

command_result_t Unparent( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !Writable( ws ) || AttachedState( pContext ) == COMMAND_STATE_NONE ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Unparent: none of the selected entities hangs from another" ) );
        return command_result_t::DISABLED;
    }
    try {
        parent_job_t job;
        job.children = Attached( ws );
        job.keep.assign( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
        if ( !MapWorkspace_ApplyMetadataEdit( ws, "Unparent", &SetParents, &job ) ) { return command_result_t::FAILED; }
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "%1 detached; they now move on their own" ).arg( Count( job.children.size(), "entity", "entities" ) ) );
        return command_result_t::OK;
    } catch ( ... ) { return command_result_t::FAILED; }
}

command_result_t SelectParent( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( AttachedState( pContext ) == COMMAND_STATE_NONE ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Select Parent: none of the selected entities hangs from another" ) );
        return command_result_t::DISABLED;
    }
    try {
        std::vector<u64> parents;
        for ( u64 id : Attached( ws ) ) {
            const u64 parent = MapHierarchy_Parent( ws->pDocument, id );
            if ( std::find( parents.begin(), parents.end(), parent ) == parents.end() && MapWireframe_FindEntity( ws->wire, parent ) != nullptr ) { parents.push_back( parent ); }
        }
        if ( parents.empty() ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Select Parent: the parent is missing from the map (Check for Problems can clear it)" ) );
            return command_result_t::FAILED;
        }
        MapWorkspace_SetSelection( ws, parents.data(), parents.size() );
        MapNotice_Post( ws, map_notice_t::INFO, parents.size() == 1 ? QStringLiteral( "Selected %1" ).arg( Label( ws, parents[0] ) ) : QStringLiteral( "Selected %1 parents" ).arg( parents.size() ) );
        return command_result_t::OK;
    } catch ( ... ) { return command_result_t::FAILED; }
}

std::vector<u64> Descendants( const map_workspace_t *ws )
{
    std::vector<u64> all;
    for ( u64 id : MapHierarchy_SelectedEntities( ws ) ) {
        for ( u64 child : MapHierarchy_Children( ws->pDocument, id, true ) ) {
            if ( std::find( all.begin(), all.end(), child ) == all.end() ) { all.push_back( child ); }
        }
    }
    return all;
}

u32 ParentsState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pDocument == nullptr ) { return COMMAND_STATE_NONE; }
    try {
        const auto links = MapHierarchy_Links( ws->pDocument );
        if ( links.empty() ) { return COMMAND_STATE_NONE; }
        for ( u64 id : MapHierarchy_SelectedEntities( ws ) ) {
            if ( std::any_of( links.begin(), links.end(), [&]( const auto &link ) { return link.second == id; } ) ) { return COMMAND_STATE_ENABLED; }
        }
        return COMMAND_STATE_NONE;
    } catch ( ... ) { return COMMAND_STATE_NONE; }
}

command_result_t SelectChildren( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ParentsState( pContext ) == COMMAND_STATE_NONE ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Select Children: nothing hangs from the selected entities" ) );
        return command_result_t::DISABLED;
    }
    try {
        std::vector<u64> children = Descendants( ws );
        children.erase( std::remove_if( children.begin(), children.end(), [&]( u64 id ) { return MapWireframe_FindEntity( ws->wire, id ) == nullptr; } ), children.end() );
        MapWorkspace_SetSelection( ws, children.data(), children.size() );
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Selected %1 hanging from it" ).arg( Count( children.size(), "entity", "entities" ) ) );
        return command_result_t::OK;
    } catch ( ... ) { return command_result_t::FAILED; }
}
} // namespace

std::vector<u64> MapHierarchy_SelectedEntities( const map_workspace_t *ws )
{
    std::vector<u64> entities;
    if ( ws == nullptr || ws->pDocument == nullptr ) { return entities; }
    const auto found = Picked().find( ws );
    const auto add = [&]( u64 id ) {
        const u64 entity = EntityOf( ws, id );
        if ( entity != 0 && std::find( entities.begin(), entities.end(), entity ) == entities.end() ) { entities.push_back( entity ); }
    };
    // Picked order when it matches the selection (it always should), else ID order.
    if ( found != Picked().end() && found->second.size() == ws->selection.ids.nCount ) {
        for ( u64 id : found->second ) { add( id ); }
    } else {
        for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) { add( ws->selection.ids.pData[i] ); }
    }
    return entities;
}

void MapHierarchy_SelectionChanged( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr ) { return; }
    try {
        auto &picked = Picked()[ws];
        const u64 *first = ws->selection.ids.pData, *last = ws->selection.ids.pData + ws->selection.ids.nCount;
        // What left the selection goes; what joined it comes last, in ID order.
        picked.erase( std::remove_if( picked.begin(), picked.end(), [&]( u64 id ) { return !std::binary_search( first, last, id ); } ), picked.end() );
        const std::unordered_set<u64> kept( picked.begin(), picked.end() );
        for ( const u64 *id = first; id != last; ++id ) { if ( kept.count( *id ) == 0 ) { picked.push_back( *id ); } }
    } catch ( ... ) { Picked().erase( ws ); }
}

void MapHierarchy_Forget( const map_workspace_t *ws ) noexcept { Picked().erase( ws ); }

command_registry_status_t MapHierarchy_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pRegistry != nullptr );
    const command_desc_t commands[]{
        { "map.entity.reparent", "Parent to Last Selected", "Attach the selected entities to the one picked last; they then move, turn, copy and delete with it (Hammer 5).",
          "group-create", nullptr, COMMAND_FLAG_NONE, Reparent, ReparentState, pWorkspace },
        { "map.entity.unparent", "Unparent", "Detach the selected entities from their parents.", "group-ungroup", nullptr,
          COMMAND_FLAG_NONE, Unparent, AttachedState, pWorkspace },
        { "map.select.parent", "Select Parent", "Select the entities the selected ones hang from.", "select-objects", nullptr,
          COMMAND_FLAG_NONE, SelectParent, AttachedState, pWorkspace },
        { "map.select.children", "Select Children", "Select every entity hanging from the selected ones, at any depth.", "select-objects", nullptr,
          COMMAND_FLAG_NONE, SelectChildren, ParentsState, pWorkspace },
    };
    return EditorCommands_Register( pRegistry, commands, std::size( commands ) );
}

} // namespace cypher::editor::map
