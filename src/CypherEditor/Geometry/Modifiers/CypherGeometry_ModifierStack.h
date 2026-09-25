//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_ModifierStack.h
//  Purpose: Declares modifier stacks: an ordered list of non-destructive
//           operations (mirror, linear array, radial array, bend, taper,
//           subdivide) evaluated over an authored mesh, with provenance,
//           and collapsed into a new mesh on request.
//  Details: The cage stays the authored source; the stack describes what
//           to do with it, so editing the cage re-runs the stack (Blender's
//           modifier workflow, which Hammer lacks). Each stage takes the
//           previous stage's polygons, so order matters: mirror then array
//           repeats the mirrored shape; array then bend bends the whole row.
//
//           What this file does not do (yet): store a stack in the geometry
//           document or the map file. Retained recipes are an approval gate
//           in the Modifiers README (they add persistent, versioned data to
//           the schema), so a stack is a transient value the host holds
//           until that is decided. `version` is here so the stored form can
//           evolve once it exists.
//
//           Stages:
//             MIRROR        reflect across the plane axis = offset (in the
//                           mesh's space), keep both halves; vertices within
//                           weldDistance of the plane are shared, so a half
//                           mesh open along the plane closes up.
//             LINEAR_ARRAY  cCopies copies stepped by `step`; with weld,
//                           coincident vertices of neighbouring copies merge.
//             RADIAL_ARRAY  cCopies copies rotated about the line through
//                           `origin` along `direction` - over a full turn
//                           when angle is 2*pi (no copy on top of the
//                           first), otherwise spread so the last copy is at
//                           `angle`.
//             BEND          bends the mesh around `axis` so its extent along
//                           `alongAxis` curls through `angle` radians about
//                           `origin` (length measured over the mesh's
//                           extent at this stage).
//             TAPER         scales the two axes perpendicular to `alongAxis`
//                           linearly from 1 at the low end to `factor` at
//                           the high end of the extent.
//             SUBDIVIDE     retained subdivision (Procedural/Subdivision).
//           A disabled stage is skipped.
//
//           Provenance: each output vertex and face records which copy it
//           belongs to (arrays and mirrors number copies in order; the
//           original is copy 0) and which cage vertex / face it came from
//           (CY_U32_MAX for vertices a subdivide stage created).
//
//           Collapse keeps the cage's identities on copy 0 and gives every
//           other element a fresh ID.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MODIFIER_STACK_H
#define CYPHER_EDITOR_GEOMETRY_MODIFIER_STACK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_SubdivisionSurface.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kModifierStackVersion = 1u;
inline constexpr common::u32 kModifierStagesMax = 32u;
inline constexpr common::u32 kModifierCopiesMax = 256u;

enum class modifier_kind_t : common::u8 {
    MIRROR = 0u,
    LINEAR_ARRAY,
    RADIAL_ARRAY,
    BEND,
    TAPER,
    SUBDIVIDE
};

struct modifier_t {
    modifier_kind_t kind{ modifier_kind_t::MIRROR };
    bool bEnabled{ true };
    // MIRROR: plane axis (0..2) = offset. BEND: bend-around axis. TAPER and
    // BEND: alongAxis is the axis measured along (must differ from axis).
    common::u32 axis{ 0u };
    common::u32 alongAxis{ 0u };
    common::f64 offset{ 0.0 };
    common::f64 weldDistance{ 1e-6 };
    bool bWeld{ true };
    // Arrays.
    common::u32 cCopies{ 2u };
    math::vec3d_t step{ 64.0, 0.0, 0.0 };
    math::vec3d_t origin{};
    math::vec3d_t direction{ 0.0, 0.0, 1.0 };
    common::f64 angle{ 0.0 };  // RADIAL_ARRAY and BEND, radians
    common::f64 factor{ 1.0 }; // TAPER
    subdivision_descriptor_t subdivision{};
};

struct modifier_stack_t {
    common::u32 version{ kModifierStackVersion };
    common::vector_t<modifier_t> stages{};
};

struct modifier_result_t {
    mesh_source_description_t mesh{};
    common::vector_t<common::u32> vertexCopies{};   // per mesh vertex
    common::vector_t<common::u32> vertexSources{};  // cage vertex index or CY_U32_MAX
    common::vector_t<common::u32> faceCopies{};     // per mesh face
    common::vector_t<common::u32> faceSources{};    // cage face index
};

CYPHER_NODISCARD geometry_status_t ModifierStack_Init( modifier_stack_t *pStack, const common::allocator_t *pAllocator ) noexcept;
void ModifierStack_Shutdown( modifier_stack_t *pStack ) noexcept;
CYPHER_NODISCARD geometry_status_t ModifierStack_TryPush( modifier_stack_t *pStack, const modifier_t &modifier ) noexcept;

CYPHER_NODISCARD geometry_status_t ModifierResult_Init( modifier_result_t *pResult, const common::allocator_t *pAllocator ) noexcept;
void ModifierResult_Shutdown( modifier_result_t *pResult ) noexcept;

// Checks one stage's parameters (axes, counts, finite values, a unit-able
// direction, a valid subdivision descriptor).
CYPHER_NODISCARD geometry_status_t Modifier_Validate( const modifier_t &modifier ) noexcept;
CYPHER_NODISCARD geometry_status_t ModifierStack_Validate( const modifier_stack_t *pStack ) noexcept;

// Runs the stack over the cage into *pResult (initialized; replaced).
// NON_MANIFOLD when a stage would put three faces on an edge or reuse a
// directed edge (e.g. a mirror of a mesh with a face lying in the mirror
// plane); DEGENERATE when welding collapses a face; LIMIT_EXCEEDED past
// the subdivision bounds. Failure leaves the result empty.
CYPHER_NODISCARD geometry_status_t ModifierStack_TryEvaluate(
    const mesh_source_t *pCage,
    const modifier_stack_t *pStack,
    modifier_result_t *pResult ) noexcept;

// Evaluates and builds a new authored mesh in canonical-empty *pOut: the
// root keeps the cage's ID, copy 0's cage vertices and faces keep theirs,
// everything else gets fresh IDs from *pIdAllocator (failure leaves it and
// *pOut unchanged).
CYPHER_NODISCARD geometry_status_t ModifierStack_TryCollapse(
    const mesh_source_t *pCage,
    const modifier_stack_t *pStack,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    mesh_source_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MODIFIER_STACK_H
