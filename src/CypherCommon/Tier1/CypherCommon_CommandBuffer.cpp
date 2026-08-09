//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandBuffer.cpp
//  Purpose: Implements an allocator-backed FIFO buffer of command lines.
//  Details: Newline delimiters make command boundaries explicit. Capacity is secured
//           before mutation, and aliased source views are rebased across relocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandBuffer.h"

#include <limits>

namespace cypher::common
{

namespace
{

bool_t CommandBuffer_IsCanonicalEmpty(
    const command_buffer_t &buffer ) noexcept
{
    return buffer.text.pData == nullptr &&
           buffer.text.cchLength == 0u &&
           buffer.text.cchCapacity == 0u &&
           buffer.text.pAllocator == nullptr &&
           buffer.iReadOffset == 0u &&
           buffer.nCommandCount == 0u;
}

bool_t CommandBuffer_IsLineValid( string_view_t commandLine ) noexcept
{
    if ( !StringView_IsValid( commandLine ) || commandLine.cchLength == 0u ) {
        return CY_FALSE;
    }
    for ( usize iCharacter = 0u;
          iCharacter < commandLine.cchLength;
          ++iCharacter ) {
        const char ch = commandLine.pData[iCharacter];
        if ( ch == '\0' || ch == '\r' || ch == '\n' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t CommandBuffer_RebaseSource(
    const command_buffer_t &buffer,
    string_view_t source,
    bool_t &bInternalOut,
    usize &iSourceOffsetOut ) noexcept
{
    bInternalOut = CY_FALSE;
    iSourceOffsetOut = 0u;
    if ( source.cchLength == 0u || buffer.text.pData == nullptr ) {
        return CY_TRUE;
    }

    constexpr uintptr nMaximumAddress =
        std::numeric_limits<uintptr>::max();
    const uintptr nStorageBegin =
        reinterpret_cast<uintptr>( buffer.text.pData );
    const uintptr nSourceBegin = reinterpret_cast<uintptr>( source.pData );
    const usize cbStorage = buffer.text.cchCapacity + 1u;
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
        nSourceEnd <= nStorageBegin + buffer.text.cchLength;
    CY_ASSERT_MSG(
        bInsideLogicalText,
        "CommandBuffer source may only alias logical text bytes." );
    if ( !bInsideLogicalText ) {
        return CY_FALSE;
    }

    bInternalOut = CY_TRUE;
    iSourceOffsetOut = nSourceBegin - nStorageBegin;
    return CY_TRUE;
}

bool_t CommandBuffer_FindPendingLine(
    const command_buffer_t &buffer,
    string_view_t &commandOut ) noexcept
{
    if ( buffer.nCommandCount == 0u ) {
        commandOut = {};
        return CY_FALSE;
    }

    const char *pData = buffer.text.pData;
    for ( usize iEnd = buffer.iReadOffset;
          iEnd < buffer.text.cchLength;
          ++iEnd ) {
        if ( pData[iEnd] == '\n' ) {
            commandOut = {
                pData + buffer.iReadOffset,
                iEnd - buffer.iReadOffset
            };
            return CY_TRUE;
        }
    }

    CY_ASSERT_MSG(
        CY_FALSE,
        "CommandBuffer pending command is missing its delimiter." );
    commandOut = {};
    return CY_FALSE;
}

} // namespace

bool_t CommandBuffer_Init(
    command_buffer_t *pBuffer,
    const allocator_t *pAllocator,
    usize cchInitialCapacity ) noexcept
{
    const bool_t bValidDestination =
        pBuffer != nullptr && CommandBuffer_IsCanonicalEmpty( *pBuffer );
    CY_ASSERT_MSG(
        bValidDestination,
        "CommandBuffer_Init requires a canonical empty destination." );
    if ( !bValidDestination ) {
        return CY_FALSE;
    }

    return TextBuffer_Init(
        &pBuffer->text,
        pAllocator,
        cchInitialCapacity );
}

void CommandBuffer_Shutdown( command_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = CommandBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "CommandBuffer_Shutdown requires a valid buffer." );
    if ( !bValidBuffer ) {
        return;
    }

    TextBuffer_Shutdown( &pBuffer->text );
    pBuffer->iReadOffset = 0u;
    pBuffer->nCommandCount = 0u;
}

void CommandBuffer_Clear( command_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = CommandBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "CommandBuffer_Clear requires a valid buffer." );
    if ( !bValidBuffer ) {
        return;
    }

    TextBuffer_Clear( &pBuffer->text );
    pBuffer->iReadOffset = 0u;
    pBuffer->nCommandCount = 0u;
}

bool_t CommandBuffer_IsValid( const command_buffer_t *pBuffer ) noexcept
{
    if ( pBuffer == nullptr || !TextBuffer_IsValid( &pBuffer->text ) ||
         pBuffer->iReadOffset > pBuffer->text.cchLength ) {
        return CY_FALSE;
    }
    if ( pBuffer->text.pAllocator == nullptr ) {
        return CommandBuffer_IsCanonicalEmpty( *pBuffer );
    }
    if ( pBuffer->nCommandCount == 0u ) {
        return pBuffer->iReadOffset == pBuffer->text.cchLength;
    }
    return pBuffer->text.pData != nullptr &&
           pBuffer->iReadOffset < pBuffer->text.cchLength &&
           pBuffer->text.pData[pBuffer->iReadOffset] != '\n' &&
           pBuffer->text.pData[pBuffer->text.cchLength - 1u] == '\n' &&
           ( pBuffer->iReadOffset == 0u ||
             pBuffer->text.pData[pBuffer->iReadOffset - 1u] == '\n' );
}

bool_t CommandBuffer_IsEmpty( const command_buffer_t *pBuffer ) noexcept
{
    return CommandBuffer_Count( pBuffer ) == 0u;
}

usize CommandBuffer_Count( const command_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = CommandBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "CommandBuffer_Count requires a valid buffer." );
    return bValidBuffer ? pBuffer->nCommandCount : 0u;
}

usize CommandBuffer_PendingBytes( const command_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = CommandBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "CommandBuffer_PendingBytes requires a valid buffer." );
    return bValidBuffer
        ? pBuffer->text.cchLength - pBuffer->iReadOffset
        : 0u;
}

bool_t CommandBuffer_Enqueue(
    command_buffer_t *pBuffer,
    string_view_t commandLine ) noexcept
{
    const bool_t bValidBuffer = CommandBuffer_IsValid( pBuffer );
    const bool_t bAllocatorBound =
        bValidBuffer && Allocator_IsValid( pBuffer->text.pAllocator );
    const bool_t bValidLine = CommandBuffer_IsLineValid( commandLine );
    const bool_t bCountAvailable =
        bValidBuffer && pBuffer->nCommandCount != CY_INVALID_SIZE;
    CY_ASSERT_MSG(
        bValidBuffer,
        "CommandBuffer_Enqueue requires a valid buffer." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "CommandBuffer_Enqueue requires an initialized allocator binding." );
    CY_ASSERT_MSG(
        bValidLine,
        "CommandBuffer_Enqueue requires one non-empty command line." );
    CY_ASSERT_MSG(
        bCountAvailable,
        "CommandBuffer command count overflowed." );
    if ( !bValidBuffer || !bAllocatorBound ||
         !bValidLine || !bCountAvailable ) {
        return CY_FALSE;
    }

    const usize cchOldLength = pBuffer->text.cchLength;
    if ( commandLine.cchLength > CY_INVALID_SIZE - cchOldLength - 1u ) {
        CY_ASSERT_MSG( CY_FALSE, "CommandBuffer text length overflowed." );
        return CY_FALSE;
    }

    bool_t bInternalSource = CY_FALSE;
    usize iSourceOffset = 0u;
    if ( !CommandBuffer_RebaseSource(
             *pBuffer,
             commandLine,
             bInternalSource,
             iSourceOffset ) ) {
        return CY_FALSE;
    }

    const usize cchRequired =
        cchOldLength + commandLine.cchLength + 1u;
    if ( !TextBuffer_Reserve( &pBuffer->text, cchRequired ) ) {
        return CY_FALSE;
    }
    if ( bInternalSource ) {
        commandLine.pData = pBuffer->text.pData + iSourceOffset;
    }

    const bool_t bAppended =
        TextBuffer_Append( &pBuffer->text, commandLine ) &&
        TextBuffer_AppendChar( &pBuffer->text, '\n' );
    CY_ASSERT_MSG(
        bAppended,
        "CommandBuffer append failed after capacity was secured." );
    if ( !bAppended ) {
        static_cast<void>(
            TextBuffer_Resize( &pBuffer->text, cchOldLength ) );
        return CY_FALSE;
    }

    ++pBuffer->nCommandCount;
    return CY_TRUE;
}

bool_t CommandBuffer_Peek(
    const command_buffer_t *pBuffer,
    string_view_t *pCommandOut ) noexcept
{
    if ( pCommandOut != nullptr ) {
        *pCommandOut = {};
}
    const bool_t bValidBuffer = CommandBuffer_IsValid( pBuffer );
    const bool_t bValidOutput = pCommandOut != nullptr;
    CY_ASSERT_MSG(
        bValidBuffer,
        "CommandBuffer_Peek requires a valid buffer." );
    CY_ASSERT_MSG(
        bValidOutput,
        "CommandBuffer_Peek requires an output view." );
    return bValidBuffer && bValidOutput &&
           CommandBuffer_FindPendingLine( *pBuffer, *pCommandOut );
}

bool_t CommandBuffer_Pop(
    command_buffer_t *pBuffer,
    string_view_t *pCommandOut ) noexcept
{
    if ( !CommandBuffer_Peek( pBuffer, pCommandOut ) ) {
        return CY_FALSE;
    }

    pBuffer->iReadOffset += pCommandOut->cchLength + 1u;
    --pBuffer->nCommandCount;
    return CY_TRUE;
}

void CommandBuffer_Compact( command_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = CommandBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "CommandBuffer_Compact requires a valid buffer." );
    if ( !bValidBuffer || pBuffer->iReadOffset == 0u ) {
        return;
    }
    
    if ( pBuffer->nCommandCount == 0u ) {
        TextBuffer_Clear( &pBuffer->text );
        pBuffer->iReadOffset = 0u;
        return;
    }

    const bool_t bErased = TextBuffer_Erase(
        &pBuffer->text,
        0u,
        pBuffer->iReadOffset );
    CY_ASSERT_MSG(
        bErased,
        "CommandBuffer compaction failed for a valid consumed prefix." );
    if ( bErased ) {
        pBuffer->iReadOffset = 0u;
    }
}

} // namespace cypher::common
