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

/*
================
Key Value Internal Contract

CYKV nodes use explicit document ownership and stable value kinds. Internal helpers preserve
parent/child links and must not expose partially constructed trees.
================
*/

#ifndef CYPHER_COMMON_TIER1_KEYVALUEINTERNAL_H
#define CYPHER_COMMON_TIER1_KEYVALUEINTERNAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

struct key_value_t {
    key_value_document_t *pDocument{ nullptr }; // Owning document and arena source.
    key_value_t *pParent{ nullptr };            // Enclosing object or array.
    key_value_t *pPrevious{ nullptr };          // Previous sibling in insertion order.
    key_value_t *pNext{ nullptr };              // Next sibling in insertion order.
    key_value_t *pFirstChild{ nullptr };        // First child for container nodes.
    key_value_t *pLastChild{ nullptr };         // Last child for O(1) append.
    const char *pName{ nullptr };               // Arena-owned object key; null for array items.
    usize cchName{ 0u };                        // Key bytes excluding terminator.
    usize nChildren{ 0u };                      // Direct-child count for containers.
    key_value_type_t type{ key_value_type_t::NULL_VALUE }; // Active value-union member.
    union {
        bool_t bValue; // BOOL payload.
        i64 iValue;    // I64 payload.
        u64 uValue;    // U64 payload.
        f64 flValue;   // F64 payload.
        struct {
            const byte *pData; // Arena-owned STRING or BINARY bytes.
            usize cbSize;      // Payload bytes; string terminator is excluded.
        } bytes;               // Shared representation for byte-oriented types.
    } value{};                 // Scalar payload selected by type.
};

struct key_value_node_block_t {
    key_value_node_block_t *pNext{ nullptr }; // Next arena block.
    usize nCapacity{ 0u };                    // Nodes physically present after this header.
    usize nUsed{ 0u };                        // Sequentially claimed node slots.
};

struct key_value_data_block_t {
    key_value_data_block_t *pNext{ nullptr }; // Next string/binary arena block.
    usize cbCapacity{ 0u };                   // Usable payload bytes after this header.
    usize cbUsed{ 0u };                       // Sequentially claimed payload bytes.
};

struct key_value_document_t {
    const allocator_t *pAllocator{ nullptr };         // Owns every arena block.
    key_value_node_block_t *pNodeBlocks{ nullptr };   // Head of node arenas.
    key_value_data_block_t *pDataBlocks{ nullptr };   // Head of payload arenas.
    key_value_t *pFreeNodes{ nullptr };               // Recycled nodes linked through pNext.
    usize nInitialNodes{ 0u };                        // First node-block capacity.
    usize nNextNodes{ 0u };                           // Capacity used for the next node block.
    usize cbInitialData{ 0u };                        // First data-block capacity.
    usize cbNextData{ 0u };                           // Capacity used for the next data block.
    usize nNodes{ 0u };                               // Live semantic node count.
    usize cbData{ 0u };                               // Live copied payload bytes.
    const char *pSchemaId{ nullptr };                 // Arena-owned schema identifier.
    usize cchSchemaId{ 0u };                          // Schema identifier bytes.
    u32 nLanguageVersion{ 0u };                       // Parsed CYKV grammar revision.
    u32 nSchemaVersion{ 0u };                         // Parsed schema revision.
    bool_t bCaseInsensitiveKeys{ CY_FALSE };          // Object-key comparison policy.
    u32 nMagic{ 0u };                                 // Runtime validity cookie.
    key_value_t root{};                               // Permanent document root node.
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
