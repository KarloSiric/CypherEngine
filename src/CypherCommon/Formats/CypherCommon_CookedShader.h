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
    Cy_MakeFourCC( 'S', 'H', 'M', 'D' ); // Program and stage descriptor payload.
inline constexpr fourcc_t CY_COOKED_SHADER_CODE_CHUNK =
    Cy_MakeFourCC( 'S', 'H', 'C', 'D' ); // One prepared source/code payload per stage.
inline constexpr fourcc_t CY_COOKED_SHADER_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'S', 'H', 'D' ); // Signature inside SHMD.

inline constexpr format_version_t CY_COOKED_SHADER_METADATA_VERSION = 2u; // SHMD layout.
inline constexpr usize CY_COOKED_SHADER_METADATA_HEADER_SIZE = 40u; // Fixed bytes.
inline constexpr usize CY_COOKED_SHADER_STAGE_RECORD_SIZE = 24u; // Per-stage bytes.
inline constexpr u32 CY_COOKED_SHADER_MAX_STAGES = 2u; // V1 graphics stage set.
inline constexpr u64 CY_COOKED_SHADER_MAX_CODE_SIZE = 16u * CY_MIB; // Per stage.
inline constexpr u32 CY_COOKED_SHADER_METADATA_ALIGNMENT = 8u; // SHMD alignment.
inline constexpr u32 CY_COOKED_SHADER_CODE_ALIGNMENT = 4u; // SHCD alignment.

// Numeric values in these enums are serialized and therefore versioned.
enum class render_shader_backend_t : u32 {
    OPENGL = 1u // OpenGL runtime backend.
};

enum class render_shader_program_kind_t : u32 {
    GRAPHICS = 1u // Linked vertex and fragment graphics program.
};

enum class render_shader_stage_t : u32 {
    VERTEX = 1u,  // Per-vertex stage.
    FRAGMENT = 2u // Per-fragment stage.
};

enum class render_shader_code_format_t : u32 {
    GLSL_UTF8 = 1u // Validated UTF-8 GLSL source without a required NUL.
};

// The language profile and version are runtime compatibility requirements.
// They are serialized independently from the backend so a loader can reject an
// unsupported shader before asking a graphics driver to compile it.
enum class render_shader_language_profile_t : u32 {
    GLSL_CORE = 1u // Desktop OpenGL core language profile.
};

enum cooked_shader_flags_t : flags32_t {
    COOKED_SHADER_FLAG_NONE = 0u
};

enum cooked_shader_stage_flags_t : flags32_t {
    COOKED_SHADER_STAGE_FLAG_NONE = 0u
};

struct cooked_shader_desc_t {
    render_shader_backend_t backend{ render_shader_backend_t::OPENGL }; // Runtime target.
    render_shader_program_kind_t kind{
        render_shader_program_kind_t::GRAPHICS
    };
    render_shader_language_profile_t languageProfile{
        render_shader_language_profile_t::GLSL_CORE
    };
    u32 nLanguageVersion{ 410u }; // GLSL integer version, for example 410 or 460.
    flags32_t flags{ COOKED_SHADER_FLAG_NONE }; // Program-wide persisted flags.
};

struct cooked_shader_stage_desc_t {
    render_shader_stage_t stage{ render_shader_stage_t::VERTEX };
    render_shader_code_format_t codeFormat{
        render_shader_code_format_t::GLSL_UTF8
    };
    flags32_t flags{ COOKED_SHADER_STAGE_FLAG_NONE }; // Stage-specific persisted flags.
    u32 iCodeChunk{ 0u }; // CYRS table index containing this stage's bytes.
    u64 cbCode{ 0u };     // Exact stage payload size.
};

struct cooked_shader_stage_source_t {
    render_shader_stage_t stage{ render_shader_stage_t::VERTEX };
    render_shader_code_format_t codeFormat{
        render_shader_code_format_t::GLSL_UTF8
    };
    flags32_t flags{ COOKED_SHADER_STAGE_FLAG_NONE }; // Stage flags to serialize.
    binary_block_t code{}; // Borrowed prepared stage bytes copied by the writer.
};

struct cooked_shader_stage_view_t {
    render_shader_stage_t stage{ render_shader_stage_t::VERTEX };
    render_shader_code_format_t codeFormat{
        render_shader_code_format_t::GLSL_UTF8
    };
    flags32_t flags{ COOKED_SHADER_STAGE_FLAG_NONE }; // Validated stage flags.
    binary_block_t code{}; // Immutable view into source CYRS bytes.
    content_hash_t contentHash{}; // Verified stage payload identity.
};

struct cooked_shader_view_t {
    render_shader_backend_t backend{ render_shader_backend_t::OPENGL };
    render_shader_program_kind_t kind{
        render_shader_program_kind_t::GRAPHICS
    };
    render_shader_language_profile_t languageProfile{
        render_shader_language_profile_t::GLSL_CORE
    };
    u32 nLanguageVersion{ 0u }; // Required GLSL language version.
    flags32_t flags{ COOKED_SHADER_FLAG_NONE }; // Program-wide flags.
    content_hash_t sourceHash{}; // Optional authored-source identity.
    cooked_shader_stage_view_t stages[CY_COOKED_SHADER_MAX_STAGES]{};
    u32 nStages{ 0u }; // Active entries in stages.
};

enum class cooked_shader_status_t : u8 {
    OK = 0u,               // Shader operation completed.
    INVALID_ARGUMENT,     // Input, span, output, or aliasing contract is invalid.
    OUTPUT_TOO_SMALL,     // Destination cannot hold canonical output.
    RESOURCE_ERROR,       // Underlying CYRS validation or writing failed.
    INVALID_RESOURCE_TYPE,// CYRS payload is not a cooked shader.
    VERSION_MISMATCH,     // Cooked shader resource version is unsupported.
    INVALID_CHUNK_COUNT,  // Metadata/stage count and CYRS chunks disagree.
    INVALID_METADATA_CHUNK,// SHMD descriptor violates the format contract.
    INVALID_METADATA,     // SHMD header or stage records are malformed.
    INVALID_BACKEND,      // Persisted runtime backend is unsupported.
    INVALID_PROGRAM_KIND, // Persisted program kind is unsupported.
    INVALID_LANGUAGE_PROFILE,// Source language profile is unsupported.
    INVALID_LANGUAGE_VERSION,// GLSL version is outside the accepted core range.
    INVALID_FLAGS,        // Program or stage contains unknown flag bits.
    STAGE_LIMIT_EXCEEDED, // Stage count is zero or exceeds the V2 limit.
    INVALID_STAGE,        // Stage enum or code format is invalid.
    DUPLICATE_STAGE,      // More than one record declares the same stage.
    INVALID_STAGE_SET,    // Program lacks its required vertex/fragment pair.
    INVALID_CODE_CHUNK,   // Stage descriptor and SHCD chunk disagree.
    INVALID_CODE,         // GLSL bytes fail NUL or UTF-8 validation.
    CONTENT_HASH_MISMATCH,// Metadata or code payload hash failed.
    NON_CANONICAL_LAYOUT  // Valid data is not in the required deterministic order.
};

struct cooked_shader_result_t {
    cooked_shader_status_t status{ cooked_shader_status_t::OK }; // Shader result.
    cooked_resource_status_t resourceStatus{
        cooked_resource_status_t::OK
    }; // Underlying CYRS result when status is RESOURCE_ERROR.
    usize cbRead{ 0u };              // Validated source bytes.
    usize cbWritten{ 0u };           // Published cooked bytes.
    usize cbRequired{ 0u };          // Exact output capacity required.
    usize iStage{ CY_INVALID_SIZE }; // First offending stage, when known.
    usize iChunk{ CY_INVALID_SIZE }; // First offending CYRS chunk, when known.
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
