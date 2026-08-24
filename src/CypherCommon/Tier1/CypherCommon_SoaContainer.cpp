//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SoaContainer.cpp
//  Purpose: Implements descriptor-driven structure-of-arrays storage.
//  Details: All columns share one aligned allocation. Reserve computes a complete
//           replacement layout before allocation so failure leaves the container intact.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Soa Container Implementation Notes

Container mutations must preserve structural invariants and element lifetime. Iterators or
handles are invalidated only according to the rules stated by the public API.
================
*/

#include "CypherCommon_SoaContainer.h"

namespace cypher::common
{

namespace
{

bool_t ContainerIsCanonicalEmpty( const soa_container_t &container ) noexcept
{
    if ( container.pAllocation != nullptr ||
         container.nColumnCount != 0u ||
         container.nCount != 0u ||
         container.nCapacity != 0u ||
         container.cbAllocation != 0u ||
         container.nAllocationAlignment != 0u ||
         container.pAllocator != nullptr ) {
        return CY_FALSE;
    }
    for ( usize iColumn = 0u; iColumn < CY_SOA_MAX_COLUMNS; ++iColumn ) {
        if ( container.pColumns[iColumn] != nullptr ||
             container.columns[iColumn].cbElement != 0u ||
             container.columns[iColumn].alignment != 1u ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

void ResetContainer( soa_container_t &container ) noexcept
{
    container.pAllocation = nullptr;
    for ( usize iColumn = 0u; iColumn < CY_SOA_MAX_COLUMNS; ++iColumn ) {
        container.pColumns[iColumn] = nullptr;
        container.columns[iColumn] = {};
    }
    container.nColumnCount = 0u;
    container.nCount = 0u;
    container.nCapacity = 0u;
    container.cbAllocation = 0u;
    container.nAllocationAlignment = 0u;
    container.pAllocator = nullptr;
}

bool_t ColumnsAreValid(
    const soa_column_desc_t *pColumns,
    usize nColumnCount ) noexcept
{
    if ( pColumns == nullptr ||
         nColumnCount == 0u ||
         nColumnCount > CY_SOA_MAX_COLUMNS ) {
        return CY_FALSE;
    }
    for ( usize iColumn = 0u; iColumn < nColumnCount; ++iColumn ) {
        if ( pColumns[iColumn].cbElement == 0u ||
             !Cy_AlignIsPowerOfTwo( pColumns[iColumn].alignment ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t CalculateLayout(
    const soa_column_desc_t *pColumns,
    usize nColumnCount,
    usize nCapacity,
    usize *pOffsetsOut,
    usize &cbAllocationOut,
    usize &nAlignmentOut ) noexcept
{
    cbAllocationOut = 0u;
    nAlignmentOut = 1u;
    // Every column occupies one aligned contiguous slice of the same allocation.
    // Checked arithmetic rejects layouts that cannot be represented by usize.
    for ( usize iColumn = 0u; iColumn < nColumnCount; ++iColumn ) {
        const soa_column_desc_t &column = pColumns[iColumn];
        usize iOffset = 0u;
        if ( !Cy_AlignUpChecked(
                 cbAllocationOut,
                 column.alignment,
                 iOffset ) ) {
            return CY_FALSE;
        }
        if ( nCapacity > CY_USIZE_MAX / column.cbElement ) {
            return CY_FALSE;
        }
        const usize cbColumn = nCapacity * column.cbElement;
        if ( cbColumn > CY_USIZE_MAX - iOffset ) {
            return CY_FALSE;
        }
        pOffsetsOut[iColumn] = iOffset;
        cbAllocationOut = iOffset + cbColumn;
        if ( column.alignment > nAlignmentOut ) {
            nAlignmentOut = column.alignment;
        }
    }
    return CY_TRUE;
}

bool_t ContainerIsInitialized( const soa_container_t *pContainer ) noexcept
{
    return pContainer != nullptr &&
           ColumnsAreValid( pContainer->columns, pContainer->nColumnCount ) &&
           Allocator_IsValid( pContainer->pAllocator ) &&
           pContainer->nCount <= pContainer->nCapacity;
}

} // namespace

soa_container_t::~soa_container_t() noexcept
{
    SoaContainer_Shutdown( this );
}

bool_t SoaContainer_Init(
    soa_container_t *pContainer,
    const soa_desc_t &desc ) noexcept
{
    const bool_t bValidDestination =
        pContainer != nullptr && ContainerIsCanonicalEmpty( *pContainer );
    const bool_t bValidDescription =
        ColumnsAreValid( desc.pColumns, desc.nColumnCount ) &&
        Allocator_IsValid( desc.pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "SoaContainer_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidDescription,
        "SoaContainer_Init requires valid columns and an allocator." );
    if ( !bValidDestination || !bValidDescription ) {
        return CY_FALSE;
    }

    // Copy descriptions because callers may build them on the stack.
    for ( usize iColumn = 0u; iColumn < desc.nColumnCount; ++iColumn ) {
        pContainer->columns[iColumn] = desc.pColumns[iColumn];
    }
    pContainer->nColumnCount = desc.nColumnCount;
    pContainer->pAllocator = desc.pAllocator;

    if ( desc.nInitialCapacity != 0u &&
         !SoaContainer_Reserve( pContainer, desc.nInitialCapacity ) ) {
        ResetContainer( *pContainer );
        return CY_FALSE;
    }
    return CY_TRUE;
}

void SoaContainer_Shutdown( soa_container_t *pContainer ) noexcept
{
    if ( pContainer == nullptr ) {
        return;
    }
    if ( ContainerIsCanonicalEmpty( *pContainer ) ) {
        return;
    }

    const bool_t bValid = SoaContainer_IsValid( pContainer );
    CY_ASSERT_MSG( bValid, "SoaContainer_Shutdown requires a valid container." );
    if ( !bValid ) {
        return;
    }
    if ( pContainer->pAllocation != nullptr ) {
        Allocator_Free(
            pContainer->pAllocator,
            pContainer->pAllocation,
            pContainer->cbAllocation,
            pContainer->nAllocationAlignment );
    }
    ResetContainer( *pContainer );
}

void SoaContainer_Clear( soa_container_t *pContainer ) noexcept
{
    const bool_t bValid = ContainerIsInitialized( pContainer );
    CY_ASSERT_MSG( bValid, "SoaContainer_Clear requires an initialized container." );
    if ( bValid ) {
        pContainer->nCount = 0u;
    }
}

bool_t SoaContainer_IsValid( const soa_container_t *pContainer ) noexcept
{
    if ( pContainer == nullptr ) {
        return CY_FALSE;
    }
    if ( ContainerIsCanonicalEmpty( *pContainer ) ) {
        return CY_TRUE;
    }
    if ( !ContainerIsInitialized( pContainer ) ) {
        return CY_FALSE;
    }
    if ( pContainer->nCapacity == 0u ) {
        return pContainer->pAllocation == nullptr &&
               pContainer->cbAllocation == 0u &&
               pContainer->nAllocationAlignment == 0u;
    }
    if ( pContainer->pAllocation == nullptr ||
         pContainer->cbAllocation == 0u ||
         !Cy_AlignIsPowerOfTwo( pContainer->nAllocationAlignment ) ||
         !Cy_AlignIsPointerAligned(
             pContainer->pAllocation,
             pContainer->nAllocationAlignment ) ) {
        return CY_FALSE;
    }
    for ( usize iColumn = 0u; iColumn < pContainer->nColumnCount; ++iColumn ) {
        if ( pContainer->pColumns[iColumn] == nullptr ||
             !Cy_AlignIsPointerAligned(
                 pContainer->pColumns[iColumn],
                 pContainer->columns[iColumn].alignment ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t SoaContainer_Reserve(
    soa_container_t *pContainer,
    usize nCapacity ) noexcept
{
    const bool_t bValid = ContainerIsInitialized( pContainer );
    CY_ASSERT_MSG( bValid, "SoaContainer_Reserve requires an initialized container." );
    if ( !bValid ) {
        return CY_FALSE;
    }
    if ( nCapacity <= pContainer->nCapacity ) {
        return CY_TRUE;
    }

    usize offsets[CY_SOA_MAX_COLUMNS]{};
    usize cbNewAllocation = 0u;
    usize nNewAlignment = 0u;
    if ( !CalculateLayout(
             pContainer->columns,
             pContainer->nColumnCount,
             nCapacity,
             offsets,
             cbNewAllocation,
             nNewAlignment ) ) {
        return CY_FALSE;
    }

    // Allocate and populate the complete replacement before releasing the old
    // block. A failed reserve therefore leaves every existing column untouched.
    void *pNewAllocation = Allocator_Allocate(
        pContainer->pAllocator,
        cbNewAllocation,
        nNewAlignment );
    if ( pNewAllocation == nullptr ) {
        return CY_FALSE;
    }

    void *newColumns[CY_SOA_MAX_COLUMNS]{};
    auto *pNewBytes = static_cast<byte *>( pNewAllocation );
    for ( usize iColumn = 0u; iColumn < pContainer->nColumnCount; ++iColumn ) {
        newColumns[iColumn] = pNewBytes + offsets[iColumn];
        const usize cbLive = pContainer->nCount *
            pContainer->columns[iColumn].cbElement;
        if ( cbLive != 0u ) {
            Cy_MemCopy(
                newColumns[iColumn],
                pContainer->pColumns[iColumn],
                cbLive );
        }
    }

    if ( pContainer->pAllocation != nullptr ) {
        Allocator_Free(
            pContainer->pAllocator,
            pContainer->pAllocation,
            pContainer->cbAllocation,
            pContainer->nAllocationAlignment );
    }
    pContainer->pAllocation = pNewAllocation;
    pContainer->cbAllocation = cbNewAllocation;
    pContainer->nAllocationAlignment = nNewAlignment;
    pContainer->nCapacity = nCapacity;
    for ( usize iColumn = 0u; iColumn < pContainer->nColumnCount; ++iColumn ) {
        pContainer->pColumns[iColumn] = newColumns[iColumn];
    }
    return CY_TRUE;
}

bool_t SoaContainer_Resize(
    soa_container_t *pContainer,
    usize nCount ) noexcept
{
    const bool_t bValid = ContainerIsInitialized( pContainer );
    CY_ASSERT_MSG( bValid, "SoaContainer_Resize requires an initialized container." );
    if ( !bValid ) {
        return CY_FALSE;
    }

    if ( nCount > pContainer->nCapacity ) {
        usize nGrowth = pContainer->nCapacity;
        if ( nGrowth == 0u ) {
            nGrowth = 1u;
        } else if ( nGrowth <= CY_USIZE_MAX - nGrowth / 2u ) {
            nGrowth += nGrowth / 2u;
        } else {
            nGrowth = CY_USIZE_MAX;
        }
        if ( nGrowth < nCount ) {
            nGrowth = nCount;
        }
        if ( !SoaContainer_Reserve( pContainer, nGrowth ) ) {
            return CY_FALSE;
        }
    }

    if ( nCount > pContainer->nCount ) {
        // SoA stores raw trivial records; new rows begin in a deterministic zero state.
        const usize nAdded = nCount - pContainer->nCount;
        for ( usize iColumn = 0u; iColumn < pContainer->nColumnCount; ++iColumn ) {
            const usize cbElement = pContainer->columns[iColumn].cbElement;
            auto *pColumn = static_cast<byte *>( pContainer->pColumns[iColumn] );
            Cy_MemZero(
                pColumn + pContainer->nCount * cbElement,
                nAdded * cbElement );
        }
    }
    pContainer->nCount = nCount;
    return CY_TRUE;
}

void *SoaContainer_Column(
    soa_container_t *pContainer,
    usize iColumn ) noexcept
{
    return const_cast<void *>( SoaContainer_Column(
        static_cast<const soa_container_t *>( pContainer ),
        iColumn ) );
}

const void *SoaContainer_Column(
    const soa_container_t *pContainer,
    usize iColumn ) noexcept
{
    if ( !ContainerIsInitialized( pContainer ) ||
         iColumn >= pContainer->nColumnCount ) {
        return nullptr;
    }
    return pContainer->pColumns[iColumn];
}

void *SoaContainer_Element(
    soa_container_t *pContainer,
    usize iColumn,
    usize iElement ) noexcept
{
    return const_cast<void *>( SoaContainer_Element(
        static_cast<const soa_container_t *>( pContainer ),
        iColumn,
        iElement ) );
}

const void *SoaContainer_Element(
    const soa_container_t *pContainer,
    usize iColumn,
    usize iElement ) noexcept
{
    const auto *pColumn = static_cast<const byte *>(
        SoaContainer_Column( pContainer, iColumn ) );
    if ( pColumn == nullptr || iElement >= pContainer->nCount ) {
        return nullptr;
    }
    return pColumn + iElement * pContainer->columns[iColumn].cbElement;
}

usize SoaContainer_Count( const soa_container_t *pContainer ) noexcept
{
    return ContainerIsInitialized( pContainer ) ? pContainer->nCount : 0u;
}

usize SoaContainer_Capacity( const soa_container_t *pContainer ) noexcept
{
    return ContainerIsInitialized( pContainer ) ? pContainer->nCapacity : 0u;
}

void SoaContainer_EraseSwap(
    soa_container_t *pContainer,
    usize iElement ) noexcept
{
    const bool_t bValid = ContainerIsInitialized( pContainer ) &&
                          iElement < pContainer->nCount;
    CY_ASSERT_MSG( bValid, "SoaContainer_EraseSwap requires a live element." );
    if ( !bValid ) {
        return;
    }

    // Swap removal preserves dense columns at the cost of row ordering. Each
    // column must move the same last index to keep the logical record coherent.
    const usize iLast = pContainer->nCount - 1u;
    for ( usize iColumn = 0u; iColumn < pContainer->nColumnCount; ++iColumn ) {
        const usize cbElement = pContainer->columns[iColumn].cbElement;
        auto *pColumn = static_cast<byte *>( pContainer->pColumns[iColumn] );
        byte *pRemoved = pColumn + iElement * cbElement;
        byte *pLast = pColumn + iLast * cbElement;
        if ( iElement != iLast ) {
            Cy_MemCopy( pRemoved, pLast, cbElement );
        }
        Cy_MemZero( pLast, cbElement );
    }
    --pContainer->nCount;
}

void SoaContainer_Move(
    soa_container_t *pDestination,
    soa_container_t *pSource ) noexcept
{
    const bool_t bValid = pDestination != nullptr &&
                          pSource != nullptr &&
                          pDestination != pSource &&
                          ContainerIsCanonicalEmpty( *pDestination ) &&
                          SoaContainer_IsValid( pSource );
    CY_ASSERT_MSG(
        bValid,
        "SoaContainer_Move requires distinct valid source and empty destination." );
    if ( !bValid ) {
        return;
    }

    // Transfer the single allocation and every derived column pointer together.
    pDestination->pAllocation = pSource->pAllocation;
    for ( usize iColumn = 0u; iColumn < CY_SOA_MAX_COLUMNS; ++iColumn ) {
        pDestination->pColumns[iColumn] = pSource->pColumns[iColumn];
        pDestination->columns[iColumn] = pSource->columns[iColumn];
    }
    pDestination->nColumnCount = pSource->nColumnCount;
    pDestination->nCount = pSource->nCount;
    pDestination->nCapacity = pSource->nCapacity;
    pDestination->cbAllocation = pSource->cbAllocation;
    pDestination->nAllocationAlignment = pSource->nAllocationAlignment;
    pDestination->pAllocator = pSource->pAllocator;
    ResetContainer( *pSource );
}

} // namespace cypher::common
