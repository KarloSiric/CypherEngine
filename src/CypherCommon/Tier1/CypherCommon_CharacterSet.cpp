//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CharacterSet.cpp
//  Purpose: Implements CypherCommon Tier1 CharacterSet support.
//  Details: Character sets provide a fixed 256-bit lookup table for byte-oriented
//           parsing, tokenization, path handling, and other allocation-free text
//           operations. Each possible byte value maps directly to one membership bit.
//
//  History:
//  - Created by Karlo Siric on 2026-07-31
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CharacterSet.h"

#include "CypherCommon_Assert.h"
#include "CypherCommon_Bits.h"

namespace cypher::common
{

static constexpr usize CharacterSet_GetWordIndex( char chValue ) noexcept
{
    const usize nValue = static_cast<usize>( static_cast<u8>( chValue ) );
    return nValue / CY_CHARACTER_SET_WORD_BITS;
}

static constexpr u64 CharacterSet_GetBitMask( char chValue ) noexcept
{
    const u8 nValue = static_cast<u8>( chValue );
    const u32 nBitIndex = static_cast<u32>( nValue % CY_CHARACTER_SET_WORD_BITS );
    return Cy_Bit64( nBitIndex );
}

void CharacterSet_Clear( character_set_t *pSet ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_Clear requires a valid set." );
    if ( pSet == nullptr ) {
        return;
    }

    *pSet = {};
}

void CharacterSet_Fill( character_set_t *pSet ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_Fill requires a valid set." );
    if ( pSet == nullptr ) {
        return;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        pSet->bitWords[iWord] = CY_U64_MAX;
    }
}

bool_t CharacterSet_IsEmpty( const character_set_t *pSet ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_IsEmpty requires a valid set." );
    if ( pSet == nullptr ) {
        return CY_FALSE;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        if ( pSet->bitWords[iWord] != 0u ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

bool_t CharacterSet_IsFull( const character_set_t *pSet ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_IsFull requires a valid set." );
    if ( pSet == nullptr ) {
        return CY_FALSE;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        if ( pSet->bitWords[iWord] != CY_U64_MAX ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

usize CharacterSet_Count( const character_set_t *pSet ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_Count requires a valid set." );
    if ( pSet == nullptr ) {
        return 0u;
    }

    usize cValues = 0u;
    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        cValues += static_cast<usize>( Cy_PopCount64( pSet->bitWords[iWord] ) );
    }

    return cValues;
}

bool_t CharacterSet_Equals(
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept
{
    CY_ASSERT_MSG( pSetA != nullptr,
                   "CharacterSet_Equals requires a valid first set." );
    CY_ASSERT_MSG( pSetB != nullptr,
                   "CharacterSet_Equals requires a valid second set." );

    if ( pSetA == nullptr || pSetB == nullptr ) {
        return CY_FALSE;
    }

    if ( pSetA == pSetB ) {
        return CY_TRUE;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        if ( pSetA->bitWords[iWord] != pSetB->bitWords[iWord] ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

void CharacterSet_Add( character_set_t *pSet, char chValue ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_Add requires a valid set." );
    if ( pSet == nullptr ) {
        return;
    }

    const usize iWordIndex = CharacterSet_GetWordIndex( chValue );
    const u64 nBitMask = CharacterSet_GetBitMask( chValue );
    pSet->bitWords[iWordIndex] |= nBitMask;
}

void CharacterSet_Remove( character_set_t *pSet, char chValue ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_Remove requires a valid set." );
    if ( pSet == nullptr ) {
        return;
    }

    const usize iWordIndex = CharacterSet_GetWordIndex( chValue );
    const u64 nBitMask = CharacterSet_GetBitMask( chValue );
    pSet->bitWords[iWordIndex] &= ~nBitMask;
}

bool_t CharacterSet_Contains( const character_set_t *pSet, char chValue ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_Contains requires a valid set." );
    if ( pSet == nullptr ) {
        return CY_FALSE;
    }

    const usize iWordIndex = CharacterSet_GetWordIndex( chValue );
    const u64 nBitMask = CharacterSet_GetBitMask( chValue );
    return ( pSet->bitWords[iWordIndex] & nBitMask ) != 0u;
}

void CharacterSet_AddRange(
    character_set_t *pSet,
    char chFirst,
    char chLast ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_AddRange requires a valid set." );
    if ( pSet == nullptr ) {
        return;
    }

    const u32 nFirstValue = static_cast<u32>( static_cast<u8>( chFirst ) );
    const u32 nLastValue = static_cast<u32>( static_cast<u8>( chLast ) );
    const bool_t bValidRange = nFirstValue <= nLastValue;

    CY_ASSERT_MSG( bValidRange,
                   "CharacterSet_AddRange requires an ordered byte range." );
    if ( !bValidRange ) {
        return;
    }

    const usize iFirstWord = nFirstValue / CY_CHARACTER_SET_WORD_BITS;
    const usize iLastWord = nLastValue / CY_CHARACTER_SET_WORD_BITS;

    for ( usize iWord = iFirstWord; iWord <= iLastWord; ++iWord ) {
        const u32 nFirstBit = iWord == iFirstWord
            ? static_cast<u32>( nFirstValue % CY_CHARACTER_SET_WORD_BITS )
            : 0u;
        const u32 nLastBit = iWord == iLastWord
            ? static_cast<u32>( nLastValue % CY_CHARACTER_SET_WORD_BITS )
            : static_cast<u32>( CY_CHARACTER_SET_WORD_BITS - 1u );
        const u32 cBits = nLastBit - nFirstBit + 1u;
        const u64 nRangeMask = Cy_BitRangeMask64( nFirstBit, cBits );

        pSet->bitWords[iWord] |= nRangeMask;
    }
}

void CharacterSet_RemoveRange(
    character_set_t *pSet,
    char chFirst,
    char chLast ) noexcept
{
    CY_ASSERT_MSG( pSet != nullptr,
                   "CharacterSet_RemoveRange requires a valid set." );
    if ( pSet == nullptr ) {
        return;
    }

    const u32 nFirstValue = static_cast<u32>( static_cast<u8>( chFirst ) );
    const u32 nLastValue = static_cast<u32>( static_cast<u8>( chLast ) );
    const bool_t bValidRange = nFirstValue <= nLastValue;

    CY_ASSERT_MSG( bValidRange,
                   "CharacterSet_RemoveRange requires an ordered byte range." );
    if ( !bValidRange ) {
        return;
    }

    const usize iFirstWord = nFirstValue / CY_CHARACTER_SET_WORD_BITS;
    const usize iLastWord = nLastValue / CY_CHARACTER_SET_WORD_BITS;

    for ( usize iWord = iFirstWord; iWord <= iLastWord; ++iWord ) {
        const u32 nFirstBit = iWord == iFirstWord
            ? static_cast<u32>( nFirstValue % CY_CHARACTER_SET_WORD_BITS )
            : 0u;
        const u32 nLastBit = iWord == iLastWord
            ? static_cast<u32>( nLastValue % CY_CHARACTER_SET_WORD_BITS )
            : static_cast<u32>( CY_CHARACTER_SET_WORD_BITS - 1u );
        const u32 cBits = nLastBit - nFirstBit + 1u;
        const u64 nRangeMask = Cy_BitRangeMask64( nFirstBit, cBits );

        pSet->bitWords[iWord] &= ~nRangeMask;
    }
}

void CharacterSet_AddView( character_set_t *pSet, string_view_t view ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    CY_ASSERT_MSG( pSet != nullptr, "CharacterSet_AddView requires a valid set." );
    CY_ASSERT_MSG( bValidView,
                   "CharacterSet_AddView requires a valid string view." );

    if ( pSet == nullptr || !bValidView ) {
        return;
    }

    for ( usize iIndex = 0u; iIndex < view.cchLength; ++iIndex ) {
        const char chValue = view.pData[iIndex];
        const usize iWordIndex = CharacterSet_GetWordIndex( chValue );
        const u64 nBitMask = CharacterSet_GetBitMask( chValue );
        pSet->bitWords[iWordIndex] |= nBitMask;
    }
}

void CharacterSet_RemoveView( character_set_t *pSet, string_view_t view ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    CY_ASSERT_MSG( pSet != nullptr,
                   "CharacterSet_RemoveView requires a valid set." );
    CY_ASSERT_MSG( bValidView,
                   "CharacterSet_RemoveView requires a valid string view." );

    if ( pSet == nullptr || !bValidView ) {
        return;
    }

    for ( usize iIndex = 0u; iIndex < view.cchLength; ++iIndex ) {
        const char chValue = view.pData[iIndex];
        const usize iWordIndex = CharacterSet_GetWordIndex( chValue );
        const u64 nBitMask = CharacterSet_GetBitMask( chValue );
        pSet->bitWords[iWordIndex] &= ~nBitMask;
    }
}

bool_t CharacterSet_ContainsAny(
    const character_set_t *pSet,
    string_view_t view ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    CY_ASSERT_MSG( pSet != nullptr,
                   "CharacterSet_ContainsAny requires a valid set." );
    CY_ASSERT_MSG( bValidView,
                   "CharacterSet_ContainsAny requires a valid string view." );

    if ( pSet == nullptr || !bValidView ) {
        return CY_FALSE;
    }

    for ( usize iIndex = 0u; iIndex < view.cchLength; ++iIndex ) {
        const char chValue = view.pData[iIndex];
        const usize iWordIndex = CharacterSet_GetWordIndex( chValue );
        const u64 nBitMask = CharacterSet_GetBitMask( chValue );

        if ( ( pSet->bitWords[iWordIndex] & nBitMask ) != 0u ) {
            return CY_TRUE;
        }
    }

    return CY_FALSE;
}

bool_t CharacterSet_ContainsAll(
    const character_set_t *pSet,
    string_view_t view ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    CY_ASSERT_MSG( pSet != nullptr,
                   "CharacterSet_ContainsAll requires a valid set." );
    CY_ASSERT_MSG( bValidView,
                   "CharacterSet_ContainsAll requires a valid string view." );

    if ( pSet == nullptr || !bValidView ) {
        return CY_FALSE;
    }

    for ( usize iIndex = 0u; iIndex < view.cchLength; ++iIndex ) {
        const char chValue = view.pData[iIndex];
        const usize iWordIndex = CharacterSet_GetWordIndex( chValue );
        const u64 nBitMask = CharacterSet_GetBitMask( chValue );

        if ( ( pSet->bitWords[iWordIndex] & nBitMask ) == 0u ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

void CharacterSet_Union(
    character_set_t *pOut,
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept
{
    CY_ASSERT_MSG( pOut != nullptr,
                   "CharacterSet_Union requires a valid output set." );
    CY_ASSERT_MSG( pSetA != nullptr,
                   "CharacterSet_Union requires a valid first input set." );
    CY_ASSERT_MSG( pSetB != nullptr,
                   "CharacterSet_Union requires a valid second input set." );

    if ( pOut == nullptr || pSetA == nullptr || pSetB == nullptr ) {
        return;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        const u64 nWordA = pSetA->bitWords[iWord];
        const u64 nWordB = pSetB->bitWords[iWord];
        pOut->bitWords[iWord] = nWordA | nWordB;
    }
}

void CharacterSet_Intersection(
    character_set_t *pOut,
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept
{
    CY_ASSERT_MSG( pOut != nullptr,
                   "CharacterSet_Intersection requires a valid output set." );
    CY_ASSERT_MSG( pSetA != nullptr,
                   "CharacterSet_Intersection requires a valid first input set." );
    CY_ASSERT_MSG( pSetB != nullptr,
                   "CharacterSet_Intersection requires a valid second input set." );

    if ( pOut == nullptr || pSetA == nullptr || pSetB == nullptr ) {
        return;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        const u64 nWordA = pSetA->bitWords[iWord];
        const u64 nWordB = pSetB->bitWords[iWord];
        pOut->bitWords[iWord] = nWordA & nWordB;
    }
}

void CharacterSet_Difference(
    character_set_t *pOut,
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept
{
    CY_ASSERT_MSG( pOut != nullptr,
                   "CharacterSet_Difference requires a valid output set." );
    CY_ASSERT_MSG( pSetA != nullptr,
                   "CharacterSet_Difference requires a valid first input set." );
    CY_ASSERT_MSG( pSetB != nullptr,
                   "CharacterSet_Difference requires a valid second input set." );

    if ( pOut == nullptr || pSetA == nullptr || pSetB == nullptr ) {
        return;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        const u64 nWordA = pSetA->bitWords[iWord];
        const u64 nWordB = pSetB->bitWords[iWord];
        pOut->bitWords[iWord] = nWordA & ~nWordB;
    }
}

void CharacterSet_Invert(
    character_set_t *pOut,
    const character_set_t *pInput ) noexcept
{
    CY_ASSERT_MSG( pOut != nullptr,
                   "CharacterSet_Invert requires a valid output set." );
    CY_ASSERT_MSG( pInput != nullptr,
                   "CharacterSet_Invert requires a valid input set." );

    if ( pOut == nullptr || pInput == nullptr ) {
        return;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        const u64 nInputWord = pInput->bitWords[iWord];
        pOut->bitWords[iWord] = ~nInputWord;
    }
}

bool_t CharacterSet_Intersects(
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept
{
    CY_ASSERT_MSG( pSetA != nullptr,
                   "CharacterSet_Intersects requires a valid first set." );
    CY_ASSERT_MSG( pSetB != nullptr,
                   "CharacterSet_Intersects requires a valid second set." );

    if ( pSetA == nullptr || pSetB == nullptr ) {
        return CY_FALSE;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        const u64 nCommonBits =
            pSetA->bitWords[iWord] & pSetB->bitWords[iWord];

        if ( nCommonBits != 0u ) {
            return CY_TRUE;
        }
    }

    return CY_FALSE;
}

bool_t CharacterSet_IsSubset(
    const character_set_t *pSubset,
    const character_set_t *pSuperset ) noexcept
{
    CY_ASSERT_MSG( pSubset != nullptr,
                   "CharacterSet_IsSubset requires a valid subset." );
    CY_ASSERT_MSG( pSuperset != nullptr,
                   "CharacterSet_IsSubset requires a valid superset." );

    if ( pSubset == nullptr || pSuperset == nullptr ) {
        return CY_FALSE;
    }

    for ( usize iWord = 0u; iWord < CY_CHARACTER_SET_WORD_COUNT; ++iWord ) {
        const u64 nSubsetWord = pSubset->bitWords[iWord];
        const u64 nSupersetWord = pSuperset->bitWords[iWord];
        const u64 nMissingBits = nSubsetWord & ~nSupersetWord;

        if ( nMissingBits != 0u ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

} // namespace cypher::common
