//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_IdAllocator.h
//  Purpose: Declares monotonic persistent identity allocation for geometry.
//  Details: Source IDs are document-local and never recycled. Live storage
//           handles remain a separate generation-checked addressing mechanism.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_IDALLOCATOR_H
#define CYPHER_EDITOR_GEOMETRY_IDALLOCATOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"

namespace cypher::editor::geometry
{

// A zero next ID is the canonical exhausted state. The default state begins at
// one because zero is permanently reserved as the invalid source identity.
struct geometry_source_id_allocator_t {
    geometry_source_id_t next{ 1u };
};

struct geometry_source_id_result_t {
    geometry_source_id_t id{};
    geometry_status_t status{ geometry_status_t::OK };
};

// Resets an allocator for a new, empty identity domain. Calling this while any
// IDs from the old domain remain live would violate persistent uniqueness.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdAllocator_Reset(
    geometry_source_id_allocator_t *pAllocator,
    geometry_source_id_t first = geometry_source_id_t{ 1u } ) noexcept;

// Allocates one identity and advances monotonically. The largest u64 identity
// is valid; allocating it transitions the allocator to the exhausted state.
CYPHER_NODISCARD geometry_source_id_result_t
GeometrySourceIdAllocator_Allocate(
    geometry_source_id_allocator_t *pAllocator ) noexcept;

// Advances the allocator beyond an identity observed while loading or merging
// authored data. Observing an older ID never moves the sequence backward.
CYPHER_NODISCARD geometry_status_t GeometrySourceIdAllocator_AdvancePast(
    geometry_source_id_allocator_t *pAllocator,
    geometry_source_id_t observed ) noexcept;

CYPHER_NODISCARD bool GeometrySourceIdAllocator_IsExhausted(
    const geometry_source_id_allocator_t *pAllocator ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_IDALLOCATOR_H
