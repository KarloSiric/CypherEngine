//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Retains an ordered, bounded CPU copy of indexed draw descriptors.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_DrawQueue.h"

#include "CypherRender/CypherRender_Public.h"

#include <new>

namespace cypher::engine::render
{
namespace common = ::cypher::common;

struct render_draw_queue_t {
    const common::allocator_t *pAllocator{ nullptr };
    render_draw_indexed_desc_t *pDraws{ nullptr };
    common::u32 capacity{ 0u };
    common::u32 count{ 0u };
    bool sealed{ false };
};

render_error_t R_InitDrawQueue(
    render_draw_queue_t **ppQueue,
    const common::allocator_t *pAllocator,
    const common::u32 capacity ) noexcept
{
    if ( ppQueue == nullptr || *ppQueue != nullptr ||
         !common::Allocator_IsValid( pAllocator ) ||
         capacity == 0u || capacity > 65536u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    void *pOwnerMemory = common::Allocator_Allocate(
        pAllocator, sizeof( render_draw_queue_t ), alignof( render_draw_queue_t ) );
    if ( pOwnerMemory == nullptr ) return render_error_t::ERR_OUT_OF_MEMORY;

    auto *pQueue = ::new ( pOwnerMemory ) render_draw_queue_t;
    pQueue->pAllocator = pAllocator;
    pQueue->capacity = capacity;
    pQueue->pDraws = common::Allocator_AllocateArrayStorage<render_draw_indexed_desc_t>(
        pAllocator, capacity );
    if ( pQueue->pDraws == nullptr ) {
        pQueue->~render_draw_queue_t();
        common::Allocator_Free( pAllocator, pOwnerMemory,
            sizeof( render_draw_queue_t ), alignof( render_draw_queue_t ) );
        return render_error_t::ERR_OUT_OF_MEMORY;
    }
    for ( common::u32 i = 0u; i < capacity; ++i ) {
        ::new ( pQueue->pDraws + i ) render_draw_indexed_desc_t;
    }
    *ppQueue = pQueue;
    return render_error_t::OK;
}

void R_ShutdownDrawQueue( render_draw_queue_t **ppQueue ) noexcept
{
    if ( ppQueue == nullptr || *ppQueue == nullptr ) return;
    render_draw_queue_t *pQueue = *ppQueue;
    const common::allocator_t *pAllocator = pQueue->pAllocator;
    for ( common::u32 i = 0u; i < pQueue->capacity; ++i ) {
        pQueue->pDraws[i].~render_draw_indexed_desc_t();
    }
    common::Allocator_FreeArrayStorage( pAllocator, pQueue->pDraws, pQueue->capacity );
    pQueue->~render_draw_queue_t();
    common::Allocator_Free( pAllocator, pQueue,
        sizeof( render_draw_queue_t ), alignof( render_draw_queue_t ) );
    *ppQueue = nullptr;
}

render_error_t R_AppendDrawQueue(
    render_draw_queue_t *pQueue,
    const render_draw_indexed_desc_t &draw ) noexcept
{
    if ( pQueue == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( pQueue->sealed ) return render_error_t::ERR_COMMAND_STATE_INVALID;
    if ( pQueue->count == pQueue->capacity ) {
        return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
    }
    pQueue->pDraws[pQueue->count] = draw;
    ++pQueue->count;
    return render_error_t::OK;
}

render_error_t R_FlushDrawQueue(
    render_draw_queue_t *pQueue,
    common::u32 *pOutCompleted ) noexcept
{
    if ( pOutCompleted == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *pOutCompleted = 0u;
    if ( pQueue == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !R_IsInitialized() ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( pQueue->sealed ) return render_error_t::ERR_COMMAND_STATE_INVALID;
    if ( !R_IsFrameActive() ) return render_error_t::ERR_FRAME_NOT_ACTIVE;

    pQueue->sealed = true;
    for ( common::u32 i = 0u; i < pQueue->count; ++i ) {
        const render_error_t result = R_DrawIndexed( pQueue->pDraws[i] );
        if ( result != render_error_t::OK ) return result;
        ++*pOutCompleted;
    }
    return render_error_t::OK;
}

render_error_t R_ResetDrawQueue( render_draw_queue_t *pQueue ) noexcept
{
    if ( pQueue == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( R_IsFrameActive() ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    pQueue->count = 0u;
    pQueue->sealed = false;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
