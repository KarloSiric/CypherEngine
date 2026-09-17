//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedShader.cpp
//  Purpose: Implements the backend-neutral cooked shader resource contract.
//  Details: Writers produce a canonical CYRS layout from prepared stage bytes.
//           Readers validate the complete resource, metadata, stage set, hashes,
//           and code representation before publishing borrowed runtime views.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedShader.h"

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"
#include "CypherCommon_HashFNV.h"
#include "CypherCommon_MemoryOps.h"
#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

inline constexpr u32 CY_COOKED_SHADER_KNOWN_FLAGS =
    COOKED_SHADER_FLAG_NONE; // Unknown program flags are rejected by V2 readers.
inline constexpr u32 CY_COOKED_SHADER_KNOWN_STAGE_FLAGS =
    COOKED_SHADER_STAGE_FLAG_NONE; // Unknown stage flags are rejected by V2 readers.
inline constexpr u32 CY_COOKED_SHADER_KNOWN_BINDING_FLAGS =
    COOKED_SHADER_BINDING_FLAG_REQUIRED |
    COOKED_SHADER_BINDING_FLAG_MATERIAL |
    COOKED_SHADER_BINDING_FLAG_INSTANCE |
    COOKED_SHADER_BINDING_FLAG_READ_ONLY;
inline constexpr u32 CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK = 3u;
inline constexpr u32 CY_COOKED_SHADER_V3_CHUNK_COUNT =
    CY_COOKED_SHADER_MAX_STAGES + CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK;

struct cooked_shader_metadata_t {
    cooked_shader_desc_t shader{}; // Fixed SHMD program header.
    u32 nStages{ 0u };             // Number of following stage records.
    u32 nBindings{ 0u };           // V3 SHRF binding records.
    u32 iReflectionChunk{ 0u };    // V3 SHRF chunk index.
    u32 iStringChunk{ 0u };        // V3 SHST chunk index.
    u32 cbStringTable{ 0u };       // Exact V3 SHST payload bytes.
    content_hash_t interfaceHash{}; // V3 canonical interface identity.
};

struct cooked_shader_binding_record_t {
    u32 iName{ 0u }; // Byte offset into SHST.
    u32 cchName{ 0u }; // Bytes excluding the NUL terminator.
    cooked_shader_binding_view_t binding{};
};

struct canonical_shader_interface_t {
    cooked_shader_binding_source_t bindings[CY_COOKED_SHADER_MAX_BINDINGS]{};
    usize nBindings{ 0u };
    usize cbStrings{ 1u }; // SHST byte zero is the canonical empty-string sentinel.
};

CYPHER_NODISCARD bool_t CheckedAdd(
    usize left,
    usize right,
    usize &valueOut ) noexcept
{
    if ( right > CY_USIZE_MAX - left ) {
        valueOut = 0u;
        return CY_FALSE;
    }
    valueOut = left + right;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t CheckedMul(
    usize left,
    usize right,
    usize &valueOut ) noexcept
{
    if ( left != 0u && right > CY_USIZE_MAX / left ) {
        valueOut = 0u;
        return CY_FALSE;
    }
    valueOut = left * right;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t CheckedAddU32(
    u32 left,
    u32 right ) noexcept
{
    return right <= CY_U32_MAX - left;
}

CYPHER_NODISCARD bool_t IsLogicalNameValid(
    string_view_t name ) noexcept
{
    if ( !StringView_IsValid( name ) || name.cchLength == 0u ||
         name.cchLength > CY_COOKED_SHADER_MAX_BINDING_NAME_LENGTH ) {
        return CY_FALSE;
    }
    const auto IsAlpha = []( char value ) noexcept {
        return ( value >= 'a' && value <= 'z' ) ||
               ( value >= 'A' && value <= 'Z' );
    };
    const auto IsDigit = []( char value ) noexcept {
        return value >= '0' && value <= '9';
    };
    if ( !IsAlpha( name.pData[0] ) && name.pData[0] != '_' ) {
        return CY_FALSE;
    }
    for ( usize iChar = 1u; iChar < name.cchLength; ++iChar ) {
        const char value = name.pData[iChar];
        if ( !IsAlpha( value ) && !IsDigit( value ) && value != '_' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsZeroRange(
    binary_block_t input,
    usize iBegin,
    usize iEnd ) noexcept
{
    if ( iBegin > iEnd || iEnd > input.cbSize ) {
        return CY_FALSE;
    }
    return iBegin == iEnd ||
           Cy_MemIsZero( input.pData + iBegin, iEnd - iBegin );
}

CYPHER_NODISCARD bool_t IsBackendValid(
    render_shader_backend_t backend ) noexcept
{
    return backend == render_shader_backend_t::OPENGL;
}

CYPHER_NODISCARD bool_t IsProgramKindValid(
    render_shader_program_kind_t kind ) noexcept
{
    return kind == render_shader_program_kind_t::GRAPHICS;
}

CYPHER_NODISCARD bool_t IsStageValid(
    render_shader_stage_t stage ) noexcept
{
    return stage == render_shader_stage_t::VERTEX ||
           stage == render_shader_stage_t::FRAGMENT;
}

CYPHER_NODISCARD bool_t IsCodeFormatValid(
    render_shader_code_format_t format ) noexcept
{
    return format == render_shader_code_format_t::GLSL_UTF8;
}

CYPHER_NODISCARD bool_t IsLanguageProfileValid(
    render_shader_language_profile_t profile ) noexcept
{
    return profile == render_shader_language_profile_t::GLSL_CORE;
}

CYPHER_NODISCARD bool_t IsGlslCoreVersionValid( u32 nVersion ) noexcept
{
    // V2 accepts desktop core GLSL 3.30 through 4.60 in ten-point revisions.
    switch ( nVersion ) {
        case 330u:
        case 400u:
        case 410u:
        case 420u:
        case 430u:
        case 440u:
        case 450u:
        case 460u:
            return CY_TRUE;
        default:
            return CY_FALSE;
    }
}

CYPHER_NODISCARD u32 StageBit( render_shader_stage_t stage ) noexcept
{
    // Persisted stage values begin at one, leaving bit zero for VERTEX.
    return IsStageValid( stage )
        ? CYPHER_BIT32( static_cast<u32>( stage ) - 1u )
        : 0u;
}

CYPHER_NODISCARD bool_t IsBindingKindValid(
    render_shader_binding_kind_t kind ) noexcept
{
    return kind >= render_shader_binding_kind_t::VALUE &&
           kind <= render_shader_binding_kind_t::FRAGMENT_OUTPUT;
}

CYPHER_NODISCARD bool_t IsValueTypeValid(
    render_shader_value_type_t type ) noexcept
{
    return type >= render_shader_value_type_t::BOOL &&
           type <= render_shader_value_type_t::F32X4X4;
}

CYPHER_NODISCARD bool_t IsResourceTypeValid(
    render_shader_resource_type_t type ) noexcept
{
    return type >= render_shader_resource_type_t::TEXTURE_1D &&
           type <= render_shader_resource_type_t::STORAGE_IMAGE_3D;
}

CYPHER_NODISCARD bool_t IsTextureResourceType(
    render_shader_resource_type_t type ) noexcept
{
    return type >= render_shader_resource_type_t::TEXTURE_1D &&
           type <= render_shader_resource_type_t::TEXTURE_CUBE_ARRAY;
}

CYPHER_NODISCARD bool_t IsSamplerResourceType(
    render_shader_resource_type_t type ) noexcept
{
    return type == render_shader_resource_type_t::SAMPLER ||
           type == render_shader_resource_type_t::SAMPLER_COMPARISON;
}

CYPHER_NODISCARD bool_t IsStorageImageResourceType(
    render_shader_resource_type_t type ) noexcept
{
    return type >= render_shader_resource_type_t::STORAGE_IMAGE_1D &&
           type <= render_shader_resource_type_t::STORAGE_IMAGE_3D;
}

CYPHER_NODISCARD u32 ValueTypeSize(
    render_shader_value_type_t type ) noexcept
{
    switch ( type ) {
        case render_shader_value_type_t::BOOL:
        case render_shader_value_type_t::I32:
        case render_shader_value_type_t::U32:
        case render_shader_value_type_t::F32: return 4u;
        case render_shader_value_type_t::F64:
        case render_shader_value_type_t::I32X2:
        case render_shader_value_type_t::U32X2:
        case render_shader_value_type_t::F32X2: return 8u;
        case render_shader_value_type_t::I32X3:
        case render_shader_value_type_t::U32X3:
        case render_shader_value_type_t::F32X3: return 12u;
        case render_shader_value_type_t::I32X4:
        case render_shader_value_type_t::U32X4:
        case render_shader_value_type_t::F32X4: return 16u;
        case render_shader_value_type_t::F32X3X3: return 36u;
        case render_shader_value_type_t::F32X4X4: return 64u;
        case render_shader_value_type_t::NONE: return 0u;
    }
    return 0u;
}

CYPHER_NODISCARD cooked_shader_status_t ValidateBinding(
    const cooked_shader_binding_source_t &binding ) noexcept
{
    u64 expectedBindingId = 0u;
    if ( !IsLogicalNameValid( binding.name ) ||
         !CookedShader_MakeLogicalBindingId(
             binding.name,
             &expectedBindingId ) ||
         binding.nLogicalBinding != expectedBindingId ||
         !IsBindingKindValid( binding.kind ) ||
         binding.nArrayElements == 0u ||
         binding.nArrayElements > CY_COOKED_SHADER_MAX_BINDING_ARRAY_COUNT ||
         binding.stageMask == RENDER_SHADER_STAGE_MASK_NONE ||
         ( binding.stageMask & ~RENDER_SHADER_STAGE_MASK_ALL_GRAPHICS ) != 0u ||
         ( binding.flags & ~CY_COOKED_SHADER_KNOWN_BINDING_FLAGS ) != 0u ||
         ( ( binding.flags & COOKED_SHADER_BINDING_FLAG_MATERIAL ) != 0u &&
           ( binding.flags & COOKED_SHADER_BINDING_FLAG_INSTANCE ) != 0u ) ||
         !CheckedAddU32( binding.iByteOffset, binding.cbByteSize ) ) {
        return cooked_shader_status_t::INVALID_BINDING;
    }

    const bool_t bHasValue = IsValueTypeValid( binding.valueType );
    const bool_t bNoValue =
        binding.valueType == render_shader_value_type_t::NONE;
    const bool_t bHasResource = IsResourceTypeValid( binding.resourceType );
    const bool_t bNoResource =
        binding.resourceType == render_shader_resource_type_t::NONE;
    if ( ( !bHasValue && !bNoValue ) ||
         ( !bHasResource && !bNoResource ) ) {
        return cooked_shader_status_t::INVALID_BINDING;
    }

    switch ( binding.kind ) {
        case render_shader_binding_kind_t::VALUE:
        case render_shader_binding_kind_t::VERTEX_INPUT:
        case render_shader_binding_kind_t::FRAGMENT_OUTPUT: {
            if ( !bHasValue || !bNoResource ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            const u32 cbElement = ValueTypeSize( binding.valueType );
            if ( cbElement == 0u ||
                 binding.nArrayElements > CY_U32_MAX / cbElement ||
                 binding.cbByteSize < cbElement * binding.nArrayElements ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            if ( binding.kind == render_shader_binding_kind_t::VALUE &&
                 ( binding.flags & COOKED_SHADER_BINDING_FLAG_MATERIAL ) !=
                     0u ) {
                const u32 nAlignment =
                    CookedShader_ValueTypeStorageAlignment(
                        binding.valueType );
                const u32 cbStorage = CookedShader_ValueTypeStorageSize(
                    binding.valueType );
                if ( binding.nArrayElements != 1u || nAlignment == 0u ||
                     cbStorage == 0u || binding.cbByteSize != cbStorage ||
                     ( binding.iByteOffset & ( nAlignment - 1u ) ) != 0u ) {
                    return cooked_shader_status_t::INVALID_BINDING;
                }
            }
            if ( binding.kind == render_shader_binding_kind_t::VERTEX_INPUT &&
                 binding.stageMask != RENDER_SHADER_STAGE_MASK_VERTEX ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            if ( binding.kind == render_shader_binding_kind_t::FRAGMENT_OUTPUT &&
                 binding.stageMask != RENDER_SHADER_STAGE_MASK_FRAGMENT ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            break;
        }
        case render_shader_binding_kind_t::SAMPLED_TEXTURE:
            if ( !bNoValue || !IsTextureResourceType( binding.resourceType ) ||
                 binding.iByteOffset != 0u || binding.cbByteSize != 0u ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            break;
        case render_shader_binding_kind_t::SAMPLER:
            if ( !bNoValue || !IsSamplerResourceType( binding.resourceType ) ||
                 binding.iByteOffset != 0u || binding.cbByteSize != 0u ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            break;
        case render_shader_binding_kind_t::UNIFORM_BUFFER:
            if ( !bNoValue ||
                 binding.resourceType !=
                     render_shader_resource_type_t::UNIFORM_BUFFER ||
                 binding.cbByteSize == 0u ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            break;
        case render_shader_binding_kind_t::STORAGE_BUFFER:
            if ( !bNoValue ||
                 binding.resourceType !=
                     render_shader_resource_type_t::STORAGE_BUFFER ||
                 binding.cbByteSize == 0u ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            break;
        case render_shader_binding_kind_t::STORAGE_IMAGE:
            if ( !bNoValue ||
                 !IsStorageImageResourceType( binding.resourceType ) ||
                 binding.iByteOffset != 0u || binding.cbByteSize != 0u ) {
                return cooked_shader_status_t::INVALID_BINDING;
            }
            break;
    }
    return cooked_shader_status_t::OK;
}

void SortBindings(
    cooked_shader_binding_source_t *pBindings,
    usize nBindings ) noexcept
{
    for ( usize iBinding = 1u; iBinding < nBindings; ++iBinding ) {
        const cooked_shader_binding_source_t value = pBindings[iBinding];
        usize iInsert = iBinding;
        while ( iInsert > 0u &&
                StringView_Compare(
                    value.name,
                    pBindings[iInsert - 1u].name ) < 0 ) {
            pBindings[iInsert] = pBindings[iInsert - 1u];
            --iInsert;
        }
        pBindings[iInsert] = value;
    }
}

CYPHER_NODISCARD cooked_shader_status_t CanonicalizeInterface(
    const cooked_shader_interface_source_t &shaderInterface,
    canonical_shader_interface_t &canonical,
    usize *pInvalidBinding ) noexcept
{
    if ( pInvalidBinding != nullptr ) {
        *pInvalidBinding = CY_INVALID_SIZE;
    }
    if ( !Span_IsValid( shaderInterface.bindings ) ) {
        return cooked_shader_status_t::INVALID_ARGUMENT;
    }
    if ( shaderInterface.bindings.nCount > CY_COOKED_SHADER_MAX_BINDINGS ) {
        return cooked_shader_status_t::BINDING_LIMIT_EXCEEDED;
    }

    canonical.nBindings = shaderInterface.bindings.nCount;
    canonical.cbStrings = 1u;
    for ( usize iBinding = 0u;
          iBinding < canonical.nBindings;
          ++iBinding ) {
        const cooked_shader_binding_source_t &source =
            shaderInterface.bindings.pData[iBinding];
        const cooked_shader_status_t status = ValidateBinding( source );
        if ( status != cooked_shader_status_t::OK ) {
            if ( pInvalidBinding != nullptr ) {
                *pInvalidBinding = iBinding;
            }
            return status;
        }
        canonical.bindings[iBinding] = source;
        usize cbName = 0u;
        if ( !CheckedAdd( source.name.cchLength, 1u, cbName ) ||
             !CheckedAdd(
                 canonical.cbStrings,
                 cbName,
                 canonical.cbStrings ) ||
             canonical.cbStrings > CY_COOKED_SHADER_MAX_STRING_TABLE_SIZE ) {
            if ( pInvalidBinding != nullptr ) {
                *pInvalidBinding = iBinding;
            }
            return cooked_shader_status_t::INVALID_BINDING;
        }
    }

    SortBindings( canonical.bindings, canonical.nBindings );
    for ( usize iBinding = 0u;
          iBinding < canonical.nBindings;
          ++iBinding ) {
        if ( iBinding > 0u &&
             StringView_Equals(
                 canonical.bindings[iBinding - 1u].name,
                 canonical.bindings[iBinding].name ) ) {
            if ( pInvalidBinding != nullptr ) {
                *pInvalidBinding = iBinding;
            }
            return cooked_shader_status_t::DUPLICATE_BINDING_NAME;
        }
        for ( usize iPrior = 0u; iPrior < iBinding; ++iPrior ) {
            if ( canonical.bindings[iPrior].nLogicalBinding ==
                 canonical.bindings[iBinding].nLogicalBinding ) {
                if ( pInvalidBinding != nullptr ) {
                    *pInvalidBinding = iBinding;
                }
                return cooked_shader_status_t::DUPLICATE_BINDING_ID;
            }
        }
    }
    usize iExpectedMaterialOffset = 0u;
    for ( usize iBinding = 0u;
          iBinding < canonical.nBindings;
          ++iBinding ) {
        const cooked_shader_binding_source_t &binding =
            canonical.bindings[iBinding];
        if ( binding.kind != render_shader_binding_kind_t::VALUE ||
             ( binding.flags & COOKED_SHADER_BINDING_FLAG_MATERIAL ) == 0u ) {
            continue;
        }
        const usize nAlignment =
            CookedShader_ValueTypeStorageAlignment( binding.valueType );
        usize iAlignedOffset = 0u;
        if ( !Cy_AlignUpChecked(
                 iExpectedMaterialOffset,
                 nAlignment,
                 iAlignedOffset ) ||
             binding.iByteOffset != iAlignedOffset ) {
            if ( pInvalidBinding != nullptr ) {
                *pInvalidBinding = iBinding;
            }
            return cooked_shader_status_t::INVALID_BINDING;
        }
        iExpectedMaterialOffset = iAlignedOffset + binding.cbByteSize;
    }
    return cooked_shader_status_t::OK;
}

CYPHER_NODISCARD bool_t IsStageSetValid(
    render_shader_program_kind_t kind,
    u32 stageMask,
    u32 nStages ) noexcept
{
    const u32 required = StageBit( render_shader_stage_t::VERTEX ) |
                         StageBit( render_shader_stage_t::FRAGMENT );
    return kind == render_shader_program_kind_t::GRAPHICS &&
           nStages == CY_COOKED_SHADER_MAX_STAGES && stageMask == required;
}

CYPHER_NODISCARD bool_t IsBackendFormatValid(
    render_shader_backend_t backend,
    render_shader_code_format_t format ) noexcept
{
    return backend == render_shader_backend_t::OPENGL &&
           format == render_shader_code_format_t::GLSL_UTF8;
}

CYPHER_NODISCARD cooked_shader_status_t ValidateShader(
    const cooked_shader_desc_t &shader ) noexcept
{
    if ( !IsBackendValid( shader.backend ) ) {
        return cooked_shader_status_t::INVALID_BACKEND;
    }
    if ( !IsProgramKindValid( shader.kind ) ) {
        return cooked_shader_status_t::INVALID_PROGRAM_KIND;
    }
    if ( !IsLanguageProfileValid( shader.languageProfile ) ) {
        return cooked_shader_status_t::INVALID_LANGUAGE_PROFILE;
    }
    if ( shader.languageProfile ==
             render_shader_language_profile_t::GLSL_CORE &&
         !IsGlslCoreVersionValid( shader.nLanguageVersion ) ) {
        return cooked_shader_status_t::INVALID_LANGUAGE_VERSION;
    }
    if ( ( shader.flags & ~CY_COOKED_SHADER_KNOWN_FLAGS ) != 0u ) {
        return cooked_shader_status_t::INVALID_FLAGS;
    }
    return cooked_shader_status_t::OK;
}

CYPHER_NODISCARD cooked_shader_status_t ValidateStageDescriptor(
    const cooked_shader_desc_t &shader,
    const cooked_shader_stage_desc_t &stage ) noexcept
{
    if ( !IsStageValid( stage.stage ) ||
         !IsCodeFormatValid( stage.codeFormat ) ) {
        return cooked_shader_status_t::INVALID_STAGE;
    }
    if ( ( stage.flags & ~CY_COOKED_SHADER_KNOWN_STAGE_FLAGS ) != 0u ) {
        return cooked_shader_status_t::INVALID_FLAGS;
    }
    if ( stage.iCodeChunk == 0u || stage.cbCode == 0u ||
         stage.cbCode > CY_COOKED_SHADER_MAX_CODE_SIZE ) {
        return cooked_shader_status_t::INVALID_CODE_CHUNK;
    }
    if ( !IsBackendFormatValid( shader.backend, stage.codeFormat ) ) {
        return cooked_shader_status_t::INVALID_CODE;
    }
    return cooked_shader_status_t::OK;
}

CYPHER_NODISCARD bool_t IsStageSourceRepresentable(
    const cooked_shader_stage_source_t &stage ) noexcept
{
    return IsStageValid( stage.stage ) &&
           IsCodeFormatValid( stage.codeFormat ) &&
           ( stage.flags & ~CY_COOKED_SHADER_KNOWN_STAGE_FLAGS ) == 0u &&
           BinaryBlock_IsValid( stage.code ) &&
           stage.code.cbSize > 0u &&
           stage.code.cbSize <= CY_COOKED_SHADER_MAX_CODE_SIZE;
}

CYPHER_NODISCARD cooked_shader_status_t ValidateStageDescriptors(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_desc_t> stages,
    u32 iFirstCodeChunk,
    usize *pInvalidStage ) noexcept
{
    if ( pInvalidStage != nullptr ) {
        *pInvalidStage = CY_INVALID_SIZE;
    }
    if ( !Span_IsValid( stages ) || stages.nCount == 0u ||
         stages.nCount > CY_COOKED_SHADER_MAX_STAGES ) {
        return cooked_shader_status_t::STAGE_LIMIT_EXCEEDED;
    }

    // Descriptors are unique, sorted by enum value, and map directly to code chunks.
    u32 stageMask = 0u;
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        const cooked_shader_stage_desc_t &stage = stages.pData[iStage];
        const cooked_shader_status_t status =
            ValidateStageDescriptor( shader, stage );
        if ( status != cooked_shader_status_t::OK ) {
            if ( pInvalidStage != nullptr ) {
                *pInvalidStage = iStage;
            }
            return status;
        }

        const u32 bit = StageBit( stage.stage );
        if ( ( stageMask & bit ) != 0u ) {
            if ( pInvalidStage != nullptr ) {
                *pInvalidStage = iStage;
            }
            return cooked_shader_status_t::DUPLICATE_STAGE;
        }
        if ( iStage > 0u &&
             static_cast<u32>( stages.pData[iStage - 1u].stage ) >=
                 static_cast<u32>( stage.stage ) ) {
            if ( pInvalidStage != nullptr ) {
                *pInvalidStage = iStage;
            }
            return cooked_shader_status_t::NON_CANONICAL_LAYOUT;
        }
        if ( stage.iCodeChunk != iStage + iFirstCodeChunk ) {
            if ( pInvalidStage != nullptr ) {
                *pInvalidStage = iStage;
            }
            return cooked_shader_status_t::NON_CANONICAL_LAYOUT;
        }
        stageMask |= bit;
    }
    return IsStageSetValid(
               shader.kind,
               stageMask,
               static_cast<u32>( stages.nCount ) )
        ? cooked_shader_status_t::OK
        : cooked_shader_status_t::INVALID_STAGE_SET;
}

CYPHER_NODISCARD bool_t WriteMetadataHeader(
    byte_writer_t &writer,
    const cooked_shader_desc_t &shader,
    u32 nStages ) noexcept
{
    // SHMD is serialized field by field; compiler struct layout is never persisted.
    return ByteWriter_WriteU32( &writer, CY_COOKED_SHADER_METADATA_MAGIC ) &&
           ByteWriter_WriteU32(
               &writer,
               CY_COOKED_SHADER_METADATA_VERSION ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( CY_COOKED_SHADER_METADATA_HEADER_SIZE ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( shader.backend ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( shader.kind ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( shader.languageProfile ) ) &&
           ByteWriter_WriteU32( &writer, shader.nLanguageVersion ) &&
           ByteWriter_WriteU32( &writer, shader.flags ) &&
           ByteWriter_WriteU32( &writer, nStages ) &&
           ByteWriter_WriteU32( &writer, 0u );
}

CYPHER_NODISCARD bool_t WriteMetadataHeaderV3(
    byte_writer_t &writer,
    const cooked_shader_desc_t &shader,
    u32 nStages,
    u32 nBindings,
    u32 cbStringTable,
    content_hash_t interfaceHash ) noexcept
{
    return ByteWriter_WriteU32( &writer, CY_COOKED_SHADER_METADATA_MAGIC ) &&
           ByteWriter_WriteU32(
               &writer,
               CY_COOKED_SHADER_METADATA_VERSION_V3 ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>(
                   CY_COOKED_SHADER_METADATA_HEADER_SIZE_V3 ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( shader.backend ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( shader.kind ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( shader.languageProfile ) ) &&
           ByteWriter_WriteU32( &writer, shader.nLanguageVersion ) &&
           ByteWriter_WriteU32( &writer, shader.flags ) &&
           ByteWriter_WriteU32( &writer, nStages ) &&
           ByteWriter_WriteU32( &writer, nBindings ) &&
           ByteWriter_WriteU32(
               &writer,
               CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK - 2u ) &&
           ByteWriter_WriteU32(
               &writer,
               CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK - 1u ) &&
           ByteWriter_WriteU32( &writer, cbStringTable ) &&
           ByteWriter_WriteU32( &writer, 0u ) &&
           ByteWriter_WriteU64( &writer, interfaceHash.low ) &&
           ByteWriter_WriteU64( &writer, interfaceHash.high );
}

CYPHER_NODISCARD bool_t WriteStageDescriptor(
    byte_writer_t &writer,
    const cooked_shader_stage_desc_t &stage ) noexcept
{
    return ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( stage.stage ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( stage.codeFormat ) ) &&
           ByteWriter_WriteU32( &writer, stage.flags ) &&
           ByteWriter_WriteU32( &writer, stage.iCodeChunk ) &&
           ByteWriter_WriteU64( &writer, stage.cbCode );
}

CYPHER_NODISCARD bool_t ReadMetadataHeader(
    byte_reader_t &reader,
    cooked_shader_metadata_t &metadata ) noexcept
{
    u32 magic = 0u;
    u32 version = 0u;
    u32 cbHeader = 0u;
    u32 backend = 0u;
    u32 kind = 0u;
    u32 languageProfile = 0u;
    u32 reserved = 0u;
    if ( !ByteReader_ReadU32( &reader, &magic ) ||
         !ByteReader_ReadU32( &reader, &version ) ||
         !ByteReader_ReadU32( &reader, &cbHeader ) ||
         !ByteReader_ReadU32( &reader, &backend ) ||
         !ByteReader_ReadU32( &reader, &kind ) ||
         !ByteReader_ReadU32( &reader, &languageProfile ) ||
         !ByteReader_ReadU32(
             &reader,
             &metadata.shader.nLanguageVersion ) ||
         !ByteReader_ReadU32( &reader, &metadata.shader.flags ) ||
         !ByteReader_ReadU32( &reader, &metadata.nStages ) ||
         !ByteReader_ReadU32( &reader, &reserved ) ) {
        return CY_FALSE;
    }
    // A nonzero reserve signals an incompatible future metadata interpretation.
    if ( magic != CY_COOKED_SHADER_METADATA_MAGIC ||
         version != CY_COOKED_SHADER_METADATA_VERSION ||
         cbHeader != CY_COOKED_SHADER_METADATA_HEADER_SIZE ||
         reserved != 0u ) {
        return CY_FALSE;
    }
    metadata.shader.backend = static_cast<render_shader_backend_t>( backend );
    metadata.shader.kind = static_cast<render_shader_program_kind_t>( kind );
    metadata.shader.languageProfile =
        static_cast<render_shader_language_profile_t>( languageProfile );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadMetadataHeaderV3(
    byte_reader_t &reader,
    cooked_shader_metadata_t &metadata ) noexcept
{
    u32 magic = 0u;
    u32 version = 0u;
    u32 cbHeader = 0u;
    u32 backend = 0u;
    u32 kind = 0u;
    u32 languageProfile = 0u;
    u32 reserved = 0u;
    if ( !ByteReader_ReadU32( &reader, &magic ) ||
         !ByteReader_ReadU32( &reader, &version ) ||
         !ByteReader_ReadU32( &reader, &cbHeader ) ||
         !ByteReader_ReadU32( &reader, &backend ) ||
         !ByteReader_ReadU32( &reader, &kind ) ||
         !ByteReader_ReadU32( &reader, &languageProfile ) ||
         !ByteReader_ReadU32(
             &reader,
             &metadata.shader.nLanguageVersion ) ||
         !ByteReader_ReadU32( &reader, &metadata.shader.flags ) ||
         !ByteReader_ReadU32( &reader, &metadata.nStages ) ||
         !ByteReader_ReadU32( &reader, &metadata.nBindings ) ||
         !ByteReader_ReadU32( &reader, &metadata.iReflectionChunk ) ||
         !ByteReader_ReadU32( &reader, &metadata.iStringChunk ) ||
         !ByteReader_ReadU32( &reader, &metadata.cbStringTable ) ||
         !ByteReader_ReadU32( &reader, &reserved ) ||
         !ByteReader_ReadU64( &reader, &metadata.interfaceHash.low ) ||
         !ByteReader_ReadU64( &reader, &metadata.interfaceHash.high ) ) {
        return CY_FALSE;
    }
    if ( magic != CY_COOKED_SHADER_METADATA_MAGIC ||
         version != CY_COOKED_SHADER_METADATA_VERSION_V3 ||
         cbHeader != CY_COOKED_SHADER_METADATA_HEADER_SIZE_V3 ||
         reserved != 0u ||
         metadata.iReflectionChunk != 1u ||
         metadata.iStringChunk != 2u ||
         metadata.cbStringTable == 0u ||
         metadata.cbStringTable > CY_COOKED_SHADER_MAX_STRING_TABLE_SIZE ||
         !ContentHash_IsValid( metadata.interfaceHash ) ) {
        return CY_FALSE;
    }
    metadata.shader.backend = static_cast<render_shader_backend_t>( backend );
    metadata.shader.kind = static_cast<render_shader_program_kind_t>( kind );
    metadata.shader.languageProfile =
        static_cast<render_shader_language_profile_t>( languageProfile );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadStageDescriptor(
    byte_reader_t &reader,
    cooked_shader_stage_desc_t &stage ) noexcept
{
    u32 stageValue = 0u;
    u32 formatValue = 0u;
    if ( !ByteReader_ReadU32( &reader, &stageValue ) ||
         !ByteReader_ReadU32( &reader, &formatValue ) ||
         !ByteReader_ReadU32( &reader, &stage.flags ) ||
         !ByteReader_ReadU32( &reader, &stage.iCodeChunk ) ||
         !ByteReader_ReadU64( &reader, &stage.cbCode ) ) {
        return CY_FALSE;
    }
    stage.stage = static_cast<render_shader_stage_t>( stageValue );
    stage.codeFormat = static_cast<render_shader_code_format_t>( formatValue );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t WriteReflectionHeader(
    byte_writer_t &writer,
    u32 nBindings,
    u32 cbStringTable ) noexcept
{
    return ByteWriter_WriteU32( &writer, CY_COOKED_SHADER_REFLECTION_MAGIC ) &&
           ByteWriter_WriteU32(
               &writer,
               CY_COOKED_SHADER_REFLECTION_VERSION ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>(
                   CY_COOKED_SHADER_REFLECTION_HEADER_SIZE ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( CY_COOKED_SHADER_BINDING_RECORD_SIZE ) ) &&
           ByteWriter_WriteU32( &writer, nBindings ) &&
           ByteWriter_WriteU32( &writer, cbStringTable ) &&
           ByteWriter_WriteU32( &writer, 0u ) &&
           ByteWriter_WriteU32( &writer, 0u );
}

CYPHER_NODISCARD bool_t ReadReflectionHeader(
    byte_reader_t &reader,
    u32 &nBindingsOut,
    u32 &cbStringTableOut ) noexcept
{
    u32 magic = 0u;
    u32 version = 0u;
    u32 cbHeader = 0u;
    u32 cbRecord = 0u;
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;
    if ( !ByteReader_ReadU32( &reader, &magic ) ||
         !ByteReader_ReadU32( &reader, &version ) ||
         !ByteReader_ReadU32( &reader, &cbHeader ) ||
         !ByteReader_ReadU32( &reader, &cbRecord ) ||
         !ByteReader_ReadU32( &reader, &nBindingsOut ) ||
         !ByteReader_ReadU32( &reader, &cbStringTableOut ) ||
         !ByteReader_ReadU32( &reader, &reserved0 ) ||
         !ByteReader_ReadU32( &reader, &reserved1 ) ) {
        return CY_FALSE;
    }
    return magic == CY_COOKED_SHADER_REFLECTION_MAGIC &&
           version == CY_COOKED_SHADER_REFLECTION_VERSION &&
           cbHeader == CY_COOKED_SHADER_REFLECTION_HEADER_SIZE &&
           cbRecord == CY_COOKED_SHADER_BINDING_RECORD_SIZE &&
           nBindingsOut <= CY_COOKED_SHADER_MAX_BINDINGS &&
           cbStringTableOut > 0u &&
           cbStringTableOut <= CY_COOKED_SHADER_MAX_STRING_TABLE_SIZE &&
           reserved0 == 0u && reserved1 == 0u;
}

CYPHER_NODISCARD bool_t WriteBindingRecord(
    byte_writer_t &writer,
    const cooked_shader_binding_source_t &binding,
    u32 iName ) noexcept
{
    return ByteWriter_WriteU32( &writer, iName ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( binding.name.cchLength ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( binding.kind ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( binding.valueType ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( binding.resourceType ) ) &&
           ByteWriter_WriteU32( &writer, binding.nArrayElements ) &&
           ByteWriter_WriteU32( &writer, binding.stageMask ) &&
           ByteWriter_WriteU32( &writer, binding.flags ) &&
           ByteWriter_WriteU64( &writer, binding.nLogicalBinding ) &&
           ByteWriter_WriteU32( &writer, binding.iByteOffset ) &&
           ByteWriter_WriteU32( &writer, binding.cbByteSize ) &&
           ByteWriter_WriteU32( &writer, 0u ) &&
           ByteWriter_WriteU32( &writer, 0u );
}

CYPHER_NODISCARD bool_t ReadBindingRecord(
    byte_reader_t &reader,
    cooked_shader_binding_record_t &record ) noexcept
{
    u32 kind = 0u;
    u32 valueType = 0u;
    u32 resourceType = 0u;
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;
    if ( !ByteReader_ReadU32( &reader, &record.iName ) ||
         !ByteReader_ReadU32( &reader, &record.cchName ) ||
         !ByteReader_ReadU32( &reader, &kind ) ||
         !ByteReader_ReadU32( &reader, &valueType ) ||
         !ByteReader_ReadU32( &reader, &resourceType ) ||
         !ByteReader_ReadU32(
             &reader,
             &record.binding.nArrayElements ) ||
         !ByteReader_ReadU32( &reader, &record.binding.stageMask ) ||
         !ByteReader_ReadU32( &reader, &record.binding.flags ) ||
         !ByteReader_ReadU64(
             &reader,
             &record.binding.nLogicalBinding ) ||
         !ByteReader_ReadU32( &reader, &record.binding.iByteOffset ) ||
         !ByteReader_ReadU32( &reader, &record.binding.cbByteSize ) ||
         !ByteReader_ReadU32( &reader, &reserved0 ) ||
         !ByteReader_ReadU32( &reader, &reserved1 ) ||
         reserved0 != 0u || reserved1 != 0u ) {
        return CY_FALSE;
    }
    record.binding.kind = static_cast<render_shader_binding_kind_t>( kind );
    record.binding.valueType =
        static_cast<render_shader_value_type_t>( valueType );
    record.binding.resourceType =
        static_cast<render_shader_resource_type_t>( resourceType );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t WriteInterfacePayloads(
    const canonical_shader_interface_t &shaderInterface,
    byte_span_t reflectionOutput,
    byte_span_t stringOutput,
    content_hash_t &interfaceHashOut ) noexcept
{
    byte_writer_t reflectionWriter{};
    byte_writer_t stringWriter{};
    if ( !ByteWriter_Init(
             &reflectionWriter,
             reflectionOutput,
             data_byte_order_t::LITTLE ) ||
         !ByteWriter_Init(
             &stringWriter,
             stringOutput,
             data_byte_order_t::LITTLE ) ||
         !WriteReflectionHeader(
             reflectionWriter,
             static_cast<u32>( shaderInterface.nBindings ),
             static_cast<u32>( shaderInterface.cbStrings ) ) ||
         !ByteWriter_WriteU8( &stringWriter, 0u ) ) {
        return CY_FALSE;
    }

    u32 iName = 1u;
    for ( usize iBinding = 0u;
          iBinding < shaderInterface.nBindings;
          ++iBinding ) {
        const cooked_shader_binding_source_t &binding =
            shaderInterface.bindings[iBinding];
        if ( !WriteBindingRecord( reflectionWriter, binding, iName ) ||
             !ByteWriter_WriteString(
                 &stringWriter,
                 binding.name,
                 CY_TRUE ) ) {
            return CY_FALSE;
        }
        iName += static_cast<u32>( binding.name.cchLength + 1u );
    }
    if ( ByteWriter_BytesWritten( &reflectionWriter ) !=
             reflectionOutput.nCount ||
         ByteWriter_BytesWritten( &stringWriter ) != stringOutput.nCount ) {
        return CY_FALSE;
    }
    interfaceHashOut = ContentHash_Combine(
        ContentHash_Data( { reflectionOutput.pData, reflectionOutput.nCount } ),
        ContentHash_Data( { stringOutput.pData, stringOutput.nCount } ) );
    return ContentHash_IsValid( interfaceHashOut );
}

CYPHER_NODISCARD bool_t HashInterfacePayloads(
    const canonical_shader_interface_t &shaderInterface,
    content_hash_t &interfaceHashOut ) noexcept
{
    hash_xxh3_stream_t reflectionStream{};
    hash_xxh3_stream_t stringStream{};
    if ( !HashXXH3_StreamInit(
             &reflectionStream,
             hash_xxh3_stream_mode_t::HASH_128 ) ||
         !HashXXH3_StreamInit(
             &stringStream,
             hash_xxh3_stream_mode_t::HASH_128 ) ) {
        return CY_FALSE;
    }

    byte reflectionHeader[CY_COOKED_SHADER_REFLECTION_HEADER_SIZE]{};
    byte_writer_t headerWriter{};
    if ( !ByteWriter_Init(
             &headerWriter,
             Span_FromArray( reflectionHeader ),
             data_byte_order_t::LITTLE ) ||
         !WriteReflectionHeader(
             headerWriter,
             static_cast<u32>( shaderInterface.nBindings ),
             static_cast<u32>( shaderInterface.cbStrings ) ) ||
         !HashXXH3_StreamUpdate(
             &reflectionStream,
             { reflectionHeader, sizeof( reflectionHeader ) } ) ) {
        return CY_FALSE;
    }

    const byte terminator = static_cast<byte>( '\0' );
    if ( !HashXXH3_StreamUpdate( &stringStream, { &terminator, 1u } ) ) {
        return CY_FALSE;
    }
    u32 iName = 1u;
    for ( usize iBinding = 0u;
          iBinding < shaderInterface.nBindings;
          ++iBinding ) {
        const cooked_shader_binding_source_t &binding =
            shaderInterface.bindings[iBinding];
        byte record[CY_COOKED_SHADER_BINDING_RECORD_SIZE]{};
        byte_writer_t recordWriter{};
        if ( !ByteWriter_Init(
                 &recordWriter,
                 Span_FromArray( record ),
                 data_byte_order_t::LITTLE ) ||
             !WriteBindingRecord( recordWriter, binding, iName ) ||
             ByteWriter_BytesWritten( &recordWriter ) != sizeof( record ) ||
             !HashXXH3_StreamUpdate(
                 &reflectionStream,
                 { record, sizeof( record ) } ) ||
             !HashXXH3_StreamUpdate(
                 &stringStream,
                 {
                     reinterpret_cast<const byte *>( binding.name.pData ),
                     binding.name.cchLength
                 } ) ||
             !HashXXH3_StreamUpdate(
                 &stringStream,
                 { &terminator, 1u } ) ) {
            return CY_FALSE;
        }
        iName += static_cast<u32>( binding.name.cchLength + 1u );
    }

    hash128_t reflectionDigest{};
    hash128_t stringDigest{};
    if ( !HashXXH3_StreamDigest128(
             &reflectionStream,
             &reflectionDigest ) ||
         !HashXXH3_StreamDigest128( &stringStream, &stringDigest ) ) {
        return CY_FALSE;
    }
    interfaceHashOut = ContentHash_Combine(
        { reflectionDigest.low, reflectionDigest.high },
        { stringDigest.low, stringDigest.high } );
    return ContentHash_IsValid( interfaceHashOut );
}

CYPHER_NODISCARD bool_t WriteMetadataV3(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_desc_t> stages,
    u32 nBindings,
    u32 cbStringTable,
    content_hash_t interfaceHash,
    byte_span_t output ) noexcept
{
    byte_writer_t writer{};
    if ( !ByteWriter_Init( &writer, output, data_byte_order_t::LITTLE ) ||
         !WriteMetadataHeaderV3(
             writer,
             shader,
             static_cast<u32>( stages.nCount ),
             nBindings,
             cbStringTable,
             interfaceHash ) ) {
        return CY_FALSE;
    }
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        if ( !WriteStageDescriptor( writer, stages.pData[iStage] ) ) {
            return CY_FALSE;
        }
    }
    return ByteWriter_BytesWritten( &writer ) == output.nCount;
}

CYPHER_NODISCARD cooked_shader_status_t ValidateCode(
    const cooked_shader_stage_desc_t &stage,
    binary_block_t code ) noexcept
{
    if ( !BinaryBlock_IsValid( code ) || code.cbSize != stage.cbCode ) {
        return cooked_shader_status_t::INVALID_CODE_CHUNK;
    }
    // Stored GLSL is one UTF-8 byte sequence followed by exactly one NUL.
    if ( code.cbSize <= 1u ||
         code.pData[code.cbSize - 1u] != static_cast<byte>( '\0' ) ) {
        return cooked_shader_status_t::INVALID_CODE;
    }
    for ( usize iByte = 0u; iByte + 1u < code.cbSize; ++iByte ) {
        if ( code.pData[iByte] == static_cast<byte>( '\0' ) ) {
            return cooked_shader_status_t::INVALID_CODE;
        }
    }
    const string_view_t text{
        reinterpret_cast<const char *>( code.pData ),
        code.cbSize - 1u
    };
    const unicode_result_t utf8 = Unicode_ValidateUtf8(
        text );
    return utf8.status == unicode_status_t::OK
        ? cooked_shader_status_t::OK
        : cooked_shader_status_t::INVALID_CODE;
}

CYPHER_NODISCARD bool_t PrepareCanonicalLayout(
    span_t<const cooked_shader_stage_source_t> stages,
    cooked_chunk_desc_t *pChunks,
    cooked_shader_stage_desc_t *pStageDescs,
    usize &cbFileOut ) noexcept
{
    // Chunk zero is SHMD; stage N always occupies code chunk N + 1.
    const u32 nChunks = static_cast<u32>( stages.nCount + 1u );
    usize iOffset = CookedResource_PrefixSize( nChunks );
    if ( iOffset == 0u ||
         !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_SHADER_METADATA_ALIGNMENT,
             iOffset ) ) {
        return CY_FALSE;
    }

    const usize cbMetadata = CookedShader_MetadataSize(
        static_cast<u32>( stages.nCount ) );
    if ( cbMetadata == 0u ) {
        return CY_FALSE;
    }
    pChunks[0].chunkType = CY_COOKED_SHADER_METADATA_CHUNK;
    pChunks[0].nAlignment = CY_COOKED_SHADER_METADATA_ALIGNMENT;
    pChunks[0].iOffset = iOffset;
    pChunks[0].cbStored = cbMetadata;
    pChunks[0].cbDecoded = cbMetadata;
    pChunks[0].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;

    if ( !CheckedAdd( iOffset, cbMetadata, iOffset ) ) {
        return CY_FALSE;
    }
    // Stage payloads follow metadata in deterministic aligned order.
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        const cooked_shader_stage_source_t &source = stages.pData[iStage];
        if ( !IsStageSourceRepresentable( source ) ) {
            return CY_FALSE;
        }
        if ( !Cy_AlignUpChecked(
                 iOffset,
                 CY_COOKED_SHADER_CODE_ALIGNMENT,
                 iOffset ) ) {
            return CY_FALSE;
        }

        cooked_chunk_desc_t &chunk = pChunks[iStage + 1u];
        chunk.chunkType = CY_COOKED_SHADER_CODE_CHUNK;
        chunk.nAlignment = CY_COOKED_SHADER_CODE_ALIGNMENT;
        chunk.iOffset = iOffset;
        chunk.cbStored = source.code.cbSize;
        chunk.cbDecoded = source.code.cbSize;
        chunk.flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
        chunk.contentHash = ContentHash_Data( source.code );

        pStageDescs[iStage] = {
            source.stage,
            source.codeFormat,
            source.flags,
            static_cast<u32>( iStage + 1u ),
            source.code.cbSize
        };
        if ( !CheckedAdd( iOffset, source.code.cbSize, iOffset ) ) {
            return CY_FALSE;
        }
    }
    cbFileOut = iOffset;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t PrepareCanonicalLayoutV3(
    span_t<const cooked_shader_stage_source_t> stages,
    const canonical_shader_interface_t &shaderInterface,
    cooked_chunk_desc_t *pChunks,
    cooked_shader_stage_desc_t *pStageDescs,
    usize &cbFileOut ) noexcept
{
    usize iOffset = CookedResource_PrefixSize(
        CY_COOKED_SHADER_V3_CHUNK_COUNT );
    if ( iOffset == 0u ||
         !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_SHADER_METADATA_ALIGNMENT,
             iOffset ) ) {
        return CY_FALSE;
    }

    const usize cbMetadata = CookedShader_MetadataSizeV3(
        static_cast<u32>( stages.nCount ) );
    const usize cbReflection = CookedShader_ReflectionSize(
        static_cast<u32>( shaderInterface.nBindings ) );
    if ( cbMetadata == 0u || cbReflection == 0u ||
         shaderInterface.cbStrings == 0u ||
         shaderInterface.cbStrings > CY_COOKED_SHADER_MAX_STRING_TABLE_SIZE ) {
        return CY_FALSE;
    }

    pChunks[0].chunkType = CY_COOKED_SHADER_METADATA_CHUNK;
    pChunks[0].nAlignment = CY_COOKED_SHADER_METADATA_ALIGNMENT;
    pChunks[0].iOffset = iOffset;
    pChunks[0].cbStored = cbMetadata;
    pChunks[0].cbDecoded = cbMetadata;
    pChunks[0].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    if ( !CheckedAdd( iOffset, cbMetadata, iOffset ) ||
         !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_SHADER_REFLECTION_ALIGNMENT,
             iOffset ) ) {
        return CY_FALSE;
    }

    pChunks[1].chunkType = CY_COOKED_SHADER_REFLECTION_CHUNK;
    pChunks[1].nAlignment = CY_COOKED_SHADER_REFLECTION_ALIGNMENT;
    pChunks[1].iOffset = iOffset;
    pChunks[1].cbStored = cbReflection;
    pChunks[1].cbDecoded = cbReflection;
    pChunks[1].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    if ( !CheckedAdd( iOffset, cbReflection, iOffset ) ||
         !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_SHADER_STRING_ALIGNMENT,
             iOffset ) ) {
        return CY_FALSE;
    }

    pChunks[2].chunkType = CY_COOKED_SHADER_STRING_CHUNK;
    pChunks[2].nAlignment = CY_COOKED_SHADER_STRING_ALIGNMENT;
    pChunks[2].iOffset = iOffset;
    pChunks[2].cbStored = shaderInterface.cbStrings;
    pChunks[2].cbDecoded = shaderInterface.cbStrings;
    pChunks[2].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    if ( !CheckedAdd( iOffset, shaderInterface.cbStrings, iOffset ) ) {
        return CY_FALSE;
    }

    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        const cooked_shader_stage_source_t &source = stages.pData[iStage];
        if ( !IsStageSourceRepresentable( source ) ||
             !Cy_AlignUpChecked(
                 iOffset,
                 CY_COOKED_SHADER_CODE_ALIGNMENT,
                 iOffset ) ) {
            return CY_FALSE;
        }
        const usize iChunk = iStage + CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK;
        cooked_chunk_desc_t &chunk = pChunks[iChunk];
        chunk.chunkType = CY_COOKED_SHADER_CODE_CHUNK;
        chunk.nAlignment = CY_COOKED_SHADER_CODE_ALIGNMENT;
        chunk.iOffset = iOffset;
        chunk.cbStored = source.code.cbSize;
        chunk.cbDecoded = source.code.cbSize;
        chunk.flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
        chunk.contentHash = ContentHash_Data( source.code );
        pStageDescs[iStage] = {
            source.stage,
            source.codeFormat,
            source.flags,
            static_cast<u32>( iChunk ),
            source.code.cbSize
        };
        if ( !CheckedAdd( iOffset, source.code.cbSize, iOffset ) ) {
            return CY_FALSE;
        }
    }
    cbFileOut = iOffset;
    return CY_TRUE;
}

} // namespace

bool_t CookedShader_MakeLogicalBindingId(
    string_view_t name,
    u64 *pBindingIdOut ) noexcept
{
    static constexpr char domain[] = "cypher.shader.binding.v1:";
    if ( pBindingIdOut == nullptr || !IsLogicalNameValid( name ) ||
         Cy_MemRangesOverlap(
             name.pData,
             name.cchLength,
             pBindingIdOut,
             sizeof( *pBindingIdOut ) ) ) {
        return CY_FALSE;
    }
    hash64_t bindingId = HashFNV1a64_Update(
        CY_FNV1A64_OFFSET,
        {
            reinterpret_cast<const byte *>( domain ),
            sizeof( domain ) - 1u
        } );
    bindingId = HashFNV1a64_Update(
        bindingId,
        {
            reinterpret_cast<const byte *>( name.pData ),
            name.cchLength
        } );
    // Zero is reserved for uninitialized data. The interface-wide collision
    // check still catches the vanishingly rare zero-to-one remap collision.
    *pBindingIdOut = bindingId == 0u ? 1u : bindingId;
    return CY_TRUE;
}

u32 CookedShader_ValueTypeValueSize(
    render_shader_value_type_t type ) noexcept
{
    return ValueTypeSize( type );
}

u32 CookedShader_ValueTypeStorageAlignment(
    render_shader_value_type_t type ) noexcept
{
    switch ( type ) {
        case render_shader_value_type_t::BOOL:
        case render_shader_value_type_t::I32:
        case render_shader_value_type_t::U32:
        case render_shader_value_type_t::F32: return 4u;
        case render_shader_value_type_t::F64:
        case render_shader_value_type_t::I32X2:
        case render_shader_value_type_t::U32X2:
        case render_shader_value_type_t::F32X2: return 8u;
        case render_shader_value_type_t::I32X3:
        case render_shader_value_type_t::I32X4:
        case render_shader_value_type_t::U32X3:
        case render_shader_value_type_t::U32X4:
        case render_shader_value_type_t::F32X3:
        case render_shader_value_type_t::F32X4:
        case render_shader_value_type_t::F32X3X3:
        case render_shader_value_type_t::F32X4X4: return 16u;
        case render_shader_value_type_t::NONE: return 0u;
    }
    return 0u;
}

u32 CookedShader_ValueTypeStorageSize(
    render_shader_value_type_t type ) noexcept
{
    switch ( type ) {
        case render_shader_value_type_t::I32X3:
        case render_shader_value_type_t::U32X3:
        case render_shader_value_type_t::F32X3: return 16u;
        case render_shader_value_type_t::F32X3X3: return 48u;
        default: return ValueTypeSize( type );
    }
}

bool_t CookedShader_ComputeInterfaceHash(
    const cooked_shader_interface_source_t &shaderInterface,
    content_hash_t *pInterfaceHashOut ) noexcept
{
    if ( pInterfaceHashOut == nullptr ||
         !Span_IsValid( shaderInterface.bindings ) ||
         shaderInterface.bindings.nCount >
             CY_COOKED_SHADER_MAX_BINDINGS ||
         Cy_MemRangesOverlap(
             &shaderInterface,
             sizeof( shaderInterface ),
             pInterfaceHashOut,
             sizeof( *pInterfaceHashOut ) ) ||
         ( shaderInterface.bindings.nCount > 0u &&
           Cy_MemRangesOverlap(
               shaderInterface.bindings.pData,
               shaderInterface.bindings.nCount *
                   sizeof( cooked_shader_binding_source_t ),
               pInterfaceHashOut,
               sizeof( *pInterfaceHashOut ) ) ) ) {
        return CY_FALSE;
    }
    for ( usize iBinding = 0u;
          iBinding < shaderInterface.bindings.nCount;
          ++iBinding ) {
        const string_view_t name = shaderInterface.bindings.pData[iBinding].name;
        if ( StringView_IsValid( name ) &&
             Cy_MemRangesOverlap(
                 name.pData,
                 name.cchLength,
                 pInterfaceHashOut,
                 sizeof( *pInterfaceHashOut ) ) ) {
            return CY_FALSE;
        }
    }

    canonical_shader_interface_t canonical{};
    if ( CanonicalizeInterface( shaderInterface, canonical, nullptr ) !=
         cooked_shader_status_t::OK ) {
        return CY_FALSE;
    }
    content_hash_t interfaceHash{};
    if ( !HashInterfacePayloads( canonical, interfaceHash ) ) {
        return CY_FALSE;
    }
    *pInterfaceHashOut = interfaceHash;
    return CY_TRUE;
}

usize CookedShader_MetadataSize( u32 nStages ) noexcept
{
    if ( nStages == 0u || nStages > CY_COOKED_SHADER_MAX_STAGES ) {
        return 0u;
    }
    return CY_COOKED_SHADER_METADATA_HEADER_SIZE +
           static_cast<usize>( nStages ) *
               CY_COOKED_SHADER_STAGE_RECORD_SIZE;
}

usize CookedShader_MetadataSizeV3( u32 nStages ) noexcept
{
    if ( nStages == 0u || nStages > CY_COOKED_SHADER_MAX_STAGES ) {
        return 0u;
    }
    return CY_COOKED_SHADER_METADATA_HEADER_SIZE_V3 +
           static_cast<usize>( nStages ) *
               CY_COOKED_SHADER_STAGE_RECORD_SIZE;
}

usize CookedShader_ReflectionSize( u32 nBindings ) noexcept
{
    if ( nBindings > CY_COOKED_SHADER_MAX_BINDINGS ) {
        return 0u;
    }
    usize cbRecords = 0u;
    usize cbReflection = 0u;
    return CheckedMul(
               static_cast<usize>( nBindings ),
               CY_COOKED_SHADER_BINDING_RECORD_SIZE,
               cbRecords ) &&
           CheckedAdd(
               CY_COOKED_SHADER_REFLECTION_HEADER_SIZE,
               cbRecords,
               cbReflection )
        ? cbReflection
        : 0u;
}

usize CookedShader_RequiredSize(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages ) noexcept
{
    if ( ValidateShader( shader ) != cooked_shader_status_t::OK ||
         !Span_IsValid( stages ) || stages.nCount == 0u ||
         stages.nCount > CY_COOKED_SHADER_MAX_STAGES ) {
        return 0u;
    }

    // Small fixed stage limits keep sizing allocation-free.
    cooked_chunk_desc_t chunks[CY_COOKED_SHADER_MAX_STAGES + 1u]{};
    cooked_shader_stage_desc_t
        stageDescs[CY_COOKED_SHADER_MAX_STAGES]{};
    usize cbFile = 0u;
    if ( !PrepareCanonicalLayout(
             stages,
             chunks,
             stageDescs,
             cbFile ) ) {
        return 0u;
    }
    const span_t<const cooked_shader_stage_desc_t> descriptors{
        stageDescs,
        stages.nCount
    };
    if ( ValidateStageDescriptors( shader, descriptors, 1u, nullptr ) !=
         cooked_shader_status_t::OK ) {
        return 0u;
    }
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        if ( ValidateCode( stageDescs[iStage], stages.pData[iStage].code ) !=
             cooked_shader_status_t::OK ) {
            return 0u;
        }
    }
    return cbFile;
}

usize CookedShader_RequiredSizeV3(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages,
    const cooked_shader_interface_source_t &shaderInterface ) noexcept
{
    if ( ValidateShader( shader ) != cooked_shader_status_t::OK ||
         !Span_IsValid( stages ) || stages.nCount == 0u ||
         stages.nCount > CY_COOKED_SHADER_MAX_STAGES ) {
        return 0u;
    }
    canonical_shader_interface_t canonical{};
    if ( CanonicalizeInterface( shaderInterface, canonical, nullptr ) !=
         cooked_shader_status_t::OK ) {
        return 0u;
    }
    cooked_chunk_desc_t chunks[CY_COOKED_SHADER_V3_CHUNK_COUNT]{};
    cooked_shader_stage_desc_t
        stageDescs[CY_COOKED_SHADER_MAX_STAGES]{};
    usize cbFile = 0u;
    if ( !PrepareCanonicalLayoutV3(
             stages,
             canonical,
             chunks,
             stageDescs,
             cbFile ) ||
         ValidateStageDescriptors(
             shader,
             { stageDescs, stages.nCount },
             CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK,
             nullptr ) != cooked_shader_status_t::OK ) {
        return 0u;
    }
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        if ( ValidateCode( stageDescs[iStage], stages.pData[iStage].code ) !=
             cooked_shader_status_t::OK ) {
            return 0u;
        }
    }
    return cbFile;
}

cooked_shader_result_t CookedShader_WriteMetadata(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_desc_t> stages,
    byte_span_t output ) noexcept
{
    cooked_shader_result_t result{};
    if ( !Span_IsValid( stages ) || !Span_IsValid( output ) ) {
        result.status = cooked_shader_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.cbRequired = CookedShader_MetadataSize(
        static_cast<u32>( stages.nCount ) );
    if ( result.cbRequired == 0u ) {
        result.status = cooked_shader_status_t::STAGE_LIMIT_EXCEEDED;
        return result;
    }

    result.status = ValidateShader( shader );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }
    result.status = ValidateStageDescriptors(
        shader,
        stages,
        1u,
        &result.iStage );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }
    if ( output.nCount < result.cbRequired ) {
        result.status = cooked_shader_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    if ( Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             &shader,
             sizeof( shader ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             stages.pData,
             stages.nCount * sizeof( cooked_shader_stage_desc_t ) ) ) {
        result.status = cooked_shader_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Alias rejection above permits direct little-endian serialization to output.
    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ||
         !WriteMetadataHeader(
             writer,
             shader,
             static_cast<u32>( stages.nCount ) ) ) {
        result.status = cooked_shader_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        if ( !WriteStageDescriptor( writer, stages.pData[iStage] ) ) {
            result.status = cooked_shader_status_t::OUTPUT_TOO_SMALL;
            result.iStage = iStage;
            return result;
        }
    }
    result.cbWritten = ByteWriter_BytesWritten( &writer );
    return result;
}

cooked_shader_result_t CookedShader_Write(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept
{
    cooked_shader_result_t result{};
    if ( !Span_IsValid( stages ) || !Span_IsValid( output ) ) {
        result.status = cooked_shader_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.status = ValidateShader( shader );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }
    if ( stages.nCount == 0u ||
         stages.nCount > CY_COOKED_SHADER_MAX_STAGES ) {
        result.status = cooked_shader_status_t::STAGE_LIMIT_EXCEEDED;
        return result;
    }

    // Prepare and validate every descriptor before mutating caller output.
    cooked_chunk_desc_t chunks[CY_COOKED_SHADER_MAX_STAGES + 1u]{};
    cooked_shader_stage_desc_t
        stageDescs[CY_COOKED_SHADER_MAX_STAGES]{};
    if ( !PrepareCanonicalLayout(
             stages,
             chunks,
             stageDescs,
             result.cbRequired ) ) {
        result.status = cooked_shader_status_t::INVALID_CODE_CHUNK;
        return result;
    }
    result.status = ValidateStageDescriptors(
        shader,
        { stageDescs, stages.nCount },
        1u,
        &result.iStage );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        const cooked_shader_status_t codeStatus = ValidateCode(
            stageDescs[iStage],
            stages.pData[iStage].code );
        if ( codeStatus != cooked_shader_status_t::OK ) {
            result.status = codeStatus;
            result.iStage = iStage;
            return result;
        }
    }
    if ( output.nCount < result.cbRequired ) {
        result.status = cooked_shader_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    if ( Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             &shader,
             sizeof( shader ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             stages.pData,
             stages.nCount * sizeof( cooked_shader_stage_source_t ) ) ) {
        result.status = cooked_shader_status_t::INVALID_ARGUMENT;
        return result;
    }
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 result.cbRequired,
                 stages.pData[iStage].code.pData,
                 stages.pData[iStage].code.cbSize ) ) {
            result.status = cooked_shader_status_t::INVALID_ARGUMENT;
            result.iStage = iStage;
            return result;
        }
    }

    // All alignment gaps are zero so equivalent inputs produce identical files.
    const usize cbPrefix = CookedResource_PrefixSize(
        static_cast<u32>( stages.nCount + 1u ) );
    if ( chunks[0].iOffset > cbPrefix ) {
        Cy_MemZero(
            output.pData + cbPrefix,
            static_cast<usize>( chunks[0].iOffset - cbPrefix ) );
    }

    const u32 nChunks = static_cast<u32>( stages.nCount + 1u );
    cooked_resource_header_t header{};
    header.resourceType = CY_RENDER_SHADER_RESOURCE_TYPE;
    header.nResourceVersion = CY_COOKED_SHADER_RESOURCE_VERSION_V2;
    header.flags = COOKED_RESOURCE_FLAG_NONE;
    header.nChunks = nChunks;
    header.cbFile = result.cbRequired;
    if ( ContentHash_IsValid( sourceHash ) ) {
        header.flags |= COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH;
        header.sourceHash = sourceHash;
    }

    byte_span_t metadataOutput{
        output.pData + chunks[0].iOffset,
        static_cast<usize>( chunks[0].cbStored )
    };
    const cooked_shader_result_t metadata = CookedShader_WriteMetadata(
        shader,
        { stageDescs, stages.nCount },
        metadataOutput );
    if ( !CookedShader_Succeeded( metadata ) ) {
        return metadata;
    }
    chunks[0].contentHash = ContentHash_Data( {
        metadataOutput.pData,
        metadataOutput.nCount
    } );

    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        const cooked_chunk_desc_t &chunk = chunks[iStage + 1u];
        const cooked_chunk_desc_t &previous = chunks[iStage];
        const usize iPreviousEnd = static_cast<usize>(
            previous.iOffset + previous.cbStored );
        if ( chunk.iOffset > iPreviousEnd ) {
            Cy_MemZero(
                output.pData + iPreviousEnd,
                static_cast<usize>( chunk.iOffset - iPreviousEnd ) );
        }
        Cy_MemCopy(
            output.pData + chunk.iOffset,
            stages.pData[iStage].code.pData,
            stages.pData[iStage].code.cbSize );
    }

    // Rewrite descriptors after computing the metadata hash, then seal the file.
    if ( !CookedResource_Succeeded( CookedResource_WriteLayout(
             header,
             { chunks, nChunks },
             output ) ) ) {
        result.status = cooked_shader_status_t::RESOURCE_ERROR;
        result.resourceStatus = cooked_resource_status_t::INVALID_HEADER;
        return result;
    }
    header.flags |= COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = CookedResource_ComputeContentHash( {
        output.pData,
        result.cbRequired
    } );
    const cooked_resource_result_t sealed = CookedResource_WriteLayout(
        header,
        { chunks, nChunks },
        output );
    if ( !CookedResource_Succeeded( sealed ) ) {
        result.status = cooked_shader_status_t::RESOURCE_ERROR;
        result.resourceStatus = sealed.status;
        result.iChunk = sealed.iChunk;
        return result;
    }

    result.cbWritten = result.cbRequired;
    return result;
}

cooked_shader_result_t CookedShader_WriteV3(
    const cooked_shader_desc_t &shader,
    span_t<const cooked_shader_stage_source_t> stages,
    const cooked_shader_interface_source_t &shaderInterface,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept
{
    cooked_shader_result_t result{};
    if ( !Span_IsValid( stages ) || !Span_IsValid( output ) ) {
        result.status = cooked_shader_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.status = ValidateShader( shader );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }
    if ( stages.nCount == 0u ||
         stages.nCount > CY_COOKED_SHADER_MAX_STAGES ) {
        result.status = cooked_shader_status_t::STAGE_LIMIT_EXCEEDED;
        return result;
    }

    canonical_shader_interface_t canonical{};
    result.status = CanonicalizeInterface(
        shaderInterface,
        canonical,
        &result.iBinding );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }

    cooked_chunk_desc_t chunks[CY_COOKED_SHADER_V3_CHUNK_COUNT]{};
    cooked_shader_stage_desc_t
        stageDescs[CY_COOKED_SHADER_MAX_STAGES]{};
    if ( !PrepareCanonicalLayoutV3(
             stages,
             canonical,
             chunks,
             stageDescs,
             result.cbRequired ) ) {
        result.status = cooked_shader_status_t::INVALID_CODE_CHUNK;
        return result;
    }
    result.status = ValidateStageDescriptors(
        shader,
        { stageDescs, stages.nCount },
        CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK,
        &result.iStage );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        result.status = ValidateCode(
            stageDescs[iStage],
            stages.pData[iStage].code );
        if ( result.status != cooked_shader_status_t::OK ) {
            result.iStage = iStage;
            return result;
        }
    }
    if ( output.nCount < result.cbRequired ) {
        result.status = cooked_shader_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    if ( Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             &shader,
             sizeof( shader ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             stages.pData,
             stages.nCount * sizeof( cooked_shader_stage_source_t ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             &shaderInterface,
             sizeof( shaderInterface ) ) ||
         ( shaderInterface.bindings.nCount > 0u &&
           Cy_MemRangesOverlap(
               output.pData,
               result.cbRequired,
               shaderInterface.bindings.pData,
               shaderInterface.bindings.nCount *
                   sizeof( cooked_shader_binding_source_t ) ) ) ) {
        result.status = cooked_shader_status_t::INVALID_ARGUMENT;
        return result;
    }
    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 result.cbRequired,
                 stages.pData[iStage].code.pData,
                 stages.pData[iStage].code.cbSize ) ) {
            result.status = cooked_shader_status_t::INVALID_ARGUMENT;
            result.iStage = iStage;
            return result;
        }
    }
    for ( usize iBinding = 0u;
          iBinding < shaderInterface.bindings.nCount;
          ++iBinding ) {
        const string_view_t name = shaderInterface.bindings.pData[iBinding].name;
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 result.cbRequired,
                 name.pData,
                 name.cchLength ) ) {
            result.status = cooked_shader_status_t::INVALID_ARGUMENT;
            result.iBinding = iBinding;
            return result;
        }
    }

    // Every prefix byte and alignment gap is deterministic zero before publishing.
    Cy_MemZero( output.pData, result.cbRequired );
    const byte_span_t reflectionOutput{
        output.pData + chunks[1].iOffset,
        static_cast<usize>( chunks[1].cbStored )
    };
    const byte_span_t stringOutput{
        output.pData + chunks[2].iOffset,
        static_cast<usize>( chunks[2].cbStored )
    };
    content_hash_t interfaceHash{};
    content_hash_t computedInterfaceHash{};
    if ( !WriteInterfacePayloads(
             canonical,
             reflectionOutput,
             stringOutput,
             interfaceHash ) ||
         !HashInterfacePayloads( canonical, computedInterfaceHash ) ||
         !ContentHash_Equals(
             interfaceHash,
             computedInterfaceHash ) ) {
        result.status = cooked_shader_status_t::INVALID_INTERFACE_HASH;
        return result;
    }
    chunks[1].contentHash = ContentHash_Data( {
        reflectionOutput.pData,
        reflectionOutput.nCount
    } );
    chunks[2].contentHash = ContentHash_Data( {
        stringOutput.pData,
        stringOutput.nCount
    } );

    const byte_span_t metadataOutput{
        output.pData + chunks[0].iOffset,
        static_cast<usize>( chunks[0].cbStored )
    };
    if ( !WriteMetadataV3(
             shader,
             { stageDescs, stages.nCount },
             static_cast<u32>( canonical.nBindings ),
             static_cast<u32>( canonical.cbStrings ),
             interfaceHash,
             metadataOutput ) ) {
        result.status = cooked_shader_status_t::INVALID_METADATA;
        return result;
    }
    chunks[0].contentHash = ContentHash_Data( {
        metadataOutput.pData,
        metadataOutput.nCount
    } );

    for ( usize iStage = 0u; iStage < stages.nCount; ++iStage ) {
        const usize iChunk =
            iStage + CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK;
        Cy_MemCopy(
            output.pData + chunks[iChunk].iOffset,
            stages.pData[iStage].code.pData,
            stages.pData[iStage].code.cbSize );
    }

    cooked_resource_header_t header{};
    header.resourceType = CY_RENDER_SHADER_RESOURCE_TYPE;
    header.nResourceVersion = CY_COOKED_SHADER_RESOURCE_VERSION_V3;
    header.flags = COOKED_RESOURCE_FLAG_NONE;
    header.nChunks = CY_COOKED_SHADER_V3_CHUNK_COUNT;
    header.cbFile = result.cbRequired;
    if ( ContentHash_IsValid( sourceHash ) ) {
        header.flags |= COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH;
        header.sourceHash = sourceHash;
    }
    cooked_resource_result_t layout = CookedResource_WriteLayout(
        header,
        { chunks, CY_COOKED_SHADER_V3_CHUNK_COUNT },
        output );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_shader_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }
    header.flags |= COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = CookedResource_ComputeContentHash( {
        output.pData,
        result.cbRequired
    } );
    layout = CookedResource_WriteLayout(
        header,
        { chunks, CY_COOKED_SHADER_V3_CHUNK_COUNT },
        output );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_shader_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }
    result.cbWritten = result.cbRequired;
    return result;
}

cooked_shader_result_t CookedShader_Read(
    binary_block_t input,
    cooked_shader_view_t *pShaderOut ) noexcept
{
    cooked_shader_result_t result{};
    if ( !BinaryBlock_IsValid( input ) || pShaderOut == nullptr ||
         Cy_MemRangesOverlap(
             input.pData,
             input.cbSize,
             pShaderOut,
             sizeof( *pShaderOut ) ) ) {
        result.status = cooked_shader_status_t::INVALID_ARGUMENT;
        return result;
    }

    // The outer CYRS pass validates bounds and file hash before shader decoding.
    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[CY_COOKED_SHADER_V3_CHUNK_COUNT]{};
    const span_t<cooked_chunk_desc_t> chunkStorage{
        chunks,
        CY_COOKED_SHADER_V3_CHUNK_COUNT
    };
    const cooked_resource_result_t layout = CookedResource_ReadLayout(
        input,
        &header,
        chunkStorage );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_shader_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }
    if ( header.resourceType != CY_RENDER_SHADER_RESOURCE_TYPE ) {
        result.status = cooked_shader_status_t::INVALID_RESOURCE_TYPE;
        return result;
    }
    const bool_t bVersion2 =
        header.nResourceVersion == CY_COOKED_SHADER_RESOURCE_VERSION_V2;
    const bool_t bVersion3 =
        header.nResourceVersion == CY_COOKED_SHADER_RESOURCE_VERSION_V3;
    if ( !bVersion2 && !bVersion3 ) {
        result.status = cooked_shader_status_t::VERSION_MISMATCH;
        return result;
    }
    if ( ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) == 0u ) {
        result.status = cooked_shader_status_t::INVALID_FLAGS;
        return result;
    }
    const u32 iFirstCodeChunk = bVersion3
        ? CY_COOKED_SHADER_V3_FIRST_CODE_CHUNK
        : 1u;
    const u32 nExpectedChunks = iFirstCodeChunk +
        CY_COOKED_SHADER_MAX_STAGES;
    if ( header.nChunks != nExpectedChunks ) {
        result.status = cooked_shader_status_t::INVALID_CHUNK_COUNT;
        return result;
    }

    // Both supported versions keep program metadata in chunk zero.
    const cooked_chunk_desc_t &metadataChunk = chunks[0];
    if ( metadataChunk.chunkType != CY_COOKED_SHADER_METADATA_CHUNK ||
         metadataChunk.codec != cooked_chunk_codec_t::NONE ||
         metadataChunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
         metadataChunk.nAlignment != CY_COOKED_SHADER_METADATA_ALIGNMENT ) {
        result.status = cooked_shader_status_t::INVALID_METADATA_CHUNK;
        result.iChunk = 0u;
        return result;
    }
    const binary_block_t metadataBytes{
        input.pData + metadataChunk.iOffset,
        static_cast<usize>( metadataChunk.cbStored )
    };
    if ( !ContentHash_Equals(
             ContentHash_Data( metadataBytes ),
             metadataChunk.contentHash ) ) {
        result.status = cooked_shader_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = 0u;
        return result;
    }

    byte_reader_t reader{};
    cooked_shader_metadata_t metadata{};
    if ( !ByteReader_Init(
             &reader,
             metadataBytes,
             data_byte_order_t::LITTLE ) ||
         !( bVersion3
                ? ReadMetadataHeaderV3( reader, metadata )
                : ReadMetadataHeader( reader, metadata ) ) ) {
        result.status = cooked_shader_status_t::INVALID_METADATA;
        return result;
    }
    result.status = ValidateShader( metadata.shader );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }
    if ( metadata.nStages == 0u ||
         metadata.nStages > CY_COOKED_SHADER_MAX_STAGES ||
         metadata.nStages + iFirstCodeChunk != header.nChunks ||
         metadataBytes.cbSize !=
             ( bVersion3
                   ? CookedShader_MetadataSizeV3( metadata.nStages )
                   : CookedShader_MetadataSize( metadata.nStages ) ) ) {
        result.status = cooked_shader_status_t::INVALID_CHUNK_COUNT;
        return result;
    }
    if ( bVersion3 &&
         metadata.nBindings > CY_COOKED_SHADER_MAX_BINDINGS ) {
        result.status = cooked_shader_status_t::BINDING_LIMIT_EXCEEDED;
        return result;
    }

    cooked_shader_stage_desc_t
        stageDescs[CY_COOKED_SHADER_MAX_STAGES]{};
    for ( usize iStage = 0u; iStage < metadata.nStages; ++iStage ) {
        if ( !ReadStageDescriptor( reader, stageDescs[iStage] ) ) {
            result.status = cooked_shader_status_t::INVALID_METADATA;
            result.iStage = iStage;
            return result;
        }
    }
    result.status = ValidateStageDescriptors(
        metadata.shader,
        { stageDescs, metadata.nStages },
        iFirstCodeChunk,
        &result.iStage );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }

    // Assemble a borrowed view locally and publish it only after all stages pass.
    cooked_shader_view_t shader{};
    shader.nResourceVersion = header.nResourceVersion;
    shader.backend = metadata.shader.backend;
    shader.kind = metadata.shader.kind;
    shader.languageProfile = metadata.shader.languageProfile;
    shader.nLanguageVersion = metadata.shader.nLanguageVersion;
    shader.flags = metadata.shader.flags;
    shader.sourceHash = header.sourceHash;
    shader.interfaceHash = metadata.interfaceHash;
    shader.nStages = metadata.nStages;
    usize iPayloadEnd = CookedResource_PrefixSize( header.nChunks );
    usize iExpectedOffset = iPayloadEnd;
    if ( !Cy_AlignUpChecked(
             iExpectedOffset,
             CY_COOKED_SHADER_METADATA_ALIGNMENT,
             iExpectedOffset ) ||
         metadataChunk.iOffset != iExpectedOffset ||
         !IsZeroRange( input, iPayloadEnd, iExpectedOffset ) ||
         !CheckedAdd(
             iExpectedOffset,
             static_cast<usize>( metadataChunk.cbStored ),
             iPayloadEnd ) ) {
        result.status = cooked_shader_status_t::NON_CANONICAL_LAYOUT;
        result.iChunk = 0u;
        return result;
    }
    if ( bVersion3 ) {
        const cooked_chunk_desc_t &reflectionChunk = chunks[1];
        iExpectedOffset = iPayloadEnd;
        if ( !Cy_AlignUpChecked(
                 iExpectedOffset,
                 CY_COOKED_SHADER_REFLECTION_ALIGNMENT,
                 iExpectedOffset ) ||
             reflectionChunk.iOffset != iExpectedOffset ||
             !IsZeroRange( input, iPayloadEnd, iExpectedOffset ) ||
             !CheckedAdd(
                 iExpectedOffset,
                 static_cast<usize>( reflectionChunk.cbStored ),
                 iPayloadEnd ) ) {
            result.status = cooked_shader_status_t::NON_CANONICAL_LAYOUT;
            result.iChunk = 1u;
            return result;
        }
        if ( reflectionChunk.chunkType !=
                 CY_COOKED_SHADER_REFLECTION_CHUNK ||
             reflectionChunk.codec != cooked_chunk_codec_t::NONE ||
             reflectionChunk.flags !=
                 COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
             reflectionChunk.nAlignment !=
                 CY_COOKED_SHADER_REFLECTION_ALIGNMENT ||
             reflectionChunk.cbStored != CookedShader_ReflectionSize(
                 metadata.nBindings ) ||
             reflectionChunk.cbDecoded != reflectionChunk.cbStored ) {
            result.status = cooked_shader_status_t::INVALID_REFLECTION_CHUNK;
            result.iChunk = 1u;
            return result;
        }
        const binary_block_t reflectionBytes{
            input.pData + reflectionChunk.iOffset,
            static_cast<usize>( reflectionChunk.cbStored )
        };
        if ( !ContentHash_Equals(
                 ContentHash_Data( reflectionBytes ),
                 reflectionChunk.contentHash ) ) {
            result.status = cooked_shader_status_t::CONTENT_HASH_MISMATCH;
            result.iChunk = 1u;
            return result;
        }

        const cooked_chunk_desc_t &stringChunk = chunks[2];
        iExpectedOffset = iPayloadEnd;
        if ( !Cy_AlignUpChecked(
                 iExpectedOffset,
                 CY_COOKED_SHADER_STRING_ALIGNMENT,
                 iExpectedOffset ) ||
             stringChunk.iOffset != iExpectedOffset ||
             !IsZeroRange( input, iPayloadEnd, iExpectedOffset ) ||
             !CheckedAdd(
                 iExpectedOffset,
                 static_cast<usize>( stringChunk.cbStored ),
                 iPayloadEnd ) ) {
            result.status = cooked_shader_status_t::NON_CANONICAL_LAYOUT;
            result.iChunk = 2u;
            return result;
        }
        if ( stringChunk.chunkType != CY_COOKED_SHADER_STRING_CHUNK ||
             stringChunk.codec != cooked_chunk_codec_t::NONE ||
             stringChunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
             stringChunk.nAlignment != CY_COOKED_SHADER_STRING_ALIGNMENT ||
             stringChunk.cbStored != metadata.cbStringTable ||
             stringChunk.cbDecoded != stringChunk.cbStored ) {
            result.status = cooked_shader_status_t::INVALID_STRING_CHUNK;
            result.iChunk = 2u;
            return result;
        }
        const binary_block_t stringBytes{
            input.pData + stringChunk.iOffset,
            static_cast<usize>( stringChunk.cbStored )
        };
        if ( !ContentHash_Equals(
                 ContentHash_Data( stringBytes ),
                 stringChunk.contentHash ) ) {
            result.status = cooked_shader_status_t::CONTENT_HASH_MISMATCH;
            result.iChunk = 2u;
            return result;
        }
        const content_hash_t interfaceHash = ContentHash_Combine(
            reflectionChunk.contentHash,
            stringChunk.contentHash );
        if ( !ContentHash_Equals(
                 interfaceHash,
                 metadata.interfaceHash ) ) {
            result.status = cooked_shader_status_t::INVALID_INTERFACE_HASH;
            return result;
        }

        byte_reader_t reflectionReader{};
        u32 nReflectedBindings = 0u;
        u32 cbReflectedStrings = 0u;
        if ( !ByteReader_Init(
                 &reflectionReader,
                 reflectionBytes,
                 data_byte_order_t::LITTLE ) ||
             !ReadReflectionHeader(
                 reflectionReader,
                 nReflectedBindings,
                 cbReflectedStrings ) ||
             nReflectedBindings != metadata.nBindings ||
             cbReflectedStrings != metadata.cbStringTable ||
             stringBytes.pData[0] != static_cast<byte>( '\0' ) ) {
            result.status = cooked_shader_status_t::INVALID_REFLECTION_CHUNK;
            result.iChunk = 1u;
            return result;
        }

        cooked_shader_binding_record_t
            records[CY_COOKED_SHADER_MAX_BINDINGS]{};
        u32 iExpectedName = 1u;
        usize iExpectedMaterialOffset = 0u;
        for ( usize iBinding = 0u;
              iBinding < metadata.nBindings;
              ++iBinding ) {
            cooked_shader_binding_record_t &record = records[iBinding];
            if ( !ReadBindingRecord( reflectionReader, record ) ||
                 record.iName != iExpectedName || record.cchName == 0u ||
                 record.cchName >
                     CY_COOKED_SHADER_MAX_BINDING_NAME_LENGTH ||
                 record.iName >= stringBytes.cbSize ||
                 record.cchName >= stringBytes.cbSize - record.iName ||
                 stringBytes.pData[
                     record.iName + record.cchName] !=
                     static_cast<byte>( '\0' ) ) {
                result.status = cooked_shader_status_t::INVALID_BINDING;
                result.iBinding = iBinding;
                return result;
            }
            record.binding.name = {
                reinterpret_cast<const char *>(
                    stringBytes.pData + record.iName ),
                record.cchName
            };
            const cooked_shader_binding_source_t source{
                record.binding.name,
                record.binding.kind,
                record.binding.valueType,
                record.binding.resourceType,
                record.binding.nArrayElements,
                record.binding.stageMask,
                record.binding.flags,
                record.binding.nLogicalBinding,
                record.binding.iByteOffset,
                record.binding.cbByteSize
            };
            if ( ValidateBinding( source ) != cooked_shader_status_t::OK ) {
                result.status = cooked_shader_status_t::INVALID_BINDING;
                result.iBinding = iBinding;
                return result;
            }
            if ( source.kind == render_shader_binding_kind_t::VALUE &&
                 ( source.flags & COOKED_SHADER_BINDING_FLAG_MATERIAL ) !=
                     0u ) {
                usize iAlignedOffset = 0u;
                if ( !Cy_AlignUpChecked(
                         iExpectedMaterialOffset,
                         CookedShader_ValueTypeStorageAlignment(
                             source.valueType ),
                         iAlignedOffset ) ||
                     source.iByteOffset != iAlignedOffset ) {
                    result.status = cooked_shader_status_t::INVALID_BINDING;
                    result.iBinding = iBinding;
                    return result;
                }
                iExpectedMaterialOffset =
                    iAlignedOffset + source.cbByteSize;
            }
            if ( iBinding > 0u ) {
                const i32 nameOrder = StringView_Compare(
                    records[iBinding - 1u].binding.name,
                    record.binding.name );
                if ( nameOrder == 0 ) {
                    result.status =
                        cooked_shader_status_t::DUPLICATE_BINDING_NAME;
                    result.iBinding = iBinding;
                    return result;
                }
                if ( nameOrder > 0 ) {
                    result.status =
                        cooked_shader_status_t::NON_CANONICAL_LAYOUT;
                    result.iBinding = iBinding;
                    return result;
                }
            }
            for ( usize iPrior = 0u; iPrior < iBinding; ++iPrior ) {
                if ( records[iPrior].binding.nLogicalBinding ==
                     record.binding.nLogicalBinding ) {
                    result.status =
                        cooked_shader_status_t::DUPLICATE_BINDING_ID;
                    result.iBinding = iBinding;
                    return result;
                }
            }
            iExpectedName += record.cchName + 1u;
            shader.bindings[iBinding] = record.binding;
        }
        if ( ByteReader_Remaining( &reflectionReader ) != 0u ||
             iExpectedName != stringBytes.cbSize ) {
            result.status = cooked_shader_status_t::NON_CANONICAL_LAYOUT;
            return result;
        }
        shader.nBindings = metadata.nBindings;
    }

    // Enforce exact order, alignment, zero padding, hash, NUL, and UTF-8 validity.
    for ( usize iStage = 0u; iStage < metadata.nStages; ++iStage ) {
        const cooked_shader_stage_desc_t &stage = stageDescs[iStage];
        if ( stage.iCodeChunk != iStage + iFirstCodeChunk ||
             stage.iCodeChunk >= header.nChunks ) {
            result.status = cooked_shader_status_t::NON_CANONICAL_LAYOUT;
            result.iStage = iStage;
            result.iChunk = stage.iCodeChunk;
            return result;
        }

        const cooked_chunk_desc_t &chunk = chunks[stage.iCodeChunk];
        iExpectedOffset = iPayloadEnd;
        if ( !Cy_AlignUpChecked(
                 iExpectedOffset,
                 CY_COOKED_SHADER_CODE_ALIGNMENT,
                 iExpectedOffset ) ||
             chunk.iOffset != iExpectedOffset ||
             !IsZeroRange( input, iPayloadEnd, iExpectedOffset ) ||
             !CheckedAdd(
                 iExpectedOffset,
                 static_cast<usize>( chunk.cbStored ),
                 iPayloadEnd ) ) {
            result.status = cooked_shader_status_t::NON_CANONICAL_LAYOUT;
            result.iStage = iStage;
            result.iChunk = stage.iCodeChunk;
            return result;
        }
        if ( chunk.chunkType != CY_COOKED_SHADER_CODE_CHUNK ||
             chunk.codec != cooked_chunk_codec_t::NONE ||
             chunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
             chunk.nAlignment != CY_COOKED_SHADER_CODE_ALIGNMENT ||
             chunk.cbDecoded != stage.cbCode ) {
            result.status = cooked_shader_status_t::INVALID_CODE_CHUNK;
            result.iStage = iStage;
            result.iChunk = stage.iCodeChunk;
            return result;
        }
        const binary_block_t code{
            input.pData + chunk.iOffset,
            static_cast<usize>( chunk.cbStored )
        };
        if ( !ContentHash_Equals(
                 ContentHash_Data( code ),
                 chunk.contentHash ) ) {
            result.status = cooked_shader_status_t::CONTENT_HASH_MISMATCH;
            result.iStage = iStage;
            result.iChunk = stage.iCodeChunk;
            return result;
        }
        result.status = ValidateCode( stage, code );
        if ( result.status != cooked_shader_status_t::OK ) {
            result.iStage = iStage;
            result.iChunk = stage.iCodeChunk;
            return result;
        }
        shader.stages[iStage] = {
            stage.stage,
            stage.codeFormat,
            stage.flags,
            code,
            chunk.contentHash
        };
    }
    if ( iPayloadEnd != input.cbSize ) {
        result.status = cooked_shader_status_t::NON_CANONICAL_LAYOUT;
        return result;
    }

    *pShaderOut = shader;
    result.cbRead = input.cbSize;
    return result;
}

const cooked_shader_stage_view_t *CookedShader_FindStage(
    const cooked_shader_view_t &shader,
    render_shader_stage_t stage ) noexcept
{
    if ( shader.nStages > CY_COOKED_SHADER_MAX_STAGES ) {
        return nullptr;
    }
    for ( usize iStage = 0u; iStage < shader.nStages; ++iStage ) {
        if ( shader.stages[iStage].stage == stage ) {
            return &shader.stages[iStage];
        }
    }
    return nullptr;
}

const cooked_shader_binding_view_t *CookedShader_FindBinding(
    const cooked_shader_view_t &shader,
    string_view_t name ) noexcept
{
    if ( shader.nBindings > CY_COOKED_SHADER_MAX_BINDINGS ||
         !StringView_IsValid( name ) ) {
        return nullptr;
    }
    usize iFirst = 0u;
    usize iEnd = shader.nBindings;
    while ( iFirst < iEnd ) {
        const usize iMiddle = iFirst + ( iEnd - iFirst ) / 2u;
        const i32 order = StringView_Compare(
            shader.bindings[iMiddle].name,
            name );
        if ( order < 0 ) {
            iFirst = iMiddle + 1u;
        } else {
            iEnd = iMiddle;
        }
    }
    return iFirst < shader.nBindings &&
           StringView_Equals( shader.bindings[iFirst].name, name )
        ? &shader.bindings[iFirst]
        : nullptr;
}

const cooked_shader_binding_view_t *CookedShader_FindBindingById(
    const cooked_shader_view_t &shader,
    u64 nLogicalBinding ) noexcept
{
    if ( shader.nBindings > CY_COOKED_SHADER_MAX_BINDINGS ) {
        return nullptr;
    }
    for ( usize iBinding = 0u;
          iBinding < shader.nBindings;
          ++iBinding ) {
        if ( shader.bindings[iBinding].nLogicalBinding == nLogicalBinding ) {
            return &shader.bindings[iBinding];
        }
    }
    return nullptr;
}

bool_t CookedShader_Succeeded(
    const cooked_shader_result_t &result ) noexcept
{
    return result.status == cooked_shader_status_t::OK;
}

bool_t CookedShader_SupportsLanguage(
    render_shader_language_profile_t profile,
    u32 nVersion ) noexcept
{
    return IsLanguageProfileValid( profile ) &&
           profile == render_shader_language_profile_t::GLSL_CORE &&
           IsGlslCoreVersionValid( nVersion );
}

const char *CookedShader_StatusName(
    cooked_shader_status_t status ) noexcept
{
    switch ( status ) {
        case cooked_shader_status_t::OK: return "OK";
        case cooked_shader_status_t::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case cooked_shader_status_t::OUTPUT_TOO_SMALL:
            return "OUTPUT_TOO_SMALL";
        case cooked_shader_status_t::RESOURCE_ERROR:
            return "RESOURCE_ERROR";
        case cooked_shader_status_t::INVALID_RESOURCE_TYPE:
            return "INVALID_RESOURCE_TYPE";
        case cooked_shader_status_t::VERSION_MISMATCH:
            return "VERSION_MISMATCH";
        case cooked_shader_status_t::INVALID_CHUNK_COUNT:
            return "INVALID_CHUNK_COUNT";
        case cooked_shader_status_t::INVALID_METADATA_CHUNK:
            return "INVALID_METADATA_CHUNK";
        case cooked_shader_status_t::INVALID_REFLECTION_CHUNK:
            return "INVALID_REFLECTION_CHUNK";
        case cooked_shader_status_t::INVALID_STRING_CHUNK:
            return "INVALID_STRING_CHUNK";
        case cooked_shader_status_t::INVALID_METADATA:
            return "INVALID_METADATA";
        case cooked_shader_status_t::INVALID_BACKEND:
            return "INVALID_BACKEND";
        case cooked_shader_status_t::INVALID_PROGRAM_KIND:
            return "INVALID_PROGRAM_KIND";
        case cooked_shader_status_t::INVALID_LANGUAGE_PROFILE:
            return "INVALID_LANGUAGE_PROFILE";
        case cooked_shader_status_t::INVALID_LANGUAGE_VERSION:
            return "INVALID_LANGUAGE_VERSION";
        case cooked_shader_status_t::INVALID_FLAGS:
            return "INVALID_FLAGS";
        case cooked_shader_status_t::STAGE_LIMIT_EXCEEDED:
            return "STAGE_LIMIT_EXCEEDED";
        case cooked_shader_status_t::INVALID_STAGE:
            return "INVALID_STAGE";
        case cooked_shader_status_t::DUPLICATE_STAGE:
            return "DUPLICATE_STAGE";
        case cooked_shader_status_t::INVALID_STAGE_SET:
            return "INVALID_STAGE_SET";
        case cooked_shader_status_t::INVALID_CODE_CHUNK:
            return "INVALID_CODE_CHUNK";
        case cooked_shader_status_t::INVALID_CODE:
            return "INVALID_CODE";
        case cooked_shader_status_t::BINDING_LIMIT_EXCEEDED:
            return "BINDING_LIMIT_EXCEEDED";
        case cooked_shader_status_t::INVALID_BINDING:
            return "INVALID_BINDING";
        case cooked_shader_status_t::DUPLICATE_BINDING_NAME:
            return "DUPLICATE_BINDING_NAME";
        case cooked_shader_status_t::DUPLICATE_BINDING_ID:
            return "DUPLICATE_BINDING_ID";
        case cooked_shader_status_t::INVALID_INTERFACE_HASH:
            return "INVALID_INTERFACE_HASH";
        case cooked_shader_status_t::CONTENT_HASH_MISMATCH:
            return "CONTENT_HASH_MISMATCH";
        case cooked_shader_status_t::NON_CANONICAL_LAYOUT:
            return "NON_CANONICAL_LAYOUT";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
