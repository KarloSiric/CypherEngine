//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Stages aligned per-object uniform records for one GPU upload.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_UNIFORM_BATCH_H
#define CYPHER_ENGINE_RENDER_UNIFORM_BATCH_H
#pragma once

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherRender/CypherRender_Draw.h"

namespace cypher::engine::render
{

// Caller-owned allocation and renderer buffer. Do not copy this owner. The
// allocator is borrowed and must outlive the batch; nullptr at create selects
// the process-lifetime system allocator. Destroy before renderer shutdown.
struct render_uniform_batch_t {
    render_buffer_handle_t buffer{};
    ::cypher::common::byte *staging{ nullptr };
    const ::cypher::common::allocator_t *allocator{ nullptr };
    ::cypher::common::u64 capacityBytes{ 0u };
    ::cypher::common::u64 usedBytes{ 0u };
    ::cypher::common::u64 alignment{ 0u };
    ::cypher::common::u64 generation{ 0u };
    bool sealed{ false };    // First commit attempt closes the append phase.
    bool committed{ false }; // Upload succeeded; records can be applied to draws.
};

// An offset and span in one committed batch generation. This is not a GPU
// object and does not retain the batch's buffer after reset or destruction.
struct render_uniform_record_t {
    render_buffer_handle_t buffer{};
    ::cypher::common::u64 offset{ 0u };
    ::cypher::common::u64 byteSize{ 0u };
    ::cypher::common::u64 generation{ 0u };
};

// Creates one fixed-size CPU staging allocation and one STREAM/UPLOAD uniform
// buffer. batchOut must be empty. No allocation or GPU handle is retained on
// ordinary failure. Capacity must fit both host and backend address spaces.
CYPHER_NODISCARD render_error_t R_CreateUniformBatch(
    ::cypher::common::u64 capacityBytes,
    const ::cypher::common::allocator_t *allocator,
    CY_OUT render_uniform_batch_t *batchOut ) noexcept;

// Copies borrowed bytes immediately, including when the source overlaps this
// batch's staging buffer. Each record begins at the device's uniform-buffer
// offset alignment, and must fit one uniform block. Failure leaves the batch
// cursor unchanged and clears recordOut.
CYPHER_NODISCARD render_error_t R_AppendUniformRecord(
    render_uniform_batch_t *batch,
    const void *bytes,
    ::cypher::common::u64 byteSize,
    CY_OUT render_uniform_record_t *recordOut ) noexcept;

// Uploads the populated span with one R_UpdateBuffer call. A failed upload
// leaves the staging bytes and tokens intact for retry; append is sealed after
// the first attempt. Repeating a successful commit is a no-op.
CYPHER_NODISCARD render_error_t R_CommitUniformBatch(
    render_uniform_batch_t *batch ) noexcept;

// Copies this record's buffer/range into one draw uniform slot only. The batch
// must be committed and the token must still name its current generation.
// Other draw fields stay unchanged, including other uniform slots. Final
// pipeline/block compatibility is checked by R_DrawIndexed.
CYPHER_NODISCARD render_error_t R_ApplyUniformRecordToDraw(
    const render_uniform_batch_t &batch,
    const render_uniform_record_t &record,
    ::cypher::common::u32 slot,
    render_draw_indexed_desc_t *draw ) noexcept;

// Discards staged records and invalidates old tokens. Call between frames,
// after draws using the previous records have been submitted. Updating a buffer
// still in use by the GPU may make OpenGL wait; this helper does not promise
// asynchronous streaming or manage a ring of in-flight buffers.
CYPHER_NODISCARD render_error_t R_ResetUniformBatch(
    render_uniform_batch_t *batch ) noexcept;

// Releases the GPU buffer before staging storage. If GPU destruction fails and
// the handle remains live, ownership stays in batch for a later retry.
CYPHER_NODISCARD render_error_t R_DestroyUniformBatch(
    render_uniform_batch_t *batch ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_UNIFORM_BATCH_H
