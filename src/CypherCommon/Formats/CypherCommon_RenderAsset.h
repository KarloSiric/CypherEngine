//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_RenderAsset.h
//  Purpose: Declares typed source views for renderer-facing assets.
//  Details: Decoders validate CYKV documents transactionally and return bounded,
//           zero-copy views. Strings borrow the source document; callers must keep
//           that document alive until the view is no longer used.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_FORMATS_RENDERASSET_H
#define CYPHER_COMMON_FORMATS_RENDERASSET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_RenderAssetSchema.h"

namespace cypher::common
{

enum class render_asset_decode_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_DOCUMENT,
    INVALID_IDENTIFIER,
    INVALID_RESOURCE_PATH,
    DUPLICATE_VALUE,
    INVALID_COMBINATION,
    INTERNAL_ERROR
};

struct render_asset_decode_result_t {
    render_asset_decode_status_t status{ render_asset_decode_status_t::OK };
    schema_validation_result_t validation{};
    string_view_t field{};
    usize iElement{ CY_INVALID_SIZE };
};

enum class render_shader_language_t : u8 {
    GLSL = 0u
};

struct render_shader_source_view_t {
    render_shader_language_t language{ render_shader_language_t::GLSL };
    string_view_t vertexSource{};
    string_view_t fragmentSource{};
    string_view_t defines[CY_RENDER_SHADER_MAX_DEFINES]{};
    usize nDefines{ 0u };
};

enum class render_texture_usage_t : u8 {
    COLOR = 0u,
    NORMAL,
    DATA
};

enum class render_texture_color_space_t : u8 {
    SRGB = 0u,
    LINEAR
};

struct render_texture_source_view_t {
    string_view_t source{};
    render_texture_usage_t usage{ render_texture_usage_t::COLOR };
    render_texture_color_space_t colorSpace{
        render_texture_color_space_t::SRGB
    };
    bool_t bGenerateMips{ CY_TRUE };
};

struct render_material_texture_view_t {
    string_view_t binding{};
    string_view_t texture{};
};

enum class render_material_parameter_type_t : u8 {
    BOOL = 0u,
    SCALAR,
    VECTOR
};

struct render_material_parameter_view_t {
    string_view_t name{};
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    };
    bool_t bValue{ CY_FALSE };
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{};
    usize nComponents{ 0u };
};

struct render_material_source_view_t {
    string_view_t shader{};
    render_material_texture_view_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    usize nTextures{ 0u };
    render_material_parameter_view_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    usize nParameters{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
render_asset_decode_result_t RenderShaderSource_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_shader_source_view_t *pShaderOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
render_asset_decode_result_t RenderTextureSource_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_texture_source_view_t *pTextureOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
render_asset_decode_result_t RenderMaterialSource_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_material_source_view_t *pMaterialOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t RenderAsset_DecodeSucceeded(
    const render_asset_decode_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *RenderAsset_DecodeStatusName(
    render_asset_decode_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *RenderTextureUsage_Name(
    render_texture_usage_t usage ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *RenderTextureColorSpace_Name(
    render_texture_color_space_t colorSpace ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_RENDERASSET_H
