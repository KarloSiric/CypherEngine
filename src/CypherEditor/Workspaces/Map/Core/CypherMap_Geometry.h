//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Geometry.h
//  Purpose: Declares the map's readable geometry codec: brushes, meshes,
//           patches, and terrains as written in `.cymapchunk` files
//           (CYMAP.md section 6), converted to and from the geometry
//           library's in-memory objects, plus the map's material table.
//  Details: The map owns its file representation. The geometry library's
//           own document format is an internal detail that may change; the
//           map format is a promise to every level ever saved. So the codec
//           uses only the library's public construction and description
//           API (the same validated paths editing uses) and writes names a
//           person understands: `plane = [ nx, ny, nz, distance ]`, material
//           paths on every face, degrees for texture rotation, meshes as
//           `[x, y, z]` vertices, terrains as height rows.
//
//           Reading splits a record in two: the geometric members become a
//           geometry object; everything else in the record (name, comment,
//           visgroups, terrain paint, mesh modifiers, members this build does
//           not know) stays in the map's record and is written back beside
//           the regenerated geometry.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GEOMETRY_H
#define CYPHER_EDITOR_MAP_GEOMETRY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_HeightField.h"
#include "CypherGeometry_MeshSource.h"
#include "CypherGeometry_Patch.h"

#include "CypherCommon/Mathlib/CypherMath_Bounds.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"

namespace cypher::editor::map
{

inline constexpr common::usize MAP_BRUSH_FACES_MIN = 4u;
inline constexpr common::usize MAP_BRUSH_FACES_MAX = 256u;
inline constexpr common::usize MAP_MESH_FACE_VERTICES_MAX = 256u;
inline constexpr common::usize MAP_MATERIAL_PATH_MAX = 259u;
inline constexpr common::f64 MAP_ANGLE_ROUNDING = 1.0e-9; // Degrees; keeps radian round trips from drifting.

enum class map_geometry_kind_t : common::u8 {
    BRUSH = 0u,
    MESH,
    PATCH,
    TERRAIN,
    COUNT
};

// Chunk section name of each kind: "brushes", "meshes", "patches", "terrains".
CYPHER_NODISCARD const char *MapGeometry_SectionName( map_geometry_kind_t kind ) noexcept;

// The member that carries a kind's geometry ("faces", "vertices", "controls",
// "heights"). A record still holding it after loading is one that could not
// be read and is kept verbatim.
CYPHER_NODISCARD const char *MapGeometry_DataMember( map_geometry_kind_t kind ) noexcept;

/*
================
Materials

Faces name materials by path; the geometry library stores a number. The
table is in memory only: references are handed out in first-seen order and
never written.
================
*/
struct map_material_entry_t {
    common::u32 iOffset{ 0u }; // Into text.
    common::u32 cchLength{ 0u };
};

struct map_materials_t {
    common::vector_t<char> text{};
    common::vector_t<map_material_entry_t> entries{}; // Reference n is entries[n - 1].
    common::vector_t<common::u32> sorted{};           // Entry indices ordered by path, for lookup.
};

CYPHER_NODISCARD common::bool_t MapMaterials_Init( map_materials_t *pMaterials, const common::allocator_t *pAllocator ) noexcept;
void MapMaterials_Shutdown( map_materials_t *pMaterials ) noexcept;

// The reference for a path, added on first use. 0 for an empty path; 0 and
// false on allocation failure or an over-long path.
CYPHER_NODISCARD common::bool_t MapMaterials_Intern( map_materials_t *pMaterials, common::string_view_t path, common::u64 *pRefOut ) noexcept;

// The path of a reference; empty for 0 or an unknown reference.
CYPHER_NODISCARD common::string_view_t MapMaterials_Path( const map_materials_t *pMaterials, common::u64 ref ) noexcept;

/*
================
Identity
================
*/

// Every ID a record names: the object and its parts. IDs that are absent or
// zero are skipped (hand-added parts get IDs from MapGeometry_AssignIds).
CYPHER_NODISCARD common::bool_t MapGeometry_CollectIds(
    map_geometry_kind_t kind,
    const common::key_value_t *pRecord,
    common::vector_t<common::u64> &idsOut ) noexcept;

// Gives the object and every part without an ID a fresh one from *pNextId.
// Returns the number assigned, or CY_INVALID_SIZE on allocation failure.
CYPHER_NODISCARD common::usize MapGeometry_AssignIds(
    map_geometry_kind_t kind,
    common::key_value_document_t *pDocument,
    common::key_value_t *pRecord,
    common::u64 *pNextId ) noexcept;

// Calls pfnVisit for every present, valid ID slot of a record (the object
// and its parts). The callback may rewrite the slot (duplicate
// reassignment); returning false stops the walk and the function returns
// false.
using map_id_slot_fn = common::bool_t ( * )( void *pContext, common::key_value_t *pSlot, common::u64 id ) noexcept;
CYPHER_NODISCARD common::bool_t MapGeometry_ForEachId(
    map_geometry_kind_t kind,
    common::key_value_t *pRecord,
    map_id_slot_fn pfnVisit,
    void *pContext ) noexcept;

/*
================
Reading
================
*/

enum class map_geometry_read_status_t : common::u8 {
    OK = 0u,
    OUT_OF_MEMORY,
    INVALID,           // The record is not readable geometry; keep it verbatim.
    IDENTITY_CONFLICT  // An ID is already used in the geometry document.
};

struct map_geometry_read_result_t {
    map_geometry_read_status_t status{ map_geometry_read_status_t::OK };
    common::u64 id{ 0u };
    const char *pReason{ "" }; // Static text naming the member at fault.
};

// Reads one record and adds its object to the geometry document.
CYPHER_NODISCARD map_geometry_read_result_t MapGeometry_Read(
    map_geometry_kind_t kind,
    const common::key_value_t *pRecord,
    map_materials_t *pMaterials,
    geometry::geometry_document_t *pGeometry,
    const geometry::geometry_policy_t &policy ) noexcept;

// Removes the members MapGeometry_Read consumed, leaving the map's own
// members (name, comment, visgroups, paint, modifiers, unknown members).
// Brush and mesh faces that carry members this build does not know keep
// those members (and their `id`) in a residual `faces` list, so saving can
// put them back on the same faces.
CYPHER_NODISCARD common::bool_t MapGeometry_StripRecord(
    map_geometry_kind_t kind,
    common::key_value_document_t *pDocument,
    common::key_value_t *pRecord ) noexcept;

// Whether a member name belongs to the kind's geometry (written by the
// codec, never copied from the record).
CYPHER_NODISCARD common::bool_t MapGeometry_IsGeometryMember( map_geometry_kind_t kind, common::string_view_t name ) noexcept;

// Appends the residual unknown face members of a stripped record to the
// faces with the same IDs in a freshly written object.
CYPHER_NODISCARD common::bool_t MapGeometry_MergeResidual(
    map_geometry_kind_t kind,
    common::key_value_document_t *pDocument,
    common::key_value_t *pOut,
    const common::key_value_t *pRecord ) noexcept;

/*
================
Bounds

Computed separately from writing because placement needs every object's
position before it knows which chunk the object is written to. bHas is
false when an object has none (a degenerate brush).
================
*/
struct map_bounds_t {
    math::aabbd_t box{};
    common::bool_t bHas{ common::CY_FALSE };
};

void MapBounds_AddPoint( map_bounds_t &bounds, math::vec3d_t point ) noexcept;
void MapBounds_AddBounds( map_bounds_t &bounds, const map_bounds_t &other ) noexcept;
CYPHER_NODISCARD math::vec3d_t MapBounds_Center( const map_bounds_t &bounds ) noexcept;

// An empty bound can represent invalid/degenerate geometry or a failed
// scratch allocation. pStatusOut (optional) distinguishes these cases for
// callers such as saving that must propagate allocation failures.
CYPHER_NODISCARD map_bounds_t MapGeometry_BrushBounds(
    const geometry::brush_source_t &brush,
    const geometry::geometry_policy_t &policy,
    const common::allocator_t *pAllocator,
    geometry::geometry_status_t *pStatusOut = nullptr ) noexcept;
// pnVertices and pnFaces (optional) receive the counts for the record's `info`.
CYPHER_NODISCARD map_bounds_t MapGeometry_MeshBounds(
    const geometry::mesh_source_t &mesh,
    const common::allocator_t *pAllocator,
    common::usize *pnVertices = nullptr,
    common::usize *pnFaces = nullptr,
    geometry::geometry_status_t *pStatusOut = nullptr ) noexcept;
CYPHER_NODISCARD map_bounds_t MapGeometry_PatchBounds( const geometry::patch_surface_t &patch ) noexcept;
// The full box; heights included.
CYPHER_NODISCARD map_bounds_t MapGeometry_TerrainBounds( const geometry::heightfield_t &terrain ) noexcept;
// The horizontal footprint used for placement, so sculpting never moves a
// terrain between cells.
CYPHER_NODISCARD map_bounds_t MapGeometry_TerrainFootprint( const geometry::heightfield_t &terrain ) noexcept;

/*
================
Writing

Each writer appends the kind's geometric members to an output object in
the order CYMAP.md lists them.
================
*/
CYPHER_NODISCARD common::bool_t MapGeometry_WriteBrush(
    const geometry::brush_source_t &brush,
    const map_materials_t &materials,
    common::key_value_document_t *pDocument,
    common::key_value_t *pOut ) noexcept;

CYPHER_NODISCARD common::bool_t MapGeometry_WriteMesh(
    const geometry::mesh_source_t &mesh,
    const map_materials_t &materials,
    const common::allocator_t *pAllocator,
    common::key_value_document_t *pDocument,
    common::key_value_t *pOut ) noexcept;

CYPHER_NODISCARD common::bool_t MapGeometry_WritePatch(
    const geometry::patch_surface_t &patch,
    const map_materials_t &materials,
    common::key_value_document_t *pDocument,
    common::key_value_t *pOut ) noexcept;

CYPHER_NODISCARD common::bool_t MapGeometry_WriteTerrain(
    const geometry::heightfield_t &terrain,
    common::key_value_document_t *pDocument,
    common::key_value_t *pOut ) noexcept;

// Writes `info = { bounds = [...] ... }` into an object.
CYPHER_NODISCARD common::bool_t MapGeometry_WriteBounds(
    common::key_value_document_t *pDocument,
    common::key_value_t *pObject,
    const char *pName,
    const map_bounds_t &bounds ) noexcept;

// -0.0 becomes 0.0 so one value has one spelling.
CYPHER_NODISCARD common::f64 MapReal( common::f64 value ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GEOMETRY_H
