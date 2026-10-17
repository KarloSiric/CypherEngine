//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ViewMenu.cpp
//  Purpose: Implements the right-click editing menu of the map views.
//
//  History:
//  - Created by Karlo Siric on 2026-10-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_ViewMenu.h"
#include "CypherMapGui_EntityIo.h"
#include "CypherMapGui_GeometryCommands.h"

#include "CypherEditorGui_Actions.h"
#include "CypherEditorGui_Style.h"
#include "CypherEditor_Keymap.h"
#include "CypherMap_Clipboard.h"
#include "CypherMap_ObjectCreate.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QMimeData>

#include <algorithm>
#include <string>
#include <vector>

namespace cypher::editor::map
{
using namespace common;

namespace
{
using vec3 = math::vec3d_t;

QString Keys( const map_workspace_t *ws, const char *command )
{
    const gui::editor_gui_t &gui = *ws->pGui;
    if ( gui.nKeymapChain == 0u ) { return {}; }
    for ( const char *context : { "map", "global" } ) {
        keymap_binding_t binding{};
        if ( EditorKeymap_FindBindingOn( gui.keymapChain, gui.nKeymapChain, EditorKeymap_HostPlatform(), StringView_FromCString( context ),
                                         StringView_FromCString( command ), &binding ) == keymap_lookup_t::BOUND && binding.nChords > 0u ) {
            return gui::EditorKeyChord_ToKeySequence( binding.chords[0] ).toString( QKeySequence::NativeText );
        }
    }
    return {};
}

// A registered command as a menu item: its label, icon and key, enabled as
// the command says, run through the registry. Unknown commands are skipped.
// With onlyEnabled the item is left out unless it can run now.
QAction *Command( QMenu *menu, map_workspace_t *ws, const char *id, bool onlyEnabled = false )
{
    const auto *command = EditorCommands_Find( &ws->pGui->commands, StringView_FromCString( id ) );
    if ( command == nullptr || MapWorkspace_IsPlannedCommand( &ws->pGui->commands, StringView_FromCString( id ) ) ) { return nullptr; }
    const u32 state = EditorCommands_State( &ws->pGui->commands, StringView_FromCString( id ) );
    const bool enabled = ( state & COMMAND_STATE_ENABLED ) != 0;
    if ( onlyEnabled && !enabled ) { return nullptr; }
    const QString keys = Keys( ws, id );
    QString label = QString::fromUtf8( command->pLabel );
    if ( !keys.isEmpty() ) { label += QLatin1Char( '\t' ) + keys; }
    QAction *action = menu->addAction( label );
    action->setObjectName( QString::fromLatin1( id ) );
    if ( command->pIcon != nullptr ) { action->setIcon( gui::EditorStyle_Icon( ws->pGui->style, command->pIcon ) ); }
    action->setEnabled( enabled );
    QString tip = QString::fromUtf8( command->pDescription != nullptr ? command->pDescription : command->pLabel );
    if ( const char *needs = enabled ? nullptr : MapGeometryCommands_Requirement( id ) ) { tip += QStringLiteral( "\nNeeds %1." ).arg( QString::fromUtf8( needs ) ); }
    action->setToolTip( tip );
    if ( ( state & COMMAND_STATE_CHECKED ) != 0 ) { action->setCheckable( true ); action->setChecked( true ); }
    const std::string target( id );
    QObject::connect( action, &QAction::triggered, menu, [ws, target]() {
        ( void )EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( target.c_str() ), {} );
    } );
    return action;
}

void Commands( QMenu *menu, map_workspace_t *ws, std::initializer_list<const char *> ids, bool onlyEnabled = false )
{
    for ( const char *id : ids ) {
        if ( id == nullptr ) { if ( !menu->isEmpty() && !menu->actions().back()->isSeparator() ) { menu->addSeparator(); } continue; }
        ( void )Command( menu, ws, id, onlyEnabled );
    }
}

QMenu *Submenu( QMenu *menu, map_workspace_t *ws, const char *title, const char *icon, std::initializer_list<const char *> ids )
{
    QMenu *sub = menu->addMenu( QString::fromLatin1( title ) );
    if ( icon != nullptr ) { sub->menuAction()->setIcon( gui::EditorStyle_Icon( ws->pGui->style, icon ) ); }
    sub->setToolTipsVisible( true );
    Commands( sub, ws, ids );
    if ( sub->isEmpty() ) { menu->removeAction( sub->menuAction() ); }
    return sub;
}

// What the current mode can do with what is selected, enabled items only:
// the short list a right click should offer first.
void ModeOperations( QMenu *menu, map_workspace_t *ws )
{
    switch ( ws->elementMode ) {
        case map_element_mode_t::VERTICES:
            Commands( menu, ws, { "map.mesh.merge", "map.mesh.collapse", "map.mesh.bevel", "map.mesh.dissolve", "map.mesh.connect_edges", "map.mesh.fill_hole", "map.mesh.offset", "map.mesh.project",
                                  "map.mesh.flatten", "map.mesh.flatten_x", "map.mesh.flatten_y", "map.mesh.flatten_z", "map.mesh.radial_align",
                                  "map.texture.weld_uvs", "map.brush.snap_to_grid", nullptr, "map.select.grow", "map.select.shrink", "map.select.connected",
                                  "map.select.path", "map.select.invert_components", "map.select.to_edges" }, true );
            break;
        case map_element_mode_t::EDGES:
            Commands( menu, ws, { "map.mesh.bevel", "map.mesh.extrude_edges", "map.mesh.extend_edges", "map.mesh.bridge", "map.mesh.fill_hole",
                                  "map.mesh.connect_edges", "map.mesh.split_edges", "map.mesh.dissolve", "map.mesh.collapse", "map.mesh.merge",
                                  "map.mesh.snap_edge_to_edge", "map.mesh.slide_loop", "map.mesh.offset", "map.mesh.flatten", nullptr,
                                  "map.mesh.normals_hard", "map.mesh.normals_soft", "map.mesh.normals_default", "map.texture.weld_uvs", nullptr,
                                  "map.select.loop", "map.select.ring", "map.select.ribs", "map.select.grow", "map.select.shrink", "map.select.connected",
                                  "map.select.boundary_edges", "map.select.sharp_edges", "map.select.invert_components", "map.select.to_vertices" }, true );
            break;
        case map_element_mode_t::FACES:
            Commands( menu, ws, { "map.mesh.extrude", "map.mesh.inset", "map.mesh.quad_slice", "map.mesh.bridge", "map.mesh.split",
                                  "map.mesh.offset", "map.mesh.flatten", "map.mesh.collapse", "map.mesh.delete_face", "map.mesh.thicken_faces", "map.mesh.extract_faces",
                                  "map.brush.extrude_new", "map.brush.sweep", "map.patch.from_face", "map.mesh.create_displacement",
                                  "map.mesh.flip_normals", "map.mesh.triangulate", "map.mesh.smoothing_set", "map.mesh.smoothing_clear", nullptr,
                                  "map.tool.apply_material", "map.texture.fast", "map.texture.hotspot", "map.texture.wrap_coplanar", "map.texture.fit",
                                  "map.texture.align_world", "map.texture.align_face", "map.texture.project_planar", "map.texture.project_box", "map.texture.unwrap",
                                  nullptr, "map.select.grow", "map.select.shrink",
                                  "map.select.connected", "map.select.coplanar", "map.select.faces_facing", "map.select.same_corners",
                                  "map.select.faces_material", "map.select.face_path", "map.select.invert_components" }, true );
            break;
        default:
            if ( MapEntityIo_Entity( ws ) != 0 ) { Commands( menu, ws, { "map.entity.io" }, true ); }
            Commands( menu, ws, { "view.properties.open", nullptr, "map.group.create", "map.group.ungroup", "map.group.add", "map.group.remove", "map.group.select", nullptr,
                                  "map.entity.tie", "map.entity.move_to_world", nullptr,
                                  "map.brush.to_mesh", "map.mesh.to_brush", "map.brush.hollow", "map.brush.carve", "map.brush.merge",
                                  "map.mesh.merge", "map.mesh.split", "map.mesh.boolean_union", "map.mesh.boolean_subtract", "map.mesh.boolean_intersect", "map.mesh.boolean_exclusive", "map.mesh.boolean_split",
                                  "map.mesh.smooth", "map.mesh.subdivide", "map.mesh.solidify", "map.mesh.flip_normals", "map.mesh.triangulate", "map.mesh.weld",
                                  "map.texture.project_box", "map.texture.project_planar", "map.texture.project_cylinder", "map.patch.thicken", "map.patch.cap" }, true );
            break;
    }
}

// Entity classes worth offering: the tool's default, then every class the
// map already uses (game profiles are not decoded yet).
std::vector<std::string> EntityClasses( const map_workspace_t *ws )
{
    std::vector<std::string> classes;
    const auto fallback = EditorSettings_Text( &ws->pGui->settings, "editor.map.default_point_class", StringView_FromCString( "info_player_start" ) );
    classes.emplace_back( fallback.pData, fallback.cchLength );
    std::vector<std::string> used;
    for ( usize i = 0; i < ws->wire.entities.nCount; ++i ) {
        const std::string name( ws->wire.entities.pData[i].className );
        if ( !name.empty() && std::find( used.begin(), used.end(), name ) == used.end() ) { used.push_back( name ); }
    }
    std::sort( used.begin(), used.end() );
    for ( const auto &name : used ) { if ( name != classes.front() ) { classes.push_back( name ); } }
    for ( const char *common : { "light", "info_player_start", "path_corner" } ) {
        if ( std::find( classes.begin(), classes.end(), common ) == classes.end() ) { classes.emplace_back( common ); }
    }
    return classes;
}

QString PointText( vec3 p ) { return QStringLiteral( "(%1, %2, %3)" ).arg( p.x, 0, 'f', 0 ).arg( p.y, 0, 'f', 0 ).arg( p.z, 0, 'f', 0 ); }
} // namespace

void MapViewMenu_SelectClicked( map_workspace_t *ws, u64 object ) noexcept
{
    if ( ws == nullptr || object == 0 || MapWorkspace_IsSelected( ws, object ) ) { return; }
    if ( ws->elementMode != map_element_mode_t::OBJECTS && ws->elementMode != map_element_mode_t::GROUPS && ws->elementMode != map_element_mode_t::MESHES ) { return; }
    MapWorkspace_Select( ws, object, MAP_SELECT_REPLACE );
}

bool MapViewMenu_PasteAt( map_workspace_t *ws, const map_tool_event_t &click, vec3 point ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || ws->pDocument->bReadOnly ) { return false; }
    const QMimeData *data = QApplication::clipboard()->mimeData();
    if ( data == nullptr || !data->hasFormat( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ) ) { return false; }
    const QByteArray bytes = data->data( QString::fromLatin1( MAP_CLIPBOARD_MIME ) );
    const string_view_t text{ bytes.constData(), static_cast<usize>( bytes.size() ) };
    map_bounds_t bounds{};
    if ( MapClipboard_ReadBounds( ws->pGui->pAllocator, text, &bounds ) != map_status_t::OK || !bounds.bHas ) { return false; }
    const vec3 centre = MapBounds_Center( bounds );
    vec3 offset{ point.x - centre.x, point.y - centre.y, point.z - centre.z };
    if ( click.view.bOrtho ) {
        // Only the pane's two axes move; the depth stays where it was copied.
        const u32 depth = 3u - click.view.axisU - click.view.axisV;
        ( depth == 0 ? offset.x : depth == 1 ? offset.y : offset.z ) = 0.0;
    } else {
        offset.z = point.z - bounds.box.minimum.z; // Standing on the clicked surface.
    }
    struct job_t { string_view_t text; vec3 offset; } job{ text, offset };
    return MapWorkspace_ApplyEdit( ws, "Paste Here", []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
        auto &j = *static_cast<job_t *>( context );
        return MapClipboard_Paste( copy, j.text, j.offset, out );
    }, &job );
}

bool MapViewMenu_PlaceEntityAt( map_workspace_t *ws, string_view_t className, vec3 point ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr || ws->pDocument->bReadOnly || className.cchLength == 0 ) { return false; }
    struct job_t { string_view_t className; vec3 point; } job{ className, point };
    return MapWorkspace_ApplyMetadataEdit( ws, "Place Entity", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        auto &j = *static_cast<job_t *>( context );
        u64 id = 0;
        const auto status = MapCreate_Entity( copy, j.className, j.point, {}, &id );
        if ( status != map_status_t::OK ) { return status; }
        *changed = true;
        return Vector_PushBack( out, id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
    }, &job );
}

QMenu *MapViewMenu_Create( map_workspace_t *ws, const map_tool_event_t &click, QMenu *pViewOptions, QWidget *pParent ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr ) { return nullptr; }
    auto *menu = new QMenu( pParent );
    menu->setObjectName( QStringLiteral( "MapViewMenu" ) );
    menu->setAttribute( Qt::WA_DeleteOnClose );
    menu->setToolTipsVisible( true );
    vec3 point{};
    const bool hasPoint = MapToolGestures_PlacePoint( ws, click, &point );

    // 1. What this mode can do right now.
    ModeOperations( menu, ws );
    if ( !menu->isEmpty() ) { menu->addSeparator(); }

    // 2. Clipboard, with Paste Here at the click.
    Commands( menu, ws, { "edit.cut", "edit.copy", "edit.paste" } );
    QAction *pasteHere = menu->addAction( gui::EditorStyle_Icon( ws->pGui->style, "edit-paste" ), QStringLiteral( "Paste Here" ) );
    pasteHere->setObjectName( QStringLiteral( "map.view.paste_here" ) );
    const QMimeData *clipboard = QApplication::clipboard()->mimeData();
    pasteHere->setEnabled( hasPoint && clipboard != nullptr && clipboard->hasFormat( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ) &&
                           ws->pDocument != nullptr && !ws->pDocument->bReadOnly );
    pasteHere->setToolTip( QStringLiteral( "Paste the copied objects centred on %1." ).arg( PointText( point ) ) );
    QObject::connect( pasteHere, &QAction::triggered, menu, [ws, click, point]() { ( void )MapViewMenu_PasteAt( ws, click, point ); } );
    Commands( menu, ws, { "edit.paste_special", "edit.duplicate", "edit.delete" } );
    menu->addSeparator();

    // 3. Creation at the click.
    QMenu *place = menu->addMenu( gui::EditorStyle_Icon( ws->pGui->style, "tool-entity" ), QStringLiteral( "Place Entity Here" ) );
    place->setObjectName( QStringLiteral( "MapViewMenuPlaceEntity" ) );
    place->setEnabled( hasPoint && ws->pDocument != nullptr && !ws->pDocument->bReadOnly );
    for ( const auto &className : EntityClasses( ws ) ) {
        QAction *action = place->addAction( QString::fromStdString( className ) );
        QObject::connect( action, &QAction::triggered, menu, [ws, className, point]() {
            ( void )MapViewMenu_PlaceEntityAt( ws, { className.data(), className.size() }, point );
        } );
    }
    QAction *prefab = menu->addAction( gui::EditorStyle_Icon( ws->pGui->style, "prefab-create" ), QStringLiteral( "Insert Prefab Here..." ) );
    prefab->setEnabled( hasPoint && ( EditorCommands_State( &ws->pGui->commands, StringView_FromCString( "map.prefab.insert" ) ) & COMMAND_STATE_ENABLED ) != 0 );
    QObject::connect( prefab, &QAction::triggered, menu, [ws, point]() {
        // Insert Prefab places at the 2D cursor; aim it at the click first.
        ws->cursor = point; ws->cursorAxes = 7u;
        ( void )EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.prefab.insert" ), {} );
    } );
    menu->addSeparator();

    // 4. Everything else, grouped.
    Submenu( menu, ws, "Select", "select-all", { "edit.select_all", "edit.select_none", "edit.invert_selection", nullptr, "map.select.touching",
        "map.select.inside", "map.select.tall", "map.select.same_material", "map.select.same_class", nullptr, "map.select.grow", "map.select.shrink",
        "map.select.connected", "map.select.loop", "map.select.ring", "map.select.invert_components" } );
    Submenu( menu, ws, "Mode", "select-objects", { "map.select_mode.vertices", "map.select_mode.edges", "map.select_mode.faces",
        "map.select_mode.meshes", "map.select_mode.objects", "map.select_mode.groups" } );
    Submenu( menu, ws, "Object", "select-objects", { "map.transform.dialog", nullptr, "map.group.create", "map.group.ungroup", "map.group.add", "map.group.remove", "map.group.select", nullptr,
        "map.hide.selected", "map.hide.unselected", "map.hide.show_all", nullptr, "map.layer.create", "map.visgroup.create", "map.selection_set.create", "map.prefab.create", nullptr,
        "map.transform.flip_horizontal", "map.transform.flip_vertical", "map.transform.rotate_cw", "map.transform.rotate_ccw", nullptr,
        "map.align.left", "map.align.right", "map.align.top", "map.align.bottom", "map.brush.snap_to_grid" } );
    Submenu( menu, ws, "CSG", "csg-carve", { "map.brush.hollow", "map.brush.make_room", "map.brush.carve", "map.brush.merge", "map.brush.extrude_new",
        nullptr, "map.brush.to_mesh", "map.mesh.to_brush", nullptr, "map.mesh.boolean_union", "map.mesh.boolean_subtract", "map.mesh.boolean_intersect", "map.mesh.boolean_exclusive", "map.mesh.boolean_split" } );
    Submenu( menu, ws, "Mesh", "mesh-merge", { "map.mesh.extrude", "map.mesh.inset", "map.mesh.bevel", "map.mesh.bridge", nullptr,
        "map.mesh.merge", "map.mesh.collapse", "map.mesh.dissolve", "map.mesh.split", "map.mesh.delete_face", nullptr, "map.mesh.slice",
        "map.mesh.quad_slice", "map.mesh.subdivide", "map.mesh.smooth", "map.mesh.solidify", nullptr, "map.mesh.offset", "map.mesh.flatten",
        "map.mesh.slide_loop", "map.mesh.snap_edge_to_edge", nullptr, "map.mesh.triangulate", "map.mesh.flip_normals", "map.mesh.fill_hole", nullptr,
        "map.mesh.weld", "map.mesh.merge_by_distance" } );
    Submenu( menu, ws, "Texture", "tool-texture", { "map.tool.apply_material", "map.texture.replace", nullptr, "map.texture.fit",
        "map.texture.align_world", "map.texture.align_face", nullptr, "map.texture.justify_left", "map.texture.justify_right", "map.texture.justify_top",
        "map.texture.justify_bottom", "map.texture.justify_center", nullptr, "map.texture.rotate", "map.texture.scale", "map.texture.shift",
        "map.texture.unwrap", nullptr, "map.texture.lock", "map.texture.scale_lock" } );
    Submenu( menu, ws, "Patch", "tool-patch", { "map.patch.insert_row", "map.patch.insert_column", "map.patch.remove_row", "map.patch.remove_column",
        "map.patch.transpose", "map.patch.invert", "map.patch.thicken", "map.patch.cap" } );
    Submenu( menu, ws, "Entity", "entity-logic", { "map.entity.io", "view.properties.open" } );
    menu->addSeparator();

    // 5. Where: pivot and views.
    QAction *pivot = menu->addAction( gui::EditorStyle_Icon( ws->pGui->style, "tool-pivot" ), QStringLiteral( "Set Pivot Here" ) );
    pivot->setObjectName( QStringLiteral( "map.view.pivot_here" ) );
    pivot->setEnabled( hasPoint );
    pivot->setToolTip( QStringLiteral( "Rotate, Scale, Rotate 90 and the flips turn about %1." ).arg( PointText( point ) ) );
    QObject::connect( pivot, &QAction::triggered, menu, [ws, point]() { MapToolGestures_SetPivot( ws, point ); } );
    Commands( menu, ws, { "map.pivot.clear" } );
    menu->addSeparator();
    Submenu( menu, ws, "Go To", "go-to", { "map.view.center_selection_2d", "map.view.center_selection_3d", "map.view.frame_all", "map.go_to" } );
    if ( pViewOptions != nullptr ) {
        QAction *options = menu->addMenu( pViewOptions );
        options->setText( QStringLiteral( "View Options" ) );
    }
    return menu;
}

} // namespace cypher::editor::map
