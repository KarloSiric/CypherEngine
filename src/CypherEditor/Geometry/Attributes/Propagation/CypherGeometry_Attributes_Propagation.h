//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_Propagation.h
//  Purpose: Declares how brush-side surfacing follows geometry through
//           transforms, clipping, CSG, and construction.
//  Details: Schema stores what was authored; this module owns the rules
//           for carrying it through an edit:
//
//           - Texture lock. WORLD_LOCKED keeps the projection fixed in
//             world space, so moving a brush slides it under a stationary
//             texture. GEOMETRY_LOCKED carries the projection with the
//             transform (origin, axes, and world scale), so the texture
//             sticks to the moved surface. Radiant calls the latter
//             "texture lock"; both are first-class here.
//
//           - Re-basing. A projection copied onto a face with a different
//             orientation is re-projected into that face's plane, keeping
//             origin, world scale, rotation, and offset. A projection that
//             would be edge-on to its face is never produced.
//
//           - Source selection. A face produced by an operation takes its
//             record from the side it came from (by provenance); a face
//             with no provenance copies the candidate side whose outward
//             normal is closest, the convention map editors use for new
//             cut and hull faces.
//
//           Every output record is orthonormal and passes
//           BrushSideAttributes_Validate.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_PROPAGATION_H
#define CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_PROPAGATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Schema.h"

#include "CypherMath_Affine3.h"

namespace cypher::editor::geometry
{

enum class geometry_texture_lock_t : common::u8 {
    WORLD_LOCKED = 0u,    // projection stays fixed in world space
    GEOMETRY_LOCKED,      // projection moves with the transformed geometry
    COUNT
};

// Re-projects a mapping onto a face with the given outward unit normal.
// When the mapping's normal already agrees with the face (|dot| close to 1)
// the mapping is returned with its normal snapped to the face orientation.
// Otherwise U is projected into the face plane (falling back to V when U is
// nearly normal to the face) and V is rebuilt orthogonally with its
// original handedness. DEGENERATE when no in-plane axis can be recovered.
CYPHER_NODISCARD geometry_status_t AttributePropagation_TryRebaseProjection(
    const geometry_numerical_policy_t &policy,
    const math::planar_uv_mappingd_t &source,
    math::vec3d_t faceNormal,
    math::planar_uv_mappingd_t *pMappingOut ) noexcept;

// Carries a mapping through an affine transform under the given lock
// mode. GEOMETRY_LOCKED transforms the origin as a point, the axes as
// directions (folding their length change into worldUnitsPerUv so the
// texture scales with the surface), orthonormalizes the result, and then
// re-bases it onto transformedFaceNormal. WORLD_LOCKED only re-bases.
CYPHER_NODISCARD geometry_status_t AttributePropagation_TryTransformProjection(
    const geometry_numerical_policy_t &policy,
    const math::planar_uv_mappingd_t &source,
    const math::affine3d_t &transform,
    geometry_texture_lock_t lock,
    math::vec3d_t transformedFaceNormal,
    math::planar_uv_mappingd_t *pMappingOut ) noexcept;

// One candidate donor side for source selection.
struct geometry_attribute_candidate_t {
    geometry_source_id_t sideId{};
    math::vec3d_t outwardNormal{};
    const geometry_brush_side_attributes_t *pAttributes{ nullptr };
};

// Picks the donor for a new face: the candidate whose sideId equals
// preferredSideId when valid and present, otherwise the candidate with the
// largest dot(outwardNormal, faceNormal) (ties to the lower index). When
// `flipped` is true the face is an exposed cut surface and normals are
// compared against the reversed face normal, so a cutter side donates to
// the fragment face it carved. With no candidates the default record is
// used. The chosen record is then re-based onto faceNormal.
CYPHER_NODISCARD geometry_status_t AttributePropagation_TrySelectForFace(
    const geometry_numerical_policy_t &policy,
    const geometry_attribute_candidate_t *pCandidates,
    common::usize cCandidates,
    geometry_source_id_t preferredSideId,
    common::bool_t bFlipped,
    math::vec3d_t faceNormal,
    geometry_brush_side_attributes_t *pAttributesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_ATTRIBUTES_PROPAGATION_H
