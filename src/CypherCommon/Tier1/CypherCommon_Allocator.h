//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Allocator.h
//  Purpose: Declares the Tier1 allocator interface used by owning utilities.
//  Details: The interface keeps allocation ownership explicit without making
//           CypherCommon depend on the higher-level CypherMemory subsystem.
//
//  History:
//  - Created by Karlo Siric on 2026-08-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_ALLOCATOR_H
#define CYPHER_COMMON_TIER1_ALLOCATOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Allocator

Borrowed allocation interface shared by Tier1 owning containers and buffers.

Rules:
- The allocator object and pUserData must outlive every allocation owner using it.
- Allocations are released through the same allocator that created them.
- cbSize == 0 produces no allocation; freeing nullptr is a no-op.
- alignment must be a non-zero power of two.
- pfnReallocate is optional; callers must support allocate-copy-free fallback.
- The interface does not imply thread safety.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

// Natural alignment used when a caller does not require an over-aligned block.
inline constexpr usize CY_ALLOCATOR_DEFAULT_ALIGNMENT = alignof( std::max_align_t );

// Allocates one raw byte block. Returning nullptr reports ordinary allocation failure.
using allocator_allocate_fn_t = void *( * )(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept;

// Replaces one allocation while preserving min(cbOldSize, cbNewSize) bytes. A
// nullptr result leaves pMemory valid and unchanged. A non-null result consumes
// pMemory and must satisfy nAlignment; violating either rule is a fatal backend bug.
using allocator_reallocate_fn_t = void *( * )(
    void *pUserData,
    void *pMemory,
    usize cbOldSize,
    usize cbNewSize,
    usize nAlignment ) noexcept;

// Releases one block using the original size and alignment metadata.
using allocator_free_fn_t = void ( * )(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept;

struct allocator_t {
    allocator_allocate_fn_t pfnAllocate{ nullptr };
    allocator_reallocate_fn_t pfnReallocate{ nullptr };
    allocator_free_fn_t pfnFree{ nullptr };
    void *pUserData{ nullptr };
};

// Move-only transfer record for raw memory whose deallocation responsibility
// changes owner. Destruction does not free the block; call Allocator_FreeOwned.
struct owned_allocation_t {
    void *pData{ nullptr };
    usize cbSize{ 0u };
    usize nAlignment{ 0u };
    const allocator_t *pAllocator{ nullptr };

    owned_allocation_t() noexcept = default;
    CYPHER_NO_COPY( owned_allocation_t );

    owned_allocation_t( owned_allocation_t &&source ) noexcept
        : pData( source.pData ),
          cbSize( source.cbSize ),
          nAlignment( source.nAlignment ),
          pAllocator( source.pAllocator )
    {
        source.pData = nullptr;
        source.cbSize = 0u;
        source.nAlignment = 0u;
        source.pAllocator = nullptr;
    }

    owned_allocation_t &operator=( owned_allocation_t &&source ) noexcept
    {
        if ( this == &source ) {
            return *this;
        }

        const bool_t bDestinationEmpty =
            pData == nullptr &&
            cbSize == 0u &&
            nAlignment == 0u &&
            pAllocator == nullptr;
        CY_ASSERT_MSG(
            bDestinationEmpty,
            "Move-assignment requires an empty owned allocation destination." );
        if ( !bDestinationEmpty ) {
            return *this;
        }

        pData = source.pData;
        cbSize = source.cbSize;
        nAlignment = source.nAlignment;
        pAllocator = source.pAllocator;

        source.pData = nullptr;
        source.cbSize = 0u;
        source.nAlignment = 0u;
        source.pAllocator = nullptr;
        return *this;
    }
};

// Returns the process-lifetime, thread-safe allocator backed by aligned operator new.
CYPHER_NODISCARD CYPHER_COMMON_API
const allocator_t *Allocator_GetSystem() noexcept;

// Returns true when the required allocation and release callbacks are present.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Allocator_IsValid( const allocator_t *pAllocator ) noexcept;

// Allocates cbSize bytes with the requested alignment.
CYPHER_NODISCARD CYPHER_COMMON_API
void *Allocator_Allocate(
    const allocator_t *pAllocator,
    usize cbSize,
    usize nAlignment = CY_ALLOCATOR_DEFAULT_ALIGNMENT ) noexcept;

// Allocates a block and clears every byte before returning it.
CYPHER_NODISCARD CYPHER_COMMON_API
void *Allocator_AllocateZeroed(
    const allocator_t *pAllocator,
    usize cbSize,
    usize nAlignment = CY_ALLOCATOR_DEFAULT_ALIGNMENT ) noexcept;

// Resizes an allocation through the native callback or allocate-copy-free fallback.
// On failure, pMemory remains owned by the caller and is still valid.
CYPHER_NODISCARD CYPHER_COMMON_API
void *Allocator_Reallocate(
    const allocator_t *pAllocator,
    void *pMemory,
    usize cbOldSize,
    usize cbNewSize,
    usize nAlignment = CY_ALLOCATOR_DEFAULT_ALIGNMENT ) noexcept;

// Releases memory through its originating allocator.
CYPHER_COMMON_API void Allocator_Free(
    const allocator_t *pAllocator,
    void *pMemory,
    usize cbSize,
    usize nAlignment = CY_ALLOCATOR_DEFAULT_ALIGNMENT ) noexcept;

// Allocates uninitialized storage for nCount objects after checking byte-count
// overflow. Object construction remains the caller's responsibility.
template <typename type_t>
CYPHER_NODISCARD type_t *Allocator_AllocateArrayStorage(
    const allocator_t *pAllocator,
    usize nCount,
    usize nAlignment = alignof( type_t ) ) noexcept
{
    usize cbSize = 0u;
    const bool_t bValidByteCount = Cy_TryArrayByteCount<type_t>( nCount, cbSize );
    CY_ASSERT_MSG( bValidByteCount, "Allocator array byte count overflowed." );
    if ( !bValidByteCount ) {
        return nullptr;
    }

    return static_cast<type_t *>(
        Allocator_Allocate( pAllocator, cbSize, nAlignment ) );
}

// Resizes raw array storage after checking both byte counts. Byte relocation is
// only valid for types that explicitly satisfy Cypher's relocation contract.
template <typename type_t>
CYPHER_NODISCARD type_t *Allocator_ReallocateArrayStorage(
    const allocator_t *pAllocator,
    type_t *pMemory,
    usize nOldCount,
    usize nNewCount,
    usize nAlignment = alignof( type_t ) ) noexcept
{
    static_assert(
        is_trivially_relocatable_v<type_t>,
        "Allocator_ReallocateArrayStorage requires a trivially relocatable type." );

    usize cbOldSize = 0u;
    usize cbNewSize = 0u;
    const bool_t bValidOldByteCount = Cy_TryArrayByteCount<type_t>( nOldCount, cbOldSize );
    const bool_t bValidNewByteCount = Cy_TryArrayByteCount<type_t>( nNewCount, cbNewSize );
    CY_ASSERT_MSG(
        bValidOldByteCount && bValidNewByteCount,
        "Allocator array reallocation byte count overflowed." );
    if ( !bValidOldByteCount || !bValidNewByteCount ) {
        return nullptr;
    }

    return static_cast<type_t *>(
        Allocator_Reallocate(
            pAllocator,
            pMemory,
            cbOldSize,
            cbNewSize,
            nAlignment ) );
}

// Releases raw array storage after checking the original byte count.
template <typename type_t>
void Allocator_FreeArrayStorage(
    const allocator_t *pAllocator,
    type_t *pMemory,
    usize nCount,
    usize nAlignment = alignof( type_t ) ) noexcept
{
    usize cbSize = 0u;
    const bool_t bValidByteCount = Cy_TryArrayByteCount<type_t>( nCount, cbSize );
    CY_ASSERT_MSG( bValidByteCount, "Allocator array byte count overflowed during release." );
    if ( !bValidByteCount ) {
        return;
    }

    Allocator_Free( pAllocator, pMemory, cbSize, nAlignment );
}

// Returns true for either a canonical empty record or a complete live owner.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Allocator_OwnedIsValid(
    const owned_allocation_t *pAllocation ) noexcept;

// Takes responsibility for an existing raw allocation. The destination must be
// empty and the supplied metadata must match the allocation's original contract.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Allocator_AdoptOwned(
    owned_allocation_t *pAllocation,
    const allocator_t *pAllocator,
    void *pMemory,
    usize cbSize,
    usize nAlignment = CY_ALLOCATOR_DEFAULT_ALIGNMENT ) noexcept;

// Allocates a new block directly into an empty ownership record.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Allocator_AllocateOwned(
    owned_allocation_t *pAllocation,
    const allocator_t *pAllocator,
    usize cbSize,
    usize nAlignment = CY_ALLOCATOR_DEFAULT_ALIGNMENT ) noexcept;

// Transfers ownership between records without allocation. The destination must
// be canonical empty; the source becomes canonical empty after a successful move.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Allocator_MoveOwned(
    owned_allocation_t *pDestination,
    owned_allocation_t *pSource ) noexcept;

// Frees a transferred allocation and resets the record to its empty state.
CYPHER_COMMON_API void Allocator_FreeOwned(
    owned_allocation_t *pAllocation ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_ALLOCATOR_H
