//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Displacement.h
//  Purpose: Declares displacement surfaces (Source-engine style): a
//           (2^power + 1)^2 grid laid over one quad face and pushed out by
//           per-vertex distances, offsets, a uniform elevation and an
//           optional deterministic noise term; plus resampling to another
//           power, sewing neighbours, and building the result as a mesh.
//  Details: Terrain and organic detail on top of brush work: the quad is
//           usually a brush side, and the grid rides on it, so moving the
//           brush moves the displacement. That is why the descriptor stores
//           displacement *relative* to the quad (distances, offsets) and
//           evaluation always takes the quad as a separate, immutable input.
//           Persistent terrain with tiles, holes and identity is HeightField,
//           not this.
//
//           Grid layout: row-major, (i, j) with i along corner0 -> corner1
//           (u) and j along corner0 -> corner3 (v), so vertex (i, j) is at
//           index j * side + i, side = 2^power + 1. The base point is the
//           bilinear blend of the four corners; points on the quad's border
//           are computed from their edge's two corners only, in a canonical
//           order, so two displacements sharing an edge get bit-identical
//           base points there.
//
//             final = base + dir * (distance + elevation + noise(base)) + offset
//
//           dir is the quad's unit normal (Newell, from the corner winding)
//           unless an explicit axis is given. alpha (0..1 per vertex) is
//           carried for material blending; it does not move anything.
//
//           Noise is value noise summed over octaves (fBm), hashed from an
//           integer seed, so the same descriptor always gives the same
//           surface on every platform (no global RNG, no time).
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DISPLACEMENT_H
#define CYPHER_EDITOR_GEOMETRY_DISPLACEMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSource.h"

namespace cypher::editor::geometry
{

// Power 1..4: 3x3 to 17x17 vertices (Source ships 2..4; 1 is useful for
// blocking out).
inline constexpr common::u32 kDisplacementPowerMin = 1u;
inline constexpr common::u32 kDisplacementPowerMax = 4u;
inline constexpr common::u32 kDisplacementOctavesMax = 8u;
// Sewing tolerance: quad corners closer than this count as shared.
inline constexpr common::f64 kDisplacementSewEpsilon = 1e-6;

struct displacement_noise_t {
    common::f64 amplitude{ 0.0 }; // 0 disables noise
    common::f64 frequency{ 1.0 / 64.0 }; // cycles per world unit
    common::u32 cOctaves{ 1u };
    common::u32 seed{ 0u };
};

struct displacement_t {
    common::u32 power{ 3u };
    common::vector_t<common::f64> distances{}; // side * side
    common::vector_t<math::vec3d_t> offsets{};  // side * side
    common::vector_t<common::f64> alphas{};     // side * side, in [0, 1]
    common::f64 elevation{ 0.0 };
    displacement_noise_t noise{};
    bool bUseAxis{ false };
    math::vec3d_t axis{ 0.0, 0.0, 1.0 }; // used when bUseAxis (unit length)
};

// The face a displacement sits on: corners counter-clockwise seen from
// the side it pushes out of, with their UVs.
struct displacement_quad_t {
    math::vec3d_t corners[4]{};
    math::vec2d_t uvs[4]{};
};

struct displacement_grid_t {
    common::vector_t<math::vec3d_t> positions{};
    common::vector_t<math::vec3d_t> normals{}; // smooth, from the displaced grid
    common::vector_t<math::vec2d_t> uvs{};
    common::vector_t<common::f64> alphas{};
    common::u32 side{ 0u };
};

CYPHER_NODISCARD common::u32 Displacement_Side( common::u32 power ) noexcept; // 2^power + 1, 0 if out of range

// Allocates a flat displacement (all distances/offsets 0, alpha 0) of the
// given power.
CYPHER_NODISCARD geometry_status_t Displacement_Init( displacement_t *pDisp, const common::allocator_t *pAllocator, common::u32 power ) noexcept;
void Displacement_Shutdown( displacement_t *pDisp ) noexcept;
CYPHER_NODISCARD geometry_status_t Displacement_TryClone( const displacement_t *pSource, const common::allocator_t *pAllocator, displacement_t *pOut ) noexcept;

// Checks array sizes, finiteness, alpha range, noise bounds and the axis.
CYPHER_NODISCARD geometry_status_t Displacement_Validate( const displacement_t *pDisp ) noexcept;
// A usable quad: finite corners, non-zero area (DEGENERATE otherwise).
CYPHER_NODISCARD geometry_status_t Displacement_ValidateQuad( const displacement_quad_t &quad ) noexcept;

CYPHER_NODISCARD geometry_status_t DisplacementGrid_Init( displacement_grid_t *pGrid, const common::allocator_t *pAllocator ) noexcept;
void DisplacementGrid_Shutdown( displacement_grid_t *pGrid ) noexcept;

// Evaluates onto the quad. Deterministic; the inputs are not changed.
// Failure leaves the grid empty (side 0).
CYPHER_NODISCARD geometry_status_t Displacement_TryEvaluate(
    const displacement_t *pDisp,
    const displacement_quad_t &quad,
    displacement_grid_t *pGrid ) noexcept;

// Changes the power in place, resampling distances, offsets and alphas
// bilinearly (going down keeps the samples that coincide). Failure leaves
// the displacement unchanged.
CYPHER_NODISCARD geometry_status_t Displacement_TryResample( displacement_t *pDisp, common::u32 newPower ) noexcept;

// Makes two displacements meet along the edge their quads share: each
// shared border vertex moves to the midpoint of the two evaluated points
// (written back as offsets with zero distance, so it holds whatever the
// neighbours' normals are). Needs equal powers (UNSUPPORTED otherwise) and
// one shared edge (INVALID_ARGUMENT otherwise). Failure changes neither.
CYPHER_NODISCARD geometry_status_t Displacement_TrySew(
    displacement_t *pA,
    const displacement_quad_t &quadA,
    displacement_t *pB,
    const displacement_quad_t &quadB ) noexcept;

// Builds the evaluated grid as an authored mesh ((side - 1)^2 quads, one
// material, smoothing group 1, UVs from the quad) with fresh IDs: root,
// then vertices row-major, then faces row-major. Failure leaves *pOut
// empty and the allocator unchanged.
CYPHER_NODISCARD geometry_status_t Displacement_TryBuildMesh(
    const displacement_t *pDisp,
    const displacement_quad_t &quad,
    geometry_material_ref_t material,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    mesh_source_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DISPLACEMENT_H
