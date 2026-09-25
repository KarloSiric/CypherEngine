//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Patch.h
//  Purpose: Declares the Patch source representation: a rectangular grid
//           of control points interpreted as a tensor-product Bézier
//           surface (biquadratic or bicubic), with per-control UVs and
//           source identity.
//  Details: The grid is a chain of sub-patches that share their boundary
//           control rows/columns, the same layout id Tech 3 map patches
//           use (3x3, 5x3, 9x3, ... for quadratic). Sharing boundaries
//           gives C0 continuity for free and lets one authored object
//           span a whole curved wall.
//
//           Control grid layout is row-major: index = iRow * cColumns +
//           iColumn. Columns advance the surface parameter u, rows advance
//           v. The front face is the side dPdu x dPdv points towards, so
//           transposing or reversing an axis flips the face.
//
//           Why UVs live on control points: authored texture layout on a
//           patch is a second Bézier field over the same basis, so it
//           stays consistent under refinement (InsertColumn/InsertRow)
//           and tessellation at any density without re-projecting.
//
//           Identity: the patch and every control point carry a source ID.
//           Refinement keeps existing control IDs and allocates new ones
//           for inserted controls, so selections of individual controls
//           survive an edit that increases density.
//
//           Bounds: kPatch* limits; every mutation is failure-atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PATCH_H
#define CYPHER_EDITOR_GEOMETRY_PATCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherCommon_Vector.h"
#include "CypherCommon_Span.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// Per-axis and total control limits. 257 = 128 quadratic sub-patches per
// axis, far beyond hand-authored use but small enough that a full grid
// evaluation stays a bounded editor operation.
inline constexpr common::u32 kPatchControlsPerAxisMax = 257u;
inline constexpr common::u32 kPatchControlsMax = 65536u;

// Largest absolute coordinate a valid patch may hold. Matches the brush
// authoring range so patches and brushes can share snapping and cooking.
inline constexpr common::f64 kPatchCoordinateMax = 1048576.0;

enum class patch_basis_t : common::u8 {
    BIQUADRATIC_BEZIER = 0u, // degree 2 per axis; controls per axis = 2k + 1
    BICUBIC_BEZIER     = 1u  // degree 3 per axis; controls per axis = 3k + 1
};

struct patch_control_t {
    math::vec3d_t position{};
    math::vec2d_t uv{};
    geometry_source_id_t sourceId{};
};

struct patch_surface_t {
    common::vector_t<patch_control_t> controls{};
    common::u32 cColumns{ 0u };
    common::u32 cRows{ 0u };
    patch_basis_t basis{ patch_basis_t::BIQUADRATIC_BEZIER };
    common::u32 materialId{ 0u };
    geometry_source_id_t sourceId{};
};

// Result of evaluating the surface at one parameter. dPdu/dPdv are
// derivatives with respect to the *global* parameters in [0, 1].
struct patch_sample_t {
    math::vec3d_t position{};
    math::vec3d_t dPdu{};
    math::vec3d_t dPdv{};
    math::vec3d_t normal{};   // unit front-face normal, zero if unresolved
    math::vec2d_t uv{};
    // True when the analytic normal was degenerate (collapsed edge, e.g. a
    // cone apex) and the normal was taken from a nudged interior sample.
    bool bNormalFromNeighbour{ false };
    bool bNormalValid{ false };
};

// ---------------------------------------------------------------------------
// Lifecycle and building
// ---------------------------------------------------------------------------

// Degree of a basis (2 or 3), or 0 for an invalid basis value.
CYPHER_NODISCARD common::u32 Patch_Degree( patch_basis_t basis ) noexcept;

// True when `count` controls form a whole number of sub-patches along one
// axis for `basis` (at least one sub-patch, within kPatchControlsPerAxisMax).
CYPHER_NODISCARD bool Patch_IsValidAxisCount( patch_basis_t basis, common::u32 count ) noexcept;

// Allocates a cColumns x cRows grid into a canonical empty destination.
// Controls start at the origin with zero
// UV and INVALID source IDs; a patch is not Validate()-clean until every
// control has been assigned an ID (TrySetControl or TryInitFlat).
CYPHER_NODISCARD geometry_status_t Patch_Init(
    patch_surface_t *pPatch,
    const common::allocator_t *pAllocator,
    patch_basis_t basis,
    common::u32 cColumns,
    common::u32 cRows,
    geometry_source_id_t patchId ) noexcept;

// Convenience: Init + a flat grid spanning origin + [0,1]*uAxis +
// [0,1]*vAxis with UVs equal to the normalized grid coordinates. Control
// IDs are allocated row-major. Failure-atomic including the ID allocator.
CYPHER_NODISCARD geometry_status_t Patch_TryInitFlat(
    patch_surface_t *pPatch,
    const common::allocator_t *pAllocator,
    patch_basis_t basis,
    common::u32 cColumns,
    common::u32 cRows,
    math::vec3d_t origin,
    math::vec3d_t uAxis,
    math::vec3d_t vAxis,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept;

void Patch_Shutdown( patch_surface_t *pPatch ) noexcept;

// True only when the vector storage, basis, dimensions, and control count form
// a complete, safely indexable patch. It does not validate control values or
// source-ID uniqueness; use Patch_Validate for authored-data validation.
CYPHER_NODISCARD bool Patch_IsInitialized( const patch_surface_t *pPatch ) noexcept;

// Deep copy into an uninitialized destination using pAllocator.
CYPHER_NODISCARD geometry_status_t Patch_TryClone(
    const patch_surface_t *pSource,
    const common::allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept;

// Control access. Out-of-range indices return nullptr.
CYPHER_NODISCARD const patch_control_t *Patch_Control(
    const patch_surface_t *pPatch, common::u32 iColumn, common::u32 iRow ) noexcept;

// Replaces one control. Position/UV must be finite and within
// kPatchCoordinateMax; the ID must be valid and unique across the patch object
// and every other control.
CYPHER_NODISCARD geometry_status_t Patch_TrySetControl(
    patch_surface_t *pPatch,
    common::u32 iColumn,
    common::u32 iRow,
    patch_control_t control ) noexcept;

// Number of sub-patches along u (columns) and v (rows).
CYPHER_NODISCARD common::u32 Patch_SubPatchColumns( const patch_surface_t *pPatch ) noexcept;
CYPHER_NODISCARD common::u32 Patch_SubPatchRows( const patch_surface_t *pPatch ) noexcept;

// Row-major grid index of local control (i, j) of sub-patch (iSubU, iSubV),
// with i, j in [0, degree]. Returns CY_INVALID_INDEX when out of range.
// This is the provenance link from tessellated output back to the exact
// control points that shaped it.
CYPHER_NODISCARD common::u32 Patch_SubPatchControlIndex(
    const patch_surface_t *pPatch,
    common::u32 iSubU,
    common::u32 iSubV,
    common::u32 i,
    common::u32 j ) noexcept;

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

// Evaluates the surface at global (u, v) in [0, 1]^2 (clamped). A parameter
// exactly on a shared sub-patch boundary is evaluated by the higher-index
// sub-patch at local parameter 0, except 1.0, which belongs to the last
// sub-patch at local parameter 1. Both sides agree on position because the
// boundary controls are shared; derivatives may differ at a crease, which is
// why the convention is fixed (PatchTessellation follows the same rule).
CYPHER_NODISCARD patch_sample_t Patch_Evaluate(
    const patch_surface_t *pPatch, common::f64 u, common::f64 v ) noexcept;

// Evaluates one sub-patch at local (s, t) in [0, 1]^2. Derivatives are with
// respect to the local parameters.
CYPHER_NODISCARD patch_sample_t Patch_EvaluateSubPatch(
    const patch_surface_t *pPatch,
    common::u32 iSubU,
    common::u32 iSubV,
    common::f64 s,
    common::f64 t ) noexcept;

// Axis-aligned bounds of the control grid. By the convex-hull property of
// Bézier surfaces this also bounds the surface, so it is safe for culling
// and spatial indexing without tessellating.
CYPHER_NODISCARD math::aabbd_t Patch_ControlBounds( const patch_surface_t *pPatch ) noexcept;

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

// Splits sub-patch column iSubU at its parameter midpoint (de Casteljau on
// every control row), inserting `degree` new control columns. The surface
// shape and UV field are unchanged; existing control IDs are kept and the
// inserted controls take fresh IDs from *pIdAllocator. Failure-atomic.
CYPHER_NODISCARD geometry_status_t Patch_TryInsertColumn(
    patch_surface_t *pPatch,
    common::u32 iSubU,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept;

// Row counterpart of TryInsertColumn.
CYPHER_NODISCARD geometry_status_t Patch_TryInsertRow(
    patch_surface_t *pPatch,
    common::u32 iSubV,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept;

// Swaps the u and v axes (and therefore flips the front face).
CYPHER_NODISCARD geometry_status_t Patch_TryTranspose( patch_surface_t *pPatch ) noexcept;

// Reverses the column order, which flips the front face while keeping the
// u/v roles. Used by "invert patch" in the editor.
void Patch_ReverseColumns( patch_surface_t *pPatch ) noexcept;

// Applies an affine transform to every control position. UVs are authored
// data and are left untouched (texture lock is the caller's decision).
// Returns NUMERIC_FAILURE without modifying the patch if any result is
// non-finite or leaves the coordinate range.
CYPHER_NODISCARD geometry_status_t Patch_TryTransform(
    patch_surface_t *pPatch,
    const math::affine3d_t &transform ) noexcept;

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

enum class patch_fault_t : common::u8 {
    NONE = 0u,
    NOT_INITIALIZED,
    INVALID_DIMENSIONS,
    INVALID_SOURCE_ID,       // patch or a control lacks an ID
    DUPLICATE_SOURCE_ID,     // controls share an ID, or a control uses the patch ID
    NON_FINITE,
    COORDINATE_RANGE,
    COLLAPSED_SURFACE,       // every control coincides: no surface at all
    VALIDATION_INCOMPLETE    // scratch allocation failed; nothing verified
};

struct patch_validation_result_t {
    patch_fault_t fault{ patch_fault_t::NONE };
    common::u32 iControl{ CY_INVALID_INDEX }; // first offending control
};

// Reports the first fault in a fixed order. Does not repair anything.
// Collapsed *edges* (cone apex, end caps) are legal and not reported; only
// a patch whose controls are all identical is rejected.
CYPHER_NODISCARD patch_validation_result_t Patch_Validate(
    const patch_surface_t *pPatch,
    const common::allocator_t *pScratchAllocator ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PATCH_H
