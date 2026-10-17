//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_TextureEdit.h
//  Purpose: Declares material alignment on authored surfaces: Hammer's and
//           TrenchBroom's Align to World / Face, Fit, Justify, and the
//           rotate, scale and shift adjustments.
//  Details: A surface is a brush side or a mesh face addressed by persistent
//           IDs; face 0 means every face of that object, which is how a
//           whole-object selection is aligned. Brushes edit the side's
//           projection record: a record other sides share is split first,
//           so aligning one face never moves the texture on another. Meshes
//           edit their corner UVs through MapMeshEdit_Apply.
//
//           PRIVATE working-copy contract as CypherMap_MeshEdit.h.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_TEXTURE_EDIT_H
#define CYPHER_EDITOR_MAP_TEXTURE_EDIT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"
#include "CypherGeometry_MeshUvTransform.h"

#include <string>
#include <vector>

namespace cypher::editor::map
{

struct map_surface_ref_t {
    common::u64 object{ 0u };
    common::u64 face{ 0u }; // Brush side or mesh face ID; 0 = every face.
};

enum class map_uv_align_t : common::u8 {
    WORLD = 0u,     // Project along the face's dominant world axis (Quake/Hammer world alignment).
    FACE,           // Project in the face's own plane, so sloped faces do not stretch.
    FIT,            // One texture repeat exactly covers the face.
    JUSTIFY_LEFT,   // Texture edge on the face's minimum U.
    JUSTIFY_RIGHT,
    JUSTIFY_TOP,    // Texture edge on the face's maximum V.
    JUSTIFY_BOTTOM,
    JUSTIFY_CENTER, // Texture centred on the face.
    COUNT
};

// World and Face keep each face's texture scale and clear rotation and
// shift. Mesh faces have no projection record; meshWorldUnitsPerUv is the
// repeat size their World/Face alignment uses.
CYPHER_NODISCARD map_status_t MapTextureEdit_Align( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    map_uv_align_t align, common::f64 meshWorldUnitsPerUv ) noexcept;

// Rotates each face's texture about the face's centre.
CYPHER_NODISCARD map_status_t MapTextureEdit_Rotate( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    common::f64 degrees ) noexcept;

// Scales each face's texture about the face's centre: 2 makes it twice as
// large on the surface, a negative factor mirrors that axis.
CYPHER_NODISCARD map_status_t MapTextureEdit_Scale( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    math::vec2d_t factor ) noexcept;

// Moves each face's texture by a distance on the surface along its U and V
// axes. Mesh faces convert it with meshWorldUnitsPerUv.
CYPHER_NODISCARD map_status_t MapTextureEdit_Shift( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    math::vec2d_t worldDistance, common::f64 meshWorldUnitsPerUv ) noexcept;

// Unwraps mesh faces into UV islands (UV Islands): connected faces facing
// the same world axis form one chart, projected flat without stretching
// along that axis, and the charts are packed side by side into the unit
// square with a small margin, so one texture covers the mesh once. Brush
// sides keep their planar projections and are not affected.
CYPHER_NODISCARD map_status_t MapTextureEdit_Unwrap( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces ) noexcept;

// Replaces one material with another everywhere it is used: brush sides,
// mesh faces and patches. UV projections stay as they are. *pReplacedOut
// (optional) receives how many sides, faces and patches changed; none is
// a successful no-op that leaves the revision alone.
CYPHER_NODISCARD map_status_t MapTextureEdit_ReplaceMaterial( map_document_t *pMap, common::string_view_t from,
    common::string_view_t to, common::usize *pReplacedOut ) noexcept;

// Sets each face's texture repeat size (world units per repeat along U and
// V) about the face's centre, keeping its rotation and shift: brush sides
// rescale their projection, mesh faces are projected along the world axis
// they face at that size.
CYPHER_NODISCARD map_status_t MapTextureEdit_SetRepeat( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    math::vec2d_t worldUnitsPerRepeat ) noexcept;

// Texture projections for mesh faces (brush sides keep their own planar
// projections and are skipped): BOX projects each face along the world
// axis it faces most (tri-planar); CYLINDER wraps around the line through
// `origin` along `axis` (U around, V along); PLANAR projects every face
// along `axis` (a decal-like projection from one direction). Repeat sizes
// are world units per texture repeat.
enum class map_uv_projection_t : common::u8 { BOX = 0u, CYLINDER, PLANAR };
CYPHER_NODISCARD map_status_t MapTextureEdit_Project( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    map_uv_projection_t projection, math::vec3d_t origin, math::vec3d_t axis, common::f64 radius, math::vec2d_t worldUnitsPerRepeat ) noexcept;

// Smoothing groups of mesh faces (a 32-bit mask; 0 is flat shaded): faces
// sharing a group shade smoothly across their common edges.
CYPHER_NODISCARD map_status_t MapTextureEdit_SetSmoothingGroups( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    common::u32 groups ) noexcept;

// Auto hard edges: every interior edge whose faces meet at more than
// `degrees` becomes hard, the rest soft (open edges stay hard).
// *pHardOut (optional) counts the hard edges over all the meshes.
CYPHER_NODISCARD map_status_t MapTextureEdit_MarkHardEdgesByAngle( map_document_t *pMap, common::span_t<const common::u64> meshes,
    common::f64 degrees, common::usize *pHardOut ) noexcept;

// Puts one material on surfaces (Hammer's Apply Current Material, Shift+T):
// brush sides, mesh faces, and whole patches. A reference with face 0 means
// every face of that brush or mesh. UV projections stay as they are.
// *pChangedOut (optional) counts the faces and patches that took the
// material; when every one already had it the map is unchanged.
CYPHER_NODISCARD map_status_t MapTextureEdit_SetMaterial( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    common::string_view_t material, common::usize *pChangedOut ) noexcept;

// Hammer 5's Fast Texture: the faces' texture coordinates fitted into one
// rectangle of the texture (UV 0..1 across the image, V downward). EXISTING
// keeps the faces' current layout and fits it as a whole; PLANAR first
// projects them flat along their average normal; SQUARE fits each face
// into the rectangle on its own. Flips mirror the result inside the
// rectangle. Brush sides keep a planar projection, so each side is fitted
// on its own whatever the mode (PLANAR first aligns it to its face).
enum class map_uv_fit_mode_t : common::u8 { EXISTING = 0u, PLANAR, SQUARE };
struct map_uv_rect_t {
    math::vec2d_t minimum{ 0.0, 0.0 };
    math::vec2d_t maximum{ 1.0, 1.0 };
    bool bFlipU{ false };
    bool bFlipV{ false };
};
CYPHER_NODISCARD map_status_t MapTextureEdit_FitToRect( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    const map_uv_rect_t &rect, map_uv_fit_mode_t mode ) noexcept;

// Hammer 5's hotspot texturing: each face takes the region of `rects` its
// size matches best (CypherMap_Hotspots.h). Mesh faces go through the
// geometry library; brush sides are aligned to their face first, measured
// at one world unit per UV, and fitted the same way.
CYPHER_NODISCARD map_status_t MapTextureEdit_ApplyHotspots( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces,
    const std::vector<geometry::mesh_hotspot_rect_t> &rects ) noexcept;

// Hammer's texture wrap for brushes: the material and projection of brush
// side `from` put on each side in `to`. A projection is anchored in the
// world, so sides facing the same way continue the texture seamlessly; the
// rest take only the material. *pChangedOut (optional) counts the sides
// that changed; none is a successful no-op that leaves the revision alone.
CYPHER_NODISCARD map_status_t MapTextureEdit_CopyBrushSide( map_document_t *pMap, map_surface_ref_t from,
    common::span_t<const map_surface_ref_t> to, common::usize *pChangedOut ) noexcept;
// The brush sides coplanar with brush side `seed` and touching it through
// other coplanar sides, across every brush in the map (a floor built from
// many brushes), seed included.
CYPHER_NODISCARD map_status_t MapTextureEdit_CoplanarBrushSides( const map_document_t *pMap, map_surface_ref_t seed,
    std::vector<map_surface_ref_t> &out );
// The UV editor's view of one face (TrenchBroom's face inspector UV view):
// its material, its corners in the world, and each corner's texture
// coordinate in repeats of the texture (V running down the image).
struct map_uv_face_t {
    map_surface_ref_t surface{};
    std::string material{};
    bool bBrush{ false }; // A brush side (a planar projection) rather than a mesh face (per-corner coordinates).
    std::vector<math::vec3d_t> corners{};
    std::vector<math::vec2d_t> uvs{};
};
// A face that does not exist, or is not a brush side or mesh face:
// UNKNOWN_OBJECT.
CYPHER_NODISCARD map_status_t MapTextureEdit_Describe( const map_document_t *pMap, map_surface_ref_t surface, map_uv_face_t *pOut );

// An edit made in texture space, the same for every face it is applied to:
// move the texture coordinates by `delta`, or turn them by `radians`, scale
// them by `factor` (2: the face covers twice as much of the texture), or
// shear them (u gains shear.x * v, v gains shear.y * u) about `pivot`, a
// point in texture space.
enum class map_uv_edit_kind_t : common::u8 { SHIFT = 0u, ROTATE, SCALE, SHEAR };
struct map_uv_edit_t {
    map_uv_edit_kind_t kind{ map_uv_edit_kind_t::SHIFT };
    math::vec2d_t pivot{ 0.0, 0.0 };
    math::vec2d_t delta{ 0.0, 0.0 };
    common::f64 radians{ 0.0 };
    math::vec2d_t factor{ 1.0, 1.0 };
    math::vec2d_t shear{ 0.0, 0.0 };
};
// Brush sides hold every edit exactly: a scale across a turned texture or a
// shear turns the side's projection axes in the face plane (see
// UvTools_TryTransformUv). Surfaces name single faces (face != 0).
CYPHER_NODISCARD map_status_t MapTextureEdit_EditUv( map_document_t *pMap, common::span_t<const map_surface_ref_t> surfaces, const map_uv_edit_t &edit ) noexcept;
} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_TEXTURE_EDIT_H
