//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueInternal.h
//  Purpose: Shares private CYKV storage contracts between implementation units.
//  Details: This header is not a public API. It exposes document internals only to
//           the tree, parser, writer, JSON, and packed-binary implementations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_KEYVALUEINTERNAL_H
#define CYPHER_COMMON_TIER1_KEYVALUEINTERNAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

struct key_value_t {
    key_value_document_t *pDocument{ nullptr };
    key_value_t *pParent{ nullptr };
    key_value_t *pPrevious{ nullptr };
    key_value_t *pNext{ nullptr };
    key_value_t *pFirstChild{ nullptr };
    key_value_t *pLastChild{ nullptr };
    const char *pName{ nullptr };
    usize cchName{ 0u };
    usize nChildren{ 0u };
    key_value_type_t type{ key_value_type_t::NULL_VALUE };
    union {
        bool_t bValue;
        i64 iValue;
        u64 uValue;
        f64 flValue;
        struct {
            const byte *pData;
            usize cbSize;
        } bytes;
    } value{};
};

struct key_value_node_block_t {
    key_value_node_block_t *pNext{ nullptr };
    usize nCapacity{ 0u };
    usize nUsed{ 0u };
};

struct key_value_data_block_t {
    key_value_data_block_t *pNext{ nullptr };
    usize cbCapacity{ 0u };
    usize cbUsed{ 0u };
};

struct key_value_document_t {
    const allocator_t *pAllocator{ nullptr };
    key_value_node_block_t *pNodeBlocks{ nullptr };
    key_value_data_block_t *pDataBlocks{ nullptr };
    key_value_t *pFreeNodes{ nullptr };
    usize nInitialNodes{ 0u };
    usize nNextNodes{ 0u };
    usize cbInitialData{ 0u };
    usize cbNextData{ 0u };
    usize nNodes{ 0u };
    usize cbData{ 0u };
    const char *pSchemaId{ nullptr };
    usize cchSchemaId{ 0u };
    u32 nLanguageVersion{ 0u };
    u32 nSchemaVersion{ 0u };
    bool_t bCaseInsensitiveKeys{ CY_FALSE };
    u32 nMagic{ 0u };
    key_value_t root{};
};

CYPHER_NODISCARD bool_t KeyValue_InternalDocumentIsValid(
    const key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t KeyValue_InternalTreeIsValid(
    const key_value_t *pRoot ) noexcept;

CYPHER_NODISCARD key_value_document_t *KeyValue_InternalCreateLike(
    const key_value_document_t *pDocument ) noexcept;

void KeyValue_InternalMoveDocumentContents(
    key_value_document_t *pDest,
    key_value_document_t *pSource ) noexcept;

CYPHER_NODISCARD usize KeyValue_InternalNodeCount(
    const key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD usize KeyValue_InternalDataSize(
    const key_value_document_t *pDocument ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEINTERNAL_H
