//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_TextBuffer.h
//  Purpose: Declares an allocator-backed mutable UTF-8 byte buffer.
//  Details: TextBuffer owns null-terminated storage but treats text as bytes; Unicode
//           validation and code-point operations remain explicit Unicode API calls.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_TEXTBUFFER_H
#define CYPHER_COMMON_TIER1_TEXTBUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct text_buffer_t {
    text_buffer_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( text_buffer_t );
    ~text_buffer_t() noexcept;

    char *pData{ nullptr };
    usize cchLength{ 0u };
    usize cchCapacity{ 0u };
    const allocator_t *pAllocator{ nullptr };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_Init(
    text_buffer_t *pBuffer,
    const allocator_t *pAllocator,
    usize cchInitialCapacity = 0u ) noexcept;

CYPHER_COMMON_API void TextBuffer_Shutdown( text_buffer_t *pBuffer ) noexcept;

CYPHER_COMMON_API void TextBuffer_Clear( text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_IsValid( const text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_IsEmpty( const text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
char *TextBuffer_Data( text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const char *TextBuffer_Data( const text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize TextBuffer_Length( const text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize TextBuffer_Capacity( const text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t TextBuffer_View( const text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *TextBuffer_CStr( const text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_Reserve(
    text_buffer_t *pBuffer,
    usize cchCapacity ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_ShrinkToFit( text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_Resize(
    text_buffer_t *pBuffer,
    usize cchLength,
    char chFill = '\0' ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_Assign(
    text_buffer_t *pBuffer,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_Append(
    text_buffer_t *pBuffer,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_AppendChar(
    text_buffer_t *pBuffer,
    char ch ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_PopBack( text_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_Insert(
    text_buffer_t *pBuffer,
    usize iPosition,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_Erase(
    text_buffer_t *pBuffer,
    usize iPosition,
    usize cchCount ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TextBuffer_Replace(
    text_buffer_t *pBuffer,
    usize iPosition,
    usize cchCount,
    string_view_t replacement ) noexcept;

CYPHER_COMMON_API void TextBuffer_Move(
    text_buffer_t *pDest,
    text_buffer_t *pSource ) noexcept;

// Transfers allocation ownership to the caller and resets pBuffer.
CYPHER_NODISCARD CYPHER_COMMON_API
owned_allocation_t TextBuffer_Release(
    text_buffer_t *pBuffer,
    usize *pcchLengthOut = nullptr ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_TEXTBUFFER_H
