//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Publishes validated map edits and owns their bounded undo snapshots.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Workspace.h"
#include "CypherMap_Edit.h"
#include "CypherMap_FaceEdit.h"
#include "CypherMap_BrushEdit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_Clipboard.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
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
    out.bounds = source.bounds;
    out.nBrokenBrushes = source.nBrokenBrushes;
    out.pAllocator = source.pAllocator;
}

void Publish( map_workspace_t *ws, map_prepared_edit_t &prepared ) noexcept
{
    // A replacement document invalidates the session's component context.
    // History restores authored geometry and roots without transient components.
    geometry::MeshSelection_Shutdown( &ws->meshSelection );
    ws->selectedMeshEdgeSeed = {};
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
    ++ws->selection.revision;
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
    prepared.live.faceObject = snapshot.faceObject;
    prepared.live.faceSide = snapshot.faceSide;
    // History restores geometry in the user's current selection mode. Keep the
    // stored component identity for later face-mode replay, without reviving a
    // component highlight after the user has returned to whole objects.
    if ( ws->elementMode == map_element_mode_t::FACES ) {
        prepared.live.meshFaceObject = snapshot.meshFaceObject;
        prepared.live.meshFaceId = snapshot.meshFaceId;
    }
    // All allocations and geometry reconstruction succeeded. Publication below
    // only transfers ownership, so failure never empties the live wireframe.
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

template <typename Apply>
bool Edit( map_workspace_t *ws, const char *label, Apply apply, const bool *metadataChanged = nullptr ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->pDocument->bReadOnly || EditorHistory_IsTransactionOpen( &ws->history ) ) { return false; }
    map_prepared_edit_t prepared;
    map_status_t status = MapEdit_Clone( ws->pDocument, &prepared.live.document );
    if ( status != map_status_t::OK ) { return false; }
    if ( !Vector_Init( &prepared.live.selection, ws->pGui->pAllocator ) ) { return false; }
    status = apply( prepared.live.document, prepared.live.selection );
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
         prepared.live.document->geometry.revision == ws->pDocument->geometry.revision ) ) { return false; }
    // Build before publishing: an edit must remain viewable, not just serializable.
    if ( status == map_status_t::OK ) {
        status = MapWireframe_Init( &prepared.wire, ws->pGui->pAllocator ) ? MapWireframe_Build( &prepared.wire, *prepared.live.document ) : map_status_t::OUT_OF_MEMORY;
    }
    if ( status != map_status_t::OK ) {
        Cy_LogWriteAt( log_level_t::Warning, log_channel_t::Editor, MapDocument_StatusName( status ), CY_SOURCE_LOCATION );
        return false;
    }
    std::unique_ptr<map_edit_state_t> owned( new ( std::nothrow ) map_edit_state_t{} );
    if ( owned == nullptr ) { return false; }
    if ( MapEdit_Clone( ws->pDocument, &owned->before.document ) != map_status_t::OK ||
         MapEdit_Clone( prepared.live.document, &owned->after.document ) != map_status_t::OK ||
         !CopySelection( owned->before.selection, Selected( *ws ), ws->pGui->pAllocator ) ||
         !CopySelection( owned->after.selection, { prepared.live.selection.pData, prepared.live.selection.nCount }, ws->pGui->pAllocator ) ) { return false; }
    owned->before.faceObject = ws->selectedBrushFaceObject;
    owned->before.faceSide = ws->selectedBrushFaceSide;
    prepared.live.faceObject = ws->selectedBrushFaceObject;
    prepared.live.faceSide = ws->selectedBrushFaceSide;
    // Removed/replaced objects cannot leave a stale authored face in the after state.
    const auto *brush = geometry::GeometryDocument_FindBrush( &prepared.live.document->geometry, { prepared.live.faceObject } );
    bool hasFace = brush != nullptr && prepared.live.selection.nCount == 1 && prepared.live.selection.pData[0] == prepared.live.faceObject;
    if ( hasFace ) {
        hasFace = false;
        for ( usize i = 0; i < brush->sides.nCount; ++i ) { hasFace |= brush->sides.pData[i].sourceId.value == prepared.live.faceSide; }
    }
    if ( !hasFace ) { prepared.live.faceObject = 0; prepared.live.faceSide = 0; }
    owned->after.faceObject = prepared.live.faceObject;
    owned->after.faceSide = prepared.live.faceSide;
    owned->before.meshFaceObject = ws->selectedMeshFaceObject;
    owned->before.meshFaceId = ws->selectedMeshFaceId;
    prepared.live.meshFaceObject = ws->selectedMeshFaceObject;
    prepared.live.meshFaceId = ws->selectedMeshFaceId;
    const auto *mesh = geometry::GeometryDocument_FindMesh( &prepared.live.document->geometry, { prepared.live.meshFaceObject } );
    geometry::geometry_mesh_face_handle_t faceHandle{};
    const bool hasMeshFace = mesh != nullptr && prepared.live.selection.nCount == 1 && prepared.live.selection.pData[0] == prepared.live.meshFaceObject &&
        geometry::MeshSource_TryFindFace( mesh, { prepared.live.meshFaceId }, &faceHandle );
    if ( !hasMeshFace ) { prepared.live.meshFaceObject = 0; prepared.live.meshFaceId = 0; }
    owned->after.meshFaceObject = prepared.live.meshFaceObject;
    owned->after.meshFaceId = prepared.live.meshFaceId;
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
    op.cbOwnedBytes = charge;
    // Register without notifying first: observers must never see a committed
    // history entry whose document has not yet been published. Failed Push
    // leaves both the live map and snapshot ownership with this scope.
    if ( EditorHistory_Push( &ws->history, op, false ) != editor_history_status_t::OK ) { return false; }
    ( void )owned.release();
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
         MapEntityEdit_ResolveOwners( ws->pDocument, Selected( *ws ), &owners ) != map_status_t::OK ) { return false; }
    bool changed = false;
    return Edit( ws, label, [&]( map_document_t *copy, vector_t<u64> &out ) {
        // Preserve the selected geometry and its component identity even when
        // the keys belong to its owner; ownership resolution is only a target.
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return apply( copy, span_t<const u64>{ owners.pData, owners.nCount }, &changed );
    }, &changed );
}
}

bool MapWorkspace_CanEditEntityProperties( const map_workspace_t *ws ) noexcept
{
    if ( !EntityEditReady( ws ) ) { return false; }
    vector_t<u64> owners{};
    return Vector_Init( &owners, ws->pDocument->pAllocator ) &&
        MapEntityEdit_ResolveOwners( ws->pDocument, Selected( *ws ), &owners ) == map_status_t::OK;
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
bool MapWorkspace_CanMoveSelection( const map_workspace_t *ws ) noexcept {
    if ( !RootEditModeAvailable( ws ) || ws->pDocument == nullptr || ws->pDocument->bReadOnly || ws->selection.ids.nCount == 0 ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ||
             ( ws->elementMode == map_element_mode_t::MESHES && !MapWorkspace_IsSelectableObject( ws, *object ) ) ) { return false; }
        if ( object->kind == map_wire_kind_t::ENTITY ) {
            const auto *entity = MapWireframe_FindEntity( ws->wire, object->id );
            if ( entity == nullptr || ( !entity->bHasOrigin && entity->nOwned == 0 ) ) { return false; }
        } else if ( object->kind != map_wire_kind_t::BRUSH && object->kind != map_wire_kind_t::MESH && object->kind != map_wire_kind_t::PATCH ) { return false; }
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
                       desc.kind == map_primitive_kind_t::QUAD ? "Create Quad" : "Create Sphere";
    return Edit( ws, name, [&]( map_document_t *copy, vector_t<u64> &out ) {
        u64 id = 0u;
        const auto material = capturedMaterial.pData != nullptr ? capturedMaterial :
            EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
        const auto status = MapEdit_CreatePrimitive( copy, desc, material, {}, &id );
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
    if ( !MapWorkspace_CanEditSelection( ws ) || !Finite( factors ) || !Finite( pivot ) || factors.x <= 0 || factors.y <= 0 || factors.z <= 0 ||
         ( factors.x == 1 && factors.y == 1 && factors.z == 1 ) ) { return false; }
    return Edit( ws, "Scale", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapEdit_Scale( copy, Selected( *ws ), factors, pivot,
            EditorSettings_Bool( &ws->pGui->settings, "editor.map.texture_scale_lock", CY_FALSE ) );
    } );
}
bool MapWorkspace_RotateSelection( map_workspace_t *ws, math::vec3d_t degrees, math::vec3d_t pivot ) noexcept
{
    if ( !MapWorkspace_CanEditSelection( ws ) || !Finite( degrees ) || !Finite( pivot ) || ( degrees.x == 0 && degrees.y == 0 && degrees.z == 0 ) ) { return false; }
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
    return MapWorkspace_CanCopySelection( ws ) && !ws->pDocument->bReadOnly;
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
    if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ) { return false; }
    const auto *brush = geometry::GeometryDocument_FindBrush( &ws->pDocument->geometry, { ws->selectedBrushFaceObject } );
    if ( brush == nullptr ) { return false; }
    for ( usize i = 0; i < brush->sides.nCount; ++i ) { if ( brush->sides.pData[i].sourceId.value == ws->selectedBrushFaceSide ) { return true; } }
    return false;
}
void MapWorkspace_ClearBrushFace( map_workspace_t *ws ) noexcept
{
    if ( ws->selectedBrushFaceObject == 0 && ws->selectedBrushFaceSide == 0 ) { return; }
    ws->selectedBrushFaceObject = 0; ws->selectedBrushFaceSide = 0;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
}
void MapWorkspace_SelectBrushFace( map_workspace_t *ws, u64 object, u64 side ) noexcept
{
    const auto *brush = geometry::GeometryDocument_FindBrush( &ws->pDocument->geometry, { object } );
    bool exists = false;
    if ( brush != nullptr ) { for ( usize i = 0; i < brush->sides.nCount; ++i ) { exists |= brush->sides.pData[i].sourceId.value == side; } }
    const auto *wireObject = MapWireframe_FindObject( ws->wire, object );
    if ( !exists || wireObject == nullptr || !MapWorkspace_IsVisible( ws, *wireObject ) ) { return; }
    MapWorkspace_Select( ws, object, MAP_SELECT_REPLACE );
    ws->selectedBrushFaceObject = object; ws->selectedBrushFaceSide = side;
    MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES );
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
}
bool MapWorkspace_PushPullFace( map_workspace_t *ws, f64 distance ) noexcept
{
    if ( !MapWorkspace_HasBrushFace( ws ) || !std::isfinite( distance ) || distance == 0 ) { return false; }
    return Edit( ws, "Push/Pull Face", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapFaceEdit_PushPull( copy, ws->selectedBrushFaceObject, ws->selectedBrushFaceSide, distance );
    } );
}
bool MapWorkspace_HasMeshFace( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->selectedMeshFaceObject == 0 || ws->selectedMeshFaceId == 0 ||
         ws->selection.ids.nCount != 1 || ws->selection.ids.pData[0] != ws->selectedMeshFaceObject ) { return false; }
    const auto *object = MapWireframe_FindObject( ws->wire, ws->selectedMeshFaceObject );
    if ( object == nullptr || !MapWorkspace_IsVisible( ws, *object ) ) { return false; }
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { ws->selectedMeshFaceObject } );
    geometry::geometry_mesh_face_handle_t face{};
    return mesh != nullptr && geometry::MeshSource_TryFindFace( mesh, { ws->selectedMeshFaceId }, &face );
}
void MapWorkspace_ClearMeshFace( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ( ws->selectedMeshFaceObject == 0 && ws->selectedMeshFaceId == 0 ) ) { return; }
    ws->selectedMeshFaceObject = 0; ws->selectedMeshFaceId = 0;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION );
}
void MapWorkspace_SelectMeshFace( map_workspace_t *ws, u64 object, u64 id ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || object == 0 || id == 0 ) { return; }
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { object } );
    const auto *wire = MapWireframe_FindObject( ws->wire, object );
    geometry::geometry_mesh_face_handle_t face{};
    if ( mesh == nullptr || wire == nullptr || !MapWorkspace_IsVisible( ws, *wire ) ||
         !geometry::MeshSource_TryFindFace( mesh, { id }, &face ) ) { return; }
    // Prepare the root selection before replacing the face, so OOM cannot
    // retain face IDs attached to a different (or empty) root selection.
    if ( !EditorSelection_Set( &ws->selection, &object, 1 ) &&
         ( ws->selection.ids.nCount != 1 || ws->selection.ids.pData[0] != object ) ) { return; }
    ws->selectedBrushFaceObject = 0; ws->selectedBrushFaceSide = 0;
    ws->selectedMeshFaceObject = object; ws->selectedMeshFaceId = id;
    geometry::MeshSelection_Shutdown( &ws->meshSelection ); ws->selectedMeshEdgeSeed = {};
    ws->elementMode = map_element_mode_t::FACES;
    MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW );
}

namespace
{
bool SameEdge( geometry::mesh_edge_ref_t a, geometry::mesh_edge_ref_t b ) noexcept
{
    return a.a.value == b.a.value && a.b.value == b.b.value;
}

const geometry::mesh_source_t *SelectedComponentMesh( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->meshSelection.meshId.value == 0 ||
         ws->selection.ids.nCount != 1 || ws->selection.ids.pData[0] != ws->meshSelection.meshId.value ) { return nullptr; }
    const auto *wire = MapWireframe_FindObject( ws->wire, ws->meshSelection.meshId.value );
    return wire != nullptr && wire->kind == map_wire_kind_t::MESH && MapWorkspace_IsVisible( ws, *wire ) ?
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
         ws->elementMode != map_element_mode_t::VERTICES || ws->tool != map_tool_t::SELECT || ws->editPreview.bActive ||
         EditorHistory_IsTransactionOpen( &ws->history ) ||
         ( mode != MAP_SELECT_REPLACE && mode != MAP_SELECT_ADD && mode != MAP_SELECT_TOGGLE && mode != MAP_SELECT_REMOVE ) ) { return false; }
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { object } );
    const auto *wire = MapWireframe_FindObject( ws->wire, object );
    geometry::geometry_mesh_vertex_handle_t handle{};
    if ( mesh == nullptr || wire == nullptr || wire->kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( ws, *wire ) ||
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
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || object == 0 ||
         ws->elementMode != map_element_mode_t::EDGES || ws->tool != map_tool_t::SELECT || ws->editPreview.bActive ||
         EditorHistory_IsTransactionOpen( &ws->history ) ||
         ( mode != MAP_SELECT_REPLACE && mode != MAP_SELECT_ADD && mode != MAP_SELECT_TOGGLE && mode != MAP_SELECT_REMOVE ) ) { return false; }
    edge = geometry::MeshEdgeRef_Make( edge.a, edge.b );
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { object } );
    const auto *wire = MapWireframe_FindObject( ws->wire, object );
    geometry::geometry_mesh_edge_handle_t handle{};
    if ( mesh == nullptr || wire == nullptr || wire->kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( ws, *wire ) ||
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
    return Edit( ws, "Extrude Mesh Face", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapMeshEdit_ExtrudeFace( copy, ws->selectedMeshFaceObject, ws->selectedMeshFaceId, distance );
    } );
}
bool MapWorkspace_InsetMeshFace( map_workspace_t *ws, f64 margin ) noexcept
{
    if ( !MapWorkspace_CanEditMeshFace( ws ) || !std::isfinite( margin ) || margin <= 0 ) { return false; }
    return Edit( ws, "Inset Mesh Face", [&]( map_document_t *copy, vector_t<u64> &out ) {
        if ( !KeepSelection( *ws, out ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapMeshEdit_InsetFace( copy, ws->selectedMeshFaceObject, ws->selectedMeshFaceId, margin );
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
bool ClipSelection( map_workspace_t *ws, math::planed_t plane, map_brush_clip_mode_t mode, string_view_t material ) noexcept
{
    if ( !MapWorkspace_CanEditBrushSelection( ws ) ) { return false; }
    return Edit( ws, mode == map_brush_clip_mode_t::BOTH ? "Slice Brushes" : "Clip Brushes", [&]( map_document_t *copy, vector_t<u64> &out ) {
        return MapBrushEdit_ClipMode( copy, Selected( *ws ), plane, mode, material, &out );
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
    if ( MapWorkspace_CanEditBrushSelection( ws ) && old.bActive && old.bClip && old.status == map_status_t::OK && old.documentRevision == revision &&
         old.selectionRevision == ws->selection.revision && old.clipMode == mode &&
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
    preview.clipGuide = guide;
    preview.documentRevision = revision; preview.selectionRevision = ws->selection.revision;
    preview.status = map_status_t::INVALID_ARGUMENT;
    if ( material.cchLength <= MAP_MATERIAL_PATH_MAX && StringView_IsValid( material ) ) {
        if ( material.cchLength != 0 ) { std::memcpy( preview.clipMaterial, material.pData, material.cchLength ); }
        if ( MapWorkspace_CanEditBrushSelection( ws ) && math::Planed_IsNormalized( plane, 1e-8 ) ) {
            // Presentation copies only the selected authored solids. It does
            // not clone unrelated map records, entities or chunk inventories.
            map_document_t copy{};
            vector_t<u64> roots{};
            map_wireframe_t wire{};
            preview.status = MapDocument_Create( &copy, ws->pDocument->pAllocator,
                { StringView_FromCString( "Clip preview" ), StringView_FromCString( MAP_WORKSPACE_DEFAULT_GAME ), {} } );
            if ( preview.status == map_status_t::OK ) {
                copy.geometryPolicy = ws->pDocument->geometryPolicy;
                copy.geometry.policy = ws->pDocument->geometry.policy;
                copy.nextId = ws->pDocument->nextId;
                for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
                    geometry::brush_source_t source{};
                    auto status = geometry::GeometryDocument_TryCopyBrushSource( &ws->pDocument->geometry, { ws->selection.ids.pData[i] }, copy.pAllocator, &source );
                    if ( status == geometry::geometry_status_t::OK ) { status = geometry::GeometryDocument_TryAddBrushSource( &copy.geometry, &source ); }
                    geometry::BrushSource_Shutdown( &source );
                    if ( status != geometry::geometry_status_t::OK ) {
                        preview.status = status == geometry::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED;
                        break;
                    }
                }
            }
            if ( preview.status == map_status_t::OK && !Vector_Init( &roots, copy.pAllocator ) ) { preview.status = map_status_t::OUT_OF_MEMORY; }
            if ( preview.status == map_status_t::OK ) { preview.status = MapBrushEdit_ClipMode( &copy, Selected( *ws ), plane, mode, material, &roots ); }
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
        a.coneTopRadiusRatio == b.coneTopRadiusRatio && a.worldUnitsPerUv == b.worldUnitsPerUv;
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
map_status_t BuildBlockPreview( map_workspace_t &ws, const map_primitive_desc_t &desc, string_view_t material, map_wireframe_t &wire ) noexcept
{
    // Only an empty private construction document is built. The live map and
    // its undo history are never touched while drawing or refining bounds.
    map_document_t preview{};
    auto status = MapDocument_Create( &preview, ws.pDocument->pAllocator,
        { StringView_FromCString( "Construction" ), StringView_FromCString( MAP_WORKSPACE_DEFAULT_GAME ), {} } );
    if ( status == map_status_t::OK ) {
        preview.geometryPolicy = ws.pDocument->geometryPolicy;
        preview.geometry.policy = ws.pDocument->geometry.policy;
        u64 id{};
        status = MapEdit_CreatePrimitive( &preview, desc, material, {}, &id );
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
            ws->editPreviewWire.points.nCount != 0 && SamePrimitive( ws->editPreview.primitive, desc ) &&
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
        if ( ws->editPreviewWire.points.nCount != 0 && SamePrimitive( ws->editPreview.primitive, desc ) ) { status = map_status_t::OK; }
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
    const bool supported = translating ? MapWorkspace_CanMoveSelection( ws ) : MapWorkspace_CanEditSelection( ws );
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
} // namespace cypher::editor::map
