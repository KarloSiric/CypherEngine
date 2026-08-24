//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.h
//  Purpose: Declares CYKV schemas for renderer-facing source assets.
//  Details: Version 1 covers shader recipes, texture import recipes, and material
//           instances. The schemas are backend-neutral and shared by tools, Mason,
//           tests, and development-time runtime loading.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Render Asset Schema Contract

This header is a serialized resource contract. Persisted fields use fixed-width values and
explicit offsets; readers validate magic, version, counts, and byte ranges before interpreting
payload data.
================
*/

#ifndef CYPHER_COMMON_FORMATS_RENDERASSETSCHEMA_H
#define CYPHER_COMMON_FORMATS_RENDERASSETSCHEMA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_RenderFormat.h"
#include "CypherCommon_Schema.h"

namespace cypher::common
{

inline constexpr u32 CY_RENDER_ASSET_SCHEMA_VERSION = 1u; // CYKV schema generation.
inline constexpr usize CY_RENDER_ASSET_PATH_MAX_LENGTH = 259u; // Virtual path bytes.
inline constexpr usize CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH = 64u; // Name bytes.
inline constexpr usize CY_RENDER_SHADER_MAX_DEFINES = 64u; // Recipe define count.
inline constexpr usize CY_RENDER_MATERIAL_MAX_TEXTURES = 32u; // Binding count.
inline constexpr usize CY_RENDER_MATERIAL_MAX_PARAMETERS = 64u; // Value count.
inline constexpr usize CY_RENDER_MATERIAL_VECTOR_MIN_COMPONENTS = 2u; // Vector floor.
inline constexpr usize CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS = 4u; // Vector ceiling.

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderShaderSchema_V1() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderTextureSchema_V1() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderMaterialSchema_V1() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_RENDERASSETSCHEMA_H
