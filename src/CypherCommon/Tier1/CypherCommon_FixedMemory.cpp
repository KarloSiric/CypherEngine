//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedMemory.cpp
//  Purpose: Implements non-owning bounded writable memory regions.
//  Details: Address checks use offset arithmetic so malformed or near-limit
//           ranges cannot wrap while being validated.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_FixedMemory.h"

namespace cypher::common
{

fixed_memory_t FixedMemory_Make( void *pData, usize cbSize ) noexcept
{
    const bool_t bValidMemory = pData != nullptr || cbSize == 0u;
    CY_ASSERT_MSG(
        bValidMemory,
        "FixedMemory_Make requires non-null data for a non-empty region." );
    if ( !bValidMemory ) {
        return {};
    }

    return { static_cast<byte *>( pData ), cbSize };
}

fixed_memory_t FixedMemory_FromSpan( byte_span_t memory ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( memory );
    CY_ASSERT_MSG(
        bValidSpan,
        "FixedMemory_FromSpan requires a valid byte span." );
    if ( !bValidSpan ) {
        return {};
    }

    return { memory.pData, memory.nCount };
}

bool_t FixedMemory_IsValid( fixed_memory_t memory ) noexcept
{
    return memory.pData != nullptr || memory.cbSize == 0u;
}

bool_t FixedMemory_IsEmpty( fixed_memory_t memory ) noexcept
{
    return memory.cbSize == 0u;
}

byte *FixedMemory_Data( fixed_memory_t memory ) noexcept
{
    const bool_t bValidMemory = FixedMemory_IsValid( memory );
    CY_ASSERT_MSG( bValidMemory, "FixedMemory_Data requires a valid region." );
    return bValidMemory ? memory.pData : nullptr;
}

usize FixedMemory_Size( fixed_memory_t memory ) noexcept
{
    const bool_t bValidMemory = FixedMemory_IsValid( memory );
    CY_ASSERT_MSG( bValidMemory, "FixedMemory_Size requires a valid region." );
    return bValidMemory ? memory.cbSize : 0u;
}

byte_span_t FixedMemory_Span( fixed_memory_t memory ) noexcept
{
    const bool_t bValidMemory = FixedMemory_IsValid( memory );
    CY_ASSERT_MSG( bValidMemory, "FixedMemory_Span requires a valid region." );
    if ( !bValidMemory ) {
        return {};
    }

    return { memory.pData, memory.cbSize };
}

bool_t FixedMemory_ContainsAddress(
    fixed_memory_t memory,
    const void *pAddress ) noexcept
{
    if ( !FixedMemory_IsValid( memory ) ||
         memory.pData == nullptr ||
         pAddress == nullptr ) {
        return CY_FALSE;
    }

    const uintptr nBaseAddress = reinterpret_cast<uintptr>( memory.pData );
    const uintptr nAddress = reinterpret_cast<uintptr>( pAddress );
    if ( nAddress < nBaseAddress ) {
        return CY_FALSE;
    }

    const uintptr nOffset = nAddress - nBaseAddress;
    return nOffset < memory.cbSize;
}

bool_t FixedMemory_ContainsRange(
    fixed_memory_t memory,
    const void *pAddress,
    usize cbRange ) noexcept
{
    if ( !FixedMemory_IsValid( memory ) ||
         memory.pData == nullptr ||
         pAddress == nullptr ) {
        return CY_FALSE;
    }

    const uintptr nBaseAddress = reinterpret_cast<uintptr>( memory.pData );
    const uintptr nAddress = reinterpret_cast<uintptr>( pAddress );
    if ( nAddress < nBaseAddress ) {
        return CY_FALSE;
    }

    const uintptr nOffset = nAddress - nBaseAddress;
    if ( nOffset > memory.cbSize ) {
        return CY_FALSE;
    }

    return cbRange <= memory.cbSize - static_cast<usize>( nOffset );
}

usize FixedMemory_OffsetOf(
    fixed_memory_t memory,
    const void *pAddress ) noexcept
{
    if ( !FixedMemory_ContainsAddress( memory, pAddress ) ) {
        return CY_INVALID_SIZE;
    }

    return static_cast<usize>(
        reinterpret_cast<uintptr>( pAddress ) -
        reinterpret_cast<uintptr>( memory.pData ) );
}

byte_span_t FixedMemory_Subspan(
    fixed_memory_t memory,
    usize iOffset,
    usize cbSize ) noexcept
{
    const bool_t bValidMemory = FixedMemory_IsValid( memory );
    CY_ASSERT_MSG(
        bValidMemory,
        "FixedMemory_Subspan requires a valid source region." );
    if ( !bValidMemory ) {
        return {};
    }

    const bool_t bOffsetInRange = iOffset <= memory.cbSize;
    CY_ASSERT_MSG(
        bOffsetInRange,
        "FixedMemory_Subspan offset is outside the source region." );
    if ( !bOffsetInRange ) {
        iOffset = memory.cbSize;
    }

    if ( memory.pData == nullptr ) {
        return {};
    }

    const usize cbAvailable = memory.cbSize - iOffset;
    const usize cbSubspan = cbSize < cbAvailable ? cbSize : cbAvailable;
    return { memory.pData + iOffset, cbSubspan };
}

} // namespace cypher::common
