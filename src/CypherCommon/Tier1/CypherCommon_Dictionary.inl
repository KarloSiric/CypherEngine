//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Dictionary.inl
//  Purpose: Implements string-keyed owning dictionaries.
//  Details: Keys are interned before entering the hash map, so stored views remain
//           stable until clear or shutdown. Erase intentionally retains pool bytes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_DICTIONARY_INL
#define CYPHER_COMMON_TIER1_DICTIONARY_INL

#ifndef CYPHER_COMMON_TIER1_DICTIONARY_H
    #include "CypherCommon_Dictionary.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Char.h"
#include "CypherCommon_HashFNV.h"

namespace cypher::common
{

inline hash64_t string_view_hash_t::operator()( string_view_t value ) const noexcept
{
    const bool_t bValidValue = StringView_IsValid( value );
    CY_ASSERT_MSG( bValidValue, "Dictionary hashing requires a valid key view." );
    if ( !bValidValue ) {
        return 0u;
    }
    if ( !bCaseInsensitiveAscii ) {
        return HashFNV1a64_String( value );
    }

    // Case-insensitive mode folds ASCII bytes before the same FNV recurrence.
    hash64_t hash = CY_FNV1A64_OFFSET;
    for ( usize iChar = 0u; iChar < value.cchLength; ++iChar ) {
        hash ^= static_cast<u8>( Char_ToLowerAscii( value.pData[iChar] ) );
        hash *= CY_FNV1A64_PRIME;
    }
    return hash;
}

inline bool_t string_view_equal_t::operator()(
    string_view_t left,
    string_view_t right ) const noexcept
{
    return bCaseInsensitiveAscii
        ? StringView_EqualsInsensitiveAscii( left, right )
        : StringView_Equals( left, right );
}

template <typename value_t>
dictionary_t<value_t>::~dictionary_t() noexcept
{
    Dictionary_Shutdown( this );
}

template <typename value_t>
bool_t Dictionary_Init(
    dictionary_t<value_t> *pDictionary,
    const allocator_t *pAllocator,
    usize nInitialCapacity,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    const bool_t bValidDestination = pDictionary != nullptr &&
        pDictionary->pKeys == nullptr &&
        pDictionary->pAllocator == nullptr &&
        HashMap_IsValid( &pDictionary->entries ) &&
        pDictionary->entries.pAllocator == nullptr;
    CY_ASSERT_MSG(
        bValidDestination,
        "Dictionary_Init requires a canonical empty destination." );
    if ( !bValidDestination || !Allocator_IsValid( pAllocator ) ) {
        return CY_FALSE;
    }

    // Interned key storage keeps every hash-map string view stable for its lifetime.
    string_pool_desc_t poolDesc{};
    poolDesc.pAllocator = pAllocator;
    poolDesc.nInitialBuckets = nInitialCapacity > 0u
        ? nInitialCapacity
        : poolDesc.nInitialBuckets;
    poolDesc.flags = bCaseInsensitiveAscii
        ? STRING_POOL_FLAG_CASE_INSENSITIVE_ASCII
        : STRING_POOL_FLAG_NONE;
    string_pool_t *pKeys = StringPool_Create( poolDesc );
    if ( pKeys == nullptr ) {
        return CY_FALSE;
    }

    const string_view_hash_t hasher{ bCaseInsensitiveAscii };
    const string_view_equal_t equalKey{ bCaseInsensitiveAscii };
    if ( !HashMap_Init(
             &pDictionary->entries,
             pAllocator,
             nInitialCapacity,
             hasher,
             equalKey ) ) {
        StringPool_Destroy( pKeys );
        return CY_FALSE;
    }

    pDictionary->pKeys = pKeys;
    pDictionary->pAllocator = pAllocator;
    pDictionary->bCaseInsensitiveAscii = bCaseInsensitiveAscii;
    return CY_TRUE;
}

template <typename value_t>
void Dictionary_Shutdown( dictionary_t<value_t> *pDictionary ) noexcept
{
    if ( pDictionary == nullptr ) {
        return;
    }
    if ( pDictionary->pKeys == nullptr &&
         pDictionary->pAllocator == nullptr &&
         pDictionary->entries.pAllocator == nullptr ) {
        return;
    }

    const bool_t bValidDictionary = Dictionary_IsValid( pDictionary );
    CY_ASSERT_MSG(
        bValidDictionary,
        "Dictionary_Shutdown requires a valid dictionary." );
    if ( !bValidDictionary ) {
        return;
    }

    HashMap_Shutdown( &pDictionary->entries );
    StringPool_Destroy( pDictionary->pKeys );
    pDictionary->pKeys = nullptr;
    pDictionary->pAllocator = nullptr;
    pDictionary->bCaseInsensitiveAscii = CY_FALSE;
}

template <typename value_t>
void Dictionary_Clear( dictionary_t<value_t> *pDictionary ) noexcept
{
    const bool_t bValidDictionary = Dictionary_IsValid( pDictionary );
    CY_ASSERT_MSG(
        bValidDictionary,
        "Dictionary_Clear requires an initialized dictionary." );
    if ( !bValidDictionary ) {
        return;
    }

    // Entries borrow key bytes, so remove them before releasing pool blocks.
    HashMap_Clear( &pDictionary->entries );
    StringPool_Clear( pDictionary->pKeys );
}

template <typename value_t>
bool_t Dictionary_IsValid(
    const dictionary_t<value_t> *pDictionary ) noexcept
{
    if ( pDictionary == nullptr ) {
        return CY_FALSE;
    }
    const bool_t bCanonicalEmpty =
        pDictionary->pKeys == nullptr &&
        pDictionary->pAllocator == nullptr &&
        pDictionary->entries.pAllocator == nullptr;
    if ( bCanonicalEmpty ) {
        return HashMap_IsValid( &pDictionary->entries );
    }

    return pDictionary->pKeys != nullptr &&
           StringPool_IsValid( pDictionary->pKeys ) &&
           Allocator_IsValid( pDictionary->pAllocator ) &&
           HashMap_IsValid( &pDictionary->entries ) &&
           pDictionary->entries.pAllocator == pDictionary->pAllocator &&
           pDictionary->entries.hasher.bCaseInsensitiveAscii ==
               pDictionary->bCaseInsensitiveAscii &&
           pDictionary->entries.equalKey.bCaseInsensitiveAscii ==
               pDictionary->bCaseInsensitiveAscii;
}

template <typename value_t>
hash_table_insert_result_t<value_t> Dictionary_Insert(
    dictionary_t<value_t> *pDictionary,
    string_view_t key,
    const value_t &value ) noexcept
{
    const bool_t bValidDictionary = Dictionary_IsValid( pDictionary ) &&
                                     pDictionary->pKeys != nullptr;
    const bool_t bValidKey = StringView_IsValid( key );
    CY_ASSERT_MSG(
        bValidDictionary,
        "Dictionary_Insert requires an initialized dictionary." );
    CY_ASSERT_MSG( bValidKey, "Dictionary_Insert requires a valid key view." );
    if ( !bValidDictionary || !bValidKey ) {
        return {};
    }

    if ( value_t *pExisting = HashMap_Find( &pDictionary->entries, key ) ) {
        return { pExisting, CY_FALSE };
    }

    // Copy before insertion because caller-owned key bytes may disappear immediately.
    const char *pOwnedKey = StringPool_Intern( pDictionary->pKeys, key );
    if ( pOwnedKey == nullptr ) {
        return {};
    }
    return HashMap_Insert(
        &pDictionary->entries,
        { pOwnedKey, key.cchLength },
        value );
}

template <typename value_t>
value_t *Dictionary_Find(
    dictionary_t<value_t> *pDictionary,
    string_view_t key ) noexcept
{
    return const_cast<value_t *>( Dictionary_Find(
        static_cast<const dictionary_t<value_t> *>( pDictionary ),
        key ) );
}

template <typename value_t>
const value_t *Dictionary_Find(
    const dictionary_t<value_t> *pDictionary,
    string_view_t key ) noexcept
{
    return Dictionary_IsValid( pDictionary ) &&
           pDictionary->pKeys != nullptr &&
           StringView_IsValid( key )
        ? HashMap_Find( &pDictionary->entries, key )
        : nullptr;
}

template <typename value_t>
bool_t Dictionary_Contains(
    const dictionary_t<value_t> *pDictionary,
    string_view_t key ) noexcept
{
    return Dictionary_Find( pDictionary, key ) != nullptr;
}

template <typename value_t>
bool_t Dictionary_Erase(
    dictionary_t<value_t> *pDictionary,
    string_view_t key ) noexcept
{
    return Dictionary_IsValid( pDictionary ) &&
           pDictionary->pKeys != nullptr &&
           StringView_IsValid( key )
        ? HashMap_Erase( &pDictionary->entries, key )
        : CY_FALSE;
}

template <typename value_t>
usize Dictionary_Count(
    const dictionary_t<value_t> *pDictionary ) noexcept
{
    return Dictionary_IsValid( pDictionary ) && pDictionary->pKeys != nullptr
        ? HashMap_Count( &pDictionary->entries )
        : 0u;
}

template <typename value_t>
bool_t Dictionary_IsEmpty(
    const dictionary_t<value_t> *pDictionary ) noexcept
{
    return Dictionary_Count( pDictionary ) == 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DICTIONARY_INL
