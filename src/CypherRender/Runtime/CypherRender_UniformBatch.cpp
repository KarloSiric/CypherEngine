//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Packs per-object uniform records into one bounded GPU upload.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_UniformBatch.h"

#include "CypherRender/CypherRender_Public.h"

#include <cstring>
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{

bool R_UniformBatchOwnsStorage( const render_uniform_batch_t &batch ) noexcept
{
    return batch.buffer.value != R_INVALID_BUFFER.value || batch.staging != nullptr;
}

bool R_UniformBatchReady( const render_uniform_batch_t &batch ) noexcept
{
    return batch.buffer.value != R_INVALID_BUFFER.value && batch.staging != nullptr &&
        batch.allocator != nullptr && batch.capacityBytes != 0u &&
        batch.alignment != 0u && batch.generation != 0u &&
        batch.usedBytes <= batch.capacityBytes;
}

} // namespace

render_error_t R_CreateUniformBatch(
    const common::u64 capacityBytes,
    const common::allocator_t *allocator,
    render_uniform_batch_t *batchOut ) noexcept
{
    if ( batchOut == nullptr || R_UniformBatchOwnsStorage( *batchOut ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !R_IsInitialized() ) return render_error_t::ERR_NOT_INITIALIZED;
    const render_info_t *info = R_GetInfo();
    if ( info == nullptr || info->limits.uniformBufferOffsetAlignment == 0u ||
         info->limits.maxUniformBlockBytes == 0u ) {
        return render_error_t::ERR_DEVICE_QUERY_FAILED;
    }
    if ( capacityBytes == 0u ||
         capacityBytes > static_cast<common::u64>( std::numeric_limits<common::isize>::max() ) ||
         capacityBytes > static_cast<common::u64>( std::numeric_limits<common::usize>::max() ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    const common::allocator_t *owner = allocator != nullptr
        ? allocator : common::Allocator_GetSystem();
    if ( !common::Allocator_IsValid( owner ) ) return render_error_t::ERR_INVALID_ARGUMENT;

    auto *staging = static_cast<common::byte *>( common::Allocator_Allocate(
        owner, static_cast<common::usize>( capacityBytes ),
        common::CY_ALLOCATOR_DEFAULT_ALIGNMENT ) );
    if ( staging == nullptr ) return render_error_t::ERR_OUT_OF_MEMORY;

    render_buffer_desc_t description{};
    description.byteSize = capacityBytes;
    description.usage = R_BUFFER_USAGE_UNIFORM;
    description.updatePolicy = render_buffer_update_t::STREAM;
    description.memory = render_buffer_memory_t::UPLOAD;
    description.debugName = "Uniform batch";
    render_buffer_handle_t buffer{};
    const render_error_t error = R_CreateBuffer( description, nullptr, &buffer );
    if ( error != render_error_t::OK ) {
        common::Allocator_Free( owner, staging,
            static_cast<common::usize>( capacityBytes ),
            common::CY_ALLOCATOR_DEFAULT_ALIGNMENT );
        return error;
    }

    *batchOut = {};
    batchOut->buffer = buffer;
    batchOut->staging = staging;
    batchOut->allocator = owner;
    batchOut->capacityBytes = capacityBytes;
    batchOut->alignment = info->limits.uniformBufferOffsetAlignment;
    batchOut->generation = 1u;
    return render_error_t::OK;
}

render_error_t R_AppendUniformRecord(
    render_uniform_batch_t *batch,
    const void *bytes,
    const common::u64 byteSize,
    render_uniform_record_t *recordOut ) noexcept
{
    if ( recordOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *recordOut = {};
    if ( batch == nullptr || bytes == nullptr || byteSize == 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !R_UniformBatchReady( *batch ) || batch->sealed ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    if ( !R_IsInitialized() ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !R_IsBufferValid( batch->buffer ) ) return render_error_t::ERR_STALE_HANDLE;
    const render_info_t *info = R_GetInfo();
    if ( info == nullptr ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( byteSize > info->limits.maxUniformBlockBytes ) {
        return render_error_t::ERR_LIMIT_EXCEEDED;
    }
    // Compute ceil(used/alignment)*alignment with subtraction, not addition,
    // so an untrusted capacity near U64_MAX cannot wrap.
    const common::u64 remainder = batch->usedBytes % batch->alignment;
    const common::u64 padding = remainder == 0u ? 0u : batch->alignment - remainder;
    if ( padding > batch->capacityBytes - batch->usedBytes ) {
        return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
    }
    const common::u64 offset = batch->usedBytes + padding;
    if ( byteSize > batch->capacityBytes - offset ) {
        return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
    }
    // The source may point into staging, including the gap about to be cleared
    // or the destination itself. Move its original bytes before zeroing the gap.
    std::memmove( batch->staging + static_cast<common::usize>( offset ),
        bytes, static_cast<common::usize>( byteSize ) );
    // Commit uploads the entire populated span, including the alignment gap.
    // Initialize that gap rather than copying old allocator contents to the GPU.
    if ( padding != 0u ) {
        std::memset( batch->staging + static_cast<common::usize>( batch->usedBytes ),
            0, static_cast<common::usize>( padding ) );
    }
    batch->usedBytes = offset + byteSize;
    *recordOut = { batch->buffer, offset, byteSize, batch->generation };
    return render_error_t::OK;
}

render_error_t R_CommitUniformBatch( render_uniform_batch_t *batch ) noexcept
{
    if ( batch == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !R_UniformBatchReady( *batch ) ) return render_error_t::ERR_INVALID_STATE;
    if ( !R_IsInitialized() ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !R_IsBufferValid( batch->buffer ) ) return render_error_t::ERR_STALE_HANDLE;
    if ( batch->committed ) return render_error_t::OK;
    if ( batch->usedBytes == 0u ) return render_error_t::ERR_INVALID_STATE;
    batch->sealed = true;
    const render_buffer_data_t source{ batch->staging, batch->usedBytes };
    const render_error_t error = R_UpdateBuffer( batch->buffer, 0u, source );
    if ( error == render_error_t::OK ) batch->committed = true;
    return error;
}

render_error_t R_ApplyUniformRecordToDraw(
    const render_uniform_batch_t &batch,
    const render_uniform_record_t &record,
    const common::u32 slot,
    render_draw_indexed_desc_t *draw ) noexcept
{
    if ( draw == nullptr || slot >= R_MAX_UNIFORM_BLOCK_BINDINGS ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !R_UniformBatchReady( batch ) || !batch.committed ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    if ( record.buffer.value != batch.buffer.value ||
         record.generation != batch.generation || record.byteSize == 0u ||
         record.offset % batch.alignment != 0u ||
         record.offset > batch.usedBytes ||
         record.byteSize > batch.usedBytes - record.offset ||
         !R_IsBufferValid( batch.buffer ) ) {
        return render_error_t::ERR_STALE_HANDLE;
    }
    draw->uniformBuffers[slot] = batch.buffer;
    draw->uniformRanges[slot] = { record.offset, record.byteSize };
    return render_error_t::OK;
}

render_error_t R_ResetUniformBatch( render_uniform_batch_t *batch ) noexcept
{
    if ( batch == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !R_UniformBatchReady( *batch ) ) return render_error_t::ERR_INVALID_STATE;
    if ( !R_IsInitialized() ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !R_IsBufferValid( batch->buffer ) ) return render_error_t::ERR_STALE_HANDLE;
    if ( R_IsFrameActive() ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    if ( batch->generation == std::numeric_limits<common::u64>::max() ) {
        return render_error_t::ERR_LIMIT_EXCEEDED;
    }
    ++batch->generation;
    batch->usedBytes = 0u;
    batch->sealed = false;
    batch->committed = false;
    return render_error_t::OK;
}

render_error_t R_DestroyUniformBatch( render_uniform_batch_t *batch ) noexcept
{
    if ( batch == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    render_error_t error = render_error_t::OK;
    if ( batch->buffer.value != R_INVALID_BUFFER.value ) {
        error = R_DestroyBuffer( batch->buffer );
        if ( error != render_error_t::OK && R_IsBufferValid( batch->buffer ) ) {
            return error;
        }
        batch->buffer = {};
    }
    if ( batch->staging != nullptr ) {
        if ( batch->allocator == nullptr || batch->capacityBytes == 0u ||
             batch->capacityBytes > static_cast<common::u64>(
                 std::numeric_limits<common::usize>::max() ) ) {
            return render_error_t::ERR_INTERNAL_ERROR;
        }
        common::Allocator_Free( batch->allocator, batch->staging,
            static_cast<common::usize>( batch->capacityBytes ),
            common::CY_ALLOCATOR_DEFAULT_ALIGNMENT );
    }
    *batch = {};
    return error;
}

} // namespace cypher::engine::render
