//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_Schema.h
//  Purpose: Declares attribute domains, material references, and the brush-side
//           attribute record.
//  Details: Value types only. Storage lives in
//           CypherGeometry_Attributes_BrushSideStore.h, and anything that
//           interpolates or propagates these across an edit belongs to
//           Attributes/Propagation, not here.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_SCHEMA_H
#define CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_SCHEMA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Policy.h"
#include "CypherGeometry_Types.h"

#include "CypherMath_UV.h"

namespace cypher::editor::geometry
{

// Where an attribute value is anchored. These are distinct because the same
// logical attribute means different things per domain: a normal stored per
// VERTEX is shared by every face meeting there, while one stored per CORNER can
// differ per face and is what makes a hard edge representable at all.
//
// Not every representation supports every domain -- BRUSH_SIDE has no meaning
// for an EditableMesh, and CONTROL_POINT has none for a BrushSolid. The owning
// representation rejects domains it does not implement rather than this enum
// pretending they are universal.
enum class geometry_attribute_domain_t : common::u8 {
    INVALID = 0u,
    REPRESENTATION_ROOT, // One value for the whole source object.
    VERTEX,              // Shared by every element meeting at a position.
    CORNER,              // Per face-vertex; permits splits such as hard edges.
    EDGE,                // Per undirected edge; creases and weights.
    FACE,                // Per face.
    BRUSH_SIDE,          // Per plane-defined brush side.
    CONTROL_POINT,       // Patch and curve control points.
    SAMPLE,              // Height-field and raster samples.
    COUNT                // Enum bound; never a stored value.
};

CYPHER_NODISCARD constexpr bool GeometryAttributeDomain_IsValid(
    geometry_attribute_domain_t domain ) noexcept
{
    return static_cast<common::u8>( domain ) >
               static_cast<common::u8>( geometry_attribute_domain_t::INVALID ) &&
           static_cast<common::u8>( domain ) <
               static_cast<common::u8>( geometry_attribute_domain_t::COUNT );
}

// An opaque handle to whatever the host calls a material. Geometry never
// resolves, loads, compares by name, or reference-counts it -- doing any of
// that here would drag asset loading and shader binding into a library that
// must stay Qt-free and renderer-free. The host owns the mapping from this
// value to an actual material; geometry only stores and copies it.
//
// Zero means "unassigned" rather than "material zero", so a default-constructed
// record is distinguishable from one deliberately pointing at a real material.
struct geometry_material_ref_t {
    common::u64 value{ 0u };
};

inline constexpr geometry_material_ref_t GEOMETRY_MATERIAL_REF_UNASSIGNED{};

CYPHER_NODISCARD constexpr bool GeometryMaterialRef_IsAssigned(
    geometry_material_ref_t reference ) noexcept
{
    return reference.value != 0u;
}

CYPHER_NODISCARD constexpr bool GeometryMaterialRef_Equals(
    geometry_material_ref_t a, geometry_material_ref_t b ) noexcept
{
    return a.value == b.value;
}

// Everything authored about one brush side's surfacing. The UV mapping is
// binary64 because its origin is a world point and world-locked texturing must
// survive a face drag on geometry far from the world origin.
//
// Texture lock MODE is deliberately absent: that is an operation-time policy
// about how this record should be updated during an edit, which
// Attributes/Propagation owns. Schema stores what was authored, not the rules
// for changing it.
struct geometry_brush_side_attributes_t {
    geometry_material_ref_t material{};
    cypher::math::planar_uv_mappingd_t uvProjection{};
};

// Returns a record with no material and an axis-aligned unit-scale projection.
// A default-constructed geometry_brush_side_attributes_t has a zeroed mapping,
// whose zero-length axes and zero UV scale are NOT valid for projection, so a
// usable default has to be built rather than value-initialized.
CYPHER_NODISCARD geometry_brush_side_attributes_t
BrushSideAttributes_MakeDefault() noexcept;

// Checks that the record could actually be used to project a point: finite
// throughout, axes long enough to normalize, and a non-degenerate UV scale.
// Validation is separate from storage so a caller can reject bad input at the
// boundary instead of discovering it mid-tessellation.
CYPHER_NODISCARD geometry_status_t BrushSideAttributes_Validate(
    const geometry_numerical_policy_t &policy,
    const geometry_brush_side_attributes_t &attributes ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_SCHEMA_H
