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

struct cooked_shader_metadata_t {
    cooked_shader_desc_t shader{}; // Fixed SHMD program header.
    u32 nStages{ 0u };             // Number of following stage records.
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
        if ( stage.iCodeChunk != iStage + 1u ) {
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

} // namespace

usize CookedShader_MetadataSize( u32 nStages ) noexcept
{
    if ( nStages == 0u || nStages > CY_COOKED_SHADER_MAX_STAGES ) {
        return 0u;
    }
    return CY_COOKED_SHADER_METADATA_HEADER_SIZE +
           static_cast<usize>( nStages ) *
               CY_COOKED_SHADER_STAGE_RECORD_SIZE;
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
    if ( ValidateStageDescriptors( shader, descriptors, nullptr ) !=
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
    header.nResourceVersion = CY_RENDER_SHADER_RESOURCE_VERSION;
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
    cooked_chunk_desc_t chunks[CY_COOKED_SHADER_MAX_STAGES + 1u]{};
    const span_t<cooked_chunk_desc_t> chunkStorage{
        chunks,
        CY_COOKED_SHADER_MAX_STAGES + 1u
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
    if ( header.nResourceVersion != CY_RENDER_SHADER_RESOURCE_VERSION ) {
        result.status = cooked_shader_status_t::VERSION_MISMATCH;
        return result;
    }
    if ( ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) == 0u ) {
        result.status = cooked_shader_status_t::INVALID_FLAGS;
        return result;
    }
    if ( header.nChunks < 2u ||
         header.nChunks > CY_COOKED_SHADER_MAX_STAGES + 1u ) {
        result.status = cooked_shader_status_t::INVALID_CHUNK_COUNT;
        return result;
    }

    // Canonical V2 shader metadata always occupies the first chunk.
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
         !ReadMetadataHeader( reader, metadata ) ) {
        result.status = cooked_shader_status_t::INVALID_METADATA;
        return result;
    }
    result.status = ValidateShader( metadata.shader );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }
    if ( metadata.nStages == 0u ||
         metadata.nStages > CY_COOKED_SHADER_MAX_STAGES ||
         metadata.nStages + 1u != header.nChunks ||
         metadataBytes.cbSize != CookedShader_MetadataSize(
             metadata.nStages ) ) {
        result.status = cooked_shader_status_t::INVALID_CHUNK_COUNT;
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
        &result.iStage );
    if ( result.status != cooked_shader_status_t::OK ) {
        return result;
    }

    // Assemble a borrowed view locally and publish it only after all stages pass.
    cooked_shader_view_t shader{};
    shader.backend = metadata.shader.backend;
    shader.kind = metadata.shader.kind;
    shader.languageProfile = metadata.shader.languageProfile;
    shader.nLanguageVersion = metadata.shader.nLanguageVersion;
    shader.flags = metadata.shader.flags;
    shader.sourceHash = header.sourceHash;
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
    // Enforce exact order, alignment, zero padding, hash, NUL, and UTF-8 validity.
    for ( usize iStage = 0u; iStage < metadata.nStages; ++iStage ) {
        const cooked_shader_stage_desc_t &stage = stageDescs[iStage];
        if ( stage.iCodeChunk != iStage + 1u ||
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
        case cooked_shader_status_t::CONTENT_HASH_MISMATCH:
            return "CONTENT_HASH_MISMATCH";
        case cooked_shader_status_t::NON_CANONICAL_LAYOUT:
            return "NON_CANONICAL_LAYOUT";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
