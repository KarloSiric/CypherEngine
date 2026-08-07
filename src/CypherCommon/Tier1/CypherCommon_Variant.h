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
    EMPTY = 0u,
    BOOL,
    I64,
    U64,
    F64,
    STRING_VIEW,
    BYTE_VIEW,
    POINTER
};

struct variant_t {
    variant_type_t type{ variant_type_t::EMPTY };
    union {
        bool_t bValue;
        i64 iValue;
        u64 uValue;
        f64 flValue;
        struct {
            const char *pData;
            usize cchLength;
        } stringValue;
        struct {
            const byte *pData;
            usize cbSize;
        } byteValue;
        void *pValue;
    } data{};
};

CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_Empty() noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromBool( bool_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromI64( i64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromU64( u64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromF64( f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromString( string_view_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromBytes( const_byte_span_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API variant_t Variant_FromPointer( void *pValue ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Variant_IsEmpty( variant_t value ) noexcept;
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
