//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_OperationContext.h
//  Purpose: Declares borrowed services shared by one geometry operation.
//  Details: The context binds validated policy, bounded scratch and diagnostics,
//           and host-neutral cooperative cancellation without owning storage.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_OPERATION_CONTEXT_H
#define CYPHER_EDITOR_GEOMETRY_OPERATION_CONTEXT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Diagnostics.h"
#include "CypherGeometry_Policy.h"
#include "CypherGeometry_Scratch.h"

#include "CypherCommon_Atomic.h"

#include <type_traits>

namespace cypher::editor::geometry
{

using common::atomic_bool_t;
using common::bool_t;

// The callback adapts host-owned cancellation sources without making Geometry
// depend on ToolFramework, Qt, a job system, or a process-global flag. It may
// receive nullptr user data and must be safe to call on the operation thread.
using geometry_cancel_query_t = bool_t ( * )( void *pUserData ) noexcept;

// Both sources are optional. The atomic flag is checked first with acquire
// ordering; an observed request skips the callback. The binding itself is
// immutable while it is borrowed, while the atomic and callback-owned state may
// change concurrently according to their own synchronization contracts.
struct geometry_cancellation_t {
    const atomic_bool_t *pRequested{ nullptr };
    geometry_cancel_query_t pfnQuery{ nullptr };
    void *pUserData{ nullptr };
};

// Policy is required. Scratch, diagnostics, and cancellation are deliberately
// optional because some read-only operations need none of them. Absence never
// selects an implicit allocator, diagnostic sink, cancellation source, or global
// singleton; an operation that requires an optional service must reject its
// absence in that operation's own preflight.
struct geometry_operation_context_desc_t {
    const geometry_policy_t *pPolicy{ nullptr };
    geometry_scratch_t *pScratch{ nullptr };
    geometry_diagnostic_buffer_t *pDiagnostics{ nullptr };
    const geometry_cancellation_t *pCancellation{ nullptr };
};

// Every pointer is borrowed for the full operation. Policy and cancellation
// bindings must not be mutated; scratch and diagnostics stay single-writer on
// the operation thread. Binding and checkpoints allocate no memory.
struct geometry_operation_context_t {
    const geometry_policy_t *pPolicy{ nullptr };
    geometry_scratch_t *pScratch{ nullptr };
    geometry_diagnostic_buffer_t *pDiagnostics{ nullptr };
    const geometry_cancellation_t *pCancellation{ nullptr };
};

// A nullptr cancellation pointer is not itself a token and is therefore not
// valid here; it is nevertheless the documented neutral optional value for
// IsRequested and for a context binding. An empty token is valid and neutral.
// User data without a callback is rejected so stale opaque state is not silently
// accepted as an active cancellation source.
CYPHER_NODISCARD bool GeometryCancellation_IsValid(
    const geometry_cancellation_t *pCancellation ) noexcept;

// Samples one checkpoint. nullptr and an empty token are neutral. When the
// atomic flag is false, the callback is invoked exactly once. Results are not
// cached or latched: hosts should expose a monotonic request that remains true
// after cancellation. This keeps polling allocation-free and matches the Common
// cooperative-cancellation contract without introducing shared mutable state.
CYPHER_NODISCARD bool_t GeometryCancellation_IsRequested(
    const geometry_cancellation_t *pCancellation ) noexcept;

// Validates every binding before publishing it. Provided scratch must be an
// initialized valid acquisition whose declared budget fits policy.cbScratchMax.
// Provided diagnostics must be an initialized structurally valid borrowed sink
// whose capacity fits policy.cDiagnosticsMax. Record-by-record diagnostic audit
// remains an explicit GeometryDiagnosticBuffer_ValidateDeep boundary check so
// frequent cancellation checkpoints do not become O(number of diagnostics).
// Failure leaves *pContext unchanged.
CYPHER_NODISCARD geometry_status_t GeometryOperationContext_Bind(
    geometry_operation_context_t *pContext,
    const geometry_operation_context_desc_t &desc ) noexcept;

// Revalidates borrowed structure and policy bounds. This detects a policy
// mutation, scratch release/corruption, diagnostic shutdown/corruption, or a
// malformed cancellation binding during the borrowed lifetime.
CYPHER_NODISCARD bool GeometryOperationContext_IsValid(
    const geometry_operation_context_t *pContext ) noexcept;

// Validation always precedes cancellation sampling, so an invalid borrowed
// context deterministically returns CORRUPT_STATE without invoking a callback.
// A default/unbound context returns NOT_INITIALIZED. A valid checkpoint returns
// exactly OK or CANCELLED and never moves scratch cursors or diagnostic counts.
CYPHER_NODISCARD geometry_status_t GeometryOperationContext_Checkpoint(
    const geometry_operation_context_t *pContext ) noexcept;

static_assert( std::is_standard_layout_v<geometry_cancellation_t> );
static_assert( std::is_trivially_copyable_v<geometry_cancellation_t> );
static_assert( std::is_standard_layout_v<geometry_operation_context_t> );
static_assert( std::is_trivially_copyable_v<geometry_operation_context_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_OPERATION_CONTEXT_H
