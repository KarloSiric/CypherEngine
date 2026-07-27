//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Align.h
//  Purpose: Declares CypherCommon Tier0 Align support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_ALIGN_H
#define CYPHER_COMMON_TIER0_ALIGN_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Align

Power-of-two alignment helpers for memory, binary file formats and GPU data.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

// Returns true when value is a non-zero power of two.
[[nodiscard]] constexpr bool_t Cy_AlignIsPowerOfTwo( usize value ) noexcept
{
    return value != 0u && ( value & ( value - 1u ) ) == 0u;
}

// Rounds value up to the next alignment boundary. Alignment must be a non-zero
// power of two and the result must be representable; use Cy_AlignUpChecked for
// untrusted values.
[[nodiscard]] constexpr usize Cy_AlignUp( usize nValue, usize nAlignment ) noexcept
{
    return ( nValue + ( nAlignment - 1u ) ) & ~( nAlignment - 1u );
}

// Rounds value down to the previous boundary. Alignment must be a non-zero
// power of two; use Cy_AlignDownChecked for untrusted values.
[[nodiscard]] constexpr usize Cy_AlignDown( usize nValue, usize nAlignment ) noexcept
{
    return nValue & ~( nAlignment - 1u );
}

// Returns true when value already satisfies alignment.
[[nodiscard]] constexpr bool_t Cy_AlignIsAligned( usize nValue, usize nAlignment ) noexcept
{
    return Cy_AlignIsPowerOfTwo( nAlignment ) &&
           ( nValue & ( nAlignment - 1u ) ) == 0u;
}

// Returns how many bytes are needed to align value upward. The same preconditions
// as Cy_AlignUp apply.
[[nodiscard]] constexpr usize Cy_AlignPadding( usize nValue, usize nAlignment ) noexcept
{
    return Cy_AlignUp( nValue, nAlignment ) - nValue;
}

// Rounds value up and reports overflow instead of wrapping silently.
[[nodiscard]] constexpr bool_t Cy_AlignUpChecked(
    usize nValue,
    usize nAlignment,
    usize &nOutValue ) noexcept
{
    if ( !Cy_AlignIsPowerOfTwo( nAlignment ) ) {
        nOutValue = 0u;
        return CY_FALSE;
    }

    const usize nMask = nAlignment - 1u;
    if ( nValue > CY_INVALID_SIZE - nMask ) {
        nOutValue = 0u;
        return CY_FALSE;
    }

    nOutValue = Cy_AlignUp( nValue, nAlignment );
    return CY_TRUE;
}

// Rounds value down and rejects invalid alignment.
[[nodiscard]] constexpr bool_t Cy_AlignDownChecked(
    usize nValue,
    usize nAlignment,
    usize &nOutValue ) noexcept
{
    if ( !Cy_AlignIsPowerOfTwo( nAlignment ) ) {
        nOutValue = 0u;
        return CY_FALSE;
    }

    nOutValue = Cy_AlignDown( nValue, nAlignment );
    return CY_TRUE;
}

// Calculates upward padding and reports invalid alignment or overflow.
[[nodiscard]] constexpr bool_t Cy_AlignPaddingChecked(
    usize nValue,
    usize nAlignment,
    usize &nOutPadding ) noexcept
{
    usize nAlignedValue = 0u;
    if ( !Cy_AlignUpChecked( nValue, nAlignment, nAlignedValue ) ) {
        nOutPadding = 0u;
        return CY_FALSE;
    }

    nOutPadding = nAlignedValue - nValue;
    return CY_TRUE;
}

// Rounds a writable pointer up to the next alignment boundary.
[[nodiscard]] inline void *Cy_AlignPointerUp( void *pPtr, usize nAlignment ) noexcept
{
    return reinterpret_cast<void *>( Cy_AlignUp( reinterpret_cast<uintptr>( pPtr ), nAlignment ) );
}

// Rounds a read-only pointer up to the next alignment boundary.
[[nodiscard]] inline const void *Cy_AlignPointerUp( const void *pPtr, usize nAlignment ) noexcept
{
    return reinterpret_cast<const void *>( Cy_AlignUp( reinterpret_cast<uintptr>( pPtr ), nAlignment ) );
}

// Rounds a writable pointer down to the previous alignment boundary.
[[nodiscard]] inline void *Cy_AlignPointerDown( void *pPtr, usize nAlignment ) noexcept
{
    return reinterpret_cast<void *>( Cy_AlignDown( reinterpret_cast<uintptr>( pPtr ), nAlignment ) );
}

// Rounds a read-only pointer down to the previous alignment boundary.
[[nodiscard]] inline const void *Cy_AlignPointerDown( const void *pPtr, usize nAlignment ) noexcept
{
    return reinterpret_cast<const void *>( Cy_AlignDown( reinterpret_cast<uintptr>( pPtr ), nAlignment ) );
}

// Returns true when ptr satisfies alignment.
[[nodiscard]] inline bool_t Cy_AlignIsPointerAligned( const void *pPtr, usize nAlignment ) noexcept
{
    return Cy_AlignIsAligned( reinterpret_cast<uintptr>( pPtr ), nAlignment );
}

// Returns how many bytes are needed to align a pointer upward.
[[nodiscard]] inline usize Cy_AlignPointerPadding( const void *pPtr, usize nAlignment ) noexcept
{
    return Cy_AlignPadding( reinterpret_cast<uintptr>( pPtr ), nAlignment );
}

// Rounds a pointer address up and reports overflow instead of wrapping silently.
[[nodiscard]] inline bool_t Cy_AlignPointerUpChecked(
    const void *pPtr,
    usize nAlignment,
    uintptr &nOutAddress ) noexcept
{
    return Cy_AlignUpChecked( reinterpret_cast<uintptr>( pPtr ), nAlignment, nOutAddress );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ALIGN_H
