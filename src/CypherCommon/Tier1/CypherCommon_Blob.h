//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Blob.h
//  Purpose: Declares allocator-backed owning byte storage.
//  Details: Blob is the owning counterpart to binary_block_t. Growth and allocation
//           failure are explicit, and stored data remains contiguous.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_BLOB_H
#define CYPHER_COMMON_TIER1_BLOB_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

struct blob_t {
    blob_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( blob_t );
    ~blob_t() noexcept;

    byte *pData{ nullptr };
    usize cbSize{ 0u };
    usize cbCapacity{ 0u };
    const allocator_t *pAllocator{ nullptr };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Blob_Init(
    blob_t *pBlob,
    const allocator_t *pAllocator,
    usize cbInitialCapacity = 0u ) noexcept;

CYPHER_COMMON_API void Blob_Shutdown( blob_t *pBlob ) noexcept;
CYPHER_COMMON_API void Blob_Clear( blob_t *pBlob ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Blob_IsValid( const blob_t *pBlob ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Blob_IsEmpty( const blob_t *pBlob ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte *Blob_Data( blob_t *pBlob ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const byte *Blob_Data( const blob_t *pBlob ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Blob_Size( const blob_t *pBlob ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Blob_Capacity( const blob_t *pBlob ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Blob_Reserve( blob_t *pBlob, usize cbCapacity ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Blob_Resize(
    blob_t *pBlob,
    usize cbSize,
    byte fill = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Blob_Assign( blob_t *pBlob, binary_block_t source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Blob_Append( blob_t *pBlob, binary_block_t source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t Blob_Block( const blob_t *pBlob ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t Blob_WritableSpan( blob_t *pBlob ) noexcept;

CYPHER_COMMON_API void Blob_Move( blob_t *pDest, blob_t *pSource ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
owned_allocation_t Blob_Release(
    blob_t *pBlob,
    usize *pcbLogicalSizeOut = nullptr ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BLOB_H
