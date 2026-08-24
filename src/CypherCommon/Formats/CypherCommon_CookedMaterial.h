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
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_RenderFormat.h"

namespace cypher::common
{

inline constexpr fourcc_t CY_COOKED_MATERIAL_METADATA_CHUNK =
    Cy_MakeFourCC( 'M', 'T', 'M', 'D' ); // Material descriptors and string offsets.
inline constexpr fourcc_t CY_COOKED_MATERIAL_STRING_CHUNK =
    Cy_MakeFourCC( 'M', 'T', 'S', 'T' ); // Canonical NUL-terminated string table.
inline constexpr fourcc_t CY_COOKED_MATERIAL_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'M', 'A', 'T' ); // Signature inside MTMD.

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
};

struct cooked_material_parameter_view_t {
    string_view_t name{}; // Borrowed name from the string table.
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    };
    bool_t bValue{ CY_FALSE }; // Decoded Boolean value.
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{}; // Decoded numbers.
    u32 nComponents{ 0u }; // Active numeric component count.
};

struct cooked_material_view_t {
    string_view_t shader{}; // Borrowed shader path from input CYRS bytes.
    cooked_material_texture_view_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    cooked_material_parameter_view_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    u32 nTextures{ 0u };   // Active entries in textures.
    u32 nParameters{ 0u }; // Active entries in parameters.
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE }; // Validated persisted flags.
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
    INVALID_STRING_CHUNK,  // MTST descriptor or size is invalid.
    INVALID_METADATA,      // Header or fixed records are malformed.
    INVALID_FLAGS,         // Unknown material flag bits are set.
    TEXTURE_LIMIT_EXCEEDED,// Texture binding count exceeds the fixed contract.
    PARAMETER_LIMIT_EXCEEDED,// Parameter count exceeds the fixed contract.
    INVALID_SHADER_PATH,   // Shader reference is not a canonical .cyshader path.
    INVALID_TEXTURE,       // Texture binding or .cytex reference is invalid.
    INVALID_PARAMETER,     // Parameter name, type, or component count is invalid.
    INVALID_STRING,        // String offset, length, terminator, or table order failed.
    DUPLICATE_NAME,        // Sorted binding or parameter names are duplicated.
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
    usize iTexture{ CY_INVALID_SIZE };   // First offending texture binding.
    usize iParameter{ CY_INVALID_SIZE }; // First offending parameter.
    usize iChunk{ CY_INVALID_SIZE };     // First offending CYRS chunk.
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
