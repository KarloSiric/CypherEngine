//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedMaterial.h
//  Purpose: Declares the backend-neutral cooked material resource contract.
//  Details: CYMT stores resolved shader-interface bindings, render policy,
//           texture/UV records, and typed packed constants. Runtime views borrow
//           immutable bytes; native renderer objects remain outside Common.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Cooked Material Contract

This header is a serialized resource contract. Persisted fields use fixed-width values and
explicit offsets; readers validate magic, version, counts, and byte ranges before interpreting
payload data.
================
*/

#ifndef CYPHER_COMMON_FORMATS_COOKEDMATERIAL_H
#define CYPHER_COMMON_FORMATS_COOKEDMATERIAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CookedResource.h"
#include "CypherCommon_CookedShader.h"
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_RenderFormat.h"

namespace cypher::common
{

inline constexpr fourcc_t CY_COOKED_MATERIAL_METADATA_CHUNK =
    Cy_MakeFourCC( 'M', 'T', 'M', 'D' ); // Material descriptors and string offsets.
inline constexpr fourcc_t CY_COOKED_MATERIAL_STRING_CHUNK =
    Cy_MakeFourCC( 'M', 'T', 'S', 'T' ); // Canonical NUL-terminated string table.
inline constexpr fourcc_t CY_COOKED_MATERIAL_CONSTANT_CHUNK =
    Cy_MakeFourCC( 'M', 'T', 'C', 'D' ); // Packed material constant bytes.
inline constexpr fourcc_t CY_COOKED_MATERIAL_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'M', 'A', 'T' ); // Signature inside MTMD.

inline constexpr format_version_t CY_COOKED_MATERIAL_RESOURCE_VERSION_V1 = 1u;
inline constexpr format_version_t CY_COOKED_MATERIAL_RESOURCE_VERSION_V2 = 2u;
inline constexpr format_version_t CY_COOKED_MATERIAL_RESOURCE_VERSION_CURRENT =
    CY_COOKED_MATERIAL_RESOURCE_VERSION_V2;
static_assert(
    CY_RENDER_MATERIAL_RESOURCE_VERSION ==
        CY_COOKED_MATERIAL_RESOURCE_VERSION_CURRENT,
    "CYMT writer and shared render-resource versions must match" );
inline constexpr format_version_t CY_COOKED_MATERIAL_METADATA_VERSION = 1u;
inline constexpr format_version_t CY_COOKED_MATERIAL_METADATA_VERSION_V2 = 2u;
inline constexpr usize CY_COOKED_MATERIAL_METADATA_HEADER_SIZE = 48u;
inline constexpr usize CY_COOKED_MATERIAL_TEXTURE_RECORD_SIZE = 16u;
inline constexpr usize CY_COOKED_MATERIAL_PARAMETER_RECORD_SIZE = 48u;
inline constexpr usize CY_COOKED_MATERIAL_METADATA_HEADER_SIZE_V2 = 128u;
inline constexpr usize CY_COOKED_MATERIAL_FEATURE_RECORD_SIZE_V2 = 32u;
inline constexpr usize CY_COOKED_MATERIAL_TEXTURE_RECORD_SIZE_V2 = 64u;
inline constexpr usize CY_COOKED_MATERIAL_PARAMETER_RECORD_SIZE_V2 = 40u;
inline constexpr usize CY_COOKED_MATERIAL_MAX_STRING_TABLE_SIZE = 64u * CY_KIB;
inline constexpr usize CY_COOKED_MATERIAL_MAX_CONSTANT_DATA_SIZE = 64u * CY_KIB;
inline constexpr u32 CY_COOKED_MATERIAL_METADATA_ALIGNMENT = 8u;
inline constexpr u32 CY_COOKED_MATERIAL_CONSTANT_ALIGNMENT = 16u;
inline constexpr u32 CY_COOKED_MATERIAL_STRING_ALIGNMENT = 1u;

enum cooked_material_flags_t : flags32_t {
    COOKED_MATERIAL_FLAG_NONE = 0u,
    COOKED_MATERIAL_FLAG_TWO_SIDED = CYPHER_BIT32( 0 ),
    COOKED_MATERIAL_FLAG_CASTS_SHADOWS = CYPHER_BIT32( 1 ),
    COOKED_MATERIAL_FLAG_RECEIVES_SHADOWS = CYPHER_BIT32( 2 )
};

// Resolved V2 feature values are persisted independently from authored override
// operations. There is no remove/inherit state in a cooked runtime material.
enum class cooked_material_feature_value_type_t : u32 {
    BOOL = 1u,
    ENUM = 2u
};

struct cooked_material_feature_source_v2_t {
    string_view_t name{};
    cooked_material_feature_value_type_t type{
        cooked_material_feature_value_type_t::BOOL
    };
    bool_t bValue{ CY_FALSE };
    string_view_t enumValue{};
};

struct cooked_material_texture_source_v2_t {
    string_view_t binding{};
    string_view_t texture{};
    // Optional preset for a combined sampled-texture binding. This does not name
    // or associate an independent CYSH SAMPLER binding; cookers must reject such
    // shader interfaces until that association receives its own format version.
    string_view_t sampler{};
    u64 nLogicalBinding{ 0u }; // Must match CookedShader_MakeLogicalBindingId.
    u32 nUvSet{ 0u };
    f64 uvScale[2]{ 1.0, 1.0 };
    f64 uvOffset[2]{ 0.0, 0.0 };
    f64 uvRotation{ 0.0 };
    bool_t bHasSampler{ CY_FALSE };
};

// Numeric fields are a host-side authoring representation. The writer converts
// the active field to explicit little-endian bytes. iByteOffset/cbByteSize come
// from the matching CYSH V3 reflection record; cbByteSize includes layout padding.
struct cooked_material_parameter_source_v2_t {
    string_view_t name{};
    u64 nLogicalBinding{ 0u }; // Must match CookedShader_MakeLogicalBindingId.
    render_shader_value_type_t type{ render_shader_value_type_t::NONE };
    u32 iByteOffset{ 0u };
    u32 cbByteSize{ 0u };
    bool_t bValue{ CY_FALSE };
    i32 signedValues[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{};
    u32 unsignedValues[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{};
    f64 floatingValues[CY_RENDER_MATERIAL_VALUE_MAX_COMPONENTS]{};
};

struct cooked_material_source_v2_t {
    string_view_t shader{};
    string_view_t surface{}; // Optional canonical .cysurface resource path.
    content_hash_t shaderInterfaceHash{}; // Required CYSH V3 interface identity.
    content_hash_t variantHash{}; // Optional expected feature hash; zero derives it.
    render_material_domain_t domain{ render_material_domain_t::SURFACE };
    render_material_alpha_mode_t alphaMode{
        render_material_alpha_mode_t::OPAQUE
    };
    f64 alphaCutoff{ 0.5 };
    span_t<const cooked_material_feature_source_v2_t> features{};
    span_t<const cooked_material_texture_source_v2_t> textures{};
    span_t<const cooked_material_parameter_source_v2_t> parameters{};
    flags32_t flags{
        COOKED_MATERIAL_FLAG_CASTS_SHADOWS |
        COOKED_MATERIAL_FLAG_RECEIVES_SHADOWS
    };
    bool_t bHasSurface{ CY_FALSE };
};

struct cooked_material_texture_source_t {
    string_view_t binding{}; // Shader sampler binding name.
    string_view_t texture{}; // Canonical .cytex resource path.
};

struct cooked_material_parameter_source_t {
    string_view_t name{}; // Shader parameter name.
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    };
    bool_t bValue{ CY_FALSE }; // Active when type is BOOL.
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{}; // Numeric components.
    u32 nComponents{ 0u }; // One for scalar; two through four for vector.
};

struct cooked_material_source_t {
    string_view_t shader{}; // Canonical .cyshader resource path.
    span_t<const cooked_material_texture_source_t> textures{}; // Sorted bindings.
    span_t<const cooked_material_parameter_source_t> parameters{}; // Sorted values.
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE }; // Material-wide persisted flags.
};

struct cooked_material_texture_view_t {
    string_view_t binding{}; // Borrowed name from the string table.
    string_view_t texture{}; // Borrowed resource path from the string table.
    string_view_t sampler{}; // V2 combined-binding sampler preset, never a binding name.
    u64 nLogicalBinding{ 0u }; // V2 stable shader-interface identity.
    u32 nUvSet{ 0u };
    f32 uvScale[2]{ 1.0F, 1.0F };
    f32 uvOffset[2]{ 0.0F, 0.0F };
    f32 uvRotation{ 0.0F };
    bool_t bHasSampler{ CY_FALSE };
};

struct cooked_material_parameter_view_t {
    string_view_t name{}; // Borrowed name from the string table.
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    };
    bool_t bValue{ CY_FALSE }; // Decoded Boolean value.
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{}; // Decoded numbers.
    u32 nComponents{ 0u }; // Active numeric component count.
    u64 nLogicalBinding{ 0u }; // V2 stable shader-interface identity.
    render_shader_value_type_t shaderType{ render_shader_value_type_t::NONE };
    u32 iByteOffset{ 0u }; // Offset into cooked_material_view_t::constantData.
    u32 cbValue{ 0u };     // Active typed bytes excluding type-defined padding.
    u32 cbByteSize{ 0u };  // Reflected occupied storage extent.
    binary_block_t data{}; // Borrowed storage bytes, including canonical padding.
    i32 signedValues[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{};
    u32 unsignedValues[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{};
    f64 floatingValues[CY_RENDER_MATERIAL_VALUE_MAX_COMPONENTS]{};
};

struct cooked_material_feature_view_t {
    string_view_t name{};
    cooked_material_feature_value_type_t type{
        cooked_material_feature_value_type_t::BOOL
    };
    bool_t bValue{ CY_FALSE };
    string_view_t enumValue{};
};

struct cooked_material_view_t {
    format_version_t nResourceVersion{ 0u };
    string_view_t shader{}; // Borrowed shader path from input CYRS bytes.
    string_view_t surface{}; // V2 optional .cysurface path.
    cooked_material_feature_view_t
        features[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    cooked_material_texture_view_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    cooked_material_parameter_view_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    u32 nFeatures{ 0u };   // V2 resolved feature values; zero for V1.
    u32 nTextures{ 0u };   // Active entries in textures.
    u32 nParameters{ 0u }; // Active entries in parameters.
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE }; // Validated persisted flags.
    render_material_domain_t domain{ render_material_domain_t::SURFACE };
    render_material_alpha_mode_t alphaMode{
        render_material_alpha_mode_t::OPAQUE
    };
    f32 alphaCutoff{ 0.5F };
    bool_t bHasSurface{ CY_FALSE };
    content_hash_t shaderInterfaceHash{};
    content_hash_t variantHash{};
    binary_block_t constantData{}; // V2 packed bytes; empty for V1/no parameters.
    content_hash_t sourceHash{}; // Optional authored-source identity.
};

enum class cooked_material_status_t : u8 {
    OK = 0u,                // Material operation completed.
    INVALID_ARGUMENT,      // Input, output, span, or aliasing contract is invalid.
    OUTPUT_TOO_SMALL,      // Destination cannot hold canonical output.
    RESOURCE_ERROR,        // Underlying CYRS validation or writing failed.
    INVALID_RESOURCE_TYPE, // CYRS payload is not a cooked material.
    VERSION_MISMATCH,      // Cooked material resource version is unsupported.
    INVALID_CHUNK_COUNT,   // Material does not contain exactly MTMD and MTST.
    INVALID_METADATA_CHUNK,// MTMD descriptor violates the format contract.
    INVALID_CONSTANT_CHUNK,// MTCD descriptor or payload is invalid.
    INVALID_STRING_CHUNK,  // MTST descriptor or size is invalid.
    INVALID_METADATA,      // Header or fixed records are malformed.
    INVALID_FLAGS,         // Unknown material flag bits are set.
    TEXTURE_LIMIT_EXCEEDED,// Texture binding count exceeds the fixed contract.
    PARAMETER_LIMIT_EXCEEDED,// Parameter count exceeds the fixed contract.
    FEATURE_LIMIT_EXCEEDED,// Feature count exceeds the fixed contract.
    INVALID_SHADER_PATH,   // Shader reference is not a canonical .cyshader path.
    INVALID_SURFACE_PATH,  // Optional surface reference is not canonical.
    INVALID_DOMAIN,        // Persisted material domain is unsupported.
    INVALID_ALPHA_MODE,    // Persisted blend/alpha mode is unsupported.
    INVALID_FEATURE,       // Feature name, kind, or resolved value is invalid.
    INVALID_TEXTURE,       // Texture binding or .cytex reference is invalid.
    INVALID_PARAMETER,     // Parameter name, type, or component count is invalid.
    INVALID_INTERFACE_HASH,// Required CYSH interface identity is absent or invalid.
    INVALID_VARIANT_HASH,  // Feature-derived identity does not match its payload.
    INVALID_CONSTANT_DATA, // Packed value offsets, types, bytes, or padding failed.
    INVALID_STRING,        // String offset, length, terminator, or table order failed.
    DUPLICATE_NAME,        // Sorted binding or parameter names are duplicated.
    DUPLICATE_BINDING_ID,  // Texture/parameter logical identifiers collide.
    NON_CANONICAL_ORDER,   // Named records are not ascending.
    NON_FINITE_VALUE,      // Numeric material data contains NaN or infinity.
    CONTENT_HASH_MISMATCH, // MTMD or MTST payload hash failed.
    NON_CANONICAL_LAYOUT   // Chunks, padding, values, or offsets are nondeterministic.
};

struct cooked_material_result_t {
    cooked_material_status_t status{ cooked_material_status_t::OK }; // Material result.
    cooked_resource_status_t resourceStatus{
        cooked_resource_status_t::OK
    }; // Underlying CYRS result when status is RESOURCE_ERROR.
    usize cbRead{ 0u };                  // Validated source bytes.
    usize cbWritten{ 0u };               // Published cooked bytes.
    usize cbRequired{ 0u };              // Exact output capacity required.
    usize iFeature{ CY_INVALID_SIZE };   // First offending feature override.
    usize iTexture{ CY_INVALID_SIZE };   // First offending texture binding.
    usize iParameter{ CY_INVALID_SIZE }; // First offending parameter.
    usize iChunk{ CY_INVALID_SIZE };     // First offending CYRS chunk.
};

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedMaterial_MetadataSize(
    u32 nTextures,
    u32 nParameters ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedMaterial_MetadataSizeV2(
    u32 nFeatures,
    u32 nTextures,
    u32 nParameters ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedMaterial_RequiredSize(
    const cooked_material_source_t &material ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedMaterial_RequiredSizeV2(
    const cooked_material_source_v2_t &material ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
cooked_material_result_t CookedMaterial_Write(
    const cooked_material_source_t &material,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
cooked_material_result_t CookedMaterial_WriteV2(
    const cooked_material_source_v2_t &material,
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
const cooked_material_texture_view_t *CookedMaterial_FindTextureById(
    const cooked_material_view_t &material,
    u64 nLogicalBinding ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const cooked_material_parameter_view_t *CookedMaterial_FindParameter(
    const cooked_material_view_t &material,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const cooked_material_parameter_view_t *CookedMaterial_FindParameterById(
    const cooked_material_view_t &material,
    u64 nLogicalBinding ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedMaterial_ComputeVariantHash(
    span_t<const cooked_material_feature_source_v2_t> features,
    content_hash_t *pVariantHashOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedMaterial_Succeeded(
    const cooked_material_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CookedMaterial_StatusName(
    cooked_material_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_COOKEDMATERIAL_H
