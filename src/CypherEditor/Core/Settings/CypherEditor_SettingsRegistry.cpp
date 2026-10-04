//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_SettingsRegistry.cpp
//  Purpose: Implements the settings registry, scope resolution, writes, and
//           the framework's settings catalogue.
//  Details: The catalogue below is CYSETTINGS.md section 4 - same paths,
//           defaults, limits, and pages - minus the map workspace's own
//           editor.map section, which that workspace registers, and the
//           plugin trust list, which the trust prompt writes directly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_SettingsRegistry.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <algorithm>
#include <cstring>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

// ---------------------------------------------------------------------------
// Catalogue builders
// ---------------------------------------------------------------------------

constexpr setting_descriptor_t Flag( const char *pPath, bool_t bDefault, const char *pLabel, const char *pDescription, const char *pPage ) noexcept
{
    setting_descriptor_t d{};
    d.pPath = pPath;
    d.type = setting_type_t::BOOL;
    d.bDefault = bDefault;
    d.pLabel = pLabel;
    d.pDescription = pDescription;
    d.pPage = pPage;
    return d;
}

constexpr setting_descriptor_t Integer( const char *pPath, i64 nDefault, i64 nMin, i64 nMax, const char *pLabel, const char *pDescription,
                                        const char *pPage ) noexcept
{
    setting_descriptor_t d{};
    d.pPath = pPath;
    d.type = setting_type_t::INTEGER;
    d.nDefault = nDefault;
    d.nMin = nMin;
    d.nMax = nMax;
    d.pLabel = pLabel;
    d.pDescription = pDescription;
    d.pPage = pPage;
    return d;
}

constexpr setting_descriptor_t Real( const char *pPath, f64 flDefault, f64 flMin, f64 flMax, const char *pLabel, const char *pDescription,
                                     const char *pPage ) noexcept
{
    setting_descriptor_t d{};
    d.pPath = pPath;
    d.type = setting_type_t::REAL;
    d.flDefault = flDefault;
    d.flMin = flMin;
    d.flMax = flMax;
    d.pLabel = pLabel;
    d.pDescription = pDescription;
    d.pPage = pPage;
    return d;
}

constexpr setting_descriptor_t Text( const char *pPath, const char *pDefault, usize cbMax, const char *pLabel, const char *pDescription,
                                     const char *pPage ) noexcept
{
    setting_descriptor_t d{};
    d.pPath = pPath;
    d.type = setting_type_t::STRING;
    d.pDefaultText = pDefault;
    d.cbMaxText = cbMax;
    d.pLabel = pLabel;
    d.pDescription = pDescription;
    d.pPage = pPage;
    return d;
}

template <usize nValues>
constexpr setting_descriptor_t Choice( const char *pPath, const char *const ( &ppValues )[nValues], const char *pDefault, const char *pLabel,
                                       const char *pDescription, const char *pPage ) noexcept
{
    setting_descriptor_t d{};
    d.pPath = pPath;
    d.type = setting_type_t::ENUM;
    d.ppEnumValues = ppValues;
    d.nEnumValues = nValues;
    d.pDefaultText = pDefault;
    d.pLabel = pLabel;
    d.pDescription = pDescription;
    d.pPage = pPage;
    return d;
}

constexpr const char *kLanguages[]{ "en" };
constexpr const char *kAntialiasing[]{ "off", "2x", "4x", "8x" };
constexpr const char *kTextureFilters[]{ "nearest", "linear", "anisotropic" };
constexpr const char *kWhen[]{ "never", "selected", "always" };
constexpr const char *kLineEndings[]{ "lf", "crlf" };
constexpr const char *kLogLevels[]{ "trace", "debug", "info", "warning", "error" };

constexpr const char *kGeneral = "General";
constexpr const char *kInterface = "Interface";
constexpr const char *kPalette = "Interface/Command Palette";
constexpr const char *kViewports = "Viewports";
constexpr const char *kGizmos = "Viewports/Selection and Gizmos";
constexpr const char *kCamera = "Viewports/Camera";
constexpr const char *kGrid = "Viewports/Grid and Snapping";
constexpr const char *kDisplay2D = "Viewports/2D Display";
constexpr const char *kDisplay3D = "Viewports/3D Display";
constexpr const char *kFiles = "Files";
constexpr const char *kConsole = "Console";
constexpr const char *kPerformance = "Performance";
constexpr const char *kPlugins = "Plugins";

constexpr setting_descriptor_t kFramework[]{
    // 4.1 General.
    Choice( "editor.general.language", kLanguages, "en", "Language", "Interface language.", kGeneral ),
    Integer( "editor.general.autosave_minutes", 5, 0, 120, "Autosave interval", "Minutes between autosaves; 0 disables autosave.", kGeneral ),
    Integer( "editor.general.autosave_keep", 10, 1, 100, "Autosave copies", "Autosave copies kept per document.", kGeneral ),
    Integer( "editor.general.undo_levels", 500, 10, 100000, "Undo levels", "Undo steps kept per document.", kGeneral ),
    Integer( "editor.general.recent_files", 16, 0, 64, "Recent files", "Entries in recent file lists.", kGeneral ),
    Flag( "editor.general.restore_session", CY_TRUE, "Restore session", "Reopen the workspace's documents on start.", kGeneral ),
    Flag( "editor.general.confirm_exit", CY_TRUE, "Confirm exit", "Ask before exiting with unsaved changes.", kGeneral ),
    Integer( "editor.general.backups_on_save", 3, 0, 32, "Backups on save", "Backup generations kept when saving documents.", kGeneral ),
    // 4.2 Interface.
    Text( "editor.ui.theme", "charcoal", 64u, "Theme", "Active theme (a theme ID).", kInterface ),
    Text( "editor.ui.keymap", "cypher_default", 64u, "Keymap", "Active keymap (a keymap ID).", kInterface ),
    Text( "editor.ui.layout", "mason_default", 64u, "Layout", "Layout new workspaces start with (a layout ID).", kInterface ),
    Flag( "editor.ui.start_maximized", CY_TRUE, "Start maximised", "Open the main window maximised.", kInterface ),
    Flag( "editor.ui.native_menu_bar", CY_FALSE, "System menu bar", "Use the macOS system menu bar instead of a menu bar inside the editor window.", kInterface ),
    Flag( "editor.ui.show_welcome", CY_TRUE, "Welcome window", "Show the welcome window (new map, open, recent maps) when Mason starts without a map.", kInterface ),
    Flag( "editor.ui.show_splash", CY_TRUE, "Startup screen", "Show Mason's startup screen while the interface and project load.", kInterface ),
    Flag( "editor.ui.tree_lines", CY_TRUE, "Tree connections", "Draw connecting lines between outliner parents and children.", kInterface ),
    Flag( "editor.ui.two_toolbar_rows", CY_TRUE, "Keep two toolbar rows", "Consolidate saved upper toolbar placement into two rows. Disable to use custom layout placement.", kInterface ),
    Flag( "editor.ui.tooltips", CY_TRUE, "Tooltips", "Show tooltips.", kInterface ),
    Integer( "editor.ui.tooltip_delay_ms", 600, 0, 5000, "Tooltip delay", "Milliseconds before a tooltip appears.", kInterface ),
    Integer( "editor.ui.command_palette_recent", 12, 0, 64, "Recent commands", "Recent commands shown first in the command palette; 0 disables recent ordering.", kPalette ),
    Integer( "editor.ui.command_palette_rows", 12, 4, 24, "Visible rows", "Number of visible command rows before scrolling. All matching results remain searchable.", kPalette ),
    Flag( "editor.ui.command_palette_show_unavailable", CY_TRUE, "Show unavailable commands", "Include commands that cannot run in the current context, shown dimmed. Disable to focus on available actions.", kPalette ),
    Flag( "editor.ui.confirm_delete", CY_FALSE, "Confirm delete", "Ask before deleting objects.", kInterface ),
    // 4.3 Viewports.
    Flag( "editor.viewport.activate_on_hover", CY_TRUE, "Activate on hover", "The pane under the pointer receives keys without a click.", kViewports ),
    Flag( "editor.viewport.highlight_active", CY_FALSE, "Highlight active pane", "Tint the active pane's header.", kViewports ),
    Flag( "editor.viewport.active_border", CY_TRUE, "Viewport hover outline", "Outline the view under the pointer in red while the pointer is over it.", kViewports ),
    Flag( "editor.viewport.hover_highlight", CY_TRUE, "Hover highlight", "Draw the object under the pointer in the hover colour, so it is clear what a click selects.", kViewports ),
    Real( "editor.viewport.gizmo_scale", 1.0, 0.5, 2.0, "Gizmo size", "Screen-space scale of transform gizmos in 2D and 3D. 1 preserves the standard size; changing it cancels an active edit drag.", kGizmos ),
    Flag( "editor.viewport.show_axes", CY_TRUE, "Show axes", "Axis orientation in all 2D panes; independent of 3D.", kDisplay2D ),
    Flag( "editor.viewport.center_axes", CY_TRUE, "Axes at origin", "Draw world origin axis lines in the 2D panes.", kDisplay2D ),
    Flag( "editor.viewport.show_rulers", CY_TRUE, "Rulers", "Coordinate rulers along 2D panes.", kDisplay2D ),
    Flag( "editor.viewport.show_fps", CY_FALSE, "Frame time", "Frame time readout in the 3D pane.", kViewports ),
    Flag( "editor.viewport.show_metrics", CY_FALSE, "Viewport metrics", "Show pixel scale and object counts in the 2D panes.", kDisplay2D ),
    Flag( "editor.viewport.show_selection_bounds", CY_TRUE, "Selection bounds", "Draw selection bounds in the 2D panes.", kDisplay2D ),
    Flag( "editor.viewport.show_selection_dimensions", CY_TRUE, "Selection dimensions", "Show selection dimensions in the 2D panes.", kDisplay2D ),
    Flag( "editor.viewport.show_selection_vertices", CY_FALSE, "Selection vertices", "Draw selected geometry vertices in the 2D panes.", kDisplay2D ),
    Choice( "editor.viewport.antialiasing", kAntialiasing, "4x", "Antialiasing", "Multisampling of the 3D view.", kViewports ),
    Choice( "editor.viewport.texture_filter", kTextureFilters, "anisotropic", "Texture filtering", "3D texture filtering.", kViewports ),
    Integer( "editor.viewport.max_fps", 0, 0, 1000, "Frame cap", "Frames per second; 0 follows the display.", kViewports ),
    Real( "editor.viewport.near_plane", 1.0, 0.01, 64.0, "Near clip", "3D near clip distance (units).", kViewports ),
    Real( "editor.viewport.far_plane", 32768.0, 256.0, 1048576.0, "Far clip", "3D far clip distance (units).", kViewports ),
    Choice( "editor.viewport.entity_names", kWhen, "always", "Entity names", "When entity names are drawn in the 2D panes.", kDisplay2D ),
    Choice( "editor.viewport.io_lines", kWhen, "selected", "Output connections", "When output connections are drawn in the 2D panes.", kDisplay2D ),
    Choice( "editor.viewport.helpers", kWhen, "selected", "Helpers", "When radius and cone helpers are drawn in the 2D panes.", kDisplay2D ),
    Flag( "editor.viewport.models_in_2d", CY_TRUE, "Models in 2D", "Draw model wireframes in the 2D views.", kDisplay2D ),
    Flag( "editor.viewport.ghost_hidden", CY_FALSE, "Ghost hidden objects", "Draw hidden objects dimmed in the 2D panes.", kDisplay2D ),
    // Perspective drawing aids intentionally do not inherit orthographic choices.
    Flag( "editor.viewport.perspective.show_axes", CY_TRUE, "Show axes", "Show the 3D axis orientation; independent of 2D.", kDisplay3D ),
    Flag( "editor.viewport.perspective.center_axes", CY_TRUE, "Axes at origin", "Draw the X, Y and Z world origin lines in 3D.", kDisplay3D ),
    Flag( "editor.viewport.perspective.show_metrics", CY_FALSE, "Viewport metrics", "Show camera coordinates and object counts in 3D.", kDisplay3D ),
    Flag( "editor.viewport.perspective.show_selection_bounds", CY_TRUE, "Selection bounds", "Draw selection bounds in 3D.", kDisplay3D ),
    Flag( "editor.viewport.perspective.show_selection_dimensions", CY_TRUE, "Selection dimensions", "Show selection dimensions in 3D.", kDisplay3D ),
    Flag( "editor.viewport.perspective.show_selection_vertices", CY_FALSE, "Selection vertices", "Draw selected geometry vertices in 3D.", kDisplay3D ),
    Choice( "editor.viewport.perspective.entity_names", kWhen, "selected", "Entity names", "When entity names are drawn in 3D.", kDisplay3D ),
    Choice( "editor.viewport.perspective.io_lines", kWhen, "selected", "Output connections", "When output connections are drawn in 3D.", kDisplay3D ),
    Flag( "editor.viewport.perspective.ghost_hidden", CY_FALSE, "Ghost hidden objects", "Draw hidden objects dimmed in 3D.", kDisplay3D ),
    // 4.4 Camera.
    Real( "editor.camera.fov", 75.0, 20.0, 130.0, "Field of view", "3D field of view (degrees).", kCamera ),
    Real( "editor.camera.move_speed", 1000.0, 10.0, 100000.0, "Move speed", "Fly speed (units per second).", kCamera ),
    Real( "editor.camera.acceleration", 0.15, 0.0, 2.0, "Acceleration", "Seconds to reach full speed; 0 is instant.", kCamera ),
    Real( "editor.camera.fast_multiplier", 4.0, 1.0, 100.0, "Fast multiplier", "Speed while the fast key is held.", kCamera ),
    Real( "editor.camera.slow_multiplier", 0.25, 0.01, 1.0, "Slow multiplier", "Speed while the slow key is held.", kCamera ),
    Real( "editor.camera.look_sensitivity", 0.2, 0.01, 5.0, "Look sensitivity", "Degrees per pixel of mouse travel.", kCamera ),
    Real( "editor.camera.pan_sensitivity", 1.0, 0.05, 10.0, "Pan sensitivity", "Pan speed scale.", kCamera ),
    Real( "editor.camera.zoom_sensitivity", 1.0, 0.05, 10.0, "Zoom sensitivity", "Wheel zoom scale.", kCamera ),
    Flag( "editor.camera.invert_y", CY_FALSE, "Invert look", "Invert vertical mouse look.", kCamera ),
    Flag( "editor.camera.invert_wheel", CY_FALSE, "Invert wheel", "Invert wheel zoom.", kCamera ),
    Flag( "editor.camera.zoom_to_cursor", CY_TRUE, "Zoom to cursor", "Zoom toward the pointer.", kCamera ),
    Flag( "editor.camera.orbit_selection", CY_TRUE, "Orbit selection", "Orbit around the selection when there is one.", kCamera ),
    Flag( "editor.camera.link_2d_views", CY_FALSE, "Link 2D views", "2D panes share pan and zoom.", kCamera ),
    Real( "editor.camera.frame_margin", 1.15, 1.0, 2.0, "Framing padding", "Space around geometry when framing selection or all objects. 1 fits tightly; larger values leave more room.", kCamera ),
    // 4.5 Grid and snapping.
    Integer( "editor.grid.size", 16, 1, 4096, "Grid size", "Grid size in units; [ and ] halve and double it.", kGrid ),
    Flag( "editor.grid.snap", CY_TRUE, "Snap to grid", "Snap to the grid.", kGrid ),
    Flag( "editor.grid.show", CY_TRUE, "Show grid", "Draw the 2D grid.", kGrid ),
    Flag( "editor.grid.show_3d", CY_TRUE, "Show 3D grid", "Draw the ground grid in perspective views.", kGrid ),
    Flag( "editor.grid.show_surface_3d", CY_TRUE, "Surface grid", "Draw a world-aligned construction grid on filled 3D surfaces, independently of the floor grid.", kDisplay3D ),
    Flag( "editor.grid.adaptive", CY_TRUE, "Adaptive grid", "Hide grid levels whose lines would crowd closer than the minimum spacing.", kGrid ),
    Flag( "editor.grid.adaptive_3d", CY_TRUE, "Adaptive 3D grid", "Adapt perspective grid density independently of the 2D views.", kDisplay3D ),
    Integer( "editor.grid.min_spacing_px", 4, 2, 128, "Minimum line spacing", "Closest grid line spacing drawn (pixels).", kGrid ),
    Integer( "editor.grid.major_every", 8, 2, 64, "Major line every", "Every Nth grid line is major.", kGrid ),
    Integer( "editor.grid.highlight_every", 1024, 0, 65536, "Highlight every", "Highlight lines every N units; 0 disables.", kGrid ),
    Real( "editor.grid.angle_snap", 15.0, 0.0, 90.0, "Angle snap", "Rotation snap in degrees; 0 disables.", kGrid ),
    Real( "editor.grid.scale_snap", 0.25, 0.0, 10.0, "Scale snap", "Scale snap step; 0 disables.", kGrid ),
    // 4.7 Files.
    Flag( "editor.files.watch_external_changes", CY_TRUE, "Watch for changes", "Reload documents changed on disk after asking.", kFiles ),
    Choice( "editor.files.line_endings", kLineEndings, "lf", "Line endings", "Line endings of written text files.", kFiles ),
    Flag( "editor.files.save_on_run", CY_TRUE, "Save on run", "Save the map before building or running it.", kFiles ),
    Text( "editor.assets.roots", "", 1024u, "Content folders",
          "Folders the asset browser lists, highest priority first, separated by ';'. Relative folders resolve against the project; empty uses the project's assets folder.",
          kFiles ),
    Integer( "editor.assets.thumbnail_size", 144, 48, 256, "Asset preview size", "Size of asset thumbnails in grid view (pixels).", "Assets" ),
    Flag( "editor.assets.list_view", CY_FALSE, "Asset list view", "Show assets in a compact list instead of thumbnail tiles.", "Assets" ),
    Text( "editor.assets.saved_searches", "", 8192u, "Saved asset searches",
          "The Asset Browser window's saved searches (name, tab, filter, asset types), as JSON. Edited from the window.", "Assets" ),
    Flag( "editor.outliner.show_ids", CY_FALSE, "Outliner IDs", "Include source object IDs in outliner labels.", kInterface ),
    Flag( "editor.audio.preview_enabled", CY_TRUE, "Sound preview", "Enable audio previews when the editor audio backend is available.", "Audio" ),
    // 4.8 Console.
    Integer( "editor.console.history_size", 200, 0, 10000, "History size", "Remembered command lines.", kConsole ),
    Integer( "editor.console.max_lines", 5000, 100, 1000000, "Maximum lines", "Lines kept in the output.", kConsole ),
    Choice( "editor.console.min_level", kLogLevels, "info", "Lowest level", "Lowest log level shown.", kConsole ),
    Flag( "editor.console.timestamps", CY_FALSE, "Timestamps", "Prefix lines with the time.", kConsole ),
    Flag( "editor.console.channels", CY_TRUE, "Channels", "Prefix lines with the log channel.", kConsole ),
    // 4.9 Performance.
    Integer( "editor.performance.worker_threads", 0, 0, 256, "Worker threads", "Background threads; 0 chooses from the CPU.", kPerformance ),
    Integer( "editor.performance.texture_memory_mb", 2048, 128, 65536, "Texture memory", "Texture cache budget (MiB).", kPerformance ),
    Flag( "editor.performance.background_build", CY_TRUE, "Background builds", "Build maps without blocking editing.", kPerformance ),
    // 4.10 Plugins.
    Flag( "editor.plugins.enabled", CY_TRUE, "Enable plugins", "Load Python plugins at all.", kPlugins ),
};

CYPHER_NODISCARD bool_t DescriptorValid( const setting_descriptor_t &descriptor ) noexcept
{
    settings_path_t path{};
    return descriptor.pPath != nullptr && descriptor.pLabel != nullptr && descriptor.pPage != nullptr &&
           SettingsPath_Parse( StringView_FromCString( descriptor.pPath ), &path ) &&
           Setting_Check( descriptor, Setting_Default( descriptor ) ) == setting_problem_code_t::NONE;
}

void Notify( settings_registry_t *pRegistry, string_view_t path ) noexcept
{
    settings_listener_t snapshot[EDITOR_SETTINGS_MAX_LISTENERS];
    const usize nListeners = pRegistry->nListeners;
    std::copy( pRegistry->listeners, pRegistry->listeners + nListeners, snapshot );
    for ( usize i = 0u; i < nListeners; ++i ) {
        const auto &listener = snapshot[i];
        const bool subscribed = std::any_of( pRegistry->listeners, pRegistry->listeners + pRegistry->nListeners,
            [&]( const settings_listener_t &live ) { return live.pfnChanged == listener.pfnChanged && live.pContext == listener.pContext; } );
        if ( subscribed ) { listener.pfnChanged( listener.pContext, path ); }
    }
}

// Resolution through the scopes from iFirst outwards.
CYPHER_NODISCARD settings_value_source_t ResolveFrom( const settings_registry_t *pRegistry, const setting_descriptor_t &descriptor, usize iFirst ) noexcept
{
    const key_value_t *bases[static_cast<usize>( settings_scope_t::COUNT )]{};
    for ( usize i = iFirst; i < std::size( bases ); ++i ) {
        const settings_document_t *pStore = pRegistry->scopes[i];
        bases[i] = pStore != nullptr ? SettingsDocument_Root( pStore ) : nullptr;
    }
    const setting_resolution_t resolution = Setting_Resolve( bases, std::size( bases ), descriptor, nullptr, 0u );
    settings_value_source_t result{};
    result.value = resolution.value;
    result.nInvalid = resolution.nProblemsRequired;
    result.source = resolution.iScope == CY_INVALID_SIZE ? settings_scope_t::DEFAULT : static_cast<settings_scope_t>( resolution.iScope );
    return result;
}

} // namespace

settings_registry_t::~settings_registry_t() noexcept
{
    EditorSettings_Shutdown( this );
}

settings_registry_status_t EditorSettings_Init( settings_registry_t *pRegistry, const allocator_t *pAllocator ) noexcept
{
    if ( pRegistry == nullptr || pAllocator == nullptr ) { return settings_registry_status_t::INVALID_ARGUMENT; }
    return Vector_Init( &pRegistry->descriptors, pAllocator ) ? settings_registry_status_t::OK : settings_registry_status_t::OUT_OF_MEMORY;
}

void EditorSettings_Shutdown( settings_registry_t *pRegistry ) noexcept
{
    if ( pRegistry == nullptr ) { return; }
    Vector_Shutdown( &pRegistry->descriptors );
    for ( settings_document_t *&pScope : pRegistry->scopes ) { pScope = nullptr; }
    pRegistry->nListeners = 0u;
}

settings_registry_status_t EditorSettings_Register( settings_registry_t *pRegistry, const setting_descriptor_t *pDescriptors, usize nDescriptors ) noexcept
{
    if ( pRegistry == nullptr || ( pDescriptors == nullptr && nDescriptors != 0u ) ) { return settings_registry_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < nDescriptors; ++i ) {
        if ( !DescriptorValid( pDescriptors[i] ) ) { return settings_registry_status_t::INVALID_ARGUMENT; }
        if ( EditorSettings_Find( pRegistry, StringView_FromCString( pDescriptors[i].pPath ) ) != nullptr ) { return settings_registry_status_t::DUPLICATE; }
        for ( usize j = 0u; j < i; ++j ) {
            if ( std::strcmp( pDescriptors[i].pPath, pDescriptors[j].pPath ) == 0 ) { return settings_registry_status_t::DUPLICATE; }
        }
    }
    if ( !Vector_Reserve( &pRegistry->descriptors, pRegistry->descriptors.nCount + nDescriptors ) ) { return settings_registry_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < nDescriptors; ++i ) {
        const bool_t bPushed = Vector_PushBack( &pRegistry->descriptors, &pDescriptors[i] ); // Reserved above.
        CY_ASSERT( bPushed );
        ( void )bPushed;
    }
    return settings_registry_status_t::OK;
}

const setting_descriptor_t *EditorSettings_Find( const settings_registry_t *pRegistry, string_view_t path ) noexcept
{
    if ( pRegistry == nullptr ) { return nullptr; }
    // Registration order is the dialog's order, so lookup is linear; a few
    // hundred settings make that cheaper than keeping a second index.
    for ( usize i = 0u; i < pRegistry->descriptors.nCount; ++i ) {
        if ( StringView_Equals( StringView_FromCString( pRegistry->descriptors.pData[i]->pPath ), path ) ) { return pRegistry->descriptors.pData[i]; }
    }
    return nullptr;
}

usize EditorSettings_Count( const settings_registry_t *pRegistry ) noexcept
{
    return pRegistry != nullptr ? pRegistry->descriptors.nCount : 0u;
}

const setting_descriptor_t *EditorSettings_At( const settings_registry_t *pRegistry, usize iIndex ) noexcept
{
    CY_ASSERT( pRegistry != nullptr && iIndex < pRegistry->descriptors.nCount );
    return pRegistry != nullptr && iIndex < pRegistry->descriptors.nCount ? pRegistry->descriptors.pData[iIndex] : nullptr;
}

void EditorSettings_SetScope( settings_registry_t *pRegistry, settings_scope_t scope, settings_document_t *pStore ) noexcept
{
    CY_ASSERT( pRegistry != nullptr && scope < settings_scope_t::COUNT );
    if ( pRegistry == nullptr || scope >= settings_scope_t::COUNT ) { return; }
    pRegistry->scopes[static_cast<usize>( scope )] = pStore;
    Notify( pRegistry, string_view_t{} );
}

settings_value_source_t EditorSettings_Resolve( const settings_registry_t *pRegistry, const setting_descriptor_t &descriptor ) noexcept
{
    CY_ASSERT( pRegistry != nullptr );
    return ResolveFrom( pRegistry, descriptor, 0u );
}

settings_value_source_t EditorSettings_ResolveInherited( const settings_registry_t *pRegistry, const setting_descriptor_t &descriptor, settings_scope_t scope ) noexcept
{
    CY_ASSERT( pRegistry != nullptr && scope < settings_scope_t::COUNT );
    return ResolveFrom( pRegistry, descriptor, static_cast<usize>( scope ) + 1u );
}

bool_t EditorSettings_Bool( const settings_registry_t *pRegistry, const char *pPath, bool_t fallback ) noexcept
{
    const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
    return pDescriptor != nullptr && pDescriptor->type == setting_type_t::BOOL ? EditorSettings_Resolve( pRegistry, *pDescriptor ).value.bValue : fallback;
}

i64 EditorSettings_Integer( const settings_registry_t *pRegistry, const char *pPath, i64 fallback ) noexcept
{
    const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
    return pDescriptor != nullptr && pDescriptor->type == setting_type_t::INTEGER ? EditorSettings_Resolve( pRegistry, *pDescriptor ).value.nValue
                                                                                  : fallback;
}

f64 EditorSettings_Real( const settings_registry_t *pRegistry, const char *pPath, f64 fallback ) noexcept
{
    const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
    return pDescriptor != nullptr && pDescriptor->type == setting_type_t::REAL ? EditorSettings_Resolve( pRegistry, *pDescriptor ).value.flValue : fallback;
}

string_view_t EditorSettings_Text( const settings_registry_t *pRegistry, const char *pPath, string_view_t fallback ) noexcept
{
    const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
    if ( pDescriptor == nullptr || ( pDescriptor->type != setting_type_t::STRING && pDescriptor->type != setting_type_t::ENUM ) ) { return fallback; }
    return EditorSettings_Resolve( pRegistry, *pDescriptor ).value.text;
}

settings_registry_status_t EditorSettings_Write( settings_registry_t *pRegistry, settings_scope_t scope, const setting_descriptor_t &descriptor, const setting_value_t &value ) noexcept
{
    if ( pRegistry == nullptr || scope >= settings_scope_t::COUNT ) { return settings_registry_status_t::INVALID_ARGUMENT; }
    settings_document_t *pStore = pRegistry->scopes[static_cast<usize>( scope )];
    if ( pStore == nullptr ) { return settings_registry_status_t::NO_SCOPE; }
    if ( Setting_Check( descriptor, value ) != setting_problem_code_t::NONE ) { return settings_registry_status_t::INVALID_ARGUMENT; }
    const settings_value_source_t inherited = EditorSettings_ResolveInherited( pRegistry, descriptor, scope );
    const settings_document_status_t status = Setting_WriteOverride( pStore, descriptor, value, inherited.value );
    if ( status != settings_document_status_t::OK ) {
        return status == settings_document_status_t::OUT_OF_MEMORY ? settings_registry_status_t::OUT_OF_MEMORY : settings_registry_status_t::STORE_FAILED;
    }
    Notify( pRegistry, StringView_FromCString( descriptor.pPath ) );
    return settings_registry_status_t::OK;
}

settings_registry_status_t EditorSettings_Reset( settings_registry_t *pRegistry, settings_scope_t scope, const setting_descriptor_t &descriptor ) noexcept
{
    if ( pRegistry == nullptr || scope >= settings_scope_t::COUNT ) { return settings_registry_status_t::INVALID_ARGUMENT; }
    settings_document_t *pStore = pRegistry->scopes[static_cast<usize>( scope )];
    if ( pStore == nullptr ) { return settings_registry_status_t::NO_SCOPE; }
    settings_path_t path{};
    if ( !SettingsPath_Parse( StringView_FromCString( descriptor.pPath ), &path ) ) { return settings_registry_status_t::INVALID_ARGUMENT; }
    if ( SettingsDocument_Remove( pStore, path ) != settings_document_status_t::OK ) { return settings_registry_status_t::STORE_FAILED; }
    Notify( pRegistry, StringView_FromCString( descriptor.pPath ) );
    return settings_registry_status_t::OK;
}

bool_t EditorSettings_AddListener( settings_registry_t *pRegistry, settings_listener_fn pfnChanged, void *pContext ) noexcept
{
    CY_ASSERT( pRegistry != nullptr && pfnChanged != nullptr );
    if ( pRegistry == nullptr || pfnChanged == nullptr || pRegistry->nListeners >= EDITOR_SETTINGS_MAX_LISTENERS ) {
        CY_LOG_WRITE( Error, Editor, "Settings listener table is full" );
        return CY_FALSE;
    }
    pRegistry->listeners[pRegistry->nListeners++] = settings_listener_t{ pfnChanged, pContext };
    return CY_TRUE;
}

void EditorSettings_RemoveListener( settings_registry_t *pRegistry, settings_listener_fn pfnChanged, void *pContext ) noexcept
{
    if ( pRegistry == nullptr ) { return; }
    for ( usize i = 0u; i < pRegistry->nListeners; ++i ) {
        if ( pRegistry->listeners[i].pfnChanged != pfnChanged || pRegistry->listeners[i].pContext != pContext ) { continue; }
        for ( usize j = i + 1u; j < pRegistry->nListeners; ++j ) { pRegistry->listeners[j - 1u] = pRegistry->listeners[j]; }
        --pRegistry->nListeners;
        return;
    }
}

const char *EditorSettings_ScopeName( settings_scope_t scope ) noexcept
{
    switch ( scope ) {
        case settings_scope_t::WORKSPACE: return "Workspace";
        case settings_scope_t::PROJECT: return "Project";
        case settings_scope_t::USER: return "User";
        case settings_scope_t::DEFAULT: return "Default";
    }
    return "Unknown";
}

settings_document_identity_t EditorSettings_FileIdentity() noexcept
{
    return { StringView_FromCString( "cypher.settings" ), 1u, 2u };
}

const setting_descriptor_t *EditorSettings_FrameworkCatalogue( usize *pnDescriptorsOut ) noexcept
{
    if ( pnDescriptorsOut != nullptr ) { *pnDescriptorsOut = std::size( kFramework ); }
    return kFramework;
}

} // namespace cypher::editor
