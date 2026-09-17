//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedShader.h
//  Purpose: Declares the backend-neutral cooked shader resource contract.
//  Details: CYSH stores validated stage code plus a backend-neutral logical
//           interface with stable binding identities. Runtime views borrow
//           immutable file bytes; native GPU objects remain renderer-owned.
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
inline constexpr fourcc_t CY_COOKED_SHADER_REFLECTION_CHUNK =
    Cy_MakeFourCC( 'S', 'H', 'R', 'F' ); // Canonical logical binding records.
inline constexpr fourcc_t CY_COOKED_SHADER_STRING_CHUNK =
    Cy_MakeFourCC( 'S', 'H', 'S', 'T' ); // Canonical binding-name string table.
inline constexpr fourcc_t CY_COOKED_SHADER_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'S', 'H', 'D' ); // Signature inside SHMD.
inline constexpr fourcc_t CY_COOKED_SHADER_REFLECTION_MAGIC =
    Cy_MakeFourCC( 'C', 'S', 'R', 'F' ); // Signature inside SHRF.

inline constexpr format_version_t CY_COOKED_SHADER_RESOURCE_VERSION_V2 = 2u;
inline constexpr format_version_t CY_COOKED_SHADER_RESOURCE_VERSION_V3 = 3u;
inline constexpr format_version_t CY_COOKED_SHADER_RESOURCE_VERSION_CURRENT =
    CY_COOKED_SHADER_RESOURCE_VERSION_V3;
static_assert(
    CY_RENDER_SHADER_RESOURCE_VERSION ==
        CY_COOKED_SHADER_RESOURCE_VERSION_CURRENT,
    "CYSH writer and shared render-resource versions must match" );
inline constexpr format_version_t CY_COOKED_SHADER_METADATA_VERSION = 2u; // V2 SHMD.
inline constexpr format_version_t CY_COOKED_SHADER_METADATA_VERSION_V3 = 3u;
inline constexpr format_version_t CY_COOKED_SHADER_REFLECTION_VERSION = 1u;
inline constexpr usize CY_COOKED_SHADER_METADATA_HEADER_SIZE = 40u; // V2 bytes.
inline constexpr usize CY_COOKED_SHADER_METADATA_HEADER_SIZE_V3 = 72u;
inline constexpr usize CY_COOKED_SHADER_STAGE_RECORD_SIZE = 24u; // Per-stage bytes.
inline constexpr usize CY_COOKED_SHADER_REFLECTION_HEADER_SIZE = 32u;
inline constexpr usize CY_COOKED_SHADER_BINDING_RECORD_SIZE = 56u;
inline constexpr u32 CY_COOKED_SHADER_MAX_STAGES = 2u; // Current graphics stage set.
inline constexpr u32 CY_COOKED_SHADER_MAX_BINDINGS = 128u;
inline constexpr u32 CY_COOKED_SHADER_MAX_BINDING_ARRAY_COUNT = 1024u;
inline constexpr usize CY_COOKED_SHADER_MAX_BINDING_NAME_LENGTH = 127u;
inline constexpr usize CY_COOKED_SHADER_MAX_STRING_TABLE_SIZE = 64u * CY_KIB;
inline constexpr u64 CY_COOKED_SHADER_MAX_CODE_SIZE = 16u * CY_MIB; // Per stage.
inline constexpr u32 CY_COOKED_SHADER_METADATA_ALIGNMENT = 8u; // SHMD alignment.
inline constexpr u32 CY_COOKED_SHADER_REFLECTION_ALIGNMENT = 8u; // SHRF alignment.
inline constexpr u32 CY_COOKED_SHADER_STRING_ALIGNMENT = 1u; // SHST alignment.
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
    GLSL_UTF8 = 1u // Validated UTF-8 GLSL followed by one NUL, included in code size.
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

// Logical interface values remain independent from backend-native enums.
enum class render_shader_binding_kind_t : u32 {
    VALUE = 1u,
    SAMPLED_TEXTURE,
    SAMPLER,
    UNIFORM_BUFFER,
    STORAGE_BUFFER,
    STORAGE_IMAGE,
    VERTEX_INPUT,
    FRAGMENT_OUTPUT
};

enum class render_shader_value_type_t : u32 {
    NONE = 0u,
    BOOL,
    I32,
    U32,
    F32,
    F64,
    I32X2,
    I32X3,
    I32X4,
    U32X2,
    U32X3,
    U32X4,
    F32X2,
    F32X3,
    F32X4,
    F32X3X3,
    F32X4X4
};

enum class render_shader_resource_type_t : u32 {
    NONE = 0u,
    TEXTURE_1D,
    TEXTURE_2D,
    TEXTURE_3D,
    TEXTURE_CUBE,
    TEXTURE_1D_ARRAY,
    TEXTURE_2D_ARRAY,
    TEXTURE_CUBE_ARRAY,
    SAMPLER,
    SAMPLER_COMPARISON,
    UNIFORM_BUFFER,
    STORAGE_BUFFER,
    STORAGE_IMAGE_1D,
    STORAGE_IMAGE_2D,
    STORAGE_IMAGE_3D
};

enum render_shader_stage_mask_t : flags32_t {
    RENDER_SHADER_STAGE_MASK_NONE = 0u,
    RENDER_SHADER_STAGE_MASK_VERTEX = CYPHER_BIT32( 0 ),
    RENDER_SHADER_STAGE_MASK_FRAGMENT = CYPHER_BIT32( 1 ),
    RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS =
        RENDER_SHADER_STAGE_MASK_VERTEX |
        RENDER_SHADER_STAGE_MASK_FRAGMENT
};

enum cooked_shader_binding_flags_t : flags32_t {
    COOKED_SHADER_BINDING_FLAG_NONE = 0u,
    COOKED_SHADER_BINDING_FLAG_REQUIRED = CYPHER_BIT32( 0 ),
    COOKED_SHADER_BINDING_FLAG_MATERIAL = CYPHER_BIT32( 1 ),
    COOKED_SHADER_BINDING_FLAG_INSTANCE = CYPHER_BIT32( 2 ),
    COOKED_SHADER_BINDING_FLAG_READ_ONLY = CYPHER_BIT32( 3 )
};

struct cooked_shader_binding_source_t {
    string_view_t name{}; // Logical interface name; canonical writer sort key.
    render_shader_binding_kind_t kind{
        render_shader_binding_kind_t::VALUE
    };
    render_shader_value_type_t valueType{
        render_shader_value_type_t::NONE
    };
    render_shader_resource_type_t resourceType{
        render_shader_resource_type_t::NONE
    };
    u32 nArrayElements{ 1u };
    flags32_t stageMask{ RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS };
    flags32_t flags{ COOKED_SHADER_BINDING_FLAG_NONE };
    u64 nLogicalBinding{ 0u }; // Stable domain-tagged name identifier.
    u32 iByteOffset{ 0u };    // Byte offset in its declared logical storage.
    u32 cbByteSize{ 0u };     // Logical storage extent; zero for opaque resources.
};

struct cooked_shader_interface_source_t {
    span_t<const cooked_shader_binding_source_t> bindings{};
};

struct cooked_shader_binding_view_t {
    string_view_t name{}; // Borrowed from the validated SHST payload.
    render_shader_binding_kind_t kind{
        render_shader_binding_kind_t::VALUE
    };
    render_shader_value_type_t valueType{
        render_shader_value_type_t::NONE
    };
    render_shader_resource_type_t resourceType{
        render_shader_resource_type_t::NONE
    };
    u32 nArrayElements{ 0u };
    flags32_t stageMask{ RENDER_SHADER_STAGE_MASK_NONE };
    flags32_t flags{ COOKED_SHADER_BINDING_FLAG_NONE };
    u64 nLogicalBinding{ 0u };
    u32 iByteOffset{ 0u };
    u32 cbByteSize{ 0u };
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
    format_version_t nResourceVersion{ 0u }; // CYSH payload contract version.
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
    content_hash_t interfaceHash{}; // V3 canonical SHRF + SHST identity.
    cooked_shader_stage_view_t stages[CY_COOKED_SHADER_MAX_STAGES]{};
    cooked_shader_binding_view_t bindings[CY_COOKED_SHADER_MAX_BINDINGS]{};
    u32 nStages{ 0u }; // Active entries in stages.
    u32 nBindings{ 0u }; // Active V3 logical interface entries; zero for V2.
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
    INVALID_REFLECTION_CHUNK,// SHRF descriptor or payload is invalid.
    INVALID_STRING_CHUNK,  // SHST descriptor or payload is invalid.
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
    BINDING_LIMIT_EXCEEDED,// Reflection binding count exceeds the V3 bound.
    INVALID_BINDING,      // Binding name, type, mask, flags, or byte range failed.
    DUPLICATE_BINDING_NAME,// Canonical interface names are not unique.
    DUPLICATE_BINDING_ID, // Logical binding identifiers are not unique.
    INVALID_INTERFACE_HASH,// Stored V3 interface identity does not match payloads.
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
    usize iBinding{ CY_INVALID_SIZE }; // First offending interface binding.
    usize iChunk{ CY_INVALID_SIZE }; // First offending CYRS chunk, when known.
};

// Returns the exact metadata payload size, or zero for an invalid stage count.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedShader_MetadataSize( u32 nStages ) noexcept;

// Returns the exact V3 SHMD size, including its stage records.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedShader_MetadataSizeV3( u32 nStages ) noexcept;

// Returns the exact V3 SHRF payload size, or zero when count exceeds its bound.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedShader_ReflectionSize( u32 nBindings ) noexcept;

// Returns the canonical CYRS file size for prepared stage code, or zero when
// the stage inputs cannot be represented by the current cooked shader version.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedShader_RequiredSize(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages ) noexcept;

// Returns the canonical CYSH V3 size for prepared stages and logical bindings.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedShader_RequiredSizeV3(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages,
    const cooked_shader_interface_source_t &shaderInterface ) noexcept;

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

// Packages stages plus one canonical logical interface into a CYSH V3 file.
CYPHER_NODISCARD CYPHER_COMMON_API
cooked_shader_result_t CookedShader_WriteV3(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages,
    const cooked_shader_interface_source_t &shaderInterface,
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
const cooked_shader_binding_view_t *CookedShader_FindBinding(
    const cooked_shader_view_t &shader,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const cooked_shader_binding_view_t *CookedShader_FindBindingById(
    const cooked_shader_view_t &shader,
    u64 nLogicalBinding ) noexcept;

// Derives the persisted nonzero V1 binding identity from one validated logical
// name. Writers require the supplied binding ID to match this exact function.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedShader_MakeLogicalBindingId(
    string_view_t name,
    u64 *pBindingIdOut ) noexcept;

// Describes the backend-neutral material-constant packing contract shared by
// shader and material cookers. Value size excludes trailing layout padding;
// storage size includes it (for example vec3=12/16 and mat3=36/48).
CYPHER_NODISCARD CYPHER_COMMON_API
u32 CookedShader_ValueTypeValueSize(
    render_shader_value_type_t type ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
u32 CookedShader_ValueTypeStorageAlignment(
    render_shader_value_type_t type ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
u32 CookedShader_ValueTypeStorageSize(
    render_shader_value_type_t type ) noexcept;

// Computes the exact V3 interface identity without serializing a CYSH file.
// Input order is irrelevant; the function hashes canonical name-sorted bytes.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedShader_ComputeInterfaceHash(
    const cooked_shader_interface_source_t &shaderInterface,
    content_hash_t *pInterfaceHashOut ) noexcept;

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
