//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Workspace.cpp
//  Purpose: Implements the Map workspace session and its `map.*` commands.
//  Details: Opening builds the new document completely before the old one
//           is released, so a map that fails to open never costs the user
//           the map they had.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Workspace.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshSourceTopology.h"

#include <QFileInfo>
#include <QDir>

#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdio>
#include <new>
#include <utility>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

constexpr usize kProblemsLogged = 50u; // A damaged map can report thousands; the rest are summarised.

// ---------------------------------------------------------------------------
// Settings (CYSETTINGS.md 4.6)
// ---------------------------------------------------------------------------

constexpr const char *kMapPage = "Map Editing";
constexpr const char *kBrushShapes[]{ "box", "wedge", "cylinder", "spike", "sphere", "quad" };
constexpr const char *kPrimitiveAxes[]{ "X", "Y", "Z" };
constexpr const char *kClipModes[]{ "both", "front", "back" };
constexpr const char *kClipAxes[]{ "x", "y", "z" };
constexpr const char *kCarveModes[]{ "slice", "precise" };
constexpr const char *kResizeModes[]{ "Each object", "Selection bounds" };
constexpr const char *kPasteOffsets[]{ "none", "grid", "cursor" };

constexpr setting_descriptor_t MapSetting( const char *pPath, setting_type_t type, const char *pLabel, const char *pDescription ) noexcept
{
    setting_descriptor_t d{};
    d.pPath = pPath;
    d.type = type;
    d.pLabel = pLabel;
    d.pDescription = pDescription;
    d.pPage = kMapPage;
    return d;
}

constexpr setting_descriptor_t MapFlag( const char *pPath, bool_t bDefault, const char *pLabel, const char *pDescription ) noexcept
{
    setting_descriptor_t d = MapSetting( pPath, setting_type_t::BOOL, pLabel, pDescription );
    d.bDefault = bDefault;
    return d;
}

constexpr setting_descriptor_t MapInteger( const char *pPath, i64 nDefault, i64 nMin, i64 nMax, const char *pLabel, const char *pDescription ) noexcept
{
    setting_descriptor_t d = MapSetting( pPath, setting_type_t::INTEGER, pLabel, pDescription );
    d.nDefault = nDefault;
    d.nMin = nMin;
    d.nMax = nMax;
    return d;
}

constexpr setting_descriptor_t MapReal( const char *pPath, f64 flDefault, f64 flMin, f64 flMax, const char *pLabel, const char *pDescription ) noexcept
{
    setting_descriptor_t d = MapSetting( pPath, setting_type_t::REAL, pLabel, pDescription );
    d.flDefault = flDefault;
    d.flMin = flMin;
    d.flMax = flMax;
    return d;
}

constexpr setting_descriptor_t MapText( const char *pPath, const char *pDefault, usize cbMax, const char *pLabel, const char *pDescription ) noexcept
{
    setting_descriptor_t d = MapSetting( pPath, setting_type_t::STRING, pLabel, pDescription );
    d.pDefaultText = pDefault;
    d.cbMaxText = cbMax;
    return d;
}

template <usize nValues>
constexpr setting_descriptor_t MapChoice( const char *pPath, const char *const ( &ppValues )[nValues], const char *pDefault, const char *pLabel,
                                          const char *pDescription ) noexcept
{
    setting_descriptor_t d = MapSetting( pPath, setting_type_t::ENUM, pLabel, pDescription );
    d.ppEnumValues = ppValues;
    d.nEnumValues = nValues;
    d.pDefaultText = pDefault;
    return d;
}

constexpr setting_descriptor_t kMapSettings[]{
    MapText( "editor.map.default_material", "materials/dev/dev_grid.cymat", 259u, "Default material", "Material of new brushes (a .cymat path)." ),
    MapFlag( "editor.map.texture_lock", CY_TRUE, "Texture lock", "Textures move with brushes." ),
    MapFlag( "editor.map.texture_scale_lock", CY_FALSE, "Texture scale lock", "Textures scale with brushes." ),
    MapReal( "editor.map.block_depth", 64.0, 1.0, 65536.0, "Block depth", "Thickness along the axis outside a drawn box footprint." ),
    MapReal( "editor.map.default_texture_scale", 0.25, 0.001, 64.0, "Default texture scale", "UV repeat size for new faces: 0.25 is 128 world units per repeat." ),
    MapChoice( "editor.map.new_brush_shape", kBrushShapes, "box", "New brush shape", "Shape the block tool draws." ),
    MapInteger( "editor.map.cylinder_sides", 16, 3, 128, "Cylinder sides", "Sides of new cylinders and spikes." ),
    MapChoice( "editor.map.primitive_axis", kPrimitiveAxes, "Z", "Primitive axis", "Cylinder or spike length, wedge slope, or numeric Quad normal direction." ),
    MapInteger( "editor.map.sphere_subdivisions", 1, 0, 1, "Sphere detail", "Subdivision level: 0 is 20 faces; 1 is 80 faces." ),
    MapReal( "editor.map.cone_top_radius", 0.0, 0.0, 1.0, "Spike top radius", "Top-to-base radius ratio: 0 is a point; 1 is a cylinder." ),
    MapChoice( "editor.map.clip_mode", kClipModes, "both", "Retain", "Back keeps n · p + d ≤ 0; Front keeps n · p + d ≥ 0; Both retains both halves." ),
    MapChoice( "editor.map.clip_axis", kClipAxes, "z", "3D plane axis", "In a 3D view, draw on a Clip construction plane through the selection center, perpendicular to this axis. The cutting line defines a Clip plane extruded along this axis. Changing the axis discards a staged plane." ),
    MapChoice( "editor.map.carve_mode", kCarveModes, "slice", "Carve mode", "How Carve splits brushes." ),
    MapReal( "editor.map.hollow_thickness", 16.0, 1.0, 1024.0, "Hollow thickness", "Wall thickness for Hollow (units)." ),
    MapReal( "editor.map.mesh_extrude_distance", 16.0, 0.001, 1000000.0, "Extrude distance", "Extend the selected mesh face along its normal, retaining it as the cap and creating side walls. Positive units." ),
    MapReal( "editor.map.mesh_inset_distance", 8.0, 0.001, 1000000.0, "Corner inset distance", "Move every corner toward the face center by this distance. This radial inset does not promise a uniform border width. The inner face keeps its identity." ),
    MapInteger( "editor.map.mesh_slice_u", 2, 1, 64, "Quad Slice U cells", "Cells along the selected quad's corner 0 to corner 1 direction. Quad Slice inserts shared boundary vertices in neighboring faces." ),
    MapInteger( "editor.map.mesh_slice_v", 2, 1, 64, "Quad Slice V cells", "Cells along the selected quad's corner 0 to corner 3 direction. A 1 by 1 grid makes no change." ),
    MapFlag( "editor.map.select_created", CY_TRUE, "Select created objects", "Select newly created objects." ),
    MapChoice( "editor.map.resize_mode", kResizeModes, "Each object", "Multiple-object resize", "Each extends every selected brush, mesh, or patch by the same grid-snapped distance about its own opposite side. Group scales the combined selection bounds. This choice is captured when grabbing a bounds handle; the Scale tool always scales the group." ),
    MapFlag( "editor.map.resize_from_center", CY_FALSE, "Resize from center", "Bounds handles widen both sides around the captured center. Otherwise the opposite side stays fixed. Hold Shift when grabbing a resize handle to use the center for that gesture. The dragged boundary snaps; the opposite boundary mirrors it around the exact center." ),
    MapChoice( "editor.map.paste_offset", kPasteOffsets, "grid", "Paste placement", "None keeps original coordinates. Grid shifts by one authored grid step on the last hovered plane (XY if unset). Cursor centers the copied bounds on that plane and snaps its center when grid snap is enabled; the omitted axis is retained. Paste In Place always keeps original coordinates." ),
    MapText( "editor.map.default_point_class", "info_player_start", 64u, "Default point class", "Entity class the entity tool places." ),
    MapText( "editor.map.default_solid_class", "func_detail", 64u, "Default solid class", "Entity class Tie to Entity uses." ),
    MapFlag( "editor.map.write_all_properties", CY_TRUE, "Write all properties", "New entities store every class property (CYMAP.md 4)." ),
    MapFlag( "editor.map.check_before_run", CY_TRUE, "Check before run", "Run Check for Problems before running a map." ),
    MapReal( "editor.map.cell_size", 8192.0, 0.0, 262144.0, "Chunk cell size", "Chunk cell size for new maps (units)." ),
    MapText( "editor.map.visgroup_presets", "", 2048u, "Visibility presets",
             "Saved Auto Vis Groups visibility (Hammer's Presets), as name=hidden-group-mask pairs separated by ';'." ),
};

// Reads the grid, snapping, and lock settings into the session; true when
// anything changed.
bool_t ReadGridSettings( map_workspace_t *pWorkspace ) noexcept
{
    const settings_registry_t *pSettings = &pWorkspace->pGui->settings;
    const f64 size = std::clamp( static_cast<f64>( EditorSettings_Integer( pSettings, "editor.grid.size", static_cast<i64>( MAP_GRID_DEFAULT ) ) ),
                                 MAP_GRID_MIN, MAP_GRID_MAX );
    const bool_t bVisible = EditorSettings_Bool( pSettings, "editor.grid.show", CY_TRUE );
    const bool_t bSnap = EditorSettings_Bool( pSettings, "editor.grid.snap", CY_TRUE );
    const bool_t bLock = EditorSettings_Bool( pSettings, "editor.map.texture_lock", CY_TRUE );
    const f64 angle = EditorSettings_Real( pSettings, "editor.grid.angle_snap", 15.0 );
    const f64 scale = EditorSettings_Real( pSettings, "editor.grid.scale_snap", 0.25 );
    const bool_t bChanged = size != pWorkspace->gridSize || bVisible != pWorkspace->bGridVisible || bSnap != pWorkspace->bSnapToGrid ||
                            bLock != pWorkspace->bTextureLock || angle != pWorkspace->angleSnap || scale != pWorkspace->scaleSnap;
    pWorkspace->gridSize = size;
    pWorkspace->bGridVisible = bVisible;
    pWorkspace->bSnapToGrid = bSnap;
    pWorkspace->bTextureLock = bLock;
    pWorkspace->angleSnap = angle;
    pWorkspace->scaleSnap = scale;
    if ( angle > 0.0 ) { pWorkspace->lastAngleSnap = angle; }
    if ( scale > 0.0 ) { pWorkspace->lastScaleSnap = scale; }
    return bChanged;
}

void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    bool_t bRelevant = path.cchLength == 0u;
    for ( const char *pPath : { "editor.grid.size", "editor.grid.show", "editor.grid.snap", "editor.grid.angle_snap", "editor.grid.scale_snap", "editor.map.texture_lock" } ) {
        bRelevant = bRelevant || StringView_Equals( path, StringView_FromCString( pPath ) );
    }
    const bool_t bGridChanged = bRelevant && ReadGridSettings( pWorkspace );
    const bool_t bClipChanged = path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.clip_mode" ) );
    const bool_t bClipAxisChanged = path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.clip_axis" ) );
    if ( bClipAxisChanged && pWorkspace->editPreview.bActive && pWorkspace->editPreview.bClip ) {
        // An existing line keeps its original meaning; changing the 3D
        // construction axis requires a new Clip gesture.
        MapWorkspace_ClearEditPreview( pWorkspace );
    } else if ( bClipChanged && pWorkspace->editPreview.bActive && pWorkspace->editPreview.bClip ) {
        // A retained-half change updates the staged result, never its plane
        // or the live document. The preview setter publishes the view notice.
        MapWorkspace_SetClipPreview( pWorkspace, pWorkspace->editPreview.clipPlane );
    } else if ( bGridChanged || bClipChanged || bClipAxisChanged ) { MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW ); }
}

// Writes a grid setting to the narrowest attached scope; false when no scope is
// attached (tests, a headless editor), in which case the session keeps the
// value itself.
bool_t WriteGridSetting( map_workspace_t *pWorkspace, const char *pPath, const setting_value_t &value ) noexcept
{
    settings_registry_t *pSettings = &pWorkspace->pGui->settings;
    const setting_descriptor_t *pDescriptor = EditorSettings_Find( pSettings, StringView_FromCString( pPath ) );
    if ( pDescriptor == nullptr ) { return CY_FALSE; }
    for ( usize i = 0u; i < static_cast<usize>( settings_scope_t::DEFAULT ); ++i ) {
        if ( pSettings->scopes[i] != nullptr ) {
            return EditorSettings_Write( pSettings, static_cast<settings_scope_t>( i ), *pDescriptor, value ) == settings_registry_status_t::OK;
        }
    }
    return CY_FALSE;
}

struct camera_speed_write_t {
    const setting_descriptor_t *pDescriptor{};
    settings_document_t *pStore{};
    settings_scope_t scope{ settings_scope_t::DEFAULT };
    setting_value_t value{};
};

bool_t PlanCameraSpeed( const map_workspace_t *pWorkspace, map_camera_speed_action_t action, camera_speed_write_t &write ) noexcept
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr ) { return CY_FALSE; }
    const auto *pSettings = &pWorkspace->pGui->settings;
    const auto *pDescriptor = EditorSettings_Find( pSettings, StringView_FromCString( "editor.camera.move_speed" ) );
    if ( pDescriptor == nullptr || pDescriptor->type != setting_type_t::REAL || !std::isfinite( pDescriptor->flMin ) ||
         !std::isfinite( pDescriptor->flMax ) || pDescriptor->flMin <= 0.0 || pDescriptor->flMax < pDescriptor->flMin ||
         Setting_Check( *pDescriptor, Setting_Default( *pDescriptor ) ) != setting_problem_code_t::NONE ) { return CY_FALSE; }
    const auto current = EditorSettings_Resolve( pSettings, *pDescriptor ).value;
    if ( Setting_Check( *pDescriptor, current ) != setting_problem_code_t::NONE ) { return CY_FALSE; }
    f64 target{};
    switch ( action ) {
        case map_camera_speed_action_t::INCREASE:
            target = current.flValue >= pDescriptor->flMax * 0.5 ? pDescriptor->flMax : current.flValue * 2.0;
            break;
        case map_camera_speed_action_t::DECREASE: target = current.flValue * 0.5; break;
        case map_camera_speed_action_t::RESET: target = pDescriptor->flDefault; break;
        default: return CY_FALSE;
    }
    target = std::clamp( target, pDescriptor->flMin, pDescriptor->flMax );
    if ( !std::isfinite( target ) || target == current.flValue ) { return CY_FALSE; }
    for ( usize i = 0u; i < static_cast<usize>( settings_scope_t::DEFAULT ); ++i ) {
        if ( pSettings->scopes[i] != nullptr ) {
            write.pStore = pSettings->scopes[i]; write.scope = static_cast<settings_scope_t>( i ); break;
        }
    }
    if ( !SettingsDocument_IsInitialized( write.pStore ) ) { return CY_FALSE; }
    settings_path_t path{};
    if ( !SettingsPath_Parse( StringView_FromCString( pDescriptor->pPath ), &path ) ) { return CY_FALSE; }
    const auto *pParent = SettingsDocument_Root( write.pStore );
    if ( KeyValue_Type( pParent ) != key_value_type_t::OBJECT ) { return CY_FALSE; }
    for ( usize i = 0u; i + 1u < path.nSegments; ++i ) {
        pParent = KeyValue_Find( pParent, path.segments[i] );
        if ( pParent == nullptr ) { break; }
        if ( KeyValue_Type( pParent ) != key_value_type_t::OBJECT ) { return CY_FALSE; }
    }
    write.pDescriptor = pDescriptor; write.value.type = setting_type_t::REAL; write.value.flValue = target;
    return CY_TRUE;
}

bool_t PrepareCameraSpeed( const camera_speed_write_t &write, const settings_registry_t &settings, settings_document_t &candidate ) noexcept
{
    if ( SettingsDocument_Init( &candidate, write.pStore->pAllocator, write.pStore->identity ) != settings_document_status_t::OK ||
         !KeyValue_SetDocumentHeader( candidate.pDocument, KeyValue_DocumentHeader( write.pStore->pDocument ) ) ) { return CY_FALSE; }
    candidate.nLoadedVersion = write.pStore->nLoadedVersion;
    auto *pRoot = KeyValue_Root( candidate.pDocument );
    for ( const auto *pChild = KeyValue_FirstChild( SettingsDocument_Root( write.pStore ) ); pChild != nullptr; pChild = KeyValue_NextSibling( pChild ) ) {
        if ( KeyValue_CloneInto( candidate.pDocument, pRoot, pChild ) == nullptr ) { return CY_FALSE; }
    }
    return Setting_WriteOverride( &candidate, *write.pDescriptor, write.value,
                                 EditorSettings_ResolveInherited( &settings, *write.pDescriptor, write.scope ).value ) == settings_document_status_t::OK;
}

struct tool_info_t {
    const char *pName;
    bool_t bAvailable;
};

constexpr tool_info_t kTools[]{
    { "Select", CY_TRUE },     { "Camera", CY_TRUE },   { "Entity", CY_FALSE }, { "Block", CY_TRUE },   { "Texture", CY_TRUE },
    { "Decal", CY_FALSE },     { "Overlay", CY_FALSE }, { "Clip", CY_TRUE },   { "Vertex", CY_FALSE },  { "Path", CY_FALSE },
    { "Measure", CY_FALSE },   { "Terrain", CY_FALSE }, { "Patch", CY_FALSE },  { "Translate", CY_TRUE }, { "Rotate", CY_TRUE },
    { "Scale", CY_TRUE },     { "Pivot", CY_FALSE },   { "Polygon", CY_FALSE }, { "Mirror", CY_FALSE }, { "Paint", CY_FALSE },
    { "Extrude", CY_TRUE },   { "Knife", CY_FALSE },   { "Loop Cut", CY_FALSE }, { "Eyedropper", CY_FALSE }, { "Workplane", CY_FALSE },
    { "Curve", CY_FALSE }, { "Navigation", CY_TRUE },
};
static_assert( std::size( kTools ) == static_cast<usize>( map_tool_t::COUNT ), "Tool table and enum diverged" );

// Modes are selectable independently of their operation readiness. Vertex
// and edge profiles disclose unavailable picking; Meshes selects real roots.
constexpr tool_info_t kElementModes[]{
    { "Vertices", CY_TRUE }, { "Edges", CY_TRUE }, { "Faces", CY_TRUE }, { "Meshes", CY_TRUE },
    { "Objects", CY_TRUE },   { "Groups", CY_TRUE }, { "Navigation", CY_FALSE },
};
static_assert( std::size( kElementModes ) == static_cast<usize>( map_element_mode_t::COUNT ), "Mode table and enum diverged" );

void LogText( log_level_t level, const QString &text ) noexcept
{
    const QByteArray utf8 = text.toUtf8();
    Cy_LogWriteAt( level, log_channel_t::Editor, utf8.constData(), CY_SOURCE_LOCATION );
}

CYPHER_NODISCARD map_document_t *CreateDocument( const allocator_t *pAllocator ) noexcept
{
    auto *pDocument = new ( std::nothrow ) map_document_t{};
    if ( pDocument == nullptr ) { return nullptr; }
    map_create_desc_t desc{};
    desc.name = StringView_FromCString( "Untitled" );
    desc.game = StringView_FromCString( MAP_WORKSPACE_DEFAULT_GAME );
    if ( MapDocument_Create( pDocument, pAllocator, desc ) != map_status_t::OK ) {
        delete pDocument;
        return nullptr;
    }
    return pDocument;
}

QString PersistenceKey( const QString &path )
{
    const QFileInfo file( path );
    QDir parent = file.absoluteDir();
    QString relative = file.fileName();
    QString canonicalParent = parent.canonicalPath();
    // New Save As folders may not exist until staging creates them. Resolve
    // the nearest existing ancestor so the inventory key stays stable then.
    while ( canonicalParent.isEmpty() && !parent.isRoot() ) {
        relative = parent.dirName() + QLatin1Char( '/' ) + relative;
        const QString ancestor = QDir::cleanPath( parent.absoluteFilePath( QStringLiteral( ".." ) ) );
        if ( ancestor == parent.absolutePath() ) { break; }
        parent = QDir( ancestor );
        canonicalParent = parent.canonicalPath();
    }
    // Resolve directory aliases, while retaining the leaf name: replacing a
    // symlink at the root file itself writes that leaf, not its previous target.
    return QDir::cleanPath( canonicalParent.isEmpty() ? file.absoluteFilePath()
                                                     : QDir( canonicalParent ).filePath( relative ) );
}

QStringList PersistedChunkPaths( const map_document_t &document )
{
    QStringList paths;
    for ( usize i = 0u; i < document.chunks.nCount; ++i ) {
        const map_chunk_t &chunk = *document.chunks.pData[i];
        if ( chunk.bDamaged || chunk.sourcePath[0] == '\0' ) { continue; }
        const QString path = QString::fromUtf8( chunk.sourcePath );
        if ( !paths.contains( path ) ) { paths.append( path ); }
    }
    return paths;
}

bool ClearMeshComponents( map_workspace_t *workspace ) noexcept
{
    const bool changed = workspace->meshSelection.meshId.value != 0 || workspace->selectedMeshEdgeSeed.a.value != 0;
    geometry::MeshSelection_Shutdown( &workspace->meshSelection );
    workspace->selectedMeshEdgeSeed = {};
    return changed;
}

// Visibility and authored topology can change independently of root IDs.
// Resolve persistent IDs without allocation before publishing any refresh.
bool PruneMeshComponents( map_workspace_t *workspace ) noexcept
{
    auto &selection = workspace->meshSelection;
    if ( selection.meshId.value == 0 ) { return false; }
    const auto *wire = MapWireframe_FindObject( workspace->wire, selection.meshId.value );
    const auto *mesh = workspace->pDocument != nullptr ?
        geometry::GeometryDocument_FindMesh( &workspace->pDocument->geometry, selection.meshId ) : nullptr;
    const bool vertices = workspace->elementMode == map_element_mode_t::VERTICES;
    const bool edges = workspace->elementMode == map_element_mode_t::EDGES;
    if ( ( !vertices && !edges ) || mesh == nullptr || wire == nullptr || wire->kind != map_wire_kind_t::MESH ||
         !MapWorkspace_IsVisible( workspace, *wire ) || workspace->selection.ids.nCount != 1 ||
         workspace->selection.ids.pData[0] != selection.meshId.value || selection.faces.nCount != 0 ||
         ( vertices && ( selection.edges.nCount != 0 || workspace->selectedMeshEdgeSeed.a.value != 0 ) ) ||
         ( edges && selection.vertices.nCount != 0 ) ) { return ClearMeshComponents( workspace ); }
    if ( vertices ) {
        usize kept = 0;
        for ( usize i = 0; i < selection.vertices.nCount; ++i ) {
            const auto vertex = selection.vertices.pData[i];
            geometry::geometry_mesh_vertex_handle_t handle{};
            if ( geometry::MeshSource_TryFindVertex( mesh, vertex, &handle ) ) { selection.vertices.pData[kept++] = vertex; }
        }
        const bool changed = kept != selection.vertices.nCount;
        selection.vertices.nCount = kept;
        return changed;
    }
    usize kept = 0;
    for ( usize i = 0; i < selection.edges.nCount; ++i ) {
        const auto edge = selection.edges.pData[i];
        geometry::geometry_mesh_edge_handle_t handle{};
        if ( geometry::MeshSourceEdit_TryFindEdge( mesh, edge.a, edge.b, &handle ) ) { selection.edges.pData[kept++] = edge; }
    }
    const bool changed = kept != selection.edges.nCount;
    selection.edges.nCount = kept;
    if ( !geometry::MeshSelection_HasEdge( &selection, workspace->selectedMeshEdgeSeed ) ) {
        workspace->selectedMeshEdgeSeed = kept != 0 ? selection.edges.pData[0] : geometry::mesh_edge_ref_t{};
    }
    return changed;
}

// Takes ownership of pDocument and makes it the open map.
void Adopt( map_workspace_t *pWorkspace, map_document_t *pDocument, const QString &path ) noexcept
{
    pWorkspace->editPreview = {};
    MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
    delete pWorkspace->pDocument;
    pWorkspace->pDocument = pDocument;
    pWorkspace->path = path;
    pWorkspace->persistedChunksByRoot.clear();
    if ( !path.isEmpty() ) { pWorkspace->persistedChunksByRoot.insert( PersistenceKey( path ), PersistedChunkPaths( *pDocument ) ); }
    // Another document: nothing of the old history applies to it.
    EditorHistory_Clear( &pWorkspace->history );
    EditorHistory_MarkClean( &pWorkspace->history );
    pWorkspace->selectedBrushFaceObject = 0; pWorkspace->selectedBrushFaceSide = 0;
    pWorkspace->selectedMeshFaceObject = 0; pWorkspace->selectedMeshFaceId = 0;
    ( void )ClearMeshComponents( pWorkspace );
    ( void )EditorSelection_Clear( &pWorkspace->selection );
    ( void )EditorSelection_Clear( &pWorkspace->hidden ); // Hidden objects belong to the map that had them.
    if ( MapWireframe_Build( &pWorkspace->wire, *pDocument ) != map_status_t::OK ) {
        CY_LOG_WRITE( Error, Editor, "Map views could not be built; the map is open but not drawn" );
    }
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION | MAP_CHANGE_TITLE | MAP_CHANGE_HISTORY );
    MapWorkspace_Frame( pWorkspace, CY_FALSE );
}

void LogProblems( const map_document_t &map ) noexcept
{
    const usize nProblems = map.problems.nCount;
    for ( usize i = 0u; i < nProblems && i < kProblemsLogged; ++i ) {
        const map_problem_t &problem = map.problems.pData[i];
        QString line = QStringLiteral( "Map problem %1" ).arg( QString::fromUtf8( MapDocument_ProblemName( problem.code ) ) );
        if ( problem.path[0] != '\0' ) { line += QStringLiteral( " in %1" ).arg( QString::fromUtf8( problem.path ) ); }
        if ( problem.member[0] != '\0' ) { line += QStringLiteral( " (%1)" ).arg( QString::fromUtf8( problem.member ) ); }
        if ( problem.id != 0u ) { line += QStringLiteral( ", object %1" ).arg( problem.id ); }
        LogText( log_level_t::Warning, line );
    }
    if ( nProblems > kProblemsLogged ) {
        LogText( log_level_t::Warning, QStringLiteral( "... and %1 more map problems" ).arg( nProblems - kProblemsLogged ) );
    }
}

// The modified mark and Undo/Redo availability follow the history.
void OnHistoryChanged( void *pContext ) noexcept
{
    MapWorkspace_Notify( static_cast<map_workspace_t *>( pContext ), MAP_CHANGE_TITLE | MAP_CHANGE_HISTORY );
}

bool_t ObjectExists( void *pContext, u64 id ) noexcept
{
    const auto *pWorkspace = static_cast<const map_workspace_t *>( pContext );
    return MapWireframe_FindObject( pWorkspace->wire, id ) != nullptr || MapWireframe_FindEntity( pWorkspace->wire, id ) != nullptr;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

template <map_tool_t kTool>
command_result_t ToolExecute( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    if ( !MapWorkspace_IsToolAvailable( kTool ) ) { return command_result_t::DISABLED; }
    if constexpr ( kTool == map_tool_t::CLIP ) {
        if ( pWorkspace->tool == map_tool_t::CLIP ) {
            const auto mode = MapWorkspace_ClipMode( pWorkspace );
            setting_value_t value{}; value.type = setting_type_t::ENUM;
            value.text = StringView_FromCString( mode == map_brush_clip_mode_t::BACK ? "front" :
                                                mode == map_brush_clip_mode_t::FRONT ? "both" : "back" );
            return WriteGridSetting( pWorkspace, "editor.map.clip_mode", value ) ? command_result_t::OK : command_result_t::FAILED;
        }
    }
    MapWorkspace_SetTool( pWorkspace, kTool );
    return command_result_t::OK;
}

template <map_tool_t kTool>
u32 ToolState( void *pContext ) noexcept
{
    const auto *pWorkspace = static_cast<const map_workspace_t *>( pContext );
    u32 state = MapWorkspace_IsToolAvailable( kTool ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
    if ( pWorkspace->tool == kTool ) { state |= COMMAND_STATE_CHECKED; }
    return state;
}

template <map_element_mode_t kMode>
command_result_t ElementModeExecute( void *pContext, const command_args_t & ) noexcept
{
    if ( !MapWorkspace_IsElementModeAvailable( kMode ) ) { return command_result_t::DISABLED; }
    auto *workspace = static_cast<map_workspace_t *>( pContext );
    MapWorkspace_SetElementMode( workspace, kMode );
    // Choosing a selection category is an explicit route back from neutral
    // navigation, without making unrelated mode notifications select a tool.
    if ( workspace->tool == map_tool_t::NONE ) { MapWorkspace_SetTool( workspace, map_tool_t::SELECT ); }
    return command_result_t::OK;
}

template <map_element_mode_t kMode>
u32 ElementModeState( void *pContext ) noexcept
{
    const auto *pWorkspace = static_cast<const map_workspace_t *>( pContext );
    u32 state = MapWorkspace_IsElementModeAvailable( kMode ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
    if ( pWorkspace->elementMode == kMode ) { state |= COMMAND_STATE_CHECKED; }
    return state;
}

u32 BrushEditState( void *pContext ) noexcept
{
    return MapWorkspace_CanEditBrushSelection( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
u32 BrushMergeState( void *pContext ) noexcept
{
    return MapWorkspace_CanMergeBrushSelection( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
u32 FaceEditState( void *pContext ) noexcept
{
    const auto *ws = static_cast<const map_workspace_t *>( pContext );
    return MapWorkspace_HasBrushFace( ws ) && !ws->pDocument->bReadOnly ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
u32 MeshFaceEditState( void *pContext ) noexcept
{
    return MapWorkspace_CanEditMeshFace( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
command_result_t ExtrudeMeshFace( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = static_cast<map_workspace_t *>( pContext );
    return MapWorkspace_ExtrudeMeshFace( ws, EditorSettings_Real( &ws->pGui->settings, "editor.map.mesh_extrude_distance", 16.0 ) ) ? command_result_t::OK : command_result_t::FAILED;
}
command_result_t InsetMeshFace( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = static_cast<map_workspace_t *>( pContext );
    return MapWorkspace_InsetMeshFace( ws, EditorSettings_Real( &ws->pGui->settings, "editor.map.mesh_inset_distance", 8.0 ) ) ? command_result_t::OK : command_result_t::FAILED;
}
u32 QuadSliceState( void *pContext ) noexcept
{
    const auto *ws = static_cast<const map_workspace_t *>( pContext );
    const auto u = EditorSettings_Integer( &ws->pGui->settings, "editor.map.mesh_slice_u", 2 );
    const auto v = EditorSettings_Integer( &ws->pGui->settings, "editor.map.mesh_slice_v", 2 );
    return u >= 1 && u <= 64 && v >= 1 && v <= 64 && MapWorkspace_CanQuadSliceMeshFace( ws, static_cast<u32>( u ), static_cast<u32>( v ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
command_result_t QuadSlice( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = static_cast<map_workspace_t *>( pContext );
    const auto u = EditorSettings_Integer( &ws->pGui->settings, "editor.map.mesh_slice_u", 2 );
    const auto v = EditorSettings_Integer( &ws->pGui->settings, "editor.map.mesh_slice_v", 2 );
    return u >= 1 && u <= 64 && v >= 1 && v <= 64 && MapWorkspace_QuadSliceMeshFace( ws, static_cast<u32>( u ), static_cast<u32>( v ) ) ? command_result_t::OK : command_result_t::FAILED;
}
command_result_t HollowSelection( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = static_cast<map_workspace_t *>( pContext );
    const f64 thickness = EditorSettings_Real( &ws->pGui->settings, "editor.map.hollow_thickness", 16.0 );
    return MapWorkspace_HollowSelection( ws, thickness ) ? command_result_t::OK : command_result_t::FAILED;
}
command_result_t MergeSelection( void *pContext, const command_args_t & ) noexcept
{
    return MapWorkspace_MergeSelection( static_cast<map_workspace_t *>( pContext ) ) ? command_result_t::OK : command_result_t::FAILED;
}
command_result_t ApplyFaceMaterial( void *pContext, const command_args_t & ) noexcept
{
    return MapWorkspace_ApplyFaceMaterial( static_cast<map_workspace_t *>( pContext ) ) ? command_result_t::OK : command_result_t::FAILED;
}
u32 BrushConversionState( void *pContext ) noexcept
{
    return MapWorkspace_CanConvertBrushSelection( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
u32 MeshEditState( void *pContext ) noexcept
{
    return MapWorkspace_CanEditMeshSelection( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
u32 MeshTriangulationState( void *pContext ) noexcept
{
    return MapWorkspace_CanTriangulateMeshSelection( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}
command_result_t ConvertBrushSelection( void *pContext, const command_args_t & ) noexcept
{
    return MapWorkspace_ConvertBrushSelection( static_cast<map_workspace_t *>( pContext ) ) ? command_result_t::OK : command_result_t::FAILED;
}
command_result_t FlipMeshNormals( void *pContext, const command_args_t & ) noexcept
{
    return MapWorkspace_FlipMeshNormals( static_cast<map_workspace_t *>( pContext ) ) ? command_result_t::OK : command_result_t::FAILED;
}
command_result_t TriangulateMeshSelection( void *pContext, const command_args_t & ) noexcept
{
    return MapWorkspace_TriangulateMeshSelection( static_cast<map_workspace_t *>( pContext ) ) ? command_result_t::OK : command_result_t::FAILED;
}

u32 EdgeTopologySelectionState( void *pContext ) noexcept
{
    return MapWorkspace_CanSelectMeshEdgeTopology( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

template <bool kLoop>
command_result_t SelectEdgeTopology( void *pContext, const command_args_t & ) noexcept
{
    auto *workspace = static_cast<map_workspace_t *>( pContext );
    if ( !MapWorkspace_CanSelectMeshEdgeTopology( workspace ) ) { return command_result_t::DISABLED; }
    const bool selected = kLoop ? MapWorkspace_SelectMeshEdgeLoop( workspace ) : MapWorkspace_SelectMeshEdgeRing( workspace );
    return selected ? command_result_t::OK : command_result_t::FAILED;
}

template <map_camera_speed_action_t kAction>
u32 CameraSpeedState( void *pContext ) noexcept
{
    return MapWorkspace_CanChangeCameraSpeed( static_cast<const map_workspace_t *>( pContext ), kAction ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

template <map_camera_speed_action_t kAction>
command_result_t CameraSpeedExecute( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    if ( !MapWorkspace_CanChangeCameraSpeed( pWorkspace, kAction ) ) { return command_result_t::DISABLED; }
    return MapWorkspace_ChangeCameraSpeed( pWorkspace, kAction ) ? command_result_t::OK : command_result_t::FAILED;
}

// Hammer 5's "View:" toggles: each shows or hides a family of visgroups.
constexpr u32 VisBit( map_visgroup_t group ) noexcept { return 1u << static_cast<u32>( group ); }
constexpr u32 kShowWorld = VisBit( map_visgroup_t::BRUSHES ) | VisBit( map_visgroup_t::MESHES ) | VisBit( map_visgroup_t::PATCHES );
constexpr u32 kShowEntities = VisBit( map_visgroup_t::INFO ) | VisBit( map_visgroup_t::OTHER_ENTITIES );
constexpr u32 kShowLights = VisBit( map_visgroup_t::LIGHTS );
constexpr u32 kShowTriggers = VisBit( map_visgroup_t::TRIGGERS );
constexpr u32 kShowProps = VisBit( map_visgroup_t::PROPS );
constexpr u32 kShowTerrain = VisBit( map_visgroup_t::TERRAIN );
constexpr u32 kShowTied = VisBit( map_visgroup_t::TIED );

template <u32 kMask>
command_result_t ShowToggle( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    // Any hidden member makes the toggle show the whole family again.
    const bool_t bHide = ( pWorkspace->hiddenVisgroups & kMask ) == 0u;
    for ( u32 g = 0u; g < static_cast<u32>( map_visgroup_t::COUNT ); ++g ) {
        if ( ( kMask & ( 1u << g ) ) != 0u ) { MapWorkspace_SetVisgroupHidden( pWorkspace, static_cast<map_visgroup_t>( g ), bHide ); }
    }
    return command_result_t::OK;
}

template <u32 kMask>
u32 ShowState( void *pContext ) noexcept
{
    return COMMAND_STATE_ENABLED | ( ( static_cast<const map_workspace_t *>( pContext )->hiddenVisgroups & kMask ) == 0u ? COMMAND_STATE_CHECKED : 0u );
}

// Hide and show (Hammer's H, Shift+H, U): a session set of hidden objects.
command_result_t HideSelected( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    for ( usize i = 0u; i < EditorSelection_Count( &pWorkspace->selection ); ++i ) {
        ( void )EditorSelection_Apply( &pWorkspace->hidden, EditorSelection_At( &pWorkspace->selection, i ), EDITOR_SELECT_ADD );
    }
    ( void )EditorSelection_Clear( &pWorkspace->selection );
    pWorkspace->selectedBrushFaceObject = 0;
    pWorkspace->selectedBrushFaceSide = 0;
    pWorkspace->selectedMeshFaceObject = 0;
    pWorkspace->selectedMeshFaceId = 0;
    ( void )ClearMeshComponents( pWorkspace );
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW );
    return command_result_t::OK;
}

command_result_t HideUnselected( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    for ( usize i = 0u; i < pWorkspace->wire.objects.nCount; ++i ) {
        const u64 id = pWorkspace->wire.objects.pData[i].id;
        if ( !EditorSelection_Contains( &pWorkspace->selection, id ) ) { ( void )EditorSelection_Apply( &pWorkspace->hidden, id, EDITOR_SELECT_ADD ); }
    }
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
    return command_result_t::OK;
}

command_result_t ShowAll( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    ( void )EditorSelection_Clear( &pWorkspace->hidden );
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
    return command_result_t::OK;
}

u32 HasHiddenState( void *pContext ) noexcept
{
    return EditorSelection_Count( &static_cast<const map_workspace_t *>( pContext )->hidden ) != 0u ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

command_result_t InvertSelection( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    if ( pWorkspace->elementMode != map_element_mode_t::OBJECTS && pWorkspace->elementMode != map_element_mode_t::GROUPS &&
         pWorkspace->elementMode != map_element_mode_t::MESHES ) { return command_result_t::DISABLED; }
    std::vector<u64> ids;
    for ( usize i = 0u; i < pWorkspace->wire.objects.nCount; ++i ) {
        const map_wire_object_t &object = pWorkspace->wire.objects.pData[i];
        if ( MapWorkspace_IsSelectableObject( pWorkspace, object ) && !EditorSelection_Contains( &pWorkspace->selection, object.id ) ) { ids.push_back( object.id ); }
    }
    MapWorkspace_SetSelection( pWorkspace, ids.data(), ids.size() );
    return command_result_t::OK;
}

u32 RootSelectionState( void *pContext ) noexcept
{
    const auto mode = static_cast<const map_workspace_t *>( pContext )->elementMode;
    return mode == map_element_mode_t::OBJECTS || mode == map_element_mode_t::GROUPS || mode == map_element_mode_t::MESHES ?
        COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

// Every visible entity of the selected entities' classes.
command_result_t SelectSameClass( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    std::vector<QByteArray> classes;
    for ( usize i = 0u; i < EditorSelection_Count( &pWorkspace->selection ); ++i ) {
        if ( const map_wire_entity_t *pEntity = MapWireframe_FindEntity( pWorkspace->wire, EditorSelection_At( &pWorkspace->selection, i ) ) ) {
            classes.emplace_back( pEntity->className );
        }
    }
    std::vector<u64> ids;
    for ( usize i = 0u; i < pWorkspace->wire.entities.nCount; ++i ) {
        const map_wire_entity_t &entity = pWorkspace->wire.entities.pData[i];
        if ( std::find( classes.begin(), classes.end(), QByteArray( entity.className ) ) == classes.end() ) { continue; }
        const map_wire_object_t *pObject = MapWireframe_FindObject( pWorkspace->wire, entity.id );
        if ( pObject == nullptr || MapWorkspace_IsVisible( pWorkspace, *pObject ) ) { ids.push_back( entity.id ); }
    }
    MapWorkspace_SetSelection( pWorkspace, ids.data(), ids.size() );
    return command_result_t::OK;
}

u32 HasEntitySelectedState( void *pContext ) noexcept
{
    const auto *pWorkspace = static_cast<const map_workspace_t *>( pContext );
    if ( pWorkspace->elementMode == map_element_mode_t::VERTICES || pWorkspace->elementMode == map_element_mode_t::EDGES ||
         pWorkspace->elementMode == map_element_mode_t::MESHES || pWorkspace->elementMode == map_element_mode_t::NAVIGATION ) { return COMMAND_STATE_NONE; }
    for ( usize i = 0u; i < EditorSelection_Count( &pWorkspace->selection ); ++i ) {
        if ( MapWireframe_FindEntity( pWorkspace->wire, EditorSelection_At( &pWorkspace->selection, i ) ) != nullptr ) { return COMMAND_STATE_ENABLED; }
    }
    return COMMAND_STATE_NONE;
}

command_result_t SnapToggle( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    MapWorkspace_SetSnapToGrid( pWorkspace, !pWorkspace->bSnapToGrid );
    return command_result_t::OK;
}

u32 SnapToggleState( void *pContext ) noexcept
{
    return COMMAND_STATE_ENABLED | ( static_cast<const map_workspace_t *>( pContext )->bSnapToGrid ? COMMAND_STATE_CHECKED : 0u );
}

command_result_t AngleSnapToggle( void *pContext, const command_args_t & ) noexcept
{
    auto *workspace = static_cast<map_workspace_t *>( pContext );
    MapWorkspace_SetAngleSnap( workspace, workspace->angleSnap > 0.0 ? 0.0 : workspace->lastAngleSnap );
    return command_result_t::OK;
}

u32 AngleSnapState( void *pContext ) noexcept
{
    return COMMAND_STATE_ENABLED | ( static_cast<const map_workspace_t *>( pContext )->angleSnap > 0.0 ? COMMAND_STATE_CHECKED : 0u );
}

command_result_t ScaleSnapToggle( void *pContext, const command_args_t & ) noexcept
{
    auto *workspace = static_cast<map_workspace_t *>( pContext );
    MapWorkspace_SetScaleSnap( workspace, workspace->scaleSnap > 0.0 ? 0.0 : workspace->lastScaleSnap );
    return command_result_t::OK;
}

u32 ScaleSnapState( void *pContext ) noexcept
{
    return COMMAND_STATE_ENABLED | ( static_cast<const map_workspace_t *>( pContext )->scaleSnap > 0.0 ? COMMAND_STATE_CHECKED : 0u );
}

command_result_t TextureLockToggle( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    MapWorkspace_SetTextureLock( pWorkspace, !pWorkspace->bTextureLock );
    return command_result_t::OK;
}

u32 TextureLockState( void *pContext ) noexcept
{
    return COMMAND_STATE_ENABLED | ( static_cast<const map_workspace_t *>( pContext )->bTextureLock ? COMMAND_STATE_CHECKED : 0u );
}

command_result_t GridSmaller( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    MapWorkspace_SetGridSize( pWorkspace, pWorkspace->gridSize * 0.5 );
    return command_result_t::OK;
}

command_result_t GridLarger( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    MapWorkspace_SetGridSize( pWorkspace, pWorkspace->gridSize * 2.0 );
    return command_result_t::OK;
}

// `map.grid <size>` from the console; any power of two in range.
command_result_t GridSet( void *pContext, const command_args_t &args ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    if ( args.nArgs != 1u ) { return command_result_t::INVALID_ARGUMENTS; }
    const QString text = QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) );
    bool bOk = false;
    const double size = text.toDouble( &bOk );
    if ( !bOk || !( size >= MAP_GRID_MIN && size <= MAP_GRID_MAX ) || std::exp2( std::round( std::log2( size ) ) ) != size ) {
        return command_result_t::INVALID_ARGUMENTS;
    }
    MapWorkspace_SetGridSize( pWorkspace, size );
    return command_result_t::OK;
}

u32 GridSmallerState( void *pContext ) noexcept
{
    return static_cast<const map_workspace_t *>( pContext )->gridSize > MAP_GRID_MIN ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

u32 GridLargerState( void *pContext ) noexcept
{
    return static_cast<const map_workspace_t *>( pContext )->gridSize < MAP_GRID_MAX ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

command_result_t GridToggle( void *pContext, const command_args_t & ) noexcept
{
    auto *pWorkspace = static_cast<map_workspace_t *>( pContext );
    MapWorkspace_SetGridVisible( pWorkspace, !pWorkspace->bGridVisible );
    return command_result_t::OK;
}

u32 GridToggleState( void *pContext ) noexcept
{
    return COMMAND_STATE_ENABLED | ( static_cast<const map_workspace_t *>( pContext )->bGridVisible ? COMMAND_STATE_CHECKED : 0u );
}

command_result_t FrameAll( void *pContext, const command_args_t & ) noexcept
{
    MapWorkspace_Frame( static_cast<map_workspace_t *>( pContext ), CY_FALSE );
    return command_result_t::OK;
}

template <map_frame_target_t kTarget>
command_result_t FrameSelection( void *pContext, const command_args_t & ) noexcept
{
    MapWorkspace_Frame( static_cast<map_workspace_t *>( pContext ), CY_TRUE, kTarget );
    return command_result_t::OK;
}

u32 HasSelectionState( void *pContext ) noexcept
{
    return EditorSelection_Count( &static_cast<const map_workspace_t *>( pContext )->selection ) != 0u ? COMMAND_STATE_ENABLED
                                                                                                        : COMMAND_STATE_NONE;
}
u32 CanFrameSelectionState( void *pContext ) noexcept
{
    return MapWorkspace_HasBlockPreview( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : HasSelectionState( pContext );
}

// Map checking and running need the game definition and the map compiler;
// listed now so menus and the keymap are complete.
command_result_t NotYetAvailable( void *, const command_args_t & ) noexcept { return command_result_t::DISABLED; }
u32 NeverEnabled( void * ) noexcept { return COMMAND_STATE_NONE; }

} // namespace

map_workspace_t::~map_workspace_t() noexcept
{
    MapWorkspace_Shutdown( this );
}

bool_t MapWorkspace_Init( map_workspace_t *pWorkspace, gui::editor_gui_t *pGui ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pGui != nullptr && pGui->bInitialized );
    if ( pWorkspace == nullptr || pGui == nullptr || pGui->pAllocator == nullptr ) { return CY_FALSE; }
    pWorkspace->pGui = pGui;
    if ( !MapWireframe_Init( &pWorkspace->wire, pGui->pAllocator ) || !EditorSelection_Init( &pWorkspace->selection, pGui->pAllocator ) ||
         !EditorSelection_Init( &pWorkspace->hidden, pGui->pAllocator ) ||
         EditorHistory_Init( &pWorkspace->history, pGui->pAllocator ) != editor_history_status_t::OK ||
         !EditorHistory_AddListener( &pWorkspace->history, &OnHistoryChanged, pWorkspace ) ) {
        return CY_FALSE;
    }
    // The settings page joins the editor registry once; a second map
    // session in the same editor finds it there.
    if ( EditorSettings_Find( &pGui->settings, StringView_FromCString( kMapSettings[0].pPath ) ) == nullptr &&
         EditorSettings_Register( &pGui->settings, kMapSettings, std::size( kMapSettings ) ) != settings_registry_status_t::OK ) {
        return CY_FALSE;
    }
    ( void )ReadGridSettings( pWorkspace );
    if ( !EditorSettings_AddListener( &pGui->settings, &OnSettingsChanged, pWorkspace ) ) { return CY_FALSE; }
    map_document_t *pDocument = CreateDocument( pGui->pAllocator );
    if ( pDocument == nullptr ) { return CY_FALSE; }
    Adopt( pWorkspace, pDocument, QString() );
    return CY_TRUE;
}

void MapWorkspace_Shutdown( map_workspace_t *pWorkspace ) noexcept
{
    if ( pWorkspace == nullptr ) { return; }
    if ( pWorkspace->pGui != nullptr ) { EditorSettings_RemoveListener( &pWorkspace->pGui->settings, &OnSettingsChanged, pWorkspace ); }
    delete pWorkspace->pDocument;
    pWorkspace->pDocument = nullptr;
    MapWireframe_Shutdown( &pWorkspace->wire );
    MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
    EditorSelection_Shutdown( &pWorkspace->selection );
    EditorSelection_Shutdown( &pWorkspace->hidden );
    geometry::MeshSelection_Shutdown( &pWorkspace->meshSelection );
    pWorkspace->selectedMeshEdgeSeed = {};
    EditorHistory_Shutdown( &pWorkspace->history );
    pWorkspace->nListeners = 0u;
}

map_status_t MapWorkspace_New( map_workspace_t *pWorkspace ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pWorkspace->pGui != nullptr );
    map_document_t *pDocument = CreateDocument( pWorkspace->pGui->pAllocator );
    if ( pDocument == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    Adopt( pWorkspace, pDocument, QString() );
    CY_LOG_WRITE( Info, Editor, "New map" );
    return map_status_t::OK;
}

map_files_result_t MapWorkspace_Open( map_workspace_t *pWorkspace, const QString &path )
{
    CY_ASSERT( pWorkspace != nullptr && pWorkspace->pGui != nullptr );
    map_files_result_t result{};
    auto *pDocument = new ( std::nothrow ) map_document_t{};
    if ( pDocument == nullptr ) {
        result.status = map_files_status_t::DOCUMENT_FAILED;
        result.documentStatus = map_status_t::OUT_OF_MEMORY;
        return result;
    }
    const QByteArray utf8 = path.toUtf8();
    result = MapFiles_Load( pDocument, pWorkspace->pGui->pAllocator, utf8.constData() );
    LogProblems( *pDocument );
    if ( result.status != map_files_status_t::OK ) {
        delete pDocument;
        return result;
    }
    if ( pDocument->bReadOnly ) {
        LogText( log_level_t::Warning, QStringLiteral( "%1 opened read-only: fix the duplicate IDs listed above, or reopen with "
                                                       "\"Reassign Duplicate IDs\"" ).arg( QFileInfo( path ).fileName() ) );
    }
    Adopt( pWorkspace, pDocument, path );
    return result;
}

map_files_result_t MapWorkspace_Save( map_workspace_t *pWorkspace )
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( pWorkspace->path.isEmpty() ) {
        map_files_result_t result{};
        result.status = map_files_status_t::INVALID_ARGUMENT;
        return result;
    }
    return MapWorkspace_SaveAs( pWorkspace, pWorkspace->path );
}

map_files_result_t MapWorkspace_SaveAs( map_workspace_t *pWorkspace, const QString &path )
{
    CY_ASSERT( pWorkspace != nullptr && pWorkspace->pDocument != nullptr );
    const QByteArray utf8 = path.toUtf8();
    const QString key = PersistenceKey( path );
    const QStringList inventory = pWorkspace->persistedChunksByRoot.value( key );
    std::vector<QByteArray> encoded;
    std::vector<string_view_t> paths;
    encoded.reserve( static_cast<usize>( inventory.size() ) );
    paths.reserve( static_cast<usize>( inventory.size() ) );
    for ( const QString &chunk : inventory ) { encoded.push_back( chunk.toUtf8() ); }
    for ( const QByteArray &chunk : encoded ) { paths.push_back( { chunk.constData(), static_cast<usize>( chunk.size() ) } ); }
    text_buffer_t published{};
    if ( !TextBuffer_Init( &published, pWorkspace->pDocument->pAllocator ) ) {
        return { map_files_status_t::DOCUMENT_FAILED, map_status_t::OUT_OF_MEMORY };
    }
    const map_files_result_t result = MapFiles_Save( pWorkspace->pDocument, utf8.constData(), { paths.data(), paths.size() }, &published );
    if ( result.status != map_files_status_t::OK ) {
        // A rename failure can happen after some chunks were replaced. Keep
        // exactly those paths owned for cleanup by a later save; unrenamed
        // destination files remain outside this document's inventory.
        if ( !TextBuffer_IsEmpty( &published ) ) {
            QStringList recovery = inventory;
            const auto replacements = QString::fromUtf8( TextBuffer_Data( &published ), static_cast<qsizetype>( TextBuffer_Length( &published ) ) )
                .split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
            for ( const QString &chunk : replacements ) { if ( !recovery.contains( chunk ) ) { recovery.append( chunk ); } }
            pWorkspace->persistedChunksByRoot.insert( key, recovery );
        }
        return result;
    }
    pWorkspace->persistedChunksByRoot.insert( key, PersistedChunkPaths( *pWorkspace->pDocument ) );
    pWorkspace->path = path;
    EditorHistory_MarkClean( &pWorkspace->history );
    // Saving can assign IDs and move objects between chunks; the records the
    // panels show may have changed even though nothing moved.
    if ( MapWireframe_Build( &pWorkspace->wire, *pWorkspace->pDocument ) != map_status_t::OK ) {
        CY_LOG_WRITE( Error, Editor, "Map views could not be rebuilt after saving" );
    }
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_DOCUMENT | MAP_CHANGE_TITLE );
    return result;
}

QString MapWorkspace_DisplayName( const map_workspace_t *pWorkspace )
{
    CY_ASSERT( pWorkspace != nullptr );
    return pWorkspace->path.isEmpty() ? QStringLiteral( "Untitled" ) : QFileInfo( pWorkspace->path ).fileName();
}

void MapWorkspace_Select( map_workspace_t *pWorkspace, u64 id, map_select_mode_t mode ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( mode != MAP_SELECT_REPLACE && mode != MAP_SELECT_ADD && mode != MAP_SELECT_TOGGLE && mode != MAP_SELECT_REMOVE ) { return; }
    const bool selected = EditorSelection_Contains( &pWorkspace->selection, id );
    const bool shouldChange = mode == MAP_SELECT_REPLACE ?
        ( id == 0 ? pWorkspace->selection.ids.nCount != 0 : !selected || pWorkspace->selection.ids.nCount != 1 ) :
        mode == MAP_SELECT_TOGGLE ? id != 0 : mode == MAP_SELECT_ADD ? id != 0 && !selected :
        mode == MAP_SELECT_REMOVE ? selected : false;
    const bool rootsChanged = EditorSelection_Apply( &pWorkspace->selection, id, mode );
    if ( shouldChange && !rootsChanged ) { return; } // OOM: preserve the component context too.
    const bool facesChanged = pWorkspace->selectedBrushFaceObject != 0 || pWorkspace->selectedMeshFaceObject != 0;
    pWorkspace->selectedBrushFaceObject = 0; pWorkspace->selectedBrushFaceSide = 0;
    pWorkspace->selectedMeshFaceObject = 0; pWorkspace->selectedMeshFaceId = 0;
    const bool componentsChanged = ClearMeshComponents( pWorkspace );
    if ( rootsChanged || facesChanged || componentsChanged ) {
        if ( !rootsChanged ) { ++pWorkspace->selection.revision; }
        MapWorkspace_Notify( pWorkspace, MAP_CHANGE_SELECTION );
    }
}

void MapWorkspace_SetSelection( map_workspace_t *pWorkspace, const u64 *pIds, usize nIds ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && ( pIds != nullptr || nIds == 0u ) );
    editor_selection_t next{};
    if ( !EditorSelection_Init( &next, pWorkspace->selection.ids.pAllocator ) ) { return; }
    bool hasId = false;
    for ( usize i = 0; i < nIds; ++i ) { hasId |= pIds[i] != 0; }
    // Starting from empty distinguishes OOM from Set's no-op return value.
    // Empty requests need no allocation, including arrays containing only 0.
    if ( hasId && !EditorSelection_Set( &next, pIds, nIds ) ) { return; }
    const bool rootsChanged = next.ids.nCount != pWorkspace->selection.ids.nCount ||
        ( next.ids.nCount != 0 && !std::equal( next.ids.pData, next.ids.pData + next.ids.nCount, pWorkspace->selection.ids.pData ) );
    if ( rootsChanged ) {
        Vector_Shutdown( &pWorkspace->selection.ids ); Vector_Move( &pWorkspace->selection.ids, &next.ids );
        ++pWorkspace->selection.revision;
    }
    const bool facesChanged = pWorkspace->selectedBrushFaceObject != 0 || pWorkspace->selectedMeshFaceObject != 0;
    pWorkspace->selectedBrushFaceObject = 0; pWorkspace->selectedBrushFaceSide = 0;
    pWorkspace->selectedMeshFaceObject = 0; pWorkspace->selectedMeshFaceId = 0;
    const bool componentsChanged = ClearMeshComponents( pWorkspace );
    if ( rootsChanged || facesChanged || componentsChanged ) {
        if ( !rootsChanged ) { ++pWorkspace->selection.revision; }
        MapWorkspace_Notify( pWorkspace, MAP_CHANGE_SELECTION );
    }
}

void MapWorkspace_ClearMeshEdges( map_workspace_t *pWorkspace ) noexcept
{
    if ( pWorkspace == nullptr || ( pWorkspace->elementMode != map_element_mode_t::EDGES && pWorkspace->meshSelection.edges.nCount == 0 ) ||
         !ClearMeshComponents( pWorkspace ) ) { return; }
    ++pWorkspace->selection.revision;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_SELECTION );
}

void MapWorkspace_ClearMeshVertices( map_workspace_t *pWorkspace ) noexcept
{
    if ( pWorkspace == nullptr || ( pWorkspace->elementMode != map_element_mode_t::VERTICES && pWorkspace->meshSelection.vertices.nCount == 0 ) ||
         !ClearMeshComponents( pWorkspace ) ) { return; }
    ++pWorkspace->selection.revision;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_SELECTION );
}

bool_t MapWorkspace_IsSelected( const map_workspace_t *pWorkspace, u64 id ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    return EditorSelection_Contains( &pWorkspace->selection, id );
}

bool_t MapWorkspace_IsModified( const map_workspace_t *pWorkspace ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    return !EditorHistory_IsClean( &pWorkspace->history );
}

editor_history_status_t MapWorkspace_Undo( map_workspace_t *pWorkspace ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    return EditorHistory_Undo( &pWorkspace->history );
}

editor_history_status_t MapWorkspace_Redo( map_workspace_t *pWorkspace ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    return EditorHistory_Redo( &pWorkspace->history );
}

void MapWorkspace_DocumentChanged( map_workspace_t *pWorkspace, bool rebuildWire ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pWorkspace->pDocument != nullptr );
    if ( rebuildWire && MapWireframe_Build( &pWorkspace->wire, *pWorkspace->pDocument ) != map_status_t::OK ) {
        CY_LOG_WRITE( Error, Editor, "Map views could not be rebuilt after an edit" );
    }
    pWorkspace->editPreview = {};
    MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
    if ( !MapWorkspace_HasBrushFace( pWorkspace ) ) { pWorkspace->selectedBrushFaceObject = 0; pWorkspace->selectedBrushFaceSide = 0; }
    u32 changes = MAP_CHANGE_DOCUMENT;
    if ( EditorSelection_Prune( &pWorkspace->selection, &ObjectExists, pWorkspace ) ) { changes |= MAP_CHANGE_SELECTION; }
    if ( !MapWorkspace_HasMeshFace( pWorkspace ) ) { pWorkspace->selectedMeshFaceObject = 0; pWorkspace->selectedMeshFaceId = 0; }
    MapWorkspace_Notify( pWorkspace, changes );
}

bool_t MapWorkspace_CanChangeCameraSpeed( const map_workspace_t *pWorkspace, map_camera_speed_action_t action ) noexcept
{
    camera_speed_write_t write{};
    return PlanCameraSpeed( pWorkspace, action, write );
}

bool_t MapWorkspace_ChangeCameraSpeed( map_workspace_t *pWorkspace, map_camera_speed_action_t action ) noexcept
{
    camera_speed_write_t write{};
    if ( !PlanCameraSpeed( pWorkspace, action, write ) ) { return CY_FALSE; }
    auto &settings = pWorkspace->pGui->settings;
    settings_document_t candidate{};
    if ( !PrepareCameraSpeed( write, settings, candidate ) ) { return CY_FALSE; }
    // Like settings import, preserve the attached store's address and ownership.
    std::swap( write.pStore->pDocument, candidate.pDocument );
    std::swap( write.pStore->nLoadedVersion, candidate.nLoadedVersion );
    // The prepared REAL leaf (or absent inherited value) makes this exact-path
    // registry write allocation-free. Listeners see the published effective speed.
    if ( EditorSettings_Write( &settings, write.scope, *write.pDescriptor, write.value ) == settings_registry_status_t::OK ) { return CY_TRUE; }
    std::swap( write.pStore->pDocument, candidate.pDocument );
    std::swap( write.pStore->nLoadedVersion, candidate.nLoadedVersion );
    return CY_FALSE;
}

void MapWorkspace_SetGridSize( map_workspace_t *pWorkspace, f64 gridSize ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( !std::isfinite( gridSize ) ) { return; }
    const f64 clamped = std::clamp( gridSize, MAP_GRID_MIN, MAP_GRID_MAX );
    if ( clamped == pWorkspace->gridSize ) { return; }
    setting_value_t value{};
    value.type = setting_type_t::INTEGER;
    value.nValue = static_cast<i64>( clamped );
    // The settings listener updates the session when the write lands.
    if ( WriteGridSetting( pWorkspace, "editor.grid.size", value ) && pWorkspace->gridSize == clamped ) { return; }
    pWorkspace->gridSize = clamped;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

void MapWorkspace_SetGridVisible( map_workspace_t *pWorkspace, bool_t bVisible ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( bVisible == pWorkspace->bGridVisible ) { return; }
    setting_value_t value{};
    value.type = setting_type_t::BOOL;
    value.bValue = bVisible;
    if ( WriteGridSetting( pWorkspace, "editor.grid.show", value ) && pWorkspace->bGridVisible == bVisible ) { return; }
    pWorkspace->bGridVisible = bVisible;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

void MapWorkspace_SetSnapToGrid( map_workspace_t *pWorkspace, bool_t bSnap ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( bSnap == pWorkspace->bSnapToGrid ) { return; }
    setting_value_t value{};
    value.type = setting_type_t::BOOL;
    value.bValue = bSnap;
    if ( WriteGridSetting( pWorkspace, "editor.grid.snap", value ) && pWorkspace->bSnapToGrid == bSnap ) { return; }
    pWorkspace->bSnapToGrid = bSnap;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

void MapWorkspace_SetAngleSnap( map_workspace_t *pWorkspace, f64 degrees ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( !std::isfinite( degrees ) ) { return; }
    const f64 clamped = std::clamp( degrees, 0.0, 90.0 );
    if ( clamped == pWorkspace->angleSnap ) { return; }
    if ( clamped > 0.0 ) { pWorkspace->lastAngleSnap = clamped; }
    setting_value_t value{};
    value.type = setting_type_t::REAL;
    value.flValue = clamped;
    if ( WriteGridSetting( pWorkspace, "editor.grid.angle_snap", value ) && pWorkspace->angleSnap == clamped ) { return; }
    pWorkspace->angleSnap = clamped;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

void MapWorkspace_SetScaleSnap( map_workspace_t *pWorkspace, f64 step ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( !std::isfinite( step ) ) { return; }
    const f64 clamped = std::clamp( step, 0.0, 10.0 );
    if ( clamped == pWorkspace->scaleSnap ) { return; }
    if ( clamped > 0.0 ) { pWorkspace->lastScaleSnap = clamped; }
    setting_value_t value{};
    value.type = setting_type_t::REAL;
    value.flValue = clamped;
    if ( WriteGridSetting( pWorkspace, "editor.grid.scale_snap", value ) && pWorkspace->scaleSnap == clamped ) { return; }
    pWorkspace->scaleSnap = clamped;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

void MapWorkspace_SetCordon( map_workspace_t *pWorkspace, const map_bounds_t &bounds ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    pWorkspace->cordon = bounds;
    pWorkspace->bCordonActive = bounds.bHas;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

void MapWorkspace_SetCordonActive( map_workspace_t *pWorkspace, bool_t bActive ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    const bool_t bNext = bActive && pWorkspace->cordon.bHas ? CY_TRUE : CY_FALSE;
    if ( bNext == pWorkspace->bCordonActive ) { return; }
    pWorkspace->bCordonActive = bNext;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

bool_t MapWorkspace_IsCordonActive( const map_workspace_t *pWorkspace ) noexcept
{
    return pWorkspace != nullptr && pWorkspace->bCordonActive && pWorkspace->cordon.bHas ? CY_TRUE : CY_FALSE;
}

void MapWorkspace_SetMaterialImages( map_workspace_t *pWorkspace, map_material_image_fn pfnImage, void *pContext ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    pWorkspace->pfnMaterialImage = pfnImage;
    pWorkspace->pMaterialImageContext = pContext;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

QImage MapWorkspace_MaterialImage( const map_workspace_t *pWorkspace, const QString &path )
{
    if ( pWorkspace == nullptr || pWorkspace->pfnMaterialImage == nullptr || path.isEmpty() ) { return {}; }
    return pWorkspace->pfnMaterialImage( pWorkspace->pMaterialImageContext, path );
}

void MapWorkspace_SetTextureLock( map_workspace_t *pWorkspace, bool_t bLock ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( bLock == pWorkspace->bTextureLock ) { return; }
    setting_value_t value{};
    value.type = setting_type_t::BOOL;
    value.bValue = bLock;
    if ( WriteGridSetting( pWorkspace, "editor.map.texture_lock", value ) && pWorkspace->bTextureLock == bLock ) { return; }
    pWorkspace->bTextureLock = bLock;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

void MapWorkspace_SetElementMode( map_workspace_t *pWorkspace, map_element_mode_t mode ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && mode < map_element_mode_t::COUNT );
    if ( pWorkspace->elementMode == mode || !MapWorkspace_IsElementModeAvailable( mode ) ) { return; }
    pWorkspace->elementMode = mode;
    const bool clearedFace = mode != map_element_mode_t::FACES &&
        ( pWorkspace->selectedMeshFaceObject != 0 || pWorkspace->selectedMeshFaceId != 0 ||
          pWorkspace->selectedBrushFaceObject != 0 || pWorkspace->selectedBrushFaceSide != 0 );
    if ( clearedFace ) {
        pWorkspace->selectedMeshFaceObject = 0; pWorkspace->selectedMeshFaceId = 0;
        pWorkspace->selectedBrushFaceObject = 0; pWorkspace->selectedBrushFaceSide = 0;
    }
    const bool clearedComponents = ClearMeshComponents( pWorkspace );
    if ( clearedComponents ) { ++pWorkspace->selection.revision; }
    if ( pWorkspace->editPreview.bActive ) {
        // Publish one coherent mode change: every observer sees canceled
        // construction and cleared components before its single refresh.
        pWorkspace->editPreview = {};
        MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
    }
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW | ( clearedFace || clearedComponents ? MAP_CHANGE_SELECTION : 0u ) );
}

map_visgroup_t MapWorkspace_VisgroupOf( const map_workspace_t *pWorkspace, const map_wire_object_t &object ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    switch ( object.kind ) {
        case map_wire_kind_t::BRUSH:
        case map_wire_kind_t::MESH:
        case map_wire_kind_t::PATCH:
            if ( object.owner != 0u ) { return map_visgroup_t::TIED; }
            return object.kind == map_wire_kind_t::BRUSH ? map_visgroup_t::BRUSHES
                                                        : ( object.kind == map_wire_kind_t::MESH ? map_visgroup_t::MESHES : map_visgroup_t::PATCHES );
        case map_wire_kind_t::TERRAIN: return map_visgroup_t::TERRAIN;
        case map_wire_kind_t::ENTITY: break;
    }
    // Entities by class family, the naming convention every Source-style
    // game follows.
    const map_wire_entity_t *pEntity = MapWireframe_FindEntity( pWorkspace->wire, object.id );
    const QLatin1StringView className( pEntity != nullptr ? pEntity->className : "" );
    if ( className.startsWith( QLatin1StringView( "light" ) ) ) { return map_visgroup_t::LIGHTS; }
    if ( className.startsWith( QLatin1StringView( "info_" ) ) ) { return map_visgroup_t::INFO; }
    if ( className.startsWith( QLatin1StringView( "trigger_" ) ) ) { return map_visgroup_t::TRIGGERS; }
    if ( className.startsWith( QLatin1StringView( "prop_" ) ) ) { return map_visgroup_t::PROPS; }
    return map_visgroup_t::OTHER_ENTITIES;
}

bool_t MapWorkspace_IsVisible( const map_workspace_t *pWorkspace, const map_wire_object_t &object ) noexcept
{
    if ( EditorSelection_Count( &pWorkspace->hidden ) != 0u && EditorSelection_Contains( &pWorkspace->hidden, object.id ) ) { return CY_FALSE; }
    if ( pWorkspace->bCordonActive && pWorkspace->cordon.bHas && object.bounds.bHas ) {
        // Touching the cordon is inside: a wall on its edge stays visible.
        const math::aabbd_t &c = pWorkspace->cordon.box;
        const math::aabbd_t &o = object.bounds.box;
        if ( o.maximum.x < c.minimum.x || o.minimum.x > c.maximum.x || o.maximum.y < c.minimum.y || o.minimum.y > c.maximum.y ||
             o.maximum.z < c.minimum.z || o.minimum.z > c.maximum.z ) {
            return CY_FALSE;
        }
    }
    return pWorkspace->hiddenVisgroups == 0u ||
           ( pWorkspace->hiddenVisgroups & ( 1u << static_cast<u32>( MapWorkspace_VisgroupOf( pWorkspace, object ) ) ) ) == 0u;
}

void MapWorkspace_SetVisgroupHidden( map_workspace_t *pWorkspace, map_visgroup_t group, bool_t bHidden ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && group < map_visgroup_t::COUNT );
    const u32 bit = 1u << static_cast<u32>( group );
    const u32 hidden = bHidden ? ( pWorkspace->hiddenVisgroups | bit ) : ( pWorkspace->hiddenVisgroups & ~bit );
    if ( hidden == pWorkspace->hiddenVisgroups ) { return; }
    pWorkspace->hiddenVisgroups = hidden;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

bool_t MapWorkspace_IsVisgroupHidden( const map_workspace_t *pWorkspace, map_visgroup_t group ) noexcept
{
    return group < map_visgroup_t::COUNT && ( pWorkspace->hiddenVisgroups & ( 1u << static_cast<u32>( group ) ) ) != 0u;
}

const char *MapWorkspace_VisgroupName( map_visgroup_t group ) noexcept
{
    constexpr const char *kNames[]{ "Brushes", "Meshes", "Patches", "Terrain", "Tied to entities", "Lights", "Info", "Triggers", "Props", "Other" };
    static_assert( std::size( kNames ) == static_cast<usize>( map_visgroup_t::COUNT ), "Visgroup names and enum diverged" );
    return group < map_visgroup_t::COUNT ? kNames[static_cast<usize>( group )] : "Unknown";
}

bool_t MapWorkspace_IsSelectableObject( const map_workspace_t *workspace, const map_wire_object_t &object ) noexcept
{
    if ( workspace == nullptr || !MapWorkspace_IsVisible( workspace, object ) ) { return CY_FALSE; }
    switch ( workspace->elementMode ) {
        case map_element_mode_t::VERTICES:
        case map_element_mode_t::EDGES:
        case map_element_mode_t::NAVIGATION: return CY_FALSE;
        case map_element_mode_t::MESHES:
        case map_element_mode_t::FACES: return object.kind == map_wire_kind_t::BRUSH || object.kind == map_wire_kind_t::MESH;
        case map_element_mode_t::OBJECTS:
        case map_element_mode_t::GROUPS: return CY_TRUE;
        case map_element_mode_t::COUNT: break;
    }
    return CY_FALSE;
}

bool_t MapWorkspace_IsElementModeAvailable( map_element_mode_t mode ) noexcept
{
    return mode < map_element_mode_t::COUNT && kElementModes[static_cast<usize>( mode )].bAvailable;
}

const char *MapWorkspace_ElementModeName( map_element_mode_t mode ) noexcept
{
    return mode < map_element_mode_t::COUNT ? kElementModes[static_cast<usize>( mode )].pName : "Unknown";
}

const setting_descriptor_t *MapWorkspace_SettingsCatalogue( usize *pnDescriptorsOut ) noexcept
{
    if ( pnDescriptorsOut != nullptr ) { *pnDescriptorsOut = std::size( kMapSettings ); }
    return kMapSettings;
}

void MapWorkspace_SetTool( map_workspace_t *pWorkspace, map_tool_t tool ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && tool < map_tool_t::COUNT );
    if ( pWorkspace->tool == tool || !MapWorkspace_IsToolAvailable( tool ) ) { return; }
    pWorkspace->tool = tool;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

bool_t MapWorkspace_IsToolAvailable( map_tool_t tool ) noexcept
{
    return tool < map_tool_t::COUNT && kTools[static_cast<usize>( tool )].bAvailable;
}

const char *MapWorkspace_ToolName( map_tool_t tool ) noexcept
{
    return tool < map_tool_t::COUNT ? kTools[static_cast<usize>( tool )].pName : "Unknown";
}

void MapWorkspace_Frame( map_workspace_t *pWorkspace, bool_t bSelectionOnly, map_frame_target_t target ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    map_bounds_t bounds{};
    const bool stagedBlock = MapWorkspace_HasBlockPreview( pWorkspace );
    if ( bSelectionOnly && stagedBlock ) {
        // Construction has no authored selection yet. Center commands frame
        // its retained private geometry, including after a retryable failure.
        bounds = pWorkspace->editPreview.bounds;
    } else if ( bSelectionOnly ) {
        for ( usize i = 0u; i < EditorSelection_Count( &pWorkspace->selection ); ++i ) {
            const map_wire_object_t *pObject = MapWireframe_FindObject( pWorkspace->wire, EditorSelection_At( &pWorkspace->selection, i ) );
            if ( pObject != nullptr ) { MapBounds_AddBounds( bounds, pObject->bounds ); }
        }
        // Point helper bounds do not describe an entity's owned solids.
        for ( usize i = 0u; i < pWorkspace->wire.objects.nCount; ++i ) {
            const auto &object = pWorkspace->wire.objects.pData[i];
            if ( object.owner != 0u && MapWorkspace_IsSelected( pWorkspace, object.owner ) && MapWorkspace_IsVisible( pWorkspace, object ) ) {
                MapBounds_AddBounds( bounds, object.bounds );
            }
        }
    } else {
        bounds = pWorkspace->wire.bounds;
        if ( stagedBlock ) { MapBounds_AddBounds( bounds, pWorkspace->editPreview.bounds ); }
    }
    // An empty map frames the origin at a comfortable working scale.
    if ( !bounds.bHas ) {
        MapBounds_AddPoint( bounds, math::Vec3d_Make( -512.0, -512.0, -512.0 ) );
        MapBounds_AddPoint( bounds, math::Vec3d_Make( 512.0, 512.0, 512.0 ) );
    }
    pWorkspace->frameBounds = bounds;
    pWorkspace->frameTarget = target;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_FRAME );
}

void MapWorkspace_SetCursor( map_workspace_t *pWorkspace, math::vec3d_t cursor, u32 axes ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    pWorkspace->cursor = cursor;
    pWorkspace->cursorAxes = axes;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_CURSOR );
}

bool_t MapWorkspace_AddListener( map_workspace_t *pWorkspace, map_listener_fn pfnChanged, void *pContext ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pfnChanged != nullptr );
    if ( pWorkspace->nListeners >= MAP_WORKSPACE_MAX_LISTENERS ) {
        CY_LOG_WRITE( Error, Editor, "Map workspace listener table is full" );
        return CY_FALSE;
    }
    pWorkspace->listeners[pWorkspace->nListeners++] = map_listener_t{ pfnChanged, pContext };
    return CY_TRUE;
}

void MapWorkspace_RemoveListener( map_workspace_t *pWorkspace, map_listener_fn pfnChanged, void *pContext ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    for ( usize i = 0u; i < pWorkspace->nListeners; ++i ) {
        if ( pWorkspace->listeners[i].pfnChanged != pfnChanged || pWorkspace->listeners[i].pContext != pContext ) { continue; }
        // Order is kept: panels refresh in the order they subscribed.
        for ( usize j = i + 1u; j < pWorkspace->nListeners; ++j ) { pWorkspace->listeners[j - 1u] = pWorkspace->listeners[j]; }
        --pWorkspace->nListeners;
        return;
    }
}

void MapWorkspace_Notify( map_workspace_t *pWorkspace, u32 changes ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( changes == MAP_CHANGE_NONE ) { return; }
    if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW ) ) != 0 && PruneMeshComponents( pWorkspace ) ) {
        ++pWorkspace->selection.revision;
        changes |= MAP_CHANGE_SELECTION;
    }
    if ( pWorkspace->editPreview.bStagedBlock && !MapWorkspace_HasBlockPreview( pWorkspace ) ) {
        // Validate the shared construction once, before any listener sees it.
        // Reset directly rather than recursively notifying from ClearPreview.
        pWorkspace->editPreview = {};
        MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
        changes |= MAP_CHANGE_VIEW;
    }
    if ( pWorkspace->editPreview.bActive && pWorkspace->editPreview.transform.kind != map_transform_preview_kind_t::NONE &&
         ( pWorkspace->pDocument == nullptr || pWorkspace->editPreview.documentRevision != pWorkspace->pDocument->geometry.revision ||
           pWorkspace->editPreview.selectionRevision != pWorkspace->selection.revision || pWorkspace->editPreview.tool != pWorkspace->tool ||
           pWorkspace->editPreview.mode != pWorkspace->elementMode || ( changes & MAP_CHANGE_SELECTION ) != 0 ||
           !MapWorkspace_PreviewVisibilityMatches( pWorkspace, pWorkspace->editPreview.visibility ) ||
           !( pWorkspace->editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE ?
               MapWorkspace_CanMoveSelection( pWorkspace ) : MapWorkspace_CanEditSelection( pWorkspace ) ) ) ) {
        // Every observer sees the same valid descriptor even when no pane owns
        // it (programmatic previews, hidden/removed selections, document edits).
        pWorkspace->editPreview = {};
        MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
        changes |= MAP_CHANGE_VIEW;
    }
    if ( pWorkspace->editPreview.bActive && pWorkspace->editPreview.bClip &&
         ( pWorkspace->tool != map_tool_t::CLIP || !MapWorkspace_CanEditBrushSelection( pWorkspace ) ||
           pWorkspace->editPreview.selectionRevision != pWorkspace->selection.revision || pWorkspace->pDocument == nullptr ||
           pWorkspace->editPreview.documentRevision != pWorkspace->pDocument->geometry.revision ) ) {
        // Every observer must see the same valid presentation, including
        // previews staged programmatically without a viewport gesture owner.
        pWorkspace->editPreview = {};
        MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
        changes |= MAP_CHANGE_VIEW;
    }
    // A listener may unsubscribe while being notified (a panel closing), so
    // walk a snapshot.
    map_listener_t snapshot[MAP_WORKSPACE_MAX_LISTENERS];
    const usize nListeners = pWorkspace->nListeners;
    std::copy( pWorkspace->listeners, pWorkspace->listeners + nListeners, snapshot );
    for ( usize i = 0u; i < nListeners; ++i ) {
        const auto &listener = snapshot[i];
        const bool subscribed = std::any_of( pWorkspace->listeners, pWorkspace->listeners + pWorkspace->nListeners,
            [&]( const map_listener_t &live ) { return live.pfnChanged == listener.pfnChanged && live.pContext == listener.pContext; } );
        if ( subscribed ) { listener.pfnChanged( listener.pContext, changes ); }
    }
}

command_registry_status_t MapWorkspace_RegisterCommands( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pRegistry != nullptr );
    constexpr u32 kTool = COMMAND_FLAG_CHECKABLE;
    const command_desc_t commands[]{
        { "map.tool.select", "Selection Tool", "Select objects in the views.", "tool-select", nullptr, kTool,
          ToolExecute<map_tool_t::SELECT>, ToolState<map_tool_t::SELECT>, pWorkspace },
        { "map.tool.camera", "Camera Tool", "Move and aim the 3D view's camera.", "tool-camera", nullptr, kTool,
          ToolExecute<map_tool_t::CAMERA>, ToolState<map_tool_t::CAMERA>, pWorkspace },
        { "map.tool.navigation", "Navigation", "Leave the editing tool and navigate while keeping the selection for inspection.", "tool-camera", nullptr, kTool,
          ToolExecute<map_tool_t::NONE>, ToolState<map_tool_t::NONE>, pWorkspace },
        { "map.camera.speed_increase", "Increase Camera Speed", "Double camera flight speed up to the configured limit.", "tool-camera", nullptr, 0u,
          CameraSpeedExecute<map_camera_speed_action_t::INCREASE>, CameraSpeedState<map_camera_speed_action_t::INCREASE>, pWorkspace },
        { "map.camera.speed_decrease", "Decrease Camera Speed", "Halve camera flight speed down to the configured limit.", "tool-camera", nullptr, 0u,
          CameraSpeedExecute<map_camera_speed_action_t::DECREASE>, CameraSpeedState<map_camera_speed_action_t::DECREASE>, pWorkspace },
        { "map.camera.speed_reset", "Reset Camera Speed", "Restore the registered default camera flight speed.", "tool-camera", nullptr, 0u,
          CameraSpeedExecute<map_camera_speed_action_t::RESET>, CameraSpeedState<map_camera_speed_action_t::RESET>, pWorkspace },
        { "map.tool.entity", "Entity Tool", "Place point entities.", "tool-entity", nullptr, kTool,
          ToolExecute<map_tool_t::ENTITY>, ToolState<map_tool_t::ENTITY>, pWorkspace },
        { "map.tool.block", "Block Tool", "Draw new brushes.", "tool-block", nullptr, kTool,
          ToolExecute<map_tool_t::BLOCK>, ToolState<map_tool_t::BLOCK>, pWorkspace },
        { "map.tool.texture", "Texture Application", "Apply and align materials on faces.", "tool-texture", nullptr, kTool,
          ToolExecute<map_tool_t::TEXTURE>, ToolState<map_tool_t::TEXTURE>, pWorkspace },
        { "map.tool.decal", "Decal Tool", "Place decals on surfaces.", "tool-decal", nullptr, kTool,
          ToolExecute<map_tool_t::DECAL>, ToolState<map_tool_t::DECAL>, pWorkspace },
        { "map.tool.overlay", "Overlay Tool", "Place overlays across faces.", "tool-overlay", nullptr, kTool,
          ToolExecute<map_tool_t::OVERLAY>, ToolState<map_tool_t::OVERLAY>, pWorkspace },
        { "map.tool.clip", "Clipping Tool", "Draw a cutting line in a 2D view or enter an exact plane. Repeat to cycle Back, Front, and Both.", "tool-clip", nullptr, kTool,
          ToolExecute<map_tool_t::CLIP>, ToolState<map_tool_t::CLIP>, pWorkspace },
        { "map.tool.vertex", "Vertex Tool", "Move brush vertices and edges.", "tool-vertex", nullptr, kTool,
          ToolExecute<map_tool_t::VERTEX>, ToolState<map_tool_t::VERTEX>, pWorkspace },
        { "map.tool.path", "Path Tool", "Lay out path corners and tracks.", "tool-path", nullptr, kTool,
          ToolExecute<map_tool_t::PATH>, ToolState<map_tool_t::PATH>, pWorkspace },
        { "map.tool.measure", "Measure Tool", "Measure distances and angles.", "tool-measure", nullptr, kTool,
          ToolExecute<map_tool_t::MEASURE>, ToolState<map_tool_t::MEASURE>, pWorkspace },
        { "map.tool.terrain", "Terrain Tool", "Sculpt and paint terrain.", "tool-terrain", nullptr, kTool,
          ToolExecute<map_tool_t::TERRAIN>, ToolState<map_tool_t::TERRAIN>, pWorkspace },
        { "map.tool.patch", "Patch Tool", "Create and edit curved patches.", "tool-patch", nullptr, kTool,
          ToolExecute<map_tool_t::PATCH>, ToolState<map_tool_t::PATCH>, pWorkspace },
        { "map.tool.translate", "Translate", "Move the selection along the axes.", "tool-translate", nullptr, kTool,
          ToolExecute<map_tool_t::TRANSLATE>, ToolState<map_tool_t::TRANSLATE>, pWorkspace },
        { "map.tool.rotate", "Rotate", "Rotate the selection about its pivot.", "tool-rotate", nullptr, kTool,
          ToolExecute<map_tool_t::ROTATE>, ToolState<map_tool_t::ROTATE>, pWorkspace },
        { "map.tool.scale", "Scale", "Scale the selection about its pivot.", "tool-scale", nullptr, kTool,
          ToolExecute<map_tool_t::SCALE>, ToolState<map_tool_t::SCALE>, pWorkspace },
        { "map.tool.pivot", "Pivot", "Place the pivot transforms use.", "tool-pivot", nullptr, kTool,
          ToolExecute<map_tool_t::PIVOT>, ToolState<map_tool_t::PIVOT>, pWorkspace },
        { "map.tool.polygon", "Polygon Tool", "Draw a polygon and extrude it into a mesh.", "tool-polygon", nullptr, kTool,
          ToolExecute<map_tool_t::POLYGON>, ToolState<map_tool_t::POLYGON>, pWorkspace },
        { "map.tool.mirror", "Mirror Tool", "Mirror the selection across a line you draw.", "tool-mirror", nullptr, kTool,
          ToolExecute<map_tool_t::MIRROR>, ToolState<map_tool_t::MIRROR>, pWorkspace },
        { "map.tool.paint", "Paint Tool", "Paint blend materials on meshes and terrain.", "tool-paint", nullptr, kTool,
          ToolExecute<map_tool_t::PAINT>, ToolState<map_tool_t::PAINT>, pWorkspace },
        { "map.select_mode.vertices", "Vertices", "Select authored mesh vertices by their persistent source IDs.", "select-vertices", nullptr, kTool,
          ElementModeExecute<map_element_mode_t::VERTICES>, ElementModeState<map_element_mode_t::VERTICES>, pWorkspace },
        { "map.select_mode.edges", "Edges", "Select authored mesh edges by their persistent endpoint IDs.", "select-edges", nullptr, kTool,
          ElementModeExecute<map_element_mode_t::EDGES>, ElementModeState<map_element_mode_t::EDGES>, pWorkspace },
        { "map.select_mode.faces", "Faces", "Select faces.", "select-faces", nullptr, kTool,
          ElementModeExecute<map_element_mode_t::FACES>, ElementModeState<map_element_mode_t::FACES>, pWorkspace },
        { "map.select_mode.meshes", "Meshes", "Select whole meshes and brushes.", "select-meshes", nullptr, kTool,
          ElementModeExecute<map_element_mode_t::MESHES>, ElementModeState<map_element_mode_t::MESHES>, pWorkspace },
        { "map.select_mode.objects", "Objects", "Select objects: brushes, meshes, and entities.", "select-objects", nullptr, kTool,
          ElementModeExecute<map_element_mode_t::OBJECTS>, ElementModeState<map_element_mode_t::OBJECTS>, pWorkspace },
        { "map.select_mode.groups", "Groups", "Select whole groups.", "select-groups", nullptr, kTool,
          ElementModeExecute<map_element_mode_t::GROUPS>, ElementModeState<map_element_mode_t::GROUPS>, pWorkspace },
        { "map.select_mode.navigation", "Navigation", "Select navigation mesh areas.", "select-navigation", nullptr, kTool,
          ElementModeExecute<map_element_mode_t::NAVIGATION>, ElementModeState<map_element_mode_t::NAVIGATION>, pWorkspace },
        { "map.show.world", "Show World Geometry", "Show or hide brushes, meshes, and patches.", "filter-world", nullptr, COMMAND_FLAG_CHECKABLE,
          ShowToggle<kShowWorld>, ShowState<kShowWorld>, pWorkspace },
        { "map.show.entities", "Show Entities", "Show or hide point entities other than lights, triggers, and props.", "filter-entities", nullptr,
          COMMAND_FLAG_CHECKABLE, ShowToggle<kShowEntities>, ShowState<kShowEntities>, pWorkspace },
        { "map.show.lights", "Show Lights", "Show or hide lights.", "filter-lights", nullptr, COMMAND_FLAG_CHECKABLE,
          ShowToggle<kShowLights>, ShowState<kShowLights>, pWorkspace },
        { "map.show.triggers", "Show Triggers", "Show or hide trigger entities.", "filter-triggers", nullptr, COMMAND_FLAG_CHECKABLE,
          ShowToggle<kShowTriggers>, ShowState<kShowTriggers>, pWorkspace },
        { "map.show.props", "Show Props", "Show or hide props.", "filter-props", nullptr, COMMAND_FLAG_CHECKABLE,
          ShowToggle<kShowProps>, ShowState<kShowProps>, pWorkspace },
        { "map.show.terrain", "Show Terrain", "Show or hide terrain.", "tool-terrain", nullptr, COMMAND_FLAG_CHECKABLE,
          ShowToggle<kShowTerrain>, ShowState<kShowTerrain>, pWorkspace },
        { "map.show.tied", "Show Tied Geometry", "Show or hide geometry tied to entities.", "filter-models", nullptr, COMMAND_FLAG_CHECKABLE,
          ShowToggle<kShowTied>, ShowState<kShowTied>, pWorkspace },
        { "map.grid.snap", "Snap to Grid", "Snap edits to the grid.", "snap-grid", nullptr, COMMAND_FLAG_CHECKABLE,
          SnapToggle, SnapToggleState, pWorkspace },
        { "map.grid.angle_snap", "Angle Snap", "Enable the configured rotation step. Tool support is required to apply snapping.", "snap-angle", nullptr, COMMAND_FLAG_CHECKABLE,
          AngleSnapToggle, AngleSnapState, pWorkspace },
        { "map.grid.scale_snap", "Scale Snap", "Enable the configured scale step. Tool support is required to apply snapping.", "tool-scale", nullptr, COMMAND_FLAG_CHECKABLE,
          ScaleSnapToggle, ScaleSnapState, pWorkspace },
        { "map.texture.lock", "Texture Lock", "Textures move with brushes.", "texture-lock", nullptr, COMMAND_FLAG_CHECKABLE,
          TextureLockToggle, TextureLockState, pWorkspace },
        { "map.grid.smaller", "Smaller Grid", "Halve the grid size.", "grid-smaller", nullptr, COMMAND_FLAG_NONE,
          GridSmaller, GridSmallerState, pWorkspace },
        { "map.grid.larger", "Larger Grid", "Double the grid size.", "grid-larger", nullptr, COMMAND_FLAG_NONE,
          GridLarger, GridLargerState, pWorkspace },
        { "map.grid.show", "Show Grid", "Show or hide the grid in the 2D views.", "grid-show", nullptr, COMMAND_FLAG_CHECKABLE,
          GridToggle, GridToggleState, pWorkspace },
        { "map.grid", "Set Grid Size", "Set the grid size to a power of two from 1 to 4096.", nullptr, "map.grid <size>",
          COMMAND_FLAG_CONSOLE_ONLY, GridSet, nullptr, pWorkspace },
        { "map.view.frame_all", "Frame Map", "Fit the whole map in every view.", "view-frame", nullptr, COMMAND_FLAG_NONE,
          FrameAll, nullptr, pWorkspace },
        { "map.view.center_selection_2d", "Center 2D Views on Selection", "Fit the selection in the 2D views.", nullptr, nullptr,
          COMMAND_FLAG_NONE, FrameSelection<map_frame_target_t::ORTHOGRAPHIC>, CanFrameSelectionState, pWorkspace },
        { "map.view.center_selection_3d", "Center 3D View on Selection", "Bring the selection in front of the 3D camera.", nullptr,
          nullptr, COMMAND_FLAG_NONE, FrameSelection<map_frame_target_t::PERSPECTIVE>, CanFrameSelectionState, pWorkspace },
        // map.check opens a window and asks the asset catalogue; the
        // application registers it (CypherMapGui_Check.h).
        { "map.run", "Run Map", "Compile the map and start the game on it.", "map-run", nullptr, COMMAND_FLAG_NONE,
          NotYetAvailable, NeverEnabled, pWorkspace },
    };
    if ( const command_registry_status_t status = EditorCommands_Register( pRegistry, commands, std::size( commands ) );
         status != command_registry_status_t::OK ) {
        return status;
    }
    // Commands that work on what the session already has.
    const command_desc_t working[]{
        { "map.select.loop", "Select Loop", "Add the authored edge loop through the active selected edge. Selection also works in read-only maps.", "select-loop", nullptr,
          COMMAND_FLAG_NONE, SelectEdgeTopology<true>, EdgeTopologySelectionState, pWorkspace },
        { "map.select.ring", "Select Ring", "Add the authored edge ring through the active selected edge. Selection also works in read-only maps.", "select-ring", nullptr,
          COMMAND_FLAG_NONE, SelectEdgeTopology<false>, EdgeTopologySelectionState, pWorkspace },
        { "map.mesh.extrude", "Extrude Mesh Face", "Extend one selected authored mesh face by the configured positive distance. The cap retains its face ID; side walls inherit source attributes.", "mesh-extrude", nullptr,
          COMMAND_FLAG_NONE, ExtrudeMeshFace, MeshFaceEditState, pWorkspace },
        { "map.mesh.inset", "Inset Mesh Face", "Inset one selected authored mesh face by moving its corners toward the center. The inner face retains its face ID; this is a radial corner inset.", "mesh-inset", nullptr,
          COMMAND_FLAG_NONE, InsetMeshFace, MeshFaceEditState, pWorkspace },
        { "map.mesh.quad_slice", "Quad Slice Mesh Face", "Slice one selected four-corner mesh face into a U/V grid of editable quads. Neighboring faces receive shared edge vertices; UVs, materials and face ancestry are retained.", "mesh-subdivide", nullptr,
          COMMAND_FLAG_NONE, QuadSlice, QuadSliceState, pWorkspace },
        { "map.brush.to_mesh", "Convert to Mesh", "Convert selected brushes to their actual polygon meshes, retaining materials, UVs, root and face identity. Undo restores the brushes.", "brush-to-mesh", nullptr,
          COMMAND_FLAG_NONE, ConvertBrushSelection, BrushConversionState, pWorkspace },
        { "map.mesh.flip_normals", "Flip Mesh Normals", "Reverse every face of the selected authored meshes. Available in Objects, Groups and Meshes modes; corner UVs and materials stay attached.", "mesh-flip-normals", nullptr,
          COMMAND_FLAG_NONE, FlipMeshNormals, MeshEditState, pWorkspace },
        { "map.mesh.triangulate", "Triangulate Meshes", "Split every polygon of the selected authored meshes into triangles, retaining materials, corner UVs and face ancestry. Already triangulated meshes add no undo step.", "mesh-triangulate", nullptr,
          COMMAND_FLAG_NONE, TriangulateMeshSelection, MeshTriangulationState, pWorkspace },
        { "map.tool.apply_material", "Apply Current Material", "Apply the active material to the selected brush face.", "tool-apply-material", nullptr,
          COMMAND_FLAG_NONE, ApplyFaceMaterial, FaceEditState, pWorkspace },
        { "map.brush.hollow", "Hollow", "Replace selected convex brushes with walls at the configured thickness.", "csg-hollow", nullptr,
          COMMAND_FLAG_NONE, HollowSelection, BrushEditState, pWorkspace },
        { "map.brush.merge", "Merge", "Merge two visible brushes in the same layer and entity into their convex union. Retain the lowest-ID selected brush's name and custom properties, and exact source-face materials and UVs. Nonconvex unions are rejected.", "csg-merge", nullptr,
          COMMAND_FLAG_NONE, MergeSelection, BrushMergeState, pWorkspace },
        { "map.hide.selected", "Hide Selected", "Hide the selection in every view.", "hide-selected", nullptr, COMMAND_FLAG_NONE, HideSelected,
          HasSelectionState, pWorkspace },
        { "map.hide.unselected", "Hide Unselected", "Hide everything but the selection.", "hide-unselected", nullptr, COMMAND_FLAG_NONE, HideUnselected,
          HasSelectionState, pWorkspace },
        { "map.hide.show_all", "Show All", "Show everything hidden.", "show-all", nullptr, COMMAND_FLAG_NONE, ShowAll, HasHiddenState, pWorkspace },
        { "edit.invert_selection", "Invert Selection", "Select every visible object that is not selected.", "select-invert", nullptr, COMMAND_FLAG_NONE,
          InvertSelection, RootSelectionState, pWorkspace },
        { "map.select.same_class", "Select Same Class", "Select every entity of the selected entities' classes.", "select-same-class", nullptr,
          COMMAND_FLAG_NONE, SelectSameClass, HasEntitySelectedState, pWorkspace },
        { "map.tool.extrude", "Extrude Tool", "Push and pull faces along their normals.", "tool-extrude", nullptr, kTool,
          ToolExecute<map_tool_t::EXTRUDE>, ToolState<map_tool_t::EXTRUDE>, pWorkspace },
        { "map.tool.knife", "Knife Tool", "Cut new edges across faces.", "tool-knife", nullptr, kTool,
          ToolExecute<map_tool_t::KNIFE>, ToolState<map_tool_t::KNIFE>, pWorkspace },
        { "map.tool.loop_cut", "Loop Cut Tool", "Cut an edge loop around a mesh.", "tool-loop-cut", nullptr, kTool,
          ToolExecute<map_tool_t::LOOP_CUT>, ToolState<map_tool_t::LOOP_CUT>, pWorkspace },
        { "map.tool.eyedropper", "Eyedropper", "Pick a surface's material as the active material.", "tool-eyedropper", nullptr, kTool,
          ToolExecute<map_tool_t::EYEDROPPER>, ToolState<map_tool_t::EYEDROPPER>, pWorkspace },
        { "map.tool.workplane", "Workplane Tool", "Align the workplane to a face.", "tool-workplane", nullptr, kTool,
          ToolExecute<map_tool_t::WORKPLANE>, ToolState<map_tool_t::WORKPLANE>, pWorkspace },
        { "map.tool.curve", "Curve Tool", "Draw curves and sweep profiles along them.", "tool-curve", nullptr, kTool,
          ToolExecute<map_tool_t::CURVE>, ToolState<map_tool_t::CURVE>, pWorkspace },
    };
    if ( const command_registry_status_t status = EditorCommands_Register( pRegistry, working, std::size( working ) );
         status != command_registry_status_t::OK ) {
        return status;
    }
    // Planned commands: listed with their icons so menus, the palette, and
    // the keymap are complete; disabled until their geometry backends are
    // wired (brush CSG, mesh editing, UVs, terrain, render modes).
    struct planned_t {
        const char *pId;
        const char *pLabel;
        const char *pDescription;
        const char *pIcon;
    };
    static constexpr planned_t kPlanned[]{
        { "map.brush.carve", "Carve", "Cut the selection out of the brushes it touches.", "csg-carve" },
        { "map.brush.snap_to_grid", "Snap Selection to Grid", "Snap the selected vertices to the grid.", "snap-to-grid" },
        { "map.mesh.bevel", "Bevel", "Bevel the selected edges.", "mesh-bevel" },
        { "map.mesh.bridge", "Bridge", "Bridge two selected edge loops or faces.", "mesh-bridge" },
        { "map.mesh.merge", "Merge Vertices", "Weld the selected vertices.", "mesh-merge" },
        { "map.mesh.collapse", "Collapse", "Collapse the selected edges or faces to points.", "mesh-collapse" },
        { "map.mesh.dissolve", "Dissolve", "Remove the selected edges, joining their faces.", "mesh-dissolve" },
        { "map.mesh.split", "Split", "Split the selected faces from the mesh.", "mesh-split" },
        { "map.mesh.slice", "Slice", "Slice the mesh along a plane.", "mesh-slice" },
        { "map.mesh.subdivide", "Subdivide", "Subdivide the selected faces.", "mesh-subdivide" },
        { "map.mesh.smooth", "Smooth", "Apply subdivision-surface smoothing.", "mesh-smooth" },
        { "map.mesh.solidify", "Solidify", "Give the selected faces thickness.", "mesh-solidify" },
        { "map.mesh.fill_hole", "Fill Hole", "Close the selected boundary with a face.", "mesh-fill-hole" },
        { "map.mesh.extrude_edges", "Extrude", "Extrude selected mesh edges. The map edit adapter is not connected yet.", "tool-extrude" },
        { "map.mesh.connect_edges", "Connect", "Connect selected mesh edges. This map operation is not connected yet.", "mesh-slice" },
        { "map.mesh.extend_edges", "Extend", "Extend selected mesh edges. This map operation is not connected yet.", "tool-extrude" },
        { "map.mesh.split_edges", "Split", "Split selected mesh edges. The map edit adapter is not connected yet.", "mesh-split" },
        { "map.mesh.snap_edge_to_edge", "Snap Edge to Edge", "Align selected edges to a target edge. Target picking and the map edit adapter are not connected yet.", "snap-to-grid" },
        { "map.mesh.normals_hard", "Hard Normals", "Set hard edge normals. The authored normal edit adapter is not connected yet.", "mesh-bevel" },
        { "map.mesh.normals_soft", "Soft Normals", "Set soft edge normals. The authored normal edit adapter is not connected yet.", "mesh-smooth" },
        { "map.mesh.normals_default", "Default Normals", "Restore default edge normals. The authored normal edit adapter is not connected yet.", "mesh-flip-normals" },
        { "map.texture.weld_uvs", "Weld UVs", "Weld selected authored corner UVs. The map UV edit adapter is not connected yet.", "texture-lock" },
        { "map.select.ribs", "Select Ribs", "Select mesh edge ribs. The component selection adapter is not connected yet.", "select-ring" },
        { "map.pivot.clear", "Clear Pivot", "Clear the custom editing pivot. The pivot workflow is not connected yet.", "tool-pivot" },
        { "map.tool.edge_cut", "Edge Cut Tool", "Cut mesh edges interactively. Component picking and the gesture adapter are not connected yet.", "tool-knife" },
        { "map.tool.edge_arc", "Edge Arc Tool", "Shape selected mesh edges into an arc. This tool is not connected yet.", "tool-curve" },
        { "map.mesh.radial_align", "Radial Align", "Align selected mesh components radially. This map operation is not connected yet.", "tool-rotate" },
        { "map.mesh.boolean_union", "Union", "Combine the selected meshes.", "boolean-union" },
        { "map.mesh.boolean_subtract", "Subtract", "Subtract the last selected mesh from the others.", "boolean-subtract" },
        { "map.mesh.boolean_intersect", "Intersect", "Keep what the selected meshes share.", "boolean-intersect" },
        { "map.mesh.to_brush", "Convert to Brush", "Convert convex meshes to brushes.", "mesh-to-brush" },
        { "map.select.grow", "Grow Selection", "Add the elements around the selection.", "select-grow" },
        { "map.select.shrink", "Shrink Selection", "Remove the selection's outer elements.", "select-shrink" },
        { "map.select.touching", "Select Touching", "Select objects touching the selection.", "select-touching" },
        { "map.select.same_material", "Select Same Material", "Select faces with the selected faces' material.", "select-same-material" },
        { "map.transform.flip_horizontal", "Flip Horizontally", "Mirror the selection across the view's vertical axis.", "transform-flip-horizontal" },
        { "map.transform.flip_vertical", "Flip Vertically", "Mirror the selection across the view's horizontal axis.", "transform-flip-vertical" },
        { "map.transform.rotate_cw", "Rotate 90 Clockwise", "Rotate the selection 90 degrees clockwise.", "transform-rotate-cw" },
        { "map.transform.rotate_ccw", "Rotate 90 Counter-Clockwise", "Rotate the selection 90 degrees counter-clockwise.", "transform-rotate-ccw" },
        { "map.align.left", "Align Left", "Align the selection's left sides.", "align-left" },
        { "map.align.right", "Align Right", "Align the selection's right sides.", "align-right" },
        { "map.align.top", "Align Top", "Align the selection's tops.", "align-top" },
        { "map.align.bottom", "Align Bottom", "Align the selection's bottoms.", "align-bottom" },
        { "map.group.create", "Group", "Group the selection.", "group-create" },
        { "map.group.ungroup", "Ungroup", "Ungroup the selected groups.", "group-ungroup" },
        { "map.prefab.create", "Create Prefab", "Save the selection as a prefab.", "prefab-create" },
        { "map.layer.create", "New Layer", "Add a layer.", "layer-new" },
        { "map.visgroup.create", "New Visgroup from Selection", "Make a visgroup of the selection.", "visgroup-new" },
        { "map.texture.fit", "Fit", "Fit the material to the selected faces.", "uv-fit" },
        { "map.texture.align_world", "Align to World", "Align the material to the world axes.", "uv-align-world" },
        { "map.texture.align_face", "Align to Face", "Align the material to each face.", "uv-align-face" },
        { "map.texture.justify_left", "Justify Left", "Justify the material to the face's left edge.", "uv-justify-left" },
        { "map.texture.justify_right", "Justify Right", "Justify the material to the face's right edge.", "uv-justify-right" },
        { "map.texture.justify_top", "Justify Top", "Justify the material to the face's top edge.", "uv-justify-top" },
        { "map.texture.justify_bottom", "Justify Bottom", "Justify the material to the face's bottom edge.", "uv-justify-bottom" },
        { "map.texture.justify_center", "Justify Center", "Centre the material on the face.", "uv-justify-center" },
        { "map.texture.rotate", "Rotate Material", "Rotate the material on the selected faces.", "uv-rotate" },
        { "map.texture.scale", "Scale Material", "Scale the material on the selected faces.", "uv-scale" },
        { "map.texture.shift", "Shift Material", "Shift the material on the selected faces.", "uv-shift" },
        { "map.texture.unwrap", "UV Islands", "Unwrap the selected faces into islands.", "uv-islands" },
        { "map.texture.replace", "Replace Materials...", "Replace one material with another across the map.", "material-replace" },
        { "map.texture.scale_lock", "Texture Scale Lock", "Textures scale with brushes.", "texture-scale-lock" },
        { "map.terrain.raise", "Raise Terrain", "Raise terrain under the brush.", "terrain-raise" },
        { "map.terrain.lower", "Lower Terrain", "Lower terrain under the brush.", "terrain-lower" },
        { "map.terrain.smooth", "Smooth Terrain", "Smooth terrain under the brush.", "terrain-smooth" },
        { "map.terrain.flatten", "Flatten Terrain", "Flatten terrain under the brush.", "terrain-flatten" },
        { "map.terrain.paint", "Paint Terrain", "Paint terrain materials.", "terrain-paint" },
        // map.view.top/front/side/perspective/maximize/cycle_2d act on the
        // views and are registered by the application that owns them.
        // map.render.* act on the views' active pane and are registered by
        // the application that owns the views.
        { "map.compile", "Compile...", "Compile the map.", "map-compile" },
        { "map.stop", "Stop Game", "Stop the running game.", "map-stop" },
        { "map.leak.show", "Show Leak", "Show the path of the last leak.", "map-leak" },
    };
    std::vector<command_desc_t> planned;
    planned.reserve( std::size( kPlanned ) );
    for ( const planned_t &entry : kPlanned ) {
        planned.push_back( { entry.pId, entry.pLabel, entry.pDescription, entry.pIcon, nullptr, COMMAND_FLAG_NONE, NotYetAvailable, NeverEnabled, pWorkspace } );
    }
    return EditorCommands_Register( pRegistry, planned.data(), planned.size() );
}

} // namespace cypher::editor::map
