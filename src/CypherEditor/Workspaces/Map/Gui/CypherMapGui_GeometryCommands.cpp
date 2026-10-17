//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_GeometryCommands.cpp
//  Purpose: Implements the geometry operation commands over the map edit
//           adapters (CypherMap_TopologyEdit, CypherMap_BrushEdit,
//           CypherMap_Edit).
//  Details: Component commands act on the mesh vertices or edges the session
//           has selected and then reselect whichever of them survived, so a
//           merge leaves the merged vertex selected for the next step.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_GeometryCommands.h"
#include "CypherMapGui_EntityIo.h"
#include "CypherMapGui_EntityLook.h"
#include "CypherMapGui_ObjectTools.h"
#include "CypherMapGui_Layers.h"
#include "CypherMapGui_Instances.h"
#include "CypherMapGui_Hierarchy.h"
#include "CypherMapGui_Notice.h"
#include "CypherMapGui_FastTexture.h"
#include "CypherMapGui_FindEntities.h"
#include "CypherMapGui_Game.h"
#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_ToolGestures.h"

#include "CypherMap_BrushEdit.h"
#include "CypherMap_Edit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_TextureEdit.h"
#include "CypherMap_PatchEdit.h"
#include "CypherMap_Prefab.h"
#include "CypherMap_TopologyEdit.h"
#include "CypherMap_Modifiers.h"
#include "CypherMap_Groups.h"
#include "CypherMap_SelectionSets.h"
#include "CypherMap_Export.h"
#include "CypherMap_ImportObj.h"
#include "CypherMap_Compile.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_BrushCSG.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshSelectionQueries.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_MeshSourceTopology.h"

#include <QApplication>
#include <QInputDialog>
#include <QSaveFile>
#include <QDir>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>

#include <algorithm>
#include <deque>
#include <memory>
#include <unordered_map>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{
using id_t = geo::geometry_source_id_t;
using edge_t = geo::mesh_edge_ref_t;

map_workspace_t *Ws( void *pContext ) noexcept { return static_cast<map_workspace_t *>( pContext ); }
u32 State( bool enabled ) noexcept { return enabled ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE; }
command_result_t Result( bool done ) noexcept { return done ? command_result_t::OK : command_result_t::FAILED; }

f64 Real( const map_workspace_t *ws, const char *path, f64 fallback ) noexcept { return EditorSettings_Real( &ws->pGui->settings, path, fallback ); }

// Numeric console arguments; false when one does not parse.
bool Numbers( const command_args_t &args, f64 *pOut, usize nMax, usize &nOut )
{
    nOut = 0;
    if ( args.nArgs > nMax ) { return false; }
    for ( usize i = 0; i < args.nArgs; ++i ) {
        const std::string text( args.pArgs[i].pData, args.pArgs[i].cchLength );
        char *end = nullptr;
        const f64 value = std::strtod( text.c_str(), &end );
        if ( text.empty() || end != text.c_str() + text.size() || !std::isfinite( value ) ) { return false; }
        pOut[nOut++] = value;
    }
    return true;
}

// The live document can take an edit: what every operation needs first.
bool Ready( const map_workspace_t *ws ) noexcept
{
    return ws != nullptr && ws->pGui != nullptr && ws->pDocument != nullptr && !ws->pDocument->bReadOnly &&
        !ws->editPreview.bActive && !EditorHistory_IsTransactionOpen( &ws->history );
}

// Whole-object operations run in the modes that select whole objects.
bool ObjectMode( const map_workspace_t *ws ) noexcept
{
    return ws->elementMode == map_element_mode_t::OBJECTS || ws->elementMode == map_element_mode_t::GROUPS ||
        ws->elementMode == map_element_mode_t::MESHES;
}

/*
================
Component selection
================
*/

// The selected components of one mesh, copied: an edit replaces the
// document and clears the live component selection it came from.
struct components_t {
    u64 mesh{ 0 };
    std::vector<id_t> vertices{};
    std::vector<edge_t> edges{};
};

// Component edits run from the Select tool and the transform tools (their
// buttons should not grey out because Translate is active); Navigation and
// the camera keep the components visible for inspection, never editable.
bool ComponentReady( const map_workspace_t *ws ) noexcept
{
    return Ready( ws ) && ( ws->tool == map_tool_t::SELECT || ws->tool == map_tool_t::TRANSLATE || ws->tool == map_tool_t::ROTATE ||
                            ws->tool == map_tool_t::SCALE );
}

bool SelectedVertices( const map_workspace_t *ws, components_t &out ) noexcept
{
    if ( !ComponentReady( ws ) || !MapWorkspace_HasMeshVertices( ws ) ) { return false; }
    out.mesh = ws->meshSelection.meshId.value;
    out.vertices.assign( ws->meshSelection.vertices.pData, ws->meshSelection.vertices.pData + ws->meshSelection.vertices.nCount );
    return true;
}

bool SelectedEdges( const map_workspace_t *ws, components_t &out ) noexcept
{
    if ( !ComponentReady( ws ) || !MapWorkspace_HasMeshEdges( ws ) ) { return false; }
    out.mesh = ws->meshSelection.meshId.value;
    out.edges.assign( ws->meshSelection.edges.pData, ws->meshSelection.edges.pData + ws->meshSelection.edges.nCount );
    return true;
}

const geo::mesh_source_t *Mesh( const map_workspace_t *ws, u64 id ) noexcept
{
    return geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, { id } );
}

// The selected mesh faces: the active face and any picked with Shift.
bool SelectedFaces( const map_workspace_t *ws, u64 &mesh, std::vector<u64> &faces )
{
    faces.clear(); mesh = 0;
    if ( !ComponentReady( ws ) || !MapWorkspace_CanEditMeshFace( ws ) ) { return false; }
    const auto set = MapWorkspace_MeshFaces( ws );
    mesh = ws->selectedMeshFaceObject;
    faces.assign( set.pData, set.pData + set.nCount );
    return !faces.empty();
}

u32 BoundaryCount( const map_workspace_t *ws, const components_t &c ) noexcept
{
    const auto *mesh = Mesh( ws, c.mesh );
    u32 count = 0;
    for ( const edge_t &edge : c.edges ) {
        geo::geometry_mesh_edge_handle_t handle{};
        if ( mesh != nullptr && geo::MeshSourceEdit_TryFindEdge( mesh, edge.a, edge.b, &handle ) &&
             geo::MeshBoundary_IsBoundaryEdge( &mesh->mesh, handle ) ) { ++count; }
    }
    return count;
}

// Distinct endpoint IDs of the edges, for vertex operations run on edges.
std::vector<id_t> Endpoints( const std::vector<edge_t> &edges )
{
    std::vector<u64> ids;
    for ( const edge_t &edge : edges ) { ids.push_back( edge.a.value ); ids.push_back( edge.b.value ); }
    std::sort( ids.begin(), ids.end() ); ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
    std::vector<id_t> out;
    for ( u64 id : ids ) { out.push_back( { id } ); }
    return out;
}

// Orthographic handles may represent several vertices. Each eligible face
// gets its own cut, rather than treating four IDs as an invalid two-point pick.
bool CanConnectVertices( const map_workspace_t *ws, const components_t &c ) noexcept
{
    if ( c.vertices.size() < 2 ) { return false; }
    const auto *mesh = Mesh( ws, c.mesh );
    if ( mesh == nullptr ) { return false; }
    bool cut = false;
    for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
        const auto &face = ws->wire.faces.pData[f];
        if ( face.id != c.mesh ) { continue; }
        id_t pair[2]{};
        usize count = 0;
        for ( u32 k = 0; k < face.nIndices; ++k ) {
            const u64 vertex = ws->wire.pointSourceIds.pData[ws->wire.faceIndices.pData[face.iFirstIndex + k]];
            if ( std::any_of( c.vertices.begin(), c.vertices.end(), [&]( id_t id ) { return id.value == vertex; } ) ) {
                if ( count == 2 ) { return false; }
                pair[count++] = { vertex };
            }
        }
        geo::geometry_mesh_edge_handle_t edge{};
        if ( count == 2 && !geo::MeshSourceEdit_TryFindEdge( mesh, pair[0], pair[1], &edge ) ) { cut = true; }
    }
    return cut;
}

int VertexColumnAxis( const map_workspace_t *ws ) noexcept
{
    return ws->meshVertexColumnRevision == ws->selection.revision ? ws->meshVertexColumnAxis : -1;
}

bool CanMergeVertices( const map_workspace_t *ws, const components_t &c ) noexcept
{
    if ( c.vertices.size() < 2 ) { return false; }
    const int axis = VertexColumnAxis( ws );
    if ( axis < 0 ) { return true; }
    const auto *mesh = Mesh( ws, c.mesh );
    if ( mesh == nullptr || axis > 2 ) { return false; }
    const auto depth = [&]( id_t id, f64 &out ) {
        geo::geometry_mesh_vertex_handle_t handle{};
        if ( !geo::MeshSource_TryFindVertex( mesh, id, &handle ) ) { return false; }
        const auto *vertex = GenerationPool_Get( &mesh->mesh.vertices, handle );
        if ( vertex == nullptr ) { return false; }
        const auto p = vertex->position;
        out = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
        return true;
    };
    bool pair = false;
    for ( usize i = 0; i < c.vertices.size(); ++i ) {
        f64 a = 0;
        if ( !depth( c.vertices[i], a ) ) { return false; }
        for ( usize j = i + 1; j < c.vertices.size(); ++j ) {
            f64 b = 0;
            if ( !depth( c.vertices[j], b ) ) { return false; }
            pair |= std::abs( a - b ) <= 1e-6;
        }
    }
    return pair;
}

// Reselects the listed components that still exist after an edit, in one
// publication.
void RestoreVertices( map_workspace_t *ws, u64 mesh, const std::vector<id_t> &ids ) noexcept
{
    const auto *source = Mesh( ws, mesh );
    std::vector<id_t> kept;
    for ( const id_t &id : ids ) {
        geo::geometry_mesh_vertex_handle_t handle{};
        if ( source != nullptr && geo::MeshSource_TryFindVertex( source, id, &handle ) ) { kept.push_back( id ); }
    }
    if ( !kept.empty() ) { ( void )MapWorkspace_SetMeshComponents( ws, mesh, { kept.data(), kept.size() }, {} ); }
}

void RestoreEdges( map_workspace_t *ws, u64 mesh, const std::vector<edge_t> &edges ) noexcept
{
    const auto *source = Mesh( ws, mesh );
    std::vector<edge_t> kept;
    for ( const edge_t &edge : edges ) {
        geo::geometry_mesh_edge_handle_t handle{};
        if ( source != nullptr && geo::MeshSourceEdit_TryFindEdge( source, edge.a, edge.b, &handle ) ) { kept.push_back( edge ); }
    }
    if ( !kept.empty() ) { ( void )MapWorkspace_SetMeshComponents( ws, mesh, {}, { kept.data(), kept.size() } ); }
}

/*
================
Publication
================
*/

bool Keep( const map_workspace_t &ws, vector_t<u64> *out ) noexcept
{
    if ( !Vector_Reserve( out, ws.selection.ids.nCount ) ) { return false; }
    for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) { if ( !Vector_PushBack( out, ws.selection.ids.pData[i] ) ) { return false; } }
    return true;
}

// One undo step around an edit of the private copy; the selection roots
// stay as they are unless the edit chooses others.
template <typename Edit>
bool Apply( map_workspace_t *ws, const char *label, Edit edit, const map_prepared_component_selection_t *preparedSelection = nullptr ) noexcept
{
    return MapWorkspace_ApplyEdit( ws, label, []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
        return ( *static_cast<Edit *>( context ) )( copy, out );
    }, &edit, preparedSelection );
}

template <typename Edit>
bool ApplyKeeping( map_workspace_t *ws, const char *label, Edit edit, const map_prepared_component_selection_t *preparedSelection = nullptr ) noexcept
{
    return Apply( ws, label, [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        return Keep( *ws, out ) ? edit( copy ) : map_status_t::OUT_OF_MEMORY;
    }, preparedSelection );
}

// Prepare the surviving component set in the private edit, not after the
// document is published. Failure must preserve both geometry and selection.
template <typename Edit>
bool ApplyVertexEdit( map_workspace_t *ws, const char *label, const components_t &c, Edit edit ) noexcept
{
    geo::mesh_selection_t next{};
    if ( geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { c.mesh } ) != geo::geometry_status_t::OK ) { return false; }
    const int depthAxis = VertexColumnAxis( ws );
    const map_prepared_component_selection_t preparedSelection{ &next, {}, depthAxis };
    const bool done = ApplyKeeping( ws, label, [&]( map_document_t *copy ) noexcept {
        const auto status = edit( copy );
        if ( status != map_status_t::OK ) { return status; }
        const auto *mesh = geo::GeometryDocument_FindMesh( &copy->geometry, { c.mesh } );
        for ( const id_t id : c.vertices ) {
            geo::geometry_mesh_vertex_handle_t handle{};
            if ( geo::MeshSource_TryFindVertex( mesh, id, &handle ) &&
                 geo::MeshSelection_TryAddVertex( &next, id ) != geo::geometry_status_t::OK ) { return map_status_t::OUT_OF_MEMORY; }
        }
        return map_status_t::OK;
    }, &preparedSelection );
    return done;
}

span_t<const u64> Selected( const map_workspace_t *ws ) noexcept { return { ws->selection.ids.pData, ws->selection.ids.nCount }; }
map_vertex_ids_t Span( const std::vector<id_t> &ids ) noexcept { return { ids.data(), ids.size() }; }
map_edge_refs_t Span( const std::vector<edge_t> &edges ) noexcept { return { edges.data(), edges.size() }; }

/*
================
View frame
================
*/

// Horizontal (u) and vertical (v) world axes of the last hovered 2D view,
// Top when none was hovered yet (the paste offset's convention).
void ViewAxes( const map_workspace_t *ws, u32 &u, u32 &v ) noexcept
{
    switch ( ws->cursorAxes & 7u ) {
        case 6u: u = 1; v = 2; break; // Front: Y right, Z up.
        case 5u: u = 0; v = 2; break; // Side: X right, Z up.
        default: u = 0; v = 1; break; // Top: X right, Y up.
    }
}

f64 Axis( math::vec3d_t p, u32 axis ) noexcept { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
math::vec3d_t AxisVector( u32 axis, f64 value ) noexcept
{
    return { axis == 0 ? value : 0.0, axis == 1 ? value : 0.0, axis == 2 ? value : 0.0 };
}

map_bounds_t SelectionBounds( const map_workspace_t *ws ) noexcept
{
    map_bounds_t bounds{};
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        if ( const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] ) ) { MapBounds_AddBounds( bounds, object->bounds ); }
    }
    return bounds;
}

bool Overlaps( const map_bounds_t &a, const map_bounds_t &b ) noexcept
{
    constexpr f64 kTouch = 1e-6; // Faces in contact count as touching.
    return a.bHas && b.bHas &&
        a.box.minimum.x <= b.box.maximum.x + kTouch && b.box.minimum.x <= a.box.maximum.x + kTouch &&
        a.box.minimum.y <= b.box.maximum.y + kTouch && b.box.minimum.y <= a.box.maximum.y + kTouch &&
        a.box.minimum.z <= b.box.maximum.z + kTouch && b.box.minimum.z <= a.box.maximum.z + kTouch;
}

// Two or more whole meshes with one owner: what Merge joins in object modes.
bool CanJoinMeshes( const map_workspace_t *ws ) noexcept
{
    if ( !Ready( ws ) || !ObjectMode( ws ) || ws->selection.ids.nCount < 2 || !MapWorkspace_CanEditMeshSelection( ws ) ) { return false; }
    const auto *first = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[0] );
    for ( usize i = 1; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object == nullptr || first == nullptr || object->owner != first->owner ) { return false; }
    }
    return true;
}

// Selected meshes made of more than one disconnected shell.
std::vector<u64> SeparableMeshes( const map_workspace_t *ws )
{
    std::vector<u64> out;
    if ( !Ready( ws ) || !ObjectMode( ws ) || !MapWorkspace_CanEditMeshSelection( ws ) ) { return out; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *mesh = Mesh( ws, ws->selection.ids.pData[i] );
        if ( mesh != nullptr && GenerationPool_Count( &mesh->mesh.shells ) > 1 ) { out.push_back( ws->selection.ids.pData[i] ); }
    }
    return out;
}

/*
================
Mesh component commands
================
*/

u32 MergeState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) ) { return State( CanMergeVertices( ws, c ) ); }
    if ( SelectedEdges( ws, c ) ) { return State( c.edges.size() == 2 && BoundaryCount( ws, c ) == 2 ); }
    return State( CanJoinMeshes( ws ) );
}
command_result_t Merge( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( CanJoinMeshes( ws ) ) {
        return Result( Apply( ws, "Merge Meshes", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
            u64 retained = 0;
            const auto status = MapTopologyEdit_JoinMeshes( copy, Selected( ws ), &retained );
            return status == map_status_t::OK && !Vector_PushBack( out, retained ) ? map_status_t::OUT_OF_MEMORY : status;
        } ) );
    }
    if ( SelectedVertices( ws, c ) && CanMergeVertices( ws, c ) ) {
        const int depthAxis = VertexColumnAxis( ws );
        const bool done = ApplyVertexEdit( ws, "Merge Vertices", c, [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_MergeVertices( copy, c.mesh, Span( c.vertices ), depthAxis );
        } );
        return Result( done );
    }
    if ( SelectedEdges( ws, c ) && c.edges.size() == 2 ) {
        return Result( ApplyKeeping( ws, "Merge Edges", [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_SewEdges( copy, c.mesh, c.edges[0], c.edges[1] );
        } ) );
    }
    return command_result_t::DISABLED;
}

u32 CollapseState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) ) { return State( c.vertices.size() >= 2 ); }
    if ( SelectedEdges( ws, c ) ) { return COMMAND_STATE_ENABLED; }
    return State( MapWorkspace_CanEditMeshFace( ws ) );
}
command_result_t Collapse( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) && c.vertices.size() >= 2 ) {
        const bool done = ApplyVertexEdit( ws, "Collapse Vertices", c, [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_MergeVertices( copy, c.mesh, Span( c.vertices ) );
        } );
        return Result( done );
    }
    if ( SelectedEdges( ws, c ) ) {
        return Result( ApplyKeeping( ws, "Collapse Edges", [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_CollapseEdges( copy, c.mesh, Span( c.edges ) );
        } ) );
    }
    if ( MapWorkspace_CanEditMeshFace( ws ) ) {
        const u64 mesh = ws->selectedMeshFaceObject, face = ws->selectedMeshFaceId;
        return Result( ApplyKeeping( ws, "Collapse Face", [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_CollapseFace( copy, mesh, face );
        } ) );
    }
    return command_result_t::DISABLED;
}

// True when no selected vertex lies on the mesh's open border: vertex
// Dissolve and Bevel replace the faces around a vertex, which needs a ring.
bool InteriorVertices( const map_workspace_t *ws, const components_t &c ) noexcept
{
    const auto *mesh = Mesh( ws, c.mesh );
    if ( mesh == nullptr ) { return false; }
    bool interior = true;
    // A vertex is on the border when a border edge touches it.
    ( void )GenerationPool_ForEach( &mesh->mesh.edges, [&]( geo::geometry_mesh_edge_handle_t h, const geo::mesh_edge_record_t & ) noexcept -> bool_t {
        if ( !geo::MeshBoundary_IsBoundaryEdge( &mesh->mesh, h ) ) { return true; }
        const auto *edge = GenerationPool_Get( &mesh->mesh.edges, h );
        const auto *half = edge != nullptr ? GenerationPool_Get( &mesh->mesh.halfEdges, edge->hHalfEdge ) : nullptr;
        const auto *next = half != nullptr ? GenerationPool_Get( &mesh->mesh.halfEdges, half->hNext ) : nullptr;
        for ( const auto *end : { half, next } ) {
            if ( end == nullptr ) { continue; }
            const u64 id = geo::MeshSource_VertexId( mesh, end->hOrigin ).value;
            if ( std::any_of( c.vertices.begin(), c.vertices.end(), [&]( id_t v ) { return v.value == id; } ) ) { interior = false; return false; }
        }
        return true;
    } );
    return interior;
}

u32 DissolveState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) ) { return State( InteriorVertices( ws, c ) ); }
    return State( SelectedEdges( ws, c ) && BoundaryCount( ws, c ) < c.edges.size() );
}
command_result_t Dissolve( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( DissolveState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    if ( SelectedVertices( ws, c ) ) {
        return Result( ApplyKeeping( ws, "Dissolve Vertices", [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_DissolveVertices( copy, c.mesh, Span( c.vertices ) );
        } ) );
    }
    if ( !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    return Result( ApplyKeeping( ws, "Dissolve Edges", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_DissolveEdges( copy, c.mesh, Span( c.edges ) );
    } ) );
}

u32 EdgesState( void *pContext ) noexcept { components_t c; return State( SelectedEdges( Ws( pContext ), c ) ); }

command_result_t SplitEdges( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    return Result( ApplyKeeping( ws, "Split Edges", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_SplitEdges( copy, c.mesh, Span( c.edges ) );
    } ) );
}

u32 ConnectState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) ) { return State( CanConnectVertices( ws, c ) ); }
    return State( SelectedEdges( ws, c ) && c.edges.size() >= 2 );
}
command_result_t Connect( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) && CanConnectVertices( ws, c ) ) {
        const bool done = ApplyVertexEdit( ws, "Connect Vertices", c, [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_ConnectVertexSet( copy, c.mesh, Span( c.vertices ) );
        } );
        return Result( done );
    }
    if ( SelectedEdges( ws, c ) && c.edges.size() >= 2 ) {
        map_status_t status = map_status_t::OK;
        const bool done = ApplyKeeping( ws, "Connect Edges", [&]( map_document_t *copy ) noexcept {
            status = MapTopologyEdit_ConnectEdges( copy, c.mesh, Span( c.edges ) );
            return status;
        } );
        if ( !done && status == map_status_t::INVALID_ARGUMENT ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Connect: every selected edge must share a face with another selected edge, with at most two selected edges per face. Remove unrelated edges and try again." ) );
        }
        return Result( done );
    }
    return command_result_t::DISABLED;
}

// Border edges whose two ends are selected vertices: what Fill Hole closes
// from Vertices mode.
std::vector<edge_t> BorderEdgesOf( const map_workspace_t *ws, const components_t &c )
{
    std::vector<edge_t> out;
    const auto *mesh = Mesh( ws, c.mesh );
    for ( usize i = 0; mesh != nullptr && i < c.vertices.size(); ++i ) {
        for ( usize j = i + 1; j < c.vertices.size(); ++j ) {
            geo::geometry_mesh_edge_handle_t handle{};
            if ( geo::MeshSourceEdit_TryFindEdge( mesh, c.vertices[i], c.vertices[j], &handle ) && geo::MeshBoundary_IsBoundaryEdge( &mesh->mesh, handle ) ) {
                out.push_back( geo::MeshEdgeRef_Make( c.vertices[i], c.vertices[j] ) );
            }
        }
    }
    return out;
}

u32 FillHoleState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) ) { return State( c.vertices.size() >= 2 && !BorderEdgesOf( ws, c ).empty() ); }
    return State( SelectedEdges( ws, c ) && BoundaryCount( ws, c ) != 0 );
}
command_result_t FillHole( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( FillHoleState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    if ( SelectedVertices( ws, c ) ) {
        const auto rims = BorderEdgesOf( ws, c );
        const bool filled = ApplyKeeping( ws, "Fill Hole", [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_FillHoles( copy, c.mesh, Span( rims ) );
        } );
        return Result( filled );
    }
    if ( !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    const bool done = ApplyKeeping( ws, "Fill Hole", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_FillHoles( copy, c.mesh, Span( c.edges ) );
    } );
    return Result( done );
}

// The selected edges as exactly two open chains of equal length, each as its
// vertices in order (what Bridge joins strip by strip).
bool TwoChains( const std::vector<edge_t> &edges, std::vector<id_t> &a, std::vector<id_t> &b )
{
    a.clear(); b.clear();
    std::vector<bool> used( edges.size(), false );
    const auto degree = [&]( u64 v ) { usize n = 0; for ( const edge_t &e : edges ) { n += ( e.a.value == v ) + ( e.b.value == v ); } return n; };
    for ( int chain = 0; chain < 2; ++chain ) {
        auto &path = chain == 0 ? a : b;
        u64 start = 0;
        for ( usize i = 0; i < edges.size() && start == 0; ++i ) {
            if ( used[i] ) { continue; }
            for ( u64 v : { edges[i].a.value, edges[i].b.value } ) { if ( degree( v ) == 1 ) { start = v; break; } }
        }
        if ( start == 0 ) { return false; } // A closed loop, or nothing left.
        path.push_back( { start } );
        for ( bool advanced = true; advanced; ) {
            advanced = false;
            for ( usize i = 0; i < edges.size(); ++i ) {
                if ( used[i] || ( edges[i].a.value != path.back().value && edges[i].b.value != path.back().value ) ) { continue; }
                used[i] = true; advanced = true;
                path.push_back( { edges[i].a.value == path.back().value ? edges[i].b.value : edges[i].a.value } );
                break;
            }
        }
    }
    for ( usize i = 0; i < edges.size(); ++i ) { if ( !used[i] ) { return false; } } // A third piece.
    for ( usize i = 0; i < edges.size(); ++i ) { if ( degree( edges[i].a.value ) > 2 || degree( edges[i].b.value ) > 2 ) { return false; } }
    return a.size() == b.size() && a.size() >= 3;
}

u32 BridgeState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c; u64 mesh = 0; std::vector<u64> faces;
    if ( SelectedFaces( ws, mesh, faces ) ) { return State( faces.size() == 2 ); }
    if ( !SelectedEdges( ws, c ) || BoundaryCount( ws, c ) != c.edges.size() ) { return COMMAND_STATE_NONE; }
    std::vector<id_t> a, b;
    return State( c.edges.size() == 2 || TwoChains( c.edges, a, b ) );
}
command_result_t Bridge( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext ); components_t c; u64 mesh = 0; std::vector<u64> faces;
    if ( SelectedFaces( ws, mesh, faces ) ) {
        if ( faces.size() != 2 ) { return command_result_t::DISABLED; }
        f64 values[1]{}; usize count = 0;
        if ( !Numbers( args, values, 1, count ) || ( count == 1 && ( values[0] < 1.0 || values[0] > 64.0 ) ) ) { return command_result_t::INVALID_ARGUMENTS; }
        const u32 segments = count == 1 ? static_cast<u32>( values[0] ) : 1u;
        const bool bridged = ApplyKeeping( ws, "Bridge Faces", [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_BridgeFaces( copy, mesh, faces[0], faces[1], segments );
        } );
        if ( !bridged && ws->lastEdit.outcome == map_edit_outcome_t::FAILED ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Bridge joins two faces of one mesh that share no corner and have the same number of corners, "
                                                                       "with room for the tube between them: to bore through a box, Inset its top and bottom first and bridge the inset faces" ) );
        }
        return Result( bridged );
    }
    if ( BridgeState( pContext ) == COMMAND_STATE_NONE || !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    std::vector<id_t> chainA, chainB;
    const bool chains = c.edges.size() > 2 && TwoChains( c.edges, chainA, chainB );
    const bool done = ApplyKeeping( ws, "Bridge Edges", [&]( map_document_t *copy ) noexcept {
        return chains ? MapTopologyEdit_BridgeEdgeChains( copy, c.mesh, Span( chainA ), Span( chainB ), 1 )
                      : MapTopologyEdit_BridgeEdges( copy, c.mesh, c.edges[0], c.edges[1] );
    } );
    return Result( done );
}

u32 BevelState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) ) { return State( InteriorVertices( ws, c ) ); }
    return State( SelectedEdges( ws, c ) );
}
command_result_t Bevel( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    const f64 width = Real( ws, "editor.map.mesh_bevel_width", 8.0 );
    if ( SelectedVertices( ws, c ) ) {
        if ( !InteriorVertices( ws, c ) ) { return command_result_t::DISABLED; }
        return Result( ApplyKeeping( ws, "Bevel Vertices", [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_BevelVertices( copy, c.mesh, Span( c.vertices ), width );
        } ) );
    }
    if ( !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    const auto segments = static_cast<u32>( std::clamp<i64>( EditorSettings_Integer( &ws->pGui->settings, "editor.map.mesh_bevel_segments", 1 ), 1, 32 ) );
    const bool beveled = ApplyKeeping( ws, "Bevel Edges", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_BevelEdges( copy, c.mesh, Span( c.edges ), width, segments );
    } );
    if ( !beveled && ws->lastEdit.outcome == map_edit_outcome_t::FAILED ) {
        MapNotice_Post( ws, map_notice_t::WARNING, BoundaryCount( ws, c ) != 0
            ? QStringLiteral( "Bevel rounds edges between two faces; an edge on the mesh's open border has only one" )
            : QStringLiteral( "Bevel: the width (%1) is too wide for these edges - the cut would cross the next edge. Lower it in Tool Properties" ).arg( width ) );
    }
    return Result( beveled );
}

u32 OpenEdgesState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    return State( SelectedEdges( ws, c ) && BoundaryCount( ws, c ) == c.edges.size() );
}
template <map_edge_extrude_t kDirection>
command_result_t ExtrudeEdges( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( OpenEdgesState( pContext ) == COMMAND_STATE_NONE || !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    const f64 distance = Real( ws, "editor.map.mesh_edge_extrude_distance", 16.0 );
    geo::mesh_selection_t next{};
    if ( geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { c.mesh } ) != geo::geometry_status_t::OK ) { return command_result_t::FAILED; }
    const map_prepared_component_selection_t preparedSelection{ &next };
    const bool done = ApplyKeeping( ws, kDirection == map_edge_extrude_t::NORMAL ? "Extrude Edges" : "Extend Edges", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_ExtrudeEdges( copy, c.mesh, Span( c.edges ), distance, kDirection, &next.edges );
    }, &preparedSelection );
    geo::MeshSelection_Shutdown( &next );
    return Result( done );
}

template <map_edge_normals_t kNormals>
command_result_t EdgeNormals( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    const char *label = kNormals == map_edge_normals_t::HARD ? "Hard Normals" : kNormals == map_edge_normals_t::SOFT ? "Soft Normals" : "Default Normals";
    const bool done = ApplyKeeping( ws, label, [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_SetEdgeNormals( copy, c.mesh, Span( c.edges ), kNormals );
    } );
    return Result( done );
}

u32 VerticesOrEdgesState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    return State( SelectedVertices( ws, c ) || SelectedEdges( ws, c ) );
}
command_result_t WeldUvs( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    const bool vertices = SelectedVertices( ws, c );
    if ( !vertices && !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    const auto ids = vertices ? c.vertices : Endpoints( c.edges );
    const bool done = ApplyKeeping( ws, "Weld UVs", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_WeldUvs( copy, c.mesh, Span( ids ) );
    } );
    return Result( done );
}

u32 RadialAlignState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) ) { return State( c.vertices.size() >= 3 ); }
    return State( SelectedEdges( ws, c ) && Endpoints( c.edges ).size() >= 3 );
}
command_result_t RadialAlign( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( RadialAlignState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const bool vertices = SelectedVertices( ws, c );
    if ( !vertices ) { ( void )SelectedEdges( ws, c ); }
    const auto ids = vertices ? c.vertices : Endpoints( c.edges );
    const bool done = ApplyKeeping( ws, "Radial Align", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_RadialAlign( copy, c.mesh, Span( ids ) );
    } );
    return Result( done );
}

u32 DetachFaceState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( MapWorkspace_CanEditMeshFace( ws ) || !SeparableMeshes( ws ).empty() );
}
command_result_t DetachFace( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( const auto meshes = SeparableMeshes( ws ); !meshes.empty() ) {
        return Result( Apply( ws, "Separate Meshes", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
            // Untouched selected objects stay selected beside the new pieces.
            for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
                if ( std::find( meshes.begin(), meshes.end(), ws->selection.ids.pData[i] ) == meshes.end() &&
                     !Vector_PushBack( out, ws->selection.ids.pData[i] ) ) { return map_status_t::OUT_OF_MEMORY; }
            }
            for ( u64 id : meshes ) {
                vector_t<u64> roots{};
                if ( !Vector_Init( &roots, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
                auto status = MapTopologyEdit_SeparateShells( copy, id, &roots );
                for ( usize k = 0; status == map_status_t::OK && k < roots.nCount; ++k ) {
                    if ( !Vector_PushBack( out, roots.pData[k] ) ) { status = map_status_t::OUT_OF_MEMORY; }
                }
                Vector_Shutdown( &roots );
                if ( status != map_status_t::OK ) { return status; }
            }
            return map_status_t::OK;
        } ) );
    }
    u64 mesh = 0; std::vector<u64> faces;
    if ( !SelectedFaces( ws, mesh, faces ) ) { return command_result_t::DISABLED; }
    const bool done = ApplyKeeping( ws, faces.size() == 1 ? "Detach Face" : "Detach Faces", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_DetachFaces( copy, mesh, { faces.data(), faces.size() } );
    } );
    return Result( done );
}

// Runs a face query on a private set (seeded with the selected faces, or
// empty) and publishes the result; the active face stays active if it is
// still in it. An empty result clears the face selection.
template <typename Query>
command_result_t RunFaceQuery( map_workspace_t *ws, bool seed, Query query ) noexcept
{
    u64 mesh = 0; std::vector<u64> faces;
    if ( !SelectedFaces( ws, mesh, faces ) ) { return command_result_t::DISABLED; }
    const u64 active = ws->selectedMeshFaceId;
    geo::mesh_selection_t next{};
    bool ok = geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { mesh } ) == geo::geometry_status_t::OK;
    for ( usize i = 0; ok && seed && i < faces.size(); ++i ) { ok = geo::MeshSelection_TryAddFace( &next, { faces[i] } ) == geo::geometry_status_t::OK; }
    ok = ok && query( next, Mesh( ws, mesh ), active ) == geo::geometry_status_t::OK;
    std::vector<u64> result;
    for ( usize i = 0; i < next.faces.nCount; ++i ) { result.push_back( next.faces.pData[i].value ); }
    geo::MeshSelection_Shutdown( &next );
    if ( !ok ) { return command_result_t::FAILED; }
    if ( result.empty() ) { MapWorkspace_ClearMeshFace( ws ); return command_result_t::OK; }
    const u64 keep = std::find( result.begin(), result.end(), active ) != result.end() ? active : result.front();
    return Result( MapWorkspace_SetMeshFaces( ws, mesh, { result.data(), result.size() }, keep ) );
}

// The wire face of the active mesh face: its normal, corners and material.
const map_wire_face_t *ActiveWireFace( const map_workspace_t *ws ) noexcept
{
    for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
        const auto &face = ws->wire.faces.pData[f];
        if ( face.id == ws->selectedMeshFaceObject && face.faceId == ws->selectedMeshFaceId ) { return &face; }
    }
    return nullptr;
}

// Grow and shrink run on a private copy of the component set; only the
// final set is published, so a failure part-way selects nothing new.
template <bool kGrow>
u32 GrowShrinkState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c; u64 mesh = 0; std::vector<u64> faces;
    return State( SelectedVertices( ws, c ) || SelectedEdges( ws, c ) || SelectedFaces( ws, mesh, faces ) );
}
template <bool kGrow>
command_result_t GrowShrink( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( ws->elementMode == map_element_mode_t::FACES ) {
        return RunFaceQuery( ws, true, []( geo::mesh_selection_t &next, const geo::mesh_source_t *mesh, u64 ) noexcept {
            return kGrow ? geo::MeshSelection_TryGrow( &next, mesh, geo::mesh_selection_mode_t::FACE ) : geo::MeshSelection_TryShrink( &next, mesh, geo::mesh_selection_mode_t::FACE );
        } );
    }
    const bool vertices = SelectedVertices( ws, c );
    if ( !vertices && !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    geo::mesh_selection_t next{};
    if ( geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { c.mesh } ) != geo::geometry_status_t::OK ) { return command_result_t::FAILED; }
    bool ok = true;
    for ( const id_t &id : c.vertices ) { ok = ok && geo::MeshSelection_TryAddVertex( &next, id ) == geo::geometry_status_t::OK; }
    for ( const edge_t &edge : c.edges ) { ok = ok && geo::MeshSelection_TryAddEdge( &next, edge ) == geo::geometry_status_t::OK; }
    const auto mode = vertices ? geo::mesh_selection_mode_t::VERTEX : geo::mesh_selection_mode_t::EDGE;
    if ( ok ) {
        ok = ( kGrow ? geo::MeshSelection_TryGrow( &next, Mesh( ws, c.mesh ), mode )
                     : geo::MeshSelection_TryShrink( &next, Mesh( ws, c.mesh ), mode ) ) == geo::geometry_status_t::OK;
    }
    std::vector<id_t> grownVertices( next.vertices.pData, next.vertices.pData + next.vertices.nCount );
    std::vector<edge_t> grownEdges( next.edges.pData, next.edges.pData + next.edges.nCount );
    geo::MeshSelection_Shutdown( &next );
    if ( !ok ) { return command_result_t::FAILED; }
    if ( vertices ? grownVertices.size() == c.vertices.size() : grownEdges.size() == c.edges.size() ) { return command_result_t::OK; }
    if ( vertices ? grownVertices.empty() : grownEdges.empty() ) {
        vertices ? MapWorkspace_ClearMeshVertices( ws ) : MapWorkspace_ClearMeshEdges( ws );
        return command_result_t::OK;
    }
    vertices ? RestoreVertices( ws, c.mesh, grownVertices ) : RestoreEdges( ws, c.mesh, grownEdges );
    return command_result_t::OK;
}

/*
================
Mesh object commands
================
*/

u32 MeshObjectsState( void *pContext ) noexcept { return State( MapWorkspace_CanEditMeshSelection( Ws( pContext ) ) ); }

// Hammer 5 has only meshes, so its mesh operations always apply. Here a
// selection of brushes and meshes counts too: the brushes become meshes in
// the same edit (one undo step), then the operation runs.
bool MeshesOrBrushes( const map_workspace_t *ws ) noexcept
{
    if ( !Ready( ws ) || !ObjectMode( ws ) || ws->selection.ids.nCount == 0 || !MapWorkspace_CanEditSelection( ws ) ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object == nullptr || ( object->kind != map_wire_kind_t::MESH && object->kind != map_wire_kind_t::BRUSH ) ) { return false; }
    }
    return true;
}

map_status_t ConvertSelectedBrushes( map_document_t *copy, span_t<const u64> ids ) noexcept
{
    std::vector<u64> brushes;
    for ( usize i = 0; i < ids.nCount; ++i ) { if ( geo::GeometryDocument_FindBrush( &copy->geometry, { ids.pData[i] } ) != nullptr ) { brushes.push_back( ids.pData[i] ); } }
    if ( brushes.empty() ) { return map_status_t::OK; }
    vector_t<u64> roots{};
    if ( !Vector_Init( &roots, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto status = MapMeshEdit_ConvertBrushes( copy, { brushes.data(), brushes.size() }, &roots );
    Vector_Shutdown( &roots );
    return status;
}

u32 SmoothState( void *pContext ) noexcept { return State( MeshesOrBrushes( Ws( pContext ) ) ); }
command_result_t Smooth( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !MeshesOrBrushes( ws ) ) { return command_result_t::DISABLED; }
    const f64 strength = Real( ws, "editor.map.mesh_smooth_strength", 0.5 );
    return Result( ApplyKeeping( ws, "Smooth Meshes", [&]( map_document_t *copy ) noexcept {
        const auto converted = ConvertSelectedBrushes( copy, Selected( ws ) );
        return converted != map_status_t::OK ? converted : MapTopologyEdit_Smooth( copy, Selected( ws ), strength, 1 );
    } ) );
}

u32 SubdivideState( void *pContext ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !MeshesOrBrushes( ws ) ) { return COMMAND_STATE_NONE; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        if ( Mesh( ws, ws->selection.ids.pData[i] ) != nullptr && MapTopologyEdit_HasFaceRecords( ws->pDocument, ws->selection.ids.pData[i] ) ) { return COMMAND_STATE_NONE; }
    }
    return COMMAND_STATE_ENABLED;
}
command_result_t Subdivide( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( SubdivideState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    return Result( ApplyKeeping( ws, "Subdivide Meshes", [&]( map_document_t *copy ) noexcept {
        const auto converted = ConvertSelectedBrushes( copy, Selected( ws ) );
        return converted != map_status_t::OK ? converted : MapTopologyEdit_Subdivide( copy, Selected( ws ) );
    } ) );
}

/*
================
Modifiers: Hammer 5's subdivision levels and non-destructive mirror, arrays,
bend and taper, kept in each mesh's record and shown evaluated
================
*/

// The meshes a modifier command acts on: the one being edited in a
// component mode, else the selected meshes and brushes (brushes become
// meshes in the same step).
std::vector<u64> ModifierTargets( const map_workspace_t *ws )
{
    std::vector<u64> out;
    if ( !Ready( ws ) ) { return out; }
    if ( !ObjectMode( ws ) ) {
        const u64 mesh = ws->selectedMeshFaceObject != 0 ? ws->selectedMeshFaceObject : ws->meshSelection.meshId.value;
        if ( mesh != 0 && Mesh( ws, mesh ) != nullptr ) { out.push_back( mesh ); }
        else if ( ws->selection.ids.nCount == 1 && Mesh( ws, ws->selection.ids.pData[0] ) != nullptr ) { out.push_back( ws->selection.ids.pData[0] ); }
        return out;
    }
    if ( !MeshesOrBrushes( ws ) ) { return out; }
    out.assign( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
    return out;
}

bool HasStack( const map_workspace_t *ws, const std::vector<u64> &meshes ) noexcept
{
    for ( u64 id : meshes ) {
        std::vector<geo::modifier_t> stages;
        if ( Mesh( ws, id ) != nullptr && MapModifiers_Read( ws->pDocument, id, stages ) == map_status_t::OK && !stages.empty() ) { return true; }
    }
    return false;
}

// One undo step: convert brushes, then change each mesh's stack.
template <typename Change>
bool ChangeStacks( map_workspace_t *ws, const char *label, const std::vector<u64> &meshes, Change change ) noexcept
{
    return ApplyKeeping( ws, label, [&]( map_document_t *copy ) noexcept {
        auto status = ConvertSelectedBrushes( copy, { meshes.data(), meshes.size() } );
        for ( usize i = 0; status == map_status_t::OK && i < meshes.size(); ++i ) { status = change( copy, meshes[i] ); }
        return status;
    } );
}

u32 ModifierState( void *pContext ) noexcept { return State( !ModifierTargets( Ws( pContext ) ).empty() ); }
u32 StackState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( HasStack( ws, ModifierTargets( ws ) ) ); }

void SubdivisionNotice( map_workspace_t *ws, const std::vector<u64> &meshes ) noexcept
{
    if ( meshes.empty() ) { return; }
    const u32 level = MapModifiers_SubdivisionLevel( ws->pDocument, meshes.front() );
    MapNotice_Post( ws, map_notice_t::SUCCESS, level == 0 ? QStringLiteral( "Subdivision off (level 0)" ) : QStringLiteral( "Subdivision level %1" ).arg( level ) );
}

template <int kStep>
command_result_t SubdivisionStep( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto meshes = ModifierTargets( ws );
    if ( meshes.empty() ) { return command_result_t::DISABLED; }
    const bool done = ChangeStacks( ws, kStep > 0 ? "Increase Subdivision" : "Decrease Subdivision", meshes, []( map_document_t *copy, u64 mesh ) noexcept {
        const i64 level = static_cast<i64>( MapModifiers_SubdivisionLevel( copy, mesh ) ) + kStep;
        bool changed = false;
        return level < 0 || level > static_cast<i64>( geo::kSubdivisionLevelsMax ) ? map_status_t::OK : MapModifiers_SetSubdivisionLevel( copy, mesh, static_cast<u32>( level ), &changed );
    } );
    if ( done ) { SubdivisionNotice( ws, meshes ); }
    return Result( done );
}

template <u32 kLevel>
command_result_t SubdivisionLevel( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto meshes = ModifierTargets( ws );
    if ( meshes.empty() ) { return command_result_t::DISABLED; }
    const bool done = ChangeStacks( ws, "Set Subdivision Level", meshes, []( map_document_t *copy, u64 mesh ) noexcept {
        bool changed = false;
        return MapModifiers_SetSubdivisionLevel( copy, mesh, kLevel, &changed );
    } );
    if ( done ) { SubdivisionNotice( ws, meshes ); }
    return Result( done );
}

// Apply: every mesh with a stack becomes its evaluated surface.
command_result_t ApplyModifiers( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto meshes = ModifierTargets( ws );
    if ( !HasStack( ws, meshes ) ) { return command_result_t::DISABLED; }
    const bool done = ApplyKeeping( ws, "Apply Modifiers", [&]( map_document_t *copy ) noexcept {
        for ( u64 mesh : meshes ) {
            std::vector<geo::modifier_t> stages;
            if ( MapModifiers_Read( copy, mesh, stages ) != map_status_t::OK || stages.empty() ) { continue; }
            const auto status = MapModifiers_Apply( copy, mesh );
            if ( status != map_status_t::OK ) { return status; }
        }
        return map_status_t::OK;
    } );
    MapNotice_Post( ws, done ? map_notice_t::SUCCESS : map_notice_t::ERROR, done ? QStringLiteral( "Modifiers applied: the mesh is now the result" )
                                                                           : QStringLiteral( "Apply Modifiers failed; nothing changed" ) );
    return Result( done );
}

template <bool kLastOnly>
command_result_t RemoveModifiers( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto meshes = ModifierTargets( ws );
    if ( !HasStack( ws, meshes ) ) { return command_result_t::DISABLED; }
    return Result( ChangeStacks( ws, kLastOnly ? "Remove Last Modifier" : "Clear Modifiers", meshes, []( map_document_t *copy, u64 mesh ) noexcept {
        std::vector<geo::modifier_t> stages;
        auto status = MapModifiers_Read( copy, mesh, stages );
        if ( status != map_status_t::OK || stages.empty() ) { return status; }
        if ( kLastOnly ) { stages.pop_back(); } else { stages.clear(); }
        bool changed = false;
        return MapModifiers_Write( copy, mesh, stages, &changed );
    } ) );
}

// Where a modifier works around: the pivot when set, else (for mirrors) the
// far side of the selection, so the copy lands beside it.
math::vec3d_t ModifierPivot( const map_workspace_t *ws, bool &hasPivot ) noexcept
{
    math::vec3d_t pivot{};
    hasPivot = MapToolGestures_Pivot( ws, &pivot );
    return pivot;
}

map_bounds_t TargetBounds( const map_workspace_t *ws, const std::vector<u64> &meshes ) noexcept
{
    map_bounds_t bounds{};
    for ( u64 id : meshes ) { if ( const auto *object = MapWireframe_FindObject( ws->wire, id ) ) { MapBounds_AddBounds( bounds, object->bounds ); } }
    return bounds;
}

f64 Coord( math::vec3d_t v, u32 axis ) noexcept { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

u32 AxisSetting( const map_workspace_t *ws, const char *path ) noexcept
{
    const auto axis = EditorSettings_Text( &ws->pGui->settings, path, StringView_FromCString( "x" ) );
    return StringView_Equals( axis, StringView_FromCString( "y" ) ) ? 1u : StringView_Equals( axis, StringView_FromCString( "z" ) ) ? 2u : 0u;
}

template <geo::modifier_kind_t kKind, u32 kAxis>
command_result_t AddModifier( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto meshes = ModifierTargets( ws );
    if ( meshes.empty() ) { return command_result_t::DISABLED; }
    const map_bounds_t bounds = TargetBounds( ws, meshes );
    bool hasPivot = false;
    const math::vec3d_t pivot = ModifierPivot( ws, hasPivot );
    geo::modifier_t m{};
    m.kind = kKind;
    switch ( kKind ) {
        case geo::modifier_kind_t::MIRROR:
            m.axis = kAxis; m.offset = hasPivot ? Coord( pivot, kAxis ) : Coord( bounds.box.maximum, kAxis );
            m.bWeld = true; m.weldDistance = 0.01;
            break;
        case geo::modifier_kind_t::LINEAR_ARRAY: {
            const u32 axis = AxisSetting( ws, "editor.map.array_axis" );
            f64 distance = Real( ws, "editor.map.array_distance", 0.0 );
            // Distance 0: copies side by side. Copies of a solid that touched
            // face to face would share those faces (not a valid solid), so
            // they stand half a grid step apart.
            if ( distance == 0.0 ) { distance = Coord( bounds.box.maximum, axis ) - Coord( bounds.box.minimum, axis ) + std::max( 1.0, ws->gridSize * 0.5 ); }
            m.cCopies = static_cast<u32>( std::clamp( Real( ws, "editor.map.array_count", 3.0 ), 2.0, 256.0 ) );
            m.step = { axis == 0 ? distance : 0.0, axis == 1 ? distance : 0.0, axis == 2 ? distance : 0.0 };
            m.bWeld = true; m.weldDistance = 0.01;
            break;
        }
        case geo::modifier_kind_t::RADIAL_ARRAY: {
            m.cCopies = static_cast<u32>( std::clamp( Real( ws, "editor.map.radial_count", 8.0 ), 2.0, 256.0 ) );
            // Without a pivot the copies circle an axis beside the object,
            // far enough out that they do not overlap one another (an axis
            // through the object itself would stack every copy on it).
            const math::vec3d_t centre = MapBounds_Center( bounds );
            const f64 width = std::max( bounds.box.maximum.x - bounds.box.minimum.x, bounds.box.maximum.y - bounds.box.minimum.y );
            const f64 radius = std::max( width, width * m.cCopies / ( 2.0 * M_PI ) * 1.25 );
            m.origin = hasPivot ? pivot : math::vec3d_t{ centre.x - radius, centre.y, centre.z };
            m.direction = { 0, 0, 1 }; m.angle = 2.0 * M_PI;
            m.bWeld = true; m.weldDistance = 0.01;
            break;
        }
        case geo::modifier_kind_t::BEND:
            m.axis = 2; m.alongAxis = AxisSetting( ws, "editor.map.array_axis" ) == 2 ? 0 : AxisSetting( ws, "editor.map.array_axis" );
            m.origin = hasPivot ? pivot : MapBounds_Center( bounds );
            m.angle = Real( ws, "editor.map.bend_angle", 90.0 ) * M_PI / 180.0;
            break;
        case geo::modifier_kind_t::TAPER:
            m.alongAxis = 2; m.factor = Real( ws, "editor.map.taper_factor", 0.5 );
            break;
        case geo::modifier_kind_t::SUBDIVIDE: break;
    }
    const QString name = QString::fromLatin1( MapModifiers_TypeName( kKind ) ).replace( QLatin1Char( '_' ), QLatin1Char( ' ' ) );
    const bool done = ChangeStacks( ws, "Add Modifier", meshes, [&]( map_document_t *copy, u64 mesh ) noexcept {
        std::vector<geo::modifier_t> stages;
        auto status = MapModifiers_Read( copy, mesh, stages );
        if ( status != map_status_t::OK ) { return status; }
        // Subdivision stays last: the shape is built before it is smoothed.
        auto at = std::find_if( stages.begin(), stages.end(), []( const geo::modifier_t &x ) { return x.kind == geo::modifier_kind_t::SUBDIVIDE; } );
        const auto index = at - stages.begin();
        stages.insert( at, m );
        bool changed = false;
        status = MapModifiers_Write( copy, mesh, stages, &changed );
        // A closed mesh mirrored across its own face would share that face
        // with its copy (non-manifold): the plane moves out half a grid step
        // so the copy sits just beside it. An open half mesh welds instead.
        geo::modifier_result_t probe{};
        if ( status == map_status_t::OK && kKind == geo::modifier_kind_t::MIRROR && !hasPivot &&
             geo::ModifierResult_Init( &probe, copy->pAllocator ) == geo::geometry_status_t::OK && MapModifiers_Evaluate( copy, mesh, &probe ) != map_status_t::OK ) {
            stages[static_cast<usize>( index )].offset += std::max( 1.0, ws->gridSize * 0.5 );
            status = MapModifiers_Write( copy, mesh, stages, &changed );
        }
        geo::ModifierResult_Shutdown( &probe );
        if ( status != map_status_t::OK ) { return status; }
        // A stack that does not evaluate is refused rather than left invisible.
        geo::modifier_result_t check{};
        if ( geo::ModifierResult_Init( &check, copy->pAllocator ) != geo::geometry_status_t::OK ) { return map_status_t::OUT_OF_MEMORY; }
        status = MapModifiers_Evaluate( copy, mesh, &check );
        geo::ModifierResult_Shutdown( &check );
        return status;
    } );
    MapNotice_Post( ws, done ? map_notice_t::SUCCESS : map_notice_t::ERROR, done ? QStringLiteral( "Added %1 (Apply Modifiers bakes it; Remove Last undoes it)" ).arg( name )
                                                                           : QStringLiteral( "The %1 modifier does not work on this mesh (it would fold or tear it)" ).arg( name ) );
    return Result( done );
}

/*
================
Create Displacement: a Source-style displacement grid on a picked face
================
*/

// The picked brush face or mesh face, when it has four corners (as the
// wireframe winds them: counter-clockwise seen from outside).
bool PickedQuad( const map_workspace_t *ws, math::vec3d_t ( &corners )[4] ) noexcept
{
    if ( !Ready( ws ) ) { return false; }
    const bool brush = MapWorkspace_HasBrushFace( ws ), mesh = !brush && MapWorkspace_HasMeshFace( ws );
    if ( !brush && !mesh ) { return false; }
    for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
        const auto &face = ws->wire.faces.pData[f];
        const bool match = brush ? face.id == ws->selectedBrushFaceObject && face.sideId == ws->selectedBrushFaceSide
                                 : face.id == ws->selectedMeshFaceObject && face.faceId == ws->selectedMeshFaceId;
        if ( !match ) { continue; }
        if ( face.nIndices != 4 ) { return false; }
        for ( u32 k = 0; k < 4; ++k ) { corners[k] = ws->wire.points.pData[ws->wire.faceIndices.pData[face.iFirstIndex + k]]; }
        return true;
    }
    return false;
}

f64 MeshRepeat( const map_workspace_t *ws ) noexcept; // With the texture commands.

u32 CreateDisplacementState( void *pContext ) noexcept { math::vec3d_t corners[4]{}; return State( PickedQuad( Ws( pContext ), corners ) ); }
command_result_t CreateDisplacement( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    map_displacement_desc_t desc{};
    if ( !PickedQuad( ws, desc.corners ) ) { return command_result_t::DISABLED; }
    desc.power = static_cast<u32>( std::clamp( Real( ws, "editor.map.displacement_power", 3.0 ), 1.0, 4.0 ) );
    desc.elevation = Real( ws, "editor.map.displacement_elevation", 0.0 );
    desc.noiseAmplitude = Real( ws, "editor.map.displacement_noise", 0.0 );
    desc.noiseScale = std::max( 1.0, Real( ws, "editor.map.displacement_noise_scale", 64.0 ) );
    desc.noiseOctaves = static_cast<u32>( std::clamp( Real( ws, "editor.map.displacement_octaves", 3.0 ), 1.0, 8.0 ) );
    desc.seed = static_cast<u32>( std::max( 0.0, Real( ws, "editor.map.displacement_seed", 1.0 ) ) );
    const auto material = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
    const f64 repeat = MeshRepeat( ws );
    u64 created = 0;
    const bool done = Apply( ws, "Create Displacement", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        const auto status = MapCreate_Displacement( copy, desc, material, repeat, &created );
        return status == map_status_t::OK && !Vector_PushBack( out, created ) ? map_status_t::OUT_OF_MEMORY : status;
    } );
    if ( done ) {
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
        MapWorkspace_Select( ws, created, MAP_SELECT_REPLACE );
        const u32 side = ( 1u << desc.power ) + 1u;
        MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Created a power %1 displacement (%2 × %2 vertices): sculpt it with the Displacement tool" ).arg( desc.power ).arg( side ) );
    }
    return Result( done );
}

/*
================
Compile Map: the geometry cook (collision, visibility, navigation,
lightmap charts, batches) and the interchange file compilers read
================
*/

u32 CompileState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( ws != nullptr && ws->pDocument != nullptr && ws->pGui != nullptr && !EditorHistory_IsTransactionOpen( &ws->history ) );
}
command_result_t CompileMap( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( CompileState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    if ( ws->path.isEmpty() ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Compile Map: save the map first; the compiled output goes beside it" ) );
        return command_result_t::FAILED;
    }
    map_compile_options_t options{};
    options.walkableSlopeDegrees = Real( ws, "editor.map.compile_walkable_slope", 45.0 );
    options.lightmapTexelsPerUnit = 1.0 / std::max( 1.0, Real( ws, "editor.map.compile_luxel_size", 16.0 ) );
    map_compile_report_t report;
    // The compiled level holds the prefabs' objects, not the instances
    // (CYMAP.md 6.12): they are collapsed into a copy first.
    map_document_t *copy = nullptr;
    QStringList instanceProblems;
    auto status = MapEdit_Clone( ws->pDocument, &copy );
    if ( status == map_status_t::OK ) { status = MapInstances_CollapseAll( ws, copy, &instanceProblems ); }
    if ( status == map_status_t::OK ) { status = MapCompile_Run( copy, options, report ); }
    delete copy;
    for ( const QString &problem : instanceProblems ) { report.problems.push_back( problem.toStdString() ); }
    if ( status != map_status_t::OK ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Compile Map failed: %1" ).arg( QString::fromUtf8( MapDocument_StatusName( status ) ) ) );
        return command_result_t::FAILED;
    }
    const QFileInfo map( ws->path );
    const QString output = map.absoluteDir().filePath( map.completeBaseName() + QStringLiteral( ".interchange.cykv" ) );
    QFile file( output );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) || file.write( report.interchange.data(), static_cast<qint64>( report.interchange.size() ) ) != static_cast<qint64>( report.interchange.size() ) ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Compile Map: \"%1\" could not be written" ).arg( QDir::toNativeSeparators( output ) ) );
        return command_result_t::FAILED;
    }
    for ( const auto &problem : report.problems ) { MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Compile: skipped %1" ).arg( QString::fromStdString( problem ) ) ); }
    MapNotice_Post( ws, report.problems.empty() ? map_notice_t::SUCCESS : map_notice_t::WARNING,
        QStringLiteral( "Compiled in %1 ms: %2 objects, %3 triangles; collision %4 surfaces; %5 walkable triangles; %6 lightmap charts on %7 pages; %8 batches%9 \u2014 \"%10\"" )
            .arg( report.milliseconds, 0, 'f', 1 ).arg( report.objects ).arg( report.triangles ).arg( report.collisionSurfaces ).arg( report.walkableTriangles )
            .arg( report.lightmapCharts ).arg( report.lightmapPages ).arg( report.batches )
            .arg( report.problems.empty() ? QString() : QStringLiteral( "; %1 skipped (see the console)" ).arg( report.problems.size() ) )
            .arg( QDir::toNativeSeparators( output ) ) );
    return command_result_t::OK;
}

/*
================
Brush face tools: Patch from Face (NetRadiant), Brush Sweep
================
*/

u32 BrushFaceState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( Ready( ws ) && MapWorkspace_HasBrushFace( ws ) ); }

command_result_t PatchFromFace( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( BrushFaceState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const u64 brush = ws->selectedBrushFaceObject, side = ws->selectedBrushFaceSide;
    usize made = 0;
    const bool done = Apply( ws, "Patch from Face", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        const auto status = MapPatchEdit_FromBrushFace( copy, brush, side, out );
        made = out->nCount;
        return status;
    } );
    if ( done ) {
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
        MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "%1 flat %2 cover the face: bend them with the patch tools" ).arg( made ).arg( made == 1 ? QStringLiteral( "patch" ) : QStringLiteral( "patches" ) ) );
    }
    return Result( done );
}

// Hammer's texture wrap over a floor or wall built from many brushes: the
// picked face's material and alignment continue over every coplanar face
// joined to it, so the texture runs on without seams.
command_result_t WrapCoplanar( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( BrushFaceState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const map_surface_ref_t seed{ ws->selectedBrushFaceObject, ws->selectedBrushFaceSide };
    std::vector<map_surface_ref_t> faces;
    auto status = MapTextureEdit_CoplanarBrushSides( ws->pDocument, seed, faces );
    usize changed = 0;
    const bool done = status == map_status_t::OK && ApplyKeeping( ws, "Wrap Texture Across Coplanar Faces", [&]( map_document_t *copy ) noexcept {
        status = MapTextureEdit_CopyBrushSide( copy, seed, { faces.data(), faces.size() }, &changed );
        return status;
    } );
    if ( done ) {
        MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Texture continued across %1 coplanar %2" ).arg( changed ).arg( changed == 1 ? QStringLiteral( "face" ) : QStringLiteral( "faces" ) ) );
    } else if ( status == map_status_t::OK ) {
        MapNotice_Post( ws, map_notice_t::INFO, faces.size() <= 1 ? QStringLiteral( "No other brush face lies in this face's plane and touches it" )
                                                                  : QStringLiteral( "The %1 coplanar faces already continue this texture" ).arg( faces.size() ) );
    }
    return Result( done );
}

// Hammer 5's Apply Hotspot: each face into the best region of its material.
command_result_t ApplyHotspots( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !Ready( ws ) ) { return command_result_t::DISABLED; }
    return Result( MapGeometryCommands_ApplyHotspots( ws ) );
}
u32 HotspotState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( Ready( ws ) && !MapGeometryCommands_TextureTargets( ws ).empty() ); }

// TrenchBroom's File > Export > Wavefront OBJ (beside it, a .mtl).
command_result_t ExportObj( void *pContext, const command_args_t &args ) noexcept
try
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pDocument == nullptr ) { return command_result_t::DISABLED; }
    QString path = args.nArgs >= 1 ? QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) ) : QString();
    if ( path.isEmpty() ) {
        const QString suggested = ws->path.isEmpty() ? QStringLiteral( "map.obj" ) : QFileInfo( ws->path ).absoluteDir().filePath( QFileInfo( ws->path ).completeBaseName() + QStringLiteral( ".obj" ) );
        path = QFileDialog::getSaveFileName( QApplication::activeWindow(), QStringLiteral( "Export Wavefront OBJ" ), suggested, QStringLiteral( "Wavefront OBJ (*.obj)" ) );
        if ( path.isEmpty() ) { return command_result_t::OK; } // Cancelled: nothing to report.
    }
    if ( !path.endsWith( QStringLiteral( ".obj" ), Qt::CaseInsensitive ) ) { path += QStringLiteral( ".obj" ); }
    const QString mtlPath = path.left( path.size() - 4 ) + QStringLiteral( ".mtl" );
    std::string obj, mtl;
    map_export_report_t report{};
    // Resolve external prefab content before serializing any output. A missing
    // instance must not disappear behind a successful OBJ notification.
    map_document_t *raw = nullptr;
    auto status = MapEdit_Clone( ws->pDocument, &raw );
    std::unique_ptr<map_document_t> copy( raw );
    QStringList problems;
    if ( status == map_status_t::OK ) { copy->bReadOnly = false; status = MapInstances_CollapseAll( ws, copy.get(), &problems ); }
    if ( status == map_status_t::OK && problems.isEmpty() ) {
        status = MapExport_Obj( copy.get(), QFileInfo( mtlPath ).fileName().toStdString(), obj, mtl, &report );
    }
    if ( status != map_status_t::OK || !problems.isEmpty() || report.skippedInstances != 0 ) {
        const QString reason = !problems.isEmpty() ? problems.join( QStringLiteral( "; " ) ) : report.skippedInstances != 0
            ? QStringLiteral( "%1 prefab instances could not be resolved" ).arg( report.skippedInstances )
            : QString::fromUtf8( MapDocument_StatusName( status ) );
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Export failed before writing files: %1" ).arg( reason ) );
        return command_result_t::FAILED;
    }
    QSaveFile objFile( path ), mtlFile( mtlPath );
    objFile.setDirectWriteFallback( false ); mtlFile.setDirectWriteFallback( false );
    // Prepare both temporary files before replacing either destination. This
    // protects an existing OBJ when its sibling MTL cannot even be prepared.
    QString failedPath;
    if ( !objFile.open( QIODevice::WriteOnly ) || objFile.write( obj.data(), static_cast<qint64>( obj.size() ) ) != static_cast<qint64>( obj.size() ) ) { failedPath = path; }
    else if ( !mtlFile.open( QIODevice::WriteOnly ) || mtlFile.write( mtl.data(), static_cast<qint64>( mtl.size() ) ) != static_cast<qint64>( mtl.size() ) ) { failedPath = mtlPath; }
    if ( !failedPath.isEmpty() ) {
        objFile.cancelWriting(); mtlFile.cancelWriting();
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Export failed before replacing files: could not prepare %1" ).arg( QDir::toNativeSeparators( failedPath ) ) );
        return command_result_t::FAILED;
    }
    // Each replacement is atomic individually. Two filesystem commits cannot
    // promise pair atomicity; prepare truthful notices before the first commit.
    const QString objFailure = QStringLiteral( "Export failed: could not replace %1; neither export file was replaced" ).arg( QDir::toNativeSeparators( path ) );
    const QString mtlFailure = QStringLiteral( "Export incomplete: %1 was replaced, but %2 could not be replaced. The two files were not updated together." )
        .arg( QDir::toNativeSeparators( path ), QDir::toNativeSeparators( mtlPath ) );
    const QString success = QStringLiteral( "Exported %1 objects (%2 triangles) to %3%4" ).arg( report.objects ).arg( report.triangles ).arg( QDir::toNativeSeparators( path ) )
        .arg( report.skipped == 0 ? QString() : QStringLiteral( "; %1 terrain objects skipped (unsupported)" ).arg( report.skippedTerrain ) );
    if ( !objFile.commit() ) {
        mtlFile.cancelWriting(); MapNotice_Post( ws, map_notice_t::ERROR, objFailure ); return command_result_t::FAILED;
    }
    if ( !mtlFile.commit() ) {
        MapNotice_Post( ws, map_notice_t::ERROR, mtlFailure ); return command_result_t::FAILED;
    }
    MapNotice_Post( ws, report.skipped == 0 ? map_notice_t::SUCCESS : map_notice_t::WARNING, success );
    return command_result_t::OK;
}
catch ( ... )
{
    MapNotice_Post( Ws( pContext ), map_notice_t::ERROR, QStringLiteral( "Export failed: not enough memory to prepare OBJ output" ) );
    return command_result_t::FAILED;
}

// Import Wavefront OBJ: each object of the file becomes an editable mesh,
// selected (scale and Y-up from the settings).
command_result_t ImportObj( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !Ready( ws ) ) { return command_result_t::DISABLED; }
    QString path = args.nArgs >= 1 ? QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) ) : QString();
    if ( path.isEmpty() ) {
        path = QFileDialog::getOpenFileName( QApplication::activeWindow(), QStringLiteral( "Import Wavefront OBJ" ), ws->path.isEmpty() ? QString() : QFileInfo( ws->path ).absolutePath(),
                                             QStringLiteral( "Wavefront OBJ (*.obj)" ) );
        if ( path.isEmpty() ) { return command_result_t::OK; } // Cancelled: nothing to report.
    }
    QFile in( path );
    if ( !in.open( QIODevice::ReadOnly ) ) { MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Import: cannot read %1" ).arg( QDir::toNativeSeparators( path ) ) ); return command_result_t::FAILED; }
    const QByteArray text = in.readAll();
    const auto material = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
    map_import_options_t options{};
    options.scale = Real( ws, "editor.map.import_scale", 1.0 );
    options.bYUp = EditorSettings_Bool( &ws->pGui->settings, "editor.map.import_y_up", CY_FALSE ) != CY_FALSE;
    options.defaultMaterial = material;
    options.worldUnitsPerUv = 512.0 * Real( ws, "editor.map.default_texture_scale", 0.25 );
    map_import_report_t report{};
    map_status_t status = map_status_t::OK;
    const bool done = Apply( ws, "Import OBJ", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        std::vector<u64> meshes;
        status = MapImport_Obj( copy, { text.constData(), static_cast<usize>( text.size() ) }, options, meshes, &report );
        if ( status != map_status_t::OK ) { return status; }
        for ( u64 id : meshes ) { if ( !Vector_PushBack( out, id ) ) { return map_status_t::OUT_OF_MEMORY; } }
        return status;
    } );
    if ( done ) {
        MapNotice_Post( ws, report.skipped == 0 ? map_notice_t::SUCCESS : map_notice_t::WARNING,
            QStringLiteral( "Imported %1 meshes (%2 faces) from %3" ).arg( report.meshes ).arg( report.faces ).arg( QFileInfo( path ).fileName() ) +
            ( report.triangulated != 0 ? QStringLiteral( "; %1 bent faces split into triangles" ).arg( report.triangulated ) : QString() ) +
            ( report.skipped != 0 ? QStringLiteral( "; %1 degenerate faces skipped" ).arg( report.skipped ) : QString() ) );
    } else {
        MapNotice_Post( ws, map_notice_t::ERROR, status == map_status_t::GEOMETRY_FAILED ? QStringLiteral( "Import: the faces of %1 do not form a valid mesh (open fans, edges shared by more than two faces, or mixed winding)" ).arg( QFileInfo( path ).fileName() )
                                                                                     : QStringLiteral( "Import failed (%1)" ).arg( QString::fromUtf8( MapDocument_StatusName( status ) ) ) );
    }
    return Result( done );
}

// Hammer's Find Entities (Ctrl+F) and Find and Replace (Ctrl+Shift+H).
template <bool kReplace>
command_result_t FindEntities( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pDocument == nullptr ) { return command_result_t::DISABLED; }
    return Result( MapFindEntities_Show( ws, QApplication::activeWindow(), kReplace ) != nullptr );
}

// Hammer 5's Fast Texture window (CypherMapGui_FastTexture).
u32 FastTextureState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( Ready( ws ) ); }
command_result_t FastTexture( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( FastTextureState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    return Result( MapFastTexture_Show( ws, QApplication::activeWindow() ) != nullptr );
}

command_result_t BrushSweep( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( BrushFaceState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const map_brush_face_t face{ ws->selectedBrushFaceObject, ws->selectedBrushFaceSide };
    // The face's outward normal, from the wireframe.
    math::vec3d_t normal{ 0, 0, 1 }, centre{};
    for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
        const auto &w = ws->wire.faces.pData[f];
        if ( w.id != face.brush || w.sideId != face.side ) { continue; }
        normal = w.normal;
        for ( u32 k = 0; k < w.nIndices; ++k ) {
            const auto p = ws->wire.points.pData[ws->wire.faceIndices.pData[w.iFirstIndex + k]];
            centre = { centre.x + p.x / w.nIndices, centre.y + p.y / w.nIndices, centre.z + p.z / w.nIndices };
        }
    }
    const auto pathText = EditorSettings_Text( &ws->pGui->settings, "editor.map.sweep_path", StringView_FromCString( "straight" ) );
    const map_sweep_path_t path = StringView_Equals( pathText, StringView_FromCString( "arc" ) ) ? map_sweep_path_t::ARC :
                                  StringView_Equals( pathText, StringView_FromCString( "s_bend" ) ) ? map_sweep_path_t::S_BEND : map_sweep_path_t::STRAIGHT;
    const f64 distance = Real( ws, "editor.map.sweep_distance", 128.0 ), sideways = Real( ws, "editor.map.sweep_sideways", 64.0 );
    // Sideways for an S-bend: across the face, level with the floor.
    math::vec3d_t across{ -normal.y, normal.x, 0.0 };
    const f64 acrossLength = std::sqrt( across.x * across.x + across.y * across.y );
    across = acrossLength > 1e-6 ? math::vec3d_t{ across.x / acrossLength, across.y / acrossLength, 0.0 } : math::vec3d_t{ 1, 0, 0 };
    math::vec3d_t offset{ normal.x * distance, normal.y * distance, normal.z * distance };
    if ( path == map_sweep_path_t::S_BEND ) { offset = { offset.x + across.x * sideways, offset.y + across.y * sideways, offset.z + across.z * sideways }; }
    // An arc turns about the vertical through the pivot, or beside the face.
    bool hasPivot = false;
    const math::vec3d_t pivot = ModifierPivot( ws, hasPivot );
    const math::vec3d_t axisOrigin = hasPivot ? pivot : math::vec3d_t{ centre.x + across.x * sideways, centre.y + across.y * sideways, centre.z };
    const f64 angle = Real( ws, "editor.map.sweep_angle", 90.0 ) * M_PI / 180.0;
    const u32 segments = static_cast<u32>( std::clamp( Real( ws, "editor.map.sweep_segments", 4.0 ), 1.0, 64.0 ) );
    const u32 iterations = static_cast<u32>( std::clamp( Real( ws, "editor.map.sweep_iterations", 1.0 ), 1.0, 64.0 ) );
    usize made = 0;
    const bool done = Apply( ws, "Brush Sweep", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        const auto status = MapCreate_BrushSweep( copy, { &face, 1 }, path, offset, axisOrigin, { 0, 0, 1 }, angle, segments, iterations, out );
        made = out->nCount;
        return status;
    } );
    if ( done ) {
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
        MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Swept the face into %1 brushes" ).arg( made ) );
    } else {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Brush Sweep: the path gives a step no volume (zero distance or angle, or an arc axis in the face's plane)" ) );
    }
    return Result( done );
}

/*
================
UV seams and islands; Repair Mesh
================
*/

u32 SeamState( void *pContext ) noexcept { components_t c; return State( SelectedEdges( Ws( pContext ), c ) ); }
template <map_seam_edit_t kEdit>
command_result_t EditSeams( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    const bool done = ApplyKeeping( ws, kEdit == map_seam_edit_t::SET ? "Mark Seams" : "Clear Seams", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_EditSeams( copy, c.mesh, Span( c.edges ), kEdit );
    } );
    return Result( done );
}

u32 UvIslandState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( ComponentReady( ws ) && MapWorkspace_HasMeshFace( ws ) ); }
command_result_t SelectUvIsland( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( UvIslandState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const u64 mesh = ws->selectedMeshFaceObject, face = ws->selectedMeshFaceId;
    std::vector<u64> island;
    usize islands = 0;
    if ( MapTopologyEdit_UvIsland( ws->pDocument, mesh, face, island, &islands ) != map_status_t::OK ) { return command_result_t::FAILED; }
    ( void )MapWorkspace_SetMeshFaces( ws, mesh, { island.data(), island.size() }, face );
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "UV island: %1 faces (the mesh has %2 islands)" ).arg( island.size() ).arg( islands ) );
    return command_result_t::OK;
}

std::vector<u64> CleanupMeshes( const map_workspace_t *ws ); // With Remove Bad Faces.
u32 RepairState( void *pContext ) noexcept { return State( !CleanupMeshes( Ws( pContext ) ).empty() ); }
command_result_t RepairMesh( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto meshes = CleanupMeshes( ws );
    if ( meshes.empty() ) { return command_result_t::DISABLED; }
    const f64 weld = Real( ws, "editor.map.mesh_weld_distance", 1.0 );
    const auto material = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
    const f64 repeat = MeshRepeat( ws );
    map_repair_report_t total{};
    const bool done = ApplyKeeping( ws, "Repair Mesh", [&]( map_document_t *copy ) noexcept {
        for ( u64 mesh : meshes ) {
            map_repair_report_t report{};
            const auto status = MapTopologyEdit_Repair( copy, mesh, weld, material, repeat, &report );
            if ( status != map_status_t::OK ) { return status; }
            total.welded += report.welded; total.degenerate += report.degenerate; total.duplicates += report.duplicates; total.flipped += report.flipped;
            total.holesFilled += report.holesFilled; total.holesOpen += report.holesOpen; total.nonManifoldEdges += report.nonManifoldEdges;
        }
        return map_status_t::OK;
    } );
    if ( !done ) { MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Repair Mesh: the mesh could not be rebuilt" ) ); return command_result_t::FAILED; }
    MapNotice_Post( ws, total.holesOpen != 0 || total.nonManifoldEdges != 0 ? map_notice_t::WARNING : map_notice_t::SUCCESS,
        QStringLiteral( "Repaired: %1 vertices welded, %2 degenerate and %3 duplicate faces removed, %4 faces flipped, %5 holes capped%6" )
            .arg( total.welded ).arg( total.degenerate ).arg( total.duplicates ).arg( total.flipped ).arg( total.holesFilled )
            .arg( total.holesOpen != 0 || total.nonManifoldEdges != 0 ? QStringLiteral( "; %1 holes left open (not flat), %2 non-manifold edges" ).arg( total.holesOpen ).arg( total.nonManifoldEdges ) : QString() ) );
    return command_result_t::OK;
}

u32 SolidifyState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    if ( !MapWorkspace_CanEditMeshSelection( ws ) ) { return COMMAND_STATE_NONE; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *mesh = Mesh( ws, ws->selection.ids.pData[i] );
        if ( mesh == nullptr || geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) == 0 ) { return COMMAND_STATE_NONE; }
    }
    return COMMAND_STATE_ENABLED;
}
command_result_t Solidify( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( SolidifyState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const f64 thickness = Real( ws, "editor.map.mesh_solidify_thickness", 16.0 );
    return Result( ApplyKeeping( ws, "Solidify Meshes", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_Solidify( copy, Selected( ws ), thickness );
    } ) );
}

/*
================
Object commands
================
*/

bool WholeObjectsEditable( const map_workspace_t *ws ) noexcept { return Ready( ws ) && ObjectMode( ws ) && MapWorkspace_CanEditSelection( ws ); }
bool WholeObjectsMovable( const map_workspace_t *ws ) noexcept { return Ready( ws ) && ObjectMode( ws ) && MapWorkspace_CanMoveSelection( ws ); }

// This broad-phase scan is also used by command-state queries. It must not
// allocate: refreshing a menu is not a fallible geometry preparation step.
bool CarveTarget( const map_workspace_t *ws, const map_wire_object_t &object, span_t<const u64> cutters ) noexcept
{
    if ( object.kind != map_wire_kind_t::BRUSH || !MapWorkspace_IsVisible( ws, object ) ) { return false; }
    for ( usize i = 0; i < cutters.nCount; ++i ) { if ( cutters.pData[i] == object.id ) { return false; } }
    for ( usize i = 0; i < cutters.nCount; ++i ) {
        const auto *cutter = MapWireframe_FindObject( ws->wire, cutters.pData[i] );
        if ( cutter != nullptr && Overlaps( cutter->bounds, object.bounds ) ) { return true; }
    }
    return false;
}

map_status_t CarveLockedTargets( const map_workspace_t *ws, span_t<const u64> targets, span_t<const u64> cutters,
    u64 *blocked ) noexcept
{
    *blocked = 0;
    for ( usize i = 0; i < targets.nCount; ++i ) {
        const u64 targetId = targets.pData[i];
        if ( !MapLayers_IsObjectLocked( ws, targetId ) ) { continue; }
        const auto *target = geo::GeometryDocument_FindBrush( &ws->pDocument->geometry, { targetId } );
        const auto *wire = MapWireframe_FindObject( ws->wire, targetId );
        if ( target == nullptr || wire == nullptr ) { return map_status_t::GEOMETRY_FAILED; }
        geo::brush_boundary_t targetBoundary{};
        auto status = geo::BrushBoundary_Init( &targetBoundary, ws->pGui->pAllocator );
        if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &targetBoundary, target, ws->pDocument->geometryPolicy ); }
        if ( status != geo::geometry_status_t::OK ) { return status == geo::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED; }
        for ( usize k = 0; k < cutters.nCount; ++k ) {
            const auto *cutterWire = MapWireframe_FindObject( ws->wire, cutters.pData[k] );
            if ( cutterWire == nullptr ) { return map_status_t::GEOMETRY_FAILED; }
            if ( !Overlaps( wire->bounds, cutterWire->bounds ) ) { continue; }
            const auto *cutter = geo::GeometryDocument_FindBrush( &ws->pDocument->geometry, { cutters.pData[k] } );
            if ( cutter == nullptr ) { return map_status_t::GEOMETRY_FAILED; }
            geo::brush_boundary_t cutterBoundary{};
            status = geo::BrushBoundary_Init( &cutterBoundary, ws->pGui->pAllocator );
            if ( status == geo::geometry_status_t::OK ) { status = geo::BrushBoundary_TryReconstruct( &cutterBoundary, cutter, ws->pDocument->geometryPolicy ); }
            if ( status != geo::geometry_status_t::OK ) { return status == geo::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED; }
            const auto relation = geo::BrushCSG_Classify( target, &targetBoundary, cutter, &cutterBoundary,
                ws->pDocument->geometryPolicy.numerical.fCoplanarDistanceTolerance );
            if ( relation == geo::brush_csg_classification_t::INVALID ) { return map_status_t::GEOMETRY_FAILED; }
            // AABB overlap alone is not a write: adjacent faces and separated
            // angled brushes may share bounds without sharing any volume.
            if ( relation != geo::brush_csg_classification_t::DISJOINT && relation != geo::brush_csg_classification_t::TOUCHING ) {
                *blocked = targetId; return map_status_t::OK;
            }
        }
    }
    return map_status_t::OK;
}

u32 CarveState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    if ( !Ready( ws ) || !ObjectMode( ws ) || !MapWorkspace_CanEditBrushSelection( ws ) ) { return COMMAND_STATE_NONE; }
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        if ( CarveTarget( ws, ws->wire.objects.pData[i], Selected( ws ) ) ) { return COMMAND_STATE_ENABLED; }
    }
    return COMMAND_STATE_NONE;
}
command_result_t Carve( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( CarveState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto fail = [&]( map_status_t status ) noexcept {
        MapNotice_Post( ws, map_notice_t::ERROR, status == map_status_t::OUT_OF_MEMORY ?
            QStringLiteral( "Carve: not enough memory to prepare the edit; no brushes were changed." ) :
            QStringLiteral( "Carve: the geometry could not be prepared; no brushes were changed." ) );
        return command_result_t::FAILED;
    };
    vector_t<u64> cutters{}, initial{};
    if ( !Vector_Init( &cutters, ws->pGui->pAllocator ) || !Vector_Append( &cutters, Selected( ws ) ) ||
         !Vector_Init( &initial, ws->pGui->pAllocator ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    const span_t<const u64> cutterIds{ cutters.pData, cutters.nCount };
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        if ( CarveTarget( ws, object, cutterIds ) && !Vector_PushBack( &initial, object.id ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    }
    u64 blocked = 0;
    const auto lockStatus = CarveLockedTargets( ws, { initial.pData, initial.nCount }, cutterIds, &blocked );
    if ( lockStatus != map_status_t::OK ) { return fail( lockStatus ); }
    if ( blocked != 0 ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Carve would change brush #%1 on a locked layer. Unlock it and retry; no brushes were changed." ).arg( blocked ) );
        return command_result_t::FAILED;
    }
    const bool done = Apply( ws, "Carve", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        if ( !Vector_Append( out, cutterIds ) ) { return map_status_t::OUT_OF_MEMORY; }
        // Each cutter carves what the previous ones left: its fragments
        // replace the brush they came from in the next cutter's target list.
        vector_t<u64> targets{};
        if ( !Vector_Init( &targets, copy->pAllocator ) || !Vector_Append( &targets, { initial.pData, initial.nCount } ) ) { return map_status_t::OUT_OF_MEMORY; }
        auto status = map_status_t::OK;
        for ( usize i = 0; status == map_status_t::OK && i < cutters.nCount && targets.nCount != 0; ++i ) {
            vector_t<u64> roots{};
            if ( !Vector_Init( &roots, copy->pAllocator ) ) { status = map_status_t::OUT_OF_MEMORY; break; }
            status = MapBrushEdit_Subtract( copy, { targets.pData, targets.nCount }, cutters.pData[i], &roots );
            if ( status == map_status_t::OK ) { Vector_Shutdown( &targets ); Vector_Move( &targets, &roots ); }
        }
        return status;
    } );
    if ( done ) { return command_result_t::OK; }
    if ( ws->lastEdit.outcome == map_edit_outcome_t::UNCHANGED && ws->lastEdit.status == map_status_t::OK ) {
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Carve: the brushes do not share interior volume; no changes were needed." ) );
        return command_result_t::OK;
    }
    return fail( ws->lastEdit.status );
}

u32 EditableObjectsState( void *pContext ) noexcept { return State( WholeObjectsEditable( Ws( pContext ) ) ); }
// Quarter turns also turn entities (their angles).
u32 TurnableObjectsState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( Ready( ws ) && ObjectMode( ws ) && MapWorkspace_CanTurnSelection( ws ) );
}

// The Pivot tool's point when one is set, else the selection's centre.
math::vec3d_t TransformCenter( const map_workspace_t *ws ) noexcept
{
    math::vec3d_t pivot{};
    return MapToolGestures_Pivot( ws, &pivot ) ? pivot : MapBounds_Center( SelectionBounds( ws ) );
}

template <bool kVertical>
command_result_t Flip( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !WholeObjectsEditable( ws ) ) { return command_result_t::DISABLED; }
    u32 u = 0, v = 1; ViewAxes( ws, u, v );
    const u32 axis = kVertical ? v : u; // Flipping horizontally mirrors across the view's vertical axis.
    const auto center = TransformCenter( ws );
    math::planed_t plane{ AxisVector( axis, 1.0 ), -Axis( center, axis ) };
    const bool lock = ws->bTextureLock != CY_FALSE;
    return Result( ApplyKeeping( ws, kVertical ? "Flip Vertically" : "Flip Horizontally", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_Mirror( copy, Selected( ws ), plane, lock );
    } ) );
}

template <bool kClockwise>
command_result_t Rotate90( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( TurnableObjectsState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    u32 u = 0, v = 1; ViewAxes( ws, u, v );
    const u32 depth = 3u - u - v;
    // Screen-clockwise is negative about the axis pointing out of the view
    // (u x v): +Z for Top, +X for Front, -Y for Side.
    const f64 outward = ( u == 0 && v == 2 ) ? -1.0 : 1.0;
    const auto degrees = AxisVector( depth, ( kClockwise ? -90.0 : 90.0 ) * outward );
    return Result( MapWorkspace_RotateSelection( ws, degrees, TransformCenter( ws ) ) );
}

u32 AlignState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( WholeObjectsMovable( ws ) && ws->selection.ids.nCount >= 2 );
}

// Roots whose owning entity is not also selected: moving both would move
// tied geometry twice.
std::vector<u64> MovableRoots( const map_workspace_t *ws )
{
    std::vector<u64> roots;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object != nullptr && ( object->owner == 0 || !EditorSelection_Contains( &ws->selection, object->owner ) ) ) { roots.push_back( object->id ); }
    }
    return roots;
}

enum class align_side_t : u8 { LEFT, RIGHT, TOP, BOTTOM };
template <align_side_t kSide>
command_result_t Align( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( AlignState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    u32 u = 0, v = 1; ViewAxes( ws, u, v );
    const bool horizontal = kSide == align_side_t::LEFT || kSide == align_side_t::RIGHT;
    const bool maximum = kSide == align_side_t::RIGHT || kSide == align_side_t::TOP;
    const u32 axis = horizontal ? u : v;
    const auto bounds = SelectionBounds( ws );
    const f64 target = Axis( maximum ? bounds.box.maximum : bounds.box.minimum, axis );
    const auto roots = MovableRoots( ws );
    const bool lock = ws->bTextureLock != CY_FALSE;
    const char *labels[]{ "Align Left", "Align Right", "Align Top", "Align Bottom" };
    return Result( ApplyKeeping( ws, labels[static_cast<u32>( kSide )], [&]( map_document_t *copy ) noexcept {
        for ( u64 id : roots ) {
            const auto *object = MapWireframe_FindObject( ws->wire, id );
            const f64 offset = target - Axis( maximum ? object->bounds.box.maximum : object->bounds.box.minimum, axis );
            if ( offset == 0.0 ) { continue; }
            const auto status = MapEdit_Translate( copy, { &id, 1 }, AxisVector( axis, offset ), lock );
            if ( status != map_status_t::OK ) { return status; }
        }
        return map_status_t::OK;
    } ) );
}

// Objects: Hammer's Snap Selection to Grid moves each object so the minimum
// corner of its bounds lands on the grid. Vertices: each moves to the nearest
// grid point.
u32 SnapState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    return State( SelectedVertices( ws, c ) || WholeObjectsMovable( ws ) );
}
command_result_t SnapToGrid( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    const f64 grid = ws != nullptr ? ws->gridSize : 0.0;
    if ( SelectedVertices( ws, c ) ) {
        const bool done = ApplyKeeping( ws, "Snap Vertices to Grid", [&]( map_document_t *copy ) noexcept {
            return MapTopologyEdit_SnapVertices( copy, c.mesh, Span( c.vertices ), grid );
        } );
        return Result( done );
    }
    if ( !WholeObjectsMovable( ws ) ) { return command_result_t::DISABLED; }
    const auto roots = MovableRoots( ws );
    const bool lock = ws->bTextureLock != CY_FALSE;
    return Result( ApplyKeeping( ws, "Snap Selection to Grid", [&]( map_document_t *copy ) noexcept {
        for ( u64 id : roots ) {
            const auto *object = MapWireframe_FindObject( ws->wire, id );
            if ( object == nullptr || !object->bounds.bHas ) { continue; }
            const auto &low = object->bounds.box.minimum;
            const math::vec3d_t offset{ std::round( low.x / grid ) * grid - low.x, std::round( low.y / grid ) * grid - low.y,
                                        std::round( low.z / grid ) * grid - low.z };
            if ( offset.x == 0.0 && offset.y == 0.0 && offset.z == 0.0 ) { continue; }
            const auto status = MapEdit_Translate( copy, { &id, 1 }, offset, lock );
            if ( status != map_status_t::OK ) { return status; }
        }
        return map_status_t::OK;
    } ) );
}

/*
================
Selection queries
================
*/

bool Selectable( const map_workspace_t *ws, const map_wire_object_t &object ) noexcept
{
    return MapWorkspace_IsVisible( ws, object ) && MapWorkspace_IsSelectableObject( ws, object );
}

u32 TouchingState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( ws != nullptr && ws->pDocument != nullptr && ObjectMode( ws ) && ws->selection.ids.nCount != 0 );
}
// Adds every visible object whose bounds meet a selected object's bounds;
// the selection itself stays selected (TrenchBroom's Select Touching
// without deleting the selecting brush).
command_result_t SelectTouching( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( TouchingState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    std::vector<u64> ids( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        if ( EditorSelection_Contains( &ws->selection, object.id ) || !Selectable( ws, object ) ) { continue; }
        for ( usize k = 0; k < ws->selection.ids.nCount; ++k ) {
            const auto *selected = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[k] );
            if ( selected != nullptr && Overlaps( selected->bounds, object.bounds ) ) { ids.push_back( object.id ); break; }
        }
    }
    if ( ids.size() == ws->selection.ids.nCount ) {
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Select Touching: nothing else touches the selection" ) );
        return command_result_t::OK;
    }
    MapWorkspace_SetSelection( ws, ids.data(), ids.size() );
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Select Touching: %1 more selected" ).arg( ids.size() - ws->selection.ids.nCount ) );
    return command_result_t::OK;
}

// TrenchBroom's Select Inside and Select Tall: the selection becomes every
// other visible object whose bounds lie within the selected objects' bounds
// (Tall: in the last 2D view's two axes, at any depth). The selecting
// objects themselves drop out, as a selection volume would.
template <bool kTall>
command_result_t SelectInside( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( TouchingState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const map_bounds_t volume = SelectionBounds( ws );
    if ( !volume.bHas ) { return command_result_t::FAILED; }
    u32 u = 0, v = 1;
    ViewAxes( ws, u, v );
    constexpr f64 kSlack = 1e-6;
    const auto inside = [&]( const map_bounds_t &b ) noexcept {
        for ( u32 axis = 0; axis < 3; ++axis ) {
            if ( kTall && axis != u && axis != v ) { continue; }
            if ( Axis( b.box.minimum, axis ) < Axis( volume.box.minimum, axis ) - kSlack || Axis( b.box.maximum, axis ) > Axis( volume.box.maximum, axis ) + kSlack ) { return false; }
        }
        return true;
    };
    std::vector<u64> ids;
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        if ( EditorSelection_Contains( &ws->selection, object.id ) || !Selectable( ws, object ) || !object.bounds.bHas ) { continue; }
        if ( inside( object.bounds ) ) { ids.push_back( object.id ); }
    }
    const QString label = kTall ? QStringLiteral( "Select Tall" ) : QStringLiteral( "Select Inside" );
    if ( ids.empty() ) { // Nothing inside: the selection stays.
        MapNotice_Post( ws, map_notice_t::INFO, kTall ? QStringLiteral( "Select Tall: no other object lies within the selection's footprint (in the last 2D view)" )
                                                      : QStringLiteral( "Select Inside: no other object lies entirely inside the selection's bounds" ) );
        return command_result_t::FAILED;
    }
    MapWorkspace_SetSelection( ws, ids.data(), ids.size() );
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "%1: %2 selected" ).arg( label ).arg( ids.size() ) );
    return command_result_t::OK;
}

// Materials of the selected face, or of every face of the selected objects.
std::vector<u64> SelectedMaterials( const map_workspace_t *ws )
{
    std::vector<u64> materials;
    const bool brushFace = ws->selectedBrushFaceObject != 0, meshFace = ws->selectedMeshFaceObject != 0;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i];
        const bool take = brushFace ? face.id == ws->selectedBrushFaceObject && face.sideId == ws->selectedBrushFaceSide :
                          meshFace ? face.id == ws->selectedMeshFaceObject && face.faceId == ws->selectedMeshFaceId :
                          EditorSelection_Contains( &ws->selection, face.id ) != CY_FALSE;
        if ( take && face.material != 0 ) { materials.push_back( face.material ); }
    }
    std::sort( materials.begin(), materials.end() ); materials.erase( std::unique( materials.begin(), materials.end() ), materials.end() );
    return materials;
}

u32 SameMaterialState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( ws != nullptr && ws->pDocument != nullptr && ws->elementMode != map_element_mode_t::VERTICES &&
        ws->elementMode != map_element_mode_t::EDGES && ws->elementMode != map_element_mode_t::NAVIGATION && !SelectedMaterials( ws ).empty() );
}
// Selects every visible object with a face in one of those materials.
command_result_t SelectSameMaterial( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( SameMaterialState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto materials = SelectedMaterials( ws );
    std::vector<u64> ids;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i];
        if ( !std::binary_search( materials.begin(), materials.end(), face.material ) || ( !ids.empty() && ids.back() == face.id ) ) { continue; }
        const auto *object = MapWireframe_FindObject( ws->wire, face.id );
        if ( object != nullptr && Selectable( ws, *object ) ) { ids.push_back( face.id ); }
    }
    if ( ids.empty() ) { return command_result_t::FAILED; }
    MapWorkspace_SetSelection( ws, ids.data(), ids.size() );
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Select Same Material: %1 %2 selected (%3)" )
        .arg( ids.size() ).arg( ids.size() == 1 ? QStringLiteral( "object" ) : QStringLiteral( "objects" ),
              materials.size() == 1 ? QStringLiteral( "1 material" ) : QStringLiteral( "%1 materials" ).arg( materials.size() ) ) );
    return command_result_t::OK;
}
/*
================
Texture commands
================
*/

// The faces a texture command acts on: the selected brush side or mesh face
// in Faces mode; every face of the selected brushes and meshes otherwise.
std::vector<map_surface_ref_t> Surfaces( const map_workspace_t *ws )
{
    std::vector<map_surface_ref_t> out;
    if ( !Ready( ws ) ) { return out; }
    if ( ws->elementMode == map_element_mode_t::FACES ) {
        if ( MapWorkspace_HasBrushFace( ws ) ) { out.push_back( { ws->selectedBrushFaceObject, ws->selectedBrushFaceSide } ); }
        else if ( MapWorkspace_HasMeshFace( ws ) ) {
            const auto faces = MapWorkspace_MeshFaces( ws );
            for ( usize i = 0; i < faces.nCount; ++i ) { out.push_back( { ws->selectedMeshFaceObject, faces.pData[i] } ); }
        }
        return out;
    }
    if ( !ObjectMode( ws ) ) { return out; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object != nullptr && MapWorkspace_IsVisible( ws, *object ) &&
             ( object->kind == map_wire_kind_t::BRUSH || object->kind == map_wire_kind_t::MESH ) ) { out.push_back( { object->id, 0 } ); }
    }
    return out;
}

// Mesh faces carry no projection record; their World/Face repeat size is the
// one new faces get (editor.map.default_texture_scale).
f64 MeshRepeat( const map_workspace_t *ws ) noexcept
{
    map_bounds_t unit{}; MapBounds_AddPoint( unit, {} ); MapBounds_AddPoint( unit, { 1, 1, 1 } );
    const f64 repeat = MapWorkspace_PrimitiveDefaults( ws, unit ).worldUnitsPerUv;
    return std::isfinite( repeat ) && repeat > 0.0 ? repeat : 128.0;
}

u32 TextureState( void *pContext ) noexcept { return State( !Surfaces( Ws( pContext ) ).empty() ); }

template <map_uv_align_t kAlign>
command_result_t TextureAlign( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto surfaces = Surfaces( ws );
    if ( surfaces.empty() ) { return command_result_t::DISABLED; }
    const char *labels[]{ "Align Material to World", "Align Material to Face", "Fit Material", "Justify Material Left", "Justify Material Right",
                          "Justify Material Top", "Justify Material Bottom", "Center Material" };
    const f64 repeat = MeshRepeat( ws );
    return Result( ApplyKeeping( ws, labels[static_cast<u32>( kAlign )], [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_Align( copy, { surfaces.data(), surfaces.size() }, kAlign, repeat );
    } ) );
}

command_result_t TextureRotate( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    f64 values[1]{ 90.0 }; usize n = 0;
    if ( !Numbers( args, values, 1, n ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const auto surfaces = Surfaces( ws );
    if ( surfaces.empty() ) { return command_result_t::DISABLED; }
    return Result( ApplyKeeping( ws, "Rotate Material", [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_Rotate( copy, { surfaces.data(), surfaces.size() }, values[0] );
    } ) );
}

command_result_t TextureScale( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    f64 values[2]{ 2.0, 2.0 }; usize n = 0;
    if ( !Numbers( args, values, 2, n ) ) { return command_result_t::INVALID_ARGUMENTS; }
    if ( n == 1 ) { values[1] = values[0]; }
    const auto surfaces = Surfaces( ws );
    if ( surfaces.empty() ) { return command_result_t::DISABLED; }
    return Result( ApplyKeeping( ws, "Scale Material", [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_Scale( copy, { surfaces.data(), surfaces.size() }, { values[0], values[1] } );
    } ) );
}

command_result_t TextureShift( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    f64 values[2]{ ws != nullptr ? ws->gridSize : 16.0, 0.0 }; usize n = 0;
    if ( !Numbers( args, values, 2, n ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const auto surfaces = Surfaces( ws );
    if ( surfaces.empty() ) { return command_result_t::DISABLED; }
    const f64 repeat = MeshRepeat( ws );
    return Result( ApplyKeeping( ws, "Shift Material", [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_Shift( copy, { surfaces.data(), surfaces.size() }, { values[0], values[1] }, repeat );
    } ) );
}

// Box, cylindrical and planar mapping of mesh faces about the selection's
// centre (repeat size: the scale typed in Set Scale's convention).
bool MeshSurfaces( const map_workspace_t *ws ) noexcept
{
    for ( const auto &surface : Surfaces( ws ) ) { if ( Mesh( ws, surface.object ) != nullptr ) { return true; } }
    return false;
}
u32 MeshSurfaceState( void *pContext ) noexcept { return State( MeshSurfaces( Ws( pContext ) ) ); }

template <map_uv_projection_t kProjection>
command_result_t ProjectTexture( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto surfaces = Surfaces( ws );
    if ( !MeshSurfaces( ws ) ) { return command_result_t::DISABLED; }
    map_bounds_t bounds{};
    for ( const auto &surface : surfaces ) { if ( const auto *object = MapWireframe_FindObject( ws->wire, surface.object ) ) { MapBounds_AddBounds( bounds, object->bounds ); } }
    const u32 axis = AxisSetting( ws, "editor.map.projection_axis" );
    const math::vec3d_t direction{ axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0 };
    const math::vec3d_t size{ bounds.box.maximum.x - bounds.box.minimum.x, bounds.box.maximum.y - bounds.box.minimum.y, bounds.box.maximum.z - bounds.box.minimum.z };
    const f64 radius = std::max( 1.0, 0.5 * std::max( axis == 0 ? size.y : size.x, axis == 2 ? size.y : size.z ) );
    const f64 repeat = MeshRepeat( ws );
    const char *label = kProjection == map_uv_projection_t::BOX ? "Box Mapping" : kProjection == map_uv_projection_t::CYLINDER ? "Cylindrical Mapping" : "Planar Mapping";
    return Result( ApplyKeeping( ws, label, [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_Project( copy, { surfaces.data(), surfaces.size() }, kProjection, MapBounds_Center( bounds ), direction, radius, { repeat, repeat } );
    } ) );
}

template <bool kClear>
command_result_t SmoothingGroups( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto surfaces = Surfaces( ws );
    if ( !MeshSurfaces( ws ) ) { return command_result_t::DISABLED; }
    const u32 group = static_cast<u32>( std::clamp( Real( ws, "editor.map.smoothing_group", 1.0 ), 1.0, 32.0 ) );
    const bool done = ApplyKeeping( ws, kClear ? "Clear Smoothing Groups" : "Set Smoothing Group", [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_SetSmoothingGroups( copy, { surfaces.data(), surfaces.size() }, kClear ? 0u : 1u << ( group - 1u ) );
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, kClear ? QStringLiteral( "Smoothing cleared: the faces shade flat" ) : QStringLiteral( "Smoothing group %1 set" ).arg( group ) ); }
    return Result( done );
}

command_result_t AutoHardEdges( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    std::vector<u64> meshes;
    for ( const auto &surface : Surfaces( ws ) ) {
        if ( Mesh( ws, surface.object ) != nullptr && std::find( meshes.begin(), meshes.end(), surface.object ) == meshes.end() ) { meshes.push_back( surface.object ); }
    }
    if ( meshes.empty() ) { return command_result_t::DISABLED; }
    const f64 degrees = Real( ws, "editor.map.sharp_edge_angle", 30.0 );
    usize hard = 0;
    const bool done = ApplyKeeping( ws, "Auto Hard Edges", [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_MarkHardEdgesByAngle( copy, { meshes.data(), meshes.size() }, degrees, &hard );
    } );
    MapNotice_Post( ws, map_notice_t::SUCCESS, done ? QStringLiteral( "%1 hard edges (sharper than %2°); the rest shade smooth" ).arg( hard ).arg( degrees )
                                                  : QStringLiteral( "Edges already match the %1° angle" ).arg( degrees ) );
    return command_result_t::OK;
}

// Set Scale: Hammer's texture scale (0.25 = 128 world units per repeat on a
// 512 texel material), absolute, one value or U and V.
command_result_t TextureSetScale( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    f64 values[2]{ 0.25, 0.25 }; usize n = 0;
    if ( !Numbers( args, values, 2, n ) || n == 0 || values[0] == 0.0 || ( n == 2 && values[1] == 0.0 ) ) { return command_result_t::INVALID_ARGUMENTS; }
    if ( n == 1 ) { values[1] = values[0]; }
    const auto surfaces = Surfaces( ws );
    if ( surfaces.empty() ) { return command_result_t::DISABLED; }
    const math::vec2d_t repeat{ std::fabs( values[0] ) * 512.0, std::fabs( values[1] ) * 512.0 };
    return Result( ApplyKeeping( ws, "Set Texture Scale", [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_SetRepeat( copy, { surfaces.data(), surfaces.size() }, repeat );
    } ) );
}

u32 UnwrapState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    for ( const auto &surface : Surfaces( ws ) ) { if ( Mesh( ws, surface.object ) != nullptr ) { return COMMAND_STATE_ENABLED; } }
    return COMMAND_STATE_NONE;
}
command_result_t Unwrap( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( UnwrapState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto surfaces = Surfaces( ws );
    return Result( ApplyKeeping( ws, "Unwrap UVs", [&]( map_document_t *copy ) noexcept {
        return MapTextureEdit_Unwrap( copy, { surfaces.data(), surfaces.size() } );
    } ) );
}

// Texture scale lock is the setting Resize and Scale already read.
u32 ScaleLockState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pGui == nullptr ) { return COMMAND_STATE_NONE; }
    return COMMAND_STATE_ENABLED | ( EditorSettings_Bool( &ws->pGui->settings, "editor.map.texture_scale_lock", CY_FALSE ) ? COMMAND_STATE_CHECKED : 0u );
}
command_result_t ScaleLockToggle( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pGui == nullptr ) { return command_result_t::DISABLED; }
    settings_registry_t *pSettings = &ws->pGui->settings;
    const auto *pDescriptor = EditorSettings_Find( pSettings, StringView_FromCString( "editor.map.texture_scale_lock" ) );
    if ( pDescriptor == nullptr ) { return command_result_t::FAILED; }
    setting_value_t value{};
    value.type = setting_type_t::BOOL;
    value.bValue = EditorSettings_Bool( pSettings, "editor.map.texture_scale_lock", CY_FALSE ) ? CY_FALSE : CY_TRUE;
    // The narrowest attached scope, as the workspace writes texture lock.
    for ( usize i = 0u; i < static_cast<usize>( settings_scope_t::DEFAULT ); ++i ) {
        if ( pSettings->scopes[i] != nullptr ) {
            return Result( EditorSettings_Write( pSettings, static_cast<settings_scope_t>( i ), *pDescriptor, value ) == settings_registry_status_t::OK );
        }
    }
    return command_result_t::FAILED;
}
/*
================
Mesh and brush conversion, booleans, material replacement
================
*/

bool ClosedMeshes( const map_workspace_t *ws, usize minimum, usize maximum ) noexcept
{
    if ( !Ready( ws ) || !MapWorkspace_CanEditMeshSelection( ws ) || ws->selection.ids.nCount < minimum || ws->selection.ids.nCount > maximum ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *mesh = Mesh( ws, ws->selection.ids.pData[i] );
        if ( mesh == nullptr || geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) != 0 ) { return false; }
    }
    return true;
}

u32 ToBrushState( void *pContext ) noexcept { return State( ClosedMeshes( Ws( pContext ), 1, CY_USIZE_MAX ) ); }
command_result_t ToBrush( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !ClosedMeshes( ws, 1, CY_USIZE_MAX ) ) { return command_result_t::DISABLED; }
    return Result( ApplyKeeping( ws, "Convert Meshes to Brushes", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_MeshesToBrushes( copy, Selected( ws ) );
    } ) );
}

// Brushes are closed and convex: they take part as they are.
bool ClosedSolids( const map_workspace_t *ws, usize minimum, usize maximum ) noexcept
{
    if ( !MeshesOrBrushes( ws ) || ws->selection.ids.nCount < minimum || ws->selection.ids.nCount > maximum ) { return false; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *mesh = Mesh( ws, ws->selection.ids.pData[i] );
        if ( mesh != nullptr && geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) != 0 ) { return false; }
    }
    return true;
}

template <map_boolean_t kOp>
u32 BooleanState( void *pContext ) noexcept
{
    return State( ClosedSolids( Ws( pContext ), 2, kOp == map_boolean_t::UNION ? CY_USIZE_MAX : 2u ) );
}
template <map_boolean_t kOp>
command_result_t Boolean( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( BooleanState<kOp>( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const char *label = kOp == map_boolean_t::UNION ? "Union" : kOp == map_boolean_t::SUBTRACT ? "Subtract" : kOp == map_boolean_t::INTERSECT ? "Intersect" :
                        kOp == map_boolean_t::EXCLUSIVE ? "Exclusive" : "Split by Mesh";
    const bool done = Apply( ws, label, [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        const auto converted = ConvertSelectedBrushes( copy, Selected( ws ) );
        return converted != map_status_t::OK ? converted : MapTopologyEdit_Boolean( copy, Selected( ws ), kOp, out );
    } );
    if ( !done ) {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "%1: the meshes do not overlap in a way that leaves a solid (or one is not closed)" ).arg( QString::fromLatin1( label ) ) );
    }
    return Result( done );
}

// Materials the open map uses, for the Replace dialog.
QStringList UsedMaterials( const map_workspace_t *ws )
{
    QStringList out;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto path = MapMaterials_Path( &ws->pDocument->materials, ws->wire.faces.pData[i].material );
        const QString text = QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) );
        if ( !text.isEmpty() && !out.contains( text ) ) { out.append( text ); }
    }
    out.sort();
    return out;
}

u32 ReplaceState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( Ready( ws ) && ws->wire.faces.nCount != 0 ); }

// Match the authored domains of MapTextureEdit_ReplaceMaterial, including
// hidden roots and patches. Wire visibility and current selection do not
// narrow this map-wide command. This admission query needs no allocation.
bool LockedMaterialUser( const map_workspace_t *ws, string_view_t from ) noexcept
{
    if ( !MapLayers_HasLocked( ws ) ) { return false; }
    const auto *map = ws->pDocument;
    const auto uses = [&]( u64 ref ) noexcept { return ref != 0 && StringView_Equals( MapMaterials_Path( &map->materials, ref ), from ); };
    for ( usize i = 0; i < map->geometry.brushes.nCount; ++i ) {
        const auto *brush = map->geometry.brushes.pData[i];
        if ( !MapLayers_IsObjectLocked( ws, brush->sourceId.value ) ) { continue; }
        const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &map->geometry, brush->sourceId );
        for ( usize k = 0; attributes != nullptr && k < attributes->records.nCount; ++k ) {
            if ( uses( attributes->records.pData[k].material.value ) ) { return true; }
        }
    }
    for ( usize i = 0; i < map->geometry.meshes.nCount; ++i ) {
        const auto *mesh = map->geometry.meshes.pData[i];
        if ( !MapLayers_IsObjectLocked( ws, mesh->sourceId.value ) ) { continue; }
        bool found = false;
        ( void )GenerationPool_ForEach( &mesh->mesh.faces, [&]( geo::geometry_mesh_face_handle_t handle, const geo::mesh_face_record_t & ) noexcept -> bool_t {
            found = uses( geo::MeshAttributeStore_GetFace( &mesh->attributes, handle ).material.value );
            return found ? CY_FALSE : CY_TRUE;
        } );
        if ( found ) { return true; }
    }
    for ( usize i = 0; i < geo::GeometryDocument_PatchCount( &map->geometry ); ++i ) {
        const auto *patch = geo::GeometryDocument_PatchAt( &map->geometry, i );
        if ( MapLayers_IsObjectLocked( ws, patch->sourceId.value ) && uses( patch->materialId ) ) { return true; }
    }
    return false;
}

// map.texture.replace <from> <to>; without arguments a dialog asks.
command_result_t ReplaceMaterials( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ReplaceState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto *document = ws->pDocument;
    const u64 geometryRevision = document->geometry.revision;
    QString from, to;
    if ( args.nArgs == 2 ) {
        from = QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) );
        to = QString::fromUtf8( args.pArgs[1].pData, static_cast<qsizetype>( args.pArgs[1].cchLength ) );
    } else if ( args.nArgs == 0 ) {
        QDialog dialog( QApplication::activeWindow() );
        dialog.setObjectName( QStringLiteral( "MapReplaceMaterialsDialog" ) );
        dialog.setWindowTitle( QStringLiteral( "Replace Materials" ) );
        auto *layout = new QFormLayout( &dialog );
        auto *find = new QComboBox( &dialog ); find->addItems( UsedMaterials( ws ) ); find->setEditable( true );
        const auto active = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
        auto *replace = new QLineEdit( QString::fromUtf8( active.pData, static_cast<qsizetype>( active.cchLength ) ), &dialog );
        layout->addRow( QStringLiteral( "Find" ), find );
        layout->addRow( QStringLiteral( "Replace with" ), replace );
        auto *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
        QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        layout->addRow( buttons );
        if ( dialog.exec() != QDialog::Accepted ) { return command_result_t::OK; }
        from = find->currentText().trimmed(); to = replace->text().trimmed();
    } else {
        return command_result_t::INVALID_ARGUMENTS;
    }
    if ( from.isEmpty() || to.isEmpty() ) { return command_result_t::INVALID_ARGUMENTS; }
    // The modal dialog runs an event loop: never apply its old request to a
    // replacement document, changed geometry, or a now-busy workspace.
    if ( !Ready( ws ) || ws->pDocument != document || ws->pDocument->geometry.revision != geometryRevision ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Replace Materials: the map changed while the dialog was open. Try again; no replacement was applied." ) );
        return command_result_t::FAILED;
    }
    const QByteArray fromUtf8 = from.toUtf8(), toUtf8 = to.toUtf8();
    const string_view_t fromPath{ fromUtf8.constData(), static_cast<usize>( fromUtf8.size() ) };
    const string_view_t toPath{ toUtf8.constData(), static_cast<usize>( toUtf8.size() ) };
    if ( !StringView_Equals( fromPath, toPath ) && LockedMaterialUser( ws, fromPath ) ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Replace Materials: a matching object is on a locked layer. Unlock it and retry; no materials were changed." ) );
        return command_result_t::FAILED;
    }
    const bool done = ApplyKeeping( ws, "Replace Materials", [&]( map_document_t *copy ) noexcept {
        usize replaced = 0;
        return MapTextureEdit_ReplaceMaterial( copy, fromPath, toPath, &replaced );
    } );
    if ( done ) { return command_result_t::OK; }
    if ( ws->lastEdit.outcome == map_edit_outcome_t::UNCHANGED && ws->lastEdit.status == map_status_t::OK ) {
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Replace Materials: no material changes were needed." ) );
        return command_result_t::OK;
    }
    MapNotice_Post( ws, map_notice_t::ERROR, ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ?
        QStringLiteral( "Replace Materials: not enough memory; no materials were changed." ) :
        QStringLiteral( "Replace Materials: the edit could not be applied; no materials were changed." ) );
    return command_result_t::FAILED;
}

/*
================
Groups and layers
================
*/

template <typename Edit>
bool ApplyMetadata( map_workspace_t *ws, const char *label, Edit edit ) noexcept
{
    return MapWorkspace_ApplyMetadataEdit( ws, label, []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        return ( *static_cast<Edit *>( context ) )( copy, out, changed );
    }, &edit );
}

// Selected roots, each replaced by its outermost group so groups nest.
std::vector<u64> GroupingMembers( map_workspace_t *ws )
{
    std::vector<u64> members;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const u64 id = ws->selection.ids.pData[i];
        const u64 group = MapDocument_GroupOf( ws->pDocument, id );
        const u64 member = group != 0 ? group : id;
        if ( std::find( members.begin(), members.end(), member ) == members.end() ) { members.push_back( member ); }
    }
    return members;
}

u32 GroupState( void *pContext ) noexcept
{
    auto *ws = Ws( pContext );
    return State( Ready( ws ) && ObjectMode( ws ) && GroupingMembers( ws ).size() >= 2 );
}
command_result_t CreateGroup( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( GroupState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto members = GroupingMembers( ws );
    return Result( ApplyMetadata( ws, "Group", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        u64 group = 0;
        const auto status = MapDocument_AddGroup( copy, StringView_FromCString( "group" ), { members.data(), members.size() }, &group );
        if ( status != map_status_t::OK ) { return status; }
        *changed = true;
        return Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } ) );
}

std::vector<u64> SelectedGroups( map_workspace_t *ws )
{
    std::vector<u64> groups;
    for ( usize i = 0; ws->pDocument != nullptr && i < ws->selection.ids.nCount; ++i ) {
        const u64 group = MapDocument_GroupOf( ws->pDocument, ws->selection.ids.pData[i] );
        if ( group != 0 && std::find( groups.begin(), groups.end(), group ) == groups.end() ) { groups.push_back( group ); }
    }
    return groups;
}
u32 UngroupState( void *pContext ) noexcept { auto *ws = Ws( pContext ); return State( Ready( ws ) && !SelectedGroups( ws ).empty() ); }
command_result_t Ungroup( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( UngroupState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto groups = SelectedGroups( ws );
    // The outermost groups go; groups nested in them become top-level again.
    return Result( ApplyMetadata( ws, "Ungroup", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        for ( u64 group : groups ) {
            const auto status = MapDocument_RemoveObject( copy, group );
            if ( status != map_status_t::OK ) { return status; }
        }
        *changed = true;
        return Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } ) );
}

// TrenchBroom's Add Objects to Group: the selection holds one group (whole
// or in part) and objects in no group; those join the group.
struct add_to_group_t { u64 group{ 0 }; std::vector<u64> loose; };
add_to_group_t AddToGroupPlan( map_workspace_t *ws )
{
    add_to_group_t plan;
    bool mixed = false;
    for ( usize i = 0; ws->pDocument != nullptr && i < ws->selection.ids.nCount; ++i ) {
        const u64 id = ws->selection.ids.pData[i];
        const u64 group = MapDocument_GroupOf( ws->pDocument, id );
        if ( group == 0 ) { plan.loose.push_back( id ); }
        else if ( plan.group == 0 ) { plan.group = group; }
        else if ( plan.group != group ) { mixed = true; }
    }
    if ( mixed ) { plan.group = 0; }
    return plan;
}
QString GroupLabel( const map_workspace_t *ws, u64 group )
{
    const auto name = MapGroups_Name( ws->pDocument, group );
    return name.empty() ? QStringLiteral( "Group #%1" ).arg( group ) : QStringLiteral( "\"%1\"" ).arg( QString::fromStdString( name ) );
}
u32 AddToGroupState( void *pContext ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !Ready( ws ) || !ObjectMode( ws ) ) { return COMMAND_STATE_NONE; }
    const auto plan = AddToGroupPlan( ws );
    return State( plan.group != 0 && !plan.loose.empty() );
}
command_result_t AddToGroup( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( AddToGroupState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto plan = AddToGroupPlan( ws );
    const bool done = ApplyMetadata( ws, "Add to Group", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        const auto status = MapGroups_Add( copy, plan.group, { plan.loose.data(), plan.loose.size() }, changed );
        return status != map_status_t::OK ? status : Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Added %1 to %2" ).arg( plan.loose.size() == 1 ? QStringLiteral( "1 object" ) : QStringLiteral( "%1 objects" ).arg( plan.loose.size() ), GroupLabel( ws, plan.group ) ) ); }
    return Result( done );
}

// Remove from Group: the selected objects leave the group they are
// directly in (Objects mode reaches inside groups); a group left empty
// dissolves.
std::vector<u64> GroupedSelection( map_workspace_t *ws )
{
    std::vector<u64> grouped;
    for ( usize i = 0; ws->pDocument != nullptr && i < ws->selection.ids.nCount; ++i ) {
        if ( MapGroups_Parent( ws->pDocument, ws->selection.ids.pData[i] ) != 0 ) { grouped.push_back( ws->selection.ids.pData[i] ); }
    }
    return grouped;
}
u32 RemoveFromGroupState( void *pContext ) noexcept
{
    auto *ws = Ws( pContext );
    return State( Ready( ws ) && ObjectMode( ws ) && ws->elementMode != map_element_mode_t::GROUPS && !GroupedSelection( ws ).empty() );
}
command_result_t RemoveFromGroup( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( RemoveFromGroupState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto grouped = GroupedSelection( ws );
    const bool done = ApplyMetadata( ws, "Remove from Group", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        const auto status = MapGroups_Remove( copy, { grouped.data(), grouped.size() }, changed );
        return status != map_status_t::OK ? status : Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "%1 left %2 group" ).arg( grouped.size() == 1 ? QStringLiteral( "1 object" ) : QStringLiteral( "%1 objects" ).arg( grouped.size() ), grouped.size() == 1 ? QStringLiteral( "its" ) : QStringLiteral( "their" ) ) ); }
    return Result( done );
}

// Select Group: everything in the outermost groups of the selection, from
// any mode (Objects mode picks inside a group; this widens to the group).
u32 SelectGroupState( void *pContext ) noexcept { auto *ws = Ws( pContext ); return State( Ready( ws ) && !SelectedGroups( ws ).empty() ); }
command_result_t SelectGroupMembers( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( SelectGroupState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    std::vector<u64> ids( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
    vector_t<u64> members{};
    if ( !Vector_Init( &members, ws->pGui->pAllocator ) ) { return command_result_t::FAILED; }
    for ( u64 group : SelectedGroups( ws ) ) {
        if ( MapDocument_GroupMembers( ws->pDocument, group, &members ) == map_status_t::OK ) { ids.insert( ids.end(), members.pData, members.pData + members.nCount ); }
    }
    Vector_Shutdown( &members );
    std::sort( ids.begin(), ids.end() );
    ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
    MapWorkspace_SetSelection( ws, ids.data(), ids.size() );
    return command_result_t::OK;
}

/*
================
Entities: Tie to Entity (Ctrl+T), Move to World (Ctrl+W), and the bound
entity and texture window commands
================
*/

bool IsGeometryId( const map_workspace_t *ws, u64 id ) noexcept
{
    const auto *object = MapWireframe_FindObject( ws->wire, id );
    return object != nullptr && ( object->kind == map_wire_kind_t::BRUSH || object->kind == map_wire_kind_t::MESH || object->kind == map_wire_kind_t::PATCH );
}
struct tie_plan_t { std::vector<u64> geometry; u64 entity{ 0 }; usize entities{ 0 }; };
tie_plan_t TiePlan( const map_workspace_t *ws )
{
    tie_plan_t plan;
    for ( usize i = 0; ws->pDocument != nullptr && i < ws->selection.ids.nCount; ++i ) {
        const u64 id = ws->selection.ids.pData[i];
        if ( IsGeometryId( ws, id ) ) { plan.geometry.push_back( id ); }
        else if ( MapWireframe_FindEntity( ws->wire, id ) != nullptr ) { plan.entity = id; ++plan.entities; }
    }
    if ( plan.entities != 1 ) { plan.entity = 0; } // Several entities: tie to a new one.
    return plan;
}
u32 TieState( void *pContext ) noexcept { auto *ws = Ws( pContext ); return State( Ready( ws ) && ObjectMode( ws ) && !TiePlan( ws ).geometry.empty() ); }
command_result_t TieToEntity( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( TieState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto plan = TiePlan( ws );
    const auto setting = EditorSettings_Text( &ws->pGui->settings, "editor.map.tie_class", StringView_FromCString( "func_detail" ) );
    const string_view_t className = args.nArgs >= 1 ? args.pArgs[0] : setting;
    u64 entity = 0;
    const bool done = Apply( ws, "Tie to Entity", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        auto status = MapCreate_TieToEntity( copy, { plan.geometry.data(), plan.geometry.size() }, plan.entity, className, &entity );
        if ( status == map_status_t::OK && plan.entity == 0 ) { status = MapGame_WriteDefaults( ws, copy, entity, className ); } // A new entity takes its class's defaults.
        return status != map_status_t::OK ? status : Vector_PushBack( out, entity ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) {
        MapNotice_Post( ws, map_notice_t::SUCCESS, plan.entity != 0 ? QStringLiteral( "Tied %1 to the selected entity" ).arg( plan.geometry.size() == 1 ? QStringLiteral( "1 object" ) : QStringLiteral( "%1 objects" ).arg( plan.geometry.size() ) )
            : QStringLiteral( "Tied %1 to a new %2 (Object Properties sets its class)" ).arg( plan.geometry.size() == 1 ? QStringLiteral( "1 object" ) : QStringLiteral( "%1 objects" ).arg( plan.geometry.size() ),
                                                                                        QString::fromUtf8( className.pData, static_cast<qsizetype>( className.cchLength ) ) ) );
    }
    return Result( done );
}

u32 MoveToWorldState( void *pContext ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !Ready( ws ) || !ObjectMode( ws ) ) { return COMMAND_STATE_NONE; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const u64 id = ws->selection.ids.pData[i];
        const auto *object = MapWireframe_FindObject( ws->wire, id );
        if ( object != nullptr && object->owner != 0 ) { return COMMAND_STATE_ENABLED; }
        if ( const auto *entity = MapWireframe_FindEntity( ws->wire, id ); entity != nullptr && entity->nOwned != 0 ) { return COMMAND_STATE_ENABLED; }
    }
    return COMMAND_STATE_NONE;
}
command_result_t MoveToWorld( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( MoveToWorldState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const std::vector<u64> ids( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
    usize moved = 0;
    const bool done = Apply( ws, "Move to World", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        // The pieces stay selected, now as world geometry.
        std::vector<u64> pieces;
        for ( u64 id : ids ) {
            if ( MapWireframe_FindEntity( ws->wire, id ) != nullptr ) {
                for ( usize k = 0; k < copy->geometryRecords.nCount; ++k ) { if ( copy->geometryRecords.pData[k].owner == id ) { pieces.push_back( copy->geometryRecords.pData[k].id ); } }
            } else { pieces.push_back( id ); }
        }
        const auto status = MapCreate_MoveToWorld( copy, { ids.data(), ids.size() }, &moved );
        if ( status != map_status_t::OK ) { return status; }
        for ( u64 id : pieces ) { if ( !Vector_PushBack( out, id ) ) { return map_status_t::OUT_OF_MEMORY; } }
        return map_status_t::OK;
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Moved %1 to the world" ).arg( moved == 1 ? QStringLiteral( "1 object" ) : QStringLiteral( "%1 objects" ).arg( moved ) ) ); }
    return Result( done );
}

// Keymap names Mason binds that are other windows' commands.
template <const char *kTarget>
command_result_t Forward( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pGui == nullptr ) { return command_result_t::DISABLED; }
    return EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( kTarget ), args );
}
template <const char *kTarget>
u32 ForwardState( void *pContext ) noexcept
{
    // An alias is offered exactly when what it runs is.
    const auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pGui == nullptr || EditorCommands_Find( &ws->pGui->commands, StringView_FromCString( kTarget ) ) == nullptr ) { return COMMAND_STATE_NONE; }
    return EditorCommands_State( &ws->pGui->commands, StringView_FromCString( kTarget ) ) & COMMAND_STATE_ENABLED;
}
constexpr char kPropertiesOpen[] = "view.properties.open";
constexpr char kBrowseMaterials[] = "assets.browse_materials";

// Writes a setting to the narrowest scope attached (the user's, in Mason).
bool WriteSetting( map_workspace_t *ws, const char *path, setting_value_t value ) noexcept
{
    settings_registry_t *settings = &ws->pGui->settings;
    const setting_descriptor_t *descriptor = EditorSettings_Find( settings, StringView_FromCString( path ) );
    if ( descriptor == nullptr ) { return false; }
    value.type = descriptor->type;
    for ( usize i = 0; i < static_cast<usize>( settings_scope_t::DEFAULT ); ++i ) {
        if ( settings->scopes[i] != nullptr ) { return EditorSettings_Write( settings, static_cast<settings_scope_t>( i ), *descriptor, value ) == settings_registry_status_t::OK; }
    }
    return false;
}

constexpr char kJustifyRight[] = "map.texture.justify_right";
constexpr char kJustifyTop[] = "map.texture.justify_top";
constexpr char kJustifyBottom[] = "map.texture.justify_bottom";
constexpr char kJustifyCenter[] = "map.texture.justify_center";
constexpr char kMeshMerge[] = "map.mesh.merge";
constexpr char kSplitEdges[] = "map.mesh.split_edges";
constexpr char kSnapToGrid[] = "map.brush.snap_to_grid";

// Clip: keep the other side of the staged cut (Back <-> Front).
u32 ClipFlipState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( Ready( ws ) && ws->editPreview.bActive && ws->editPreview.bClip && ws->editPreview.clipMode != map_brush_clip_mode_t::BOTH );
}
command_result_t ClipFlip( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ClipFlipState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    setting_value_t value{}; value.text = StringView_FromCString( ws->editPreview.clipMode == map_brush_clip_mode_t::BACK ? "front" : "back" );
    return Result( WriteSetting( ws, "editor.map.clip_mode", value ) );
}

// Block tool: the next shape in the list.
command_result_t NextShape( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !Ready( ws ) ) { return command_result_t::DISABLED; }
    static constexpr const char *kShapes[]{ "box", "wedge", "cylinder", "spike", "sphere", "quad", "arch", "stairs", "spiral_stairs", "pipe", "torus" };
    const auto current = EditorSettings_Text( &ws->pGui->settings, "editor.map.new_brush_shape", StringView_FromCString( "box" ) );
    usize next = 0;
    for ( usize i = 0; i < std::size( kShapes ); ++i ) { if ( StringView_Equals( current, StringView_FromCString( kShapes[i] ) ) ) { next = ( i + 1 ) % std::size( kShapes ); } }
    setting_value_t value{}; value.text = StringView_FromCString( kShapes[next] );
    const bool done = WriteSetting( ws, "editor.map.new_brush_shape", value );
    if ( done ) { MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Block shape: %1" ).arg( QString::fromLatin1( kShapes[next] ) ) ); }
    return Result( done );
}

// Entity tool: the next class, from the game's point classes (or, without a
// game profile, common ones), then those the map uses.
command_result_t NextClass( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !Ready( ws ) ) { return command_result_t::DISABLED; }
    std::vector<std::string> classes;
    for ( const auto *cls : MapGame_Placeable( ws, map_game_kind_t::POINT ) ) { classes.push_back( cls->name ); }
    if ( classes.empty() ) {
        classes = { "info_player_start", "light", "light_spot", "light_environment", "prop_static", "prop_dynamic", "prop_physics",
                    "info_target", "logic_relay", "logic_auto", "ambient_generic", "env_sprite" };
    }
    for ( usize i = 0; i < ws->wire.entities.nCount; ++i ) {
        const std::string name = ws->wire.entities.pData[i].className;
        if ( !name.empty() && std::find( classes.begin(), classes.end(), name ) == classes.end() ) { classes.push_back( name ); }
    }
    const auto current = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_point_class", StringView_FromCString( "info_player_start" ) );
    const std::string now( current.pData, current.cchLength );
    const auto it = std::find( classes.begin(), classes.end(), now );
    const std::string &next = it == classes.end() || it + 1 == classes.end() ? classes.front() : *( it + 1 );
    setting_value_t value{}; value.text = { next.data(), next.size() };
    const bool done = WriteSetting( ws, "editor.map.default_point_class", value );
    if ( done ) { MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Entity class: %1" ).arg( QString::fromStdString( next ) ) ); }
    return Result( done );
}

command_result_t Show3dGrid( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pGui == nullptr ) { return command_result_t::DISABLED; }
    setting_value_t value{}; value.bValue = EditorSettings_Bool( &ws->pGui->settings, "editor.grid.show_3d", CY_TRUE ) ? CY_FALSE : CY_TRUE;
    return Result( WriteSetting( ws, "editor.grid.show_3d", value ) );
}
u32 ReadyState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( Ready( ws ) ); }
u32 AlwaysState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( ws != nullptr && ws->pGui != nullptr ); }

// map.terrain.raise|lower|smooth|flatten: the Terrain tool's brush (and the tool).
template <const char *kMode>
command_result_t TerrainMode( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pGui == nullptr ) { return command_result_t::DISABLED; }
    setting_value_t value{}; value.text = StringView_FromCString( kMode );
    if ( !WriteSetting( ws, "editor.map.terrain_mode", value ) ) { return command_result_t::FAILED; }
    MapWorkspace_SetTool( ws, map_tool_t::TERRAIN );
    return command_result_t::OK;
}
template <const char *kMode>
u32 TerrainModeState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pGui == nullptr ) { return COMMAND_STATE_NONE; }
    const bool on = ws->tool == map_tool_t::TERRAIN &&
        StringView_Equals( EditorSettings_Text( &ws->pGui->settings, "editor.map.terrain_mode", StringView_FromCString( "raise" ) ), StringView_FromCString( kMode ) );
    return COMMAND_STATE_ENABLED | ( on ? COMMAND_STATE_CHECKED : 0u );
}
constexpr char kTerrainRaise[] = "raise", kTerrainLower[] = "lower", kTerrainSmooth[] = "smooth", kTerrainFlatten[] = "flatten";

// map.game.reload: read game/<game>.cygame again (after editing it).
command_result_t ReloadGame( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pDocument == nullptr ) { return command_result_t::DISABLED; }
    QString summary;
    const bool loaded = MapGame_Reload( ws, &summary ) == map_status_t::OK;
    const map_game_profile_t *profile = MapGame_Profile( ws );
    MapNotice_Post( ws, !loaded ? map_notice_t::WARNING : profile->problems.empty() ? map_notice_t::SUCCESS : map_notice_t::WARNING, summary );
    // Class sizes and colours may have changed: the views are rebuilt.
    MapWorkspace_DocumentChanged( ws );
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW | MAP_CHANGE_SELECTION ); // Panels show the classes again.
    return loaded ? command_result_t::OK : command_result_t::FAILED;
}

// Move Selection to Layer: the selected geometry into a layer (the one named,
// else chosen from a list).
u32 MoveToLayerState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( Ready( ws ) && ws->pDocument->layers.nCount > 1 && ws->selection.ids.nCount != 0 ); }
command_result_t MoveToLayer( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( MoveToLayerState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    std::string layer;
    if ( args.nArgs >= 1 ) { layer.assign( args.pArgs[0].pData, args.pArgs[0].cchLength ); }
    else {
        QStringList names;
        for ( usize i = 0; i < ws->pDocument->layers.nCount; ++i ) { names << QString::fromUtf8( ws->pDocument->layers.pData[i].id ); }
        bool ok = false;
        const QString chosen = QInputDialog::getItem( QApplication::activeWindow(), QStringLiteral( "Move to Layer" ), QStringLiteral( "Layer" ), names, 0, false, &ok );
        if ( !ok ) { return command_result_t::OK; } // Cancelled: nothing to report.
        layer = chosen.toStdString();
    }
    if ( !MapDocument_HasLayer( ws->pDocument, { layer.data(), layer.size() } ) ) { return command_result_t::INVALID_ARGUMENTS; }
    std::vector<u64> geometry;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) { if ( IsGeometryId( ws, ws->selection.ids.pData[i] ) ) { geometry.push_back( ws->selection.ids.pData[i] ); } }
    const bool done = ApplyMetadata( ws, "Move to Layer", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        for ( u64 id : geometry ) {
            const auto status = MapDocument_SetGeometryLayer( copy, id, { layer.data(), layer.size() } );
            if ( status != map_status_t::OK ) { return status; }
        }
        *changed = !geometry.empty();
        return Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Moved %1 to layer %2" ).arg( geometry.size() == 1 ? QStringLiteral( "1 object" ) : QStringLiteral( "%1 objects" ).arg( geometry.size() ), QString::fromStdString( layer ) ) ); }
    return Result( done );
}

// Geometry roots in the selection (entities keep their records for now).
std::vector<u64> SelectedGeometry( const map_workspace_t *ws )
{
    std::vector<u64> out;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object != nullptr && object->kind != map_wire_kind_t::ENTITY ) { out.push_back( object->id ); }
    }
    return out;
}
u32 LayerState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( Ready( ws ) && ws->pDocument->layers.nCount < MAP_LAYERS_MAX ); }
// New Layer: adds a layer; the selected geometry moves into it.
command_result_t CreateLayer( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( LayerState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    std::string id;
    for ( usize n = ws->pDocument->layers.nCount + 1; n < 100000; ++n ) {
        id = "layer_" + std::to_string( n );
        if ( !MapDocument_HasLayer( ws->pDocument, { id.data(), id.size() } ) ) { break; }
    }
    const std::string name = "Layer " + id.substr( 6 );
    const auto geometry = SelectedGeometry( ws );
    return Result( ApplyMetadata( ws, "New Layer", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        auto status = MapDocument_AddLayer( copy, { id.data(), id.size() }, { name.data(), name.size() } );
        for ( usize i = 0; status == map_status_t::OK && i < geometry.size(); ++i ) { status = MapDocument_SetGeometryLayer( copy, geometry[i], { id.data(), id.size() } ); }
        if ( status != map_status_t::OK ) { return status; }
        *changed = true;
        return Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } ) );
}

u32 VisgroupState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( Ready( ws ) && ws->selection.ids.nCount != 0 ); }
// A named visgroup holding the selection; the Auto Vis Groups panel lists it
// under Visgroups to show and hide.
command_result_t CreateVisgroup( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( VisgroupState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto *existing = KeyValue_Find( SettingsDocument_Root( &ws->pDocument->root ), StringView_FromCString( "visgroups" ) );
    std::string name = "Visgroup " + std::to_string( KeyValue_ChildCount( existing ) + 1 );
    if ( args.nArgs == 1 && args.pArgs[0].cchLength != 0 ) { name.assign( args.pArgs[0].pData, args.pArgs[0].cchLength ); }
    std::vector<u64> members( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
    return Result( ApplyMetadata( ws, "New Visgroup", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        u64 visgroup = 0;
        auto status = MapDocument_AddVisgroup( copy, { name.data(), name.size() }, &visgroup );
        for ( usize i = 0; status == map_status_t::OK && i < members.size(); ++i ) { status = MapDocument_AddToVisgroup( copy, visgroup, members[i] ); }
        if ( status != map_status_t::OK ) { return status; }
        *changed = true;
        return Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } ) );
}

/*
================
Selection sets (Hammer 5): named object lists kept with the map, made from
the selection; the Selection Sets panel drives these with the set's ID.
================
*/

u64 SetArgument( const command_args_t &args ) noexcept
{
    if ( args.nArgs == 0 || args.pArgs[0].cchLength == 0 || args.pArgs[0].cchLength > 20 ) { return 0; }
    char text[24]{};
    std::memcpy( text, args.pArgs[0].pData, args.pArgs[0].cchLength );
    char *end = nullptr;
    const unsigned long long value = std::strtoull( text, &end, 10 );
    return end != nullptr && *end == '\0' ? static_cast<u64>( value ) : 0u;
}
bool HasSet( const map_workspace_t *ws, u64 set )
{
    for ( const auto &existing : MapSelectionSets_All( ws->pDocument ) ) { if ( existing.id == set ) { return true; } }
    return false;
}
std::vector<u64> SelectionIds( const map_workspace_t *ws )
{
    return std::vector<u64>( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
}
QString SetLabel( const map_workspace_t *ws, u64 set )
{
    for ( const auto &existing : MapSelectionSets_All( ws->pDocument ) ) {
        if ( existing.id == set ) { return QStringLiteral( "\"%1\"" ).arg( QString::fromStdString( existing.name ) ); }
    }
    return QStringLiteral( "set #%1" ).arg( set );
}

u32 SelectionSetCreateState( void *pContext ) noexcept { auto *ws = Ws( pContext ); return State( Ready( ws ) && ws->selection.ids.nCount != 0 ); }
u32 SelectionSetEditState( void *pContext ) noexcept
{
    auto *ws = Ws( pContext );
    return State( Ready( ws ) && ws->selection.ids.nCount != 0 && !MapSelectionSets_All( ws->pDocument ).empty() );
}
u32 SelectionSetState( void *pContext ) noexcept { auto *ws = Ws( pContext ); return State( Ready( ws ) && !MapSelectionSets_All( ws->pDocument ).empty() ); }

// map.selection_set.create [name]: a new set of the selection, named
// "Set N" unless a name is given (rename it in the panel).
command_result_t CreateSelectionSet( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( SelectionSetCreateState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    std::string name = "Set " + std::to_string( MapSelectionSets_All( ws->pDocument ).size() + 1 );
    if ( args.nArgs == 1 && args.pArgs[0].cchLength != 0 ) { name.assign( args.pArgs[0].pData, args.pArgs[0].cchLength ); }
    if ( name.size() > MAP_NAME_MAX_LENGTH ) { return command_result_t::INVALID_ARGUMENTS; }
    const auto members = SelectionIds( ws );
    const bool done = ApplyMetadata( ws, "New Selection Set", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        u64 set = 0;
        const auto status = MapSelectionSets_New( copy, { name.data(), name.size() }, { members.data(), members.size() }, &set );
        if ( status != map_status_t::OK ) { return status; }
        *changed = true;
        return Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) {
        MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Selection set \"%1\": %2" ).arg( QString::fromStdString( name ),
            members.size() == 1 ? QStringLiteral( "1 object" ) : QStringLiteral( "%1 objects" ).arg( members.size() ) ) );
    }
    return Result( done );
}

// map.selection_set.add|remove <set>: the selection joins or leaves a set.
template <bool kAdd>
command_result_t EditSelectionSet( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( SelectionSetEditState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const u64 set = SetArgument( args );
    if ( set == 0 || !HasSet( ws, set ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const auto ids = SelectionIds( ws );
    const span_t<const u64> none{}, selected{ ids.data(), ids.size() };
    bool edited = false;
    map_status_t result = map_status_t::OK;
    const bool done = ApplyMetadata( ws, kAdd ? "Add to Selection Set" : "Remove from Selection Set", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        result = MapSelectionSets_Edit( copy, set, kAdd ? selected : none, kAdd ? none : selected, changed );
        edited = *changed;
        return result != map_status_t::OK ? result : Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    const QString label = SetLabel( ws, set );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, ( kAdd ? QStringLiteral( "Added the selection to %1" ) : QStringLiteral( "Removed the selection from %1" ) ).arg( label ) ); }
    // Nothing to change is not a failure the generic notice should report.
    else if ( result == map_status_t::OK && !edited ) {
        MapNotice_Post( ws, map_notice_t::INFO, ( kAdd ? QStringLiteral( "%1 already holds the selection" ) : QStringLiteral( "%1 holds none of the selection" ) ).arg( label ) );
        return command_result_t::OK;
    }
    return Result( done );
}

// map.selection_set.rename <set> <name>
command_result_t RenameSelectionSet( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( SelectionSetState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const u64 set = SetArgument( args );
    if ( set == 0 || args.nArgs != 2 || args.pArgs[1].cchLength == 0 || args.pArgs[1].cchLength > MAP_NAME_MAX_LENGTH || !HasSet( ws, set ) ) {
        return command_result_t::INVALID_ARGUMENTS;
    }
    const string_view_t name = args.pArgs[1];
    return Result( ApplyMetadata( ws, "Rename Selection Set", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        const auto status = MapSelectionSets_Rename( copy, set, name, changed );
        return status != map_status_t::OK ? status : Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } ) );
}

// map.selection_set.delete <set>: the set goes; its objects stay.
command_result_t DeleteSelectionSet( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( SelectionSetState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const u64 set = SetArgument( args );
    if ( set == 0 || !HasSet( ws, set ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const QString label = SetLabel( ws, set );
    const bool done = ApplyMetadata( ws, "Delete Selection Set", [&]( map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        const auto status = MapSelectionSets_Delete( copy, set );
        if ( status != map_status_t::OK ) { return status; }
        *changed = true;
        return Keep( *ws, out ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Deleted selection set %1; its objects stay" ).arg( label ) ); }
    return Result( done );
}

// A set's members that still exist (deleted objects stay listed).
std::vector<u64> LiveSetMembers( const map_workspace_t *ws, u64 set )
{
    std::vector<u64> live;
    for ( const auto &existing : MapSelectionSets_All( ws->pDocument ) ) {
        if ( existing.id != set ) { continue; }
        for ( u64 id : existing.members ) {
            if ( MapWireframe_FindObject( ws->wire, id ) != nullptr || MapWireframe_FindEntity( ws->wire, id ) != nullptr ) { live.push_back( id ); }
        }
    }
    return live;
}

// map.selection_set.select|hide|show <set>: view-only, no undo step.
enum class set_view_t : u8 { SELECT, HIDE, SHOW };
template <set_view_t kView>
command_result_t ViewSelectionSet( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pDocument == nullptr || MapSelectionSets_All( ws->pDocument ).empty() ) { return command_result_t::DISABLED; }
    const u64 set = SetArgument( args );
    if ( set == 0 || !HasSet( ws, set ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const auto members = LiveSetMembers( ws, set );
    if constexpr ( kView == set_view_t::SELECT ) {
        MapWorkspace_SetSelection( ws, members.data(), members.size() );
    } else {
        std::vector<u64> kept;
        for ( u64 id : members ) {
            ( void )EditorSelection_Apply( &ws->hidden, id, kView == set_view_t::HIDE ? EDITOR_SELECT_ADD : EDITOR_SELECT_REMOVE );
        }
        for ( usize i = 0; kView == set_view_t::HIDE && i < ws->selection.ids.nCount; ++i ) {
            const u64 id = ws->selection.ids.pData[i];
            if ( std::find( members.begin(), members.end(), id ) == members.end() ) { kept.push_back( id ); }
        }
        if ( kView == set_view_t::HIDE && kept.size() != ws->selection.ids.nCount ) { MapWorkspace_SetSelection( ws, kept.data(), kept.size() ); }
        MapWorkspace_Notify( ws, MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW );
    }
    return command_result_t::OK;
}

/*
================
Prefabs
================
*/

QString PathArgument( const command_args_t &args )
{
    return args.nArgs == 1 ? QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) ) : QString();
}

u32 PrefabCreateState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( ws != nullptr && ws->pDocument != nullptr && ws->selection.ids.nCount != 0 ); }
// map.prefab.create [file]: writes the selection as a .cyprefab (the map keeps it).
command_result_t CreatePrefab( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( PrefabCreateState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    if ( args.nArgs > 1 ) { return command_result_t::INVALID_ARGUMENTS; }
    QString path = PathArgument( args );
    if ( path.isEmpty() ) {
        path = QFileDialog::getSaveFileName( QApplication::activeWindow(), QStringLiteral( "Create Prefab" ), QString(), QStringLiteral( "Prefabs (*.cyprefab)" ) );
        if ( path.isEmpty() ) { return command_result_t::OK; }
    }
    if ( !path.endsWith( QStringLiteral( ".cyprefab" ) ) ) { path += QStringLiteral( ".cyprefab" ); }
    const QByteArray name = QFileInfo( path ).completeBaseName().toUtf8();
    text_buffer_t text{};
    if ( !TextBuffer_Init( &text, ws->pGui->pAllocator, 0 ) ) { return command_result_t::FAILED; }
    const auto status = MapPrefab_Write( ws->pDocument, Selected( ws ), { name.constData(), static_cast<usize>( name.size() ) }, &text );
    bool written = false;
    if ( status == map_status_t::OK ) {
        QFile file( path );
        const auto view = TextBuffer_View( &text );
        written = file.open( QIODevice::WriteOnly | QIODevice::Truncate ) &&
            file.write( view.pData, static_cast<qint64>( view.cchLength ) ) == static_cast<qint64>( view.cchLength );
    }
    TextBuffer_Shutdown( &text );
    return Result( written );
}

u32 PrefabInsertState( void *pContext ) noexcept { return State( Ready( Ws( pContext ) ) ); }
// map.prefab.insert [file]: places a copy at the last 2D cursor position.
command_result_t InsertPrefab( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !Ready( ws ) ) { return command_result_t::DISABLED; }
    if ( args.nArgs > 1 ) { return command_result_t::INVALID_ARGUMENTS; }
    QString path = PathArgument( args );
    if ( path.isEmpty() ) {
        path = QFileDialog::getOpenFileName( QApplication::activeWindow(), QStringLiteral( "Insert Prefab" ), QString(), QStringLiteral( "Prefabs (*.cyprefab)" ) );
        if ( path.isEmpty() ) { return command_result_t::OK; }
    }
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) { return command_result_t::FAILED; }
    const QByteArray text = file.readAll();
    math::vec3d_t at = ws->cursor;
    for ( u32 axis = 0; axis < 3; ++axis ) {
        f64 &value = axis == 0 ? at.x : axis == 1 ? at.y : at.z;
        value = ( ws->cursorAxes & ( 1u << axis ) ) != 0 && ws->bSnapToGrid && ws->gridSize > 0.0 ? std::round( value / ws->gridSize ) * ws->gridSize :
                ( ws->cursorAxes & ( 1u << axis ) ) != 0 ? value : 0.0;
    }
    return Result( Apply( ws, "Insert Prefab", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        return MapPrefab_Insert( copy, { text.constData(), static_cast<usize>( text.size() ) }, at, out );
    } ) );
}

u32 PivotState( void *pContext ) noexcept { return State( MapToolGestures_Pivot( Ws( pContext ), nullptr ) ); }
command_result_t ClearPivot( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !MapToolGestures_Pivot( ws, nullptr ) ) { return command_result_t::DISABLED; }
    MapToolGestures_ClearPivot( ws );
    return command_result_t::OK;
}

// Slice: apply the plane staged with the Clip tool, or start the Clip tool
// so one can be drawn (brushes and meshes alike).
bool ClipStaged( const map_workspace_t *ws ) noexcept
{
    return ws->tool == map_tool_t::CLIP && ws->editPreview.bActive && ws->editPreview.bClip && ws->editPreview.status == map_status_t::OK;
}
u32 SliceState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    // In the Clip tool it applies the staged plane; elsewhere it starts the tool.
    return State( ws->tool == map_tool_t::CLIP ? ClipStaged( ws ) : Ready( ws ) && MapWorkspace_CanClipSelection( ws ) );
}
command_result_t Slice( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ClipStaged( ws ) ) { return Result( MapWorkspace_CommitClipPreview( ws ) ); }
    if ( !Ready( ws ) || !MapWorkspace_CanClipSelection( ws ) ) { return command_result_t::DISABLED; }
    MapWorkspace_SetTool( ws, map_tool_t::CLIP );
    return command_result_t::OK;
}

/*
================
Component selection queries
================
*/

// The selected components as the geometry library's selection, for the
// queries to grow from.
bool CopyComponents( const map_workspace_t *ws, const components_t &c, geo::mesh_selection_t &out ) noexcept
{
    if ( geo::MeshSelection_Init( &out, ws->pGui->pAllocator, { c.mesh } ) != geo::geometry_status_t::OK ) { return false; }
    bool ok = true;
    for ( const id_t &id : c.vertices ) { ok = ok && geo::MeshSelection_TryAddVertex( &out, id ) == geo::geometry_status_t::OK; }
    for ( const edge_t &edge : c.edges ) { ok = ok && geo::MeshSelection_TryAddEdge( &out, edge ) == geo::geometry_status_t::OK; }
    return ok;
}

// Publishes one mode's set of a query result (an empty set clears).
command_result_t PublishQuery( map_workspace_t *ws, u64 mesh, bool vertices, geo::mesh_selection_t &next ) noexcept
{
    const std::vector<id_t> ids( next.vertices.pData, next.vertices.pData + next.vertices.nCount );
    const std::vector<edge_t> edges( next.edges.pData, next.edges.pData + next.edges.nCount );
    geo::MeshSelection_Shutdown( &next );
    if ( vertices ? ids.empty() : edges.empty() ) {
        vertices ? MapWorkspace_ClearMeshVertices( ws ) : MapWorkspace_ClearMeshEdges( ws );
        return command_result_t::OK;
    }
    const bool done = vertices ? MapWorkspace_SetMeshComponents( ws, mesh, { ids.data(), ids.size() }, {} )
                               : MapWorkspace_SetMeshComponents( ws, mesh, {}, { edges.data(), edges.size() } );
    return Result( done );
}

u32 ComponentsState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    return State( SelectedVertices( ws, c ) || SelectedEdges( ws, c ) );
}

enum class component_query_t : u8 { CONNECTED, INVERT, RIBS };

template <component_query_t kQuery>
u32 ComponentQueryState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    if ( kQuery == component_query_t::RIBS ) { return State( SelectedEdges( ws, c ) ); }
    u64 mesh = 0; std::vector<u64> faces;
    return State( ComponentsState( pContext ) != COMMAND_STATE_NONE || SelectedFaces( ws, mesh, faces ) );
}

// Edges touching the selected edges' vertices that are not selected: the
// ribs across an edge loop (Hammer's Select Ribs).
bool CollectRibs( const map_workspace_t *ws, const components_t &c, geo::mesh_selection_t &next ) noexcept
{
    const auto *mesh = Mesh( ws, c.mesh );
    geo::mesh_source_description_t d{};
    if ( mesh == nullptr || geo::MeshSourceDescription_Init( &d, ws->pGui->pAllocator, { c.mesh } ) != geo::geometry_status_t::OK ) { return false; }
    bool ok = geo::MeshSource_TryDescribe( mesh, &d ) == geo::geometry_status_t::OK &&
              geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { c.mesh } ) == geo::geometry_status_t::OK;
    const auto ends = Endpoints( c.edges );
    const auto touches = [&]( id_t v ) { return std::binary_search( ends.begin(), ends.end(), v, []( id_t a, id_t b ) { return a.value < b.value; } ); };
    for ( usize f = 0; ok && f < d.faces.nCount; ++f ) {
        const auto &face = d.faces.pData[f];
        for ( u32 k = 0; ok && k < face.cCorners; ++k ) {
            const id_t a = d.vertices.pData[d.corners.pData[face.iFirstCorner + k].iVertex].sourceId;
            const id_t b = d.vertices.pData[d.corners.pData[face.iFirstCorner + ( k + 1 ) % face.cCorners].iVertex].sourceId;
            const edge_t edge = geo::MeshEdgeRef_Make( a, b );
            const bool selected = std::any_of( c.edges.begin(), c.edges.end(), [&]( const edge_t &e ) { return e.a.value == edge.a.value && e.b.value == edge.b.value; } );
            if ( !selected && ( touches( a ) || touches( b ) ) ) { ok = geo::MeshSelection_TryAddEdge( &next, edge ) == geo::geometry_status_t::OK; }
        }
    }
    geo::MeshSourceDescription_Shutdown( &d );
    return ok;
}

template <component_query_t kQuery>
command_result_t ComponentQuery( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( kQuery != component_query_t::RIBS && ws->elementMode == map_element_mode_t::FACES ) {
        return RunFaceQuery( ws, true, []( geo::mesh_selection_t &next, const geo::mesh_source_t *mesh, u64 ) noexcept {
            return kQuery == component_query_t::CONNECTED ? geo::MeshSelection_TrySelectConnected( &next, mesh, geo::mesh_selection_mode_t::FACE )
                                                          : geo::MeshSelection_TryInvert( &next, mesh, geo::mesh_selection_mode_t::FACE );
        } );
    }
    const bool vertices = SelectedVertices( ws, c );
    if ( !vertices && !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    if ( kQuery == component_query_t::RIBS && vertices ) { return command_result_t::DISABLED; }
    const auto mode = vertices ? geo::mesh_selection_mode_t::VERTEX : geo::mesh_selection_mode_t::EDGE;
    geo::mesh_selection_t next{};
    bool ok = false;
    if ( kQuery == component_query_t::RIBS ) {
        ok = CollectRibs( ws, c, next );
    } else {
        ok = CopyComponents( ws, c, next ) &&
             ( kQuery == component_query_t::CONNECTED ? geo::MeshSelection_TrySelectConnected( &next, Mesh( ws, c.mesh ), mode )
                                                      : geo::MeshSelection_TryInvert( &next, Mesh( ws, c.mesh ), mode ) ) == geo::geometry_status_t::OK;
    }
    if ( !ok ) { geo::MeshSelection_Shutdown( &next ); return command_result_t::FAILED; }
    return PublishQuery( ws, c.mesh, vertices, next );
}

// The one selected mesh root in Edges mode, for whole-mesh edge queries.
u64 EdgeModeMesh( const map_workspace_t *ws ) noexcept
{
    if ( !ComponentReady( ws ) || ws->elementMode != map_element_mode_t::EDGES || ws->selection.ids.nCount != 1 ) { return 0; }
    const u64 id = ws->selection.ids.pData[0];
    const auto *object = MapWireframe_FindObject( ws->wire, id );
    return object != nullptr && object->kind == map_wire_kind_t::MESH && MapWorkspace_IsVisible( ws, *object ) && Mesh( ws, id ) != nullptr ? id : 0;
}

template <bool kSharp>
u32 EdgeQueryState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    const u64 mesh = EdgeModeMesh( ws );
    if ( mesh == 0 ) { return COMMAND_STATE_NONE; }
    return State( kSharp || geo::MeshBoundary_CountBoundaryEdges( &Mesh( ws, mesh )->mesh ) != 0 );
}
template <bool kSharp>
command_result_t EdgeQuery( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    const u64 mesh = EdgeModeMesh( ws );
    if ( mesh == 0 ) { return command_result_t::DISABLED; }
    f64 values[1]{}; usize count = 0;
    if ( !Numbers( args, values, 1, count ) || ( count == 1 && ( values[0] <= 0.0 || values[0] >= 180.0 ) ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const f64 degrees = count == 1 ? values[0] : Real( ws, "editor.map.sharp_edge_angle", 30.0 );
    geo::mesh_selection_t next{};
    if ( geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { mesh } ) != geo::geometry_status_t::OK ) { return command_result_t::FAILED; }
    const auto status = kSharp ? geo::MeshSelection_TrySelectSharpEdges( &next, Mesh( ws, mesh ), degrees * 3.14159265358979323846 / 180.0 )
                               : geo::MeshSelection_TrySelectBoundaryEdges( &next, Mesh( ws, mesh ) );
    if ( status != geo::geometry_status_t::OK ) { geo::MeshSelection_Shutdown( &next ); return command_result_t::FAILED; }
    return PublishQuery( ws, mesh, false, next );
}

u32 PathState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    return State( SelectedVertices( ws, c ) && c.vertices.size() == 2 );
}
command_result_t SelectPath( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( !SelectedVertices( ws, c ) || c.vertices.size() != 2 ) { return command_result_t::DISABLED; }
    geo::mesh_selection_t next{};
    bool found = false;
    const bool ok = geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { c.mesh } ) == geo::geometry_status_t::OK &&
        geo::MeshSelection_TrySelectVertexPath( &next, Mesh( ws, c.mesh ), c.vertices[0], c.vertices[1], geo::mesh_path_metric_t::LENGTH, &found ) == geo::geometry_status_t::OK;
    if ( !ok || !found ) { geo::MeshSelection_Shutdown( &next ); return command_result_t::FAILED; } // Not connected.
    Vector_Clear( &next.edges );
    return PublishQuery( ws, c.mesh, true, next );
}

// Vertices to edges (edges with both ends selected) and back, switching mode.
template <bool kToEdges>
u32 ConvertState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    return State( kToEdges ? SelectedVertices( ws, c ) && c.vertices.size() >= 2 : SelectedEdges( ws, c ) );
}
template <bool kToEdges>
command_result_t ConvertSelection( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( kToEdges ? !SelectedVertices( ws, c ) : !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    geo::mesh_selection_t next{};
    const auto from = kToEdges ? geo::mesh_selection_mode_t::VERTEX : geo::mesh_selection_mode_t::EDGE;
    const auto to = kToEdges ? geo::mesh_selection_mode_t::EDGE : geo::mesh_selection_mode_t::VERTEX;
    if ( !CopyComponents( ws, c, next ) || geo::MeshSelection_TryConvert( &next, Mesh( ws, c.mesh ), from, to ) != geo::geometry_status_t::OK ||
         ( kToEdges ? next.edges.nCount == 0 : next.vertices.nCount == 0 ) ) { geo::MeshSelection_Shutdown( &next ); return command_result_t::FAILED; }
    if ( kToEdges ) { Vector_Clear( &next.vertices ); } else { Vector_Clear( &next.edges ); }
    MapWorkspace_SetElementMode( ws, kToEdges ? map_element_mode_t::EDGES : map_element_mode_t::VERTICES );
    return PublishQuery( ws, c.mesh, !kToEdges, next );
}

/*
================
More component and mesh operations
================
*/

// Vertices, edges or the mesh faces the session has selected.
bool SelectedComponentSet( const map_workspace_t *ws, components_t &c, std::vector<u64> &faces ) noexcept
{
    faces.clear();
    if ( SelectedVertices( ws, c ) || SelectedEdges( ws, c ) ) { return true; }
    return SelectedFaces( ws, c.mesh, faces );
}

std::vector<id_t> FaceIds( const std::vector<u64> &faces )
{
    std::vector<id_t> out;
    for ( u64 face : faces ) { out.push_back( { face } ); }
    return out;
}

u32 OffsetState( void *pContext ) noexcept { components_t c; std::vector<u64> faces; return State( SelectedComponentSet( Ws( pContext ), c, faces ) ); }
command_result_t Offset( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext ); components_t c; std::vector<u64> faces;
    if ( !SelectedComponentSet( ws, c, faces ) ) { return command_result_t::DISABLED; }
    f64 values[1]{}; usize count = 0;
    if ( !Numbers( args, values, 1, count ) || ( count == 1 && values[0] == 0.0 ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const f64 distance = count == 1 ? values[0] : Real( ws, "editor.map.mesh_offset_distance", 8.0 );
    const auto faceIds = FaceIds( faces );
    const map_mesh_components_t set{ Span( c.vertices ), Span( c.edges ), { faceIds.data(), faceIds.size() } };
    const bool done = ApplyKeeping( ws, "Offset", [&]( map_document_t *copy ) noexcept { return MapTopologyEdit_OffsetComponents( copy, c.mesh, set, distance ); } );
    return Result( done );
}

template <map_flatten_t kPlane>
u32 FlattenState( void *pContext ) noexcept
{
    components_t c; std::vector<u64> faces;
    if ( !SelectedComponentSet( Ws( pContext ), c, faces ) ) { return COMMAND_STATE_NONE; }
    const usize n = !faces.empty() ? 3 : c.vertices.empty() ? Endpoints( c.edges ).size() : c.vertices.size();
    return State( n >= ( kPlane == map_flatten_t::BEST_FIT ? 3u : 2u ) );
}
template <map_flatten_t kPlane>
command_result_t Flatten( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c; std::vector<u64> faces;
    if ( FlattenState<kPlane>( pContext ) == COMMAND_STATE_NONE || !SelectedComponentSet( ws, c, faces ) ) { return command_result_t::DISABLED; }
    const auto faceIds = FaceIds( faces );
    const map_mesh_components_t set{ Span( c.vertices ), Span( c.edges ), { faceIds.data(), faceIds.size() } };
    const char *labels[]{ "Flatten", "Flatten to Normal", "Flatten X", "Flatten Y", "Flatten Z" };
    const bool done = ApplyKeeping( ws, labels[static_cast<u32>( kPlane )], [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_FlattenComponents( copy, c.mesh, set, kPlane );
    } );
    return Result( done );
}

// Hammer 5's Mesh Projection: the picked vertices (Vertices mode), else
// whole selected meshes, dropped along the projection direction onto the
// world, the projection offset short of it.
u32 ProjectState( void *pContext ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( SelectedVertices( ws, c ) ) { return COMMAND_STATE_ENABLED; }
    if ( !Ready( ws ) || !ObjectMode( ws ) ) { return COMMAND_STATE_NONE; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) { if ( geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { ws->selection.ids.pData[i] } ) != nullptr ) { return COMMAND_STATE_ENABLED; } }
    return COMMAND_STATE_NONE;
}
command_result_t ProjectMesh( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ProjectState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto text = EditorSettings_Text( &ws->pGui->settings, "editor.map.project_direction", StringView_FromCString( "-Z" ) );
    math::vec3d_t direction{ 0, 0, -1 };
    if ( text.cchLength == 2 ) {
        const f64 sign = text.pData[0] == '-' ? -1.0 : 1.0;
        direction = text.pData[1] == 'X' ? math::vec3d_t{ sign, 0, 0 } : text.pData[1] == 'Y' ? math::vec3d_t{ 0, sign, 0 } : math::vec3d_t{ 0, 0, sign };
    }
    const f64 offset = Real( ws, "editor.map.project_offset", 0.0 );
    components_t c;
    std::vector<std::pair<u64, std::vector<id_t>>> jobs;
    if ( SelectedVertices( ws, c ) ) { jobs.push_back( { c.mesh, c.vertices } ); }
    else {
        for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
            const u64 id = ws->selection.ids.pData[i];
            if ( geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { id } ) != nullptr ) { jobs.push_back( { id, {} } ); }
        }
    }
    usize moved = 0;
    const bool done = ApplyKeeping( ws, "Project onto World", [&]( map_document_t *copy ) noexcept {
        for ( const auto &[mesh, vertices] : jobs ) {
            usize count = 0;
            const auto status = MapTopologyEdit_Project( copy, mesh, Span( vertices ), direction, offset, &count );
            if ( status != map_status_t::OK ) { return status; }
            moved += count;
        }
        return map_status_t::OK;
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Projected %1 vertices onto the world (%2)" ).arg( moved ).arg( QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) ) ); }
    else if ( moved == 0 ) { MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Project: nothing lies %1 of the selection" ).arg( QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) ) ); }
    return Result( done );
}

u32 SlideState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); components_t c; return State( SelectedEdges( ws, c ) && c.edges.size() == 1 ); }
command_result_t SlideLoop( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( !SelectedEdges( ws, c ) || c.edges.size() != 1 ) { return command_result_t::DISABLED; }
    f64 values[1]{}; usize count = 0;
    if ( !Numbers( args, values, 1, count ) || ( count == 1 && ( values[0] == 0.0 || values[0] <= -1.0 || values[0] >= 1.0 ) ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const f64 factor = count == 1 ? values[0] : Real( ws, "editor.map.mesh_slide_factor", 0.25 );
    const bool done = ApplyKeeping( ws, "Slide Edge Loop", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_SlideEdgeLoop( copy, c.mesh, c.edges[0], factor );
    } );
    if ( !done && ws->lastEdit.outcome == map_edit_outcome_t::FAILED ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Slide Edge Loop slides a closed loop whose every corner joins four edges, between two strips of quads "
                                                                   "(a Loop Cut makes one). This edge's loop stops at a corner where three edges meet" ) );
    }
    return Result( done );
}

u32 SnapEdgeState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext ); components_t c;
    return State( SelectedEdges( ws, c ) && c.edges.size() == 2 && geo::MeshSelection_HasEdge( &ws->meshSelection, ws->selectedMeshEdgeSeed ) );
}
command_result_t SnapEdgeToEdge( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); components_t c;
    if ( SnapEdgeState( pContext ) == COMMAND_STATE_NONE || !SelectedEdges( ws, c ) ) { return command_result_t::DISABLED; }
    // The edge picked last is the target; the other one moves onto it.
    const edge_t target = ws->selectedMeshEdgeSeed;
    const edge_t source = c.edges[0].a.value == target.a.value && c.edges[0].b.value == target.b.value ? c.edges[1] : c.edges[0];
    geo::mesh_selection_t next{};
    if ( geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { c.mesh } ) != geo::geometry_status_t::OK ||
         geo::MeshSelection_TryAddEdge( &next, target ) != geo::geometry_status_t::OK ) {
        geo::MeshSelection_Shutdown( &next ); return command_result_t::FAILED;
    }
    const map_prepared_component_selection_t preparedSelection{ &next, target };
    const bool done = ApplyKeeping( ws, "Snap Edge to Edge", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_SnapEdgeToEdge( copy, c.mesh, source, target );
    }, &preparedSelection );
    geo::MeshSelection_Shutdown( &next );
    return Result( done );
}

u32 DeleteFaceState( void *pContext ) noexcept { u64 mesh = 0; std::vector<u64> faces; return State( SelectedFaces( Ws( pContext ), mesh, faces ) ); }
command_result_t DeleteFace( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); u64 mesh = 0; std::vector<u64> faces;
    if ( !SelectedFaces( ws, mesh, faces ) ) { return command_result_t::DISABLED; }
    return Result( ApplyKeeping( ws, faces.size() == 1 ? "Delete Face" : "Delete Faces", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_DeleteFaces( copy, mesh, { faces.data(), faces.size() } );
    } ) );
}

// Hammer 5's Faces operations: Extract and Thicken make a new mesh from the
// picked faces (selected afterwards), Combine joins adjacent picked faces,
// Remove Bad Faces collapses zero-area ones.
template <bool kThicken>
command_result_t ExtractFaces( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); u64 mesh = 0; std::vector<u64> faces;
    if ( !SelectedFaces( ws, mesh, faces ) ) { return command_result_t::DISABLED; }
    const f64 thickness = Real( ws, "editor.map.thicken_distance", 16.0 );
    u64 created = 0;
    const bool done = Apply( ws, kThicken ? "Thicken Faces" : "Extract Faces", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        const auto status = kThicken ? MapTopologyEdit_ThickenFaces( copy, mesh, { faces.data(), faces.size() }, thickness, &created )
                                     : MapTopologyEdit_ExtractFaces( copy, mesh, { faces.data(), faces.size() }, &created );
        return status == map_status_t::OK && !Vector_PushBack( out, created ) ? map_status_t::OUT_OF_MEMORY : status;
    } );
    if ( done ) {
        MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "%1 %2 %3 into a new mesh" ).arg( kThicken ? QStringLiteral( "Thickened" ) : QStringLiteral( "Extracted" ) )
            .arg( faces.size() ).arg( faces.size() == 1 ? QStringLiteral( "face" ) : QStringLiteral( "faces" ) ) );
    }
    return Result( done );
}

u32 CombineFacesState( void *pContext ) noexcept { u64 mesh = 0; std::vector<u64> faces; return State( SelectedFaces( Ws( pContext ), mesh, faces ) && faces.size() >= 2 ); }
command_result_t CombineFaces( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext ); u64 mesh = 0; std::vector<u64> faces;
    if ( !SelectedFaces( ws, mesh, faces ) || faces.size() < 2 ) { return command_result_t::DISABLED; }
    const bool done = ApplyKeeping( ws, "Combine Faces", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_CombineFaces( copy, mesh, { faces.data(), faces.size() } );
    } );
    if ( !done ) { MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Combine Faces needs picked faces that share edges" ) ); }
    return Result( done );
}

// Faces mode: the picked faces' mesh; otherwise the selected meshes.
std::vector<u64> CleanupMeshes( const map_workspace_t *ws )
{
    u64 mesh = 0; std::vector<u64> faces;
    if ( SelectedFaces( ws, mesh, faces ) ) { return { mesh }; }
    std::vector<u64> out;
    if ( !MapWorkspace_CanEditMeshSelection( ws ) ) { return out; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) { if ( Mesh( ws, ws->selection.ids.pData[i] ) != nullptr ) { out.push_back( ws->selection.ids.pData[i] ); } }
    return out;
}
u32 RemoveBadFacesState( void *pContext ) noexcept { return State( !CleanupMeshes( Ws( pContext ) ).empty() ); }
command_result_t RemoveBadFaces( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    const auto meshes = CleanupMeshes( ws );
    if ( meshes.empty() ) { return command_result_t::DISABLED; }
    usize removed = 0;
    const bool done = ApplyKeeping( ws, "Remove Bad Faces", [&]( map_document_t *copy ) noexcept {
        return MapTopologyEdit_RemoveBadFaces( copy, { meshes.data(), meshes.size() }, 1e-6, &removed );
    } );
    MapNotice_Post( ws, done ? map_notice_t::SUCCESS : map_notice_t::INFO, done ? QStringLiteral( "Removed %1 bad %2" ).arg( removed ).arg( removed == 1 ? QStringLiteral( "face" ) : QStringLiteral( "faces" ) )
                                                                              : QStringLiteral( "No bad faces found" ) );
    return done || removed == 0 ? command_result_t::OK : command_result_t::FAILED;
}

template <bool kMerge>
command_result_t Weld( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( !MapWorkspace_CanEditMeshSelection( ws ) ) { return command_result_t::DISABLED; }
    f64 values[1]{}; usize count = 0;
    if ( !Numbers( args, values, 1, count ) || ( count == 1 && values[0] <= 0.0 ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const f64 tolerance = count == 1 ? values[0] : Real( ws, "editor.map.mesh_weld_distance", 1.0 );
    usize merged = 0;
    const bool done = ApplyKeeping( ws, kMerge ? "Merge by Distance" : "Weld", [&]( map_document_t *copy ) noexcept {
        return kMerge ? MapTopologyEdit_MergeByDistance( copy, Selected( ws ), tolerance, &merged ) : MapTopologyEdit_Weld( copy, Selected( ws ), tolerance, &merged );
    } );
    if ( !done ) { Cy_LogWriteAt( log_level_t::Info, log_channel_t::Editor, "Nothing within the weld distance", CY_SOURCE_LOCATION ); }
    return Result( done );
}

u32 ExtrudeNewState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return State( Ready( ws ) && ws->elementMode == map_element_mode_t::FACES && MapWorkspace_HasBrushFace( ws ) );
}
command_result_t ExtrudeNew( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ExtrudeNewState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    f64 values[1]{}; usize count = 0;
    if ( !Numbers( args, values, 1, count ) || ( count == 1 && values[0] == 0.0 ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const f64 distance = count == 1 ? values[0] : ws->gridSize;
    const u64 brush = ws->selectedBrushFaceObject, side = ws->selectedBrushFaceSide;
    return Result( Apply( ws, "Extrude to New Brush", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        u64 added = 0;
        const auto status = MapTopologyEdit_ExtrudeBrushFaceToNew( copy, brush, side, distance, &added );
        return status == map_status_t::OK && !Vector_PushBack( out, added ) ? map_status_t::OUT_OF_MEMORY : status;
    } ) );
}

/*
================
Face queries (Faces mode, from the active face)
================
*/

enum class face_query_t : u8 { COPLANAR, FACING, SAME_CORNERS, SAME_MATERIAL, PATH };

template <face_query_t kQuery>
u32 FaceQueryState( void *pContext ) noexcept
{
    u64 mesh = 0; std::vector<u64> faces;
    if ( !SelectedFaces( Ws( pContext ), mesh, faces ) ) { return COMMAND_STATE_NONE; }
    return State( kQuery != face_query_t::PATH || faces.size() == 2 );
}
template <face_query_t kQuery>
command_result_t FaceQuery( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( FaceQueryState<kQuery>( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const auto *face = ActiveWireFace( ws );
    if ( face == nullptr ) { return command_result_t::FAILED; }
    f64 values[1]{}; usize count = 0;
    if ( !Numbers( args, values, 1, count ) || ( count == 1 && ( values[0] < 0.0 || values[0] >= 180.0 ) ) ) { return command_result_t::INVALID_ARGUMENTS; }
    constexpr f64 kRadians = 3.14159265358979323846 / 180.0;
    const f64 angle = ( count == 1 ? values[0] : kQuery == face_query_t::COPLANAR ? 1.0 : Real( ws, "editor.map.face_select_angle", 15.0 ) ) * kRadians;
    const auto normal = face->normal;
    const u32 corners = face->nIndices;
    const geo::geometry_material_ref_t material{ face->material };
    u64 mesh = 0; std::vector<u64> faces;
    ( void )SelectedFaces( ws, mesh, faces );
    const u64 other = faces.size() == 2 ? ( faces[0] == ws->selectedMeshFaceId ? faces[1] : faces[0] ) : 0;
    return RunFaceQuery( ws, false, [&]( geo::mesh_selection_t &next, const geo::mesh_source_t *source, u64 active ) noexcept {
        switch ( kQuery ) {
            case face_query_t::COPLANAR: return geo::MeshSelection_TrySelectCoplanar( &next, source, { active }, angle, 0.01 );
            case face_query_t::FACING: return geo::MeshSelection_TrySelectFacesFacing( &next, source, normal, angle );
            case face_query_t::SAME_CORNERS: return geo::MeshSelection_TrySelectFacesBySize( &next, source, corners, corners );
            case face_query_t::SAME_MATERIAL: return geo::MeshSelection_TrySelectFacesByMaterial( &next, source, material );
            case face_query_t::PATH: {
                bool found = false;
                const auto status = geo::MeshSelection_TrySelectFacePath( &next, source, { other }, { active }, geo::mesh_path_metric_t::LENGTH, &found );
                return status == geo::geometry_status_t::OK && !found ? geo::geometry_status_t::INVALID_ARGUMENT : status;
            }
        }
        return geo::geometry_status_t::INVALID_ARGUMENT;
    } );
}

/*
================
Patch matrix
================
*/

// Every selected object is a patch the operation applies to.
template <map_patch_op_t kOp>
u32 PatchState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    if ( !Ready( ws ) || !ObjectMode( ws ) || ws->selection.ids.nCount == 0 ) { return COMMAND_STATE_NONE; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        if ( !MapPatchEdit_Can( ws->pDocument, ws->selection.ids.pData[i], kOp ) ) { return COMMAND_STATE_NONE; }
    }
    return COMMAND_STATE_ENABLED;
}
template <map_patch_op_t kOp>
command_result_t PatchOp( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( PatchState<kOp>( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    const char *labels[]{ "Insert Patch Row", "Insert Patch Column", "Remove Patch Row", "Remove Patch Column", "Transpose Patch", "Invert Patch" };
    return Result( ApplyKeeping( ws, labels[static_cast<u32>( kOp )], [&]( map_document_t *copy ) noexcept {
        return MapPatchEdit_Apply( copy, Selected( ws ), kOp );
    } ) );
}

/*
================
Make Room and Thicken
================
*/

u32 MakeRoomState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    if ( !Ready( ws ) || !ObjectMode( ws ) || ws->selection.ids.nCount == 0 ) { return COMMAND_STATE_NONE; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object == nullptr || object->kind != map_wire_kind_t::BRUSH || !MapWorkspace_IsVisible( ws, *object ) ) { return COMMAND_STATE_NONE; }
    }
    return COMMAND_STATE_ENABLED;
}
command_result_t MakeRoom( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( MakeRoomState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    f64 values[1]{}; usize count = 0;
    if ( !Numbers( args, values, 1, count ) || ( count == 1 && values[0] <= 0.0 ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const f64 thickness = count == 1 ? values[0] : Real( ws, "editor.map.hollow_thickness", 16.0 );
    return Result( Apply( ws, "Make Room", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        return MapTopologyEdit_MakeRoom( copy, Selected( ws ), thickness, out );
    } ) );
}

u32 ThickenState( void *pContext ) noexcept { return PatchState<map_patch_op_t::INVERT>( pContext ); } // Any patch.
command_result_t Thicken( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ThickenState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    f64 values[1]{}; usize count = 0;
    if ( !Numbers( args, values, 1, count ) || ( count == 1 && values[0] == 0.0 ) ) { return command_result_t::INVALID_ARGUMENTS; }
    const f64 thickness = count == 1 ? values[0] : Real( ws, "editor.map.patch_thickness", 16.0 );
    const bool seams = EditorSettings_Bool( &ws->pGui->settings, "editor.map.patch_thicken_seams", CY_TRUE ) != CY_FALSE;
    return Result( Apply( ws, "Thicken Patch", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        return MapPatchEdit_Thicken( copy, Selected( ws ), thickness, seams, out );
    } ) );
}

// NetRadiant's Cap: fill the closed borders of the selected patches.
command_result_t CapPatch( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ThickenState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    usize caps = 0;
    const bool done = Apply( ws, "Cap Patch", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        return MapPatchEdit_Cap( copy, Selected( ws ), out, &caps );
    } );
    if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "%1 %2 added" ).arg( caps ).arg( caps == 1 ? QStringLiteral( "cap" ) : QStringLiteral( "caps" ) ) ); }
    else if ( caps == 0 ) { MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Cap Patch: no selected patch has a border that closes on itself (a cylinder's or cone's open end)" ) ); }
    return Result( done );
}

u32 EntityIoState( void *pContext ) noexcept { const auto *ws = Ws( pContext ); return State( ws != nullptr && ws->pGui != nullptr && ws->pDocument != nullptr ); }
command_result_t EntityIo( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( EntityIoState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    return Result( MapEntityIo_Show( ws, QApplication::activeWindow() ) != nullptr );
}
} // namespace

const char *MapGeometryCommands_NothingToDo( const char *pCommand ) noexcept
{
    struct phrase_t { const char *command, *why; };
    static constexpr phrase_t kPhrases[]{
        { "map.brush.snap_to_grid", "the selection is already on the grid" },
        { "map.mesh.weld", "no vertices are closer together than the weld distance (Tool Properties)" },
        { "map.mesh.merge_by_distance", "no vertices are closer together than the weld distance (Tool Properties)" },
        { "map.mesh.flatten", "the selection is already flat" },
        { "map.mesh.flatten_normal", "the selection is already flat across its normal" },
        { "map.mesh.flatten_x", "the selection already shares one X" },
        { "map.mesh.flatten_y", "the selection already shares one Y" },
        { "map.mesh.flatten_z", "the selection already shares one Z" },
        { "map.mesh.radial_align", "the vertices already lie on a circle" },
        { "map.mesh.smoothing_clear", "no face of the selection is in a smoothing group" },
        { "map.mesh.smoothing_set", "the faces are already in that smoothing group" },
        { "map.mesh.normals_default", "the edges already use the default normals" },
        { "map.mesh.normals_soft", "the edges are already soft" },
        { "map.mesh.normals_hard", "the edges are already hard" },
        { "map.mesh.seam_clear", "no selected edge is a UV seam" },
        { "map.mesh.seam_mark", "the edges are already UV seams" },
        { "map.mesh.subdivision_decrease", "the subdivision level is already 0" },
        { "map.mesh.subdivision_increase", "the subdivision level is already at its maximum" },
        { "map.mesh.project", "nothing lies below the selection to project onto" },
        { "map.mesh.repair", "nothing in the mesh needs repair" },
        { "map.mesh.triangulate", "every face is already a triangle" },
        { "map.texture.fit", "the material already fits" },
        { "map.texture.align_world", "the faces are already aligned to the world" },
        { "map.texture.align_face", "the faces are already aligned to themselves" },
        { "map.texture.wrap_coplanar", "no face lies in the same plane next to the picked one" },
        { "map.tool.apply_material", "the faces already use the active material" },
        { "map.select.inside", "no other object lies inside the selection" },
        { "map.select.tall", "no other object lies within the selection's footprint" },
        { "map.select.touching", "no other object touches the selection" },
        { "map.select.ring", "the edge has no ring of quads across it" },
        { "map.select.loop", "the edge has no loop through quads" },
        { "map.align.left", "the objects are already aligned" }, { "map.align.right", "the objects are already aligned" },
        { "map.align.top", "the objects are already aligned" }, { "map.align.bottom", "the objects are already aligned" },
    };
    if ( pCommand == nullptr ) { return nullptr; }
    for ( const auto &phrase : kPhrases ) { if ( std::strcmp( phrase.command, pCommand ) == 0 ) { return phrase.why; } }
    return nullptr;
}

const char *MapGeometryCommands_Requirement( const char *pCommand ) noexcept
{
    struct requirement_t { const char *command, *needs; };
    static constexpr requirement_t kRequirements[]{
        { "map.mesh.merge", "two or more vertices, two open edges, or two meshes (Meshes or Objects mode)" },
        { "map.mesh.collapse", "two or more vertices, some edges, or a mesh face" },
        { "map.mesh.dissolve", "selected interior edges, or vertices off the mesh's open border" },
        { "map.mesh.split_edges", "selected edges" },
        { "map.mesh.connect_edges", "two vertices on one face with no edge between them, or edges that share a face" },
        { "map.mesh.fill_hole", "selected open (border) edges, or two border vertices next to each other" },
        { "map.mesh.bridge", "two open edges, two chains of open edges, or two mesh faces" },
        { "map.mesh.bevel", "selected edges, or vertices off the mesh's open border" },
        { "map.mesh.extrude_edges", "selected open (border) edges" },
        { "map.mesh.extend_edges", "selected open (border) edges" },
        { "map.mesh.normals_hard", "selected edges" }, { "map.mesh.normals_soft", "selected edges" }, { "map.mesh.normals_default", "selected edges" },
        { "map.texture.weld_uvs", "selected vertices or edges" },
        { "map.mesh.radial_align", "three or more selected vertices" },
        { "map.mesh.split", "mesh faces, or a mesh made of separate pieces" },
        { "map.select.grow", "selected vertices, edges or faces" }, { "map.select.shrink", "selected vertices, edges or faces" },
        { "map.mesh.smooth", "meshes or brushes selected in Meshes or Objects mode" }, { "map.mesh.subdivide", "meshes or brushes selected in Meshes or Objects mode" },
        { "map.mesh.solidify", "an open mesh selected" },
        { "map.mesh.to_brush", "a closed convex mesh selected" },
        { "map.mesh.boolean_union", "two or more brushes or closed meshes" },
        { "map.mesh.boolean_subtract", "two brushes or closed meshes" }, { "map.mesh.boolean_intersect", "two brushes or closed meshes" },
        { "map.mesh.boolean_exclusive", "two brushes or closed meshes" }, { "map.mesh.boolean_split", "two brushes or closed meshes" },
        { "map.mesh.offset", "selected vertices, edges or mesh faces" },
        { "map.mesh.flatten", "three or more vertices (or a face)" }, { "map.mesh.flatten_normal", "two or more vertices" },
        { "map.mesh.flatten_x", "two or more vertices" }, { "map.mesh.flatten_y", "two or more vertices" }, { "map.mesh.flatten_z", "two or more vertices" },
        { "map.mesh.slide_loop", "one selected edge of a loop between quad strips" },
        { "map.mesh.snap_edge_to_edge", "two selected edges; the one picked last is the target" },
        { "map.mesh.delete_face", "selected mesh faces" },
        { "map.mesh.create_displacement", "one picked face with four corners (Faces mode)" },
        { "map.patch.from_face", "a picked brush face (Faces mode)" },
        { "map.group.add", "one group and objects in no group, selected together" },
        { "map.entity.tie", "selected brushes or meshes (Objects mode)" },
        { "map.entity.move_to_world", "geometry tied to an entity, or a brush entity, selected" },
        { "map.layer.move_selection", "a selection and a second layer" },
        { "map.group.remove", "objects inside a group, picked in Objects mode" },
        { "map.group.select", "a selected object that is in a group" },
        { "map.selection_set.create", "a selection to save" }, { "map.selection_set.add", "a selection and a selection set" },
        { "map.selection_set.remove", "a selection and a selection set" }, { "map.selection_set.rename", "a selection set" },
        { "map.selection_set.delete", "a selection set" }, { "map.selection_set.select", "a selection set" },
        { "map.selection_set.hide", "a selection set" }, { "map.selection_set.show", "a selection set" },
        { "map.texture.wrap_coplanar", "a picked brush face (Faces mode)" },
        { "map.texture.hotspot", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.mesh.seam_mark", "selected mesh edges" }, { "map.mesh.seam_clear", "selected mesh edges" }, { "map.select.uv_island", "a picked mesh face" },
        { "map.mesh.repair", "a mesh, or picked faces of one" }, { "map.brush.sweep", "a picked brush face (Faces mode)" },
        { "map.mesh.extract_faces", "selected mesh faces (Faces mode)" },
        { "map.mesh.thicken_faces", "selected mesh faces (Faces mode)" },
        { "map.mesh.combine_faces", "two or more adjacent mesh faces" },
        { "map.mesh.remove_bad_faces", "a mesh, or picked faces of one" },
        { "map.mesh.weld", "meshes selected" }, { "map.mesh.merge_by_distance", "meshes selected" },
        { "map.brush.extrude_new", "a brush face selected in Faces mode" },
        { "map.brush.make_room", "brushes selected" },
        { "map.patch.insert_row", "patches selected" }, { "map.patch.insert_column", "patches selected" },
        { "map.patch.remove_row", "patches with two or more rows of spans" }, { "map.patch.remove_column", "patches with two or more columns of spans" },
        { "map.patch.transpose", "patches selected" }, { "map.patch.invert", "patches selected" }, { "map.patch.thicken", "patches selected" }, { "map.patch.cap", "a patch with a closed border (cylinder, cone or lathe) selected" },
        { "map.select.path", "exactly two selected vertices" },
        { "map.select.to_edges", "two or more selected vertices" }, { "map.select.to_vertices", "selected edges" },
        { "map.select.ribs", "selected edges" },
        { "map.select.boundary_edges", "a mesh with open edges, in Edges mode" }, { "map.select.sharp_edges", "a mesh selected in Edges mode" },
        { "map.select.connected", "selected vertices, edges or faces" }, { "map.select.invert_components", "selected vertices, edges or faces" },
        { "map.select.coplanar", "a selected mesh face" }, { "map.select.faces_facing", "a selected mesh face" },
        { "map.select.same_corners", "a selected mesh face" }, { "map.select.faces_material", "a selected mesh face" },
        { "map.select.face_path", "exactly two selected mesh faces" },
        { "map.select.touching", "objects selected" }, { "map.select.inside", "objects selected" }, { "map.select.tall", "objects selected" },
        { "map.mesh.extrude", "one mesh face selected" }, { "map.mesh.inset", "one mesh face selected" }, { "map.mesh.inset_width", "a connected coplanar region with one convex perimeter" }, { "map.mesh.quad_slice", "one quad mesh face selected" },
        { "map.mesh.triangulate", "meshes selected" }, { "map.mesh.flip_normals", "meshes selected" },
        { "map.brush.hollow", "brushes selected" }, { "map.brush.carve", "brushes selected" }, { "map.brush.merge", "two brushes selected" },
        { "map.brush.to_mesh", "brushes selected" },
        { "map.select.loop", "a selected edge" }, { "map.select.ring", "a selected edge" },
        { "map.select.same_material", "selected objects or a picked face" }, { "map.select.same_class", "a selected entity" },
        { "map.brush.snap_to_grid", "selected objects (Objects mode) or selected vertices" },
        { "map.transform.flip_horizontal", "a selection that can move (Objects mode)" }, { "map.transform.flip_vertical", "a selection that can move (Objects mode)" },
        { "map.transform.rotate_cw", "a selection that can move (Objects mode)" }, { "map.transform.rotate_ccw", "a selection that can move (Objects mode)" },
        { "map.align.left", "two or more selected objects" }, { "map.align.right", "two or more selected objects" },
        { "map.align.top", "two or more selected objects" }, { "map.align.bottom", "two or more selected objects" },
        { "map.group.create", "two or more selected objects" }, { "map.group.ungroup", "a selected group" },
        { "map.prefab.create", "a selection to save as a prefab" }, { "map.visgroup.create", "a selection to put in the visgroup" },
        { "map.cordon.edit", "a selection to cordon" }, { "map.cordon.toggle", "a cordon (Cordon Selection makes one)" },
        { "map.run", "a compiled map" }, { "map.stop", "a running map" }, { "map.leak.show", "a leak found by Compile Map" },
        { "map.mesh.slice", "selected meshes or brushes (a Clip line in a 2D view sets the cut)" },
        { "map.tool.apply_material", "picked faces (Faces mode) or selected brushes, meshes or patches" },
        { "map.texture.fit", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.align_world", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.align_face", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.justify_left", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.justify_right", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.justify_top", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.justify_bottom", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.justify_center", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.rotate", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.scale", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.shift", "picked faces (Faces mode) or selected brushes or meshes" },
        { "map.texture.unwrap", "picked mesh faces or selected brushes or meshes" },
    };
    if ( pCommand == nullptr ) { return nullptr; }
    for ( const auto &requirement : kRequirements ) { if ( std::strcmp( requirement.command, pCommand ) == 0 ) { return requirement.needs; } }
    return nullptr;
}

namespace
{
/*
================
Brushes for mesh operations. Hammer 5's blocks are meshes, so a mesh
operation run on a brush retains its root and face IDs. Extrude, Corner
Inset and Quad Slice convert and operate in one private transaction. Other
legacy adapters below still publish conversion separately and undo it if
the following operation fails. editor.map.components_convert_brushes turns
these adapters off (TrenchBroom-style brush maps).
================
*/

// Operations on picked faces, and on whole selected objects.
constexpr const char *kBrushFaceOperations[]{
    "map.mesh.extrude", "map.mesh.inset", "map.mesh.offset", "map.mesh.flatten", "map.mesh.delete_face",
    "map.mesh.extract_faces", "map.mesh.split", "map.mesh.thicken_faces", "map.mesh.collapse", "map.mesh.quad_slice",
    "map.mesh.smoothing_set", "map.mesh.smoothing_clear", "map.mesh.auto_hard_edges", "map.texture.project_box",
    "map.texture.project_cylinder", "map.texture.project_planar", "map.texture.unwrap", "map.mesh.subdivision_increase",
    "map.mesh.subdivision_decrease", "map.mesh.subdivision_level_0", "map.mesh.subdivision_level_1", "map.mesh.subdivision_level_2",
    "map.mesh.subdivision_level_3", "map.mesh.subdivision_level_4", "map.mesh.subdivision_level_5", "map.mesh.modifier_mirror_x",
    "map.mesh.modifier_mirror_y", "map.mesh.modifier_mirror_z", "map.mesh.modifier_array", "map.mesh.modifier_radial",
    "map.mesh.modifier_bend", "map.mesh.modifier_taper" };
constexpr const char *kBrushObjectOperations[]{
    "map.texture.project_box", "map.texture.project_cylinder", "map.texture.project_planar", "map.mesh.smoothing_set",
    "map.mesh.smoothing_clear", "map.mesh.auto_hard_edges", "map.mesh.project", "map.mesh.flip_normals", "map.mesh.triangulate",
    "map.mesh.weld", "map.mesh.merge_by_distance", "map.mesh.merge", "map.texture.unwrap" };

enum class atomic_brush_face_t { NONE, EXTRUDE, INSET, QUAD_SLICE };
struct brush_adapter_t {
    map_workspace_t *ws{ nullptr };
    command_execute_fn pfnExecute{ nullptr };
    command_state_fn pfnState{ nullptr };
    void *pContext{ nullptr };
    const char *pLabel{ nullptr };
    bool bFaces{ false }, bObjects{ false };
    usize nMinObjects{ 1u }; // Joining needs two.
    atomic_brush_face_t atomicFace{ atomic_brush_face_t::NONE };
};
std::unordered_map<const map_workspace_t *, std::deque<brush_adapter_t>> &BrushAdapters()
{
    static std::unordered_map<const map_workspace_t *, std::deque<brush_adapter_t>> adapters;
    return adapters;
}

u32 OriginalState( const brush_adapter_t &a ) noexcept { return a.pfnState != nullptr ? a.pfnState( a.pContext ) : COMMAND_STATE_ENABLED; }

struct brush_face_operation_t {
    u64 root{}, side{};
    f64 amount{};
    u32 cellsU{}, cellsV{};
};
bool PrepareBrushFaceOperation( const brush_adapter_t &a, brush_face_operation_t &operation ) noexcept
{
    const auto *ws = a.ws;
    if ( a.atomicFace == atomic_brush_face_t::NONE || !Ready( ws ) || ws->elementMode != map_element_mode_t::FACES ||
         !EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) ||
         !MapWorkspace_HasBrushFace( ws ) || MapWorkspace_HasMeshFace( ws ) ) { return false; }
    // HasBrushFace verifies the sole root, live side, visibility and layer lock.
    operation.root = ws->selectedBrushFaceObject; operation.side = ws->selectedBrushFaceSide;
    if ( a.atomicFace != atomic_brush_face_t::QUAD_SLICE ) {
        operation.amount = a.atomicFace == atomic_brush_face_t::EXTRUDE ?
            Real( ws, "editor.map.mesh_extrude_distance", 16.0 ) : Real( ws, "editor.map.mesh_inset_distance", 8.0 );
        return std::isfinite( operation.amount ) && operation.amount > 0;
    }
    const auto u = EditorSettings_Integer( &ws->pGui->settings, "editor.map.mesh_slice_u", 2 );
    const auto v = EditorSettings_Integer( &ws->pGui->settings, "editor.map.mesh_slice_v", 2 );
    if ( u < 1 || u > 64 || v < 1 || v > 64 || ( u == 1 && v == 1 ) ) { return false; }
    operation.cellsU = static_cast<u32>( u ); operation.cellsV = static_cast<u32>( v );
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i];
        if ( face.id == operation.root && face.sideId == operation.side ) { return face.nIndices == 4; }
    }
    return false;
}

command_result_t AtomicBrushFaceExecute( const brush_adapter_t &a ) noexcept
{
    brush_face_operation_t operation{};
    if ( !PrepareBrushFaceOperation( a, operation ) ) { return command_result_t::DISABLED; }
    auto *ws = a.ws;
    const auto *document = ws->pDocument;
    const auto geometryRevision = document->geometry.revision, selectionRevision = ws->selection.revision;
    const auto tool = ws->tool;
    const auto current = [&]() noexcept {
        return Ready( ws ) && ws->pDocument == document && document->geometry.revision == geometryRevision &&
            ws->selection.revision == selectionRevision && ws->tool == tool && ws->elementMode == map_element_mode_t::FACES &&
            ws->selectedBrushFaceObject == operation.root && ws->selectedBrushFaceSide == operation.side &&
            EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) && MapWorkspace_HasBrushFace( ws );
    };
    geo::mesh_selection_t next{};
    const map_prepared_component_selection_t continuation{ &next, {}, -1, operation.side };
    const bool applied = Apply( ws, a.pLabel, [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        if ( !current() ) { return map_status_t::INVALID_ARGUMENT; }
        vector_t<u64> roots{};
        if ( !Vector_Init( &roots, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
        const auto converted = MapMeshEdit_ConvertBrushes( copy, { &operation.root, 1 }, &roots );
        Vector_Shutdown( &roots );
        if ( converted != map_status_t::OK ) { return converted; }
        const auto convertedRevision = copy->geometry.revision;
        const auto status = a.atomicFace == atomic_brush_face_t::EXTRUDE ?
            MapMeshEdit_ExtrudeFace( copy, operation.root, operation.side, operation.amount ) :
            a.atomicFace == atomic_brush_face_t::INSET ? MapMeshEdit_InsetFace( copy, operation.root, operation.side, operation.amount ) :
            MapMeshEdit_QuadSliceFace( copy, operation.root, operation.side, operation.cellsU, operation.cellsV );
        if ( status != map_status_t::OK ) { return status; }
        if ( !current() ) { return map_status_t::INVALID_ARGUMENT; }
        if ( copy->geometry.revision == convertedRevision ) {
            // Conversion alone is not the requested edit. Mark this private
            // candidate unchanged so Apply discards it before history/wire work.
            copy->geometry.revision = geometryRevision; return map_status_t::OK;
        }
        if ( geo::MeshSelection_Init( &next, ws->pGui->pAllocator, { operation.root } ) != geo::geometry_status_t::OK ||
             !Vector_PushBack( &next.faces, id_t{ operation.side } ) || !Vector_PushBack( out, operation.root ) ) {
            return map_status_t::OUT_OF_MEMORY;
        }
        return map_status_t::OK;
    }, &continuation );
    geo::MeshSelection_Shutdown( &next );
    if ( !applied && ws->lastEdit.outcome == map_edit_outcome_t::FAILED ) {
        const auto reason = ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ?
            QStringLiteral( "not enough memory to prepare the geometry and Undo state" ) :
            QStringLiteral( "the requested face operation could not produce valid geometry" );
        MapNotice_Post( ws, map_notice_t::ERROR,
            QStringLiteral( "%1: %2. The brush, selection and history are unchanged." ).arg( QString::fromUtf8( a.pLabel ), reason ) );
    }
    return Result( applied );
}

// The brushes the operation would work on once they are meshes.
std::vector<u64> AdapterBrushes( const brush_adapter_t &a )
{
    std::vector<u64> brushes;
    map_workspace_t *ws = a.ws;
    if ( !Ready( ws ) || !EditorSettings_Bool( &ws->pGui->settings, "editor.map.components_convert_brushes", CY_TRUE ) ) { return brushes; }
    const auto editable = [ws]( u64 id ) {
        const auto *object = MapWireframe_FindObject( ws->wire, id );
        return object != nullptr && object->kind == map_wire_kind_t::BRUSH && MapWorkspace_IsVisible( ws, *object );
    };
    if ( a.bFaces && ws->elementMode == map_element_mode_t::FACES ) {
        if ( MapWorkspace_HasBrushFace( ws ) && !MapWorkspace_HasMeshFace( ws ) && editable( ws->selectedBrushFaceObject ) ) { brushes.push_back( ws->selectedBrushFaceObject ); }
        return brushes;
    }
    // Only a selection of brushes and meshes: anything else in it (a patch,
    // an entity, a stale ID) refuses the operation as it is.
    if ( a.bObjects && ObjectMode( ws ) && MapWorkspace_CanEditSelection( ws ) ) {
        for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
            const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
            if ( object == nullptr || ( object->kind != map_wire_kind_t::BRUSH && object->kind != map_wire_kind_t::MESH ) ) { brushes.clear(); return brushes; }
            if ( editable( object->id ) ) { brushes.push_back( object->id ); }
        }
        if ( ws->selection.ids.nCount < a.nMinObjects ) { brushes.clear(); }
    }
    return brushes;
}

u32 AdaptedState( void *pContext ) noexcept
{
    const auto &a = *static_cast<const brush_adapter_t *>( pContext );
    const u32 state = OriginalState( a );
    if ( ( state & COMMAND_STATE_ENABLED ) != 0u ) { return state; }
    if ( a.atomicFace != atomic_brush_face_t::NONE ) {
        brush_face_operation_t operation{};
        return PrepareBrushFaceOperation( a, operation ) ? state | COMMAND_STATE_ENABLED : state;
    }
    try { return AdapterBrushes( a ).empty() ? state : ( state | COMMAND_STATE_ENABLED ); } catch ( ... ) { return state; }
}

command_result_t AdaptedExecute( void *pContext, const command_args_t &args ) noexcept
{
    const auto &a = *static_cast<const brush_adapter_t *>( pContext );
    if ( ( OriginalState( a ) & COMMAND_STATE_ENABLED ) != 0u ) { return a.pfnExecute( a.pContext, args ); }
    if ( a.atomicFace != atomic_brush_face_t::NONE ) { return AtomicBrushFaceExecute( a ); }
    map_workspace_t *ws = a.ws;
    std::vector<u64> brushes, kept;
    try {
        brushes = AdapterBrushes( a );
        kept.assign( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
    } catch ( ... ) { return command_result_t::FAILED; }
    if ( brushes.empty() ) { return a.pfnExecute( a.pContext, args ); } // It says why itself.
    const bool faces = ws->elementMode == map_element_mode_t::FACES;
    const u64 faceObject = ws->selectedBrushFaceObject, faceSide = ws->selectedBrushFaceSide;
    const bool converted = Apply( ws, "Convert Brush to Mesh", [&]( map_document_t *copy, vector_t<u64> *out ) noexcept {
        vector_t<u64> roots{};
        if ( !Vector_Init( &roots, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
        const auto status = MapMeshEdit_ConvertBrushes( copy, { brushes.data(), brushes.size() }, &roots );
        Vector_Shutdown( &roots );
        for ( u64 id : kept ) { if ( status == map_status_t::OK && !Vector_PushBack( out, id ) ) { return map_status_t::OUT_OF_MEMORY; } }
        return status;
    } );
    if ( !converted ) { return command_result_t::FAILED; }
    // Brush side IDs are the mesh's face IDs.
    if ( faces ) { MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES ); MapWorkspace_SelectMeshFace( ws, faceObject, faceSide ); }
    // Taking the conversion back leaves no Redo step behind: the user never
    // asked for it on its own.
    const auto undo = [&]() {
        if ( MapWorkspace_Undo( ws ) == editor_history_status_t::OK ) { EditorHistory_DiscardRedo( &ws->history ); }
        if ( faces ) { MapWorkspace_SelectBrushFace( ws, faceObject, faceSide ); }
    };
    if ( ( OriginalState( a ) & COMMAND_STATE_ENABLED ) == 0u ) { undo(); return a.pfnExecute( a.pContext, args ); } // Not for this selection: it says why.
    // The conversion stays only when the operation then changed something;
    // saying so waits until then, so a refusal is not hidden behind it.
    const usize steps = EditorHistory_AppliedStepCount( &ws->history );
    const command_result_t result = a.pfnExecute( a.pContext, args );
    if ( EditorHistory_AppliedStepCount( &ws->history ) == steps ) { undo(); return result; }
    const QString what = brushes.size() == 1 ? QStringLiteral( "Brush #%1 is now a mesh" ).arg( brushes.front() ) : QStringLiteral( "%1 brushes are now meshes" ).arg( brushes.size() );
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "%1 for %2 (Undo goes back one step at a time)" ).arg( what, QString::fromUtf8( a.pLabel ) ) );
    return result;
}

void InstallBrushAdapters( map_workspace_t *ws, command_registry_t *pRegistry )
{
    auto &list = BrushAdapters()[ws];
    list.clear();
    const auto in = []( const auto &table, const char *pId ) {
        return std::any_of( std::begin( table ), std::end( table ), [&]( const char *p ) { return std::strcmp( p, pId ) == 0; } );
    };
    for ( usize i = 0; i < pRegistry->commands.nCount; ++i ) {
        command_desc_t &command = pRegistry->commands.pData[i];
        const bool faces = in( kBrushFaceOperations, command.pId ), objects = in( kBrushObjectOperations, command.pId );
        if ( !faces && !objects ) { continue; }
        const auto atomicFace = std::strcmp( command.pId, "map.mesh.extrude" ) == 0 ? atomic_brush_face_t::EXTRUDE :
            std::strcmp( command.pId, "map.mesh.inset" ) == 0 ? atomic_brush_face_t::INSET :
            std::strcmp( command.pId, "map.mesh.quad_slice" ) == 0 ? atomic_brush_face_t::QUAD_SLICE : atomic_brush_face_t::NONE;
        list.push_back( { ws, command.pfnExecute, command.pfnState, command.pContext, command.pLabel, faces, objects,
                          std::strcmp( command.pId, "map.mesh.merge" ) == 0 ? usize{ 2u } : usize{ 1u }, atomicFace } );
        command.pfnExecute = AdaptedExecute; command.pfnState = AdaptedState; command.pContext = &list.back();
    }
}
} // namespace

command_registry_status_t MapGeometryCommands_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pRegistry != nullptr );
    constexpr u32 kEdit = COMMAND_FLAG_EDITS_DOCUMENT;
    const command_desc_t commands[]{
        // Mesh components: Hammer's vertex and edge selection tools.
        { "map.mesh.merge", "Merge", "Vertices: merge at the average; 2D-picked columns merge separately at each depth to preserve thickness. Drag onto a vertex to weld at that target. Edges: weld two open edges together. Meshes: join the selected meshes into one object.", "mesh-merge", nullptr,
          kEdit, Merge, MergeState, pWorkspace },
        { "map.mesh.collapse", "Collapse", "Collapse the selected vertices, each connected group of selected edges, or the selected face to a single point at its average.", "mesh-collapse", nullptr,
          kEdit, Collapse, CollapseState, pWorkspace },
        { "map.mesh.dissolve", "Dissolve", "Edges: remove the selected interior edges, joining the faces on either side. Vertices: remove each selected vertex, the faces around it becoming one.", "mesh-dissolve", nullptr,
          kEdit, Dissolve, DissolveState, pWorkspace },
        { "map.mesh.split_edges", "Split", "Insert a vertex at the midpoint of each selected edge.", "mesh-split", nullptr,
          kEdit, SplitEdges, EdgesState, pWorkspace },
        { "map.mesh.connect_edges", "Connect", "Vertices: add an edge on each face with two selected corners, including both depths of 2D-picked columns. Edges: join midpoints; every selected edge must share a face with another selected edge, with at most two selected edges per face.", "mesh-slice", nullptr,
          kEdit, Connect, ConnectState, pWorkspace },
        { "map.mesh.fill_hole", "Fill Hole", "Close the open boundary through each selected edge with one face, using the material across that edge.", "mesh-fill-hole", nullptr,
          kEdit, FillHole, FillHoleState, pWorkspace },
        { "map.mesh.bridge", "Bridge", "Edges: join the open boundaries through two selected open edges with a band of faces, or two selected chains of open edges strip by strip. Faces: join two selected faces with a tube.", "mesh-bridge", nullptr,
          kEdit, Bridge, BridgeState, pWorkspace },
        { "map.mesh.bevel", "Bevel", "Edges: bevel by the configured width and segments (one segment chamfers; more round the edge). Vertices: cut each selected corner off at the bevel width.", "mesh-bevel", nullptr,
          kEdit, Bevel, BevelState, pWorkspace },
        { "map.mesh.extrude_edges", "Extrude", "Pull the selected open edges off the surface, along their faces' normal, by the configured edge distance.", "tool-extrude", nullptr,
          kEdit, ExtrudeEdges<map_edge_extrude_t::NORMAL>, OpenEdgesState, pWorkspace },
        { "map.mesh.extend_edges", "Extend", "Continue the surface outward from the selected open edges, in its own plane, by the configured edge distance.", "tool-extrude", nullptr,
          kEdit, ExtrudeEdges<map_edge_extrude_t::EXTEND>, OpenEdgesState, pWorkspace },
        { "map.mesh.normals_hard", "Hard Normals", "Split shading across the selected edges.", "mesh-bevel", nullptr,
          kEdit, EdgeNormals<map_edge_normals_t::HARD>, EdgesState, pWorkspace },
        { "map.mesh.normals_soft", "Soft Normals", "Shade across the selected edges as one smooth surface, giving the faces a shared smoothing group when they have none.", "mesh-smooth", nullptr,
          kEdit, EdgeNormals<map_edge_normals_t::SOFT>, EdgesState, pWorkspace },
        { "map.mesh.normals_default", "Default Normals", "Remove the selected edges' hard or soft override; the faces' smoothing groups decide.", "mesh-flip-normals", nullptr,
          kEdit, EdgeNormals<map_edge_normals_t::DEFAULT>, EdgesState, pWorkspace },
        { "map.texture.weld_uvs", "Weld UVs", "Give every face corner at the selected vertices (or the selected edges' ends) the average of their material UVs.", "texture-lock", nullptr,
          kEdit, WeldUvs, VerticesOrEdgesState, pWorkspace },
        { "map.mesh.radial_align", "Radial Align", "Place three or more selected vertices on one circle around their centre, at their average radius.", "tool-rotate", nullptr,
          kEdit, RadialAlign, RadialAlignState, pWorkspace },
        { "map.mesh.split", "Split", "Faces: detach the selected mesh face from its neighbours. Meshes: separate each disconnected piece into its own object.", "mesh-split", nullptr,
          kEdit, DetachFace, DetachFaceState, pWorkspace },
        { "map.select.grow", "Grow Selection", "Add the vertices or edges next to the selected ones.", "select-grow", nullptr,
          COMMAND_FLAG_NONE, GrowShrink<true>, GrowShrinkState<true>, pWorkspace },
        { "map.select.shrink", "Shrink Selection", "Remove the selected vertices or edges at the selection's border.", "select-shrink", nullptr,
          COMMAND_FLAG_NONE, GrowShrink<false>, GrowShrinkState<false>, pWorkspace },
        // Whole meshes.
        { "map.mesh.smooth", "Smooth", "Relax every vertex of the selected meshes toward its neighbours by the configured strength.", "mesh-smooth", nullptr,
          kEdit, Smooth, SmoothState, pWorkspace },
        { "map.mesh.to_brush", "Convert to Brush", "Convert the selected closed convex meshes back to brushes; each face keeps its material and its UVs are fitted to the side's projection.", "mesh-to-brush", nullptr,
          kEdit, ToBrush, ToBrushState, pWorkspace },
        { "map.mesh.boolean_union", "Union", "Merge the selected closed meshes (and brushes) into one mesh, concave or not.", "boolean-union", nullptr,
          kEdit, Boolean<map_boolean_t::UNION>, BooleanState<map_boolean_t::UNION>, pWorkspace },
        { "map.mesh.boolean_subtract", "Subtract", "Cut the newer of two selected closed meshes out of the older one and remove it (holes and tunnels too).", "boolean-subtract", nullptr,
          kEdit, Boolean<map_boolean_t::SUBTRACT>, BooleanState<map_boolean_t::SUBTRACT>, pWorkspace },
        { "map.mesh.boolean_intersect", "Intersect", "Keep only what two selected closed meshes share; the other is removed.", "boolean-intersect", nullptr,
          kEdit, Boolean<map_boolean_t::INTERSECT>, BooleanState<map_boolean_t::INTERSECT>, pWorkspace },
        { "map.mesh.boolean_exclusive", "Exclusive", "Keep what only one of two selected closed meshes covers (where they overlap is cut away).", "boolean-subtract", nullptr,
          kEdit, Boolean<map_boolean_t::EXCLUSIVE>, BooleanState<map_boolean_t::EXCLUSIVE>, pWorkspace },
        { "map.mesh.boolean_split", "Split by Mesh", "Cut the older of two selected closed meshes along the newer one's surface into the part inside and the part outside; the newer stays.", "mesh-split", nullptr,
          kEdit, Boolean<map_boolean_t::SPLIT>, BooleanState<map_boolean_t::SPLIT>, pWorkspace },
        { "map.texture.replace", "Replace Materials...", "Replace one material with another on every brush side, mesh face and patch of the map.", "material-replace", "map.texture.replace [from] [to]",
          kEdit, ReplaceMaterials, ReplaceState, pWorkspace },
        { "map.group.create", "Group", "Group the selected objects so they select and move as one in Groups mode. Objects already grouped bring their group along, nested.", "group-create", nullptr,
          kEdit, CreateGroup, GroupState, pWorkspace },
        { "map.group.ungroup", "Ungroup", "Remove the outermost groups of the selected objects; nested groups become top-level.", "group-ungroup", nullptr,
          kEdit, Ungroup, UngroupState, pWorkspace },
        { "map.entity.tie", "Tie to Entity", "Make the selected brushes and meshes part of a brush entity (Hammer's Ctrl+T): the one entity also selected, else a new one of the Tie class.", "group-create", "map.entity.tie [class]",
          kEdit, TieToEntity, TieState, pWorkspace },
        { "map.entity.move_to_world", "Move to World", "Take the selected geometry (or every piece of the selected entities) out of its entity, back into the world; an entity left empty goes.", "group-ungroup", nullptr,
          kEdit, MoveToWorld, MoveToWorldState, pWorkspace },
        { "map.entity.connections", "Entity Connections...", "The selected entity's outputs and inputs (as Entity I/O).", "entity-logic", nullptr,
          COMMAND_FLAG_NONE, EntityIo, EntityIoState, pWorkspace },
        { "map.entity.properties", "Entity Properties", "Open Object Properties for the selection.", "view-properties", nullptr,
          COMMAND_FLAG_NONE, Forward<kPropertiesOpen>, ForwardState<kPropertiesOpen>, pWorkspace },
        { "map.texture.browser", "Material Browser", "Open the material browser to pick the active material.", "asset-material", nullptr,
          COMMAND_FLAG_NONE, Forward<kBrowseMaterials>, ForwardState<kBrowseMaterials>, pWorkspace },
        { "map.clip.flip", "Flip Kept Side", "Clip: keep the other side of the staged cut (Back and Front swap); nothing is cut until Enter.", "tool-clip", nullptr,
          COMMAND_FLAG_NONE, ClipFlip, ClipFlipState, pWorkspace },
        { "map.block.next_shape", "Next Block Shape", "Block tool: switch to the next shape (box, wedge, cylinder, spike, sphere, quad, arch, stairs, spiral stairs, pipe, torus).", "shape-box", nullptr,
          COMMAND_FLAG_NONE, NextShape, ReadyState, pWorkspace },
        { "map.entity.next_class", "Next Entity Class", "Entity tool: switch to the next class (those the map uses and common ones).", "entity-point", nullptr,
          COMMAND_FLAG_NONE, NextClass, ReadyState, pWorkspace },
        { "map.grid.show_3d", "Show 3D Grid", "Show or hide the ground grid in the 3D views.", "grid-show", nullptr,
          COMMAND_FLAG_NONE, Show3dGrid, AlwaysState, pWorkspace },
        { "map.texture.right", "Justify Right", "Texture tool: justify the texture to the face's right edge.", "uv-justify-right", nullptr,
          COMMAND_FLAG_NONE, Forward<kJustifyRight>, ForwardState<kJustifyRight>, pWorkspace },
        { "map.texture.top", "Justify Top", "Texture tool: justify the texture to the face's top edge.", "uv-justify-top", nullptr,
          COMMAND_FLAG_NONE, Forward<kJustifyTop>, ForwardState<kJustifyTop>, pWorkspace },
        { "map.texture.bottom", "Justify Bottom", "Texture tool: justify the texture to the face's bottom edge.", "uv-justify-bottom", nullptr,
          COMMAND_FLAG_NONE, Forward<kJustifyBottom>, ForwardState<kJustifyBottom>, pWorkspace },
        { "map.texture.center", "Justify Center", "Texture tool: centre the texture on the face.", "uv-justify-center", nullptr,
          COMMAND_FLAG_NONE, Forward<kJustifyCenter>, ForwardState<kJustifyCenter>, pWorkspace },
        { "map.vertex.merge", "Merge Vertices", "Vertex tool: merge the selected vertices (as Merge).", "vertex-weld", nullptr,
          COMMAND_FLAG_NONE, Forward<kMeshMerge>, ForwardState<kMeshMerge>, pWorkspace },
        { "map.vertex.split_edge", "Split Edge", "Vertex tool: split the selected edges (as Split Edges).", "select-edges", nullptr,
          COMMAND_FLAG_NONE, Forward<kSplitEdges>, ForwardState<kSplitEdges>, pWorkspace },
        { "map.vertex.snap", "Snap to Grid", "Vertex tool: snap the selection to the grid.", "snap-to-grid", nullptr,
          COMMAND_FLAG_NONE, Forward<kSnapToGrid>, ForwardState<kSnapToGrid>, pWorkspace },
        { "map.layer.move_selection", "Move to Layer...", "Move the selected geometry into another layer (named, or chosen from a list).", "layer-new", "map.layer.move_selection [layer]",
          kEdit, MoveToLayer, MoveToLayerState, pWorkspace },
        { "map.group.add", "Add to Group", "Put the selected objects that are in no group into the one group also selected (TrenchBroom's Add Objects to Group).", "group-create", nullptr,
          kEdit, AddToGroup, AddToGroupState, pWorkspace },
        { "map.group.remove", "Remove from Group", "Take the selected objects out of the group they are in (pick them in Objects mode); a group left empty goes away.", "group-ungroup", nullptr,
          kEdit, RemoveFromGroup, RemoveFromGroupState, pWorkspace },
        { "map.group.select", "Select Group", "Select everything in the groups of the selected objects (from Objects mode, widen to the whole group).", "select-groups", nullptr,
          kEdit, SelectGroupMembers, SelectGroupState, pWorkspace },
        { "map.layer.create", "New Layer", "Add a layer and move the selected geometry into it.", "layer-new", nullptr,
          kEdit, CreateLayer, LayerState, pWorkspace },
        { "map.visgroup.create", "New Visgroup from Selection", "Make a named visgroup of the selection; show and hide it under Visgroups in Auto Vis Groups.", "visgroup-new", "map.visgroup.create [name]",
          kEdit, CreateVisgroup, VisgroupState, pWorkspace },
        { "map.prefab.create", "Create Prefab...", "Save the selection as a .cyprefab file, relative to its base; the map keeps the objects.", "prefab-create", "map.prefab.create [file]",
          COMMAND_FLAG_NONE, CreatePrefab, PrefabCreateState, pWorkspace },
        { "map.prefab.insert", "Insert Prefab...", "Place a copy of a .cyprefab file at the last 2D cursor position, with fresh IDs.", "prefab-create", "map.prefab.insert [file]",
          kEdit, InsertPrefab, PrefabInsertState, pWorkspace },
        { "map.mesh.slice", "Slice", "Cut the selected meshes along a line drawn with the Clip tool in a 2D view. Clip mode Both only splits the crossed faces; Front or Back removes the other side and caps the opening. Without a line, starts the Clip tool.", "mesh-slice", nullptr,
          kEdit, Slice, SliceState, pWorkspace },
        { "map.pivot.clear", "Clear Pivot", "Remove the Pivot tool's point; Rotate 90 and the flips turn about the selection's centre again.", "tool-pivot", nullptr,
          COMMAND_FLAG_NONE, ClearPivot, PivotState, pWorkspace },
        { "map.mesh.subdivide", "Subdivide", "Replace the selected meshes by one level of Catmull-Clark subdivision. UVs and materials follow each face; hard edges stay sharp and open corners stay put.", "mesh-subdivide", nullptr,
          kEdit, Subdivide, SubdivideState, pWorkspace },
        { "map.mesh.subdivision_increase", "Increase Subdivision", "Smooth the mesh one level further (Catmull-Clark, shown live; the cage stays editable). Bake makes it real.", "mesh-subdivide", nullptr,
          kEdit, SubdivisionStep<1>, ModifierState, pWorkspace },
        { "map.mesh.subdivision_decrease", "Decrease Subdivision", "Smooth the mesh one level less.", "mesh-subdivide", nullptr, kEdit, SubdivisionStep<-1>, ModifierState, pWorkspace },
        { "map.mesh.subdivision_level_0", "Level 0", "No subdivision: the cage as it is.", "mesh-subdivide", nullptr, kEdit, SubdivisionLevel<0>, ModifierState, pWorkspace },
        { "map.mesh.subdivision_level_1", "Level 1", "Subdivision level 1 (each quad into 4).", "mesh-subdivide", nullptr, kEdit, SubdivisionLevel<1>, ModifierState, pWorkspace },
        { "map.mesh.subdivision_level_2", "Level 2", "Subdivision level 2.", "mesh-subdivide", nullptr, kEdit, SubdivisionLevel<2>, ModifierState, pWorkspace },
        { "map.mesh.subdivision_level_3", "Level 3", "Subdivision level 3.", "mesh-subdivide", nullptr, kEdit, SubdivisionLevel<3>, ModifierState, pWorkspace },
        { "map.mesh.subdivision_level_4", "Level 4", "Subdivision level 4.", "mesh-subdivide", nullptr, kEdit, SubdivisionLevel<4>, ModifierState, pWorkspace },
        { "map.mesh.subdivision_level_5", "Level 5", "Subdivision level 5 (the most: about 1000 faces per cage quad).", "mesh-subdivide", nullptr, kEdit, SubdivisionLevel<5>, ModifierState, pWorkspace },
        { "map.mesh.subdivision_bake", "Bake Subdivision", "Make the shown subdivision (and any other modifiers) the mesh itself.", "mesh-subdivide", nullptr, kEdit, ApplyModifiers, StackState, pWorkspace },
        { "map.mesh.modifier_mirror_x", "Mirror X", "Add a live mirror across X: through the pivot when set, else the mesh's far side. Seam vertices weld.", "tool-mirror", nullptr,
          kEdit, AddModifier<geo::modifier_kind_t::MIRROR, 0>, ModifierState, pWorkspace },
        { "map.mesh.modifier_mirror_y", "Mirror Y", "Add a live mirror across Y: through the pivot when set, else the mesh's far side.", "tool-mirror", nullptr,
          kEdit, AddModifier<geo::modifier_kind_t::MIRROR, 1>, ModifierState, pWorkspace },
        { "map.mesh.modifier_mirror_z", "Mirror Z", "Add a live mirror across Z: through the pivot when set, else the mesh's top.", "tool-mirror", nullptr,
          kEdit, AddModifier<geo::modifier_kind_t::MIRROR, 2>, ModifierState, pWorkspace },
        { "map.mesh.modifier_array", "Array", "Add a live row of copies along the array axis (count and distance below; distance 0: copies touch).", "edit-duplicate", nullptr,
          kEdit, AddModifier<geo::modifier_kind_t::LINEAR_ARRAY, 0>, ModifierState, pWorkspace },
        { "map.mesh.modifier_radial", "Radial Array", "Add live copies in a full circle about the vertical line through the pivot (or the world origin).", "tool-rotate", nullptr,
          kEdit, AddModifier<geo::modifier_kind_t::RADIAL_ARRAY, 0>, ModifierState, pWorkspace },
        { "map.mesh.modifier_bend", "Bend", "Add a live bend about the vertical axis by the bend angle, along the array axis.", "tool-curve", nullptr,
          kEdit, AddModifier<geo::modifier_kind_t::BEND, 0>, ModifierState, pWorkspace },
        { "map.mesh.modifier_taper", "Taper", "Add a live taper: the mesh narrows upward to the taper factor.", "tool-scale", nullptr,
          kEdit, AddModifier<geo::modifier_kind_t::TAPER, 0>, ModifierState, pWorkspace },
        { "map.mesh.modifiers_apply", "Apply Modifiers", "Make the modifiers' result the mesh itself (the stack is removed).", "material-apply", nullptr, kEdit, ApplyModifiers, StackState, pWorkspace },
        { "map.mesh.modifier_remove_last", "Remove Last Modifier", "Remove the most recently added modifier.", "edit-undo", nullptr, kEdit, RemoveModifiers<true>, StackState, pWorkspace },
        { "map.mesh.modifiers_clear", "Clear Modifiers", "Remove every modifier: the mesh shows as its cage again.", "edit-delete", nullptr, kEdit, RemoveModifiers<false>, StackState, pWorkspace },
        { "map.mesh.solidify", "Solidify", "Thicken the selected open meshes into closed solids by the configured thickness. The original surface keeps its identity.", "mesh-solidify", nullptr,
          kEdit, Solidify, SolidifyState, pWorkspace },
        // Objects.
        { "map.brush.carve", "Carve", "Cut the selected brushes out of every visible brush they touch. The selected brushes stay.", "csg-carve", nullptr,
          kEdit, Carve, CarveState, pWorkspace },
        { "map.brush.snap_to_grid", "Snap Selection to Grid", "Objects: move each so the minimum corner of its bounds lies on the grid. Vertices: move each to the nearest grid point.", "snap-to-grid", nullptr,
          kEdit, SnapToGrid, SnapState, pWorkspace },
        { "map.transform.flip_horizontal", "Flip Horizontally", "Mirror the selection across the last 2D view's vertical axis through its centre. Identities and, with texture lock, UVs follow.", "transform-flip-horizontal", nullptr,
          kEdit, Flip<false>, EditableObjectsState, pWorkspace },
        { "map.transform.flip_vertical", "Flip Vertically", "Mirror the selection across the last 2D view's horizontal axis through its centre.", "transform-flip-vertical", nullptr,
          kEdit, Flip<true>, EditableObjectsState, pWorkspace },
        { "map.transform.rotate_cw", "Rotate 90 Clockwise", "Rotate the selection 90 degrees clockwise as seen in the last 2D view, about its centre.", "transform-rotate-cw", nullptr,
          kEdit, Rotate90<true>, TurnableObjectsState, pWorkspace },
        { "map.transform.rotate_ccw", "Rotate 90 Counter-Clockwise", "Rotate the selection 90 degrees counter-clockwise as seen in the last 2D view, about its centre.", "transform-rotate-ccw", nullptr,
          kEdit, Rotate90<false>, TurnableObjectsState, pWorkspace },
        { "map.align.left", "Align Left", "Move each selected object so its left side meets the selection's leftmost, in the last 2D view.", "align-left", nullptr,
          kEdit, Align<align_side_t::LEFT>, AlignState, pWorkspace },
        { "map.align.right", "Align Right", "Move each selected object so its right side meets the selection's rightmost, in the last 2D view.", "align-right", nullptr,
          kEdit, Align<align_side_t::RIGHT>, AlignState, pWorkspace },
        { "map.align.top", "Align Top", "Move each selected object so its top meets the selection's highest, in the last 2D view.", "align-top", nullptr,
          kEdit, Align<align_side_t::TOP>, AlignState, pWorkspace },
        { "map.align.bottom", "Align Bottom", "Move each selected object so its bottom meets the selection's lowest, in the last 2D view.", "align-bottom", nullptr,
          kEdit, Align<align_side_t::BOTTOM>, AlignState, pWorkspace },
        // Selection queries.
        { "map.select.touching", "Select Touching", "Add every visible object whose bounds touch a selected object.", "select-touching", nullptr,
          COMMAND_FLAG_NONE, SelectTouching, TouchingState, pWorkspace },
        { "map.select.same_material", "Select Same Material", "Select every visible object that uses the selected face's material, or any material of the selected objects.", "select-same-material", nullptr,
          COMMAND_FLAG_NONE, SelectSameMaterial, SameMaterialState, pWorkspace },
        // Materials: the selected face in Faces mode, every face of the selected brushes and meshes otherwise.
        { "map.texture.align_world", "Align to World", "Project the material along each face's dominant world axis, keeping its scale; rotation and shift reset.", "uv-align-world", nullptr,
          kEdit, TextureAlign<map_uv_align_t::WORLD>, TextureState, pWorkspace },
        { "map.texture.align_face", "Align to Face", "Project the material in each face's own plane, so sloped faces do not stretch; scale kept, rotation and shift reset.", "uv-align-face", nullptr,
          kEdit, TextureAlign<map_uv_align_t::FACE>, TextureState, pWorkspace },
        { "map.texture.fit", "Fit", "Scale and shift the material so one repeat exactly covers each face.", "uv-fit", nullptr,
          kEdit, TextureAlign<map_uv_align_t::FIT>, TextureState, pWorkspace },
        { "map.texture.justify_left", "Justify Left", "Move the material so a texture edge lies on each face's left (minimum U).", "uv-justify-left", nullptr,
          kEdit, TextureAlign<map_uv_align_t::JUSTIFY_LEFT>, TextureState, pWorkspace },
        { "map.texture.justify_right", "Justify Right", "Move the material so a texture edge lies on each face's right (maximum U).", "uv-justify-right", nullptr,
          kEdit, TextureAlign<map_uv_align_t::JUSTIFY_RIGHT>, TextureState, pWorkspace },
        { "map.texture.justify_top", "Justify Top", "Move the material so a texture edge lies on each face's top (maximum V).", "uv-justify-top", nullptr,
          kEdit, TextureAlign<map_uv_align_t::JUSTIFY_TOP>, TextureState, pWorkspace },
        { "map.texture.justify_bottom", "Justify Bottom", "Move the material so a texture edge lies on each face's bottom (minimum V).", "uv-justify-bottom", nullptr,
          kEdit, TextureAlign<map_uv_align_t::JUSTIFY_BOTTOM>, TextureState, pWorkspace },
        { "map.texture.justify_center", "Justify Center", "Centre the material on each face.", "uv-justify-center", nullptr,
          kEdit, TextureAlign<map_uv_align_t::JUSTIFY_CENTER>, TextureState, pWorkspace },
        { "map.texture.rotate", "Rotate Material", "Rotate the material about each face's centre: 90 degrees, or the given angle.", "uv-rotate", "map.texture.rotate [degrees]",
          kEdit, TextureRotate, TextureState, pWorkspace },
        { "map.texture.scale", "Scale Material", "Scale the material about each face's centre: twice as large, or by the given factor (one, or U and V).", "uv-scale", "map.texture.scale [factor] [factorV]",
          kEdit, TextureScale, TextureState, pWorkspace },
        { "map.texture.project_box", "Box Mapping", "Project the material on mesh faces from the world axis each faces most (tri-planar), at the default scale.", "uv-align-world", nullptr,
          kEdit, ProjectTexture<map_uv_projection_t::BOX>, MeshSurfaceState, pWorkspace },
        { "map.texture.project_cylinder", "Cylindrical Mapping", "Wrap the material around the selection's centre line along the projection axis (pillars, pipes).", "uv-align-face", nullptr,
          kEdit, ProjectTexture<map_uv_projection_t::CYLINDER>, MeshSurfaceState, pWorkspace },
        { "map.texture.project_planar", "Planar Mapping", "Project the material on every picked mesh face straight along the projection axis, like a decal.", "uv-align-face", nullptr,
          kEdit, ProjectTexture<map_uv_projection_t::PLANAR>, MeshSurfaceState, pWorkspace },
        { "map.mesh.smoothing_set", "Set Smoothing Group", "Put the picked mesh faces (or whole meshes) in the smoothing group below: faces in one group shade smoothly together.", "mesh-smooth", nullptr,
          kEdit, SmoothingGroups<false>, MeshSurfaceState, pWorkspace },
        { "map.mesh.smoothing_clear", "Clear Smoothing", "Take the picked mesh faces (or whole meshes) out of every smoothing group: they shade flat.", "mesh-flip-normals", nullptr,
          kEdit, SmoothingGroups<true>, MeshSurfaceState, pWorkspace },
        { "map.mesh.auto_hard_edges", "Auto Hard Edges", "Make edges sharper than the sharp edge angle hard and the rest soft, on the picked faces' mesh or the selected meshes.", "mesh-bevel", nullptr,
          kEdit, AutoHardEdges, MeshSurfaceState, pWorkspace },
        { "map.texture.set_scale", "Set Texture Scale", "Give the material this scale on every selected face (Hammer's convention: 0.25 is 128 world units per repeat); one value, or U and V.", "uv-scale",
          "map.texture.set_scale <scale> [scaleV]", kEdit, TextureSetScale, TextureState, pWorkspace },
        { "map.texture.shift", "Shift Material", "Move the material along its U axis by one grid step, or by the given U and V distances in world units.", "uv-shift", "map.texture.shift [u] [v]",
          kEdit, TextureShift, TextureState, pWorkspace },
        { "map.texture.unwrap", "UV Islands", "Unwrap the selected mesh face, or the selected meshes, into flat islands by facing direction, packed into one texture repeat.", "uv-islands", nullptr,
          kEdit, Unwrap, UnwrapState, pWorkspace },
        { "map.entity.io", "Entity I/O...", "Edit the selected entity's outputs and see the connections that reach it (Hammer's Outputs and Inputs).", "entity-logic", nullptr,
          COMMAND_FLAG_NONE, EntityIo, EntityIoState, pWorkspace },
        { "map.select.inside", "Select Inside", "Select every other visible object that lies entirely within the selected objects' bounds; the selected objects drop out.", "select-touching", nullptr,
          COMMAND_FLAG_NONE, SelectInside<false>, TouchingState, pWorkspace },
        { "map.select.tall", "Select Tall", "Select every other visible object within the selected objects' bounds as seen in the last 2D view, at any depth.", "select-touching", nullptr,
          COMMAND_FLAG_NONE, SelectInside<true>, TouchingState, pWorkspace },
        { "map.select.connected", "Select Connected", "Add every vertex or edge connected to the selected ones.", "select-all", nullptr,
          COMMAND_FLAG_NONE, ComponentQuery<component_query_t::CONNECTED>, ComponentQueryState<component_query_t::CONNECTED>, pWorkspace },
        { "map.select.invert_components", "Invert Component Selection", "Select the mesh's other vertices or edges instead.", "select-invert", nullptr,
          COMMAND_FLAG_NONE, ComponentQuery<component_query_t::INVERT>, ComponentQueryState<component_query_t::INVERT>, pWorkspace },
        { "map.select.ribs", "Select Ribs", "Select the edges that cross the selected edges at their vertices: the ribs of an edge loop.", "select-ring", nullptr,
          COMMAND_FLAG_NONE, ComponentQuery<component_query_t::RIBS>, ComponentQueryState<component_query_t::RIBS>, pWorkspace },
        { "map.select.boundary_edges", "Select Open Edges", "Select every edge of the mesh with a face on one side only: the rims of its holes.", "mesh-fill-hole", nullptr,
          COMMAND_FLAG_NONE, EdgeQuery<false>, EdgeQueryState<false>, pWorkspace },
        { "map.select.sharp_edges", "Select Sharp Edges", "Select every edge whose faces meet at the sharp edge angle or more, or the given angle in degrees.", "mesh-bevel",
          "map.select.sharp_edges [degrees]", COMMAND_FLAG_NONE, EdgeQuery<true>, EdgeQueryState<true>, pWorkspace },
        { "map.select.path", "Select Path", "With two vertices selected, select the shortest path of vertices between them.", "tool-path", nullptr,
          COMMAND_FLAG_NONE, SelectPath, PathState, pWorkspace },
        { "map.select.to_edges", "Convert to Edges", "Switch to Edges mode with the edges whose two ends are selected.", "select-edges", nullptr,
          COMMAND_FLAG_NONE, ConvertSelection<true>, ConvertState<true>, pWorkspace },
        { "map.select.to_vertices", "Convert to Vertices", "Switch to Vertices mode with the selected edges' ends.", "select-vertices", nullptr,
          COMMAND_FLAG_NONE, ConvertSelection<false>, ConvertState<false>, pWorkspace },
        { "map.select.coplanar", "Select Coplanar", "From the active face, add the neighbouring faces that lie in its plane (within 1 degree, or the given angle).", "select-faces",
          "map.select.coplanar [degrees]", COMMAND_FLAG_NONE, FaceQuery<face_query_t::COPLANAR>, FaceQueryState<face_query_t::COPLANAR>, pWorkspace },
        { "map.select.faces_facing", "Select Faces Facing", "Select every face of the mesh facing the active face's way, within the face select angle (or the given angle).", "select-faces",
          "map.select.faces_facing [degrees]", COMMAND_FLAG_NONE, FaceQuery<face_query_t::FACING>, FaceQueryState<face_query_t::FACING>, pWorkspace },
        { "map.select.same_corners", "Select Same Corner Count", "Select every face of the mesh with as many corners as the active face: all triangles, all quads...", "select-same-class",
          nullptr, COMMAND_FLAG_NONE, FaceQuery<face_query_t::SAME_CORNERS>, FaceQueryState<face_query_t::SAME_CORNERS>, pWorkspace },
        { "map.select.faces_material", "Select Faces by Material", "Select every face of the mesh with the active face's material.", "select-same-material",
          nullptr, COMMAND_FLAG_NONE, FaceQuery<face_query_t::SAME_MATERIAL>, FaceQueryState<face_query_t::SAME_MATERIAL>, pWorkspace },
        { "map.select.face_path", "Select Face Path", "With two faces selected, select the shortest strip of faces between them.", "tool-path",
          nullptr, COMMAND_FLAG_NONE, FaceQuery<face_query_t::PATH>, FaceQueryState<face_query_t::PATH>, pWorkspace },
        { "map.mesh.offset", "Offset", "Move the selected vertices, edges or face along their normals by the offset distance, or the given distance (negative moves in).", "mesh-extrude",
          "map.mesh.offset [distance]", COMMAND_FLAG_NONE, Offset, OffsetState, pWorkspace },
        { "map.mesh.flatten", "Flatten", "Move the selected vertices, edges or face onto their best-fitting plane.", "terrain-flatten", nullptr,
          COMMAND_FLAG_NONE, Flatten<map_flatten_t::BEST_FIT>, FlattenState<map_flatten_t::BEST_FIT>, pWorkspace },
        { "map.mesh.flatten_normal", "Flatten to Normal", "Move the selected components onto the plane of their average normal.", "terrain-flatten", nullptr,
          COMMAND_FLAG_NONE, Flatten<map_flatten_t::AVERAGE_NORMAL>, FlattenState<map_flatten_t::AVERAGE_NORMAL>, pWorkspace },
        { "map.mesh.flatten_x", "Flatten X", "Give the selected components one X coordinate, their average.", "terrain-flatten", nullptr,
          COMMAND_FLAG_NONE, Flatten<map_flatten_t::AXIS_X>, FlattenState<map_flatten_t::AXIS_X>, pWorkspace },
        { "map.mesh.flatten_y", "Flatten Y", "Give the selected components one Y coordinate, their average.", "terrain-flatten", nullptr,
          COMMAND_FLAG_NONE, Flatten<map_flatten_t::AXIS_Y>, FlattenState<map_flatten_t::AXIS_Y>, pWorkspace },
        { "map.mesh.flatten_z", "Flatten Z", "Give the selected components one Z coordinate, their average.", "terrain-flatten", nullptr,
          COMMAND_FLAG_NONE, Flatten<map_flatten_t::AXIS_Z>, FlattenState<map_flatten_t::AXIS_Z>, pWorkspace },
        { "map.mesh.slide_loop", "Slide Edge Loop", "Slide the loop through the selected edge toward a neighbouring loop by the slide factor, or the given factor between -1 and 1.", "mesh-loop-cut",
          "map.mesh.slide_loop [factor]", COMMAND_FLAG_NONE, SlideLoop, SlideState, pWorkspace },
        { "map.mesh.snap_edge_to_edge", "Snap Edge to Edge", "With two edges selected, weld the first onto the edge picked last; the target keeps its place.", "snap-to-grid", nullptr,
          COMMAND_FLAG_NONE, SnapEdgeToEdge, SnapEdgeState, pWorkspace },
        { "map.mesh.delete_face", "Delete Face", "Remove the selected mesh face, opening a hole; edges and vertices nothing else uses go too.", "edit-delete", nullptr,
          COMMAND_FLAG_NONE, DeleteFace, DeleteFaceState, pWorkspace },
        { "map.compile", "Compile Map", "Cook the map's geometry (collision, visibility, walkable surfaces, lightmap charts, render batches) and write the interchange file the map compilers read, beside the map.", "map-compile", nullptr,
          COMMAND_FLAG_NONE, CompileMap, CompileState, pWorkspace },
        { "map.mesh.seam_mark", "Mark Seam", "Mark the selected edges as UV seams: where the texture layout may tear.", "select-edges", nullptr,
          COMMAND_FLAG_NONE, EditSeams<map_seam_edit_t::SET>, SeamState, pWorkspace },
        { "map.mesh.seam_clear", "Clear Seam", "Clear the UV seam mark from the selected edges.", "select-edges", nullptr, COMMAND_FLAG_NONE, EditSeams<map_seam_edit_t::CLEAR>, SeamState, pWorkspace },
        { "map.select.uv_island", "Select UV Island", "Pick every face of the picked face's UV island (joined across non-seam edges with matching UVs).", "select-faces", nullptr,
          COMMAND_FLAG_NONE, SelectUvIsland, UvIslandState, pWorkspace },
        { "map.mesh.repair", "Repair Mesh", "Weld close vertices, remove degenerate and duplicate faces, fix inconsistent winding and cap flat holes. Reports what changed; UVs are re-projected.", "vertex-weld", nullptr,
          COMMAND_FLAG_NONE, RepairMesh, RepairState, pWorkspace },
        { "map.patch.from_face", "Patch from Face", "Cover the picked brush face with flat Bezier patches (NetRadiant), with its material, ready to bend into curved detail. The brush stays.", "tool-patch", nullptr,
          COMMAND_FLAG_NONE, PatchFromFace, BrushFaceState, pWorkspace },
        { "file.import", "Import Wavefront OBJ...", "Add the objects of a Wavefront OBJ as editable meshes, keeping their UVs (Import scale and Y up below).", "file-open", "file.import [path.obj]",
          COMMAND_FLAG_NONE, ImportObj, ReadyState, pWorkspace },
        { "file.export", "Export Wavefront OBJ...", "Write the map's brushes, meshes (modifiers applied) and patches to a Wavefront OBJ with UVs, normals and a .mtl of its materials (TrenchBroom's Export).", "file-save-as", "file.export [path.obj]",
          COMMAND_FLAG_NONE, ExportObj, AlwaysState, pWorkspace },
        { "edit.find", "Find Entities...", "Find entities by class, name or any property value; select or frame them (Hammer's Find Entities).", "edit-find", nullptr,
          COMMAND_FLAG_NONE, FindEntities<false>, ReadyState, pWorkspace },
        { "edit.find_replace", "Find and Replace...", "Replace text in the class, name and property values of the entities found, as one undo step.", "edit-find", nullptr,
          COMMAND_FLAG_NONE, FindEntities<true>, ReadyState, pWorkspace },
        { "map.mesh.project", "Project onto World", "Hammer 5's Mesh Projection: drop the picked vertices (or whole selected meshes) along the projection direction onto the surfaces below, the offset short of them (a road onto terrain, a decal strip onto a wall).", "snap-surface", nullptr,
          COMMAND_FLAG_NONE, ProjectMesh, ProjectState, pWorkspace },
        { "map.texture.fast", "Fast Texture...", "Hammer 5's Fast Texture: the faces' material shown whole over a grid; drag a rectangle (or click a cell) and the picked faces' texture fits into it. Use Existing, Unwrap Planar or Unwrap Square; flips and an inset for trims.", "tool-texture", nullptr,
          COMMAND_FLAG_NONE, FastTexture, FastTextureState, pWorkspace },
        { "map.texture.hotspot", "Apply Hotspots", "Hotspot texturing (Hammer 5): each picked face takes the region of its material's hotspot rectangles (materials/x.cyrect beside materials/x.cymat) that fits its size best. Mark regions in Fast Texture.", "uv-fit", nullptr,
          COMMAND_FLAG_NONE, ApplyHotspots, HotspotState, pWorkspace },
        { "map.texture.wrap_coplanar", "Wrap Texture Across Coplanar Faces", "Continue the picked brush face's material and alignment over every face in its plane that touches it, across brushes: one seamless texture over a floor or wall made of many brushes.", "uv-align-world", nullptr,
          COMMAND_FLAG_NONE, WrapCoplanar, BrushFaceState, pWorkspace },
        { "map.brush.sweep", "Brush Sweep", "Extrude the picked brush face along a straight, arc or S-bend path in segments: one convex brush per step, watertight (stairs that turn, curved corridors, ramps).", "curve-sweep", nullptr,
          COMMAND_FLAG_NONE, BrushSweep, BrushFaceState, pWorkspace },
        { "map.mesh.create_displacement", "Create Displacement", "Turn the picked four-cornered face into a displacement grid mesh (power, elevation and noise below), ready to sculpt with the Displacement tool.", "displacement", nullptr,
          COMMAND_FLAG_NONE, CreateDisplacement, CreateDisplacementState, pWorkspace },
        { "map.mesh.extract_faces", "Extract Faces", "Copy the picked faces into a new mesh object, which is selected; the original mesh stays as it is.", "mesh-split", nullptr,
          COMMAND_FLAG_NONE, ExtractFaces<false>, DeleteFaceState, pWorkspace },
        { "map.mesh.thicken_faces", "Thicken Faces", "Make a new closed mesh from the picked faces, thickened by the thicken distance; the original mesh stays as it is.", "mesh-solidify", nullptr,
          COMMAND_FLAG_NONE, ExtractFaces<true>, DeleteFaceState, pWorkspace },
        { "map.mesh.combine_faces", "Combine Faces", "Merge adjacent picked faces into one face (the edges between them are removed).", "mesh-merge", nullptr,
          COMMAND_FLAG_NONE, CombineFaces, CombineFacesState, pWorkspace },
        { "map.mesh.remove_bad_faces", "Remove Bad Faces", "Collapse faces with no area (slivers and points) in the picked faces' mesh or the selected meshes.", "mesh-collapse", nullptr,
          COMMAND_FLAG_NONE, RemoveBadFaces, RemoveBadFacesState, pWorkspace },
        { "map.mesh.weld", "Weld", "Join vertices of the selected meshes that share an edge and lie within the weld distance (or the given distance), collapsing short edges.", "vertex-weld",
          "map.mesh.weld [distance]", COMMAND_FLAG_NONE, Weld<false>, MeshObjectsState, pWorkspace },
        { "map.mesh.merge_by_distance", "Merge by Distance", "Join any vertices of the selected meshes within the weld distance (or the given distance), closing seams.", "mesh-merge",
          "map.mesh.merge_by_distance [distance]", COMMAND_FLAG_NONE, Weld<true>, MeshObjectsState, pWorkspace },
        { "map.brush.extrude_new", "Extrude to New Brush", "Extrude the selected brush face into a separate brush one grid step (or the given distance) out; a negative distance cuts the slab off the brush instead.", "tool-extrude",
          "map.brush.extrude_new [distance]", COMMAND_FLAG_NONE, ExtrudeNew, ExtrudeNewState, pWorkspace },
        { "map.patch.insert_row", "Insert Patch Row", "Add a row of controls to the selected patches without changing their shape, so they can bend more.", "tool-patch", nullptr,
          COMMAND_FLAG_NONE, PatchOp<map_patch_op_t::INSERT_ROW>, PatchState<map_patch_op_t::INSERT_ROW>, pWorkspace },
        { "map.patch.insert_column", "Insert Patch Column", "Add a column of controls to the selected patches without changing their shape.", "tool-patch", nullptr,
          COMMAND_FLAG_NONE, PatchOp<map_patch_op_t::INSERT_COLUMN>, PatchState<map_patch_op_t::INSERT_COLUMN>, pWorkspace },
        { "map.patch.remove_row", "Remove Patch Row", "Merge the last two rows of spans of the selected patches.", "tool-patch", nullptr,
          COMMAND_FLAG_NONE, PatchOp<map_patch_op_t::REMOVE_ROW>, PatchState<map_patch_op_t::REMOVE_ROW>, pWorkspace },
        { "map.patch.remove_column", "Remove Patch Column", "Merge the last two columns of spans of the selected patches.", "tool-patch", nullptr,
          COMMAND_FLAG_NONE, PatchOp<map_patch_op_t::REMOVE_COLUMN>, PatchState<map_patch_op_t::REMOVE_COLUMN>, pWorkspace },
        { "map.patch.transpose", "Transpose Patch", "Swap the selected patches' rows and columns; the texture turns and the other side faces out.", "transform-rotate-cw", nullptr,
          COMMAND_FLAG_NONE, PatchOp<map_patch_op_t::TRANSPOSE>, PatchState<map_patch_op_t::TRANSPOSE>, pWorkspace },
        { "map.patch.thicken", "Thicken Patch", "Add a copy of each selected patch behind it by the patch thickness (or the given distance), with seams closing the open borders when Thicken seams is on.", "mesh-solidify",
          "map.patch.thicken [thickness]", COMMAND_FLAG_NONE, Thicken, ThickenState, pWorkspace },
        { "map.patch.cap", "Cap Patch", "Close every border of the selected patches that runs round in a loop (a cylinder's, cone's or lathe's open ends) with a patch facing outward, as NetRadiant's Cap.", "tool-patch",
          nullptr, COMMAND_FLAG_NONE, CapPatch, ThickenState, pWorkspace },
        { "map.terrain.raise", "Raise Terrain", "Terrain tool: drag on a terrain to raise the ground (Ctrl/Command lowers it).", "terrain-raise", nullptr,
          COMMAND_FLAG_CHECKABLE, TerrainMode<kTerrainRaise>, TerrainModeState<kTerrainRaise>, pWorkspace },
        { "map.terrain.lower", "Lower Terrain", "Terrain tool: drag on a terrain to lower the ground.", "terrain-lower", nullptr,
          COMMAND_FLAG_CHECKABLE, TerrainMode<kTerrainLower>, TerrainModeState<kTerrainLower>, pWorkspace },
        { "map.terrain.smooth", "Smooth Terrain", "Terrain tool: drag to even out bumps.", "terrain-smooth", nullptr,
          COMMAND_FLAG_CHECKABLE, TerrainMode<kTerrainSmooth>, TerrainModeState<kTerrainSmooth>, pWorkspace },
        { "map.terrain.flatten", "Flatten Terrain", "Terrain tool: drag to level the ground at the height the stroke started at.", "terrain-flatten", nullptr,
          COMMAND_FLAG_CHECKABLE, TerrainMode<kTerrainFlatten>, TerrainModeState<kTerrainFlatten>, pWorkspace },
        { "map.game.reload", "Reload Game Profile", "Read the game profile (game/<game>.cygame in the content folders) again: entity classes, their properties, inputs and outputs.", "file-open",
          nullptr, COMMAND_FLAG_NONE, ReloadGame, AlwaysState, pWorkspace },
        { "map.selection_set.create", "New Selection Set", "Save the selection as a named selection set (Hammer 5), kept with the map; pick it again from the Selection Sets panel.", "selection-set-new",
          "map.selection_set.create [name]", COMMAND_FLAG_NONE, CreateSelectionSet, SelectionSetCreateState, pWorkspace },
        { "map.selection_set.add", "Add to Selection Set", "Add the selected objects to the selection set.", "selection-set-add",
          "map.selection_set.add <set>", COMMAND_FLAG_NONE, EditSelectionSet<true>, SelectionSetEditState, pWorkspace },
        { "map.selection_set.remove", "Remove from Selection Set", "Take the selected objects out of the selection set (the objects stay in the map).", "selection-set-remove",
          "map.selection_set.remove <set>", COMMAND_FLAG_NONE, EditSelectionSet<false>, SelectionSetEditState, pWorkspace },
        { "map.selection_set.rename", "Rename Selection Set", "Give the selection set a new name.", "view-selection-sets",
          "map.selection_set.rename <set> <name>", COMMAND_FLAG_NONE, RenameSelectionSet, SelectionSetState, pWorkspace },
        { "map.selection_set.delete", "Delete Selection Set", "Forget the selection set; its objects stay in the map.", "view-selection-sets",
          "map.selection_set.delete <set>", COMMAND_FLAG_NONE, DeleteSelectionSet, SelectionSetState, pWorkspace },
        { "map.selection_set.select", "Select Selection Set", "Select the set's objects that still exist.", "view-selection-sets",
          "map.selection_set.select <set>", COMMAND_FLAG_NONE, ViewSelectionSet<set_view_t::SELECT>, SelectionSetState, pWorkspace },
        { "map.selection_set.hide", "Hide Selection Set", "Hide the set's objects in every view (Show All brings them back).", "hide-selected",
          "map.selection_set.hide <set>", COMMAND_FLAG_NONE, ViewSelectionSet<set_view_t::HIDE>, SelectionSetState, pWorkspace },
        { "map.selection_set.show", "Show Selection Set", "Show the set's hidden objects again.", "show-all",
          "map.selection_set.show <set>", COMMAND_FLAG_NONE, ViewSelectionSet<set_view_t::SHOW>, SelectionSetState, pWorkspace },
        { "map.brush.make_room", "Make Room", "Hollow the selected brushes with the walls outside, at the hollow thickness (or the given one), so the room inside keeps the brushes' size.", "csg-hollow",
          "map.brush.make_room [thickness]", COMMAND_FLAG_NONE, MakeRoom, MakeRoomState, pWorkspace },
        { "map.patch.invert", "Invert Patch", "Turn the selected patches inside out: the other side faces out.", "mesh-flip-normals", nullptr,
          COMMAND_FLAG_NONE, PatchOp<map_patch_op_t::INVERT>, PatchState<map_patch_op_t::INVERT>, pWorkspace },
        { "map.texture.scale_lock", "Texture Scale Lock", "Textures scale with brushes when they are resized or scaled.", "texture-scale-lock", nullptr,
          COMMAND_FLAG_CHECKABLE, ScaleLockToggle, ScaleLockState, pWorkspace },
    };
    const auto status = EditorCommands_Register( pRegistry, commands, std::size( commands ) );
    if ( status != command_registry_status_t::OK ) { return status; }
    InstallBrushAdapters( pWorkspace, pRegistry );
    // The parity review's object and layer commands.
    const auto objects = MapObjectTools_Register( pWorkspace, pRegistry );
    const auto layers = objects == command_registry_status_t::OK ? MapLayers_Register( pWorkspace, pRegistry ) : objects;
    const auto instances = layers == command_registry_status_t::OK ? MapInstances_Register( pWorkspace, pRegistry ) : layers;
    return instances == command_registry_status_t::OK ? MapHierarchy_Register( pWorkspace, pRegistry ) : instances;
}

/*
================
Apply Current Material (Shift+T)
================
*/

namespace
{
// What Shift+T paints: the picked brush face or mesh faces, else every face
// of the selected brushes, meshes and patches (Hammer applies to whole
// objects outside Faces mode).
std::vector<map_surface_ref_t> MaterialTargets( const map_workspace_t *ws )
{
    std::vector<map_surface_ref_t> out;
    if ( !Ready( ws ) ) { return out; }
    if ( MapWorkspace_HasBrushFace( ws ) ) { out.push_back( { ws->selectedBrushFaceObject, ws->selectedBrushFaceSide } ); return out; }
    if ( MapWorkspace_HasMeshFace( ws ) ) {
        const auto faces = MapWorkspace_MeshFaces( ws );
        for ( usize i = 0; i < faces.nCount; ++i ) { out.push_back( { ws->selectedMeshFaceObject, faces.pData[i] } ); }
        return out;
    }
    if ( ws->elementMode == map_element_mode_t::FACES || !ObjectMode( ws ) ) { return out; }
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] );
        if ( object != nullptr && MapWorkspace_IsVisible( ws, *object ) &&
             ( object->kind == map_wire_kind_t::BRUSH || object->kind == map_wire_kind_t::MESH || object->kind == map_wire_kind_t::PATCH ) ) { out.push_back( { object->id, 0 } ); }
    }
    return out;
}
} // namespace

bool MapGeometryCommands_CanApplyMaterial( const map_workspace_t *pWorkspace ) noexcept
{
    return !MaterialTargets( pWorkspace ).empty();
}

bool MapGeometryCommands_ApplyMaterial( map_workspace_t *pWorkspace ) noexcept
{
    const auto targets = MaterialTargets( pWorkspace );
    if ( targets.empty() ) {
        MapNotice_Post( pWorkspace, map_notice_t::WARNING, QStringLiteral( "Apply Material: select faces (Faces mode) or objects first" ) );
        return false;
    }
    const auto material = EditorSettings_Text( &pWorkspace->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
    const QString name = QString::fromUtf8( material.pData, static_cast<qsizetype>( material.cchLength ) );
    usize changed = 0;
    map_status_t status = map_status_t::OK;
    const bool done = ApplyKeeping( pWorkspace, "Apply Material", [&]( map_document_t *copy ) noexcept {
        status = MapTextureEdit_SetMaterial( copy, { targets.data(), targets.size() }, material, &changed );
        return status;
    } );
    if ( done ) {
        MapNotice_Post( pWorkspace, map_notice_t::SUCCESS, QStringLiteral( "Applied \"%1\" to %2 %3" ).arg( name ).arg( changed ).arg( changed == 1 ? QStringLiteral( "face" ) : QStringLiteral( "faces" ) ) );
        return true;
    }
    // Already so is done, not a failure: the generic "did not complete" warning must not follow.
    if ( status == map_status_t::OK && changed == 0 ) { MapNotice_Post( pWorkspace, map_notice_t::INFO, QStringLiteral( "Every selected face already uses \"%1\"" ).arg( name ) ); return true; }
    MapNotice_Post( pWorkspace, map_notice_t::ERROR, QStringLiteral( "Apply Material failed (%1); nothing changed" ).arg( QString::fromUtf8( MapDocument_StatusName( status ) ) ) );
    return false;
}

std::vector<map_surface_ref_t> MapGeometryCommands_TextureTargets( const map_workspace_t *pWorkspace )
{
    auto targets = MaterialTargets( pWorkspace );
    targets.erase( std::remove_if( targets.begin(), targets.end(), [&]( const map_surface_ref_t &t ) {
        return geometry::GeometryDocument_FindPatch( &pWorkspace->pDocument->geometry, { t.object } ) != nullptr;
    } ), targets.end() );
    return targets;
}

bool MapGeometryCommands_FitTexture( map_workspace_t *pWorkspace, const map_uv_rect_t &rect, map_uv_fit_mode_t mode ) noexcept
{
    const auto targets = MapGeometryCommands_TextureTargets( pWorkspace );
    if ( targets.empty() ) {
        MapNotice_Post( pWorkspace, map_notice_t::WARNING, QStringLiteral( "Fast Texture: pick faces (Faces mode) or select brushes or meshes first; the window stays open while you pick" ) );
        return false;
    }
    map_status_t status = map_status_t::OK;
    const bool done = ApplyKeeping( pWorkspace, "Fast Texture", [&]( map_document_t *copy ) noexcept {
        status = MapTextureEdit_FitToRect( copy, { targets.data(), targets.size() }, rect, mode );
        return status;
    } );
    if ( done ) {
        MapNotice_Post( pWorkspace, map_notice_t::SUCCESS, QStringLiteral( "Fast Texture: %1 fitted to U %2-%3, V %4-%5" )
            .arg( targets.size() == 1 && targets[0].face != 0 ? QStringLiteral( "1 face" ) : QStringLiteral( "%1 targets" ).arg( targets.size() ) )
            .arg( rect.minimum.x, 0, 'g', 4 ).arg( rect.maximum.x, 0, 'g', 4 ).arg( rect.minimum.y, 0, 'g', 4 ).arg( rect.maximum.y, 0, 'g', 4 ) );
    } else if ( status != map_status_t::OK ) {
        MapNotice_Post( pWorkspace, map_notice_t::ERROR, QStringLiteral( "Fast Texture failed (%1): a face may have no area to fit" ).arg( QString::fromUtf8( MapDocument_StatusName( status ) ) ) );
    } else {
        MapNotice_Post( pWorkspace, map_notice_t::INFO, QStringLiteral( "Fast Texture: the faces already sit in that rectangle" ) );
    }
    return done;
}

bool MapGeometryCommands_ApplyHotspots( map_workspace_t *pWorkspace ) noexcept
{
    const auto targets = MapGeometryCommands_TextureTargets( pWorkspace );
    if ( targets.empty() ) {
        MapNotice_Post( pWorkspace, map_notice_t::ERROR, QStringLiteral( "Apply Hotspots: pick faces (Faces mode) or select brushes or meshes first" ) );
        return false;
    }
    // Each face, by the material it wears: every material has its own regions.
    std::vector<std::pair<QString, std::vector<map_surface_ref_t>>> byMaterial;
    const auto &wire = pWorkspace->wire;
    for ( usize f = 0; f < wire.faces.nCount; ++f ) {
        const auto &face = wire.faces.pData[f];
        const u64 id = face.sideId != 0 ? face.sideId : face.faceId;
        if ( id == 0 ) { continue; } // A modifier's copy: no face of its own.
        const bool wanted = std::any_of( targets.begin(), targets.end(), [&]( const map_surface_ref_t &t ) { return t.object == face.id && ( t.face == 0 || t.face == id ); } );
        if ( !wanted ) { continue; }
        const string_view_t text = MapMaterials_Path( &pWorkspace->pDocument->materials, face.material );
        const QString material = QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) );
        auto it = std::find_if( byMaterial.begin(), byMaterial.end(), [&]( const auto &entry ) { return entry.first == material; } );
        if ( it == byMaterial.end() ) { byMaterial.push_back( { material, {} } ); it = byMaterial.end() - 1; }
        const map_surface_ref_t ref{ face.id, id };
        if ( std::none_of( it->second.begin(), it->second.end(), [&]( const map_surface_ref_t &r ) { return r.object == ref.object && r.face == ref.face; } ) ) { it->second.push_back( ref ); }
    }
    std::vector<std::pair<std::vector<geometry::mesh_hotspot_rect_t>, std::vector<map_surface_ref_t>>> jobs;
    QString problem;
    usize faces = 0;
    for ( auto &[material, refs] : byMaterial ) {
        QString why;
        auto rects = MapHotspots_Load( pWorkspace, material, &why );
        if ( rects.empty() ) { if ( problem.isEmpty() ) { problem = why; } continue; }
        faces += refs.size();
        jobs.push_back( { std::move( rects ), std::move( refs ) } );
    }
    if ( jobs.empty() ) {
        MapNotice_Post( pWorkspace, map_notice_t::ERROR, QStringLiteral( "Apply Hotspots: %1. Mark the regions in Fast Texture (Add Hotspot)" ).arg( problem ) );
        return false;
    }
    map_status_t status = map_status_t::OK;
    const bool done = ApplyKeeping( pWorkspace, "Apply Hotspots", [&]( map_document_t *copy ) noexcept {
        for ( const auto &[rects, refs] : jobs ) {
            status = MapTextureEdit_ApplyHotspots( copy, { refs.data(), refs.size() }, rects );
            if ( status != map_status_t::OK ) { return status; }
        }
        return status;
    } );
    if ( done ) {
        MapNotice_Post( pWorkspace, problem.isEmpty() ? map_notice_t::SUCCESS : map_notice_t::WARNING,
            QStringLiteral( "Hotspots applied to %1 %2" ).arg( faces ).arg( faces == 1 ? QStringLiteral( "face" ) : QStringLiteral( "faces" ) ) +
            ( problem.isEmpty() ? QString() : QStringLiteral( "; skipped: %1" ).arg( problem ) ) );
    } else if ( status != map_status_t::OK ) {
        MapNotice_Post( pWorkspace, map_notice_t::ERROR, QStringLiteral( "Apply Hotspots failed (%1)" ).arg( QString::fromUtf8( MapDocument_StatusName( status ) ) ) );
    } else {
        MapNotice_Post( pWorkspace, map_notice_t::INFO, QStringLiteral( "Apply Hotspots: the faces already sit in their regions" ) );
    }
    return done;
}

} // namespace cypher::editor::map

