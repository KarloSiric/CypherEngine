//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedMaterial.h
//  Purpose: Declares the backend-neutral cooked material resource contract.
//  Details: Version 1 stores canonical shader and texture resource references
//           plus typed material values. Runtime views borrow immutable file bytes;
//           renderer handles, descriptor sets, and Qt objects remain outside Common.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_FORMATS_COOKEDMATERIAL_H
#define CYPHER_COMMON_FORMATS_COOKEDMATERIAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CookedResource.h"
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_RenderFormat.h"

namespace cypher::common
{

inline constexpr fourcc_t CY_COOKED_MATERIAL_METADATA_CHUNK =
    Cy_MakeFourCC( 'M', 'T', 'M', 'D' );
inline constexpr fourcc_t CY_COOKED_MATERIAL_STRING_CHUNK =
    Cy_MakeFourCC( 'M', 'T', 'S', 'T' );
inline constexpr fourcc_t CY_COOKED_MATERIAL_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'M', 'A', 'T' );

inline constexpr format_version_t CY_COOKED_MATERIAL_METADATA_VERSION = 1u;
inline constexpr usize CY_COOKED_MATERIAL_METADATA_HEADER_SIZE = 48u;
inline constexpr usize CY_COOKED_MATERIAL_TEXTURE_RECORD_SIZE = 16u;
inline constexpr usize CY_COOKED_MATERIAL_PARAMETER_RECORD_SIZE = 48u;
inline constexpr usize CY_COOKED_MATERIAL_MAX_STRING_TABLE_SIZE = 64u * CY_KIB;
inline constexpr u32 CY_COOKED_MATERIAL_METADATA_ALIGNMENT = 8u;
inline constexpr u32 CY_COOKED_MATERIAL_STRING_ALIGNMENT = 1u;

enum cooked_material_flags_t : flags32_t {
    COOKED_MATERIAL_FLAG_NONE = 0u
};

struct cooked_material_texture_source_t {
    string_view_t binding{};
    string_view_t texture{};
};

struct cooked_material_parameter_source_t {
    string_view_t name{};
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    };
    bool_t bValue{ CY_FALSE };
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{};
    u32 nComponents{ 0u };
};

struct cooked_material_source_t {
    string_view_t shader{};
    span_t<const cooked_material_texture_source_t> textures{};
    span_t<const cooked_material_parameter_source_t> parameters{};
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE };
};

struct cooked_material_texture_view_t {
    string_view_t binding{};
    string_view_t texture{};
};

struct cooked_material_parameter_view_t {
    string_view_t name{};
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    };
    bool_t bValue{ CY_FALSE };
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{};
    u32 nComponents{ 0u };
};

struct cooked_material_view_t {
    string_view_t shader{};
    cooked_material_texture_view_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    cooked_material_parameter_view_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    u32 nTextures{ 0u };
    u32 nParameters{ 0u };
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE };
    content_hash_t sourceHash{};
};

enum class cooked_material_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUTPUT_TOO_SMALL,
    RESOURCE_ERROR,
    INVALID_RESOURCE_TYPE,
    VERSION_MISMATCH,
    INVALID_CHUNK_COUNT,
    INVALID_METADATA_CHUNK,
    INVALID_STRING_CHUNK,
    INVALID_METADATA,
    INVALID_FLAGS,
    TEXTURE_LIMIT_EXCEEDED,
    PARAMETER_LIMIT_EXCEEDED,
    INVALID_SHADER_PATH,
    INVALID_TEXTURE,
    INVALID_PARAMETER,
    INVALID_STRING,
    DUPLICATE_NAME,
    NON_CANONICAL_ORDER,
    NON_FINITE_VALUE,
    CONTENT_HASH_MISMATCH,
    NON_CANONICAL_LAYOUT
};

struct cooked_material_result_t {
    cooked_material_status_t status{ cooked_material_status_t::OK };
    cooked_resource_status_t resourceStatus{
        cooked_resource_status_t::OK
    };
    usize cbRead{ 0u };
    usize cbWritten{ 0u };
    usize cbRequired{ 0u };
    usize iTexture{ CY_INVALID_SIZE };
    usize iParameter{ CY_INVALID_SIZE };
    usize iChunk{ CY_INVALID_SIZE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedMaterial_MetadataSize(
    u32 nTextures,
    u32 nParameters ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedMaterial_RequiredSize(
    const cooked_material_source_t &material ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
cooked_material_result_t CookedMaterial_Write(
    const cooked_material_source_t &material,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
cooked_material_result_t CookedMaterial_Read(
    binary_block_t input,
    cooked_material_view_t *pMaterialOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const cooked_material_texture_view_t *CookedMaterial_FindTexture(
    const cooked_material_view_t &material,
    string_view_t binding ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const cooked_material_parameter_view_t *CookedMaterial_FindParameter(
    const cooked_material_view_t &material,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedMaterial_Succeeded(
    const cooked_material_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CookedMaterial_StatusName(
    cooked_material_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_COOKEDMATERIAL_H
