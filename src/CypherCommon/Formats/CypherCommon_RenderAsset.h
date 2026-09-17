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
Typed Render Asset Source Contract

These structures are bounded borrowed views produced from validated CYKV documents. They are
not disk layouts. Cooked runtime serialization lives in the CYSH, CYTX, and CYMT contracts.
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

// Version 2 source views are separate from the frozen V1 structures. They expose
// authored intent to cookers without storing backend binding numbers or API state.
enum class render_asset_value_type_t : u8 {
    BOOL = 0u,
    I64,
    U64,
    F64,
    F64_ARRAY
};

struct render_asset_value_view_t {
    render_asset_value_type_t type{ render_asset_value_type_t::F64 };
    bool_t bValue{ CY_FALSE };
    i64 iValue{ 0 };
    u64 uValue{ 0u };
    f64 values[CY_RENDER_MATERIAL_VALUE_MAX_COMPONENTS]{};
    usize nComponents{ 0u };
};

struct render_shader_stage_source_v2_view_t {
    string_view_t source{}; // Canonical stage source path.
    string_view_t entry{};  // ASCII entry point; defaults to main.
};

enum class render_shader_texture_type_t : u8 {
    TEXTURE_2D = 0u,
    TEXTURE_CUBE,
    TEXTURE_2D_ARRAY,
    TEXTURE_3D
};

struct render_shader_texture_interface_v2_view_t {
    string_view_t name{};
    render_shader_texture_type_t type{
        render_shader_texture_type_t::TEXTURE_2D
    };
    render_texture_usage_t usage{ render_texture_usage_t::COLOR };
    render_texture_color_space_t colorSpace{
        render_texture_color_space_t::SRGB
    };
    bool_t bRequired{ CY_TRUE };
};

enum class render_shader_sampler_type_t : u8 {
    FILTERING = 0u,
    COMPARISON
};

struct render_shader_sampler_interface_v2_view_t {
    string_view_t name{};
    render_shader_sampler_type_t type{
        render_shader_sampler_type_t::FILTERING
    };
    string_view_t defaultPreset{};
    bool_t bHasDefaultPreset{ CY_FALSE };
    bool_t bRequired{ CY_TRUE };
};

enum class render_shader_parameter_type_t : u8 {
    BOOL = 0u,
    I32,
    U32,
    F32,
    F32X2,
    F32X3,
    F32X4,
    COLOR3,
    COLOR4,
    MAT3,
    MAT4
};

struct render_shader_parameter_interface_v2_view_t {
    string_view_t name{};
    render_shader_parameter_type_t type{ render_shader_parameter_type_t::F32 };
    render_asset_value_view_t defaultValue{};
    bool_t bHasDefault{ CY_FALSE };
    f64 minimum{ 0.0 };
    f64 maximum{ 0.0 };
    bool_t bHasMinimum{ CY_FALSE };
    bool_t bHasMaximum{ CY_FALSE };
    bool_t bRequired{ CY_FALSE };
};

enum class render_shader_feature_type_t : u8 {
    BOOL = 0u,
    ENUM
};

enum class render_shader_feature_mode_t : u8 {
    STATIC = 0u,
    DYNAMIC
};

struct render_shader_feature_v2_view_t {
    string_view_t name{};
    render_shader_feature_type_t type{ render_shader_feature_type_t::BOOL };
    render_shader_feature_mode_t mode{ render_shader_feature_mode_t::STATIC };
    bool_t bDefault{ CY_FALSE };
    string_view_t defaultEnum{};
    string_view_t enumValues[CY_RENDER_SHADER_MAX_ENUM_VALUES]{};
    usize nEnumValues{ 0u };
};

struct render_shader_source_v2_view_t {
    render_shader_language_t language{ render_shader_language_t::GLSL };
    render_shader_stage_source_v2_view_t vertex{};
    render_shader_stage_source_v2_view_t fragment{};
    string_view_t defines[CY_RENDER_SHADER_MAX_DEFINES]{};
    usize nDefines{ 0u };
    render_shader_texture_interface_v2_view_t
        textures[CY_RENDER_SHADER_MAX_INTERFACE_TEXTURES]{};
    usize nTextures{ 0u };
    render_shader_sampler_interface_v2_view_t
        samplers[CY_RENDER_SHADER_MAX_INTERFACE_SAMPLERS]{};
    usize nSamplers{ 0u };
    render_shader_parameter_interface_v2_view_t
        parameters[CY_RENDER_SHADER_MAX_INTERFACE_PARAMETERS]{};
    usize nParameters{ 0u };
    render_shader_feature_v2_view_t
        features[CY_RENDER_SHADER_MAX_FEATURES]{};
    usize nFeatures{ 0u };
    u32 nVariantBudget{ CY_RENDER_SHADER_DEFAULT_VARIANT_BUDGET };
    u32 nStaticVariantCount{ 1u };
};

enum class render_texture_type_t : u8 {
    TEXTURE_2D = 0u
};

enum class render_texture_alpha_mode_t : u8 {
    NONE = 0u,
    STRAIGHT,
    PREMULTIPLIED,
    MASK,
    DATA
};

struct render_texture_alpha_policy_v2_t {
    render_texture_alpha_mode_t mode{ render_texture_alpha_mode_t::NONE };
    f64 cutoff{ 0.5 };
    bool_t bDilateRgb{ CY_FALSE };
};

enum class render_texture_mip_mode_t : u8 {
    GENERATE = 0u,
    PRESERVE,
    NONE
};

enum class render_texture_mip_filter_t : u8 {
    BOX = 0u,
    KAISER,
    LANCZOS
};

enum class render_texture_edge_mode_t : u8 {
    CLAMP = 0u,
    REPEAT,
    MIRROR
};

struct render_texture_mip_policy_v2_t {
    render_texture_mip_mode_t mode{ render_texture_mip_mode_t::GENERATE };
    render_texture_mip_filter_t filter{ render_texture_mip_filter_t::BOX };
    render_texture_edge_mode_t edge{ render_texture_edge_mode_t::CLAMP };
    bool_t bPreserveAlphaCoverage{ CY_FALSE };
};

enum class render_texture_output_format_t : u8 {
    AUTO = 0u,
    UNCOMPRESSED
};

enum class render_texture_output_quality_t : u8 {
    FAST = 0u,
    BALANCED,
    PRODUCTION
};

struct render_texture_output_policy_v2_t {
    render_texture_output_format_t format{
        render_texture_output_format_t::AUTO
    };
    render_texture_output_quality_t quality{
        render_texture_output_quality_t::BALANCED
    };
};

struct render_texture_streaming_policy_v2_t {
    string_view_t resourceClass{};
    usize nResidentMipCount{ 1u };
    bool_t bEnabled{ CY_FALSE };
};

struct render_texture_source_v2_view_t {
    string_view_t source{};
    render_texture_type_t type{ render_texture_type_t::TEXTURE_2D };
    render_texture_usage_t usage{ render_texture_usage_t::COLOR };
    render_texture_color_space_t colorSpace{
        render_texture_color_space_t::SRGB
    };
    render_texture_alpha_policy_v2_t alpha{};
    render_texture_mip_policy_v2_t mips{};
    render_texture_output_policy_v2_t output{};
    render_texture_streaming_policy_v2_t streaming{};
};

enum class render_material_domain_t : u8 {
    SURFACE = 0u,
    DECAL,
    UI,
    POSTPROCESS,
    PARTICLE
};

enum class render_material_alpha_mode_t : u8 {
    OPAQUE = 0u,
    MASK,
    BLEND,
    ADDITIVE
};

struct render_material_state_v2_view_t {
    render_material_alpha_mode_t alphaMode{
        render_material_alpha_mode_t::OPAQUE
    };
    f64 alphaCutoff{ 0.5 };
    bool_t bTwoSided{ CY_FALSE };
    bool_t bCastsShadows{ CY_TRUE };
    bool_t bReceivesShadows{ CY_TRUE };
    bool_t bHasAlphaMode{ CY_FALSE };
    bool_t bHasAlphaCutoff{ CY_FALSE };
    bool_t bHasTwoSided{ CY_FALSE };
    bool_t bHasCastsShadows{ CY_FALSE };
    bool_t bHasReceivesShadows{ CY_FALSE };
};

struct render_material_uv_binding_v2_view_t {
    u32 nSet{ 0u };
    f64 scale[2]{ 1.0, 1.0 };
    f64 offset[2]{ 0.0, 0.0 };
    f64 rotation{ 0.0 };
    bool_t bPresent{ CY_FALSE };
};

struct render_material_texture_v2_view_t {
    string_view_t binding{};
    string_view_t resource{};
    string_view_t sampler{};
    render_material_uv_binding_v2_view_t uv{};
    bool_t bHasResource{ CY_FALSE };
    bool_t bHasSampler{ CY_FALSE };
    bool_t bRemove{ CY_FALSE };
};

enum class render_material_feature_value_type_t : u8 {
    BOOL = 0u,
    ENUM
};

struct render_material_feature_v2_view_t {
    string_view_t name{};
    render_material_feature_value_type_t type{
        render_material_feature_value_type_t::BOOL
    };
    bool_t bValue{ CY_FALSE };
    string_view_t enumValue{};
    bool_t bRemove{ CY_FALSE };
};

struct render_material_parameter_v2_view_t {
    string_view_t name{};
    render_asset_value_view_t value{};
    bool_t bRemove{ CY_FALSE };
};

struct render_material_source_v2_view_t {
    string_view_t base{};
    string_view_t shader{};
    string_view_t surface{};
    bool_t bHasBase{ CY_FALSE };
    bool_t bHasShader{ CY_FALSE };
    bool_t bHasSurface{ CY_FALSE };
    render_material_domain_t domain{ render_material_domain_t::SURFACE };
    bool_t bHasDomain{ CY_FALSE };
    render_material_state_v2_view_t state{};
    render_material_feature_v2_view_t
        features[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    usize nFeatures{ 0u };
    render_material_texture_v2_view_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    usize nTextures{ 0u };
    render_material_parameter_v2_view_t
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
render_asset_decode_result_t RenderShaderSourceV2_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_shader_source_v2_view_t *pShaderOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
render_asset_decode_result_t RenderTextureSourceV2_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_texture_source_v2_view_t *pTextureOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
render_asset_decode_result_t RenderMaterialSourceV2_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    render_material_source_v2_view_t *pMaterialOut ) noexcept;

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
