//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Scratch.h
//  Purpose: Declares bounded temporary storage for editor-geometry operations.
//  Details: Geometry scratch borrows local bytes first, falls back through an
//           explicit allocator, and exposes one marker-based linear lifetime.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SCRATCH_H
#define CYPHER_EDITOR_GEOMETRY_SCRATCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"

#include "CypherCommon_Align.h"
#include "CypherCommon_MemoryStack.h"
#include "CypherCommon_ScratchBuffer.h"

#include <cstddef>
#include <type_traits>

namespace cypher::editor::geometry
{

using common::allocator_t;
using common::bool_t;
using common::byte_span_t;
using common::memory_stack_t;
using common::scratch_buffer_t;
using common::u64;
using common::usize;

// The requested capacity is the actual addressable arena size for one operation.
// The budget is the host-approved upper bound. Acquisition fails before touching
// either local or fallback storage when capacity exceeds that budget.
struct geometry_scratch_desc_t {
    byte_span_t localStorage{};
    const allocator_t *pFallbackAllocator{ nullptr };
    usize cbCapacity{ 0u };
    usize cbBudget{ 0u };
    usize nBaseAlignment{ alignof( std::max_align_t ) };
};

struct geometry_scratch_t;

// A mark is valid only for the current acquisition of the scratch object that
// produced it. Owner and acquisition identity are checked at rewind so a mark
// from another arena or an earlier acquisition cannot move the live cursor.
struct geometry_scratch_mark_t {
    const geometry_scratch_t *pOwner{ nullptr };
    u64 nAcquisitionSerial{ 0u };
    u64 nMarkerSerial{ 0u };
    u64 nParentMarkerSerial{ 0u };
    usize iOffset{ 0u };
};

struct geometry_scratch_stats_t {
    usize cbBudget{ 0u };
    usize cbCapacity{ 0u };
    usize cbUsed{ 0u };
    usize cbRemaining{ 0u };
    usize cbHighWater{ 0u };
    bool_t bUsesLocalStorage{ common::CY_FALSE };
    bool_t bUsesFallbackAllocation{ common::CY_FALSE };
};

// Geometry scratch is deliberately single-threaded. Acquire, allocate, mark,
// rewind, query, and release one instance from one owner thread. Publish only
// completed immutable results across threads; never publish scratch pointers.
struct geometry_scratch_t {
    scratch_buffer_t buffer{};
    memory_stack_t stack{};
    usize cbBudget{ 0u };
    usize nBaseAlignment{ 0u };
    // Retained across release so old marks cannot become valid after reacquire.
    // Reaching the terminal value permanently retires this scratch object.
    u64 nAcquisitionSerial{ 0u };
    // Markers form a checked LIFO chain inside one acquisition. A rewind pops
    // exactly its current mark, preventing stale offsets from cutting through a
    // later allocation after the arena has been rewound and reused.
    u64 nMarkerSerialCounter{ 0u };
    u64 nCurrentMarkerSerial{ 0u };
    bool_t bInitialized{ common::CY_FALSE };
};

static_assert( !std::is_copy_constructible_v<geometry_scratch_t> );
static_assert( !std::is_copy_assignable_v<geometry_scratch_t> );
static_assert( !std::is_move_constructible_v<geometry_scratch_t> );
static_assert( !std::is_move_assignable_v<geometry_scratch_t> );

// Acquires exactly desc.cbCapacity bytes. Local storage is preferred when it can
// satisfy both size and base alignment; otherwise the explicit fallback allocator
// supplies one owned block. Local bytes must remain alive and unmodified, and a
// fallback allocator descriptor plus backend state must remain alive, for the
// acquisition. Marks cannot outlive the scratch object. Callers must destroy
// explicitly constructed non-trivial objects before rewind or release. Ordinary
// allocation failure leaves pScratch empty.
CYPHER_NODISCARD geometry_status_t GeometryScratch_Acquire(
    geometry_scratch_t *pScratch,
    const geometry_scratch_desc_t &desc ) noexcept;

// Releases fallback ownership, invalidates all allocations and marks, and
// restores its storage to the canonical released state while retaining the
// acquisition serial. It is safe to release an already-empty valid object.
CYPHER_NODISCARD geometry_status_t GeometryScratch_Release(
    geometry_scratch_t *pScratch ) noexcept;

CYPHER_NODISCARD bool GeometryScratch_IsValid(
    const geometry_scratch_t *pScratch ) noexcept;

CYPHER_NODISCARD bool GeometryScratch_IsInitialized(
    const geometry_scratch_t *pScratch ) noexcept;

// Allocates raw uninitialized bytes. A failed call writes nullptr to ppStorage
// and leaves both the live cursor and high-water mark unchanged.
CYPHER_NODISCARD geometry_status_t GeometryScratch_Allocate(
    geometry_scratch_t *pScratch,
    usize cbSize,
    usize nAlignment,
    void **ppStorage ) noexcept;

// Typed storage does not construct objects or run destructors. Callers may use
// it directly for implicit-lifetime geometry records or construct and destroy
// non-trivial objects explicitly before rewinding their storage.
template <typename type_t>
CYPHER_NODISCARD geometry_status_t GeometryScratch_AllocateArrayStorage(
    geometry_scratch_t *pScratch,
    usize nCount,
    type_t **ppStorage,
    usize nAlignment = alignof( type_t ) ) noexcept;

CYPHER_NODISCARD geometry_status_t GeometryScratch_Mark(
    geometry_scratch_t *pScratch,
    geometry_scratch_mark_t *pMark ) noexcept;

// Rewind invalidates every pointer returned after mark. Marks are one-shot and
// strictly LIFO: nested marks must be rewound from newest to oldest. Foreign,
// stale, reused, and future marks are rejected without moving the cursor.
CYPHER_NODISCARD geometry_status_t GeometryScratch_Rewind(
    geometry_scratch_t *pScratch,
    geometry_scratch_mark_t mark ) noexcept;

CYPHER_NODISCARD geometry_status_t GeometryScratch_QueryStats(
    const geometry_scratch_t *pScratch,
    geometry_scratch_stats_t *pStats ) noexcept;

} // namespace cypher::editor::geometry

#include "CypherGeometry_Scratch.inl"

#endif // CYPHER_EDITOR_GEOMETRY_SCRATCH_H
