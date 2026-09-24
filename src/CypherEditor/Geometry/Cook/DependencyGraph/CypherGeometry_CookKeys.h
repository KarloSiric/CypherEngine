//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookKeys.h
//  Purpose: Declares deterministic cook dependency keys: a content hash per
//           source object in an immutable snapshot, a policy hash, product
//           keys derived from them, and the diff between two key sets that
//           tells an incremental cook exactly which products to rebuild.
//  Details: Why content hashes rather than revisions: the document revision
//           advances on every commit anywhere, so it cannot say *which*
//           object changed. A source hash depends only on the object's own
//           canonical data, so an edit to one brush changes that brush's key
//           and leaves every other key byte-identical - the DependencyGraph
//           acceptance gate.
//
//           Canonical data per kind:
//             brush - source ID, then per side in order: side ID, plane
//                     normal and distance, attribute index;
//             mesh  - the canonical description (MeshSource.h): root ID,
//                     vertices, corners with attributes, faces with
//                     attributes, non-default edges.
//           Values are encoded explicitly little-endian (f64 by bit
//           pattern), so keys are identical across platforms and compilers.
//
//           product key = H(kCookKeysVersion, source kind, product kind,
//                           policy hash, source hash)
//           A policy change therefore invalidates every product, and bumping
//           kCookKeysVersion (any change to a cook's output format)
//           invalidates everything cooked by older builds.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_KEYS_H
#define CYPHER_EDITOR_GEOMETRY_COOK_KEYS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Snapshot.h"
#include "CypherCommon/Tier1/CypherCommon_ContentHash.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kCookKeysVersion = 1u;

enum class cook_source_kind_t : common::u8 {
    INVALID = 0u,
    BRUSH   = 1u,
    MESH    = 2u
};

enum class cook_product_kind_t : common::u8 {
    INVALID   = 0u,
    COLLISION = 1u,
    RENDER    = 2u
};

struct cook_source_key_t {
    geometry_source_id_t sourceId{};
    cook_source_kind_t kind{ cook_source_kind_t::INVALID };
    common::content_hash_t sourceHash{};
};

struct cook_key_set_t {
    common::vector_t<cook_source_key_t> keys{}; // ascending by sourceId
    common::content_hash_t policyHash{};
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };
};

struct cook_key_diff_t {
    common::vector_t<geometry_source_id_t> added{};
    common::vector_t<geometry_source_id_t> removed{};
    common::vector_t<geometry_source_id_t> changed{};
    common::vector_t<geometry_source_id_t> unchanged{};
    bool bPolicyChanged{ false };
};

// Hash of every numerical and limit policy field.
CYPHER_NODISCARD common::content_hash_t CookKeys_PolicyHash( const geometry_policy_t &policy ) noexcept;

CYPHER_NODISCARD geometry_status_t CookKeySet_Init(
    cook_key_set_t *pSet,
    const common::allocator_t *pAllocator ) noexcept;

void CookKeySet_Shutdown( cook_key_set_t *pSet ) noexcept;

// Computes keys for every brush and mesh in the snapshot (replacing the
// set's contents). Keys are sorted by source ID, so the set does not depend
// on document order.
CYPHER_NODISCARD geometry_status_t CookKeySet_TryBuild(
    cook_key_set_t *pSet,
    const geometry_snapshot_t *pSnapshot ) noexcept;

// Key for one product of one source under the set's policy.
CYPHER_NODISCARD common::content_hash_t CookKeys_ProductKey(
    const cook_key_set_t *pSet,
    const cook_source_key_t &source,
    cook_product_kind_t product ) noexcept;

// Looks up a source key; nullptr when absent.
CYPHER_NODISCARD const cook_source_key_t *CookKeySet_Find(
    const cook_key_set_t *pSet,
    geometry_source_id_t sourceId ) noexcept;

CYPHER_NODISCARD geometry_status_t CookKeyDiff_Init(
    cook_key_diff_t *pDiff,
    const common::allocator_t *pAllocator ) noexcept;

void CookKeyDiff_Shutdown( cook_key_diff_t *pDiff ) noexcept;

// Classifies every source of `next` against `previous`. With a policy
// change every common source is `changed`. Each output list is ascending.
CYPHER_NODISCARD geometry_status_t CookKeyDiff_TryCompute(
    const cook_key_set_t *pPrevious,
    const cook_key_set_t *pNext,
    cook_key_diff_t *pDiff ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_KEYS_H
