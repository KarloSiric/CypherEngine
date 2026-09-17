//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.h
//  Purpose: Declares CYKV schemas for renderer-facing source assets.
//  Details: Version 1 remains frozen for existing content. Version 2 adds typed
//           shader interfaces, production texture policies, and inheritable
//           material recipes without changing the CYKV language.
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

The immutable descriptors validate CYKV structure before typed semantic decoding. Schema
versions are source-language contracts and remain independent from cooked resource versions.
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

inline constexpr u32 CY_RENDER_ASSET_SCHEMA_VERSION_V1 = 1u; // Frozen first generation.
inline constexpr u32 CY_RENDER_ASSET_SCHEMA_VERSION_V2 = 2u; // Typed render recipes.
// Compatibility alias used by existing V1 compiler code.
inline constexpr u32 CY_RENDER_ASSET_SCHEMA_VERSION =
    CY_RENDER_ASSET_SCHEMA_VERSION_V1;
inline constexpr usize CY_RENDER_ASSET_PATH_MAX_LENGTH = 259u; // Virtual path bytes.
inline constexpr usize CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH = 64u; // Name bytes.
inline constexpr usize CY_RENDER_SHADER_MAX_DEFINES = 64u; // Recipe define count.
inline constexpr usize CY_RENDER_SHADER_MAX_INTERFACE_TEXTURES = 32u;
inline constexpr usize CY_RENDER_SHADER_MAX_INTERFACE_SAMPLERS = 16u;
inline constexpr usize CY_RENDER_SHADER_MAX_INTERFACE_PARAMETERS = 64u;
inline constexpr usize CY_RENDER_SHADER_MAX_FEATURES = 32u;
inline constexpr usize CY_RENDER_SHADER_MAX_ENUM_VALUES = 16u;
inline constexpr u32 CY_RENDER_SHADER_DEFAULT_VARIANT_BUDGET = 64u;
inline constexpr u32 CY_RENDER_SHADER_MAX_VARIANT_BUDGET = 1024u;
inline constexpr usize CY_RENDER_MATERIAL_MAX_TEXTURES = 32u; // Binding count.
inline constexpr usize CY_RENDER_MATERIAL_MAX_PARAMETERS = 64u; // Value count.
inline constexpr usize CY_RENDER_MATERIAL_MAX_FEATURES = 32u; // Feature override count.
inline constexpr usize CY_RENDER_MATERIAL_VECTOR_MIN_COMPONENTS = 2u; // Vector floor.
inline constexpr usize CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS = 4u; // Vector ceiling.
inline constexpr usize CY_RENDER_MATERIAL_VALUE_MAX_COMPONENTS = 16u;
inline constexpr usize CY_RENDER_TEXTURE_MAX_RESIDENT_MIP_COUNT = 15u;
inline constexpr u32 CY_RENDER_MATERIAL_MAX_UV_SET = 7u;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderShaderSchema_V1() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderTextureSchema_V1() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderMaterialSchema_V1() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderShaderSchema_V2() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderTextureSchema_V2() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *RenderMaterialSchema_V2() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_RENDERASSETSCHEMA_H
