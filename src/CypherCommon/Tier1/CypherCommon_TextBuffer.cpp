//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_TextBuffer.cpp
//  Purpose: Implements allocator-backed mutable UTF-8 byte storage.
//  Details: Every live allocation reserves a trailing terminator byte. Growth is
//           transactional, aliases survive reallocation, and ownership release
//           preserves the allocator metadata required for correct deallocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TextBuffer.h"

#include <limits>

namespace cypher::common
{

namespace
{

constexpr usize CY_TEXT_BUFFER_MAX_CAPACITY = CY_USIZE_MAX - 1u;

bool_t TextBufferIsCanonicalEmpty( const text_buffer_t &buffer ) noexcept
{
    return buffer.pData == nullptr &&
           buffer.cchLength == 0u &&
           buffer.cchCapacity == 0u &&
           buffer.pAllocator == nullptr;
}

void ResetTextBuffer( text_buffer_t &buffer ) noexcept
{
    buffer.pData = nullptr;
    buffer.cchLength = 0u;
    buffer.cchCapacity = 0u;
    buffer.pAllocator = nullptr;
}

bool_t CalculateTextBufferGrowth(
    usize cchCurrentCapacity,
    usize cchRequiredCapacity,
    usize &cchCapacityOut ) noexcept
{
    if ( cchRequiredCapacity > CY_TEXT_BUFFER_MAX_CAPACITY ) {
        return CY_FALSE;
    }

    constexpr usize cchMinimumCapacity = 64u;
    usize cchCandidate = cchCurrentCapacity < cchMinimumCapacity
        ? cchMinimumCapacity
        : cchCurrentCapacity;
    if ( cchCandidate < cchRequiredCapacity ) {
        const usize cchHalf = cchCandidate / 2u;
        cchCandidate = cchCandidate > CY_TEXT_BUFFER_MAX_CAPACITY - cchHalf
            ? CY_TEXT_BUFFER_MAX_CAPACITY
            : cchCandidate + cchHalf;
    }
    if ( cchCandidate < cchRequiredCapacity ) {
        cchCandidate = cchRequiredCapacity;
    }

    cchCapacityOut = cchCandidate;
    return cchCandidate >= cchRequiredCapacity;
}

bool_t EnsureTextBufferCapacity(
    text_buffer_t *pBuffer,
    usize cchRequiredCapacity ) noexcept
{
    if ( cchRequiredCapacity <= pBuffer->cchCapacity ) {
        return CY_TRUE;
    }

    usize cchGrowthCapacity = 0u;
    const bool_t bValidGrowth = CalculateTextBufferGrowth(
        pBuffer->cchCapacity,
        cchRequiredCapacity,
        cchGrowthCapacity );
    CY_ASSERT_MSG(
        bValidGrowth,
        "TextBuffer growth exceeds addressable storage." );
    return bValidGrowth && TextBuffer_Reserve( pBuffer, cchGrowthCapacity );
}

bool_t RebaseTextBufferSource(
    const text_buffer_t &buffer,
    string_view_t source,
    bool_t &bInternalOut,
    usize &iSourceOffsetOut ) noexcept
{
    bInternalOut = CY_FALSE;
    iSourceOffsetOut = 0u;
    if ( source.cchLength == 0u || buffer.pData == nullptr ) {
        return CY_TRUE;
    }

    constexpr uintptr nMaximumAddress =
        std::numeric_limits<uintptr>::max();
    const uintptr nStorageBegin = reinterpret_cast<uintptr>( buffer.pData );
    const uintptr nSourceBegin = reinterpret_cast<uintptr>( source.pData );
    const usize cbStorage = buffer.cchCapacity + 1u;
    if ( nStorageBegin > nMaximumAddress - cbStorage ||
         nSourceBegin > nMaximumAddress - source.cchLength ) {
        return CY_FALSE;
    }

    const uintptr nStorageEnd = nStorageBegin + cbStorage;
    const uintptr nSourceEnd = nSourceBegin + source.cchLength;
    const bool_t bOverlapsStorage =
        nSourceBegin < nStorageEnd && nStorageBegin < nSourceEnd;
    if ( !bOverlapsStorage ) {
        return CY_TRUE;
    }

    const bool_t bInsideLogicalText =
        nSourceBegin >= nStorageBegin &&
        nSourceEnd <= nStorageBegin + buffer.cchLength;
    CY_ASSERT_MSG(
        bInsideLogicalText,
        "TextBuffer source may only overlap logical bytes owned by the buffer." );
    if ( !bInsideLogicalText ) {
        return CY_FALSE;
    }

    bInternalOut = CY_TRUE;
    iSourceOffsetOut = nSourceBegin - nStorageBegin;
    return CY_TRUE;
}

} // namespace

text_buffer_t::~text_buffer_t() noexcept
{
    TextBuffer_Shutdown( this );
}

bool_t TextBuffer_Init(
    text_buffer_t *pBuffer,
    const allocator_t *pAllocator,
    usize cchInitialCapacity ) noexcept
{
    const bool_t bValidDestination =
        pBuffer != nullptr && TextBufferIsCanonicalEmpty( *pBuffer );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    const bool_t bValidCapacity =
        cchInitialCapacity <= CY_TEXT_BUFFER_MAX_CAPACITY;
    CY_ASSERT_MSG(
        bValidDestination,
        "TextBuffer_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "TextBuffer_Init requires a valid allocator." );
    CY_ASSERT_MSG(
        bValidCapacity,
        "TextBuffer_Init capacity exceeds addressable storage." );
    if ( !bValidDestination || !bValidAllocator || !bValidCapacity ) {
        return CY_FALSE;
    }

    if ( cchInitialCapacity == 0u ) {
        pBuffer->pAllocator = pAllocator;
        return CY_TRUE;
    }

    char *pData = static_cast<char *>( Allocator_Allocate(
        pAllocator,
        cchInitialCapacity + 1u,
        alignof( char ) ) );
    if ( pData == nullptr ) {
        return CY_FALSE;
    }

    pData[0] = '\0';
    pBuffer->pData = pData;
    pBuffer->cchCapacity = cchInitialCapacity;
    pBuffer->pAllocator = pAllocator;
    return CY_TRUE;
}

void TextBuffer_Shutdown( text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Shutdown requires a valid buffer." );
    if ( !bValidBuffer ) {
        return;
    }

    if ( pBuffer->pData != nullptr ) {
        Allocator_Free(
            pBuffer->pAllocator,
            pBuffer->pData,
            pBuffer->cchCapacity + 1u,
            alignof( char ) );
    }
    ResetTextBuffer( *pBuffer );
}

void TextBuffer_Clear( text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Clear requires a valid buffer." );
    if ( !bValidBuffer ) {
        return;
    }

    pBuffer->cchLength = 0u;
    if ( pBuffer->pData != nullptr ) {
        pBuffer->pData[0] = '\0';
    }
}

bool_t TextBuffer_IsValid( const text_buffer_t *pBuffer ) noexcept
{
    if ( pBuffer == nullptr ) {
        return CY_FALSE;
    }
    if ( pBuffer->pData == nullptr ) {
        return pBuffer->cchLength == 0u &&
               pBuffer->cchCapacity == 0u &&
               ( pBuffer->pAllocator == nullptr ||
                 Allocator_IsValid( pBuffer->pAllocator ) );
    }

    return pBuffer->cchCapacity > 0u &&
           pBuffer->cchCapacity <= CY_TEXT_BUFFER_MAX_CAPACITY &&
           pBuffer->cchLength <= pBuffer->cchCapacity &&
           pBuffer->pData[pBuffer->cchLength] == '\0' &&
           Allocator_IsValid( pBuffer->pAllocator );
}

bool_t TextBuffer_IsEmpty( const text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_IsEmpty requires a valid buffer." );
    return bValidBuffer ? pBuffer->cchLength == 0u : CY_TRUE;
}

char *TextBuffer_Data( text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Data requires a valid buffer." );
    return bValidBuffer ? pBuffer->pData : nullptr;
}

const char *TextBuffer_Data( const text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Data requires a valid buffer." );
    return bValidBuffer ? pBuffer->pData : nullptr;
}

usize TextBuffer_Length( const text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Length requires a valid buffer." );
    return bValidBuffer ? pBuffer->cchLength : 0u;
}

usize TextBuffer_Capacity( const text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Capacity requires a valid buffer." );
    return bValidBuffer ? pBuffer->cchCapacity : 0u;
}

string_view_t TextBuffer_View( const text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_View requires a valid buffer." );
    return bValidBuffer
        ? string_view_t{ pBuffer->pData, pBuffer->cchLength }
        : string_view_t{};
}

const char *TextBuffer_CStr( const text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_CStr requires a valid buffer." );
    return bValidBuffer && pBuffer->pData != nullptr ? pBuffer->pData : "";
}

bool_t TextBuffer_Reserve(
    text_buffer_t *pBuffer,
    usize cchCapacity ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    const bool_t bAllocatorBound =
        bValidBuffer && Allocator_IsValid( pBuffer->pAllocator );
    const bool_t bValidCapacity = cchCapacity <= CY_TEXT_BUFFER_MAX_CAPACITY;
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Reserve requires a valid buffer." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "TextBuffer_Reserve requires an initialized allocator binding." );
    CY_ASSERT_MSG(
        bValidCapacity,
        "TextBuffer_Reserve capacity exceeds addressable storage." );
    if ( !bValidBuffer || !bAllocatorBound || !bValidCapacity ) {
        return CY_FALSE;
    }
    if ( cchCapacity <= pBuffer->cchCapacity ) {
        return CY_TRUE;
    }

    const usize cbOldAllocation = pBuffer->pData != nullptr
        ? pBuffer->cchCapacity + 1u
        : 0u;
    void *pNewData = Allocator_Reallocate(
        pBuffer->pAllocator,
        pBuffer->pData,
        cbOldAllocation,
        cchCapacity + 1u,
        alignof( char ) );
    if ( pNewData == nullptr ) {
        return CY_FALSE;
    }

    pBuffer->pData = static_cast<char *>( pNewData );
    pBuffer->cchCapacity = cchCapacity;
    pBuffer->pData[pBuffer->cchLength] = '\0';
    return CY_TRUE;
}

bool_t TextBuffer_ShrinkToFit( text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    const bool_t bAllocatorBound =
        bValidBuffer && Allocator_IsValid( pBuffer->pAllocator );
    CY_ASSERT_MSG(
        bValidBuffer,
        "TextBuffer_ShrinkToFit requires a valid buffer." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "TextBuffer_ShrinkToFit requires an initialized allocator binding." );
    if ( !bValidBuffer || !bAllocatorBound ) {
        return CY_FALSE;
    }
    if ( pBuffer->cchLength == pBuffer->cchCapacity ) {
        return CY_TRUE;
    }
    if ( pBuffer->cchLength == 0u ) {
        if ( pBuffer->pData != nullptr ) {
            Allocator_Free(
                pBuffer->pAllocator,
                pBuffer->pData,
                pBuffer->cchCapacity + 1u,
                alignof( char ) );
            pBuffer->pData = nullptr;
            pBuffer->cchCapacity = 0u;
        }
        return CY_TRUE;
    }

    void *pNewData = Allocator_Reallocate(
        pBuffer->pAllocator,
        pBuffer->pData,
        pBuffer->cchCapacity + 1u,
        pBuffer->cchLength + 1u,
        alignof( char ) );
    if ( pNewData == nullptr ) {
        return CY_FALSE;
    }

    pBuffer->pData = static_cast<char *>( pNewData );
    pBuffer->cchCapacity = pBuffer->cchLength;
    return CY_TRUE;
}

bool_t TextBuffer_Resize(
    text_buffer_t *pBuffer,
    usize cchLength,
    char chFill ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    const bool_t bValidLength = cchLength <= CY_TEXT_BUFFER_MAX_CAPACITY;
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Resize requires a valid buffer." );
    CY_ASSERT_MSG(
        bValidLength,
        "TextBuffer_Resize length exceeds addressable storage." );
    if ( !bValidBuffer || !bValidLength ||
         !Allocator_IsValid( pBuffer->pAllocator ) ) {
        return CY_FALSE;
    }

    const usize cchOldLength = pBuffer->cchLength;
    if ( cchLength > cchOldLength ) {
        if ( !EnsureTextBufferCapacity( pBuffer, cchLength ) ) {
            return CY_FALSE;
        }
        Cy_MemSet(
            pBuffer->pData + cchOldLength,
            static_cast<byte>( chFill ),
            cchLength - cchOldLength );
    }

    pBuffer->cchLength = cchLength;
    if ( pBuffer->pData != nullptr ) {
        pBuffer->pData[cchLength] = '\0';
    }
    return CY_TRUE;
}

bool_t TextBuffer_Assign(
    text_buffer_t *pBuffer,
    string_view_t text ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Assign requires a valid buffer." );
    CY_ASSERT_MSG( bValidText, "TextBuffer_Assign requires a valid text view." );
    if ( !bValidBuffer || !bValidText ||
         !Allocator_IsValid( pBuffer->pAllocator ) ) {
        return CY_FALSE;
    }

    bool_t bInternalSource = CY_FALSE;
    usize iSourceOffset = 0u;
    if ( !RebaseTextBufferSource(
             *pBuffer,
             text,
             bInternalSource,
             iSourceOffset ) ||
         !EnsureTextBufferCapacity( pBuffer, text.cchLength ) ) {
        return CY_FALSE;
    }

    const char *pSource = bInternalSource
        ? pBuffer->pData + iSourceOffset
        : text.pData;
    if ( text.cchLength > 0u ) {
        Cy_MemMove( pBuffer->pData, pSource, text.cchLength );
    }
    pBuffer->cchLength = text.cchLength;
    if ( pBuffer->pData != nullptr ) {
        pBuffer->pData[pBuffer->cchLength] = '\0';
    }
    return CY_TRUE;
}

bool_t TextBuffer_Append(
    text_buffer_t *pBuffer,
    string_view_t text ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Append requires a valid buffer." );
    CY_ASSERT_MSG( bValidText, "TextBuffer_Append requires a valid text view." );
    if ( !bValidBuffer || !bValidText ||
         !Allocator_IsValid( pBuffer->pAllocator ) ) {
        return CY_FALSE;
    }
    if ( text.cchLength == 0u ) {
        return CY_TRUE;
    }
    if ( text.cchLength > CY_TEXT_BUFFER_MAX_CAPACITY - pBuffer->cchLength ) {
        CY_ASSERT_MSG( CY_FALSE, "TextBuffer append length overflowed." );
        return CY_FALSE;
    }

    bool_t bInternalSource = CY_FALSE;
    usize iSourceOffset = 0u;
    if ( !RebaseTextBufferSource(
             *pBuffer,
             text,
             bInternalSource,
             iSourceOffset ) ) {
        return CY_FALSE;
    }

    const usize cchNewLength = pBuffer->cchLength + text.cchLength;
    if ( !EnsureTextBufferCapacity( pBuffer, cchNewLength ) ) {
        return CY_FALSE;
    }

    const char *pSource = bInternalSource
        ? pBuffer->pData + iSourceOffset
        : text.pData;
    Cy_MemMove(
        pBuffer->pData + pBuffer->cchLength,
        pSource,
        text.cchLength );
    pBuffer->cchLength = cchNewLength;
    pBuffer->pData[pBuffer->cchLength] = '\0';
    return CY_TRUE;
}

bool_t TextBuffer_AppendChar(
    text_buffer_t *pBuffer,
    char ch ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "TextBuffer_AppendChar requires a valid buffer." );
    if ( !bValidBuffer || !Allocator_IsValid( pBuffer->pAllocator ) ||
         pBuffer->cchLength == CY_TEXT_BUFFER_MAX_CAPACITY ) {
        return CY_FALSE;
    }

    if ( !EnsureTextBufferCapacity( pBuffer, pBuffer->cchLength + 1u ) ) {
        return CY_FALSE;
    }
    pBuffer->pData[pBuffer->cchLength] = ch;
    ++pBuffer->cchLength;
    pBuffer->pData[pBuffer->cchLength] = '\0';
    return CY_TRUE;
}

bool_t TextBuffer_PopBack( text_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_PopBack requires a valid buffer." );
    if ( !bValidBuffer || pBuffer->cchLength == 0u ) {
        return CY_FALSE;
    }

    --pBuffer->cchLength;
    pBuffer->pData[pBuffer->cchLength] = '\0';
    return CY_TRUE;
}

bool_t TextBuffer_Insert(
    text_buffer_t *pBuffer,
    usize iPosition,
    string_view_t text ) noexcept
{
    return TextBuffer_Replace( pBuffer, iPosition, 0u, text );
}

bool_t TextBuffer_Erase(
    text_buffer_t *pBuffer,
    usize iPosition,
    usize cchCount ) noexcept
{
    return TextBuffer_Replace( pBuffer, iPosition, cchCount, {} );
}

bool_t TextBuffer_Replace(
    text_buffer_t *pBuffer,
    usize iPosition,
    usize cchCount,
    string_view_t replacement ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    const bool_t bValidReplacement = StringView_IsValid( replacement );
    const bool_t bValidPosition =
        bValidBuffer && iPosition <= pBuffer->cchLength;
    const bool_t bValidCount =
        bValidPosition && cchCount <= pBuffer->cchLength - iPosition;
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Replace requires a valid buffer." );
    CY_ASSERT_MSG(
        bValidReplacement,
        "TextBuffer_Replace requires a valid replacement view." );
    CY_ASSERT_MSG(
        bValidPosition,
        "TextBuffer_Replace position is outside the text range." );
    CY_ASSERT_MSG(
        bValidCount,
        "TextBuffer_Replace count exceeds the remaining text." );
    if ( !bValidBuffer || !bValidReplacement ||
         !bValidPosition || !bValidCount ||
         !Allocator_IsValid( pBuffer->pAllocator ) ) {
        return CY_FALSE;
    }

    const usize cchRetained = pBuffer->cchLength - cchCount;
    if ( replacement.cchLength >
         CY_TEXT_BUFFER_MAX_CAPACITY - cchRetained ) {
        CY_ASSERT_MSG( CY_FALSE, "TextBuffer replacement length overflowed." );
        return CY_FALSE;
    }

    bool_t bInternalSource = CY_FALSE;
    usize iSourceOffset = 0u;
    if ( !RebaseTextBufferSource(
             *pBuffer,
             replacement,
             bInternalSource,
             iSourceOffset ) ) {
        return CY_FALSE;
    }

    char *pTemporary = nullptr;
    const char *pReplacement = replacement.pData;
    if ( bInternalSource && replacement.cchLength > 0u ) {
        pTemporary = static_cast<char *>( Allocator_Allocate(
            pBuffer->pAllocator,
            replacement.cchLength,
            alignof( char ) ) );
        if ( pTemporary == nullptr ) {
            return CY_FALSE;
        }
        Cy_MemMove(
            pTemporary,
            pBuffer->pData + iSourceOffset,
            replacement.cchLength );
        pReplacement = pTemporary;
    }

    const usize cchNewLength = cchRetained + replacement.cchLength;
    if ( !EnsureTextBufferCapacity( pBuffer, cchNewLength ) ) {
        Allocator_Free(
            pBuffer->pAllocator,
            pTemporary,
            replacement.cchLength,
            alignof( char ) );
        return CY_FALSE;
    }

    const usize iTail = iPosition + cchCount;
    const usize cchTail = pBuffer->cchLength - iTail;
    if ( cchTail > 0u && replacement.cchLength != cchCount ) {
        Cy_MemMove(
            pBuffer->pData + iPosition + replacement.cchLength,
            pBuffer->pData + iTail,
            cchTail );
    }
    if ( replacement.cchLength > 0u ) {
        Cy_MemMove(
            pBuffer->pData + iPosition,
            pReplacement,
            replacement.cchLength );
    }

    pBuffer->cchLength = cchNewLength;
    if ( pBuffer->pData != nullptr ) {
        pBuffer->pData[cchNewLength] = '\0';
    }
    Allocator_Free(
        pBuffer->pAllocator,
        pTemporary,
        replacement.cchLength,
        alignof( char ) );
    return CY_TRUE;
}

void TextBuffer_Move(
    text_buffer_t *pDest,
    text_buffer_t *pSource ) noexcept
{
    const bool_t bDistinctBuffers =
        pDest != nullptr && pSource != nullptr && pDest != pSource;
    const bool_t bDestinationEmpty =
        bDistinctBuffers && TextBufferIsCanonicalEmpty( *pDest );
    const bool_t bSourceValid =
        bDistinctBuffers && TextBuffer_IsValid( pSource );
    const bool_t bValidMove =
        bDistinctBuffers && bDestinationEmpty && bSourceValid;
    CY_ASSERT_MSG(
        bValidMove,
        "TextBuffer_Move requires distinct buffers and a canonical empty destination." );
    if ( !bValidMove ) {
        return;
    }

    pDest->pData = pSource->pData;
    pDest->cchLength = pSource->cchLength;
    pDest->cchCapacity = pSource->cchCapacity;
    pDest->pAllocator = pSource->pAllocator;
    ResetTextBuffer( *pSource );
}

owned_allocation_t TextBuffer_Release(
    text_buffer_t *pBuffer,
    usize *pcchLengthOut ) noexcept
{
    const bool_t bValidBuffer = TextBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "TextBuffer_Release requires a valid buffer." );
    if ( pcchLengthOut != nullptr ) {
        *pcchLengthOut = 0u;
    }
    if ( !bValidBuffer ) {
        return {};
    }

    owned_allocation_t allocation{};
    if ( pBuffer->pData != nullptr ) {
        const bool_t bAdopted = Allocator_AdoptOwned(
            &allocation,
            pBuffer->pAllocator,
            pBuffer->pData,
            pBuffer->cchCapacity + 1u,
            alignof( char ) );
        if ( !bAdopted ) {
            return {};
        }
    }

    if ( pcchLengthOut != nullptr ) {
        *pcchLengthOut = pBuffer->cchLength;
    }
    ResetTextBuffer( *pBuffer );
    return allocation;
}

} // namespace cypher::common
