//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_CoordinateKey.h
//  Purpose: Declares canonical coordinate quantization and total ordering.
//  Details: Gives authored positions a deterministic identity and sort order
//           so traversal, hashing, and serialization do not depend on the
//           order in which geometry happened to be constructed.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_KERNEL_COORDINATEKEY_H
#define CYPHER_EDITOR_GEOMETRY_KERNEL_COORDINATEKEY_H
#ifndef PRAGMA_ONCE
#pragma once
#endif

#include "CypherGeometry_Policy.h"
#include "CypherGeometry_Types.h"

#include "CypherMath_Vector3.h"

namespace cypher::editor::geometry
{

using common::i64;

// A position snapped to the policy's canonical lattice. Integer, so equality
// and ordering are exact and cannot drift the way float comparison does.
//
// NOT A WELD TEST. Two positions closer together than the quantum still land
// in different cells whenever they straddle a cell boundary, which makes
// bucket equality non-transitive as a proximity relation. Use this for
// deterministic ordering, hashing, and bucketing; use policy.fWeldDistance
// with a real distance test (over neighbouring cells) to decide whether two
// vertices are actually the same point.
struct geometry_coordinate_key_t {
    i64 x;
    i64 y;
    i64 z;
};

// Snaps point onto the canonical lattice defined by
// policy.fCanonicalQuantization. Returns NUMERIC_FAILURE for non-finite input
// and LIMIT_EXCEEDED beyond policy.fCoordinateMagnitudeLimit; the lattice index
// is only guaranteed exactly representable inside that range, which is the
// relationship GeometryNumericalPolicy_IsValid already enforces against
// 2^53 - 1.
CYPHER_NODISCARD geometry_status_t Kernel_TryQuantizePoint(
    const geometry_numerical_policy_t &policy,
    cypher::math::vec3d_t point,
    CY_OUT geometry_coordinate_key_t *pKey ) noexcept;

CYPHER_NODISCARD constexpr bool Kernel_CoordinateKeyEquals(
    geometry_coordinate_key_t a, geometry_coordinate_key_t b ) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Strict weak ordering over x, then y, then z. Any deterministic total order
// works; lexicographic is chosen because it is stable, trivially auditable,
// and groups neighbours along x, which matches how brush sides are swept.
CYPHER_NODISCARD constexpr bool Kernel_CoordinateKeyLess(
    geometry_coordinate_key_t a, geometry_coordinate_key_t b ) noexcept
{
    if ( a.x != b.x ) {
        return a.x < b.x;
    }
    if ( a.y != b.y ) {
        return a.y < b.y;
    }
    return a.z < b.z;
}

}       // namespace cypher::editor::geometry

#endif          // CYPHER_EDITOR_GEOMETRY_KERNEL_COORDINATEKEY_H
