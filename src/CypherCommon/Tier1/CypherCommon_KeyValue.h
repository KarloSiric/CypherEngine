//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValue.h
//  Purpose: Declares the owned hierarchical data model used by Cypher text formats.
//  Details: A document owns every node, key, string, and binary value through one
//           explicit allocator. Node pointers remain valid until removal or document clear.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_KEYVALUE_H
#define CYPHER_COMMON_TIER1_KEYVALUE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class key_value_type_t : u8 {
    NULL_VALUE = 0u,
    BOOL,
    I64,
    U64,
    F64,
    STRING,
    BINARY,
    OBJECT,
    ARRAY
};

struct key_value_t;
struct key_value_document_t;

struct key_value_document_desc_t {
    const allocator_t *pAllocator{ nullptr };
    usize nInitialNodes{ 128u };
    usize cbInitialStrings{ 8u * CY_KIB };
    bool_t bCaseInsensitiveKeys{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_document_t *KeyValue_CreateDocument(
    const key_value_document_desc_t &desc ) noexcept;

CYPHER_COMMON_API void KeyValue_DestroyDocument(
    key_value_document_t *pDocument ) noexcept;

CYPHER_COMMON_API void KeyValue_ClearDocument(
    key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_t *KeyValue_Root( key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const key_value_t *KeyValue_Root(
    const key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t KeyValue_SetRootType(
    key_value_document_t *pDocument,
    key_value_type_t type ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_type_t KeyValue_Type( const key_value_t *pValue ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t KeyValue_Name( const key_value_t *pValue ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_GetBool( const key_value_t *pValue, bool_t *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_GetI64( const key_value_t *pValue, i64 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_GetU64( const key_value_t *pValue, u64 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_GetF64( const key_value_t *pValue, f64 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_GetString( const key_value_t *pValue, string_view_t *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_GetBinary( const key_value_t *pValue, binary_block_t *pOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_SetNull( key_value_document_t *pDocument, key_value_t *pValue ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_SetBool( key_value_document_t *pDocument, key_value_t *pValue, bool_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_SetI64( key_value_document_t *pDocument, key_value_t *pValue, i64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_SetU64( key_value_document_t *pDocument, key_value_t *pValue, u64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_SetF64( key_value_document_t *pDocument, key_value_t *pValue, f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_SetString( key_value_document_t *pDocument, key_value_t *pValue, string_view_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_SetBinary( key_value_document_t *pDocument, key_value_t *pValue, binary_block_t value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t KeyValue_SetContainerType( key_value_document_t *pDocument, key_value_t *pValue, key_value_type_t type ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize KeyValue_ChildCount( const key_value_t *pContainer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_t *KeyValue_ChildAt( key_value_t *pContainer, usize iChild ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const key_value_t *KeyValue_ChildAt(
    const key_value_t *pContainer,
    usize iChild ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_t *KeyValue_Find(
    key_value_t *pObject,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const key_value_t *KeyValue_Find(
    const key_value_t *pObject,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_t *KeyValue_ObjectInsert(
    key_value_document_t *pDocument,
    key_value_t *pObject,
    string_view_t name,
    key_value_type_t type ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_t *KeyValue_ArrayAppend(
    key_value_document_t *pDocument,
    key_value_t *pArray,
    key_value_type_t type ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t KeyValue_Remove(
    key_value_document_t *pDocument,
    key_value_t *pParent,
    key_value_t *pChild ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_t *KeyValue_CloneInto(
    key_value_document_t *pDestDocument,
    key_value_t *pDestParent,
    const key_value_t *pSource ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUE_H
