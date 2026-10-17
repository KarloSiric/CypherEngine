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
#include "CypherMapGui_EntityLook.h"
#include "CypherMapGui_ObjectTools.h"
#include "CypherMapGui_Layers.h"
#include "CypherMapGui_Instances.h"
#include "CypherMapGui_Hierarchy.h"
#include "CypherMapGui_Notice.h"
#include "CypherMapGui_GeometryCommands.h"
#include "CypherMapGui_ToolGestures.h"
#include "CypherMapGui_EntityIo.h"
#include "CypherMapGui_Hotspots.h"

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
constexpr const char *kBrushShapes[]{ "box", "wedge", "cylinder", "spike", "sphere", "quad", "arch", "stairs", "spiral_stairs", "pipe", "torus" };
constexpr const char *kStairsDirections[]{ "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
constexpr const char *kPrimitiveAxes[]{ "X", "Y", "Z" };
constexpr const char *kClipModes[]{ "both", "front", "back" };
constexpr const char *kClipAxes[]{ "x", "y", "z" };
constexpr const char *kClipPlacements[]{ "surface", "plane" };
constexpr const char *kCurveShapes[]{ "straight", "smooth", "lathe", "walls", "hollow_pipe" };
constexpr const char *kWallJoins[]{ "miter", "bevel", "round" };
constexpr const char *kSweepPaths[]{ "straight", "arc", "s_bend" };
constexpr const char *kCurveProfiles[]{ "rectangle", "round" };
constexpr const char *kCurveOutputs[]{ "mesh", "patch" };
constexpr const char *kDisplaceModes[]{ "push_pull", "smooth", "flatten", "inflate", "pinch", "noise" };
constexpr const char *kDisplaceNormals[]{ "average", "brush", "view", "z" };
constexpr const char *kTerrainModes[]{ "raise", "lower", "smooth", "flatten" };
constexpr const char *kDisplaceFalloffs[]{ "smooth", "linear", "sharp", "constant", "custom" };
constexpr const char *kDisplaceTargets[]{ "selected", "everything" };
constexpr const char *kCarveModes[]{ "slice", "precise" };
constexpr const char *kResizeModes[]{ "Each object", "Selection bounds" };
constexpr const char *kPasteOffsets[]{ "none", "grid", "cursor" };
constexpr const char *kBlockOutputs[]{ "mesh", "brush" };
constexpr const char *kPatchShapes[]{ "plane", "cylinder", "cone", "sphere", "disc", "bevel", "torus" };
constexpr const char *kPaintChannels[]{ "color", "red", "green", "blue", "alpha" };
constexpr const char *kEdgeDragActions[]{ "move", "extend" };

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
    MapFlag( "editor.map.snap_to_vertices", CY_FALSE, "Snap to vertices",
             "Moving objects locks a corner of the selection onto a corner of other geometry within the geometry snap distance on screen (Hammer's Snap to Vertices)." ),
    MapReal( "editor.map.block_depth", 64.0, 1.0, 65536.0, "Block depth", "Thickness along the axis outside a drawn box footprint." ),
    MapFlag( "editor.map.block_create_on_release", CY_FALSE, "Create blocks on release",
             "On: a block is created as soon as the drag ends (TrenchBroom). Off: it stays staged with handles to adjust until Enter (Hammer 5); picking another tool creates it too." ),
    MapReal( "editor.map.default_texture_scale", 0.25, 0.001, 64.0, "Default texture scale", "UV repeat size for new faces: 0.25 is 128 world units per repeat." ),
    MapChoice( "editor.map.new_brush_shape", kBrushShapes, "box", "New brush shape", "Shape the block tool draws." ),
    MapChoice( "editor.map.block_output", kBlockOutputs, "brush", "Block tool creates",
               "Mesh (Hammer 5): blocks are editable meshes, so vertex, edge and face tools work on them at once. Brush (Quake/TrenchBroom): convex brushes for Clip, Hollow, Carve and brush Merge." ),
    MapFlag( "editor.map.components_convert_brushes", CY_TRUE, "Convert brushes for mesh editing",
             "A brush clicked in Vertices or Edges mode, or worked on by a mesh tool (Knife, Loop Cut, Paint, Displace) or a mesh operation (Extrude, Inset, Subdivide...), becomes a mesh first (one undo step), as every Hammer 5 block is a mesh. Off keeps brushes as brushes (TrenchBroom style)." ),
    MapChoice( "editor.map.edge_drag_action", kEdgeDragActions, "move", "Edge drag",
               "Move deforms the selected edges. Extend creates connected faces from open boundary edges and selects their new outer edges. Applies to direct edge dragging and move, rotate and scale gizmos. Select shows move arrows; Rotate and Scale use the placed pivot. Shift-dragging an explicit transform gizmo also extends. Numeric transform fields still deform the selection." ),
    MapInteger( "editor.map.cylinder_sides", 16, 3, 128, "Cylinder sides", "Sides of new cylinders and spikes." ),
    MapChoice( "editor.map.primitive_axis", kPrimitiveAxes, "Z", "Primitive axis", "Cylinder or spike length, wedge slope, or numeric Quad normal direction." ),
    MapInteger( "editor.map.sphere_subdivisions", 1, 0, 1, "Sphere detail", "UV sphere: 0 is 16 sides and 8 latitude bands (128 faces); 1 is 24 sides and 10 bands (240 faces)." ),
    MapReal( "editor.map.cone_top_radius", 0.0, 0.0, 1.0, "Spike top radius", "Top-to-base radius ratio: 0 is a point; 1 is a cylinder." ),
    MapChoice( "editor.map.project_direction", kStairsDirections, "-Z", "Project toward", "Project onto World drops the selection this way (-Z: down onto the floor or terrain)." ),
    MapReal( "editor.map.project_offset", 0.0, -1024.0, 1024.0, "Project offset", "Project onto World stops this far short of the surface (a hair above it keeps a strip from z-fighting)." ),
    MapReal( "editor.map.import_scale", 1.0, 0.0001, 10000.0, "Import scale", "Import OBJ: how many map units one unit of the file becomes." ),
    MapFlag( "editor.map.import_y_up", CY_FALSE, "Import Y up", "Import OBJ: the file is Y up (most modelling tools); turn it Z up." ),
    MapText( "editor.map.tie_class", "func_detail", 63u, "Tie class", "Tie to Entity makes a new brush entity of this class (Hammer's default: func_detail)." ),
    MapReal( "editor.map.arch_thickness", 16.0, 1.0, 4096.0, "Wall width", "Arch and Pipe: how thick the curved wall is, in world units." ),
    MapReal( "editor.map.arch_angle", 180.0, 1.0, 360.0, "Arc", "Arch: how far round it goes, in degrees (360: a full ring)." ),
    MapReal( "editor.map.arch_start", 0.0, -360.0, 360.0, "Start angle", "Arch: where it starts, in degrees round the primitive axis." ),
    MapInteger( "editor.map.stairs_steps", 8, 1, 256, "Steps", "Stairs and Spiral stairs: how many steps." ),
    MapChoice( "editor.map.stairs_direction", kStairsDirections, "+X", "Rise toward", "Stairs climb toward this world direction (one across the primitive axis)." ),
    MapReal( "editor.map.spiral_turn", 360.0, -3600.0, 3600.0, "Turn", "Spiral stairs: how far the steps turn in all, in degrees (negative: the other way)." ),
    MapReal( "editor.map.spiral_inner", 16.0, 0.0, 4096.0, "Inner radius", "Spiral stairs: the open middle, in world units." ),
    MapReal( "editor.map.torus_tube", 0.25, 0.02, 0.49, "Tube size", "Torus: tube radius as a part of the half footprint." ),
    MapChoice( "editor.map.clip_mode", kClipModes, "both", "Retain", "Back keeps n · p + d ≤ 0; Front keeps n · p + d ≥ 0; Both retains both halves." ),
    MapFlag( "editor.map.clip_cap", CY_TRUE, "Cap new surfaces", "Close the cut of a clipped mesh with new faces (brushes are always closed). Off leaves the mesh open along the cut." ),
    MapChoice( "editor.map.clip_3d_placement", kClipPlacements, "surface", "3D placement",
               "Surface (Hammer 5, TrenchBroom): in a 3D view, draw the cut line across any face, locking onto vertices; it cuts straight through, perpendicular to that face, and a third point tilts it. Plane: draw on the construction plane below. A press off every surface uses the plane." ),
    MapChoice( "editor.map.clip_axis", kClipAxes, "z", "3D plane axis", "In a 3D view, draw on a Clip construction plane through the selection center, perpendicular to this axis. The cutting line defines a Clip plane extruded along this axis. Changing the axis discards a staged plane." ),
    MapChoice( "editor.map.carve_mode", kCarveModes, "slice", "Carve mode", "How Carve splits brushes." ),
    MapReal( "editor.map.hollow_thickness", 16.0, 1.0, 1024.0, "Hollow thickness", "Wall thickness for Hollow (units)." ),
    MapReal( "editor.map.mesh_extrude_distance", 16.0, 0.001, 1000000.0, "Extrude distance", "Extend one mesh face or an edge-connected coplanar face region along its shared normal. Caps keep their IDs; only the region perimeter receives walls. Positive units." ),
    MapReal( "editor.map.mesh_inset_distance", 8.0, 0.001, 1000000.0, "Corner inset distance", "Select exactly one mesh face. Move every corner toward its center by this distance. This radial inset does not promise a uniform border width. The inner face keeps its identity." ),
    MapReal( "editor.map.mesh_inset_width", 8.0, 0.001, 1000000.0, "Inset width", "Perpendicular border width on exactly one planar convex mesh face. Collapsed offsets, concave or warped faces refuse. The inner face keeps its identity." ),
    MapInteger( "editor.map.mesh_slice_u", 2, 1, 64, "Quad Slice U cells", "Cells along the selected quad's corner 0 to corner 1 direction. Quad Slice inserts shared boundary vertices in neighboring faces." ),
    MapInteger( "editor.map.mesh_slice_v", 2, 1, 64, "Quad Slice V cells", "Cells along the selected quad's corner 0 to corner 3 direction. A 1 by 1 grid makes no change." ),
    MapReal( "editor.map.mesh_bevel_width", 8.0, 0.001, 1000000.0, "Bevel width", "Distance a bevel cuts into each face beside the selected edges. A width the faces cannot hold is rejected unchanged." ),
    MapInteger( "editor.map.mesh_bevel_segments", 1, 1, 32, "Bevel segments", "1 chamfers the edge flat; more segments round it." ),
    MapReal( "editor.map.mesh_edge_extrude_distance", 16.0, 0.001, 1000000.0, "Edge extrude distance", "How far Extrude pulls open edges off the surface and Extend continues the surface outward." ),
    MapReal( "editor.map.mesh_solidify_thickness", 16.0, 0.001, 1000000.0, "Solidify thickness", "Thickness Solidify gives open meshes, grown along their face normals." ),
    MapChoice( "editor.map.patch_shape", kPatchShapes, "cylinder", "Patch shape", "Shape the Patch tool draws: a box sets its extent; cylinders, cones and discs run along the pane's depth." ),
    MapChoice( "editor.map.paint_channel", kPaintChannels, "color", "Paint channel", "What the Paint tool paints: the paint colour, or one vertex colour channel taken down from white. Ctrl/Command paints back toward white." ),
    MapText( "editor.map.paint_color", "#e05a28", 16u, "Paint colour", "Colour the Paint tool paints in colour mode, as #RRGGBB." ),
    MapReal( "editor.map.paint_radius", 32.0, 0.5, 65536.0, "Paint radius", "Radius of the Paint tool's brush (units)." ),
    MapReal( "editor.map.paint_strength", 1.0, 0.01, 1.0, "Paint strength", "How far one dab moves a corner toward the painted value." ),
    MapReal( "editor.map.curve_width", 32.0, 0.5, 65536.0, "Curve width", "Width of the strip the Curve tool sweeps along its points." ),
    MapReal( "editor.map.curve_height", 16.0, 0.5, 65536.0, "Curve height", "Height of the strip the Curve tool sweeps along its points." ),
    MapReal( "editor.map.curve_outer_radius", 16.0, 0.5, 65536.0, "Outer radius", "Hollow Pipe: distance from the path centre to the outer wall." ),
    MapReal( "editor.map.curve_wall_thickness", 4.0, 0.01, 65536.0, "Wall thickness", "Hollow Pipe: wall thickness, greater than zero and smaller than the outer radius." ),
    MapReal( "editor.map.mesh_smooth_strength", 0.5, 0.01, 1.0, "Smooth strength", "How far one Smooth moves each vertex toward the average of its neighbours." ),
    MapReal( "editor.map.mesh_offset_distance", 8.0, 0.001, 1000000.0, "Offset distance", "How far Offset moves the selected vertices, edges or face along their normals." ),
    MapReal( "editor.map.mesh_slide_factor", 0.25, -0.99, 0.99, "Slide factor", "How far Slide Edge Loop moves a loop toward its neighbour: 0.5 is halfway, negative goes the other way." ),
    MapReal( "editor.map.mesh_weld_distance", 1.0, 0.0001, 1000.0, "Weld distance", "Vertices closer than this join on Weld and Merge by Distance." ),
    MapChoice( "editor.map.displace_mode", kDisplaceModes, "push_pull", "Brush",
               "Push/Pull raises the surface (Ctrl/Command lowers it); Smooth evens it out; Flatten presses it onto the plane under the brush; Inflate swells it along its normals; Pinch draws it toward the brush centre; Noise roughens it." ),
    MapChoice( "editor.map.terrain_mode", kTerrainModes, "raise", "Terrain brush", "Raise or lower the ground under the brush (Ctrl/Command does the opposite), smooth it, or flatten it to the height the stroke started at." ),
    MapReal( "editor.map.terrain_radius", 256.0, 8.0, 16384.0, "Terrain radius", "Terrain brush radius in world units." ),
    MapReal( "editor.map.terrain_strength", 25.0, 1.0, 100.0, "Terrain strength", "How much one dab changes the ground (Raise at 100: a quarter of the radius)." ),
    MapReal( "editor.map.displace_radius", 64.0, 1.0, 4096.0, "Radius", "Brush radius in world units. [ and ] change the radius for the next stroke; an active stroke keeps its starting radius." ),
    MapFlag( "editor.map.displace_auto_tessellate", CY_TRUE, "Refine coarse quad faces",
             "Slices a four-sided face wider than the brush into connected cells before sculpting. Preparation and the stroke commit together; cancellation leaves the original geometry unchanged." ),
    MapReal( "editor.map.displace_strength", 50.0, 1.0, 100.0, "Strength", "How far one dab moves the surface (100: a fifth of the radius)." ),
    MapReal( "editor.map.displace_smooth", 50.0, 0.0, 100.0, "Smooth amount", "How strongly Smooth evens the surface per dab." ),
    MapChoice( "editor.map.displace_normal", kDisplaceNormals, "average", "Normal mode",
               "Direction Push/Pull, Flatten and Noise move along: the average normal under the brush, the normal of the face hit, toward the view, or straight up." ),
    MapChoice( "editor.map.displace_falloff", kDisplaceFalloffs, "smooth", "Falloff", "How the effect fades from the brush centre to its edge." ),
    MapText( "editor.map.displace_curve", "0:1 0.5:0.5 1:0", 255u, "Custom falloff", "The Custom falloff: strength against distance from the centre, as x:y points (drag them on the Displacement panel's curve)." ),
    MapChoice( "editor.map.displace_target", kDisplaceTargets, "selected", "Apply to", "Restrict strokes to selected objects; with no selection, use unlocked visible meshes. Everything includes all unlocked visible meshes under the brush." ),
    MapReal( "editor.map.compile_walkable_slope", 45.0, 0.0, 90.0, "Walkable slope", "Compile Map: the steepest surface (degrees from flat) the navigation cook counts as walkable." ),
    MapReal( "editor.map.compile_luxel_size", 16.0, 1.0, 1024.0, "Luxel size", "Compile Map: world units per lightmap texel." ),
    MapChoice( "editor.map.curve_shape", kCurveShapes, "straight", "Curve shape",
               "Straight: a strip along the clicked points. Smooth: the profile swept along a smooth curve through them. Lathe: the points are a profile spun about the vertical through the first one (vases, pillars, domes). Walls: walls along the path, Width thick and Height tall, with clean corners. Hollow Pipe: an open smooth path with inner and outer walls and annular end rims; mesh output only." ),
    MapChoice( "editor.map.wall_join", kWallJoins, "miter", "Wall corners", "How Walls meet at a corner: mitred (sharp), bevelled (cut) or round." ),
    MapChoice( "editor.map.curve_profile", kCurveProfiles, "rectangle", "Profile", "The cross-section a smooth sweep carries: a width × height rectangle, or a solid round cable as wide as the width. Use Hollow Pipe for an open bore." ),
    MapChoice( "editor.map.curve_output", kCurveOutputs, "mesh", "Output",
               "Mesh: editable polygon geometry (Hollow Pipe has an open bore). Patch: one curved patch, as in Radiant - Straight and Walls give a strip with a corner at each point, Smooth an arch pulled toward the inner points, both Height tall; Lathe spins the profile a full turn." ),
    MapFlag( "editor.map.curve_closed", CY_FALSE, "Closed loop", "A smooth sweep joins its last point back to the first (a ring)." ),
    MapReal( "editor.map.lathe_angle", 360.0, 1.0, 360.0, "Lathe angle", "How far the lathe spins the profile, in degrees (360: all the way round)." ),
    MapChoice( "editor.map.sweep_path", kSweepPaths, "straight", "Sweep path", "Brush Sweep moves the face straight out, round an arc, or along an S-bend that ends parallel." ),
    MapReal( "editor.map.sweep_distance", 128.0, -100000.0, 100000.0, "Sweep distance", "How far a straight or S-bend sweep reaches out from the face." ),
    MapReal( "editor.map.sweep_sideways", 64.0, -100000.0, 100000.0, "Sweep sideways", "S-bend: how far it shifts across; arc: the radius from the face to the turning axis (unless a pivot is set)." ),
    MapReal( "editor.map.sweep_angle", 90.0, -359.0, 359.0, "Sweep angle", "How far an arc sweep turns, in degrees." ),
    MapInteger( "editor.map.sweep_segments", 4, 1, 64, "Sweep segments", "Brushes per sweep (more: a smoother arc or bend)." ),
    MapInteger( "editor.map.sweep_iterations", 1, 1, 64, "Sweep repeats", "Repeat the sweep from its end (a spiral stair from one arc step)." ),
    MapChoice( "editor.map.projection_axis", kClipAxes, "z", "Projection axis", "Planar Mapping projects along it; Cylindrical Mapping wraps around it." ),
    MapInteger( "editor.map.smoothing_group", 1, 1, 32, "Smoothing group", "The group Set Smoothing Group puts faces in (1..32)." ),
    MapInteger( "editor.map.displacement_power", 3, 1, 4, "Displacement power", "Grid density of Create Displacement: power 2 is 5 × 5 vertices, 3 is 9 × 9, 4 is 17 × 17 (as in Source)." ),
    MapReal( "editor.map.displacement_elevation", 0.0, -100000.0, 100000.0, "Elevation", "How far the new displacement stands off its face." ),
    MapReal( "editor.map.displacement_noise", 0.0, 0.0, 100000.0, "Noise amplitude", "Height of random roughness on the new displacement; 0 keeps it flat." ),
    MapReal( "editor.map.displacement_noise_scale", 64.0, 1.0, 100000.0, "Noise scale", "World units per roughness bump." ),
    MapInteger( "editor.map.displacement_octaves", 3, 1, 8, "Noise detail", "Layers of finer roughness added to the noise." ),
    MapInteger( "editor.map.displacement_seed", 1, 0, 1000000, "Noise seed", "Another seed gives other roughness; the same seed gives the same surface." ),
    MapChoice( "editor.map.array_axis", kClipAxes, "x", "Array axis", "The axis Array copies along (and Bend works along)." ),
    MapInteger( "editor.map.array_count", 3, 2, 256, "Array copies", "How many copies Array makes, the original included." ),
    MapReal( "editor.map.array_distance", 0.0, -100000.0, 100000.0, "Array distance", "The step between copies; 0 makes them touch (the mesh's size along the axis)." ),
    MapInteger( "editor.map.radial_count", 8, 2, 256, "Radial copies", "How many copies Radial Array spreads over a full circle." ),
    MapReal( "editor.map.bend_angle", 90.0, -3600.0, 3600.0, "Bend angle", "How far Bend curls the mesh, in degrees." ),
    MapReal( "editor.map.taper_factor", 0.5, 0.0, 100.0, "Taper factor", "The mesh's size at its top relative to its bottom." ),
    MapReal( "editor.map.thicken_distance", 16.0, 0.001, 100000.0, "Thicken distance", "How thick Thicken Faces makes the new mesh (units)." ),
    MapReal( "editor.map.patch_thickness", 16.0, 0.01, 4096.0, "Patch thickness", "How far behind a patch Thicken Patch places its copy." ),
    MapFlag( "editor.map.patch_thicken_seams", CY_TRUE, "Thicken seams", "Thicken Patch also closes the open borders between a patch and its copy." ),
    MapReal( "editor.map.face_select_angle", 15.0, 0.0, 179.0, "Face select angle", "Select Faces Facing picks faces whose normal is within this many degrees of the active face's." ),
    MapReal( "editor.map.sharp_edge_angle", 30.0, 0.1, 179.0, "Sharp edge angle", "Select Sharp Edges picks edges whose faces meet at this angle in degrees or more." ),
    MapFlag( "editor.map.select_created", CY_TRUE, "Select created objects", "Select newly created objects." ),
    MapChoice( "editor.map.resize_mode", kResizeModes, "Each object", "Multiple-object resize", "Each extends every selected brush, mesh, or patch by the same grid-snapped distance about its own opposite side. Group scales the combined selection bounds. This choice is captured when grabbing a bounds handle; the Scale tool always scales the group." ),
    MapFlag( "editor.map.resize_from_center", CY_FALSE, "Resize from center", "Bounds handles widen both sides around the captured center. Otherwise the opposite side stays fixed. Hold Shift when grabbing a resize handle to use the center for that gesture. The dragged boundary snaps; the opposite boundary mirrors it around the exact center." ),
    MapChoice( "editor.map.paste_offset", kPasteOffsets, "grid", "Paste placement", "None keeps original coordinates. Grid shifts by one authored grid step on the last hovered plane (XY if unset). Cursor centers the copied bounds on that plane and snaps its center when grid snap is enabled; the omitted axis is retained. Paste In Place always keeps original coordinates." ),
    MapText( "editor.map.default_point_class", "info_player_start", 64u, "Default point class", "Entity class the entity tool places." ),
    MapText( "editor.map.default_solid_class", "func_detail", 64u, "Default solid class", "Entity class Tie to Entity uses." ),
    MapFlag( "editor.map.write_all_properties", CY_TRUE, "Write all properties", "New entities store every class property (CYMAP.md 4)." ),
    MapFlag( "editor.map.check_before_run", CY_TRUE, "Check before run", "Run Check for Problems before running a map." ),
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
    const bool_t bClipChanged = path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.clip_mode" ) ) ||
                                StringView_Equals( path, StringView_FromCString( "editor.map.clip_cap" ) );
    const bool_t bClipAxisChanged = path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.clip_axis" ) );
    const bool_t bConversionChanged = path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.components_convert_brushes" ) );
    if ( bClipAxisChanged && pWorkspace->editPreview.bActive && pWorkspace->editPreview.bClip ) {
        // An existing line keeps its original meaning; changing the 3D
        // construction axis requires a new Clip gesture.
        MapWorkspace_ClearEditPreview( pWorkspace );
    } else if ( bClipChanged && pWorkspace->editPreview.bActive && pWorkspace->editPreview.bClip ) {
        // A retained-half change updates the staged result, never its plane
        // or the live document. The preview setter publishes the view notice.
        MapWorkspace_SetClipPreview( pWorkspace, pWorkspace->editPreview.clipPlane );
    } else if ( bGridChanged || bClipChanged || bClipAxisChanged || bConversionChanged ) { MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW ); }
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

bool_t PlanCameraSpeed( const map_workspace_t *pWorkspace, map_camera_speed_action_t action, f64 steps, camera_speed_write_t &write ) noexcept
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr || !std::isfinite( steps ) || steps <= 0.0 ) { return CY_FALSE; }
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
            // Compare logarithms before exponentiation so extreme wheel events
            // saturate without overflow, and fractional trackpad steps remain smooth.
            target = steps >= std::log2( pDescriptor->flMax ) - std::log2( current.flValue )
                ? pDescriptor->flMax : current.flValue * std::exp2( steps );
            break;
        case map_camera_speed_action_t::DECREASE:
            target = steps >= std::log2( current.flValue ) - std::log2( pDescriptor->flMin )
                ? pDescriptor->flMin : current.flValue * std::exp2( -steps );
            break;
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
    { "Measure", CY_FALSE },   { "Terrain", CY_TRUE }, { "Patch", CY_FALSE },  { "Translate", CY_TRUE }, { "Rotate", CY_TRUE },
    { "Scale", CY_TRUE },     { "Pivot", CY_FALSE },   { "Polygon", CY_FALSE }, { "Mirror", CY_FALSE }, { "Paint", CY_FALSE },
    { "Extrude", CY_TRUE },   { "Knife", CY_FALSE },   { "Loop Cut", CY_FALSE }, { "Eyedropper", CY_FALSE }, { "Workplane", CY_FALSE },
    { "Curve", CY_FALSE }, { "Edge Cut", CY_FALSE }, { "Edge Arc", CY_FALSE }, { "Displace", CY_FALSE }, { "Shear", CY_FALSE }, { "Navigation", CY_TRUE },
};
static_assert( std::size( kTools ) == static_cast<usize>( map_tool_t::COUNT ), "Tool table and enum diverged" );

// Modes are selectable independently of their operation readiness. Vertex
// and edge profiles disclose unavailable picking; Meshes selects real roots.
constexpr tool_info_t kElementModes[]{
    { "Vertices", CY_TRUE }, { "Edges", CY_TRUE }, { "Faces", CY_TRUE }, { "Meshes", CY_TRUE },
    { "Objects", CY_TRUE },   { "Groups", CY_TRUE }, { "Navigation", CY_TRUE },
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
         !MapWorkspace_IsVisible( workspace, *wire ) || MapLayers_IsObjectLocked( workspace, selection.meshId.value ) || workspace->selection.ids.nCount != 1 ||
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
    // Another document: nothing of the old history applies to it, nor the
    // tools' staged state (a workplane, the pivot, a knife chain).
    MapToolGestures_Release( pWorkspace );
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
    MapWorkspace_WireRebuilt( pWorkspace );
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION | MAP_CHANGE_TITLE | MAP_CHANGE_HISTORY );
    MapWorkspace_Frame( pWorkspace, CY_FALSE );
}

void LogProblems( const map_document_t &map ) noexcept
{
    const usize nProblems = map.problems.nCount;
    for ( usize i = 0u; i < nProblems && i < kProblemsLogged; ++i ) {
        const map_problem_t &problem = map.problems.pData[i];
        QString line = QStringLiteral( "Map problem %1" ).arg( QString::fromUtf8( MapDocument_ProblemName( problem.code ) ) );
        if ( problem.layer[0] != '\0' ) { line += QStringLiteral( " in layer %1" ).arg( QString::fromUtf8( problem.layer ) ); }
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
    // Selection categories retain an active transform, but leave creation
    // tools so the next viewport click picks the requested component.
    const bool keepTool = workspace->tool == map_tool_t::SELECT || workspace->tool == map_tool_t::TRANSLATE ||
                          workspace->tool == map_tool_t::ROTATE || workspace->tool == map_tool_t::SCALE;
    if ( !keepTool ) {
        // Finish staged construction before changing mode: SetElementMode
        // clears its preview. A failed commit must leave it available to retry.
        if ( workspace->tool == map_tool_t::BLOCK && MapWorkspace_HasBlockPreview( workspace ) ) {
            if ( !MapWorkspace_CommitBlockPreview( workspace ) ) { return command_result_t::FAILED; }
            MapNotice_Post( workspace, map_notice_t::INFO, QStringLiteral( "Created the block you drew (Undo removes it)" ) );
        }
        MapWorkspace_SetTool( workspace, map_tool_t::SELECT );
    }
    MapWorkspace_SetElementMode( workspace, kMode );
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
u32 MeshFaceInsetState( void *pContext ) noexcept
{
    const auto *ws = static_cast<const map_workspace_t *>( pContext );
    return MapWorkspace_CanEditMeshFace( ws ) && MapWorkspace_MeshFaces( ws ).nCount == 1 ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
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
command_result_t InsetMeshFaceByEdgeWidth( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = static_cast<map_workspace_t *>( pContext );
    return MapWorkspace_InsetMeshFaceByEdgeWidth( ws, EditorSettings_Real( &ws->pGui->settings, "editor.map.mesh_inset_width", 8.0 ) ) ? command_result_t::OK : command_result_t::FAILED;
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
    auto *ws = static_cast<map_workspace_t *>( pContext );
    if ( MapWorkspace_MergeSelection( ws ) ) { return command_result_t::OK; }
    // A brush stays convex: Merge joins two only when together they are one
    // convex shape. Anything else is a mesh Boolean's job.
    MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Merge joins two brushes only when together they make one convex shape (two halves of a box, a wedge on its block). "
                                                               "For any other shape use Union (CSG > Union), which makes a mesh" ) );
    return command_result_t::FAILED;
}
command_result_t ApplyFaceMaterial( void *pContext, const command_args_t & ) noexcept
{
    // Shift+T: the picked face(s), or every face of the selected objects.
    return MapGeometryCommands_ApplyMaterial( static_cast<map_workspace_t *>( pContext ) ) ? command_result_t::OK : command_result_t::FAILED;
}
u32 ApplyMaterialState( void *pContext ) noexcept
{
    return MapGeometryCommands_CanApplyMaterial( static_cast<const map_workspace_t *>( pContext ) ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
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
    if ( !selected ) {
        // Loops run through corners where four edges meet, rings across quads.
        MapNotice_Post( workspace, map_notice_t::INFO, kLoop ? QStringLiteral( "Select Loop: the loop goes no further than the edge itself (it stops where three or five edges meet)" )
                                                             : QStringLiteral( "Select Ring: no strip of quads runs across this edge" ) );
    }
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
    // Selecting changes no undo step, so the count is the only sign it ran.
    std::sort( classes.begin(), classes.end() );
    classes.erase( std::unique( classes.begin(), classes.end() ), classes.end() );
    const QString what = classes.size() == 1 ? QString::fromUtf8( classes.front() ) : QStringLiteral( "%1 classes" ).arg( classes.size() );
    MapNotice_Post( pWorkspace, map_notice_t::INFO, QStringLiteral( "Select Same Class: %1 %2 selected (%3)" )
        .arg( ids.size() ).arg( ids.size() == 1 ? QStringLiteral( "entity" ) : QStringLiteral( "entities" ), what ) );
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
    // Windows that outlive the workspace (a Find dialog closed with a deferred
    // delete) must not reach it later. Tell every listener first, from a copy:
    // a listener may unsubscribe while it is told.
    map_listener_t listeners[MAP_WORKSPACE_MAX_LISTENERS]{};
    const usize nListeners = pWorkspace->nListeners;
    for ( usize i = 0u; i < nListeners; ++i ) { listeners[i] = pWorkspace->listeners[i]; }
    for ( usize i = 0u; i < nListeners; ++i ) {
        if ( listeners[i].pfnChanged != nullptr ) { listeners[i].pfnChanged( listeners[i].pContext, MAP_CHANGE_SHUTDOWN ); }
    }
    // Asset callbacks can capture objects owned by this workspace's session.
    // Remove them before teardown, including when its address is later reused.
    MapHotspots_SetResolver( pWorkspace, {} );
    MapAssets_SetLister( pWorkspace, {} );
    MapAssets_SetRoots( pWorkspace, {} );
    MapToolGestures_Release( pWorkspace );
    MapEntityIo_Release( pWorkspace );
    MapEntityLook_Forget( pWorkspace );
    MapObjectTools_Forget( pWorkspace );
    MapLayers_Forget( pWorkspace );
    MapInstances_Forget( pWorkspace );
    MapHierarchy_Forget( pWorkspace );
    if ( pWorkspace->pGui != nullptr ) { EditorSettings_RemoveListener( &pWorkspace->pGui->settings, &OnSettingsChanged, pWorkspace ); }
    delete pWorkspace->pDocument;
    pWorkspace->pDocument = nullptr;
    MapWireframe_Shutdown( &pWorkspace->wire );
    MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
    EditorSelection_Shutdown( &pWorkspace->selection );
    EditorSelection_Shutdown( &pWorkspace->hidden );
    geometry::MeshSelection_Shutdown( &pWorkspace->meshSelection );
    Vector_Shutdown( &pWorkspace->meshFaceSet ); pWorkspace->meshFaceSetObject = 0;
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
    const map_files_result_t result = MapFiles_Save( pWorkspace->pDocument, utf8.constData() );
    if ( result.status != map_files_status_t::OK ) { return result; }
    pWorkspace->path = path;
    EditorHistory_MarkClean( &pWorkspace->history );
    // Saving can assign IDs and moves groups to their first member's layer;
    // the records the panels show may have changed even though nothing moved.
    if ( MapWireframe_Build( &pWorkspace->wire, *pWorkspace->pDocument ) != map_status_t::OK ) {
        CY_LOG_WRITE( Error, Editor, "Map views could not be rebuilt after saving" );
    }
    MapWorkspace_WireRebuilt( pWorkspace );
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_DOCUMENT | MAP_CHANGE_TITLE );
    return result;
}

QString MapWorkspace_DisplayName( const map_workspace_t *pWorkspace )
{
    CY_ASSERT( pWorkspace != nullptr );
    return pWorkspace->path.isEmpty() ? QStringLiteral( "Untitled" ) : QFileInfo( pWorkspace->path ).fileName();
}

namespace
{
void LockedSelectionNotice( const map_workspace_t *workspace ) noexcept
{
    MapNotice_Post( workspace, map_notice_t::WARNING,
        QStringLiteral( "The selection includes objects on a locked layer. Unlock the layer before selecting them." ) );
}

// Groups mode selects a group's members together (CYMAP.md 6.10 groups).
void SelectGroup( map_workspace_t *pWorkspace, u64 group, u64 clicked, map_select_mode_t mode ) noexcept
{
    vector_t<u64> members{}, next{};
    const auto *allocator = pWorkspace->selection.ids.pAllocator;
    if ( !Vector_Init( &members, allocator ) || !Vector_Init( &next, allocator ) ) { return; }
    if ( MapDocument_GroupMembers( pWorkspace->pDocument, group, &members ) != map_status_t::OK || members.nCount == 0 ) { return; }
    const bool remove = mode == MAP_SELECT_REMOVE || ( mode == MAP_SELECT_TOGGLE && EditorSelection_Contains( &pWorkspace->selection, clicked ) );
    if ( mode != MAP_SELECT_REPLACE ) {
        for ( usize i = 0; i < pWorkspace->selection.ids.nCount; ++i ) {
            const u64 id = pWorkspace->selection.ids.pData[i];
            if ( ( !remove || std::find( members.pData, members.pData + members.nCount, id ) == members.pData + members.nCount ) &&
                 !Vector_PushBack( &next, id ) ) { return; }
        }
    }
    if ( !remove && !Vector_Append( &next, { members.pData, members.nCount } ) ) { return; }
    MapWorkspace_SetSelection( pWorkspace, next.pData, next.nCount );
}
} // namespace

void MapWorkspace_Select( map_workspace_t *pWorkspace, u64 id, map_select_mode_t mode ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr );
    if ( mode != MAP_SELECT_REPLACE && mode != MAP_SELECT_ADD && mode != MAP_SELECT_TOGGLE && mode != MAP_SELECT_REMOVE ) { return; }
    const bool selected = EditorSelection_Contains( &pWorkspace->selection, id );
    const bool adding = mode == MAP_SELECT_REPLACE || mode == MAP_SELECT_ADD || ( mode == MAP_SELECT_TOGGLE && !selected );
    if ( adding && id != 0 && MapLayers_IsObjectLocked( pWorkspace, id ) ) { LockedSelectionNotice( pWorkspace ); return; }
    if ( pWorkspace->elementMode == map_element_mode_t::GROUPS && id != 0 && pWorkspace->pDocument != nullptr ) {
        const u64 group = MapDocument_GroupOf( pWorkspace->pDocument, id );
        // A refused or failed group selection must never degrade to selecting
        // just its clicked member. The whole request belongs to the group.
        if ( group != 0 ) { SelectGroup( pWorkspace, group, id, mode ); return; }
    }
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
    bool hasId = false;
    for ( usize i = 0; i < nIds; ++i ) {
        if ( pIds[i] == 0 ) { continue; }
        if ( MapLayers_IsObjectLocked( pWorkspace, pIds[i] ) ) { LockedSelectionNotice( pWorkspace ); return; }
        hasId = true;
    }
    editor_selection_t next{};
    if ( !EditorSelection_Init( &next, pWorkspace->selection.ids.pAllocator ) ) { return; }
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

void MapWorkspace_WireRebuilt( map_workspace_t *pWorkspace ) noexcept
{
    if ( pWorkspace == nullptr ) { return; }
    MapInstances_Refresh( pWorkspace );  // Prefab instances, read from their files.
    MapEntityLook_Refresh( pWorkspace ); // Class boxes and colours from the game profile.
}

void MapWorkspace_DocumentChanged( map_workspace_t *pWorkspace, bool rebuildWire ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pWorkspace->pDocument != nullptr );
    if ( rebuildWire && MapWireframe_Build( &pWorkspace->wire, *pWorkspace->pDocument ) != map_status_t::OK ) {
        CY_LOG_WRITE( Error, Editor, "Map views could not be rebuilt after an edit" );
    }
    MapWorkspace_WireRebuilt( pWorkspace );
    pWorkspace->editPreview = {};
    MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
    if ( !MapWorkspace_HasBrushFace( pWorkspace ) ) { pWorkspace->selectedBrushFaceObject = 0; pWorkspace->selectedBrushFaceSide = 0; }
    u32 changes = MAP_CHANGE_DOCUMENT;
    if ( EditorSelection_Prune( &pWorkspace->selection, &ObjectExists, pWorkspace ) ) { changes |= MAP_CHANGE_SELECTION; }
    if ( !MapWorkspace_HasMeshFace( pWorkspace ) ) { pWorkspace->selectedMeshFaceObject = 0; pWorkspace->selectedMeshFaceId = 0; }
    MapWorkspace_Notify( pWorkspace, changes );
}

bool_t MapWorkspace_CanChangeCameraSpeed( const map_workspace_t *pWorkspace, map_camera_speed_action_t action, f64 steps ) noexcept
{
    camera_speed_write_t write{};
    return PlanCameraSpeed( pWorkspace, action, steps, write );
}

bool_t MapWorkspace_ChangeCameraSpeed( map_workspace_t *pWorkspace, map_camera_speed_action_t action, f64 steps ) noexcept
{
    camera_speed_write_t write{};
    if ( !PlanCameraSpeed( pWorkspace, action, steps, write ) ) { return CY_FALSE; }
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
    if ( mode == map_element_mode_t::NAVIGATION ) { MapWorkspace_NavigationReport( pWorkspace ); }
}

f64 MapWorkspace_WalkableSlope( const map_workspace_t *pWorkspace ) noexcept
{
    return pWorkspace != nullptr && pWorkspace->pGui != nullptr ? EditorSettings_Real( &pWorkspace->pGui->settings, "editor.map.compile_walkable_slope", 45.0 ) : 45.0;
}

map_walkable_t MapWorkspace_FaceWalkable( const map_workspace_t *pWorkspace, const map_wire_face_t &face ) noexcept
{
    // The navigation cook's rule: a face whose normal is within the walkable
    // slope of up is floor. Steeper faces that still face up are too steep.
    const f64 slope = MapWorkspace_WalkableSlope( pWorkspace ) * 3.14159265358979323846 / 180.0;
    if ( face.normal.z >= std::cos( slope ) - 1e-9 ) { return map_walkable_t::WALKABLE; }
    return face.normal.z > 1e-6 ? map_walkable_t::TOO_STEEP : map_walkable_t::NOT_FLOOR;
}

void MapWorkspace_NavigationReport( const map_workspace_t *pWorkspace ) noexcept
{
    if ( pWorkspace == nullptr || pWorkspace->pDocument == nullptr ) { return; }
    const map_wireframe_t &wire = pWorkspace->wire;
    const bool selection = pWorkspace->selection.ids.nCount != 0;
    usize walkable = 0, steep = 0;
    f64 area = 0.0, steepest = 0.0;
    for ( usize i = 0; i < wire.faces.nCount; ++i ) {
        const map_wire_face_t &face = wire.faces.pData[i];
        const map_wire_object_t *pObject = MapWireframe_FindObject( wire, face.id );
        if ( pObject == nullptr || !MapWorkspace_IsVisible( pWorkspace, *pObject ) || face.nIndices < 3 ) { continue; }
        if ( selection && EditorSelection_Contains( &pWorkspace->selection, face.id ) == CY_FALSE ) { continue; }
        const map_walkable_t kind = MapWorkspace_FaceWalkable( pWorkspace, face );
        if ( kind == map_walkable_t::TOO_STEEP ) { ++steep; continue; }
        if ( kind != map_walkable_t::WALKABLE ) { continue; }
        ++walkable;
        steepest = std::max( steepest, std::acos( std::clamp( face.normal.z, -1.0, 1.0 ) ) * 180.0 / 3.14159265358979323846 );
        // Polygon area (Newell), in square metres.
        math::vec3d_t sum{};
        for ( u32 k = 0; k < face.nIndices; ++k ) {
            const math::vec3d_t a = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]];
            const math::vec3d_t b = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + ( k + 1 ) % face.nIndices]];
            sum.x += ( a.y - b.y ) * ( a.z + b.z ); sum.y += ( a.z - b.z ) * ( a.x + b.x ); sum.z += ( a.x - b.x ) * ( a.y + b.y );
        }
        area += 0.5 * std::sqrt( sum.x * sum.x + sum.y * sum.y + sum.z * sum.z );
    }
    constexpr f64 unitsPerMeter = 39.37; // Inches, the game profile's scale.
    const QString scope = selection ? QStringLiteral( "the selection" ) : QStringLiteral( "the map" );
    MapNotice_Post( pWorkspace, map_notice_t::INFO,
        QStringLiteral( "Navigation: %1 has %2 walkable faces (%3 m\u00b2, steepest %4\u00b0; green) and %5 too steep to walk (over %6\u00b0; red). Click an object to measure it" )
            .arg( scope ).arg( walkable ).arg( area / ( unitsPerMeter * unitsPerMeter ), 0, 'f', 1 ).arg( steepest, 0, 'f', 0 ).arg( steep )
            .arg( MapWorkspace_WalkableSlope( pWorkspace ), 0, 'f', 0 ) );
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
        case map_wire_kind_t::PREFAB: return map_visgroup_t::PREFABS;
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
    if ( MapLayers_IsHidden( pWorkspace, object.iLayer ) ) { return CY_FALSE; }
    if ( MapEntityLook_AnyClassHidden( pWorkspace ) ) {
        // A hidden entity class takes the brushes its entities own with it.
        const u64 entity = object.kind == map_wire_kind_t::ENTITY ? object.id : object.owner;
        const map_wire_entity_t *pEntity = entity != 0u ? MapWireframe_FindEntity( pWorkspace->wire, entity ) : nullptr;
        if ( pEntity != nullptr && MapEntityLook_IsClassHidden( pWorkspace, pEntity->className ) ) { return CY_FALSE; }
    }
    if ( pWorkspace->bCordonActive && pWorkspace->cordon.bHas && object.bounds.bHas ) {
        // Touching the cordon is inside: a wall on its edge stays visible.
        const math::aabbd_t &c = pWorkspace->cordon.box;
        const math::aabbd_t &o = object.bounds.box;
        if ( o.maximum.x < c.minimum.x || o.minimum.x > c.maximum.x || o.maximum.y < c.minimum.y || o.minimum.y > c.maximum.y ||
             o.maximum.z < c.minimum.z || o.minimum.z > c.maximum.z ) {
            return CY_FALSE;
        }
    }
    if ( pWorkspace->hiddenVisgroups == 0u ) { return CY_TRUE; }
    const auto hidden = [pWorkspace]( const map_wire_object_t &o ) {
        return ( pWorkspace->hiddenVisgroups & ( 1u << static_cast<u32>( MapWorkspace_VisgroupOf( pWorkspace, o ) ) ) ) != 0u;
    };
    if ( hidden( object ) ) { return CY_FALSE; }
    // A brush entity's geometry also follows its entity's group, as in
    // Hammer: hiding Triggers hides the trigger volumes, not just their
    // origin markers.
    if ( object.owner != 0u ) {
        const map_wire_object_t *pOwner = MapWireframe_FindObject( pWorkspace->wire, object.owner );
        if ( pOwner != nullptr && hidden( *pOwner ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
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
    constexpr const char *kNames[]{ "Brushes", "Meshes", "Patches", "Terrain", "Tied to entities", "Lights", "Info", "Triggers", "Props", "Other", "Prefab instances" };
    static_assert( std::size( kNames ) == static_cast<usize>( map_visgroup_t::COUNT ), "Visgroup names and enum diverged" );
    return group < map_visgroup_t::COUNT ? kNames[static_cast<usize>( group )] : "Unknown";
}

bool_t MapWorkspace_IsSelectableObject( const map_workspace_t *workspace, const map_wire_object_t &object ) noexcept
{
    if ( workspace == nullptr || !MapWorkspace_IsVisible( workspace, object ) ) { return CY_FALSE; }
    if ( MapLayers_IsLocked( workspace, object.iLayer ) ) { return CY_FALSE; } // TrenchBroom: a locked layer shows, nothing in it is picked.
    switch ( workspace->elementMode ) {
        case map_element_mode_t::VERTICES:
        case map_element_mode_t::EDGES: return CY_FALSE;
        // Navigation inspects the surfaces things walk on.
        case map_element_mode_t::NAVIGATION: return object.kind != map_wire_kind_t::ENTITY;
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
    const auto mode = pWorkspace->elementMode;
    if ( tool == map_tool_t::CLIP && ( mode == map_element_mode_t::VERTICES || mode == map_element_mode_t::EDGES || mode == map_element_mode_t::NAVIGATION ) ) {
        // Clip cuts whole objects. Entered from a component mode (often right
        // after pulling a mesh's vertices) it works on that mesh in Objects
        // mode, instead of silently refusing every gesture.
        const u64 mesh = pWorkspace->meshSelection.meshId.value;
        MapWorkspace_SetElementMode( pWorkspace, map_element_mode_t::OBJECTS );
        if ( mesh != 0u && !EditorSelection_Contains( &pWorkspace->selection, mesh ) ) { MapWorkspace_Select( pWorkspace, mesh, MAP_SELECT_REPLACE ); }
    }
    // A block drawn but not yet confirmed is created rather than lost when
    // another tool is picked (Undo removes it).
    if ( pWorkspace->tool == map_tool_t::BLOCK && MapWorkspace_HasBlockPreview( pWorkspace ) && MapWorkspace_CommitBlockPreview( pWorkspace ) ) {
        MapNotice_Post( pWorkspace, map_notice_t::INFO, QStringLiteral( "Created the block you drew (Undo removes it)" ) );
    }
    pWorkspace->tool = tool;
    MapWorkspace_Notify( pWorkspace, MAP_CHANGE_VIEW );
}

bool_t MapWorkspace_IsToolAvailable( map_tool_t tool ) noexcept
{
    // Gesture tools (CypherMapGui_ToolGestures) are available once their
    // viewport input exists.
    return tool < map_tool_t::COUNT && ( kTools[static_cast<usize>( tool )].bAvailable || MapToolGestures_Implements( tool ) );
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
    if ( pWorkspace->editPreview.bFacePushPull && !MapWorkspace_HasFacePreview( pWorkspace ) ) {
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
               MapWorkspace_CanMoveSelection( pWorkspace ) : pWorkspace->editPreview.transform.bResize ?
               MapWorkspace_CanEditSelection( pWorkspace ) : MapWorkspace_CanTurnSelection( pWorkspace ) ) ) ) {
        // Every observer sees the same valid descriptor even when no pane owns
        // it (programmatic previews, hidden/removed selections, document edits).
        pWorkspace->editPreview = {};
        MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
        changes |= MAP_CHANGE_VIEW;
    }
    if ( pWorkspace->editPreview.bActive && pWorkspace->editPreview.bClip &&
         ( pWorkspace->tool != map_tool_t::CLIP || !MapWorkspace_CanClipSelection( pWorkspace ) ||
           pWorkspace->editPreview.selectionRevision != pWorkspace->selection.revision || pWorkspace->pDocument == nullptr ||
           pWorkspace->editPreview.documentRevision != pWorkspace->pDocument->geometry.revision ) ) {
        // Every observer must see the same valid presentation, including
        // previews staged programmatically without a viewport gesture owner.
        pWorkspace->editPreview = {};
        MapWireframe_Shutdown( &pWorkspace->editPreviewWire );
        changes |= MAP_CHANGE_VIEW;
    }
    // Surface strokes can target unselected objects. Retire an invalid capture
    // synchronously: hide/show without another mouse event must not revive it.
    if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW ) ) != 0 &&
         MapToolGestures_ValidateCaptures( pWorkspace ) ) { changes |= MAP_CHANGE_VIEW; }
    // The order things are picked in (Parent to Last Selected), before any listener reads it.
    if ( ( changes & MAP_CHANGE_SELECTION ) != 0 ) { MapHierarchy_SelectionChanged( pWorkspace ); }
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
        { "map.tool.terrain", "Terrain Tool", "Create a heightfield, then drag on it to raise, lower, smooth or flatten the ground.", "tool-terrain", nullptr, kTool,
          ToolExecute<map_tool_t::TERRAIN>, ToolState<map_tool_t::TERRAIN>, pWorkspace },
        { "map.terrain.create", "Create Terrain...", "Open Terrain Tool Properties to choose an exact footprint and create a flat heightfield.", "tool-terrain", nullptr, 0u,
          ToolExecute<map_tool_t::TERRAIN>, nullptr, pWorkspace },
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
        { "map.tool.shear", "Shear Tool", "Shear the selection by sliding a side of its bounds.", "tool-shear", nullptr, kTool,
          ToolExecute<map_tool_t::SHEAR>, ToolState<map_tool_t::SHEAR>, pWorkspace },
        { "map.tool.paint", "Vertex Paint", "Paint mesh corner colours. Brush conversion and painting publish together on release.", "tool-paint", nullptr, kTool,
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
        { "map.select_mode.navigation", "Navigation", "Show where things can walk: faces within the walkable slope (Compile settings) are green, steeper ones red. Click an object to measure its walkable area.", "select-navigation", nullptr, kTool,
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
        { "map.mesh.extrude", "Extrude Mesh Faces", "Extend one face or an edge-connected coplanar region by the configured positive distance. Caps retain their face IDs; only perimeter edges receive walls, inheriting adjacent face attributes.", "mesh-extrude", nullptr,
          COMMAND_FLAG_NONE, ExtrudeMeshFace, MeshFaceEditState, pWorkspace },
        { "map.mesh.inset", "Corner Inset Mesh Face", "Select exactly one authored mesh face. Move its corners toward the center; the inner face retains its ID. This radial inset does not support face regions.", "mesh-inset", nullptr,
          COMMAND_FLAG_NONE, InsetMeshFace, MeshFaceInsetState, pWorkspace },
        { "map.mesh.inset_width", "Inset Face (Width)", "Inset exactly one planar convex mesh face by its perpendicular border width. The cap retains its ID. Concave/warped faces, collapsed offsets and face regions refuse.", "mesh-inset", nullptr,
          COMMAND_FLAG_NONE, InsetMeshFaceByEdgeWidth, MeshFaceInsetState, pWorkspace },
        { "map.mesh.quad_slice", "Quad Slice Mesh Face", "Slice one selected four-corner mesh face into a U/V grid of editable quads. Neighboring faces receive shared edge vertices; UVs, materials and face ancestry are retained.", "mesh-subdivide", nullptr,
          COMMAND_FLAG_NONE, QuadSlice, QuadSliceState, pWorkspace },
        { "map.brush.to_mesh", "Convert to Mesh", "Convert selected brushes to their actual polygon meshes, retaining materials, UVs, root and face identity. Undo restores the brushes.", "brush-to-mesh", nullptr,
          COMMAND_FLAG_NONE, ConvertBrushSelection, BrushConversionState, pWorkspace },
        { "map.mesh.flip_normals", "Flip Mesh Normals", "Reverse every face of the selected authored meshes. Available in Objects, Groups and Meshes modes; corner UVs and materials stay attached.", "mesh-flip-normals", nullptr,
          COMMAND_FLAG_NONE, FlipMeshNormals, MeshEditState, pWorkspace },
        { "map.mesh.triangulate", "Triangulate Meshes", "Split every polygon of the selected authored meshes into triangles, retaining materials, corner UVs and face ancestry. Already triangulated meshes add no undo step.", "mesh-triangulate", nullptr,
          COMMAND_FLAG_NONE, TriangulateMeshSelection, MeshTriangulationState, pWorkspace },
        { "map.tool.apply_material", "Apply Current Material", "Apply the active material to the selected faces, or to every face of the selected brushes, meshes and patches.", "tool-apply-material", nullptr,
          COMMAND_FLAG_NONE, ApplyFaceMaterial, ApplyMaterialState, pWorkspace },
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
        { "map.tool.knife", "Knife Tool", "Click points on a mesh: the cut follows the straight line between them across every face it crosses, and an end inside a face runs on to that face's edge. Enter cuts.", "tool-knife", nullptr, kTool,
          ToolExecute<map_tool_t::KNIFE>, ToolState<map_tool_t::KNIFE>, pWorkspace },
        { "map.tool.loop_cut", "Loop Cut Tool", "Cut an edge loop around a mesh.", "tool-loop-cut", nullptr, kTool,
          ToolExecute<map_tool_t::LOOP_CUT>, ToolState<map_tool_t::LOOP_CUT>, pWorkspace },
        { "map.tool.eyedropper", "Eyedropper", "Pick a surface's material as the active material.", "tool-eyedropper", nullptr, kTool,
          ToolExecute<map_tool_t::EYEDROPPER>, ToolState<map_tool_t::EYEDROPPER>, pWorkspace },
        { "map.tool.workplane", "Workplane Tool", "Align the workplane to a face.", "tool-workplane", nullptr, kTool,
          ToolExecute<map_tool_t::WORKPLANE>, ToolState<map_tool_t::WORKPLANE>, pWorkspace },
        { "map.tool.curve", "Curve Tool", "Draw curves and sweep profiles along them.", "tool-curve", nullptr, kTool,
          ToolExecute<map_tool_t::CURVE>, ToolState<map_tool_t::CURVE>, pWorkspace },
        { "map.tool.edge_cut", "Edge Cut Tool", "Click points on mesh edges; Confirm cuts across the faces between them.", "tool-knife", nullptr, kTool,
          ToolExecute<map_tool_t::EDGE_CUT>, ToolState<map_tool_t::EDGE_CUT>, pWorkspace },
        { "map.tool.edge_arc", "Edge Arc Tool", "Drag to bend the selected chain of mesh edges into an arc between its ends.", "tool-curve", nullptr, kTool,
          ToolExecute<map_tool_t::EDGE_ARC>, ToolState<map_tool_t::EDGE_ARC>, pWorkspace },
        { "map.tool.displace", "Displacement Tool", "Sculpt mesh surfaces with a brush: push and pull, smooth, flatten, inflate, pinch or roughen. Each stroke is one undo step.", "displacement", nullptr, kTool,
          ToolExecute<map_tool_t::DISPLACE>, ToolState<map_tool_t::DISPLACE>, pWorkspace },
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
    // Operations wired since they were listed register first (with their
    // own descriptions); their placeholders are skipped.
    if ( const command_registry_status_t status = MapGeometryCommands_Register( pWorkspace, pRegistry );
         status != command_registry_status_t::OK ) {
        return status;
    }
    std::vector<command_desc_t> planned;
    planned.reserve( std::size( kPlanned ) );
    for ( const planned_t &entry : kPlanned ) {
        if ( EditorCommands_Find( pRegistry, StringView_FromCString( entry.pId ) ) != nullptr ) { continue; }
        planned.push_back( { entry.pId, entry.pLabel, entry.pDescription, entry.pIcon, nullptr, COMMAND_FLAG_NONE, NotYetAvailable, NeverEnabled, pWorkspace } );
    }
    return EditorCommands_Register( pRegistry, planned.data(), planned.size() );
}

bool MapWorkspace_IsPlannedCommand( const command_registry_t *pRegistry, string_view_t id ) noexcept
{
    const command_desc_t *pCommand = pRegistry != nullptr ? EditorCommands_Find( pRegistry, id ) : nullptr;
    return pCommand != nullptr && pCommand->pfnExecute == &NotYetAvailable;
}

} // namespace cypher::editor::map
