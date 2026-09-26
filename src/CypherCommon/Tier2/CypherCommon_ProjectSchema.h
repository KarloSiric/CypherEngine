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
//  - Added V2 on 2026-09-25
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

inline constexpr u32 CY_PROJECT_SCHEMA_VERSION = 1u; // cypher.project V1 generation.
inline constexpr u32 CY_PROJECT_SCHEMA_VERSION_V2 = 2u; // Game profile, maps root, settings (ADR 0009).
inline constexpr u32 CY_PROJECT_SCHEMA_CURRENT_VERSION = CY_PROJECT_SCHEMA_VERSION_V2; // Written by tools.
inline constexpr usize CY_PROJECT_GAME_MAX_LENGTH = 64u; // Game profile ID bytes.
inline constexpr usize CY_PROJECT_ID_MAX_LENGTH = 64u; // Stable ID bytes.
inline constexpr usize CY_PROJECT_NAME_MAX_LENGTH = 128u; // Display-name bytes.
// Matches the current VFS/resource runtime contract: 259 bytes plus terminator.
inline constexpr usize CY_PROJECT_PATH_MAX_LENGTH = 259u; // Virtual path bytes.
inline constexpr usize CY_PROJECT_MAX_SEARCH_PATHS = 64u; // Ordered mount roots.

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *ProjectSchema_V1() noexcept;

// V2 adds optional game, maps_path, settings, and map_defaults, makes
// start_map optional (a new project has no map yet), and opens the root so
// members written by newer tools survive.
CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *ProjectSchema_V2() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_PROJECTSCHEMA_H
