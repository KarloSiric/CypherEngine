//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Variant.h
//  Purpose: Declares a compact non-owning primitive variant.
//  Details: String and byte values are borrowed views. Variant performs no allocation
//           and is intended for commands, properties, diagnostics, and data bridges.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Variant Contract

This dependency-light Tier1 utility keeps ownership, capacity, and failure behavior explicit so
higher engine systems can use it without hidden allocation or platform state.
================
*/

#ifndef CYPHER_COMMON_TIER1_VARIANT_H
#define CYPHER_COMMON_TIER1_VARIANT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class variant_type_t : u8 {
    EMPTY = 0u, // No payload is active.
    BOOL,       // Boolean payload.
    I64,        // Signed integer payload.
    U64,        // Unsigned integer payload.
    F64,        // Double-precision payload.
    STRING_VIEW, // Borrowed character range.
    BYTE_VIEW,   // Borrowed arbitrary byte range.
    POINTER      // Opaque non-owning pointer.
};

struct variant_t {
    variant_type_t type{ variant_type_t::EMPTY }; // Selects the active union member.
    union {
        bool_t bValue; // BOOL payload.
        i64 iValue;    // I64 payload.
        u64 uValue;    // U64 payload.
        f64 flValue;   // F64 payload.
        struct {
            const char *pData; // Borrowed UTF-8 bytes.
            usize cchLength;   // Character bytes; no terminator required.
        } stringValue;         // STRING_VIEW payload.
        struct {
            const byte *pData; // Borrowed arbitrary bytes.
            usize cbSize;      // Number of bytes in the view.
        } byteValue;           // BYTE_VIEW payload.
        void *pValue;          // POINTER payload.
    } data{};                  // Storage selected by type.
};

CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_Empty() noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromBool( bool_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromI64( i64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromU64( u64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromF64( f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromString( string_view_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromBytes( const_byte_span_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromPointer( void *pValue ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_IsValid( variant_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_IsEmpty( variant_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_type_t Variant_Type( variant_t value ) noexcept;
CYPHER_COMMON_API void Variant_Reset( variant_t *pValue ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_GetBool( variant_t value, bool_t *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_GetI64( variant_t value, i64 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_GetU64( variant_t value, u64 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_GetF64( variant_t value, f64 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_GetString( variant_t value, string_view_t *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_GetBytes( variant_t value, const_byte_span_t *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_GetPointer( variant_t value, void **ppOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Variant_Equals( variant_t left, variant_t right ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_VARIANT_H
