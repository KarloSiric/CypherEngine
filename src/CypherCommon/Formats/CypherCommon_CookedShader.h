//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedShader.h
//  Purpose: Declares the backend-neutral cooked shader resource contract.
//  Details: A CYSH resource stores one target backend, one program kind, and
//           bounded stage-code chunks. Runtime views borrow immutable file bytes;
//           native GPU object creation remains renderer-owned.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_FORMATS_COOKEDSHADER_H
#define CYPHER_COMMON_FORMATS_COOKEDSHADER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CookedResource.h"
#include "CypherCommon_RenderFormat.h"

namespace cypher::common
{

inline constexpr fourcc_t CY_COOKED_SHADER_METADATA_CHUNK =
    Cy_MakeFourCC( 'S', 'H', 'M', 'D' );
inline constexpr fourcc_t CY_COOKED_SHADER_CODE_CHUNK =
    Cy_MakeFourCC( 'S', 'H', 'C', 'D' );
inline constexpr fourcc_t CY_COOKED_SHADER_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'S', 'H', 'D' );

inline constexpr format_version_t CY_COOKED_SHADER_METADATA_VERSION = 2u;
inline constexpr usize CY_COOKED_SHADER_METADATA_HEADER_SIZE = 40u;
inline constexpr usize CY_COOKED_SHADER_STAGE_RECORD_SIZE = 24u;
inline constexpr u32 CY_COOKED_SHADER_MAX_STAGES = 2u;
inline constexpr u64 CY_COOKED_SHADER_MAX_CODE_SIZE = 16u * CY_MIB;
inline constexpr u32 CY_COOKED_SHADER_METADATA_ALIGNMENT = 8u;
inline constexpr u32 CY_COOKED_SHADER_CODE_ALIGNMENT = 4u;

// Numeric values in these enums are serialized and therefore versioned.
enum class render_shader_backend_t : u32 {
    OPENGL = 1u
};

enum class render_shader_program_kind_t : u32 {
    GRAPHICS = 1u
};

enum class render_shader_stage_t : u32 {
    VERTEX = 1u,
    FRAGMENT = 2u
};

enum class render_shader_code_format_t : u32 {
    GLSL_UTF8 = 1u
};

// The language profile and version are runtime compatibility requirements.
// They are serialized independently from the backend so a loader can reject an
// unsupported shader before asking a graphics driver to compile it.
enum class render_shader_language_profile_t : u32 {
    GLSL_CORE = 1u
};

enum cooked_shader_flags_t : flags32_t {
    COOKED_SHADER_FLAG_NONE = 0u
};

enum cooked_shader_stage_flags_t : flags32_t {
    COOKED_SHADER_STAGE_FLAG_NONE = 0u
};

struct cooked_shader_desc_t {
    render_shader_backend_t backend{ render_shader_backend_t::OPENGL };
    render_shader_program_kind_t kind{
        render_shader_program_kind_t::GRAPHICS
    };
    render_shader_language_profile_t languageProfile{
        render_shader_language_profile_t::GLSL_CORE
    };
    u32 nLanguageVersion{ 410u };
    flags32_t flags{ COOKED_SHADER_FLAG_NONE };
};

struct cooked_shader_stage_desc_t {
    render_shader_stage_t stage{ render_shader_stage_t::VERTEX };
    render_shader_code_format_t codeFormat{
        render_shader_code_format_t::GLSL_UTF8
    };
    flags32_t flags{ COOKED_SHADER_STAGE_FLAG_NONE };
    u32 iCodeChunk{ 0u };
    u64 cbCode{ 0u };
};

struct cooked_shader_stage_source_t {
    render_shader_stage_t stage{ render_shader_stage_t::VERTEX };
    render_shader_code_format_t codeFormat{
        render_shader_code_format_t::GLSL_UTF8
    };
    flags32_t flags{ COOKED_SHADER_STAGE_FLAG_NONE };
    binary_block_t code{};
};

struct cooked_shader_stage_view_t {
    render_shader_stage_t stage{ render_shader_stage_t::VERTEX };
    render_shader_code_format_t codeFormat{
        render_shader_code_format_t::GLSL_UTF8
    };
    flags32_t flags{ COOKED_SHADER_STAGE_FLAG_NONE };
    binary_block_t code{};
    content_hash_t contentHash{};
};

struct cooked_shader_view_t {
    render_shader_backend_t backend{ render_shader_backend_t::OPENGL };
    render_shader_program_kind_t kind{
        render_shader_program_kind_t::GRAPHICS
    };
    render_shader_language_profile_t languageProfile{
        render_shader_language_profile_t::GLSL_CORE
    };
    u32 nLanguageVersion{ 0u };
    flags32_t flags{ COOKED_SHADER_FLAG_NONE };
    content_hash_t sourceHash{};
    cooked_shader_stage_view_t stages[CY_COOKED_SHADER_MAX_STAGES]{};
    u32 nStages{ 0u };
};

enum class cooked_shader_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUTPUT_TOO_SMALL,
    RESOURCE_ERROR,
    INVALID_RESOURCE_TYPE,
    VERSION_MISMATCH,
    INVALID_CHUNK_COUNT,
    INVALID_METADATA_CHUNK,
    INVALID_METADATA,
    INVALID_BACKEND,
    INVALID_PROGRAM_KIND,
    INVALID_LANGUAGE_PROFILE,
    INVALID_LANGUAGE_VERSION,
    INVALID_FLAGS,
    STAGE_LIMIT_EXCEEDED,
    INVALID_STAGE,
    DUPLICATE_STAGE,
    INVALID_STAGE_SET,
    INVALID_CODE_CHUNK,
    INVALID_CODE,
    CONTENT_HASH_MISMATCH,
    NON_CANONICAL_LAYOUT
};

struct cooked_shader_result_t {
    cooked_shader_status_t status{ cooked_shader_status_t::OK };
    cooked_resource_status_t resourceStatus{
        cooked_resource_status_t::OK
    };
    usize cbRead{ 0u };
    usize cbWritten{ 0u };
    usize cbRequired{ 0u };
    usize iStage{ CY_INVALID_SIZE };
    usize iChunk{ CY_INVALID_SIZE };
};

// Returns the exact metadata payload size, or zero for an invalid stage count.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedShader_MetadataSize( u32 nStages ) noexcept;

// Returns the canonical CYRS file size for prepared stage code, or zero when
// the stage inputs cannot be represented by the current cooked shader version.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedShader_RequiredSize(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages ) noexcept;

// Serializes only the SHMD payload. The shader compiler owns CYRS layout, code
// chunks, dependency collection, preprocessing, and deterministic file output.
CYPHER_NODISCARD CYPHER_COMMON_API
cooked_shader_result_t CookedShader_WriteMetadata(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_desc_t> stages,
    byte_span_t output ) noexcept;

// Packages already prepared stage code into one deterministic CYSH/CYRS file.
// Shader preprocessing or compilation is deliberately outside this function.
CYPHER_NODISCARD CYPHER_COMMON_API
cooked_shader_result_t CookedShader_Write(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept;

// Validates a complete CYSH/CYRS file and returns zero-copy views of stage bytes.
// The source file must remain alive and unchanged while the view is in use.
CYPHER_NODISCARD CYPHER_COMMON_API
cooked_shader_result_t CookedShader_Read(
    binary_block_t input,
    cooked_shader_view_t *pShaderOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const cooked_shader_stage_view_t *CookedShader_FindStage(
    const cooked_shader_view_t &shader,
    render_shader_stage_t stage ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedShader_Succeeded(
    const cooked_shader_result_t &result ) noexcept;

// Reports whether the current cooked contract can represent this source
// language profile/version pair. Target-platform limits remain cooker policy.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedShader_SupportsLanguage(
    render_shader_language_profile_t profile,
    u32 nVersion ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CookedShader_StatusName(
    cooked_shader_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_COOKEDSHADER_H
