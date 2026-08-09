//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Allocator.cpp
//  Purpose: Implements the Tier1 allocator interface used by owning utilities.
//  Details: Validates shared allocation contracts, supplies portable system-backed
//           allocation, and provides reallocation fallback without coupling Tier1
//           containers to the higher-level CypherMemory subsystem.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Allocator.h"

#include <new>

namespace cypher::common
{

namespace
{

// Aligned operator new is specified for extended alignments. Normalizing smaller
// requests also gives operator delete the exact alignment used by allocation.
usize SystemEffectiveAlignment( usize nAlignment ) noexcept
{
    return nAlignment < CY_ALLOCATOR_DEFAULT_ALIGNMENT
        ? CY_ALLOCATOR_DEFAULT_ALIGNMENT
        : nAlignment;
}

void *SystemAllocate(
    void *,
    usize cbSize,
    usize nAlignment ) noexcept
{
    const usize nEffectiveAlignment = SystemEffectiveAlignment( nAlignment );
    return ::operator new(
        cbSize,
        static_cast<std::align_val_t>( nEffectiveAlignment ),
        std::nothrow );
}

void SystemFree(
    void *,
    void *pMemory,
    usize,
    usize nAlignment ) noexcept
{
    const usize nEffectiveAlignment = SystemEffectiveAlignment( nAlignment );
    ::operator delete(
        pMemory,
        static_cast<std::align_val_t>( nEffectiveAlignment ) );
}

const allocator_t g_systemAllocator{
    SystemAllocate,
    nullptr,
    SystemFree,
    nullptr
};

bool_t OwnedAllocationIsCanonicalEmpty(
    const owned_allocation_t &allocation ) noexcept
{
    return allocation.pData == nullptr &&
           allocation.cbSize == 0u &&
           allocation.nAlignment == 0u &&
           allocation.pAllocator == nullptr;
}

void ResetOwnedAllocation( owned_allocation_t &allocation ) noexcept
{
    allocation.pData = nullptr;
    allocation.cbSize = 0u;
    allocation.nAlignment = 0u;
    allocation.pAllocator = nullptr;
}

} // namespace

const allocator_t *Allocator_GetSystem() noexcept
{
    return &g_systemAllocator;
}

bool_t Allocator_IsValid( const allocator_t *pAllocator ) noexcept
{
    return pAllocator != nullptr &&
           pAllocator->pfnAllocate != nullptr &&
           pAllocator->pfnFree != nullptr;
}

void *Allocator_Allocate(
    const allocator_t *pAllocator,
    usize cbSize,
    usize nAlignment ) noexcept
{
    if ( cbSize == 0u ) {
        return nullptr;
    }

    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG( bValidAllocator, "Allocator_Allocate requires a valid allocator." );
    if ( !bValidAllocator ) {
        return nullptr;
    }

    const bool_t bValidAlignment = Cy_AlignIsPowerOfTwo( nAlignment );
    CY_ASSERT_MSG( bValidAlignment, "Allocator_Allocate requires a non-zero power-of-two alignment." );
    if ( !bValidAlignment ) {
        return nullptr;
    }

    void *pMemory = pAllocator->pfnAllocate(
        pAllocator->pUserData,
        cbSize,
        nAlignment );
    if ( pMemory == nullptr ) {
        return nullptr;
    }

    const bool_t bMemoryAligned = Cy_AlignIsPointerAligned( pMemory, nAlignment );
    CY_ASSERT_MSG( bMemoryAligned, "Allocator callback returned incorrectly aligned memory." );
    if ( !bMemoryAligned ) {
        pAllocator->pfnFree(
            pAllocator->pUserData,
            pMemory,
            cbSize,
            nAlignment );
        return nullptr;
    }

    return pMemory;
}

void *Allocator_AllocateZeroed(
    const allocator_t *pAllocator,
    usize cbSize,
    usize nAlignment ) noexcept
{
    void *pMemory = Allocator_Allocate( pAllocator, cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        Cy_MemZero( pMemory, cbSize );
    }
    return pMemory;
}

void *Allocator_Reallocate(
    const allocator_t *pAllocator,
    void *pMemory,
    usize cbOldSize,
    usize cbNewSize,
    usize nAlignment ) noexcept
{
    if ( pMemory == nullptr ) {
        const bool_t bValidOldSize = cbOldSize == 0u;
        CY_ASSERT_MSG( bValidOldSize, "Allocator_Reallocate requires zero old size for null memory." );
        if ( !bValidOldSize ) {
            return nullptr;
        }

        return Allocator_Allocate( pAllocator, cbNewSize, nAlignment );
    }

    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG( bValidAllocator, "Allocator_Reallocate requires a valid allocator." );
    if ( !bValidAllocator ) {
        return nullptr;
    }

    const bool_t bValidOldSize = cbOldSize > 0u;
    CY_ASSERT_MSG( bValidOldSize, "Allocator_Reallocate requires the original non-zero allocation size." );
    if ( !bValidOldSize ) {
        return nullptr;
    }

    const bool_t bValidAlignment = Cy_AlignIsPowerOfTwo( nAlignment );
    CY_ASSERT_MSG( bValidAlignment, "Allocator_Reallocate requires a non-zero power-of-two alignment." );
    if ( !bValidAlignment ) {
        return nullptr;
    }

    const bool_t bMemoryAligned = Cy_AlignIsPointerAligned( pMemory, nAlignment );
    CY_ASSERT_MSG( bMemoryAligned, "Allocator_Reallocate received incorrectly aligned memory." );
    if ( !bMemoryAligned ) {
        return nullptr;
    }

    if ( cbNewSize == 0u ) {
        pAllocator->pfnFree(
            pAllocator->pUserData,
            pMemory,
            cbOldSize,
            nAlignment );
        return nullptr;
    }

    if ( cbNewSize == cbOldSize ) {
        return pMemory;
    }

    if ( pAllocator->pfnReallocate != nullptr ) {
        void *pNewMemory = pAllocator->pfnReallocate(
            pAllocator->pUserData,
            pMemory,
            cbOldSize,
            cbNewSize,
            nAlignment );
        if ( pNewMemory == nullptr ) {
            return nullptr;
        }

        const bool_t bNewMemoryAligned = Cy_AlignIsPointerAligned(
            pNewMemory,
            nAlignment );
        CY_ASSERT_MSG( bNewMemoryAligned, "Allocator reallocation callback returned incorrectly aligned memory." );
        if ( !bNewMemoryAligned ) {
            CY_CRASH( "Allocator reallocation callback violated its alignment contract." );
        }

        return pNewMemory;
    }

    void *pNewMemory = Allocator_Allocate(
        pAllocator,
        cbNewSize,
        nAlignment );
    if ( pNewMemory == nullptr ) {
        return nullptr;
    }

    const usize cbCopySize = cbOldSize < cbNewSize
        ? cbOldSize
        : cbNewSize;
    Cy_MemCopy( pNewMemory, pMemory, cbCopySize );
    pAllocator->pfnFree(
        pAllocator->pUserData,
        pMemory,
        cbOldSize,
        nAlignment );

    return pNewMemory;
}

void Allocator_Free(
    const allocator_t *pAllocator,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    if ( pMemory == nullptr ) {
        return;
    }

    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG( bValidAllocator, "Allocator_Free requires a valid allocator." );
    if ( !bValidAllocator ) {
        return;
    }

    const bool_t bValidSize = cbSize > 0u;
    CY_ASSERT_MSG( bValidSize, "Allocator_Free requires the original non-zero allocation size." );
    if ( !bValidSize ) {
        return;
    }

    const bool_t bValidAlignment = Cy_AlignIsPowerOfTwo( nAlignment );
    CY_ASSERT_MSG( bValidAlignment, "Allocator_Free requires a non-zero power-of-two alignment." );
    if ( !bValidAlignment ) {
        return;
    }

    const bool_t bMemoryAligned = Cy_AlignIsPointerAligned( pMemory, nAlignment );
    CY_ASSERT_MSG( bMemoryAligned, "Allocator_Free received incorrectly aligned memory." );
    if ( !bMemoryAligned ) {
        return;
    }

    pAllocator->pfnFree(
        pAllocator->pUserData,
        pMemory,
        cbSize,
        nAlignment );
}

bool_t Allocator_OwnedIsValid(
    const owned_allocation_t *pAllocation ) noexcept
{
    if ( pAllocation == nullptr ) {
        return CY_FALSE;
    }

    if ( OwnedAllocationIsCanonicalEmpty( *pAllocation ) ) {
        return CY_TRUE;
    }

    return pAllocation->pData != nullptr &&
           pAllocation->cbSize > 0u &&
           Allocator_IsValid( pAllocation->pAllocator ) &&
           Cy_AlignIsPowerOfTwo( pAllocation->nAlignment ) &&
           Cy_AlignIsPointerAligned(
               pAllocation->pData,
               pAllocation->nAlignment );
}

bool_t Allocator_AdoptOwned(
    owned_allocation_t *pAllocation,
    const allocator_t *pAllocator,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    const bool_t bValidDestination =
        pAllocation != nullptr &&
        OwnedAllocationIsCanonicalEmpty( *pAllocation );
    CY_ASSERT_MSG(
        bValidDestination,
        "Allocator_AdoptOwned requires an empty ownership record." );
    if ( !bValidDestination ) {
        return CY_FALSE;
    }

    if ( pMemory == nullptr ) {
        const bool_t bCanonicalEmptyInput =
            cbSize == 0u &&
            pAllocator == nullptr;
        CY_ASSERT_MSG(
            bCanonicalEmptyInput,
            "A null owned allocation must not carry allocation metadata." );
        return bCanonicalEmptyInput;
    }

    const bool_t bValidOwnership =
        cbSize > 0u &&
        Allocator_IsValid( pAllocator ) &&
        Cy_AlignIsPowerOfTwo( nAlignment ) &&
        Cy_AlignIsPointerAligned( pMemory, nAlignment );
    CY_ASSERT_MSG(
        bValidOwnership,
        "Allocator_AdoptOwned received invalid allocation metadata." );
    if ( !bValidOwnership ) {
        return CY_FALSE;
    }

    pAllocation->pData = pMemory;
    pAllocation->cbSize = cbSize;
    pAllocation->nAlignment = nAlignment;
    pAllocation->pAllocator = pAllocator;
    return CY_TRUE;
}

bool_t Allocator_AllocateOwned(
    owned_allocation_t *pAllocation,
    const allocator_t *pAllocator,
    usize cbSize,
    usize nAlignment ) noexcept
{
    const bool_t bValidDestination =
        pAllocation != nullptr &&
        OwnedAllocationIsCanonicalEmpty( *pAllocation );
    CY_ASSERT_MSG(
        bValidDestination,
        "Allocator_AllocateOwned requires an empty ownership record." );
    if ( !bValidDestination ) {
        return CY_FALSE;
    }

    if ( cbSize == 0u ) {
        return CY_TRUE;
    }

    void *pMemory = Allocator_Allocate( pAllocator, cbSize, nAlignment );
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }

    pAllocation->pData = pMemory;
    pAllocation->cbSize = cbSize;
    pAllocation->nAlignment = nAlignment;
    pAllocation->pAllocator = pAllocator;
    return CY_TRUE;
}

bool_t Allocator_MoveOwned(
    owned_allocation_t *pDestination,
    owned_allocation_t *pSource ) noexcept
{
    const bool_t bDistinctRecords =
        pDestination != nullptr &&
        pSource != nullptr &&
        pDestination != pSource;
    const bool_t bDestinationEmpty =
        bDistinctRecords &&
        OwnedAllocationIsCanonicalEmpty( *pDestination );
    const bool_t bSourceValid =
        bDistinctRecords &&
        Allocator_OwnedIsValid( pSource );
    const bool_t bValidMove =
        bDistinctRecords &&
        bDestinationEmpty &&
        bSourceValid;
    CY_ASSERT_MSG(
        bValidMove,
        "Allocator_MoveOwned requires distinct, valid records and an empty destination." );
    if ( !bValidMove ) {
        return CY_FALSE;
    }

    pDestination->pData = pSource->pData;
    pDestination->cbSize = pSource->cbSize;
    pDestination->nAlignment = pSource->nAlignment;
    pDestination->pAllocator = pSource->pAllocator;
    ResetOwnedAllocation( *pSource );
    return CY_TRUE;
}

void Allocator_FreeOwned( owned_allocation_t *pAllocation ) noexcept
{
    const bool_t bValidRecord = pAllocation != nullptr;
    CY_ASSERT_MSG( bValidRecord, "Allocator_FreeOwned requires an allocation record." );
    if ( !bValidRecord ) {
        return;
    }

    if ( pAllocation->pData == nullptr ) {
        const bool_t bCanonicalEmpty = OwnedAllocationIsCanonicalEmpty( *pAllocation );
        if ( !bCanonicalEmpty ) {
            CY_ASSERT_MSG( bCanonicalEmpty, "An empty owned allocation must not retain ownership metadata." );
        }
        ResetOwnedAllocation( *pAllocation );
        return;
    }

    const bool_t bValidAllocator = Allocator_IsValid( pAllocation->pAllocator );
    const bool_t bValidSize = pAllocation->cbSize > 0u;
    const bool_t bValidAlignment = Cy_AlignIsPowerOfTwo( pAllocation->nAlignment );
    const bool_t bMemoryAligned = bValidAlignment && Cy_AlignIsPointerAligned(
        pAllocation->pData,
        pAllocation->nAlignment );
    const bool_t bValidOwnership =
        bValidAllocator &&
        bValidSize &&
        bMemoryAligned;

    CY_ASSERT_MSG( bValidOwnership, "Allocator_FreeOwned received an invalid ownership record." );
    if ( !bValidOwnership ) {
        return;
    }

    Allocator_Free(
        pAllocation->pAllocator,
        pAllocation->pData,
        pAllocation->cbSize,
        pAllocation->nAlignment );
    ResetOwnedAllocation( *pAllocation );
}

} // namespace cypher::common
