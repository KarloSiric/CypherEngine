//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Localization.h
//  Purpose: Declares owned locale catalogs and deterministic string lookup.
//  Details: Tier1 provides catalog storage and named substitution only. ID lookup is
//           fast but collision-aware; key lookup performs exact key validation. Plural
//           rules, collation, shaping, and grammar require a dedicated higher layer.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_LOCALIZATION_H
#define CYPHER_COMMON_TIER1_LOCALIZATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

using localized_string_id_t = u64;

struct localization_entry_t {
    string_view_t key{};
    string_view_t text{};
};

struct localization_argument_t {
    string_view_t name{};
    string_view_t value{};
};

struct localization_catalog_desc_t {
    const allocator_t *pAllocator{ nullptr };
    string_view_t localeTag{};
    usize nInitialEntries{ 1024u };
};

struct localization_catalog_t;

CYPHER_NODISCARD CYPHER_COMMON_API
localization_catalog_t *Localization_CreateCatalog(
    const localization_catalog_desc_t &desc ) noexcept;

CYPHER_COMMON_API void Localization_DestroyCatalog(
    localization_catalog_t *pCatalog ) noexcept;

CYPHER_COMMON_API void Localization_Clear(
    localization_catalog_t *pCatalog ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
localized_string_id_t Localization_IdFromKey( string_view_t key ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Localization_Add(
    localization_catalog_t *pCatalog,
    string_view_t key,
    string_view_t text ) noexcept;

// Returns empty when an ID is missing or ambiguous because of a hash collision.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t Localization_Find(
    const localization_catalog_t *pCatalog,
    localized_string_id_t id ) noexcept;

// Resolves by full key and therefore validates any hash collision in the ID index.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t Localization_FindByKey(
    const localization_catalog_t *pCatalog,
    string_view_t key ) noexcept;

// Replaces {name} fields using exact argument names. Double braces emit one
// literal brace. Unknown and malformed fields are preserved verbatim.
CYPHER_NODISCARD CYPHER_COMMON_API
usize Localization_Format(
    const localization_catalog_t *pCatalog,
    localized_string_id_t id,
    const localization_argument_t *pArguments,
    usize nArgumentCount,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t Localization_LocaleTag(
    const localization_catalog_t *pCatalog ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Localization_Count(
    const localization_catalog_t *pCatalog ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_LOCALIZATION_H
