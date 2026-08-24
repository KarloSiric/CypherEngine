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

/*
================
Render Asset Contract

This header is a serialized resource contract. Persisted fields use fixed-width values and
explicit offsets; readers validate magic, version, counts, and byte ranges before interpreting
payload data.
================
*/

#ifndef CYPHER_COMMON_FORMATS_RENDERASSET_H
#define CYPHER_COMMON_FORMATS_RENDERASSET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_RenderAssetSchema.h"

namespace cypher::common
{

enum class render_asset_decode_status_t : u8 {
    OK = 0u,             // Source document decoded successfully.
    INVALID_ARGUMENT,   // Required input, output, or diagnostic storage is invalid.
    INVALID_DOCUMENT,   // Generic CYKV schema validation failed.
    INVALID_IDENTIFIER, // A binding, define, or parameter name is not canonical.
    INVALID_RESOURCE_PATH,// A referenced asset path or extension is invalid.
    DUPLICATE_VALUE,    // A set-like source field contains a duplicate value.
    INVALID_COMBINATION,// Individually valid fields conflict semantically.
    INTERNAL_ERROR      // Validated data could not be decoded as its declared type.
};

struct render_asset_decode_result_t {
    render_asset_decode_status_t status{ render_asset_decode_status_t::OK }; // Decode result.
    schema_validation_result_t validation{}; // Detailed structural validation result.
    string_view_t field{};                   // Borrowed field name for semantic errors.
    usize iElement{ CY_INVALID_SIZE };        // Failing array element, when applicable.
};

enum class render_shader_language_t : u8 {
    GLSL = 0u // OpenGL Shading Language source recipe.
};

struct render_shader_source_view_t {
    render_shader_language_t language{ render_shader_language_t::GLSL }; // Source language.
    string_view_t vertexSource{};   // Canonical virtual path borrowed from CYKV.
    string_view_t fragmentSource{}; // Canonical virtual path borrowed from CYKV.
    string_view_t defines[CY_RENDER_SHADER_MAX_DEFINES]{}; // Unique preprocessor names.
    usize nDefines{ 0u };           // Active entries in defines.
};

enum class render_texture_usage_t : u8 {
    COLOR = 0u, // Color data; sRGB or linear according to the source recipe.
    NORMAL,     // Normal-vector data; always linear.
    DATA        // Masks, roughness, height, or other non-color data; always linear.
};

enum class render_texture_color_space_t : u8 {
    SRGB = 0u, // Gamma-encoded color values.
    LINEAR     // Linear numeric values suitable for computation.
};

struct render_texture_source_view_t {
    string_view_t source{}; // Borrowed canonical path to imported image data.
    render_texture_usage_t usage{ render_texture_usage_t::COLOR }; // Intended sampling use.
    render_texture_color_space_t colorSpace{
        render_texture_color_space_t::SRGB
    }; // Interpretation applied by import and mip generation.
    bool_t bGenerateMips{ CY_TRUE }; // Build a complete mip chain when true.
};

struct render_material_texture_view_t {
    string_view_t binding{}; // Shader binding name borrowed from the member name.
    string_view_t texture{}; // Borrowed canonical .cytex resource path.
};

enum class render_material_parameter_type_t : u8 {
    BOOL = 0u, // Boolean shader parameter.
    SCALAR,    // One numeric component.
    VECTOR     // Two through four numeric components.
};

struct render_material_parameter_view_t {
    string_view_t name{}; // Shader parameter name borrowed from the member name.
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    }; // Selects the active value representation below.
    bool_t bValue{ CY_FALSE }; // Active only when type is BOOL.
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{}; // Scalar/vector components.
    usize nComponents{ 0u }; // One for scalar; two through four for vector.
};

struct render_material_source_view_t {
    string_view_t shader{}; // Borrowed canonical .cyshader resource path.
    render_material_texture_view_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{}; // Texture bindings in source order.
    usize nTextures{ 0u }; // Active entries in textures.
    render_material_parameter_view_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{}; // Typed shader parameters.
    usize nParameters{ 0u }; // Active entries in parameters.
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
