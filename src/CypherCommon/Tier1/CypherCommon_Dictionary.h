//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Dictionary.h
//  Purpose: Declares string-keyed owning dictionaries.
//  Details: Dictionary interns and owns every key, then indexes borrowed key views in
//           a hash map. Removed key bytes are reclaimed only on clear or shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_DICTIONARY_H
#define CYPHER_COMMON_TIER1_DICTIONARY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_HashMap.h"
#include "CypherCommon_StringPool.h"

namespace cypher::common
{

struct string_view_hash_t {
    bool_t bCaseInsensitiveAscii{ CY_FALSE };
    CYPHER_NODISCARD hash64_t operator()( string_view_t value ) const noexcept;
};

struct string_view_equal_t {
    bool_t bCaseInsensitiveAscii{ CY_FALSE };
    CYPHER_NODISCARD bool_t operator()(
        string_view_t left,
        string_view_t right ) const noexcept;
};

template <typename value_t>
struct dictionary_t {
    string_pool_t *pKeys{ nullptr };
    hash_map_t<string_view_t, value_t, string_view_hash_t, string_view_equal_t> entries{};
    const allocator_t *pAllocator{ nullptr };
    bool_t bCaseInsensitiveAscii{ CY_FALSE };
};

template <typename value_t>
CYPHER_NODISCARD bool_t Dictionary_Init(
    dictionary_t<value_t> *pDictionary,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u,
    bool_t bCaseInsensitiveAscii = CY_FALSE ) noexcept;

template <typename value_t>
void Dictionary_Shutdown( dictionary_t<value_t> *pDictionary ) noexcept;

template <typename value_t>
void Dictionary_Clear( dictionary_t<value_t> *pDictionary ) noexcept;

template <typename value_t>
CYPHER_NODISCARD hash_table_insert_result_t<value_t> Dictionary_Insert(
    dictionary_t<value_t> *pDictionary,
    string_view_t key,
    const value_t &value ) noexcept;

template <typename value_t>
CYPHER_NODISCARD value_t *Dictionary_Find(
    dictionary_t<value_t> *pDictionary,
    string_view_t key ) noexcept;

template <typename value_t>
CYPHER_NODISCARD bool_t Dictionary_Erase(
    dictionary_t<value_t> *pDictionary,
    string_view_t key ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DICTIONARY_H
