//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Layers.cpp
//  Purpose: Implements the current, hidden and locked layers.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Layers.h"

#include "CypherMapGui_Notice.h"

#include <QApplication>
#include <QHash>
#include <QInputDialog>
#include <QSet>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

namespace cypher::editor::map
{
using namespace common;

namespace
{
struct layer_state_t {
    QSet<QByteArray> hidden, locked;
};
QHash<const map_workspace_t *, layer_state_t> &States()
{
    static QHash<const map_workspace_t *, layer_state_t> states;
    return states;
}

map_workspace_t *Ws( void *pContext ) noexcept { return static_cast<map_workspace_t *>( pContext ); }

bool Has( const map_workspace_t *ws, const QByteArray &id ) noexcept
{
    return ws != nullptr && ws->pDocument != nullptr && MapDocument_HasLayer( ws->pDocument, { id.constData(), static_cast<usize>( id.size() ) } );
}

bool Contains( const QSet<QByteArray> &set, const map_workspace_t *ws, u32 iLayer ) noexcept
{
    if ( set.isEmpty() || ws->pDocument == nullptr || iLayer >= ws->pDocument->layers.nCount ) { return false; }
    const char *id = ws->pDocument->layers.pData[iLayer].id;
    return set.contains( QByteArray::fromRawData( id, static_cast<qsizetype>( std::strlen( id ) ) ) );
}

u32 LayerOf( const map_workspace_t *ws, u64 id ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || id == 0 ) { return CY_U32_MAX; }
    if ( const auto *object = MapWireframe_FindObject( ws->wire, id ) ) { return object->iLayer; }
    if ( const auto *entity = MapWireframe_FindEntity( ws->wire, id ) ) { return entity->iLayer; }
    const auto &placements = ws->pDocument->geometryRecords;
    usize low = 0, high = placements.nCount;
    while ( low < high ) {
        const usize middle = low + ( high - low ) / 2;
        if ( placements.pData[middle].id < id ) { low = middle + 1; } else { high = middle; }
    }
    if ( low < placements.nCount && placements.pData[low].id == id ) { return placements.pData[low].iLayer; }
    map_layer_store_t *store = nullptr;
    if ( MapDocument_FindObject( ws->pDocument, id, &store ) != nullptr && store != nullptr ) {
        for ( usize i = 0; i < ws->pDocument->layers.nCount; ++i ) {
            if ( std::strcmp( ws->pDocument->layers.pData[i].id, store->layer ) == 0 ) { return static_cast<u32>( i ); }
        }
    }
    return CY_U32_MAX;
}

// Pruning cannot allocate: changing a lock must not leave protected roots
// selected when a replacement-selection allocation fails. Callers publish
// the completed context once, after invalidating the selection revision.
bool DropUnreachable( map_workspace_t *ws ) noexcept
{
    usize kept = 0;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const u64 id = ws->selection.ids.pData[i];
        const u32 layer = LayerOf( ws, id );
        if ( !MapLayers_IsHidden( ws, layer ) && !MapLayers_IsLocked( ws, layer ) ) { ws->selection.ids.pData[kept++] = id; }
    }
    if ( kept == ws->selection.ids.nCount ) { return false; }
    ws->selection.ids.nCount = kept;
    ws->selectedBrushFaceObject = 0; ws->selectedBrushFaceSide = 0;
    ws->selectedMeshFaceObject = 0; ws->selectedMeshFaceId = 0;
    Vector_Clear( &ws->meshFaceSet ); ws->meshFaceSetObject = 0;
    geometry::MeshSelection_Shutdown( &ws->meshSelection ); ws->selectedMeshEdgeSeed = {};
    return true;
}

QStringList LayerIds( const map_workspace_t *ws )
{
    QStringList ids;
    for ( usize i = 0; ws->pDocument != nullptr && i < ws->pDocument->layers.nCount; ++i ) { ids << QString::fromUtf8( ws->pDocument->layers.pData[i].id ); }
    return ids;
}

// The layer a command acts on: the one named; else the layer of the first
// selected object; else (when allowed) one chosen from a list; else the
// current layer. Empty when there is none (the chooser was cancelled).
QString Target( map_workspace_t *ws, const command_args_t &args, bool choose )
{
    if ( args.nArgs >= 1 ) { return QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) ); }
    if ( ws->selection.ids.nCount != 0 ) {
        const u32 layer = LayerOf( ws, ws->selection.ids.pData[0] );
        if ( layer < ws->pDocument->layers.nCount ) { return QString::fromUtf8( ws->pDocument->layers.pData[layer].id ); }
    }
    if ( choose && QApplication::instance() != nullptr && ws->pDocument->layers.nCount > 1 ) {
        const QStringList ids = LayerIds( ws );
        bool ok = false;
        const QString chosen = QInputDialog::getItem( QApplication::activeWindow(), QStringLiteral( "Layer" ), QStringLiteral( "Layer" ), ids,
                                                      std::max<qsizetype>( 0, ids.indexOf( MapLayers_Current( ws ) ) ), false, &ok );
        return ok ? chosen : QString();
    }
    return MapLayers_Current( ws );
}

u32 LayerState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return ws != nullptr && ws->pDocument != nullptr && ws->pDocument->layers.nCount != 0 ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

command_result_t SetCurrent( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( LayerState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const QString layer = Target( ws, args, args.nArgs == 0 && ws->selection.ids.nCount == 0 );
    if ( layer.isEmpty() ) { return command_result_t::OK; } // Cancelled.
    if ( !MapLayers_SetCurrent( ws, layer ) ) { return command_result_t::INVALID_ARGUMENTS; }
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "New objects go to layer %1" ).arg( layer ) );
    return command_result_t::OK;
}

template <bool kLock>
command_result_t Toggle( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( LayerState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const QString layer = Target( ws, args, false );
    if ( layer.isEmpty() ) { return command_result_t::OK; }
    const bool on = kLock ? !MapLayers_IsLockedId( ws, layer ) : !MapLayers_IsHiddenId( ws, layer );
    if ( !( kLock ? MapLayers_SetLocked( ws, layer, on ) : MapLayers_SetHidden( ws, layer, on ) ) ) { return command_result_t::INVALID_ARGUMENTS; }
    MapNotice_Post( ws, map_notice_t::INFO, kLock ? ( on ? QStringLiteral( "Layer %1 is locked: its objects show but cannot be picked" ).arg( layer ) : QStringLiteral( "Layer %1 is unlocked" ).arg( layer ) )
                                                  : ( on ? QStringLiteral( "Layer %1 is hidden" ).arg( layer ) : QStringLiteral( "Layer %1 is shown" ).arg( layer ) ) );
    return command_result_t::OK;
}

// Shows only this layer: every other layer is hidden.
command_result_t Isolate( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( LayerState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const QString layer = Target( ws, args, false );
    if ( layer.isEmpty() || !Has( ws, layer.toUtf8() ) ) { return command_result_t::INVALID_ARGUMENTS; }
    for ( const QString &other : LayerIds( ws ) ) { ( void )MapLayers_SetHidden( ws, other, other != layer ); }
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Only layer %1 is shown (Show All Layers brings the others back)" ).arg( layer ) );
    return command_result_t::OK;
}

command_result_t ShowAll( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( LayerState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    layer_state_t &state = States()[ws];
    const bool any = !state.hidden.isEmpty() || !state.locked.isEmpty();
    const bool locksChanged = !state.locked.isEmpty();
    state.hidden.clear(); state.locked.clear();
    u32 changes = MAP_CHANGE_VIEW;
    if ( locksChanged ) { ++ws->selection.revision; changes |= MAP_CHANGE_SELECTION; }
    MapWorkspace_Notify( ws, changes );
    MapNotice_Post( ws, map_notice_t::INFO, any ? QStringLiteral( "Every layer is shown and unlocked" ) : QStringLiteral( "Every layer was already shown and unlocked" ) );
    return command_result_t::OK;
}

// TrenchBroom's Select All in Layer.
command_result_t SelectLayer( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( LayerState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const QString layer = Target( ws, args, true );
    if ( layer.isEmpty() ) { return command_result_t::OK; }
    const QByteArray id = layer.toUtf8();
    if ( !Has( ws, id ) ) { return command_result_t::INVALID_ARGUMENTS; }
    std::vector<u64> ids;
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        if ( object.iLayer < ws->pDocument->layers.nCount && id == ws->pDocument->layers.pData[object.iLayer].id && object.owner == 0 &&
             MapWorkspace_IsSelectableObject( ws, object ) ) { ids.push_back( object.id ); }
    }
    MapWorkspace_SetSelection( ws, ids.data(), ids.size() );
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Selected %1 %2 in layer %3" ).arg( ids.size() ).arg( ids.size() == 1 ? QStringLiteral( "object" ) : QStringLiteral( "objects" ), layer ) );
    return command_result_t::OK;
}
} // namespace

bool MapLayers_IsHidden( const map_workspace_t *ws, u32 iLayer ) noexcept
{
    const auto it = States().constFind( ws );
    return it != States().constEnd() && Contains( it.value().hidden, ws, iLayer );
}

bool MapLayers_IsLocked( const map_workspace_t *ws, u32 iLayer ) noexcept
{
    const auto it = States().constFind( ws );
    return it != States().constEnd() && Contains( it.value().locked, ws, iLayer );
}

bool MapLayers_HasLocked( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return false; }
    const auto it = States().constFind( ws );
    if ( it == States().constEnd() ) { return false; }
    for ( usize i = 0; i < ws->pDocument->layers.nCount; ++i ) {
        if ( Contains( it.value().locked, ws, static_cast<u32>( i ) ) ) { return true; }
    }
    return false;
}

bool MapLayers_IsObjectLocked( const map_workspace_t *ws, u64 id ) noexcept
{
    return ws != nullptr && ws->pDocument != nullptr && id != 0 && MapLayers_IsLocked( ws, LayerOf( ws, id ) );
}

bool MapLayers_IsHiddenId( const map_workspace_t *ws, const QString &layer ) { return States().value( ws ).hidden.contains( layer.toUtf8() ); }
bool MapLayers_IsLockedId( const map_workspace_t *ws, const QString &layer ) { return States().value( ws ).locked.contains( layer.toUtf8() ); }

bool MapLayers_SetHidden( map_workspace_t *ws, const QString &layer, bool hidden )
{
    const QByteArray id = layer.toUtf8();
    if ( !Has( ws, id ) ) { return false; }
    layer_state_t &state = States()[ws];
    if ( hidden ) { state.hidden.insert( id ); } else { state.hidden.remove( id ); }
    const bool pruned = DropUnreachable( ws );
    u32 changes = MAP_CHANGE_VIEW;
    if ( pruned ) { ++ws->selection.revision; changes |= MAP_CHANGE_SELECTION; }
    MapWorkspace_Notify( ws, changes );
    return true;
}

bool MapLayers_SetLocked( map_workspace_t *ws, const QString &layer, bool locked )
{
    const QByteArray id = layer.toUtf8();
    if ( !Has( ws, id ) ) { return false; }
    const auto current = States().constFind( ws );
    const bool wasLocked = current != States().constEnd() && current.value().locked.contains( id );
    if ( wasLocked == locked ) { return true; }
    layer_state_t &state = States()[ws];
    if ( locked ) { state.locked.insert( id ); } else { state.locked.remove( id ); }
    ( void )DropUnreachable( ws );
    // A parent can be selected while its child is on this layer. Invalidate
    // every captured edit even if pruning did not change the explicit roots.
    ++ws->selection.revision;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW );
    return true;
}

bool MapLayers_SetCurrent( map_workspace_t *ws, const QString &layer )
{
    const QByteArray id = layer.toUtf8();
    if ( !Has( ws, id ) || static_cast<usize>( id.size() ) >= sizeof( ws->pDocument->creationLayer ) ) { return false; }
    std::memset( ws->pDocument->creationLayer, 0, sizeof( ws->pDocument->creationLayer ) );
    std::memcpy( ws->pDocument->creationLayer, id.constData(), static_cast<usize>( id.size() ) );
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
    return true;
}

QString MapLayers_Current( const map_workspace_t *ws )
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return QString(); }
    const string_view_t layer = MapDocument_CreationLayer( ws->pDocument );
    return QString::fromUtf8( layer.pData, static_cast<qsizetype>( layer.cchLength ) );
}

void MapLayers_Forget( const map_workspace_t *ws ) noexcept { States().remove( ws ); }

command_registry_status_t MapLayers_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pRegistry != nullptr );
    const command_desc_t commands[]{
        { "map.layer.set_current", "Set Current Layer...", "New objects go to this layer: the one named, the selection's, or one chosen from a list (TrenchBroom).", "layer-new",
          "map.layer.set_current [layer]", COMMAND_FLAG_NONE, SetCurrent, LayerState, pWorkspace },
        { "map.layer.toggle_hidden", "Hide or Show Layer", "Hide the layer (the one named, else the selection's), or show it again.", "hide-selected",
          "map.layer.toggle_hidden [layer]", COMMAND_FLAG_NONE, Toggle<false>, LayerState, pWorkspace },
        { "map.layer.toggle_locked", "Lock or Unlock Layer", "Lock the layer so its objects show but cannot be picked, or unlock it.", "texture-lock",
          "map.layer.toggle_locked [layer]", COMMAND_FLAG_NONE, Toggle<true>, LayerState, pWorkspace },
        { "map.layer.isolate", "Isolate Layer", "Show only this layer; every other one is hidden.", "hide-unselected",
          "map.layer.isolate [layer]", COMMAND_FLAG_NONE, Isolate, LayerState, pWorkspace },
        { "map.layer.show_all", "Show All Layers", "Show and unlock every layer.", "show-all", nullptr,
          COMMAND_FLAG_NONE, ShowAll, LayerState, pWorkspace },
        { "map.layer.select_objects", "Select Objects in Layer", "Select everything in the layer that can be picked.", "select-objects",
          "map.layer.select_objects [layer]", COMMAND_FLAG_NONE, SelectLayer, LayerState, pWorkspace },
    };
    return EditorCommands_Register( pRegistry, commands, std::size( commands ) );
}

} // namespace cypher::editor::map
