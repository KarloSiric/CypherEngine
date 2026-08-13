//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_ProjectSchema.h
//  Purpose: Declares the initial CYKV schema for Cypher project documents.
//  Details: The project schema is the first end-to-end Tier2 contract. It proves
//           parser metadata, registry lookup, structural validation, and diagnostics
//           before map and asset schemas are introduced.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_PROJECTSCHEMA_H
#define CYPHER_COMMON_TIER2_PROJECTSCHEMA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Schema.h"

namespace cypher::common
{

inline constexpr u32 CY_PROJECT_SCHEMA_VERSION = 1u;
inline constexpr usize CY_PROJECT_ID_MAX_LENGTH = 64u;
inline constexpr usize CY_PROJECT_NAME_MAX_LENGTH = 128u;
// Matches the current VFS/resource runtime contract: 259 bytes plus terminator.
inline constexpr usize CY_PROJECT_PATH_MAX_LENGTH = 259u;
inline constexpr usize CY_PROJECT_MAX_SEARCH_PATHS = 64u;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *ProjectSchema_V1() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_PROJECTSCHEMA_H
