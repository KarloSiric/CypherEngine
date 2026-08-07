//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ScratchBuffer.h
//  Purpose: Declares scoped temporary byte storage with local-first allocation.
//  Details: ScratchBuffer uses caller-provided local bytes when they fit and falls
//           back to an explicit allocator otherwise. It owns at most one allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SCRATCHBUFFER_H
#define CYPHER_COMMON_TIER1_SCRATCHBUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

struct scratch_buffer_t {
    scratch_buffer_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( scratch_buffer_t );

    byte *pData{ nullptr };
    usize cbSize{ 0u };
    usize alignment{ 0u };
    byte_span_t localStorage{};
    const allocator_t *pAllocator{ nullptr };
    bool_t bOwnsAllocation{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ScratchBuffer_Acquire(
    scratch_buffer_t *pBuffer,
    byte_span_t localStorage,
    const allocator_t *pFallbackAllocator,
    usize cbSize,
    usize alignment ) noexcept;

CYPHER_COMMON_API void ScratchBuffer_Release(
    scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t ScratchBuffer_Span( scratch_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ScratchBuffer_UsesLocalStorage(
    const scratch_buffer_t *pBuffer ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SCRATCHBUFFER_H
