//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Publishes validated map edits and owns their bounded undo snapshots.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Workspace.h"
#include "CypherMapGui_Notice.h"
#include "CypherMapGui_Layers.h"
#include "CypherMapGui_ToolGestures.h"
#include "CypherMap_Edit.h"
#include "CypherMap_FaceEdit.h"
#include "CypherMap_BrushEdit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_TopologyEdit.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_Clipboard.h"
#include "CypherMap_Groups.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_HeightField.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>

namespace cypher::editor::map
{
using namespace common;
namespace
{
// History owns both states. Neither ever becomes the editable live document:
// saving a map or changing its selection cannot grow a journal entry's charge.
struct map_edit_snapshot_t {
    map_document_t *document{ nullptr };
    vector_t<u64> selection{};
    u64 faceObject{ 0 }, faceSide{ 0 };
    u64 meshFaceObject{ 0 }, meshFaceId{ 0 };
    vector_t<u64> meshFaces{};
    geometry::mesh_selection_t components{};
    geometry::mesh_edge_ref_t edgeSeed{};
    int vertexColumnAxis{ -1 };
    ~map_edit_snapshot_t() { delete document; Vector_Shutdown( &selection ); }
};
struct map_edit_state_t { map_edit_snapshot_t before{}, after{}; };
struct map_prepared_edit_t { map_edit_snapshot_t live{}; map_wireframe_t wire{}; };

map_edit_state_t *State( binary_block_t payload ) noexcept
{
    map_edit_state_t *state = nullptr;
    if ( payload.cbSize == sizeof( state ) ) { Cy_MemCopy( &state, payload.pData, sizeof( state ) ); }
    return state;
}

bool CopySelection( vector_t<u64> &out, span_t<const u64> ids, const allocator_t *allocator ) noexcept
{
    if ( !Vector_Init( &out, allocator, ids.nCount ) ) { return false; }
    for ( usize i = 0; i < ids.nCount; ++i ) { if ( !Vector_PushBack( &out, ids.pData[i] ) ) { return false; } }
    return true;
}

// Borrowed input only. Every retained set is copied with the GUI allocator;
// neither a tool's temporary result nor the live selection belongs to history.
struct selection_context_t {
    u64 faceObject{}, faceSide{}, meshFaceObject{}, meshFaceId{};
    span_t<const u64> meshFaces{};
    const geometry::mesh_selection_t *components{};
    geometry::mesh_edge_ref_t edgeSeed{};
    int vertexColumnAxis{ -1 };
};
selection_context_t Context( const map_workspace_t &ws ) noexcept
{
    return { ws.selectedBrushFaceObject, ws.selectedBrushFaceSide, ws.selectedMeshFaceObject, ws.selectedMeshFaceId,
        MapWorkspace_MeshFaces( &ws ), &ws.meshSelection, ws.selectedMeshEdgeSeed,
        ws.meshVertexColumnRevision == ws.selection.revision ? ws.meshVertexColumnAxis : -1 };
}
selection_context_t Context( const map_edit_snapshot_t &snapshot ) noexcept
{
    return { snapshot.faceObject, snapshot.faceSide, snapshot.meshFaceObject, snapshot.meshFaceId,
        { snapshot.meshFaces.pData, snapshot.meshFaces.nCount }, &snapshot.components, snapshot.edgeSeed, snapshot.vertexColumnAxis };
}
bool SoleRoot( const map_edit_snapshot_t &snapshot, u64 id ) noexcept
{
    return id != 0 && snapshot.selection.nCount == 1 && snapshot.selection.pData[0] == id;
}

// Ordinary edits prune only IDs genuinely removed by topology. An explicit
// tool result instead has to be wholly valid: publishing half a new rim would
// hide a broken operation behind an apparently successful history step.
map_status_t PrepareContext( map_edit_snapshot_t &out, const selection_context_t &source,
    map_element_mode_t mode, const allocator_t *allocator, bool strict = false ) noexcept
{
    if ( mode == map_element_mode_t::FACES && !strict ) {
        const auto *brush = geometry::GeometryDocument_FindBrush( &out.document->geometry, { source.faceObject } );
        if ( brush != nullptr && SoleRoot( out, source.faceObject ) ) {
            for ( usize i = 0; i < brush->sides.nCount; ++i ) {
                if ( brush->sides.pData[i].sourceId.value == source.faceSide ) {
                    out.faceObject = source.faceObject; out.faceSide = source.faceSide; break;
                }
            }
        }
        const auto *mesh = geometry::GeometryDocument_FindMesh( &out.document->geometry, { source.meshFaceObject } );
        if ( mesh == nullptr || !SoleRoot( out, source.meshFaceObject ) ) { return map_status_t::OK; }
        if ( !Vector_Init( &out.meshFaces, allocator, source.meshFaces.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
        for ( usize i = 0; i < source.meshFaces.nCount; ++i ) {
            geometry::geometry_mesh_face_handle_t handle{};
            if ( geometry::MeshSource_TryFindFace( mesh, { source.meshFaces.pData[i] }, &handle ) &&
                 !Vector_PushBack( &out.meshFaces, source.meshFaces.pData[i] ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        if ( out.meshFaces.nCount != 0 ) {
            out.meshFaceObject = source.meshFaceObject;
            out.meshFaceId = std::find( out.meshFaces.pData, out.meshFaces.pData + out.meshFaces.nCount, source.meshFaceId ) !=
                out.meshFaces.pData + out.meshFaces.nCount ? source.meshFaceId : out.meshFaces.pData[out.meshFaces.nCount - 1];
        }
        return map_status_t::OK;
    }
    const auto *set = source.components;
    const bool vertices = mode == map_element_mode_t::VERTICES, edges = mode == map_element_mode_t::EDGES;
    if ( set == nullptr ) { return strict ? map_status_t::INVALID_ARGUMENT : map_status_t::OK; }
    const bool empty = set->vertices.nCount == 0 && set->edges.nCount == 0 && set->faces.nCount == 0;
    if ( empty && set->meshId.value == 0 ) { return map_status_t::OK; }
    const auto *mesh = geometry::GeometryDocument_FindMesh( &out.document->geometry, set->meshId );
    const bool compatible = ( vertices || edges ) && set->faces.nCount == 0 &&
        ( vertices ? set->edges.nCount == 0 : set->vertices.nCount == 0 ) && SoleRoot( out, set->meshId.value ) && mesh != nullptr;
    if ( !compatible ) { return strict ? map_status_t::INVALID_ARGUMENT : map_status_t::OK; }
    if ( !Span_IsValid( span_t<const geometry::geometry_source_id_t>{ set->vertices.pData, set->vertices.nCount } ) ||
         !Span_IsValid( span_t<const geometry::mesh_edge_ref_t>{ set->edges.pData, set->edges.nCount } ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( geometry::MeshSelection_Init( &out.components, allocator, set->meshId ) != geometry::geometry_status_t::OK ||
         !Vector_Reserve( &out.components.vertices, set->vertices.nCount ) ||
         !Vector_Reserve( &out.components.edges, set->edges.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < set->vertices.nCount; ++i ) {
        geometry::geometry_mesh_vertex_handle_t handle{};
        const auto id = set->vertices.pData[i];
        const bool valid = geometry::MeshSource_TryFindVertex( mesh, id, &handle ) &&
            ( i == 0 || set->vertices.pData[i - 1].value < id.value );
        if ( !valid ) { if ( strict ) { return map_status_t::INVALID_ARGUMENT; } continue; }
        if ( !Vector_PushBack( &out.components.vertices, id ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    for ( usize i = 0; i < set->edges.nCount; ++i ) {
        geometry::geometry_mesh_edge_handle_t handle{};
        const auto edge = set->edges.pData[i];
        const bool ordered = i == 0 || set->edges.pData[i - 1].a.value < edge.a.value ||
            ( set->edges.pData[i - 1].a.value == edge.a.value && set->edges.pData[i - 1].b.value < edge.b.value );
        const bool valid = edge.a.value < edge.b.value && ordered && geometry::MeshSourceEdit_TryFindEdge( mesh, edge.a, edge.b, &handle );
        if ( !valid ) { if ( strict ) { return map_status_t::INVALID_ARGUMENT; } continue; }
        if ( !Vector_PushBack( &out.components.edges, edge ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( out.components.edges.nCount != 0 ) {
        out.edgeSeed = geometry::MeshSelection_HasEdge( &out.components, source.edgeSeed ) ? source.edgeSeed : out.components.edges.pData[0];
    }
    if ( out.components.vertices.nCount != 0 && source.vertexColumnAxis >= 0 && source.vertexColumnAxis < 3 ) {
        out.vertexColumnAxis = source.vertexColumnAxis;
    }
    return map_status_t::OK;
}

void MoveWire( map_wireframe_t &out, map_wireframe_t &source ) noexcept
{
    MapWireframe_Shutdown( &out );
    Vector_Move( &out.points, &source.points );
    Vector_Move( &out.pointSourceIds, &source.pointSourceIds );
    Vector_Move( &out.lines, &source.lines );
    Vector_Move( &out.objects, &source.objects );
    Vector_Move( &out.entities, &source.entities );
    Vector_Move( &out.connections, &source.connections );
    Vector_Move( &out.faces, &source.faces );
    Vector_Move( &out.faceIndices, &source.faceIndices );
    Vector_Move( &out.meshSurfaces, &source.meshSurfaces );
    out.bounds = source.bounds;
    out.nBrokenBrushes = source.nBrokenBrushes;
    out.pAllocator = source.pAllocator;
}

void Publish( map_workspace_t *ws, map_prepared_edit_t &prepared ) noexcept
{
    // Transfer the already admitted selection alongside its document. This
    // phase must not allocate or call the separately notifying pick APIs.
    geometry::MeshSelection_Shutdown( &ws->meshSelection );
    Vector_Move( &ws->meshSelection.vertices, &prepared.live.components.vertices );
    Vector_Move( &ws->meshSelection.edges, &prepared.live.components.edges );
    Vector_Move( &ws->meshSelection.faces, &prepared.live.components.faces );
    ws->meshSelection.meshId = prepared.live.components.meshId;
    ws->selectedMeshEdgeSeed = prepared.live.edgeSeed;
    Vector_Shutdown( &ws->meshFaceSet );
    Vector_Move( &ws->meshFaceSet, &prepared.live.meshFaces );
    ws->meshFaceSetObject = ws->meshFaceSet.nCount != 0 ? prepared.live.meshFaceObject : 0;
    ws->meshVertexColumnAxis = prepared.live.vertexColumnAxis;
    ws->meshVertexColumnRevision = 0;
    // The current layer is the user's choice, not part of the step: undo
    // and redo keep the one in use.
    if ( ws->pDocument != nullptr && prepared.live.document != nullptr ) {
        std::memcpy( prepared.live.document->creationLayer, ws->pDocument->creationLayer, sizeof( prepared.live.document->creationLayer ) );
    }
    delete ws->pDocument;
    ws->pDocument = prepared.live.document;
    prepared.live.document = nullptr;
    Vector_Shutdown( &ws->selection.ids );
    Vector_Move( &ws->selection.ids, &prepared.live.selection );
    MoveWire( ws->wire, prepared.wire );
    ws->selectedBrushFaceObject = prepared.live.faceObject;
    ws->selectedBrushFaceSide = prepared.live.faceSide;
    ws->selectedMeshFaceObject = prepared.live.meshFaceObject;
    ws->selectedMeshFaceId = prepared.live.meshFaceId;
    // Session locks survive history restoration. Restore the document, but do
    // not revive an editable selection containing a currently locked object.
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        if ( !MapLayers_IsObjectLocked( ws, ws->selection.ids.pData[i] ) ) { continue; }
        Vector_Clear( &ws->selection.ids );
        ws->selectedBrushFaceObject = ws->selectedBrushFaceSide = 0;
        ws->selectedMeshFaceObject = ws->selectedMeshFaceId = 0;
        Vector_Clear( &ws->meshFaceSet ); ws->meshFaceSetObject = 0;
        geometry::MeshSelection_Shutdown( &ws->meshSelection ); ws->selectedMeshEdgeSeed = {};
        ws->meshVertexColumnAxis = -1;
        break;
    }
    ++ws->selection.revision;
    if ( ws->meshVertexColumnAxis >= 0 ) { ws->meshVertexColumnRevision = ws->selection.revision; }
    MapWorkspace_DocumentChanged( ws, false );
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
}

error_code_t Restore( const map_edit_snapshot_t &snapshot, map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || snapshot.document == nullptr ) { return Cy_ErrorMake( common_error_t::ERR_INVALID_ARGUMENT ); }
    map_prepared_edit_t prepared;
    if ( MapEdit_Clone( snapshot.document, &prepared.live.document ) != map_status_t::OK ||
         !CopySelection( prepared.live.selection, { snapshot.selection.pData, snapshot.selection.nCount }, ws->pGui->pAllocator ) ||
         !MapWireframe_Init( &prepared.wire, ws->pGui->pAllocator ) ||
         MapWireframe_Build( &prepared.wire, *prepared.live.document ) != map_status_t::OK ) {
        return Cy_ErrorMake( common_error_t::ERR_OUT_OF_MEMORY );
    }
    if ( PrepareContext( prepared.live, Context( snapshot ), ws->elementMode, ws->pGui->pAllocator ) != map_status_t::OK ) {
        return Cy_ErrorMake( common_error_t::ERR_OUT_OF_MEMORY );
    }
    // All allocations and geometry reconstruction succeeded. Publication below
    // only transfers ownership, so failure never empties the live wireframe.
    MapToolGestures_HistoryWillRestore( ws, prepared.live.document );
    Publish( ws, prepared );
    return CY_ERROR_OK;
}

error_code_t UndoEdit( binary_block_t payload, void *context ) noexcept
{
    const auto *state = State( payload );
    return state != nullptr ? Restore( state->before, static_cast<map_workspace_t *>( context ) )
                            : Cy_ErrorMake( common_error_t::ERR_INVALID_ARGUMENT );
}
error_code_t RedoEdit( binary_block_t payload, void *context ) noexcept
{
    const auto *state = State( payload );
    return state != nullptr ? Restore( state->after, static_cast<map_workspace_t *>( context ) )
                            : Cy_ErrorMake( common_error_t::ERR_INVALID_ARGUMENT );
}

void DisposeEdit( binary_block_t payload, void * ) noexcept { delete State( payload ); }

span_t<const u64> Selected( const map_workspace_t &ws ) noexcept { return { ws.selection.ids.pData, ws.selection.ids.nCount }; }
bool Finite( math::vec3d_t v ) noexcept { return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z ); }

bool AddCharge( usize &total, usize bytes ) noexcept
{
    if ( bytes > CY_USIZE_MAX - total ) { return false; }
    total += bytes;
    return true;
}

// Records how an edit ended in ws->lastEdit when it leaves scope (FAILED
// unless the edit says otherwise), whichever return it takes.
struct edit_report_scope_t {
    map_workspace_t *ws;
    map_edit_outcome_t outcome{ map_edit_outcome_t::FAILED };
    map_status_t status{ map_status_t::OUT_OF_MEMORY };
    explicit edit_report_scope_t( map_workspace_t *workspace ) noexcept : ws( workspace ) { ++ws->lastEdit.serial; }
    ~edit_report_scope_t() noexcept { ws->lastEdit.outcome = outcome; ws->lastEdit.status = status; }
    CYPHER_NO_COPY_MOVE( edit_report_scope_t );
};

template <typename Apply>
bool Edit( map_workspace_t *ws, const char *label, Apply apply, const bool *metadataChanged = nullptr,
    const map_prepared_component_selection_t *preparedSelection = nullptr ) noexcept
{
    if ( ws == nullptr ) { return false; }
    edit_report_scope_t report( ws );
    if ( ws->pDocument == nullptr || ws->pDocument->bReadOnly || EditorHistory_IsTransactionOpen( &ws->history ) ) {
        report.status = ws->pDocument != nullptr && ws->pDocument->bReadOnly ? map_status_t::READ_ONLY : map_status_t::INVALID_ARGUMENT;
        return false;
    }
    map_prepared_edit_t prepared;
    map_status_t status = MapEdit_Clone( ws->pDocument, &prepared.live.document );
    if ( status != map_status_t::OK ) { report.status = status; return false; }
    if ( !Vector_Init( &prepared.live.selection, ws->pGui->pAllocator ) ) { return false; }
    status = apply( prepared.live.document, prepared.live.selection );
    // Deleting, merging or cutting objects can leave a group naming objects
    // that are gone; those members drop out (and empty groups dissolve)
    // before the edit publishes, so selecting a group never names them.
    if ( status == map_status_t::OK ) { bool pruned = false; status = MapGroups_Prune( prepared.live.document, &pruned ); }
    // Core operations may return roots in source order while allocating owners
    // before their geometry. EditorSelection is a sorted set: its hit/highlight
    // and toggle queries use lower_bound. Canonicalize before snapshotting too,
    // so paste, duplicate and undo/redo share that invariant without allocation.
    if ( status == map_status_t::OK && prepared.live.selection.nCount > 1 ) {
        std::sort( prepared.live.selection.pData, prepared.live.selection.pData + prepared.live.selection.nCount );
    }
    // Geometry callers keep their revision-based no-op check. Metadata callers
    // report an actual authored change explicitly; entity keys do not revise
    // the geometry document merely to pass through the publication boundary.
    if ( status == map_status_t::OK && ( metadataChanged != nullptr ? !*metadataChanged :
         prepared.live.document->geometry.revision == ws->pDocument->geometry.revision ) ) {
        report.outcome = map_edit_outcome_t::UNCHANGED; report.status = map_status_t::OK;
        return false;
    }
    // Build before publishing: an edit must remain viewable, not just serializable.
    if ( status == map_status_t::OK ) {
        status = MapWireframe_Init( &prepared.wire, ws->pGui->pAllocator ) ? MapWireframe_Build( &prepared.wire, *prepared.live.document ) : map_status_t::OUT_OF_MEMORY;
    }
    if ( status != map_status_t::OK ) {
        Cy_LogWriteAt( log_level_t::Warning, log_channel_t::Editor, MapDocument_StatusName( status ), CY_SOURCE_LOCATION );
        if ( status == map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "This transform cannot preserve the live modifier's shape. No edit was published. Use a supported transform or edit the modifier first." ) );
        } else if ( status == map_status_t::PREFAB_TRANSFORM_UNSUPPORTED ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "This scale would shear the rotated linked prefab. No edit was published. Use uniform scaling or compatible factors along its rotated axes." ) );
        }
        report.status = status;
        return false;
    }
    auto afterContext = Context( *ws );
    if ( preparedSelection != nullptr ) {
        afterContext = {};
        afterContext.components = preparedSelection->pMeshSelection;
        afterContext.edgeSeed = preparedSelection->edgeSeed;
        afterContext.vertexColumnAxis = preparedSelection->vertexColumnAxis;
    }
    status = PrepareContext( prepared.live, afterContext, ws->elementMode, ws->pGui->pAllocator, preparedSelection != nullptr );
    if ( status != map_status_t::OK ) { report.status = status; return false; }
    std::unique_ptr<map_edit_state_t> owned( new ( std::nothrow ) map_edit_state_t{} );
    if ( owned == nullptr ) { return false; }
    if ( MapEdit_Clone( ws->pDocument, &owned->before.document ) != map_status_t::OK ||
         MapEdit_Clone( prepared.live.document, &owned->after.document ) != map_status_t::OK ||
         !CopySelection( owned->before.selection, Selected( *ws ), ws->pGui->pAllocator ) ||
         !CopySelection( owned->after.selection, { prepared.live.selection.pData, prepared.live.selection.nCount }, ws->pGui->pAllocator ) ) { return false; }
    if ( PrepareContext( owned->before, Context( *ws ), ws->elementMode, ws->pGui->pAllocator ) != map_status_t::OK ||
         PrepareContext( owned->after, Context( prepared.live ), ws->elementMode, ws->pGui->pAllocator ) != map_status_t::OK ) { return false; }
    map_edit_state_t *state = owned.get();
    undo_operation_desc_t op{};
    op.id = 0x4d415045444954u;
    op.label = StringView_FromCString( label );
    op.payload = BinaryBlock_FromData( &state, sizeof( state ) );
    op.pfnUndo = &UndoEdit; op.pfnRedo = &RedoEdit; op.pUserData = ws; op.pfnDispose = &DisposeEdit;
    usize charge = sizeof( *state );
    if ( !AddCharge( charge, MapEdit_EstimateBytes( state->before.document ) ) ||
         !AddCharge( charge, MapEdit_EstimateBytes( state->after.document ) ) ||
         !AddCharge( charge, state->before.selection.nCapacity * sizeof( u64 ) ) ||
         !AddCharge( charge, state->after.selection.nCapacity * sizeof( u64 ) ) ) { return false; }
    for ( const auto *snapshot : { &state->before, &state->after } ) {
        if ( !AddCharge( charge, snapshot->meshFaces.nCapacity * sizeof( u64 ) ) ||
             !AddCharge( charge, snapshot->components.vertices.nCapacity * sizeof( geometry::geometry_source_id_t ) ) ||
             !AddCharge( charge, snapshot->components.edges.nCapacity * sizeof( geometry::mesh_edge_ref_t ) ) ||
             !AddCharge( charge, snapshot->components.faces.nCapacity * sizeof( geometry::geometry_source_id_t ) ) ) { return false; }
    }
    op.cbOwnedBytes = charge;
    // Register without notifying first: observers must never see a committed
    // history entry whose document has not yet been published. Failed Push
    // leaves both the live map and snapshot ownership with this scope.
    if ( EditorHistory_Push( &ws->history, op, false ) != editor_history_status_t::OK ) { return false; }
    ( void )owned.release();
    report.outcome = map_edit_outcome_t::APPLIED; report.status = map_status_t::OK;
    Publish( ws, prepared );
    EditorHistory_NotifyChanged( &ws->history );
    return true;
}
bool KeepSelection( const map_workspace_t &ws, vector_t<u64> &out ) noexcept
{
    if ( !Vector_Reserve( &out, ws.selection.ids.nCount ) ) { return false; }
    for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) { if ( !Vector_PushBack( &out, ws.selection.ids.pData[i] ) ) { return false; } }
    return true;
}

bool EntityEditReady( const map_workspace_t *ws ) noexcept
{
    return ws != nullptr && ws->pGui != nullptr && ws->pDocument != nullptr && !ws->pDocument->bReadOnly &&
        ws->elementMode != map_element_mode_t::VERTICES && ws->elementMode != map_element_mode_t::EDGES && ws->selection.ids.nCount != 0u &&
        !ws->editPreview.bActive && !EditorHistory_IsTransactionOpen( &ws->history );
}

// Retained component roots are inspection state, not a substitute for vertex
// or edge edit adapters. Faces keep their established dedicated edits.
bool RootEditModeAvailable( const map_workspace_t *ws ) noexcept
{
    return ws != nullptr && ws->elementMode != map_element_mode_t::VERTICES &&
        ws->elementMode != map_element_mode_t::EDGES && ws->elementMode != map_element_mode_t::NAVIGATION;
}

template <typename Apply>
bool EditEntityMetadata( map_workspace_t *ws, const char *label, Apply apply ) noexcept
{
    if ( !EntityEditReady( ws ) ) { return false; }
    vector_t<u64> owners{};
    if ( !Vector_Init( &owners, ws->pDocument->pAllocator ) ||
         MapEntityEdit_ResolveOwners( ws->pDocument, Selected( *ws ), &owners ) != map_status_t::OK ||
         !MapWorkspace_AreEditTargetsUnlocked( ws, { owners.pData, owners.nCount } ) ) { return false; }
    bool changed = false;
    return Edit( ws, label, [&]( map_document_t *copy, vector_t<u64> &out ) {
        // Preserve the selected geometry and its component identity even when
        // the keys belong to its owner; ownership resolution is only a target.
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return apply( copy, span_t<const u64>{ owners.pData, owners.nCount }, &changed );
    }, &changed );
}
}

bool MapWorkspace_AreEditTargetsUnlocked( const map_workspace_t *ws, span_t<const u64> ids, bool expandHierarchy ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || !Span_IsValid( ids ) ) { return false; }
    if ( !MapLayers_HasLocked( ws ) ) { return true; }
    bool hasEntityRoot = false;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        if ( MapLayers_IsObjectLocked( ws, ids.pData[i] ) ) { return false; }
        if ( expandHierarchy ) {
            const auto *object = MapWireframe_FindObject( ws->wire, ids.pData[i] );
            // Only entity roots own geometry or have attached children. A
            // surface/instance drag needs no whole-map hierarchy scan. Missing
            // wire entries still take the authoritative Core lookup below.
            hasEntityRoot |= object == nullptr || object->kind == map_wire_kind_t::ENTITY;
        }
    }
    if ( !expandHierarchy || !hasEntityRoot ) { return true; }
    vector_t<u64> affected{};
    if ( ws->pGui == nullptr || !Vector_Init( &affected, ws->pGui->pAllocator ) ||
         MapEdit_CollectAffectedObjects( ws->pDocument, ids, &affected ) != map_status_t::OK ) { return false; }
    for ( usize i = 0; i < affected.nCount; ++i ) {
        if ( MapLayers_IsObjectLocked( ws, affected.pData[i] ) ) { return false; }
    }
    return true;
}

bool MapWorkspace_CanEditEntityProperties( const map_workspace_t *ws ) noexcept
{
    if ( !EntityEditReady( ws ) ) { return false; }
    vector_t<u64> owners{};
    return Vector_Init( &owners, ws->pDocument->pAllocator ) &&
        MapEntityEdit_ResolveOwners( ws->pDocument, Selected( *ws ), &owners ) == map_status_t::OK &&
        MapWorkspace_AreEditTargetsUnlocked( ws, { owners.pData, owners.nCount } );
}

bool MapWorkspace_SetEntityProperty( map_workspace_t *ws, string_view_t key, const key_value_t *value ) noexcept
{
    return EditEntityMetadata( ws, "Set Entity Property", [&]( map_document_t *copy, span_t<const u64> owners, bool *changed ) {
        return MapEntityEdit_SetProperty( copy, owners, key, value, changed );
    } );
}

bool MapWorkspace_RenameEntityProperty( map_workspace_t *ws, string_view_t key, string_view_t newKey ) noexcept
{
    return EditEntityMetadata( ws, "Rename Entity Property", [&]( map_document_t *copy, span_t<const u64> owners, bool *changed ) {
        return MapEntityEdit_RenameProperty( copy, owners, key, newKey, changed );
    } );
}

bool MapWorkspace_RemoveEntityProperty( map_workspace_t *ws, string_view_t key ) noexcept
{
    return EditEntityMetadata( ws, "Remove Entity Property", [&]( map_document_t *copy, span_t<const u64> owners, bool *changed ) {
        return MapEntityEdit_RemoveProperty( copy, owners, key, changed );
    } );
}

bool MapWorkspace_SetEntityIdentityField( map_workspace_t *ws, string_view_t field, string_view_t value ) noexcept
{
    return EditEntityMetadata( ws, "Set Entity Identity", [&]( map_document_t *copy, span_t<const u64> owners, bool *changed ) {
        return MapEntityEdit_SetIdentityField( copy, owners, field, value, changed );
    } );
}

bool MapWorkspace_CanEditSelection( const map_workspace_t *ws ) noexcept
{
    if ( !RootEditModeAvailable( ws ) || ws->pDocument == nullptr || ws->pDocument->bReadOnly || ws->selection.ids.nCount == 0u ) { return false; }
    if ( !MapWorkspace_AreEditTargetsUnlocked( ws, Selected( *ws ), false ) ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        // Arbitrary terrain rotation cannot be represented by the height-field format.
        // Entity rotation/scale require per-class property semantics, not helper boxes.
        if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ||
             ( ws->elementMode == map_element_mode_t::MESHES && !MapWorkspace_IsSelectableObject( ws, *object ) ) ||
             ( object->kind != map_wire_kind_t::BRUSH && object->kind != map_wire_kind_t::MESH && object->kind != map_wire_kind_t::PATCH ) ) { return false; }
    }
    return true;
}
bool MapWorkspace_CanTurnSelection( const map_workspace_t *ws ) noexcept
{
    if ( !RootEditModeAvailable( ws ) || ws->pDocument == nullptr || ws->pDocument->bReadOnly || ws->selection.ids.nCount == 0u ) { return false; }
    if ( !MapWorkspace_AreEditTargetsUnlocked( ws, Selected( *ws ), true ) ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        // A height field cannot turn (its cells stay square and level).
        if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) || object->kind == map_wire_kind_t::TERRAIN ||
             ( ws->elementMode == map_element_mode_t::MESHES && !MapWorkspace_IsSelectableObject( ws, *object ) ) ) { return false; }
    }
    return true;
}
bool MapWorkspace_CanMoveSelection( const map_workspace_t *ws ) noexcept {
    if ( !RootEditModeAvailable( ws ) || ws->pDocument == nullptr || ws->pDocument->bReadOnly || ws->selection.ids.nCount == 0 ) { return false; }
    if ( !MapWorkspace_AreEditTargetsUnlocked( ws, Selected( *ws ), true ) ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ||
             ( ws->elementMode == map_element_mode_t::MESHES && !MapWorkspace_IsSelectableObject( ws, *object ) ) ) { return false; }
        if ( object->kind == map_wire_kind_t::ENTITY ) {
            const auto *entity = MapWireframe_FindEntity( ws->wire, object->id );
            if ( entity == nullptr || ( !entity->bHasOrigin && entity->nOwned == 0 ) ) { return false; }
        } else if ( object->kind != map_wire_kind_t::BRUSH && object->kind != map_wire_kind_t::MESH && object->kind != map_wire_kind_t::PATCH &&
                    object->kind != map_wire_kind_t::PREFAB ) { return false; } // An instance moves by its origin.
    }
    return true;
}
bool MapWorkspace_CanEditBrushSelection( const map_workspace_t *ws ) noexcept {
    if ( !MapWorkspace_CanEditSelection( ws ) ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object->kind != map_wire_kind_t::BRUSH ) { return false; }
    } return true;
}
// The Clip tool cuts brushes and meshes alike (a mixed selection in one step).
bool MapWorkspace_CanClipSelection( const map_workspace_t *ws ) noexcept {
    if ( !MapWorkspace_CanEditSelection( ws ) ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object->kind != map_wire_kind_t::BRUSH && object->kind != map_wire_kind_t::MESH ) { return false; }
    } return true;
}
bool MapWorkspace_CanMergeBrushSelection( const map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_CanEditBrushSelection( ws ) || ws->selection.ids.nCount != 2u ||
         !MapBrushEdit_CanEdit( ws->pDocument, Selected( *ws ) ) ) { return false; }
    const map_geometry_record_t *placement[2]{};
    const auto &records = ws->pDocument->geometryRecords; // Sorted by authored ID.
    for ( usize operand = 0; operand < 2; ++operand ) {
        const u64 id = ws->selection.ids.pData[operand];
        usize low = 0, high = records.nCount;
        while ( low < high ) {
            const usize middle = low + ( high - low ) / 2;
            if ( records.pData[middle].id < id ) { low = middle + 1; } else { high = middle; }
        }
        if ( low < records.nCount && records.pData[low].id == id ) { placement[operand] = &records.pData[low]; }
    }
    return placement[0] != nullptr && placement[1] != nullptr &&
           placement[0]->iLayer < ws->pDocument->layers.nCount && placement[1]->iLayer < ws->pDocument->layers.nCount &&
           placement[0]->iLayer == placement[1]->iLayer && placement[0]->owner == placement[1]->owner;
}
map_primitive_desc_t MapWorkspace_PrimitiveDefaults( const map_workspace_t *ws, const map_bounds_t &bounds ) noexcept
{
    map_primitive_desc_t desc{};
    desc.bounds = bounds.box;
    if ( ws == nullptr || ws->pGui == nullptr ) { return desc; }
    const auto &settings = ws->pGui->settings;
    const auto shape = EditorSettings_Text( &settings, "editor.map.new_brush_shape", StringView_FromCString( "box" ) );
    if ( StringView_Equals( shape, StringView_FromCString( "wedge" ) ) ) { desc.kind = map_primitive_kind_t::WEDGE; }
    else if ( StringView_Equals( shape, StringView_FromCString( "cylinder" ) ) ) { desc.kind = map_primitive_kind_t::CYLINDER; }
    else if ( StringView_Equals( shape, StringView_FromCString( "spike" ) ) ) { desc.kind = map_primitive_kind_t::CONE; }
    else if ( StringView_Equals( shape, StringView_FromCString( "sphere" ) ) ) { desc.kind = map_primitive_kind_t::SPHERE; }
    else if ( StringView_Equals( shape, StringView_FromCString( "quad" ) ) ) { desc.kind = map_primitive_kind_t::QUAD; }
    else if ( StringView_Equals( shape, StringView_FromCString( "arch" ) ) ) { desc.kind = map_primitive_kind_t::ARCH; }
    else if ( StringView_Equals( shape, StringView_FromCString( "stairs" ) ) ) { desc.kind = map_primitive_kind_t::STAIRS; }
    else if ( StringView_Equals( shape, StringView_FromCString( "spiral_stairs" ) ) ) { desc.kind = map_primitive_kind_t::SPIRAL_STAIRS; }
    else if ( StringView_Equals( shape, StringView_FromCString( "pipe" ) ) ) { desc.kind = map_primitive_kind_t::PIPE; }
    else if ( StringView_Equals( shape, StringView_FromCString( "torus" ) ) ) { desc.kind = map_primitive_kind_t::TORUS; }
    else if ( !StringView_Equals( shape, StringView_FromCString( "box" ) ) ) { desc.kind = map_primitive_kind_t::COUNT; }
    const auto axis = EditorSettings_Text( &settings, "editor.map.primitive_axis", StringView_FromCString( "Z" ) );
    desc.axis = StringView_Equals( axis, StringView_FromCString( "X" ) ) ? 0u : StringView_Equals( axis, StringView_FromCString( "Y" ) ) ? 1u : 2u;
    desc.wedgeSlopeAxis = desc.axis;
    desc.wedgeCutAxis = ( desc.axis + 1u ) % 3u;
    desc.nSides = static_cast<u32>( EditorSettings_Integer( &settings, "editor.map.cylinder_sides", 16 ) );
    desc.sphereSubdivisions = static_cast<u32>( EditorSettings_Integer( &settings, "editor.map.sphere_subdivisions", 1 ) );
    desc.coneTopRadiusRatio = EditorSettings_Real( &settings, "editor.map.cone_top_radius", 0.0 );
    // This preserves the former 128-unit mapping at the 0.25 default.
    // No source texture resolution is inferred from an asset name.
    desc.worldUnitsPerUv = 512.0 * EditorSettings_Real( &settings, "editor.map.default_texture_scale", 0.25 );
    // Hammer 5's mesh shapes.
    desc.wallThickness = EditorSettings_Real( &settings, "editor.map.arch_thickness", 16.0 );
    desc.arcDegrees = EditorSettings_Real( &settings, "editor.map.arch_angle", 180.0 );
    desc.startDegrees = EditorSettings_Real( &settings, "editor.map.arch_start", 0.0 );
    desc.nSteps = static_cast<u32>( EditorSettings_Integer( &settings, "editor.map.stairs_steps", 8 ) );
    const auto rise = EditorSettings_Text( &settings, "editor.map.stairs_direction", StringView_FromCString( "+X" ) );
    if ( rise.cchLength == 2u ) {
        const i32 axisCode = rise.pData[1] == 'X' ? 1 : rise.pData[1] == 'Y' ? 2 : 3;
        desc.stairsDirection = rise.pData[0] == '-' ? -axisCode : axisCode;
    }
    desc.turnDegrees = EditorSettings_Real( &settings, "editor.map.spiral_turn", 360.0 );
    desc.innerRadius = EditorSettings_Real( &settings, "editor.map.spiral_inner", 16.0 );
    desc.tubeRatio = EditorSettings_Real( &settings, "editor.map.torus_tube", 0.25 );
    return desc;
}
namespace
{
bool CreatePrimitive( map_workspace_t *ws, const map_bounds_t &bounds, const map_primitive_desc_t &desc,
                      string_view_t capturedMaterial = {} ) noexcept
{
    if ( !bounds.bHas ) { return false; }
    const char *name = desc.kind == map_primitive_kind_t::BOX ? "Create Box" : desc.kind == map_primitive_kind_t::WEDGE ? "Create Wedge" :
                       desc.kind == map_primitive_kind_t::CYLINDER ? "Create Cylinder" : desc.kind == map_primitive_kind_t::CONE ? "Create Spike" :
                       desc.kind == map_primitive_kind_t::QUAD ? "Create Quad" : desc.kind == map_primitive_kind_t::ARCH ? "Create Arch" :
                       desc.kind == map_primitive_kind_t::STAIRS ? "Create Stairs" : desc.kind == map_primitive_kind_t::SPIRAL_STAIRS ? "Create Spiral Stairs" :
                       desc.kind == map_primitive_kind_t::PIPE ? "Create Pipe" : desc.kind == map_primitive_kind_t::TORUS ? "Create Torus" : "Create Sphere";
    return Edit( ws, name, [&]( map_document_t *copy, vector_t<u64> &out ) {
        u64 id = 0u;
        const auto material = capturedMaterial.pData != nullptr ? capturedMaterial :
            EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
        const bool meshOutput = StringView_Equals( EditorSettings_Text( &ws->pGui->settings, "editor.map.block_output", StringView_FromCString( "brush" ) ), StringView_FromCString( "mesh" ) );
        const bool directSphere = meshOutput && desc.kind == map_primitive_kind_t::SPHERE;
        auto status = directSphere ? MapEdit_CreateSphereMesh( copy, desc, material, {}, &id ) : MapEdit_CreatePrimitive( copy, desc, material, {}, &id );
        // Hammer 5 blocks are meshes; Quake-style maps keep convex brushes.
        // Conversion keeps the root ID, materials and UVs in the same step.
        if ( status == map_status_t::OK && !directSphere && desc.kind != map_primitive_kind_t::QUAD && !MapPrimitive_IsMesh( desc.kind ) && meshOutput ) {
            vector_t<u64> roots{};
            status = Vector_Init( &roots, copy->pAllocator ) ? MapMeshEdit_ConvertBrushes( copy, { &id, 1 }, &roots ) : map_status_t::OUT_OF_MEMORY;
            Vector_Shutdown( &roots );
        }
        if ( status == map_status_t::OK && EditorSettings_Bool( &ws->pGui->settings, "editor.map.select_created", CY_TRUE ) ) {
            if ( !Vector_PushBack( &out, id ) ) { return map_status_t::OUT_OF_MEMORY; }
        } else if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return status;
    } );
}
}
bool MapWorkspace_CreateBox( map_workspace_t *ws, const map_bounds_t &bounds ) noexcept
{
    auto desc = MapWorkspace_PrimitiveDefaults( ws, bounds );
    desc.kind = map_primitive_kind_t::BOX;
    return CreatePrimitive( ws, bounds, desc );
}
bool MapWorkspace_CreatePrimitive( map_workspace_t *ws, const map_bounds_t &bounds ) noexcept
{
    return CreatePrimitive( ws, bounds, MapWorkspace_PrimitiveDefaults( ws, bounds ) );
}
bool MapWorkspace_CreatePrimitive( map_workspace_t *ws, const map_primitive_desc_t &primitive ) noexcept
{
    map_bounds_t bounds{}; bounds.bHas = CY_TRUE; bounds.box = primitive.bounds;
    return CreatePrimitive( ws, bounds, primitive );
}
bool MapWorkspace_CanCreateTerrain( const map_workspace_t *ws, const map_terrain_create_desc_t &desc ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pGui->pAllocator == nullptr || ws->pDocument == nullptr ||
         ws->pDocument->pAllocator == nullptr || ws->pDocument->bReadOnly || ws->editPreview.bActive ||
         EditorHistory_IsTransactionOpen( &ws->history ) || ws->pDocument->layers.nCount == 0u ||
         ws->pDocument->nextId == 0u || ws->pDocument->geometry.revision == CY_U64_MAX ||
         geometry::GeometrySourceIdAllocator_IsExhausted( &ws->pDocument->geometry.sourceIds.allocator ) ||
         !Finite( desc.origin ) || !std::isfinite( desc.cellSize ) || desc.cellSize < 1.0 || desc.cellSize > 8192.0 ||
         desc.cellsX < MAP_TERRAIN_TILE_CELLS || desc.cellsX > MAP_TERRAIN_MAX_CELLS || desc.cellsX % MAP_TERRAIN_TILE_CELLS != 0u ||
         desc.cellsY < MAP_TERRAIN_TILE_CELLS || desc.cellsY > MAP_TERRAIN_MAX_CELLS || desc.cellsY % MAP_TERRAIN_TILE_CELLS != 0u ) { return false; }
    const f64 limit = geometry::kHeightFieldCoordinateMax;
    if ( std::abs( desc.origin.x ) > limit || std::abs( desc.origin.y ) > limit || std::abs( desc.origin.z ) > limit ||
         std::abs( desc.origin.x + desc.cellSize * desc.cellsX ) > limit ||
         std::abs( desc.origin.y + desc.cellSize * desc.cellsY ) > limit ) { return false; }
    const u64 first = std::max( ws->pDocument->nextId, ws->pDocument->geometry.sourceIds.allocator.next.value );
    const u64 identities = 1u + static_cast<u64>( desc.cellsX / MAP_TERRAIN_TILE_CELLS ) * ( desc.cellsY / MAP_TERRAIN_TILE_CELLS );
    // The map needs a nonzero next_id after allocating the root and tiles.
    return identities <= CY_U64_MAX - first;
}
bool MapWorkspace_CreateTerrain( map_workspace_t *ws, const map_terrain_create_desc_t &desc ) noexcept
{
    if ( !MapWorkspace_CanCreateTerrain( ws, desc ) ) { return false; }
    return Edit( ws, "Create Terrain", [&]( map_document_t *copy, vector_t<u64> &out ) {
        namespace geo = geometry;
        geo::geometry_source_id_allocator_t ids{ { std::max( copy->nextId, copy->geometry.sourceIds.allocator.next.value ) } };
        const auto root = geo::GeometrySourceIdAllocator_Allocate( &ids );
        geo::heightfield_t field{};
        auto status = root.status;
        if ( status == geo::geometry_status_t::OK ) {
            status = geo::HeightField_TryInit( &field, copy->pAllocator, desc.origin, desc.cellSize,
                desc.cellsX, desc.cellsY, MAP_TERRAIN_TILE_CELLS, root.id, &ids );
        }
        if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryDocument_TryAddHeightField( &copy->geometry, &field ); }
        // The document stores its own clone; no temporary backing arrays survive.
        geo::HeightField_Shutdown( &field );
        if ( status != geo::geometry_status_t::OK ) {
            return status == geo::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY :
                status == geo::geometry_status_t::LIMIT_EXCEEDED || status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ?
                    map_status_t::LIMIT_EXCEEDED : map_status_t::GEOMETRY_FAILED;
        }
        copy->nextId = ids.next.value;
        const auto placement = MapDocument_SetGeometryLayer( copy, root.id.value, StringView_FromCString( copy->layers.pData[0].id ) );
        if ( placement != map_status_t::OK ) { return placement; }
        ++copy->geometry.revision;
        if ( EditorSettings_Bool( &ws->pGui->settings, "editor.map.select_created", CY_TRUE ) ) {
            if ( !Vector_PushBack( &out, root.id.value ) ) { return map_status_t::OUT_OF_MEMORY; }
        } else if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return map_status_t::OK;
    } );
}
bool MapWorkspace_TranslateSelection( map_workspace_t *ws, math::vec3d_t delta, bool clone ) noexcept
{
    if ( !MapWorkspace_CanMoveSelection( ws ) || !Finite( delta ) || ( !clone && delta.x == 0 && delta.y == 0 && delta.z == 0 ) ) { return false; }
    if ( clone ) { return MapWorkspace_DuplicateSelection( ws, delta ); }
    return Edit( ws, "Translate", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapEdit_Translate( copy, Selected( *ws ), delta, ws->bTextureLock );
    } );
}
bool MapWorkspace_ResizeSelection( map_workspace_t *ws, math::vec3d_t sides, math::vec3d_t delta, bool fromCenter ) noexcept
{
    if ( !MapWorkspace_CanEditSelection( ws ) || ws->elementMode == map_element_mode_t::VERTICES ||
         ws->elementMode == map_element_mode_t::EDGES || ws->elementMode == map_element_mode_t::FACES || !Finite( delta ) ||
         ( delta.x == 0 && delta.y == 0 && delta.z == 0 ) ) { return false; }
    const map_bounds_resize_t resize{ sides, delta, fromCenter ? CY_TRUE : CY_FALSE };
    return Edit( ws, "Resize Each Object", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapEdit_Resize( copy, Selected( *ws ), resize,
            EditorSettings_Bool( &ws->pGui->settings, "editor.map.texture_scale_lock", CY_FALSE ) );
    } );
}
bool MapWorkspace_ScaleSelection( map_workspace_t *ws, math::vec3d_t factors, math::vec3d_t pivot ) noexcept
{
    if ( !MapWorkspace_CanTurnSelection( ws ) || !Finite( factors ) || !Finite( pivot ) || factors.x <= 0 || factors.y <= 0 || factors.z <= 0 ||
         ( factors.x == 1 && factors.y == 1 && factors.z == 1 ) ) { return false; }
    return Edit( ws, "Scale", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapEdit_Scale( copy, Selected( *ws ), factors, pivot,
            EditorSettings_Bool( &ws->pGui->settings, "editor.map.texture_scale_lock", CY_FALSE ) );
    } );
}
bool MapWorkspace_RotateSelection( map_workspace_t *ws, math::vec3d_t degrees, math::vec3d_t pivot ) noexcept
{
    if ( !MapWorkspace_CanTurnSelection( ws ) || !Finite( degrees ) || !Finite( pivot ) || ( degrees.x == 0 && degrees.y == 0 && degrees.z == 0 ) ) { return false; }
    return Edit( ws, "Rotate", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapEdit_Rotate( copy, Selected( *ws ), degrees, pivot, ws->bTextureLock );
    } );
}
bool MapWorkspace_DeleteSelection( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_CanMoveSelection( ws ) ) { return false; }
    return Edit( ws, "Delete", [&]( map_document_t *copy, vector_t<u64> & ) { return MapEdit_Delete( copy, Selected( *ws ) ); } );
}
bool MapWorkspace_DuplicateSelection( map_workspace_t *ws, math::vec3d_t offset ) noexcept
{
    if ( !MapWorkspace_CanMoveSelection( ws ) || !Finite( offset ) ) { return false; }
    return Edit( ws, "Duplicate", [&]( map_document_t *copy, vector_t<u64> &out ) { return MapEdit_Duplicate( copy, Selected( *ws ), offset, &out ); } );
}
bool MapWorkspace_CanCopySelection( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || ws->selection.ids.nCount == 0 ||
         ws->editPreview.bActive || EditorHistory_IsTransactionOpen( &ws->history ) ||
         ( ws->elementMode != map_element_mode_t::OBJECTS && ws->elementMode != map_element_mode_t::GROUPS && ws->elementMode != map_element_mode_t::MESHES ) ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const u64 id = ws->selection.ids.pData[i];
        const auto *object = MapWireframe_FindObject( ws->wire, id );
        // Meshes addresses brush/mesh roots, including owned geometry, but
        // never widens that set to an entity bundle or a different surface.
        if ( ws->elementMode == map_element_mode_t::MESHES &&
             ( object == nullptr || !MapWorkspace_IsSelectableObject( ws, *object ) ) ) { return false; }
        if ( object == nullptr ) {
            // Origin-less entities still carry authored key values and outputs.
            if ( MapWireframe_FindEntity( ws->wire, id ) == nullptr ) { return false; }
        } else if ( !MapWorkspace_IsVisible( ws, *object ) ||
                    ( object->kind != map_wire_kind_t::BRUSH && object->kind != map_wire_kind_t::MESH &&
                      object->kind != map_wire_kind_t::PATCH && object->kind != map_wire_kind_t::TERRAIN && object->kind != map_wire_kind_t::ENTITY ) ) { return false; }
    }
    return true;
}
bool MapWorkspace_CanCutSelection( const map_workspace_t *ws ) noexcept
{
    return MapWorkspace_CanCopySelection( ws ) && !ws->pDocument->bReadOnly &&
        MapWorkspace_AreEditTargetsUnlocked( ws, Selected( *ws ), true );
}
bool MapWorkspace_CanPaste( const map_workspace_t *ws ) noexcept
{
    // Bundles can contain entities and other surface kinds. Meshes does not
    // yet have a filtered paste adapter; explicit component-mode paste keeps
    // its established transition back to the general Objects workflow.
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || ws->pDocument->bReadOnly ||
         ws->elementMode == map_element_mode_t::MESHES ||
         ws->editPreview.bActive || EditorHistory_IsTransactionOpen( &ws->history ) || QGuiApplication::instance() == nullptr ) { return false; }
    const auto *clipboard = QApplication::clipboard();
    const auto *mime = clipboard != nullptr ? clipboard->mimeData() : nullptr;
    if ( mime == nullptr || !mime->hasFormat( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ) ) { return false; }
    const QByteArray bytes = mime->data( QString::fromLatin1( MAP_CLIPBOARD_MIME ) );
    return !bytes.isEmpty() && static_cast<usize>( bytes.size() ) <= MAP_CLIPBOARD_TEXT_MAX;
}
namespace
{
std::unique_ptr<QMimeData> CopyMime( const map_workspace_t &ws )
{
    text_buffer_t buffer{};
    if ( !TextBuffer_Init( &buffer, ws.pGui->pAllocator ) ||
         MapClipboard_WriteSelection( ws.pDocument, Selected( ws ), &buffer ) != map_status_t::OK ) { return {}; }
    auto mime = std::unique_ptr<QMimeData>( new ( std::nothrow ) QMimeData() );
    if ( mime != nullptr ) {
        const auto text = TextBuffer_View( &buffer );
        mime->setData( QString::fromLatin1( MAP_CLIPBOARD_MIME ), QByteArray( text.pData, static_cast<qsizetype>( text.cchLength ) ) );
        // Text is useful for inspecting/recovering the authored bundle. Map paste
        // requires our MIME type; ordinary copied text never creates geometry.
        mime->setText( QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) );
    }
    return mime;
}
math::vec3d_t PasteOffset( const map_workspace_t &ws, const map_bounds_t &bounds, bool inPlace ) noexcept
{
    math::vec3d_t offset{};
    if ( inPlace ) { return offset; }
    const auto preference = EditorSettings_Text( &ws.pGui->settings, "editor.map.paste_offset", StringView_FromCString( "grid" ) );
    const u32 axes = ws.cursorAxes != 0 ? ws.cursorAxes : 3u;
    if ( StringView_Equals( preference, StringView_FromCString( "grid" ) ) ) {
        if ( axes & 1u ) { offset.x = ws.gridSize; }
        if ( axes & 2u ) { offset.y = ws.gridSize; }
        if ( axes & 4u ) { offset.z = ws.gridSize; }
    } else if ( StringView_Equals( preference, StringView_FromCString( "cursor" ) ) && bounds.bHas && ws.cursorAxes != 0 ) {
        const math::vec3d_t center{ ( bounds.box.minimum.x + bounds.box.maximum.x ) * 0.5,
                                   ( bounds.box.minimum.y + bounds.box.maximum.y ) * 0.5,
                                   ( bounds.box.minimum.z + bounds.box.maximum.z ) * 0.5 };
        auto target = ws.cursor;
        if ( ws.bSnapToGrid && ws.gridSize > 0 && std::isfinite( ws.gridSize ) ) {
            target = { std::round( target.x / ws.gridSize ) * ws.gridSize, std::round( target.y / ws.gridSize ) * ws.gridSize, std::round( target.z / ws.gridSize ) * ws.gridSize };
        }
        if ( axes & 1u ) { offset.x = target.x - center.x; }
        if ( axes & 2u ) { offset.y = target.y - center.y; }
        if ( axes & 4u ) { offset.z = target.z - center.z; }
    }
    return offset;
}
}
bool MapWorkspace_CopySelection( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_CanCopySelection( ws ) || QGuiApplication::instance() == nullptr ) { return false; }
    auto mime = CopyMime( *ws );
    auto *clipboard = QApplication::clipboard();
    if ( mime == nullptr || clipboard == nullptr ) { return false; }
    clipboard->setMimeData( mime.release() );
    return true;
}
bool MapWorkspace_CutSelection( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_CanCutSelection( ws ) || QGuiApplication::instance() == nullptr ) { return false; }
    auto mime = CopyMime( *ws );
    auto *clipboard = QApplication::clipboard();
    if ( mime == nullptr || clipboard == nullptr ) { return false; }
    // Complete all fallible engine preparation before replacing the clipboard.
    // Failed deletion preserves both the live map and the previous MIME data.
    if ( !Edit( ws, "Cut", [&]( map_document_t *copy, vector_t<u64> & ) { return MapEdit_Delete( copy, Selected( *ws ) ); } ) ) { return false; }
    clipboard->setMimeData( mime.release() );
    return true;
}
bool MapWorkspace_Paste( map_workspace_t *ws, bool inPlace ) noexcept
{
    if ( !MapWorkspace_CanPaste( ws ) ) { return false; }
    // Hold an independent QByteArray before publication/clipboard notifications.
    const QByteArray bytes = QApplication::clipboard()->mimeData()->data( QString::fromLatin1( MAP_CLIPBOARD_MIME ) );
    const string_view_t text{ bytes.constData(), static_cast<usize>( bytes.size() ) };
    map_bounds_t bounds{};
    if ( MapClipboard_ReadBounds( ws->pGui->pAllocator, text, &bounds ) != map_status_t::OK ) { return false; }
    const auto offset = PasteOffset( *ws, bounds, inPlace );
    if ( !Finite( offset ) || !Edit( ws, inPlace ? "Paste In Place" : "Paste", [&]( map_document_t *copy, vector_t<u64> &out ) {
        return MapClipboard_Paste( copy, text, offset, &out );
    } ) ) { return false; }
    MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    return true;
}
bool MapWorkspace_HasBrushFace( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->selectedBrushFaceObject == 0 || ws->selectedBrushFaceSide == 0 ||
         ws->selection.ids.nCount != 1 || ws->selection.ids.pData[0] != ws->selectedBrushFaceObject ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, ws->selectedBrushFaceObject );
    if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) || MapLayers_IsObjectLocked( ws, ws->selectedBrushFaceObject ) ) { return false; }
    const auto *brush = geometry::GeometryDocument_FindBrush( &ws->pDocument->geometry, { ws->selectedBrushFaceObject } );
    if ( brush == nullptr ) { return false; }
    for ( usize i = 0; i < brush->sides.nCount; ++i ) { if ( brush->sides.pData[i].sourceId.value == ws->selectedBrushFaceSide ) { return true; } }
    return false;
}
void MapWorkspace_ClearBrushFace( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ( ws->selectedBrushFaceObject == 0 && ws->selectedBrushFaceSide == 0 ) ) { return; }
    ws->selectedBrushFaceObject = 0; ws->selectedBrushFaceSide = 0;
    ++ws->selection.revision;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
}
void MapWorkspace_SelectBrushFace( map_workspace_t *ws, u64 object, u64 side ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || object == 0 || side == 0 ) { return; }
    const auto *brush = geometry::GeometryDocument_FindBrush( &ws->pDocument->geometry, { object } );
    bool exists = false;
    if ( brush != nullptr ) { for ( usize i = 0; i < brush->sides.nCount; ++i ) { exists |= brush->sides.pData[i].sourceId.value == side; } }
    const auto *wireObject = MapWireframe_FindObject( ws->wire, object );
    if ( !exists || wireObject == nullptr || !MapWorkspace_IsVisible( ws, *wireObject ) || MapLayers_IsObjectLocked( ws, object ) ) { return; }
    const bool sameRoot = ws->selection.ids.nCount == 1 && ws->selection.ids.pData[0] == object;
    if ( sameRoot && ws->selectedBrushFaceObject == object && ws->selectedBrushFaceSide == side &&
         ws->selectedMeshFaceObject == 0 && ws->meshSelection.meshId.value == 0 && ws->elementMode == map_element_mode_t::FACES ) { return; }
    vector_t<u64> roots{};
    if ( !sameRoot && !CopySelection( roots, { &object, 1 }, ws->pGui->pAllocator ) ) { return; }
    // Prepare the inspection root before publishing a face or notifying panes.
    // A failed root allocation must leave the previous component context intact.
    if ( !sameRoot ) { Vector_Shutdown( &ws->selection.ids ); Vector_Move( &ws->selection.ids, &roots ); }
    ws->selectedMeshFaceObject = 0; ws->selectedMeshFaceId = 0;
    Vector_Clear( &ws->meshFaceSet ); ws->meshFaceSetObject = 0;
    geometry::MeshSelection_Shutdown( &ws->meshSelection ); ws->selectedMeshEdgeSeed = {};
    ws->selectedBrushFaceObject = object; ws->selectedBrushFaceSide = side;
    ws->elementMode = map_element_mode_t::FACES;
    ++ws->selection.revision;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW );
}
bool MapWorkspace_PushPullFace( map_workspace_t *ws, f64 distance ) noexcept
{
    if ( !MapWorkspace_HasBrushFace( ws ) || !std::isfinite( distance ) || distance == 0 ) { return false; }
    return Edit( ws, "Push/Pull Face", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapFaceEdit_PushPull( copy, ws->selectedBrushFaceObject, ws->selectedBrushFaceSide, distance );
    } );
}
bool MapWorkspace_HasFacePreview( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->pDocument->bReadOnly || !MapWorkspace_HasBrushFace( ws ) ) { return false; }
    const auto &preview = ws->editPreview;
    return preview.bActive && preview.bFacePushPull && preview.pFaceDocument == ws->pDocument &&
        preview.faceObject == ws->selectedBrushFaceObject && preview.faceSide == ws->selectedBrushFaceSide &&
        preview.documentRevision == ws->pDocument->geometry.revision && preview.selectionRevision == ws->selection.revision &&
        preview.tool == ws->tool && preview.mode == ws->elementMode &&
        ( ws->tool == map_tool_t::EXTRUDE || ( ws->tool == map_tool_t::SELECT && ws->elementMode == map_element_mode_t::FACES ) ) &&
        MapWorkspace_PreviewVisibilityMatches( ws, preview.visibility );
}
void MapWorkspace_SetFacePreview( map_workspace_t *ws, f64 distance ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->pDocument->bReadOnly || !MapWorkspace_HasBrushFace( ws ) ||
         !( ws->tool == map_tool_t::EXTRUDE || ( ws->tool == map_tool_t::SELECT && ws->elementMode == map_element_mode_t::FACES ) ) || distance == 0.0 ) {
        MapWorkspace_ClearEditPreview( ws ); return;
    }
    if ( MapWorkspace_HasFacePreview( ws ) && ws->editPreview.status == map_status_t::OK &&
         ws->editPreview.faceDistance == distance && ws->editPreviewWire.faces.nCount != 0 ) { return; }
    map_edit_preview_t preview{};
    preview.bActive = preview.bFacePushPull = CY_TRUE;
    preview.pFaceDocument = ws->pDocument;
    preview.faceObject = ws->selectedBrushFaceObject; preview.faceSide = ws->selectedBrushFaceSide;
    preview.faceDistance = distance; preview.tool = ws->tool; preview.mode = ws->elementMode;
    preview.documentRevision = ws->pDocument->geometry.revision; preview.selectionRevision = ws->selection.revision;
    preview.visibility = MapWorkspace_PreviewVisibility( ws );
    preview.status = map_status_t::INVALID_ARGUMENT;
    map_document_t copy{};
    map_wireframe_t wire{};
    if ( std::isfinite( distance ) ) {
        preview.status = MapDocument_Create( &copy, ws->pDocument->pAllocator,
            { StringView_FromCString( "Face preview" ), StringView_FromCString( MAP_WORKSPACE_DEFAULT_GAME ), {} } );
        if ( preview.status == map_status_t::OK ) {
            copy.geometryPolicy = ws->pDocument->geometryPolicy;
            copy.geometry.policy = ws->pDocument->geometry.policy;
            geometry::brush_source_t source{};
            auto status = geometry::GeometryDocument_TryCopyBrushSource( &ws->pDocument->geometry,
                { preview.faceObject }, copy.pAllocator, &source );
            if ( status == geometry::geometry_status_t::OK ) { status = geometry::GeometryDocument_TryAddBrushSource( &copy.geometry, &source ); }
            geometry::BrushSource_Shutdown( &source );
            if ( status != geometry::geometry_status_t::OK ) {
                preview.status = status == geometry::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED;
            }
        }
        if ( preview.status == map_status_t::OK ) { preview.status = MapFaceEdit_PushPull( &copy, preview.faceObject, preview.faceSide, distance ); }
        if ( preview.status == map_status_t::OK && !MapWireframe_Init( &wire, copy.pAllocator ) ) { preview.status = map_status_t::OUT_OF_MEMORY; }
        if ( preview.status == map_status_t::OK ) { preview.status = MapWireframe_Build( &wire, copy ); }
        if ( preview.status == map_status_t::OK && ( !wire.bounds.bHas || wire.nBrokenBrushes != 0 || wire.faces.nCount < 4 ) ) {
            preview.status = map_status_t::GEOMETRY_FAILED;
        }
        if ( preview.status == map_status_t::OK ) { preview.bounds = wire.bounds; MoveWire( ws->editPreviewWire, wire ); }
    }
    if ( preview.status != map_status_t::OK ) { MapWireframe_Shutdown( &ws->editPreviewWire ); }
    // Both types also own their arrays through their destructors; release
    // temporary geometry before observers receive the retained candidate.
    MapWireframe_Shutdown( &wire );
    MapDocument_Shutdown( &copy );
    ws->editPreview = preview;
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
}
bool MapWorkspace_CommitFacePreview( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_HasFacePreview( ws ) || ws->editPreview.status != map_status_t::OK || ws->editPreviewWire.faces.nCount == 0 ) { return false; }
    const f64 distance = ws->editPreview.faceDistance;
    const bool committed = MapWorkspace_PushPullFace( ws, distance );
    MapWorkspace_ClearEditPreview( ws );
    return committed;
}
bool MapWorkspace_HasMeshFace( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->selectedMeshFaceObject == 0 || ws->selectedMeshFaceId == 0 ||
         ws->selection.ids.nCount != 1 || ws->selection.ids.pData[0] != ws->selectedMeshFaceObject ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, ws->selectedMeshFaceObject );
    if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) || MapLayers_IsObjectLocked( ws, ws->selectedMeshFaceObject ) ) { return false; }
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { ws->selectedMeshFaceObject } );
    geometry::geometry_mesh_face_handle_t face{};
    return mesh != nullptr && geometry::MeshSource_TryFindFace( mesh, { ws->selectedMeshFaceId }, &face );
}
void MapWorkspace_ClearMeshFace( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ( ws->selectedMeshFaceObject == 0 && ws->selectedMeshFaceId == 0 ) ) { return; }
    ws->selectedMeshFaceObject = 0; ws->selectedMeshFaceId = 0;
    Vector_Clear( &ws->meshFaceSet ); ws->meshFaceSetObject = 0;
    ++ws->selection.revision;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
}
void MapWorkspace_SelectMeshFace( map_workspace_t *ws, u64 object, u64 id ) noexcept
{
    ( void )MapWorkspace_SetMeshFaces( ws, object, { &id, 1 }, id );
}

namespace
{
bool FaceSetValid( const map_workspace_t *ws ) noexcept
{
    return ws->meshFaceSetObject != 0 && ws->meshFaceSetObject == ws->selectedMeshFaceObject && ws->meshFaceSet.nCount != 0 &&
           std::binary_search( ws->meshFaceSet.pData, ws->meshFaceSet.pData + ws->meshFaceSet.nCount, ws->selectedMeshFaceId );
}

bool MeshFaceExists( const map_workspace_t *ws, u64 object, u64 face ) noexcept
{
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { object } );
    geometry::geometry_mesh_face_handle_t handle{};
    return mesh != nullptr && face != 0 && geometry::MeshSource_TryFindFace( mesh, { face }, &handle );
}

} // namespace

span_t<const u64> MapWorkspace_MeshFaces( const map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_HasMeshFace( ws ) ) { return {}; }
    if ( FaceSetValid( ws ) ) { return { ws->meshFaceSet.pData, ws->meshFaceSet.nCount }; }
    return { &ws->selectedMeshFaceId, 1 };
}

bool MapWorkspace_IsMeshFaceSelected( const map_workspace_t *ws, u64 object, u64 face ) noexcept
{
    if ( ws == nullptr || face == 0 || object == 0 || object != ws->selectedMeshFaceObject ) { return false; }
    if ( face == ws->selectedMeshFaceId ) { return true; }
    return FaceSetValid( ws ) && std::binary_search( ws->meshFaceSet.pData, ws->meshFaceSet.pData + ws->meshFaceSet.nCount, face );
}

bool MapWorkspace_AddMeshFace( map_workspace_t *ws, u64 object, u64 face ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || !MapWorkspace_HasMeshFace( ws ) || object != ws->selectedMeshFaceObject ||
         !MeshFaceExists( ws, object, face ) ) { return false; }
    if ( MapWorkspace_IsMeshFaceSelected( ws, object, face ) ) {
        if ( ws->selectedMeshFaceId != face ) {
            ws->selectedMeshFaceId = face;
            ++ws->selection.revision;
            MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
        }
        return true;
    }
    // Seed a private set, not the live vector: either allocation can fail.
    const auto current = MapWorkspace_MeshFaces( ws );
    vector_t<u64> next{};
    if ( !CopySelection( next, current, ws->pGui->pAllocator ) || !Vector_PushBack( &next, face ) ) { return false; }
    return MapWorkspace_SetMeshFaces( ws, object, { next.pData, next.nCount }, face );
}

bool MapWorkspace_RemoveMeshFace( map_workspace_t *ws, u64 object, u64 face ) noexcept
{
    if ( ws == nullptr || !MapWorkspace_IsMeshFaceSelected( ws, object, face ) ) { return false; }
    if ( !FaceSetValid( ws ) || ws->meshFaceSet.nCount == 1 ) { MapWorkspace_ClearMeshFace( ws ); return true; }
    u64 *end = ws->meshFaceSet.pData + ws->meshFaceSet.nCount;
    u64 *at = std::lower_bound( ws->meshFaceSet.pData, end, face );
    std::rotate( at, at + 1, end );
    --ws->meshFaceSet.nCount;
    if ( ws->selectedMeshFaceId == face ) { ws->selectedMeshFaceId = ws->meshFaceSet.pData[ws->meshFaceSet.nCount - 1]; }
    ++ws->selection.revision;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
    return true;
}

bool MapWorkspace_SetMeshFaces( map_workspace_t *ws, u64 object, span_t<const u64> faces, u64 active ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || object == 0 || !Span_IsValid( faces ) || faces.nCount == 0 ) { return false; }
    const auto *wire = MapWireframe_FindObject( ws->wire, object );
    if ( wire == nullptr || wire->kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( ws, *wire ) || MapLayers_IsObjectLocked( ws, object ) ) { return false; }
    bool hasActive = false;
    for ( usize i = 0; i < faces.nCount; ++i ) {
        if ( !MeshFaceExists( ws, object, faces.pData[i] ) ) { return false; }
        hasActive |= faces.pData[i] == active;
    }
    if ( !hasActive ) { return false; }
    const bool sameRoot = ws->selection.ids.nCount == 1 && ws->selection.ids.pData[0] == object;
    const auto current = MapWorkspace_MeshFaces( ws );
    const bool sameContext = sameRoot && ws->selectedMeshFaceObject == object && ws->selectedMeshFaceId == active &&
        ws->selectedBrushFaceObject == 0 && ws->meshSelection.meshId.value == 0 && ws->elementMode == map_element_mode_t::FACES;
    if ( sameContext && current.nCount == faces.nCount && std::equal( faces.pData, faces.pData + faces.nCount, current.pData ) ) { return true; }
    // Callers may pass the borrowed live set. Validate and canonicalize its
    // private copy before touching either the face IDs or their inspection root.
    vector_t<u64> next{}, roots{};
    span_t<const u64> wanted = faces;
    if ( faces.nCount > 1 ) {
        if ( !CopySelection( next, faces, ws->pGui->pAllocator ) ) { return false; }
        std::sort( next.pData, next.pData + next.nCount );
        next.nCount = static_cast<usize>( std::unique( next.pData, next.pData + next.nCount ) - next.pData );
        wanted = { next.pData, next.nCount };
    }
    if ( sameContext && current.nCount == wanted.nCount && std::equal( wanted.pData, wanted.pData + wanted.nCount, current.pData ) ) { return true; }
    if ( !sameRoot && !CopySelection( roots, { &object, 1 }, ws->pGui->pAllocator ) ) { return false; }

    // Everything fallible is ready. Publish once; listeners never observe a
    // new root with the old face set, or a partially prepared replacement.
    if ( !sameRoot ) { Vector_Shutdown( &ws->selection.ids ); Vector_Move( &ws->selection.ids, &roots ); }
    if ( wanted.nCount > 1 ) {
        Vector_Shutdown( &ws->meshFaceSet ); Vector_Move( &ws->meshFaceSet, &next );
        ws->meshFaceSetObject = object;
    } else { Vector_Clear( &ws->meshFaceSet ); ws->meshFaceSetObject = 0; }
    ws->selectedBrushFaceObject = 0; ws->selectedBrushFaceSide = 0;
    ws->selectedMeshFaceObject = object; ws->selectedMeshFaceId = active;
    geometry::MeshSelection_Shutdown( &ws->meshSelection ); ws->selectedMeshEdgeSeed = {};
    ws->elementMode = map_element_mode_t::FACES;
    ++ws->selection.revision;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW );
    return true;
}

namespace
{
bool ComponentPickTool( map_tool_t tool ) noexcept
{
    return tool == map_tool_t::SELECT || tool == map_tool_t::TRANSLATE ||
           tool == map_tool_t::ROTATE || tool == map_tool_t::SCALE;
}

bool SameEdge( geometry::mesh_edge_ref_t a, geometry::mesh_edge_ref_t b ) noexcept
{
    return a.a.value == b.a.value && a.b.value == b.b.value;
}

const geometry::mesh_source_t *SelectedComponentMesh( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->meshSelection.meshId.value == 0 ||
         ws->selection.ids.nCount != 1 || ws->selection.ids.pData[0] != ws->meshSelection.meshId.value ) { return nullptr; }
    const auto *wire = MapWireframe_FindObject( ws->wire, ws->meshSelection.meshId.value );
    return wire != nullptr && wire->kind == map_wire_kind_t::MESH && MapWorkspace_IsVisible( ws, *wire ) &&
        !MapLayers_IsObjectLocked( ws, ws->meshSelection.meshId.value ) ?
        geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, ws->meshSelection.meshId ) : nullptr;
}

bool PrepareEdges( geometry::mesh_selection_t &next, const map_workspace_t &ws, u64 object, bool keep ) noexcept
{
    if ( geometry::MeshSelection_Init( &next, ws.pGui->pAllocator, { object } ) != geometry::geometry_status_t::OK ) { return false; }
    if ( !keep ) { return true; }
    if ( !Vector_Reserve( &next.edges, ws.meshSelection.edges.nCount ) ) { return false; }
    for ( usize i = 0; i < ws.meshSelection.edges.nCount; ++i ) {
        if ( !Vector_PushBack( &next.edges, ws.meshSelection.edges.pData[i] ) ) { return false; }
    }
    return true;
}

bool PrepareVertices( geometry::mesh_selection_t &next, const map_workspace_t &ws, u64 object, bool keep ) noexcept
{
    if ( geometry::MeshSelection_Init( &next, ws.pGui->pAllocator, { object } ) != geometry::geometry_status_t::OK ) { return false; }
    if ( !keep ) { return true; }
    if ( !Vector_Reserve( &next.vertices, ws.meshSelection.vertices.nCount ) ) { return false; }
    for ( usize i = 0; i < ws.meshSelection.vertices.nCount; ++i ) {
        if ( !Vector_PushBack( &next.vertices, ws.meshSelection.vertices.pData[i] ) ) { return false; }
    }
    return true;
}

void PublishMeshComponents( map_workspace_t *ws, geometry::mesh_selection_t &next, geometry::mesh_edge_ref_t seed ) noexcept
{
    geometry::MeshSelection_Shutdown( &ws->meshSelection );
    Vector_Move( &ws->meshSelection.vertices, &next.vertices );
    Vector_Move( &ws->meshSelection.edges, &next.edges );
    Vector_Move( &ws->meshSelection.faces, &next.faces );
    ws->meshSelection.meshId = next.meshId;
    ws->selectedMeshEdgeSeed = seed;
    ++ws->selection.revision;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
}

bool SelectEdgeTopology( map_workspace_t *ws, bool loop ) noexcept
{
    if ( !MapWorkspace_CanSelectMeshEdgeTopology( ws ) ) { return false; }
    const auto *mesh = SelectedComponentMesh( ws );
    geometry::mesh_selection_t next{};
    if ( !PrepareEdges( next, *ws, ws->meshSelection.meshId.value, true ) ) { return false; }
    // The geometry query inserts each result separately. Work privately so a
    // later failed insertion cannot publish a partially selected loop/ring.
    const auto status = loop ? geometry::MeshSelection_TrySelectEdgeLoop( &next, mesh, ws->selectedMeshEdgeSeed ) :
        geometry::MeshSelection_TrySelectEdgeRing( &next, mesh, ws->selectedMeshEdgeSeed );
    if ( status != geometry::geometry_status_t::OK ) { return false; }
    if ( next.edges.nCount != ws->meshSelection.edges.nCount ) { PublishMeshComponents( ws, next, ws->selectedMeshEdgeSeed ); }
    return true;
}
}

bool MapWorkspace_HasMeshVertices( const map_workspace_t *ws ) noexcept
{
    const auto *mesh = SelectedComponentMesh( ws );
    if ( mesh == nullptr || ws->elementMode != map_element_mode_t::VERTICES || ws->meshSelection.vertices.nCount == 0 ||
         ws->meshSelection.edges.nCount != 0 || ws->meshSelection.faces.nCount != 0 || ws->selectedMeshEdgeSeed.a.value != 0 ) { return false; }
    for ( usize i = 0; i < ws->meshSelection.vertices.nCount; ++i ) {
        geometry::geometry_mesh_vertex_handle_t handle{};
        if ( !geometry::MeshSource_TryFindVertex( mesh, ws->meshSelection.vertices.pData[i], &handle ) ) { return false; }
    }
    return true;
}

bool MapWorkspace_SelectMeshVertex( map_workspace_t *ws, u64 object, geometry::geometry_source_id_t vertex, map_select_mode_t mode ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || object == 0 ||
         ws->elementMode != map_element_mode_t::VERTICES || !ComponentPickTool( ws->tool ) || ws->editPreview.bActive ||
         EditorHistory_IsTransactionOpen( &ws->history ) ||
         ( mode != MAP_SELECT_REPLACE && mode != MAP_SELECT_ADD && mode != MAP_SELECT_TOGGLE && mode != MAP_SELECT_REMOVE ) ) { return false; }
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { object } );
    const auto *wire = MapWireframe_FindObject( ws->wire, object );
    geometry::geometry_mesh_vertex_handle_t handle{};
    if ( mesh == nullptr || wire == nullptr || wire->kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( ws, *wire ) || MapLayers_IsObjectLocked( ws, object ) ||
         !geometry::MeshSource_TryFindVertex( mesh, vertex, &handle ) ) { return false; }
    const bool sameMesh = ws->meshSelection.meshId.value == object;
    if ( mode != MAP_SELECT_REPLACE && !sameMesh && ws->meshSelection.vertices.nCount != 0 ) { return false; }
    if ( mode == MAP_SELECT_REMOVE && !sameMesh ) { return false; }
    geometry::mesh_selection_t next{};
    if ( !PrepareVertices( next, *ws, object, sameMesh && mode != MAP_SELECT_REPLACE ) ) { return false; }
    const bool remove = mode == MAP_SELECT_REMOVE || ( mode == MAP_SELECT_TOGGLE && geometry::MeshSelection_HasVertex( &next, vertex ) );
    if ( remove ) { ( void )geometry::MeshSelection_RemoveVertex( &next, vertex ); }
    else if ( geometry::MeshSelection_TryAddVertex( &next, vertex ) != geometry::geometry_status_t::OK ) { return false; }
    const bool sameRoots = ws->selection.ids.nCount == 1 && ws->selection.ids.pData[0] == object;
    const bool sameComponents = sameMesh && next.vertices.nCount == ws->meshSelection.vertices.nCount &&
        ( next.vertices.nCount == 0 || std::equal( next.vertices.pData, next.vertices.pData + next.vertices.nCount,
            ws->meshSelection.vertices.pData, []( auto a, auto b ) { return a.value == b.value; } ) ) &&
        ws->meshSelection.edges.nCount == 0 && ws->meshSelection.faces.nCount == 0 && ws->selectedMeshEdgeSeed.a.value == 0 &&
        ws->selectedBrushFaceObject == 0 && ws->selectedMeshFaceObject == 0;
    if ( sameRoots && sameComponents ) { return true; }
    vector_t<u64> roots{};
    if ( !sameRoots && ( !Vector_Init( &roots, ws->pGui->pAllocator, 1 ) || !Vector_PushBack( &roots, object ) ) ) { return false; }
    // Components and the matching inspection root are ready before either moves.
    if ( !sameRoots ) {
        Vector_Shutdown( &ws->selection.ids ); Vector_Move( &ws->selection.ids, &roots );
    }
    ws->selectedBrushFaceObject = 0; ws->selectedBrushFaceSide = 0;
    ws->selectedMeshFaceObject = 0; ws->selectedMeshFaceId = 0;
    PublishMeshComponents( ws, next, {} );
    return true;
}

bool MapWorkspace_HasMeshEdges( const map_workspace_t *ws ) noexcept
{
    const auto *mesh = SelectedComponentMesh( ws );
    if ( mesh == nullptr || ws->elementMode != map_element_mode_t::EDGES || ws->meshSelection.edges.nCount == 0 ||
         ws->meshSelection.vertices.nCount != 0 || ws->meshSelection.faces.nCount != 0 ) { return false; }
    for ( usize i = 0; i < ws->meshSelection.edges.nCount; ++i ) {
        const auto edge = ws->meshSelection.edges.pData[i];
        geometry::geometry_mesh_edge_handle_t handle{};
        if ( !geometry::MeshSourceEdit_TryFindEdge( mesh, edge.a, edge.b, &handle ) ) { return false; }
    }
    return true;
}

bool MapWorkspace_SelectMeshEdge( map_workspace_t *ws, u64 object, geometry::mesh_edge_ref_t edge, map_select_mode_t mode ) noexcept
{
    // Edge Arc picks the chain it bends itself.
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || object == 0 ||
         ws->elementMode != map_element_mode_t::EDGES || ( !ComponentPickTool( ws->tool ) && ws->tool != map_tool_t::EDGE_ARC ) || ws->editPreview.bActive ||
         EditorHistory_IsTransactionOpen( &ws->history ) ||
         ( mode != MAP_SELECT_REPLACE && mode != MAP_SELECT_ADD && mode != MAP_SELECT_TOGGLE && mode != MAP_SELECT_REMOVE ) ) { return false; }
    edge = geometry::MeshEdgeRef_Make( edge.a, edge.b );
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { object } );
    const auto *wire = MapWireframe_FindObject( ws->wire, object );
    geometry::geometry_mesh_edge_handle_t handle{};
    if ( mesh == nullptr || wire == nullptr || wire->kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( ws, *wire ) || MapLayers_IsObjectLocked( ws, object ) ||
         !geometry::MeshSourceEdit_TryFindEdge( mesh, edge.a, edge.b, &handle ) ) { return false; }
    const bool sameMesh = ws->meshSelection.meshId.value == object;
    if ( mode != MAP_SELECT_REPLACE && !sameMesh && ws->meshSelection.edges.nCount != 0 ) { return false; }
    if ( mode == MAP_SELECT_REMOVE && !sameMesh ) { return false; }
    geometry::mesh_selection_t next{};
    if ( !PrepareEdges( next, *ws, object, sameMesh && mode != MAP_SELECT_REPLACE ) ) { return false; }
    const bool remove = mode == MAP_SELECT_REMOVE || ( mode == MAP_SELECT_TOGGLE && geometry::MeshSelection_HasEdge( &next, edge ) );
    if ( remove ) { ( void )geometry::MeshSelection_RemoveEdge( &next, edge ); }
    else if ( geometry::MeshSelection_TryAddEdge( &next, edge ) != geometry::geometry_status_t::OK ) { return false; }
    const auto seed = remove ? ( next.edges.nCount != 0 ? next.edges.pData[0] : geometry::mesh_edge_ref_t{} ) : edge;
    const bool sameRoots = ws->selection.ids.nCount == 1 && ws->selection.ids.pData[0] == object;
    const bool sameComponents = sameMesh && next.edges.nCount == ws->meshSelection.edges.nCount &&
        ( next.edges.nCount == 0 || std::equal( next.edges.pData, next.edges.pData + next.edges.nCount, ws->meshSelection.edges.pData, SameEdge ) ) &&
        SameEdge( seed, ws->selectedMeshEdgeSeed ) && ws->meshSelection.vertices.nCount == 0 && ws->meshSelection.faces.nCount == 0 &&
        ws->selectedBrushFaceObject == 0 && ws->selectedMeshFaceObject == 0;
    if ( sameRoots && sameComponents ) { return true; }
    vector_t<u64> roots{};
    if ( !sameRoots && ( !Vector_Init( &roots, ws->pGui->pAllocator, 1 ) || !Vector_PushBack( &roots, object ) ) ) { return false; }
    // Both sets are ready. From this point publication cannot allocate.
    if ( !sameRoots ) {
        Vector_Shutdown( &ws->selection.ids ); Vector_Move( &ws->selection.ids, &roots );
    }
    ws->selectedBrushFaceObject = 0; ws->selectedBrushFaceSide = 0;
    ws->selectedMeshFaceObject = 0; ws->selectedMeshFaceId = 0;
    PublishMeshComponents( ws, next, seed );
    return true;
}

// One publication for a whole component set (grow, select connected, the
// component gizmo's reselection), instead of one per added component.
bool MapWorkspace_SetMeshComponents( map_workspace_t *ws, u64 object, span_t<const geometry::geometry_source_id_t> vertices,
    span_t<const geometry::mesh_edge_ref_t> edges ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || object == 0 || !Span_IsValid( vertices ) || !Span_IsValid( edges ) ||
         ws->editPreview.bActive || EditorHistory_IsTransactionOpen( &ws->history ) ) { return false; }
    // Select and transform tools share component selection; Edge Arc keeps
    // the chain it edits.
    if ( ws->tool != map_tool_t::SELECT && ws->tool != map_tool_t::TRANSLATE && ws->tool != map_tool_t::ROTATE && ws->tool != map_tool_t::SCALE &&
         ws->tool != map_tool_t::EDGE_ARC ) { return false; }
    const bool vertexMode = ws->elementMode == map_element_mode_t::VERTICES, edgeMode = ws->elementMode == map_element_mode_t::EDGES;
    if ( ( vertexMode && edges.nCount != 0 ) || ( edgeMode && vertices.nCount != 0 ) || ( !vertexMode && !edgeMode ) ) { return false; }
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { object } );
    const auto *wire = MapWireframe_FindObject( ws->wire, object );
    if ( mesh == nullptr || wire == nullptr || wire->kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( ws, *wire ) || MapLayers_IsObjectLocked( ws, object ) ) { return false; }
    geometry::mesh_selection_t next{};
    if ( geometry::MeshSelection_Init( &next, ws->pGui->pAllocator, { object } ) != geometry::geometry_status_t::OK ) { return false; }
    bool ok = true;
    for ( usize i = 0; ok && i < vertices.nCount; ++i ) {
        geometry::geometry_mesh_vertex_handle_t handle{};
        ok = geometry::MeshSource_TryFindVertex( mesh, vertices.pData[i], &handle ) &&
             geometry::MeshSelection_TryAddVertex( &next, vertices.pData[i] ) == geometry::geometry_status_t::OK;
    }
    for ( usize i = 0; ok && i < edges.nCount; ++i ) {
        const auto edge = geometry::MeshEdgeRef_Make( edges.pData[i].a, edges.pData[i].b );
        geometry::geometry_mesh_edge_handle_t handle{};
        ok = geometry::MeshSourceEdit_TryFindEdge( mesh, edge.a, edge.b, &handle ) &&
             geometry::MeshSelection_TryAddEdge( &next, edge ) == geometry::geometry_status_t::OK;
    }
    vector_t<u64> roots{};
    const bool sameRoots = ws->selection.ids.nCount == 1 && ws->selection.ids.pData[0] == object;
    if ( ok && !sameRoots ) { ok = Vector_Init( &roots, ws->pGui->pAllocator, 1 ) && Vector_PushBack( &roots, object ); }
    if ( !ok ) { Vector_Shutdown( &roots ); geometry::MeshSelection_Shutdown( &next ); return false; }
    if ( !sameRoots ) { Vector_Shutdown( &ws->selection.ids ); Vector_Move( &ws->selection.ids, &roots ); }
    ws->selectedBrushFaceObject = 0; ws->selectedBrushFaceSide = 0;
    ws->selectedMeshFaceObject = 0; ws->selectedMeshFaceId = 0;
    // The loop/ring seed survives when it is still selected.
    const auto seed = geometry::MeshSelection_HasEdge( &next, ws->selectedMeshEdgeSeed ) ? ws->selectedMeshEdgeSeed :
                      next.edges.nCount != 0 ? next.edges.pData[0] : geometry::mesh_edge_ref_t{};
    PublishMeshComponents( ws, next, seed );
    geometry::MeshSelection_Shutdown( &next );
    return true;
}

bool MapWorkspace_RestorePreparedMeshComponents( map_workspace_t *ws, geometry::mesh_selection_t *prepared ) noexcept
{
    if ( ws == nullptr || prepared == nullptr || prepared == &ws->meshSelection || ws->pDocument == nullptr ||
         ws->selection.ids.nCount != 1 || ws->selection.ids.pData[0] != prepared->meshId.value ||
         prepared->faces.nCount != 0 || ws->editPreview.bActive || EditorHistory_IsTransactionOpen( &ws->history ) ) { return false; }
    const bool edges = ws->elementMode == map_element_mode_t::EDGES, vertices = ws->elementMode == map_element_mode_t::VERTICES;
    if ( ( !edges && !vertices ) || ( edges && prepared->vertices.nCount != 0 ) || ( vertices && prepared->edges.nCount != 0 ) ) { return false; }
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, prepared->meshId );
    const auto *wire = MapWireframe_FindObject( ws->wire, prepared->meshId.value );
    if ( mesh == nullptr || wire == nullptr || !MapWorkspace_IsVisible( ws, *wire ) || MapLayers_IsObjectLocked( ws, prepared->meshId.value ) ) { return false; }
    for ( usize i = 0; i < prepared->vertices.nCount; ++i ) {
        geometry::geometry_mesh_vertex_handle_t handle{};
        if ( !geometry::MeshSource_TryFindVertex( mesh, prepared->vertices.pData[i], &handle ) ||
             ( i != 0 && prepared->vertices.pData[i - 1].value >= prepared->vertices.pData[i].value ) ) { return false; }
    }
    for ( usize i = 0; i < prepared->edges.nCount; ++i ) {
        const auto edge = prepared->edges.pData[i];
        geometry::geometry_mesh_edge_handle_t handle{};
        if ( edge.a.value >= edge.b.value || !geometry::MeshSourceEdit_TryFindEdge( mesh, edge.a, edge.b, &handle ) ) { return false; }
        if ( i != 0 ) {
            const auto prior = prepared->edges.pData[i - 1];
            if ( prior.a.value > edge.a.value || ( prior.a.value == edge.a.value && prior.b.value >= edge.b.value ) ) { return false; }
        }
    }
    ws->selectedBrushFaceObject = ws->selectedBrushFaceSide = ws->selectedMeshFaceObject = ws->selectedMeshFaceId = 0;
    const auto seed = prepared->edges.nCount != 0 ? prepared->edges.pData[0] : geometry::mesh_edge_ref_t{};
    PublishMeshComponents( ws, *prepared, seed );
    return true;
}

bool MapWorkspace_CanSelectMeshEdgeTopology( const map_workspace_t *ws ) noexcept
{
    return MapWorkspace_HasMeshEdges( ws ) && ws->pGui != nullptr && ws->elementMode == map_element_mode_t::EDGES &&
        ws->tool == map_tool_t::SELECT && !ws->editPreview.bActive && !EditorHistory_IsTransactionOpen( &ws->history ) &&
        geometry::MeshSelection_HasEdge( &ws->meshSelection, ws->selectedMeshEdgeSeed );
}

bool MapWorkspace_SelectMeshEdgeLoop( map_workspace_t *ws ) noexcept { return SelectEdgeTopology( ws, true ); }
bool MapWorkspace_SelectMeshEdgeRing( map_workspace_t *ws ) noexcept { return SelectEdgeTopology( ws, false ); }

bool MapWorkspace_CanEditMeshFace( const map_workspace_t *ws ) noexcept
{
    return MapWorkspace_HasMeshFace( ws ) && ws->pGui != nullptr && ws->elementMode == map_element_mode_t::FACES &&
        !ws->pDocument->bReadOnly && !ws->editPreview.bActive && !EditorHistory_IsTransactionOpen( &ws->history );
}
bool MapWorkspace_ExtrudeMeshFace( map_workspace_t *ws, f64 distance ) noexcept
{
    if ( !MapWorkspace_CanEditMeshFace( ws ) || !std::isfinite( distance ) || distance <= 0 ) { return false; }
    const u64 mesh = ws->selectedMeshFaceObject;
    // Freeze the whole selection before cloning or publishing. The active
    // face remains the continuation cap, not a substitute for the region.
    struct face_snapshot_t {
        vector_t<u64> ids{};
        ~face_snapshot_t() { Vector_Shutdown( &ids ); }
    } faces;
    if ( !Vector_Init( &faces.ids, ws->pGui->pAllocator ) || !Vector_Append( &faces.ids, MapWorkspace_MeshFaces( ws ) ) ) {
        edit_report_scope_t report( ws );
        return false;
    }
    if ( faces.ids.nCount == 0 ) { return false; }
    return Edit( ws, faces.ids.nCount == 1 ? "Extrude Mesh Face" : "Extrude Mesh Face Region", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return faces.ids.nCount == 1 ? MapMeshEdit_ExtrudeFace( copy, mesh, faces.ids.pData[0], distance ) :
            MapTopologyEdit_ExtrudeFaceRegionBy( copy, mesh, { faces.ids.pData, faces.ids.nCount }, distance );
    } );
}
bool MapWorkspace_InsetMeshFace( map_workspace_t *ws, f64 margin ) noexcept
{
    if ( !MapWorkspace_CanEditMeshFace( ws ) || MapWorkspace_MeshFaces( ws ).nCount != 1 || !std::isfinite( margin ) || margin <= 0 ) { return false; }
    return Edit( ws, "Inset Mesh Face", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapMeshEdit_InsetFace( copy, ws->selectedMeshFaceObject, ws->selectedMeshFaceId, margin );
    } );
}
bool MapWorkspace_InsetMeshFaceByEdgeWidth( map_workspace_t *ws, f64 width ) noexcept
{
    if ( !MapWorkspace_CanEditMeshFace( ws ) || MapWorkspace_MeshFaces( ws ).nCount != 1 || !std::isfinite( width ) || width <= 0 ) { return false; }
    const u64 root = ws->selectedMeshFaceObject, face = ws->selectedMeshFaceId;
    return Edit( ws, "Inset Face (Width)", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapMeshEdit_InsetFaceByEdgeWidth( copy, root, face, width );
    } );
}
bool MapWorkspace_CanQuadSliceMeshFace( const map_workspace_t *ws, u32 cellsU, u32 cellsV ) noexcept
{
    if ( !MapWorkspace_CanEditMeshFace( ws ) || cellsU < 1 || cellsV < 1 || cellsU > 64 || cellsV > 64 ||
         ( cellsU == 1 && cellsV == 1 ) ) { return false; }
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i];
        if ( face.id == ws->selectedMeshFaceObject && face.faceId == ws->selectedMeshFaceId ) { return face.nIndices == 4; }
    }
    return false;
}
bool MapWorkspace_QuadSliceMeshFace( map_workspace_t *ws, u32 cellsU, u32 cellsV ) noexcept
{
    if ( !MapWorkspace_CanQuadSliceMeshFace( ws, cellsU, cellsV ) ) { return false; }
    return Edit( ws, "Quad Slice Mesh Face", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapMeshEdit_QuadSliceFace( copy, ws->selectedMeshFaceObject, ws->selectedMeshFaceId, cellsU, cellsV );
    } );
}
bool MapWorkspace_ApplyFaceMaterial( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_HasBrushFace( ws ) ) { return false; }
    return Edit( ws, "Apply Face Material", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        const auto material = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
        return MapFaceEdit_SetMaterial( copy, ws->selectedBrushFaceObject, ws->selectedBrushFaceSide, material );
    } );
}
namespace {
bool WholeObjectEditReady( const map_workspace_t *ws ) noexcept
{
    return ws != nullptr && ws->pGui != nullptr && !ws->editPreview.bActive &&
        ( ws->elementMode == map_element_mode_t::OBJECTS || ws->elementMode == map_element_mode_t::GROUPS ||
          ws->elementMode == map_element_mode_t::MESHES ) &&
        !EditorHistory_IsTransactionOpen( &ws->history );
}
}
bool MapWorkspace_CanConvertBrushSelection( const map_workspace_t *ws ) noexcept
{
    return WholeObjectEditReady( ws ) && MapWorkspace_CanEditBrushSelection( ws );
}
bool MapWorkspace_CanEditMeshSelection( const map_workspace_t *ws ) noexcept
{
    if ( !WholeObjectEditReady( ws ) || !MapWorkspace_CanEditSelection( ws ) ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object->kind != map_wire_kind_t::MESH ) { return false; }
    }
    return true;
}
bool MapWorkspace_CanTriangulateMeshSelection( const map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_CanEditMeshSelection( ws ) ) { return false; }
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i];
        if ( face.nIndices > 3 && EditorSelection_Contains( &ws->selection, face.id ) ) { return true; }
    }
    return false;
}
bool MapWorkspace_ConvertBrushSelection( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_CanConvertBrushSelection( ws ) ) { return false; }
    return Edit( ws, "Convert Brushes to Meshes", [&]( map_document_t *copy, vector_t<u64> &out ) {
        return MapMeshEdit_ConvertBrushes( copy, Selected( *ws ), &out );
    } );
}
bool MapWorkspace_ConvertBrushForComponents( map_workspace_t *ws, u64 id ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || ws->pDocument->bReadOnly || id == 0 ||
         ( ws->elementMode != map_element_mode_t::VERTICES && ws->elementMode != map_element_mode_t::EDGES ) ||
         !ComponentPickTool( ws->tool ) || ws->editPreview.bActive || EditorHistory_IsTransactionOpen( &ws->history ) ||
         !EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, id );
    if ( object == nullptr || object->kind != map_wire_kind_t::BRUSH || !MapWorkspace_IsVisible( ws, *object ) ||
         MapLayers_IsObjectLocked( ws, id ) ) { return false; }
    return Edit( ws, "Convert Brush to Mesh", [&]( map_document_t *copy, vector_t<u64> &out ) {
        vector_t<u64> roots{};
        if ( !Vector_Init( &roots, copy->pAllocator ) || !Vector_PushBack( &out, id ) ) { return map_status_t::OUT_OF_MEMORY; }
        const auto status = MapMeshEdit_ConvertBrushes( copy, { &id, 1 }, &roots );
        Vector_Shutdown( &roots );
        return status;
    } );
}
bool MapWorkspace_FlipMeshNormals( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_CanEditMeshSelection( ws ) ) { return false; }
    return Edit( ws, "Flip Mesh Normals", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapMeshEdit_FlipNormals( copy, Selected( *ws ) );
    } );
}
bool MapWorkspace_TriangulateMeshSelection( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_CanTriangulateMeshSelection( ws ) ) { return false; }
    return Edit( ws, "Triangulate Meshes", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapMeshEdit_Triangulate( copy, Selected( *ws ) );
    } );
}
bool MapWorkspace_HollowSelection( map_workspace_t *ws, f64 thickness ) noexcept
{
    if ( !MapWorkspace_CanEditBrushSelection( ws ) || !std::isfinite( thickness ) || thickness <= 0 ) { return false; }
    return Edit( ws, "Hollow Brushes", [&]( map_document_t *copy, vector_t<u64> &out ) {
        return MapBrushEdit_Hollow( copy, Selected( *ws ), thickness, &out );
    } );
}
bool MapWorkspace_MergeSelection( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || !MapWorkspace_CanMergeBrushSelection( ws ) ) { return false; }
    return Edit( ws, "Merge Brushes", [&]( map_document_t *copy, vector_t<u64> &out ) {
        return MapBrushEdit_Merge( copy, Selected( *ws ), &out );
    } );
}
bool MapWorkspace_ClipSelection( map_workspace_t *ws, math::planed_t plane ) noexcept
{
    if ( !MapWorkspace_CanEditBrushSelection( ws ) ) { return false; }
    return Edit( ws, "Clip Brushes", [&]( map_document_t *copy, vector_t<u64> &out ) {
        const auto material = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
        return MapBrushEdit_Clip( copy, Selected( *ws ), plane, material, &out );
    } );
}
map_brush_clip_mode_t MapWorkspace_ClipMode( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr ) { return map_brush_clip_mode_t::BOTH; }
    const auto mode = EditorSettings_Text( &ws->pGui->settings, "editor.map.clip_mode", StringView_FromCString( "both" ) );
    if ( StringView_Equals( mode, StringView_FromCString( "front" ) ) ) { return map_brush_clip_mode_t::FRONT; }
    if ( StringView_Equals( mode, StringView_FromCString( "back" ) ) ) { return map_brush_clip_mode_t::BACK; }
    return map_brush_clip_mode_t::BOTH;
}
u32 MapWorkspace_ClipAxis( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr ) { return 2u; }
    const auto axis = EditorSettings_Text( &ws->pGui->settings, "editor.map.clip_axis", StringView_FromCString( "z" ) );
    if ( StringView_Equals( axis, StringView_FromCString( "x" ) ) || StringView_Equals( axis, StringView_FromCString( "X" ) ) ) { return 0u; }
    if ( StringView_Equals( axis, StringView_FromCString( "y" ) ) || StringView_Equals( axis, StringView_FromCString( "Y" ) ) ) { return 1u; }
    return 2u;
}
namespace {
// The selected brushes and meshes, apart: each kind has its own clip.
void ClipTargets( const map_workspace_t &ws, std::vector<u64> &brushes, std::vector<u64> &meshes ) noexcept
{
    for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws.wire, ws.selection.ids.pData[i] );
        ( object != nullptr && object->kind == map_wire_kind_t::MESH ? meshes : brushes ).push_back( ws.selection.ids.pData[i] );
    }
}
map_slice_keep_t MeshClipKeep( map_brush_clip_mode_t mode ) noexcept
{
    return mode == map_brush_clip_mode_t::FRONT ? map_slice_keep_t::FRONT : mode == map_brush_clip_mode_t::BACK ? map_slice_keep_t::BACK : map_slice_keep_t::BOTH;
}
bool ClipSelection( map_workspace_t *ws, math::planed_t plane, map_brush_clip_mode_t mode, string_view_t material ) noexcept
{
    if ( !MapWorkspace_CanClipSelection( ws ) ) { return false; }
    std::vector<u64> brushes, meshes;
    ClipTargets( *ws, brushes, meshes );
    const bool both = mode == map_brush_clip_mode_t::BOTH;
    const char *label = meshes.empty() ? ( both ? "Slice Brushes" : "Clip Brushes" ) : brushes.empty() ? ( both ? "Slice Meshes" : "Clip Meshes" ) : ( both ? "Slice" : "Clip" );
    return Edit( ws, label, [&]( map_document_t *copy, vector_t<u64> &out ) {
        auto status = brushes.empty() ? map_status_t::OK : MapBrushEdit_ClipMode( copy, { brushes.data(), brushes.size() }, plane, mode, material, &out );
        const bool cap = EditorSettings_Bool( &ws->pGui->settings, "editor.map.clip_cap", CY_TRUE );
        if ( status == map_status_t::OK && !meshes.empty() ) { status = MapTopologyEdit_ClipMeshes( copy, { meshes.data(), meshes.size() }, plane, MeshClipKeep( mode ), &out, cap ); }
        return status;
    } );
}
bool SamePlane( math::planed_t a, math::planed_t b ) noexcept
{
    return math::Vec3d_EqualsExact( a.normal, b.normal ) && a.d == b.d;
}
bool SameGuide( const map_clip_guide_t &a, const map_clip_guide_t &b ) noexcept
{
    if ( a.bHas != b.bHas ) { return false; }
    return !a.bHas || ( a.extrusionAxis == b.extrusionAxis && math::Vec3d_EqualsExact( a.points[0], b.points[0] ) &&
        math::Vec3d_EqualsExact( a.points[1], b.points[1] ) );
}
f64 Coordinate( math::vec3d_t p, u32 axis ) noexcept { return axis == 0u ? p.x : axis == 1u ? p.y : p.z; }
bool GuidePlane( const map_workspace_t *ws, const map_clip_guide_t &guide, math::planed_t *pOut ) noexcept
{
    if ( !guide.bHas || guide.extrusionAxis > 2u || !Finite( guide.points[0] ) || !Finite( guide.points[1] ) ||
         Coordinate( guide.points[0], guide.extrusionAxis ) != Coordinate( guide.points[1], guide.extrusionAxis ) ) { return false; }
    const u32 u = guide.extrusionAxis == 0u ? 1u : 0u, v = guide.extrusionAxis == 2u ? 1u : 2u;
    const f64 du = Coordinate( guide.points[1], u ) - Coordinate( guide.points[0], u );
    const f64 dv = Coordinate( guide.points[1], v ) - Coordinate( guide.points[0], v );
    const f64 length = std::hypot( du, dv );
    if ( !std::isfinite( length ) || length <= ws->pDocument->geometryPolicy.numerical.fAbsoluteDistanceTolerance ) { return false; }
    math::vec3d_t normal{};
    if ( u == 0u ) { normal.x = dv / length; } else { normal.y = dv / length; }
    if ( v == 1u ) { normal.y = -du / length; } else { normal.z = -du / length; }
    const math::planed_t plane{ normal, -math::Vec3d_Dot( normal, guide.points[0] ) };
    if ( !math::Planed_IsFinite( plane ) || !math::Planed_IsNormalized( plane, 1e-8 ) ) { return false; }
    *pOut = plane; return true;
}
}
bool MapWorkspace_ClipSelection( map_workspace_t *ws, math::planed_t plane, map_brush_clip_mode_t mode ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr ) { return false; }
    return ClipSelection( ws, plane, mode, EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material",
        StringView_FromCString( "materials/dev/dev_grid.cymat" ) ) );
}
namespace {
void SetClipPreview( map_workspace_t *ws, math::planed_t plane, const map_clip_guide_t *pGuide ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->pGui == nullptr ) { return; }
    const auto mode = MapWorkspace_ClipMode( ws );
    const auto material = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
    const auto &old = ws->editPreview;
    // A numeric edit to an unchanged plane (including mode/material rebuilds)
    // keeps the viewport's anchors. A changed numeric plane has no such guide.
    const map_clip_guide_t guide = pGuide ? *pGuide : old.bActive && old.bClip && SamePlane( old.clipPlane, plane ) ? old.clipGuide : map_clip_guide_t{};
    const auto revision = ws->pDocument->geometry.revision;
    if ( MapWorkspace_CanClipSelection( ws ) && old.bActive && old.bClip && old.status == map_status_t::OK && old.documentRevision == revision &&
         old.selectionRevision == ws->selection.revision && old.clipMode == mode && old.bClipCap == EditorSettings_Bool( &ws->pGui->settings, "editor.map.clip_cap", CY_TRUE ) &&
         SamePlane( old.clipPlane, plane ) && StringView_Equals( material, StringView_FromCString( old.clipMaterial ) ) ) {
        // Moving anchors along the same infinite plane only changes their
        // presentation. Keep the exact ghost arrays and notify after metadata.
        if ( !SameGuide( old.clipGuide, guide ) ) {
            ws->editPreview.clipGuide = guide;
            MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
        }
        return;
    }
    map_edit_preview_t preview{};
    preview.bActive = preview.bClip = CY_TRUE;
    preview.clipPlane = plane; preview.clipMode = mode;
    preview.bClipCap = EditorSettings_Bool( &ws->pGui->settings, "editor.map.clip_cap", CY_TRUE );
    preview.clipGuide = guide;
    preview.documentRevision = revision; preview.selectionRevision = ws->selection.revision;
    preview.status = map_status_t::INVALID_ARGUMENT;
    if ( material.cchLength <= MAP_MATERIAL_PATH_MAX && StringView_IsValid( material ) ) {
        if ( material.cchLength != 0 ) { std::memcpy( preview.clipMaterial, material.pData, material.cchLength ); }
        if ( MapWorkspace_CanClipSelection( ws ) && math::Planed_IsNormalized( plane, 1e-8 ) ) {
            // Presentation copies only the selected authored solids. It does
            // not clone unrelated map records, entities or layers.
            std::vector<u64> brushes, meshes;
            ClipTargets( *ws, brushes, meshes );
            map_document_t copy{};
            vector_t<u64> roots{};
            map_wireframe_t wire{};
            preview.status = MapDocument_Create( &copy, ws->pDocument->pAllocator,
                { StringView_FromCString( "Clip preview" ), StringView_FromCString( MAP_WORKSPACE_DEFAULT_GAME ), {} } );
            if ( preview.status == map_status_t::OK ) {
                copy.geometryPolicy = ws->pDocument->geometryPolicy;
                copy.geometry.policy = ws->pDocument->geometry.policy;
                copy.nextId = ws->pDocument->nextId;
                for ( const u64 brush : brushes ) {
                    geometry::brush_source_t source{};
                    auto status = geometry::GeometryDocument_TryCopyBrushSource( &ws->pDocument->geometry, { brush }, copy.pAllocator, &source );
                    if ( status == geometry::geometry_status_t::OK ) { status = geometry::GeometryDocument_TryAddBrushSource( &copy.geometry, &source ); }
                    geometry::BrushSource_Shutdown( &source );
                    if ( status != geometry::geometry_status_t::OK ) {
                        preview.status = status == geometry::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED;
                        break;
                    }
                }
            }
            if ( preview.status == map_status_t::OK && !Vector_Init( &roots, copy.pAllocator ) ) { preview.status = map_status_t::OUT_OF_MEMORY; }
            if ( preview.status == map_status_t::OK && !brushes.empty() ) {
                preview.status = MapBrushEdit_ClipMode( &copy, { brushes.data(), brushes.size() }, plane, mode, material, &roots );
            }
            if ( preview.status == map_status_t::OK && !meshes.empty() ) {
                preview.status = MapTopologyEdit_AddClipPreview( &copy, ws->pDocument, { meshes.data(), meshes.size() }, plane, MeshClipKeep( mode ), preview.bClipCap != CY_FALSE );
            }
            if ( preview.status == map_status_t::OK && !MapWireframe_Init( &wire, copy.pAllocator ) ) { preview.status = map_status_t::OUT_OF_MEMORY; }
            if ( preview.status == map_status_t::OK ) { preview.status = MapWireframe_Build( &wire, copy ); }
            if ( preview.status == map_status_t::OK ) {
                preview.bounds = wire.bounds;
                MoveWire( ws->editPreviewWire, wire );
            }
            Vector_Shutdown( &roots );
        }
    }
    if ( preview.status != map_status_t::OK ) { MapWireframe_Shutdown( &ws->editPreviewWire ); }
    ws->editPreview = preview;
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
}
}
void MapWorkspace_SetClipPreview( map_workspace_t *ws, math::planed_t plane ) noexcept
{
    SetClipPreview( ws, plane, nullptr );
}
void MapWorkspace_SetClipGuide( map_workspace_t *ws, const map_clip_guide_t &guide ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->pGui == nullptr ) { return; }
    math::planed_t plane{ { 0, 0, 0 }, 0 };
    if ( GuidePlane( ws, guide, &plane ) ) { SetClipPreview( ws, plane, &guide ); }
    else {
        const map_clip_guide_t empty{};
        SetClipPreview( ws, plane, &empty );
    }
}
bool MapWorkspace_CommitClipPreview( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return false; }
    const auto preview = ws->editPreview;
    if ( !preview.bActive || !preview.bClip || preview.status != map_status_t::OK || ws->tool != map_tool_t::CLIP ||
         preview.documentRevision != ws->pDocument->geometry.revision || preview.selectionRevision != ws->selection.revision ) { return false; }
    const bool committed = ClipSelection( ws, preview.clipPlane, preview.clipMode, StringView_FromCString( preview.clipMaterial ) );
    MapWorkspace_ClearEditPreview( ws );
    return committed;
}
bool MapWorkspace_SubtractBrush( map_workspace_t *ws, u64 cutterId ) noexcept
{
    if ( !MapWorkspace_CanEditBrushSelection( ws ) || EditorSelection_Contains( &ws->selection, cutterId ) ) { return false; }
    const auto *cutter = MapWireframe_FindObject( ws->wire, cutterId );
    if ( cutter == nullptr || cutter->kind != map_wire_kind_t::BRUSH || !MapWorkspace_IsVisible( ws, *cutter ) ) { return false; }
    return Edit( ws, "Subtract Brush", [&]( map_document_t *copy, vector_t<u64> &out ) {
        return MapBrushEdit_Subtract( copy, Selected( *ws ), cutterId, &out );
    } );
}
namespace
{
f64 PrimitiveCoordinate( math::vec3d_t point, u32 axis ) noexcept { return axis == 0u ? point.x : axis == 1u ? point.y : point.z; }
void FlattenQuad( map_primitive_desc_t &desc ) noexcept
{
    if ( desc.kind != map_primitive_kind_t::QUAD || desc.axis > 2u ) { return; }
    // An inverted/non-finite input remains invalid instead of becoming valid
    // accidentally when its normal extent is collapsed for the display cache.
    const f64 low = PrimitiveCoordinate( desc.bounds.minimum, desc.axis );
    const f64 high = PrimitiveCoordinate( desc.bounds.maximum, desc.axis );
    if ( !std::isfinite( low ) || !std::isfinite( high ) || high < low ) { return; }
    if ( desc.axis == 0u ) { desc.bounds.maximum.x = low; }
    else if ( desc.axis == 1u ) { desc.bounds.maximum.y = low; }
    else { desc.bounds.maximum.z = low; }
}
bool SamePrimitive( const map_primitive_desc_t &a, const map_primitive_desc_t &b ) noexcept
{
    const auto equal = []( math::vec3d_t x, math::vec3d_t y ) { return x.x == y.x && x.y == y.y && x.z == y.z; };
    return equal( a.bounds.minimum, b.bounds.minimum ) && equal( a.bounds.maximum, b.bounds.maximum ) &&
        a.kind == b.kind && a.axis == b.axis && a.nSides == b.nSides && a.wedgeCutAxis == b.wedgeCutAxis &&
        a.wedgeSlopeAxis == b.wedgeSlopeAxis && a.sphereSubdivisions == b.sphereSubdivisions &&
        a.coneTopRadiusRatio == b.coneTopRadiusRatio && a.worldUnitsPerUv == b.worldUnitsPerUv &&
        a.wallThickness == b.wallThickness && a.arcDegrees == b.arcDegrees && a.startDegrees == b.startDegrees && a.nSteps == b.nSteps &&
        a.stairsDirection == b.stairsDirection && a.turnDegrees == b.turnDegrees && a.innerRadius == b.innerRadius && a.tubeRatio == b.tubeRatio;
}
bool BlockContextMatches( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || ws->pDocument->bReadOnly ) { return false; }
    const auto &preview = ws->editPreview;
    return preview.bActive && preview.bounds.bHas && preview.pBlockDocument == ws->pDocument &&
        ws->tool == map_tool_t::BLOCK && preview.tool == ws->tool && preview.mode == ws->elementMode &&
        preview.documentRevision == ws->pDocument->geometry.revision && preview.selectionRevision == ws->selection.revision &&
        MapWorkspace_PreviewVisibilityMatches( ws, preview.visibility );
}
bool SpherePreviewFitsBrushBudget( const map_workspace_t &ws, const map_primitive_desc_t &desc ) noexcept
{
    if ( desc.kind != map_primitive_kind_t::SPHERE || StringView_Equals( EditorSettings_Text( &ws.pGui->settings,
        "editor.map.block_output", StringView_FromCString( "brush" ) ), StringView_FromCString( "mesh" ) ) ) { return true; }
    const u32 faces = desc.sphereSubdivisions == 0u ? 128u : 240u;
    return faces <= MAP_BRUSH_FACES_MAX && faces <= ws.pDocument->geometryPolicy.limits.cBrushSidesPerBrushMax;
}
map_status_t BuildBlockPreview( map_workspace_t &ws, const map_primitive_desc_t &desc, string_view_t material, map_wireframe_t &wire ) noexcept
{
    // Only an empty private construction document is built. The live map and
    // its undo history are never touched while drawing or refining bounds.
    if ( !SpherePreviewFitsBrushBudget( ws, desc ) ) { return map_status_t::LIMIT_EXCEEDED; }
    map_document_t preview{};
    auto status = MapDocument_Create( &preview, ws.pDocument->pAllocator,
        { StringView_FromCString( "Construction" ), StringView_FromCString( MAP_WORKSPACE_DEFAULT_GAME ), {} } );
    if ( status == map_status_t::OK ) {
        preview.geometryPolicy = ws.pDocument->geometryPolicy;
        preview.geometry.policy = ws.pDocument->geometry.policy;
        u64 id{};
        // A sphere has the same rings in either output. Preview those rings
        // directly: reconstructing hundreds of convex planes on every mouse
        // move is unnecessary. The confirmed object still follows the user's
        // Brush/Mesh setting, and brush-side limits still reject its preview.
        const bool sphere = desc.kind == map_primitive_kind_t::SPHERE;
        status = sphere ? MapEdit_CreateSphereMesh( &preview, desc, material, {}, &id ) : MapEdit_CreatePrimitive( &preview, desc, material, {}, &id );
    }
    if ( status == map_status_t::OK && !MapWireframe_Init( &wire, ws.pDocument->pAllocator ) ) { status = map_status_t::OUT_OF_MEMORY; }
    if ( status == map_status_t::OK ) { status = MapWireframe_Build( &wire, preview ); }
    return status;
}
}
void MapWorkspace_SetEditPreview( map_workspace_t *ws, const map_bounds_t &bounds, u32 constructionPlaneAxis ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->pGui == nullptr ) { return; }
    // A staged primitive captures its construction defaults. Programmatic
    // bounds updates must not silently replace its kind, tessellation, or UVs.
    if ( MapWorkspace_HasBlockPreview( ws ) ) { ( void )MapWorkspace_SetBlockPreviewBounds( ws, bounds ); return; }
    map_status_t status = map_status_t::OK;
    auto desc = MapWorkspace_PrimitiveDefaults( ws, bounds );
    if ( desc.kind == map_primitive_kind_t::QUAD && constructionPlaneAxis < 3u ) { desc.axis = constructionPlaneAxis; }
    if ( constructionPlaneAxis > 3u ) { desc.kind = map_primitive_kind_t::COUNT; }
    FlattenQuad( desc );
    auto displayedBounds = bounds; displayedBounds.box = desc.bounds;
    const auto material = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
    const bool validMaterial = StringView_IsValid( material ) && material.cchLength <= MAP_MATERIAL_PATH_MAX &&
        ( material.cchLength == 0 || std::memchr( material.pData, '\0', material.cchLength ) == nullptr );
    if ( ws->tool == map_tool_t::BLOCK && bounds.bHas ) {
        const bool cached = ws->editPreview.bActive && ws->editPreview.status == map_status_t::OK &&
            ws->editPreviewWire.points.nCount != 0 && SamePrimitive( ws->editPreview.primitive, desc ) && SpherePreviewFitsBrushBudget( *ws, desc ) &&
            validMaterial && StringView_Equals( StringView_FromCString( ws->editPreview.blockMaterial ), material );
        if ( !cached ) {
            map_wireframe_t wire{};
            status = validMaterial ? BuildBlockPreview( *ws, desc, material, wire ) : map_status_t::INVALID_ARGUMENT;
            if ( status == map_status_t::OK ) { MoveWire( ws->editPreviewWire, wire ); }
            else { MapWireframe_Shutdown( &ws->editPreviewWire ); }
        }
    } else { MapWireframe_Shutdown( &ws->editPreviewWire ); }
    ws->editPreview = { displayedBounds.bHas, displayedBounds, desc, status };
    if ( ws->tool == map_tool_t::BLOCK ) {
        auto &preview = ws->editPreview;
        preview.pBlockDocument = ws->pDocument;
        preview.documentRevision = ws->pDocument->geometry.revision; preview.selectionRevision = ws->selection.revision;
        preview.tool = ws->tool; preview.mode = ws->elementMode;
        preview.visibility = MapWorkspace_PreviewVisibility( ws );
        preview.bBlockMaterialValid = validMaterial;
        if ( desc.kind == map_primitive_kind_t::QUAD ) { preview.blockDepthAxis = desc.axis; }
        if ( validMaterial && material.cchLength != 0 ) { Cy_MemCopy( preview.blockMaterial, material.pData, material.cchLength ); }
    }
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
}
bool MapWorkspace_HasBlockPreview( const map_workspace_t *ws ) noexcept
{
    // A failed rebuild keeps its last good bounds and cache available for
    // another resize. Status deliberately does not hide those retry controls.
    return BlockContextMatches( ws ) && ws->editPreview.bStagedBlock;
}
bool MapWorkspace_StageBlockPreview( map_workspace_t *ws, u32 depthAxis ) noexcept
{
    if ( depthAxis > 2u || !BlockContextMatches( ws ) ) { return false; }
    if ( !ws->editPreview.bStagedBlock ) {
        ws->editPreview.blockDepthAxis = ws->editPreview.primitive.kind == map_primitive_kind_t::QUAD ? ws->editPreview.primitive.axis : depthAxis;
    }
    ws->editPreview.bStagedBlock = CY_TRUE;
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
    return true;
}
bool MapWorkspace_SetBlockPreviewBounds( map_workspace_t *ws, const map_bounds_t &bounds ) noexcept
{
    if ( !MapWorkspace_HasBlockPreview( ws ) ) {
        if ( ws != nullptr && ws->editPreview.bStagedBlock ) { MapWorkspace_ClearEditPreview( ws ); }
        return false;
    }
    auto desc = ws->editPreview.primitive;
    desc.bounds = bounds.box;
    map_status_t status = map_status_t::INVALID_ARGUMENT;
    const bool planar = desc.kind != map_primitive_kind_t::QUAD || ( desc.axis < 3u &&
        PrimitiveCoordinate( bounds.box.minimum, desc.axis ) == PrimitiveCoordinate( bounds.box.maximum, desc.axis ) );
    if ( bounds.bHas && planar && ws->editPreview.bBlockMaterialValid ) {
        if ( ws->editPreviewWire.points.nCount != 0 && SamePrimitive( ws->editPreview.primitive, desc ) && SpherePreviewFitsBrushBudget( *ws, desc ) ) { status = map_status_t::OK; }
        else {
            map_wireframe_t wire{};
            status = BuildBlockPreview( *ws, desc, StringView_FromCString( ws->editPreview.blockMaterial ), wire );
            if ( status == map_status_t::OK ) { MoveWire( ws->editPreviewWire, wire ); }
        }
    }
    ws->editPreview.status = status;
    ws->editPreview.bBlockCommitFailed = CY_FALSE;
    if ( status == map_status_t::OK ) {
        ws->editPreview.bounds = bounds;
        ws->editPreview.primitive = desc;
    }
    // Bounds/descriptor/cache are published together only on success. Failure
    // is visible without replacing the last good geometry or appending history.
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
    return status == map_status_t::OK;
}
bool MapWorkspace_CommitBlockPreview( map_workspace_t *ws ) noexcept
{
    if ( !MapWorkspace_HasBlockPreview( ws ) ) {
        if ( ws != nullptr && ws->editPreview.bStagedBlock ) { MapWorkspace_ClearEditPreview( ws ); }
        return false;
    }
    if ( ws->editPreview.status != map_status_t::OK || !ws->editPreview.bBlockMaterialValid || ws->editPreviewWire.points.nCount == 0 ) { return false; }
    const auto desc = ws->editPreview.primitive;
    const auto bounds = ws->editPreview.bounds;
    const auto material = StringView_FromCString( ws->editPreview.blockMaterial );
    if ( CreatePrimitive( ws, bounds, desc, material ) ) { return true; } // Atomic publication clears the stage.
    // History/allocation/publication can fail after a valid preview was built.
    // Keep it valid so another Confirm retries the exact same construction.
    ws->editPreview.bBlockCommitFailed = CY_TRUE;
    CY_LOG_WRITE( Warning, Editor, "Block creation failed; the staged primitive is retained for retry or cancel" );
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
    return false;
}
math::affine3d_t MapWorkspace_TransformPreviewAffine( const map_transform_preview_t &transform ) noexcept
{
    if ( transform.kind == map_transform_preview_kind_t::TRANSLATE ) { return math::Affine3d_FromTranslation( transform.delta ); }
    math::affine3d_t linear = math::CY_AFFINE3D_IDENTITY;
    if ( transform.kind == map_transform_preview_kind_t::SCALE ) { linear = math::Affine3d_FromScale( transform.factors ); }
    else if ( transform.kind == map_transform_preview_kind_t::ROTATE ) {
        // Same XYZ Euler convention and bounded angles as MapEdit_Rotate.
        constexpr f64 radians = 3.14159265358979323846 / 180.0;
        const f64 x = std::remainder( transform.degrees.x, 360.0 ) * radians;
        const f64 y = std::remainder( transform.degrees.y, 360.0 ) * radians;
        const f64 z = std::remainder( transform.degrees.z, 360.0 ) * radians;
        const f64 cx = std::cos( x ), sx = std::sin( x ), cy = std::cos( y ), sy = std::sin( y ), cz = std::cos( z ), sz = std::sin( z );
        linear = math::Affine3d_FromColumns( { cz * cy, sz * cy, -sy },
            { cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx },
            { cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx }, {} );
    } else { return linear; }
    const auto pivot = transform.pivot;
    return math::Affine3d_Multiply( math::Affine3d_FromTranslation( pivot ),
        math::Affine3d_Multiply( linear, math::Affine3d_FromTranslation( { -pivot.x, -pivot.y, -pivot.z } ) ) );
}
math::affine3d_t MapWorkspace_TransformPreviewObjectAffine( const map_transform_preview_t &transform, const map_wire_object_t &object ) noexcept
{
    if ( !transform.bResizeIndividually ) { return MapWorkspace_TransformPreviewAffine( transform ); }
    math::vec3d_t factors{}, pivot{};
    if ( !object.bounds.bHas || MapEdit_ResizeTransform( object.bounds.box,
         { transform.resizeSides, transform.delta, transform.bResizeFromCenter }, &factors, &pivot ) != map_status_t::OK ) {
        return math::CY_AFFINE3D_IDENTITY;
    }
    auto individual = transform;
    individual.bResizeIndividually = CY_FALSE; individual.factors = factors; individual.pivot = pivot;
    return MapWorkspace_TransformPreviewAffine( individual );
}
math::vec3d_t MapWorkspace_TransformPreviewPoint( const map_transform_preview_t &transform, math::vec3d_t point ) noexcept
{
    return math::Affine3d_TransformPoint( MapWorkspace_TransformPreviewAffine( transform ), point );
}
bool_t MapWorkspace_IsTransformPreviewObject( const map_workspace_t *ws, const map_wire_object_t &object ) noexcept
{
    return ws != nullptr && MapWorkspace_IsVisible( ws, object ) &&
        ( MapWorkspace_IsSelected( ws, object.id ) || ( object.owner != 0 && MapWorkspace_IsSelected( ws, object.owner ) ) );
}
map_preview_visibility_t MapWorkspace_PreviewVisibility( const map_workspace_t *ws ) noexcept
{
    return ws != nullptr ? map_preview_visibility_t{ ws->hidden.revision, ws->hiddenVisgroups, ws->bCordonActive, ws->cordon } : map_preview_visibility_t{};
}
bool_t MapWorkspace_PreviewVisibilityMatches( const map_workspace_t *ws, const map_preview_visibility_t &visibility ) noexcept
{
    if ( ws == nullptr || visibility.hiddenRevision != ws->hidden.revision || visibility.hiddenVisgroups != ws->hiddenVisgroups ||
         visibility.bCordonActive != ws->bCordonActive ) { return CY_FALSE; }
    if ( !ws->bCordonActive ) { return CY_TRUE; }
    if ( visibility.cordon.bHas != ws->cordon.bHas ) { return CY_FALSE; }
    const auto &a = visibility.cordon.box, &b = ws->cordon.box;
    return !ws->cordon.bHas || ( a.minimum.x == b.minimum.x && a.minimum.y == b.minimum.y && a.minimum.z == b.minimum.z &&
        a.maximum.x == b.maximum.x && a.maximum.y == b.maximum.y && a.maximum.z == b.maximum.z );
}
void MapWorkspace_SetTransformPreview( map_workspace_t *ws, const map_transform_preview_t &transform ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return; }
    const bool translating = transform.kind == map_transform_preview_kind_t::TRANSLATE;
    const bool supported = translating ? MapWorkspace_CanMoveSelection( ws ) :
        transform.bResize ? MapWorkspace_CanEditSelection( ws ) : MapWorkspace_CanTurnSelection( ws );
    if ( transform.kind == map_transform_preview_kind_t::NONE || transform.kind > map_transform_preview_kind_t::ROTATE || !supported ||
         ( transform.bClone && !translating ) ||
         ( transform.bResize && transform.kind != map_transform_preview_kind_t::SCALE ) ||
         ( transform.bResizeFromCenter && !transform.bResize ) ||
         ( transform.bResizeIndividually && ( !transform.bResize || ws->elementMode == map_element_mode_t::VERTICES ||
             ws->elementMode == map_element_mode_t::EDGES || ws->elementMode == map_element_mode_t::FACES ) ) ||
         !Finite( transform.pivot ) || !Finite( transform.delta ) || !Finite( transform.factors ) || !Finite( transform.degrees ) ||
         transform.factors.x <= 0 || transform.factors.y <= 0 || transform.factors.z <= 0 ) {
        MapWorkspace_ClearEditPreview( ws ); return;
    }
    map_edit_preview_t preview{};
    preview.transform = transform;
    preview.tool = ws->tool; preview.mode = ws->elementMode;
    preview.visibility = MapWorkspace_PreviewVisibility( ws );
    preview.documentRevision = ws->pDocument->geometry.revision; preview.selectionRevision = ws->selection.revision;
    auto affine = MapWorkspace_TransformPreviewAffine( transform );
    if ( !math::Affine3d_IsFinite( affine ) ) { MapWorkspace_ClearEditPreview( ws ); return; }
    const f64 limit = ws->pDocument->geometryPolicy.numerical.fCoordinateMagnitudeLimit;
    bool valid = true;
    const auto add = [&]( math::vec3d_t point ) {
        point = math::Affine3d_TransformPoint( affine, point );
        if ( !Finite( point ) || std::abs( point.x ) > limit || std::abs( point.y ) > limit || std::abs( point.z ) > limit ) { valid = false; return; }
        MapBounds_AddPoint( preview.bounds, point );
    };
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        if ( !MapWorkspace_IsTransformPreviewObject( ws, object ) ) { continue; }
        if ( transform.bResizeIndividually ) {
            math::vec3d_t factors{}, pivot{};
            if ( !object.bounds.bHas || MapEdit_ResizeTransform( object.bounds.box,
                 { transform.resizeSides, transform.delta, transform.bResizeFromCenter }, &factors, &pivot ) != map_status_t::OK ) {
                MapWorkspace_ClearEditPreview( ws ); return;
            }
            affine = MapWorkspace_TransformPreviewObjectAffine( transform, object );
            if ( !math::Affine3d_IsFinite( affine ) ) { MapWorkspace_ClearEditPreview( ws ); return; }
        }
        // Translation preserves the authoritative extents, including point
        // entity helpers and owned height fields. Rotation needs actual points.
        if ( translating && object.bounds.bHas ) {
            add( object.bounds.box.minimum ); add( object.bounds.box.maximum );
        } else { for ( u32 point = 0; point < object.nPoints; ++point ) { add( ws->wire.points.pData[object.iFirstPoint + point] ); } }
    }
    // Reject overflowing presentation coordinates rather than painting a stale
    // previous result. Actual geometry/UV policy validation occurs at commit.
    if ( !valid || !preview.bounds.bHas ) {
        MapWorkspace_ClearEditPreview( ws ); return;
    }
    preview.bActive = CY_TRUE;
    MapWireframe_Shutdown( &ws->editPreviewWire );
    ws->editPreview = preview;
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
}
void MapWorkspace_ClearEditPreview( map_workspace_t *ws ) noexcept
{
    if ( ws != nullptr && ws->editPreview.bActive ) {
        ws->editPreview = {}; MapWireframe_Shutdown( &ws->editPreviewWire ); MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
    }
}
bool MapWorkspace_ApplyEdit( map_workspace_t *ws, const char *label, map_document_edit_fn pfnApply, void *pContext,
    const map_prepared_component_selection_t *preparedSelection ) noexcept
{
    if ( pfnApply == nullptr || label == nullptr ) { return false; }
    return Edit( ws, label, [&]( map_document_t *copy, vector_t<u64> &out ) { return pfnApply( pContext, copy, &out ); }, nullptr, preparedSelection );
}
bool MapWorkspace_ApplyMetadataEdit( map_workspace_t *ws, const char *label, map_metadata_edit_fn pfnApply, void *pContext ) noexcept
{
    if ( pfnApply == nullptr || label == nullptr ) { return false; }
    bool changed = false;
    return Edit( ws, label, [&]( map_document_t *copy, vector_t<u64> &out ) { return pfnApply( pContext, copy, &out, &changed ); }, &changed );
}
} // namespace cypher::editor::map
