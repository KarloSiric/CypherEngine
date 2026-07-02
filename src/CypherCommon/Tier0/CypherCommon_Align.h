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
#pragma once

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
constexpr bool_t IsPowerOfTwo( usize value )
{
    return value != 0u && ( value & ( value - 1u ) ) == 0u;
}

// Rounds value up to the next alignment boundary.
constexpr usize AlignUp( usize nValue, usize nAlignment )
{
    return ( nValue + ( nAlignment - 1u ) ) & ~( nAlignment - 1u );
}

// Rounds value down to the previous alignment boundary.
constexpr usize AlignDown( usize nValue, usize nAlignment )
{
    return nValue & ~( nAlignment - 1u );
}

// Returns true when value already satisfies alignment.
constexpr bool_t IsAligned( usize nValue, usize nAlignment )
{
    return ( nValue & ( nAlignment - 1u ) ) == 0u;
}

// Returns how many bytes are needed to align value upward.
constexpr usize AlignPadding( usize nValue, usize nAlignment )
{
    return AlignUp( nValue, nAlignment ) - nValue;
}

// Rounds value up and reports overflow instead of wrapping silently.
constexpr bool_t AlignUpChecked( usize nValue, usize nAlignment, usize &nOutValue )
{
    if ( !IsPowerOfTwo( nAlignment ) ) {
        nOutValue = 0u;
        return CY_FALSE;
    }

    const usize nMask = nAlignment - 1u;
    if ( nValue > CY_INVALID_SIZE - nMask ) {
        nOutValue = 0u;
        return CY_FALSE;
    }

    nOutValue = AlignUp( nValue, nAlignment );
    return CY_TRUE;
}

// Rounds a writable pointer up to the next alignment boundary.
inline void *AlignPointerUp( void *pPtr, usize nAlignment )
{
    return reinterpret_cast<void *>( AlignUp( reinterpret_cast<uintptr>( pPtr ), nAlignment ) );
}

// Rounds a read-only pointer up to the next alignment boundary.
inline const void *AlignPointerUp( const void *pPtr, usize nAlignment )
{
    return reinterpret_cast<const void *>( AlignUp( reinterpret_cast<uintptr>( pPtr ), nAlignment ) );
}

// Rounds a writable pointer down to the previous alignment boundary.
inline void *AlignPointerDown( void *pPtr, usize nAlignment )
{
    return reinterpret_cast<void *>( AlignDown( reinterpret_cast<uintptr>( pPtr ), nAlignment ) );
}

// Rounds a read-only pointer down to the previous alignment boundary.
inline const void *AlignPointerDown( const void *pPtr, usize nAlignment )
{
    return reinterpret_cast<const void *>( AlignDown( reinterpret_cast<uintptr>( pPtr ), nAlignment ) );
}

// Returns true when ptr satisfies alignment.
inline bool_t IsPointerAligned( const void *pPtr, usize nAlignment )
{
    return IsAligned( reinterpret_cast<uintptr>( pPtr ), nAlignment );
}

// Returns how many bytes are needed to align a pointer upward.
inline usize AlignPointerPadding( const void *pPtr, usize nAlignment )
{
    return AlignPadding( reinterpret_cast<uintptr>( pPtr ), nAlignment );
}

// Rounds a pointer address up and reports overflow instead of wrapping silently.
inline bool_t AlignPointerUpChecked( const void *pPtr, usize nAlignment, uintptr &nOutAddress )
{
    return AlignUpChecked( reinterpret_cast<uintptr>( pPtr ), nAlignment, nOutAddress );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ALIGN_H
