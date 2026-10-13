//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Declares bounded, ordered CPU storage for indexed draw commands.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_DRAW_QUEUE_H
#define CYPHER_ENGINE_RENDER_DRAW_QUEUE_H
#pragma once

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherRender/CypherRender_Draw.h"

namespace cypher::engine::render
{

// Calls are single-threaded on the renderer thread, or externally serialized.
// Concurrent append, flush, reset, or shutdown of one queue is unsupported.
struct render_draw_queue_t; // Opaque, non-copyable owner of copied draw records.

// Allocates all CPU storage up front; no GPU resources are created. The
// allocator is borrowed and must outlive the queue. Capacity is 1..65536.
// The output must be null. Ordinary failure leaves it null and leaks nothing.
CYPHER_NODISCARD render_error_t R_InitDrawQueue(
    render_draw_queue_t **ppQueue,
    const ::cypher::common::allocator_t *pAllocator,
    ::cypher::common::u32 capacity ) noexcept;

// Frees only the CPU records. Draw descriptors contain renderer handles by
// value; the queue never owns or keeps their GPU resources alive. Stop using
// this pointer before shutdown; calling through an old pointer is invalid.
void R_ShutdownDrawQueue( render_draw_queue_t **ppQueue ) noexcept;

// Copies one descriptor immediately, in order. No draw validation or GPU work
// happens here. Capacity failure leaves existing records unchanged.
// Appending is sealed by the first in-frame flush attempt.
CYPHER_NODISCARD render_error_t R_AppendDrawQueue(
    render_draw_queue_t *pQueue,
    const render_draw_indexed_desc_t &draw ) noexcept;

// Requires an initialized renderer and an active frame. Permits ONE attempt
// per reset, even when the queue is empty or a draw fails. Executes in order
// with R_DrawIndexed and stops at the first error. The completed count reports
// draws whose calls returned OK; the failing draw may already have reached
// the GPU. Repeated flushes are rejected to avoid duplicate work, including
// in a later frame.
// Flush is not atomic. Handles may become stale between append and flush.
CYPHER_NODISCARD render_error_t R_FlushDrawQueue(
    render_draw_queue_t *pQueue,
    ::cypher::common::u32 *pOutCompleted ) noexcept;

// Discards recorded work and unlocks append. Only legal outside a frame;
// callers must decide how to handle a partially submitted frame before reset.
// CPU storage remains allocated for reuse. No GPU synchronization is implied.
CYPHER_NODISCARD render_error_t R_ResetDrawQueue(
    render_draw_queue_t *pQueue ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_DRAW_QUEUE_H
